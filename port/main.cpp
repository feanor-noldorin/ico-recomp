/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#include <io.h>         /* _dup2/_fileno: ICO_LOG_FILE */
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* libs/system/cellGame.c: title id/version from PARAM.SFO. Without it every
 * title-id path uses the BLES00000 placeholder -- and ICO picks its region
 * from the title id, so it fell back to EUROPE on the US disc. */
extern "C" void cellGame_init_from_paramsfo(const char* sfo_path);

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);

/* Diagnostics the runtime already keeps (ppu_hle.cpp, ppu_loader.cpp). */
extern const char* g_hle_inflight[];          /* 64 entries, by guest tid */
extern uint32_t    g_last_hle_nid;
extern const char* g_last_hle_name;
void ppu_report_guest_lrs(void);
void ico_dump_lfqueues(void);                 /* stubs.cpp */
void ico_dump_fibers(void);                   /* stubs.cpp */
void ico_prof_start(unsigned every_sec);      /* stubs.cpp: ICO_PROF sampler */

/* libs/video/cellGcmSys.c: delivers the guest's queued vblank, flip and user
 * command handlers on the calling guest thread. */
void ppu_gcm_pump(void);
}

/* The lv2 dispatch table (runtime/syscalls/lv2_register.c). Declared by shape
 * rather than through lv2_syscall_table.h, whose ppu_context comes from a
 * different header than the lifter's. */
struct ico_lv2_table { int64_t (*handlers[1024])(ppu_context*); };
extern "C" ico_lv2_table g_lv2_syscalls;

static volatile LONG s_calls_flip, s_calls_vblank, s_calls_user;

static const uint32_t ICO_USER_HANDLER_OPD = 0x004B4990u;

/* sys_timer_usleep (141) with the GCM pump in front.
 *
 * The runtime runs ppu_gcm_pump() from ps3_hle_call, i.e. whenever the guest
 * calls a firmware import. ICO's main thread waits in raw `sc` loops -- the
 * per-frame SPU job wait (func_00341F9C: while (!sync[0x2C]) usleep(30)) and
 * the frames-in-flight throttle (func_0016FA1C) -- that never reach an import,
 * so without this nothing queued for it was delivered while it waited, and it
 * spun after two flips. */
static int64_t (*s_orig_usleep)(ppu_context*) = nullptr;
/* ICO_FC_WAKE=1: the frame clock waits on this event (4 ms timeout) instead
 * of Sleep(4), and a PPU usleep sets it -- a guest that is polling is waiting
 * on something the drain may produce (a label, a fence), so drain now rather
 * than at the clock's next ~6.5 ms turn. Experiment for the slowdown. */
static HANDLE s_fc_wake_ev = NULL;
/* The runtime calls this when a flip releases the prepare marker the FIFO walker
 * is holding at (GCM_PREPAREFLIP_QUEUE). The frame clock drains only between its
 * sleeps, so it is woken at once: on ps3recomp v0.18 the walker reaches the next
 * frame's marker before the flip, and waiting out the rest of a 4 ms sleep there
 * made frames miss their vblank (gameplay ~40 fps instead of 60). */
extern "C" void (*g_gcm_prepare_release_hook)(void);
static void fc_on_prepare_release(void) { if (s_fc_wake_ev) SetEvent(s_fc_wake_ev); }
static int    s_skip_mlaa_ppu = -1;   /* ICO_SKIP_MLAA, read once in main() */
static int    s_fc_wake_on = 0;
/* Who sleeps: the guest caller (lr) of each usleep and its argument, so the
 * watchdog can name a polling loop. Approximate counts are enough. */
static volatile uint32_t s_us_lr[16], s_us_arg[16];
static volatile long     s_us_n[16];
/* QPC ticks spent inside the wrapper (pump + sleep) per caller, total and
 * since the last ICO_PROF report: how much of each frame the PPU waits. */
static volatile LONG64   s_us_ticks[16], s_us_ticks_rep[16];
/* MLAA job timeline (lr 0x461EC = func_00341F9C polling the job's done word).
 * An episode is a run of those polls with gaps under 2 ms. Per episode:
 * its length, when RSX label 0x45 (the job's dependency) last changed, and
 * when the user handler (F5AAC0DE, which signals the job's tasks) last ran. */
static volatile LONG64 s_t_user;
static double   s_q_sum; static long s_q_n; static uint32_t s_q_max;
static double   s_sig2lbl_sum; static long s_sig2lbl_n;
static LONG64 s_ep_start, s_ep_last, s_lbl_t, s_lbl_v;
static double s_ep_sum, s_ep_lbl_sum, s_ep_usr_sum, s_ep_tail_sum;
static long   s_ep_n, s_ep_lbl_n, s_ep_usr_n;
static void ico_timeline_poll(uint32_t lr, LONG64 now)
{
    if (lr != 0x000461ECu) return;
    { /* Render-job queue 0x40056000 (depth 2048, counters mod 4096): what the
       * SPU renderer still has to consume while the PPU waits. */
      const uint32_t q = 0x40056000u;
      const uint32_t h1 = vm_read32(q + 0x0) >> 16, h5 = vm_read32(q + 0x8) >> 16;
      const uint32_t backlog = (h5 + 4096u - h1) % 4096u;
      s_q_sum += backlog; s_q_n++; if (backlog > s_q_max) s_q_max = backlog; }
    const uint32_t lbl = vm_read32(0x20000450u);
    if ((LONG64)lbl != s_lbl_v) {
        s_lbl_v = lbl; s_lbl_t = now;
        /* Signal-to-label: from the user handler that signals the MLAA tasks
         * to the label change that ends the PPU's wait. */
        const LONG64 tu = s_t_user;
        if (tu && now > tu) { LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
            s_sig2lbl_sum += 1000.0 * (double)(now - tu) / (double)fq.QuadPart; s_sig2lbl_n++; }
    }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    const LONG64 gap = (LONG64)(f.QuadPart / 500);           /* 2 ms */
    if (s_ep_last && now - s_ep_last > gap) {                /* previous episode ended */
        const double ms = 1000.0 / (double)f.QuadPart;
        s_ep_sum += (double)(s_ep_last - s_ep_start) * ms; s_ep_n++;
        if (s_lbl_t >= s_ep_start && s_lbl_t <= s_ep_last) {
            s_ep_lbl_sum += (double)(s_lbl_t - s_ep_start) * ms;
            s_ep_tail_sum += (double)(s_ep_last - s_lbl_t) * ms; s_ep_lbl_n++;
        }
        const LONG64 tu = s_t_user;
        if (tu >= s_ep_start && tu <= s_ep_last) { s_ep_usr_sum += (double)(tu - s_ep_start) * ms; s_ep_usr_n++; }
        s_ep_start = now;
    }
    if (!s_ep_last) s_ep_start = now;
    s_ep_last = now;
}
static int64_t ico_usleep_with_pump(ppu_context* ctx)
{
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    unsigned i = 0;
    { const uint32_t lr = (uint32_t)ctx->lr;
      for (; i < 16; i++) {
          if (s_us_lr[i] == lr) break;
          if (!s_us_lr[i]) { s_us_lr[i] = lr; s_us_arg[i] = (uint32_t)ctx->gpr[3]; break; }
      }
      if (i < 16) InterlockedIncrement(&s_us_n[i]); }
    ico_timeline_poll((uint32_t)ctx->lr, t0.QuadPart);
    /* ICO_SKIP_MLAA: complete the job the PPU is waiting on, here, rather than
     * when its user command fires (stubs.cpp). func_00341F9C polls [r31+0x2C]
     * with this usleep; completing from the handler raced the PPU queueing the
     * next job (it could mark the wrong one, and the PPU then waited on a job
     * whose trigger had already gone by -- a stall at ~flip 2183). */
    if (s_skip_mlaa_ppu && (uint32_t)ctx->lr == 0x000461ECu) {
        const uint32_t obj = (uint32_t)ctx->gpr[31];
        const uint32_t desc = obj ? vm_read32(obj + 0x14) : 0;
        if (desc && vm_read32(obj + 0x2C) == 0) {
            const uint32_t seq = vm_read32(desc), lbl = vm_read32(desc + 4);
            if (lbl) vm_write32(lbl, seq);
            vm_write32(obj + 0x2C, 1);
        }
    }
    if (s_fc_wake_on) SetEvent(s_fc_wake_ev);
    ppu_gcm_pump();
    const int64_t rc = s_orig_usleep(ctx);
    QueryPerformanceCounter(&t1);
    if (i < 16) {
        InterlockedAdd64(&s_us_ticks[i], t1.QuadPart - t0.QuadPart);
        InterlockedAdd64(&s_us_ticks_rep[i], t1.QuadPart - t0.QuadPart);
    }
    return rc;
}
/* For the ICO_PROF report: ms per second spent in usleep, by caller, over the
 * last interval (and resets the interval counters). */
extern "C" void ico_frameclock_report(double secs);   /* below, by frame_clock */
extern "C" void ico_usleep_report(double secs)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    fprintf(stderr, "[ICO-PROF]   PPU usleep time by caller (ms/s):");
    for (unsigned i = 0; i < 16 && s_us_lr[i]; i++) {
        const LONG64 t = InterlockedExchange64(&s_us_ticks_rep[i], 0);
        const double ms = 1000.0 * (double)t / (double)f.QuadPart / secs;
        if (ms >= 1.0) fprintf(stderr, " lr=0x%X:%.0f", s_us_lr[i], ms);
    }
    fprintf(stderr, "\n");
    ico_frameclock_report(secs);
    if (s_ep_n)
        fprintf(stderr, "[ICO-PROF]   MLAA wait episodes: %ld, avg %.1f ms | label 0x45 changed at +%.1f ms (%ld eps), done %.1f ms after it"
                        " | user handler at +%.1f ms (%ld eps)\n",
                s_ep_n, s_ep_sum / s_ep_n,
                s_ep_lbl_n ? s_ep_lbl_sum / s_ep_lbl_n : -1.0, s_ep_lbl_n,
                s_ep_lbl_n ? s_ep_tail_sum / s_ep_lbl_n : -1.0,
                s_ep_usr_n ? s_ep_usr_sum / s_ep_usr_n : -1.0, s_ep_usr_n);
    if (s_q_n)
        fprintf(stderr, "[ICO-PROF]   render queue 0x40056000 backlog during MLAA waits: avg %.1f max %u (%ld polls)\n",
                s_q_sum / s_q_n, s_q_max, s_q_n);
    if (s_sig2lbl_n)
        fprintf(stderr, "[ICO-PROF]   user handler -> label 0x45 change: avg %.1f ms (%ld)\n",
                s_sig2lbl_sum / s_sig2lbl_n, s_sig2lbl_n);
    s_sig2lbl_sum = 0; s_sig2lbl_n = 0;
    s_q_sum = 0; s_q_n = 0; s_q_max = 0;
    s_ep_sum = s_ep_lbl_sum = s_ep_usr_sum = s_ep_tail_sum = 0; s_ep_n = s_ep_lbl_n = s_ep_usr_n = 0;
}
/* Read-only census of the GCM command ring (0x40100000..0x40300000, the
 * title's 2 MB buffer): walk it as NV4097 method headers and count the
 * primitives (SET_BEGIN_END 0x1808), indexed/array draws (0x1824/0x1814), the
 * index type (SET_INDEX_ARRAY_DMA 0x1820, bit 4 = u16) and primitive restart
 * (0x1DAC enable / 0x1DB0 index), which the D3D12 path does not implement.
 * The ring holds the last ~1000 frames' worth of commands, so this is a
 * sample of what the title emits, not of what the drain decoded. */
static void ico_ring_census(void)
{
    const uint32_t begin = 0x40100000u, end = 0x40300000u;
    unsigned prim[16] = {0}, nidx = 0, narr = 0, idx16 = 0, idx32 = 0, rst_on = 0, rst_off = 0;
    uint32_t rst_val = 0; unsigned rst_vals = 0, inline_w = 0;
    for (uint32_t a = begin; a + 8 <= end; ) {
        const uint32_t h = vm_read32(a);
        if (h == 0 || (h & 0xE0000003u) != 0) { a += 4; continue; }   /* not an increasing/non-inc header */
        const uint32_t cnt = (h >> 18) & 0x7FFu, m = h & 0x1FFCu, sub = (h >> 13) & 7u;
        if (!cnt || cnt > 2047 || a + 4 + cnt * 4 > end) { a += 4; continue; }
        if (sub == 0) {
            const uint32_t d = vm_read32(a + 4);
            if (m == 0x1808 && d && d < 16) prim[d]++;
            else if (m == 0x1824) nidx += cnt;
            else if (m == 0x1814) narr += cnt;
            else if (m == 0x1818) inline_w += cnt;
            else if (m == 0x1820) { if (d & 0x10u) idx16++; else idx32++; }
            else if (m == 0x1DAC) { if (d) rst_on++; else rst_off++; }
            else if (m == 0x1DB0) { rst_val = d; rst_vals++; }
        }
        a += 4 + cnt * 4;
    }
    fprintf(stderr, "[ICO-WD]   ring census: prims");
    static const char* pn[16] = {"?", "POINTS", "LINES", "LINE_LOOP", "LINE_STRIP", "TRIS", "TRI_STRIP",
                                 "TRI_FAN", "QUADS", "QUAD_STRIP", "POLYGON", "?", "?", "?", "?", "?"};
    for (int i = 1; i < 16; i++) if (prim[i]) fprintf(stderr, " %s=%u", pn[i], prim[i]);
    fprintf(stderr, " | DRAW_INDEX_ARRAY=%u DRAW_ARRAYS=%u INLINE words=%u | index u16=%u u32=%u"
                    " | RESTART enable=%u disable=%u index(last)=0x%X (%u sets)\n",
            nidx, narr, inline_w, idx16, idx32, rst_on, rst_off, rst_val, rst_vals);
}

static void ico_dump_usleep_callers(void)
{
    fprintf(stderr, "[ICO-WD]   usleep callers:");
    for (unsigned i = 0; i < 16 && s_us_lr[i]; i++)
        fprintf(stderr, " lr=0x%X(us=%u)x%ld", s_us_lr[i], s_us_arg[i], (long)s_us_n[i]);
    fprintf(stderr, "\n");
}

/* ICO_WATCHDOG=<sec>: at <sec> and again 5 s later, print what every guest
 * thread is doing -- the HLE it is inside, its lr/r3/sp -- plus the flip count.
 * Two samples so a parked thread (same lr) can be told from one still moving.
 * A port-sized stand-in for the boot harness's PS3_DEBUG console, which the
 * template runner does not carry. */
static DWORD WINAPI ico_watchdog(LPVOID arg)
{
    const DWORD first_ms = (DWORD)(uintptr_t)arg;
    Sleep(first_ms);
    for (int pass = 1; pass <= 2; pass++) {
        fprintf(stderr, "[ICO-WD] pass %d at %lus: flips=%u last HLE=0x%08X (%s)\n",
                pass, (unsigned long)((first_ms + (pass - 1) * 5000) / 1000),
                cellGcm_flip_request_count(), g_last_hle_nid,
                g_last_hle_name ? g_last_hle_name : "");
        for (unsigned t = 0; t < 64; t++)
            if (g_hle_inflight[t])
                fprintf(stderr, "[ICO-WD]   guest-tid %-3u inside %s\n", t, g_hle_inflight[t]);
        ppu_report_guest_lrs();
        ico_dump_fibers();
        ico_dump_lfqueues();
        ico_dump_usleep_callers();
        ico_ring_census();
        /* GCM labels 0x40..0x47 (0x20000400..): the MLAA job descriptor names
         * label 0x45 as the RSX-side dependency of each job. */
        fprintf(stderr, "[ICO-WD]   labels 0x40..0x47:");
        for (uint32_t k = 0; k < 8; k++) fprintf(stderr, " %08X", vm_read32(0x20000400u + k * 0x10u));
        fprintf(stderr, "\n");
        fprintf(stderr, "[ICO-WD]   handler calls: flip=%ld vblank=%ld user=%ld\n",
                (long)s_calls_flip, (long)s_calls_vblank, (long)s_calls_user);
        /* ICO's renderer object: **(TOC - 0x3A40), the pointer the flip
         * handler (func_0016E29C) bumps +0x380 through. TOC = 0x004CFEB0. */
        { const uint32_t pp = vm_read32(0x004CFEB0u - 0x3A40u);
          const uint32_t gfx = pp ? vm_read32(pp) : 0;
          if (gfx)
              fprintf(stderr, "[ICO-WD]   gfx=0x%08X submitted(+0x37C)=%u completed(+0x380)=%u\n",
                      gfx, vm_read32(gfx + 0x37C), vm_read32(gfx + 0x380)); }
        /* The PPU<->SPU sync object the main thread waits on (func_00341F9C on
         * mgr+0x2C, mgr = *(TOC-0x7340)): +0 task count, +8 taskset, +0xC id
         * list; the wait is for word +0x2C. The F5AAC0DE user handler signals
         * every listed task (func_00341FE8 -> _cellSpursSendSignal). */
        { const uint32_t mgr = vm_read32(0x004CFEB0u - 0x7340u);
          if (mgr) {
              const uint32_t o = mgr + 0x2C;
              fprintf(stderr, "[ICO-WD]   sync obj 0x%08X:", o);
              for (uint32_t k = 0; k < 0x40; k += 4) fprintf(stderr, " %08X", vm_read32(o + k));
              const uint32_t n = vm_read32(o), ids = vm_read32(o + 0xC);
              /* The SPURS barrier (+0x1C) and its neighbour (+0x20) the job uses. */
              for (uint32_t which = 0x1C; which <= 0x20; which += 4) {
                  const uint32_t b = vm_read32(o + which);
                  if (!b) continue;
                  fprintf(stderr, "\n[ICO-WD]   line 0x%08X:", b);
                  for (uint32_t k = 0; k < 0x40; k += 4) fprintf(stderr, " %08X", vm_read32(b + k));
              }
              /* Its taskset's first six 128-bit bitsets: running, ready,
               * pending ready, enabled, signalled, waiting (task 0 = MSB). */
              { const uint32_t ts = vm_read32(o + 8);
                static const char* nm[6] = { "running", "ready", "pending", "enabled", "signalled", "waiting" };
                for (uint32_t k = 0; k < 6 && ts; k++)
                    fprintf(stderr, "\n[ICO-WD]   taskset 0x%08X %-9s %08X", ts, nm[k], vm_read32(ts + k * 0x10)); }
              /* The per-task 64-byte job descriptors func_00341E50 writes. */
              { const uint32_t tbl = vm_read32(o + 0x14), cnt = vm_read32(o);
                for (uint32_t t = 0; t < cnt && t < 8 && tbl; t++) {
                    fprintf(stderr, "\n[ICO-WD]   desc[%u]:", t);
                    for (uint32_t k = 0; k < 0x40; k += 4) fprintf(stderr, " %08X", vm_read32(tbl + t * 0x40 + k));
                } }
              fprintf(stderr, "\n[ICO-WD]   sync obj ids:");
              for (uint32_t k = 0; k < n && k < 8 && ids; k++) fprintf(stderr, " %u", vm_read32(ids + 4 * k));
              fprintf(stderr, "\n");
          } }
        /* The precache loader the main loop polls (func_0009CF0C): it reads
         * ico_precachefiles.txt, then each file it lists. Base 0x00A16F18
         * (TOC-0x63DC); state 0 open/1 list read/2 entries/3 done. */
        fprintf(stderr, "[ICO-WD]   precache state=%u handle=0x%08X lines=%u entry=0x%08X\n",
                vm_read32(0x00A23D5Cu), vm_read32(0x00A23D68u), vm_read32(0x00A23D58u),
                vm_read32(0x00A23D6Cu));
        /* The FIFO as the title sees it: the GCM control register
         * (cellGcmGetControlRegister = inject base + 0x2000: put/get/ref) and
         * its current context (_cellGcmInitBody wrote the pointer to 0xC342C8). */
        { extern uint32_t ppu_hle_inject_base;
          const uint32_t ctl = ppu_hle_inject_base + 0x2000u;
          const uint32_t gctx = vm_read32(0x00C342C8u);
          fprintf(stderr, "[ICO-WD]   gcm control put=0x%08X get=0x%08X ref=0x%08X\n",
                  vm_read32(ctl), vm_read32(ctl + 4), vm_read32(ctl + 8));
          if (gctx) {
              const uint32_t begin = vm_read32(gctx), cur = vm_read32(gctx + 8);
              fprintf(stderr, "[ICO-WD]   gcm ctx 0x%08X begin=0x%08X end=0x%08X current=0x%08X\n",
                      gctx, begin, vm_read32(gctx + 4), cur);
              /* Words written but not yet kicked: begin + put .. current. */
              const uint32_t from = begin + vm_read32(ctl);
              if (pass == 1 && cur > from && cur - from <= 0x400)
                  for (uint32_t a = from; a < cur; a += 16)
                      fprintf(stderr, "[ICO-WD]     io 0x%05X: %08X %08X %08X %08X\n", a - begin,
                              vm_read32(a), vm_read32(a + 4), vm_read32(a + 8), vm_read32(a + 12));
          } }
        fflush(stderr);
        if (pass == 1) Sleep(5000);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

/* ICO_PROF: where the frame clock's time goes (QPC ticks), and how often a
 * drain returns with the walker still short of put. */
static volatile LONG64 s_fc_sleep, s_fc_drain, s_fc_present, s_fc_pump;
static volatile LONG   s_fc_drains, s_fc_short, s_fc_iters;
static volatile LONG64 s_fc_bytes;
static inline LONG64 fc_now(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static void fc_drain(void)
{
    extern uint32_t ppu_hle_inject_base;
    const uint32_t ctl = ppu_hle_inject_base + 0x2000u;
    const uint32_t g0 = vm_read32(ctl + 4);
    const LONG64 a = fc_now();
    cellGcm_rsx_process_fifo();
    InterlockedAdd64(&s_fc_drain, fc_now() - a);
    const uint32_t g1 = vm_read32(ctl + 4), put = vm_read32(ctl + 0);
    InterlockedIncrement(&s_fc_drains);
    if (g1 != put) InterlockedIncrement(&s_fc_short);
    if (g1 >= g0) InterlockedAdd64(&s_fc_bytes, (LONG64)(g1 - g0));
}
static void fc_present(void (*fn)(void))
{
    const LONG64 a = fc_now();
    fn();
    InterlockedAdd64(&s_fc_present, fc_now() - a);
}
extern "C" void ico_frameclock_report(double secs)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    const double k = 1000.0 / (double)f.QuadPart / secs;
    const LONG drains = InterlockedExchange(&s_fc_drains, 0), shorts = InterlockedExchange(&s_fc_short, 0);
    fprintf(stderr, "[ICO-PROF]   frame clock (ms/s): sleep %.0f drain %.0f present %.0f pump %.0f | iters %ld drains %ld"
                    " short(get!=put) %ld | walker %.0f KB/s\n",
            (double)InterlockedExchange64(&s_fc_sleep, 0) * k, (double)InterlockedExchange64(&s_fc_drain, 0) * k,
            (double)InterlockedExchange64(&s_fc_present, 0) * k, (double)InterlockedExchange64(&s_fc_pump, 0) * k,
            (long)InterlockedExchange(&s_fc_iters, 0), (long)drains, (long)shorts,
            (double)InterlockedExchange64(&s_fc_bytes, 0) / 1024.0 / secs);
}

/* ---------------------------------------------------------------------------
 * "Exit Game" in the title and pause menus (ICO_EXIT_ITEM=0 turns it off).
 *
 * ICO's 2D menus are data:
 *  - layers: 0x70-byte records at [TOC-0x7990] (0x4C8520 -> 0x51E3F4), 0x1D0 of
 *    them. +0x14/+0x18/+0x1C texture, +0x2C state entered on confirm, +0x38
 *    down / +0x3C up neighbour, +0x4C width override, +0x50 Y, +0x5C U,
 *    +0x60 height, +0x64 width, +0x68 V (texture half-pixels), +0x6C flags;
 *  - screens: 56-byte records at 0x5BF75C, [first, end) layer range, +0x28
 *    default and +0x2C selected layer; the current screen is [0x5E9B78].
 *    Title = screen 12 (Continue 0x31, New Game 0x32), pause = screen 57
 *    (0x124-0x128: two backdrops, Options, Back, Return to title screen).
 * The selected item is highlighted by the game's own draw code (layer ==
 * selected), so an item added as a real layer gets the same pulsing effect.
 *
 *  - on-screen rects: func_0003232C looks a drawn layer up in a table of 0x1A
 *    entries of 0x2C bytes at [TOC-0x7498] (0x3BD8CC) -- screen, layer range
 *    (inclusive), then slots -- and scales the rect by 0.75; layers 0x31/0x32
 *    alone (hard-coded) also get +240 in Y. A layer outside its screen's
 *    entry is not drawn.
 *
 * A screen draws a contiguous layer range and the next layer after each menu
 * belongs to another screen, so the array is copied to free guest memory with
 * room for 3 more layers and the TOC entry is pointed at the copy.
 *  - Title: the confirm compares the selected layer with 0x31 and 0x32 by
 *    number (func_0001E34C), so Continue and New Game stay; Exit takes 0x33
 *    and screen 13's one layer (a hidden "New Game") moves to 0x1D2.
 *  - Pause: the confirm compares with 0x127 (Back, func_0001FEA4) and the
 *    Options screen after it is full of numbers too, so nothing moves. Exit
 *    takes 0x129 -- Options' first layer, a dim bar identical to the pause's
 *    own 0x124 -- while the pause is the current screen, and the bar is put
 *    back whenever it is not. Back's rect is trimmed so it no longer reaches
 *    the label.
 *  - The rect table follows. Exit at 0x33 lacks the +240 of 0x31/0x32, so its
 *    Y gets the equivalent +20.
 *  - Flags bit 0x08000000 hides a layer. Every frame the draw loop sets it from
 *    bit 0x04000000 on the shown screen's layers, and the title's handler then
 *    clears it on 0x31/0x32 by number; Exit simply has no 0x04. Screen 13's
 *    handler hides 0x33 by number (func_0001E0CC), so the moved layer copies
 *    that bit from 0x33 instead.
 * The game resolves textures for every layer of a screen's range itself.
 *
 * The label is built in the menu texture itself (menu_PAL_03, 1024x512 A8R8G8B8
 * swizzled, 11 mips) from the game's own letters -- "E" of "Film Effect", "xi"
 * of "Maximum", "t" of "Activate", "Game" of "New Game" -- into a free area
 * right of "Back" (U 196, V 20), so no game image is shipped with the port.
 * The texture is found in guest memory by two content hashes; the game's
 * texture cache re-hashes it each frame, so the change shows up by itself.
 * Choosing Exit enters an unused screen (0x43), which this loop turns into a
 * process exit. Until the label is in place the Exit layers stay unlinked. */
static const uint32_t EX_TOC_LAYERS = 0x4C8520u, EX_OLD_LAYERS = 0x51E3F4u;
static const uint32_t EX_SCREENS = 0x5BF75Cu, EX_CUR_SCREEN = 0x5E9B78u;
static const uint32_t EX_NEW_LAYERS = 0x0C000000u, EX_N_OLD = 0x1D0u;
static const uint32_t EX_EXIT_SCREEN = 0x43u, EX_RECTS_TOC = 0x4C8A18u;
/* title Exit, pause Exit; new layers: the pause Exit's record, Options' bar
 * while the pause Exit borrows its slot, screen 13's layer */
static const uint32_t EX_TEXIT = 0x33u, EX_PEXIT = 0x129u;
static const uint32_t EX_PREC = 0x1D0u, EX_BAR = 0x1D1u, EX_S13 = 0x1D2u, EX_N_NEW = 0x1D3u;
static int s_ex_p129 = 0;          /* 0x129 holds: 0 Options' bar, 1 the pause Exit */
static int s_ex_on = 0, s_ex_linked = 0;
static uint32_t s_ex_tex[4]; static int s_ex_ntex = 0;

static inline uint32_t ex_layer(uint32_t i) { return EX_NEW_LAYERS + i * 0x70u; }
static inline uint32_t ex_screen(uint32_t i) { return EX_SCREENS + i * 56u; }

static uint32_t ex_swz(uint32_t x, uint32_t y, uint32_t lw, uint32_t lh)
{
    uint32_t i = 0, bit = 0;
    while (lw || lh) {
        if (lw) { i |= (x & 1u) << bit++; x >>= 1; lw--; }
        if (lh) { i |= (y & 1u) << bit++; y >>= 1; lh--; }
    }
    return i;
}
static uint64_t ex_fnv(const uint8_t* p, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/* Level l of the texture at guest `base`: byte offset and log2 size. */
static uint32_t ex_level(uint32_t l, uint32_t* lw, uint32_t* lh)
{
    uint32_t off = 0, w = 10, h = 9;
    for (uint32_t k = 0; k < l; k++) {
        off += (1u << w) * (1u << h) * 4u;
        if (w) w--;
        if (h) h--;
    }
    *lw = w; *lh = h;
    return off;
}
/* A8R8G8B8 texel (bytes A,R,G,B in memory) as r,g,b,a. */
static void ex_get(uint32_t base, uint32_t l, uint32_t x, uint32_t y, uint8_t c[4])
{
    uint32_t lw, lh; const uint32_t off = ex_level(l, &lw, &lh);
    const uint8_t* p = vm_base + base + off + ex_swz(x, y, lw, lh) * 4u;
    c[0] = p[1]; c[1] = p[2]; c[2] = p[3]; c[3] = p[0];
}
static void ex_put(uint32_t base, uint32_t l, uint32_t x, uint32_t y, const uint8_t c[4])
{
    uint32_t lw, lh; const uint32_t off = ex_level(l, &lw, &lh);
    uint8_t* p = vm_base + base + off + ex_swz(x, y, lw, lh) * 4u;
    p[1] = c[0]; p[2] = c[1]; p[3] = c[2]; p[0] = c[3];
}

/* menu_PAL_03 (US) is recognised by two 256-byte content hashes at known
 * level-0 offsets, both outside the box the label goes into. */
static const struct { uint32_t off; uint64_t hash; } EX_SIG[2] = {
    { 0x144C00u, 0xCF4B6C0B6CFDE50Cull },   /* "New Game" */
    { 0x06B100u, 0x22B9A787DEC12825ull },   /* "Film Effect" */
};
static const uint32_t EX_SPAN = 0x2AAAACu;  /* all 11 levels */

static bool ex_tex_valid(uint32_t base)
{
    for (const auto& s : EX_SIG)
        if (ex_fnv(vm_base + base + s.off, 256) != s.hash) return false;
    return true;
}

/* Destination of the label in level 0 (texture pixels) and its UV box. */
static const uint32_t EX_BX = 392, EX_BY = 40, EX_BW = 242, EX_BH = 40;

/* Compose "Exit Game" from the texture's own glyphs into the free box, then
 * rebuild that box in every mip. Alpha is PS2-range (0..128); letters are
 * combined with "over". Returns
 *    1  the label was written,
 *   -1  the box is already in use: in the US sheet it is empty until the label
 *       goes in, so that is the label,
 *    0  the label does not fit,
 *   -2  `base` no longer holds the menu texture (the memory was reused):
 *       nothing is written. */
static int ex_patch_texture(uint32_t base)
{
    if (!ex_tex_valid(base)) return -2;
    for (uint32_t y = EX_BY; y < EX_BY + EX_BH; y++)
        for (uint32_t x = EX_BX; x < EX_BX + EX_BW; x++) {
            uint8_t c[4]; ex_get(base, 0, x, y, c);
            if (c[3]) return -1;
        }
    struct { int x0, x1, base, lc, rc, gap; } G[5] = {
        { 252, 278, 228, 249, 279,  0 },   /* E  (Film Effect) */
        { 333, 356, 108, 332, 357,  4 },   /* x  (Maximum)     */
        { 359, 364, 108, 358, 366,  4 },   /* i  (Maximum)     */
        { 610, 622, 349, 609, 624,  4 },   /* t  (Activate)    */
        { 843, 969,  28, 835, 975, 16 },   /* Game (New Game)  */
    };
    const int PAD_T = 26, CH = 35, CW = 260;
    static uint8_t canvas[35][260][4];
    memset(canvas, 0, sizeof canvas);
    int cx = 8;
    for (int k = 0; k < 5; k++) {
        if (G[k].gap) cx += G[k].gap - (G[k].x0 - G[k].lc);
        for (int yy = 0; yy < CH; yy++) {
            const int sy = G[k].base - PAD_T + yy;
            for (int sx = G[k].lc; sx < G[k].rc; sx++) {
                uint8_t s[4]; ex_get(base, 0, (uint32_t)sx, (uint32_t)sy, s);
                if (!s[3]) continue;
                const int dx = cx + (sx - G[k].lc);
                if (dx < 0 || dx >= CW) continue;
                uint8_t* d = canvas[yy][dx];
                const double fa = s[3] / 128.0, ba = d[3] / 128.0, oa = fa + ba * (1 - fa);
                if (oa > 0) {
                    for (int i = 0; i < 3; i++)
                        d[i] = (uint8_t)((s[i] * fa + d[i] * ba * (1 - fa)) / oa);
                    const double a = oa * 128.0 + 0.5;
                    d[3] = (uint8_t)(a > 128 ? 128 : a);
                }
            }
        }
        cx += (G[k].rc - G[k].lc) - (G[k].rc - G[k].x1);
    }
    const int w = cx + 10;
    if (w > (int)EX_BW) return 0;
    for (int yy = 0; yy < CH; yy++)
        for (int xx = 0; xx < w; xx++)
            ex_put(base, 0, EX_BX + xx, EX_BY + 2 + yy, canvas[yy][xx]);
    for (uint32_t l = 1; l < 11; l++) {
        uint32_t lw, lh; ex_level(l, &lw, &lh);
        const uint32_t W = 1u << lw, H = 1u << lh;
        const uint32_t x0 = EX_BX >> l, y0 = EX_BY >> l;
        uint32_t x1 = (EX_BX + EX_BW + (1u << l) - 1) >> l, y1 = (EX_BY + EX_BH + (1u << l) - 1) >> l;
        if (x1 > W) x1 = W;
        if (y1 > H) y1 = H;
        uint32_t pw, ph; ex_level(l - 1, &pw, &ph);
        for (uint32_t y = y0; y < y1; y++)
            for (uint32_t x = x0; x < x1; x++) {
                unsigned acc[4] = { 0, 0, 0, 0 }, n = 0;
                for (uint32_t j = 0; j < 2; j++)
                    for (uint32_t i = 0; i < 2; i++) {
                        const uint32_t sx = 2 * x + i, sy = 2 * y + j;
                        if (sx >= (1u << pw) || sy >= (1u << ph)) continue;
                        uint8_t c[4]; ex_get(base, l - 1, sx, sy, c);
                        for (int q = 0; q < 4; q++) acc[q] += c[q];
                        n++;
                    }
                if (!n) continue;
                uint8_t c[4];
                for (int q = 0; q < 4; q++) c[q] = (uint8_t)((acc[q] + n / 2) / n);
                ex_put(base, l, x, y, c);
            }
    }
    return 1;
}

/* Find every copy of menu_PAL_03 in guest memory (main heap and RSX local),
 * by two 256-byte content hashes at known level-0 offsets, 128-byte aligned. */
static void ex_find_textures(void)
{
    s_ex_ntex = 0;
    const uint32_t ranges[2][2] = { { 0x30000000u, 0x50000000u }, { 0xC0000000u, 0xD0000000u } };
    for (int r = 0; r < 2; r++) {
        for (uint64_t a = ranges[r][0]; a < ranges[r][1]; ) {
            MEMORY_BASIC_INFORMATION mb;
            if (!VirtualQuery(vm_base + a, &mb, sizeof mb)) break;
            uint64_t end = (uint64_t)((uint8_t*)mb.BaseAddress + mb.RegionSize - vm_base);
            if (end <= a) break;
            if (end > ranges[r][1]) end = ranges[r][1];
            if (mb.State == MEM_COMMIT && !(mb.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                for (uint64_t p = (a + 127) & ~127ull; p + 256 <= end; p += 128) {
                    if (ex_fnv(vm_base + p, 256) != EX_SIG[0].hash) continue;
                    if (p < EX_SIG[0].off) continue;
                    const uint64_t base = p - EX_SIG[0].off;
                    if (base + EX_SPAN > ranges[r][1]) continue;
                    if (ex_fnv(vm_base + base + EX_SIG[1].off, 256) != EX_SIG[1].hash) continue;
                    if (s_ex_ntex < 4) s_ex_tex[s_ex_ntex++] = (uint32_t)base;
                }
            }
            a = end;
        }
    }
}

static void ex_copy_layer(uint32_t dst, uint32_t src)
{
    for (uint32_t o = 0; o < 0x70u; o += 4) vm_write32(ex_layer(dst) + o, vm_read32(ex_layer(src) + o));
}

/* The rect-table entry (func_0003232C) of a screen, or 0. */
static uint32_t ex_rect_entry(uint32_t screen)
{
    const uint32_t T = vm_read32(EX_RECTS_TOC);
    for (uint32_t i = 0; i < 0x1A; i++)
        if (vm_read32(T + i * 0x2C) == screen) return T + i * 0x2C;
    return 0;
}

/* Before the game runs: copy the layer array with the new layers, point the
 * TOC entry at it, and adjust screens 12, 13 and 57 and their rect entries.
 * Both Exit items are in their screens from the start but unlinked until the
 * label exists. */
static void ex_init(void)
{
    const char* e = getenv("ICO_EXIT_ITEM");
    if (e && *e == '0') return;
    const uint32_t r12 = ex_rect_entry(12), r13 = ex_rect_entry(13), r57 = ex_rect_entry(57);
    if (vm_read32(EX_TOC_LAYERS) != EX_OLD_LAYERS || vm_read32(ex_screen(12)) != 0x31u ||
        vm_read32(ex_screen(12) + 4) != 0x33u || vm_read32(ex_screen(13)) != 0x33u ||
        vm_read32(ex_screen(13) + 4) != 0x34u || vm_read32(ex_screen(57)) != 0x124u ||
        vm_read32(ex_screen(57) + 4) != 0x129u || vm_read32(ex_screen(58)) != 0x129u ||
        !r12 || !r13 || !r57 ||
        vm_read32(r12 + 4) != 0x30u || vm_read32(r12 + 8) != 0x32u ||
        vm_read32(r13 + 4) != 0x33u || vm_read32(r13 + 8) != 0x34u ||
        vm_read32(r57 + 4) != 0x124u || vm_read32(r57 + 8) != 0x129u) {
        fprintf(stderr, "[ico] exit item: menu tables not as expected -- off\n");
        return;
    }
    for (uint32_t o = 0; o < EX_N_OLD * 0x70u; o += 4)
        vm_write32(EX_NEW_LAYERS + o, vm_read32(EX_OLD_LAYERS + o));
    ex_copy_layer(EX_S13, 0x33);
    vm_write32(ex_layer(EX_S13) + 0x6C, vm_read32(ex_layer(EX_S13) + 0x6C) & ~0x04000000u);
    ex_copy_layer(EX_TEXIT, 0x32);
    ex_copy_layer(EX_PREC, 0x128);
    /* Back's rect (U 128-256, V 20-40) reaches into the label's box (U 196+);
     * its letters end at U 189, so draw U 128-192 at the same 1:1 width. */
    if (vm_read32(ex_layer(0x127) + 0x5C) == 128u && vm_read32(ex_layer(0x127) + 0x68) == 20u &&
        vm_read32(ex_layer(0x127) + 0x64) == 128u && vm_read32(ex_layer(0x127) + 0x4C) == 128u) {
        vm_write32(ex_layer(0x127) + 0x64, 64);
        vm_write32(ex_layer(0x127) + 0x4C, 64);
    }
    const uint32_t ex[2] = { EX_TEXIT, EX_PREC };
    for (int k = 0; k < 2; k++) {
        const uint32_t L = ex_layer(ex[k]);
        for (uint32_t o = 0x20; o < 0x40; o += 4) vm_write32(L + o, 0xFFFFFFFFu);
        vm_write32(L + 0x2C, EX_EXIT_SCREEN);
        vm_write32(L + 0x4C, 0);                      /* no width override */
        vm_write32(L + 0x5C, EX_BX / 2); vm_write32(L + 0x68, EX_BY / 2);
        vm_write32(L + 0x64, EX_BW / 2); vm_write32(L + 0x60, EX_BH / 2);
    }
    vm_write32(ex_layer(EX_PREC) + 0x3C, 0x128);
    vm_write32(ex_layer(EX_TEXIT) + 0x6C, vm_read32(ex_layer(EX_TEXIT) + 0x6C) & ~0x0C000000u);
    /* Title, 30 apart as before and moved up by 15 to stay centred: Continue
     * 120, New Game 150, Exit 180 -- written as 200, since only 0x31/0x32 get
     * +240 on screen (y = 12*Y - 1356 for the rest). Pause: Exit 20 below
     * Return, the spacing of Options and Back. */
    vm_write32(ex_layer(0x31) + 0x50, 120);
    vm_write32(ex_layer(0x32) + 0x50, 150);
    vm_write32(ex_layer(EX_TEXIT) + 0x50, 200);
    vm_write32(ex_layer(EX_PREC) + 0x50, vm_read32(ex_layer(0x128) + 0x50) + 20);
    vm_write32(EX_TOC_LAYERS, EX_NEW_LAYERS);
    vm_write32(ex_screen(12) + 4, EX_TEXIT + 1);
    vm_write32(ex_screen(13), EX_S13); vm_write32(ex_screen(13) + 4, EX_S13 + 1);
    vm_write32(ex_screen(13) + 0x28, EX_S13); vm_write32(ex_screen(13) + 0x2C, EX_S13);
    vm_write32(ex_screen(57) + 4, EX_PEXIT + 1);
    /* rect entries, ranges inclusive (57's already reaches 0x129) */
    vm_write32(r12 + 8, EX_TEXIT);
    vm_write32(r13 + 4, EX_S13); vm_write32(r13 + 8, EX_S13 + 1);
    s_ex_on = 1;
    fprintf(stderr, "[ico] exit item: menu layers copied to 0x%08X\n", EX_NEW_LAYERS);
}

/* Link the Exit items in (or out) of both menus. */
static void ex_link(int on)
{
    const uint32_t none = 0xFFFFFFFFu;
    vm_write32(ex_layer(0x32) + 0x38, on ? EX_TEXIT : none);
    vm_write32(ex_layer(EX_TEXIT) + 0x3C, 0x32);
    vm_write32(ex_layer(0x128) + 0x38, on ? EX_PEXIT : none);
    s_ex_linked = on;
}

/* Every pass of the frame loop. */
static void ex_tick(void)
{
    if (!s_ex_on) return;
    const uint32_t cur0 = vm_read32(EX_CUR_SCREEN);
    { const uint32_t s = vm_read32(ex_screen(13) + 0x2C);
      if (s == 0x33u) vm_write32(ex_screen(13) + 0x2C, EX_S13); }
    /* screen 13's moved layer follows the hide bit its handler sets on 0x33
     * (only while screen 13 is up: on the title menu the draw loop clears it) */
    if (cur0 == 13u) {
        const uint32_t f = vm_read32(ex_layer(EX_S13) + 0x6C);
        const uint32_t h = vm_read32(ex_layer(EX_TEXIT) + 0x6C) & 0x08000000u;
        if ((f & 0x08000000u) != h) vm_write32(ex_layer(EX_S13) + 0x6C, (f & ~0x08000000u) | h);
    }
    /* 0x129: the pause Exit while the pause is up, Options' bar otherwise */
    if (cur0 == 57u && !s_ex_p129) {
        ex_copy_layer(EX_BAR, EX_PEXIT);
        for (uint32_t o = 0; o < 0x70u; o += 4)
            if (o < 0x14 || o > 0x1C)   /* texture fields: the resolved ones of Return */
                vm_write32(ex_layer(EX_PEXIT) + o, vm_read32(ex_layer(EX_PREC) + o));
        for (uint32_t o = 0x14; o <= 0x1C; o += 4)
            vm_write32(ex_layer(EX_PEXIT) + o, vm_read32(ex_layer(0x128) + o));
        s_ex_p129 = 1;
    } else if (cur0 != 57u && s_ex_p129) {
        if (vm_read32(ex_screen(57) + 0x2C) == EX_PEXIT) vm_write32(ex_screen(57) + 0x2C, 0x128);
        ex_copy_layer(EX_PEXIT, EX_BAR);
        s_ex_p129 = 0;
    }

    const uint32_t cur = vm_read32(EX_CUR_SCREEN);
    if (cur == EX_EXIT_SCREEN) {
        fprintf(stderr, "[ico] Exit Game chosen -- exiting\n");
        fflush(stdout); fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    /* The label: while a menu is up, re-check each known copy of the texture
     * once a second (a reload in place brings back the unpatched sheet) and
     * forget copies whose memory now holds something else. A scan hashes up
     * to ~0.5 GB on this thread (~0.4 s), so it runs only when no copy is
     * known, and stops after three empty ones: on a sheet this does not
     * recognise, the Exit items simply stay unlinked. */
    static ULONGLONG last = 0, last_scan = 0;
    static int empty_scans = 0;
    const ULONGLONG now = GetTickCount64();
    if ((cur == 12u || cur == 57u) && now - last > 1000) {
        last = now;
        int ok = 0, n = 0;
        for (int i = 0; i < s_ex_ntex; i++) {
            const int r = ex_patch_texture(s_ex_tex[i]);
            if (r == -2) continue;                  /* memory reused: forget it */
            s_ex_tex[n++] = s_ex_tex[i];
            if (r == 1 || r == -1) ok++;
        }
        s_ex_ntex = n;
        if (!s_ex_ntex && empty_scans < 3 && (!last_scan || now - last_scan > 10000)) {
            last_scan = now;
            const ULONGLONG t0 = GetTickCount64();
            ex_find_textures();
            fprintf(stderr, "[ico] exit item: %d menu texture copies found in %llu ms\n",
                    s_ex_ntex, (unsigned long long)(GetTickCount64() - t0));
            empty_scans = s_ex_ntex ? 0 : empty_scans + 1;
            for (int i = 0; i < s_ex_ntex; i++) {
                const int r = ex_patch_texture(s_ex_tex[i]);
                if (r == 1 || r == -1) ok++;
                if (r == 1) fprintf(stderr, "[ico] exit item: label written into menu texture at 0x%08X\n", s_ex_tex[i]);
            }
        }
        if ((ok > 0) != s_ex_linked) ex_link(ok > 0);
    }
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    /* The vblank beat: exactly 60 Hz on the QPC clock. ICO takes its frame
     * time from vblanks elapsed (dt = vblanks / 60); `+= 16` ms on
     * GetTickCount64 was 62.5 Hz and ran the whole game ~4% fast. Tick k is
     * due at tick_q0 + k * freq / 60. */
    LARGE_INTEGER tick_fq; QueryPerformanceFrequency(&tick_fq);
    LONG64 tick_q0 = fc_now(); unsigned long long tick_k = 0;
    /* The frame clock sleeps on this event: a flip releasing a held prepare
     * marker sets it (always), and with ICO_FC_WAKE=1 a PPU usleep does too. */
    s_fc_wake_ev = CreateEventW(NULL, FALSE, FALSE, NULL);   /* auto-reset */
    if (s_fc_wake_ev) g_gcm_prepare_release_hook = fc_on_prepare_release;
    { const char* e = getenv("ICO_FC_WAKE");
      if (e && atoi(e) > 0) {
          s_fc_wake_on = s_fc_wake_ev != NULL;
          fprintf(stderr, "[ico] ICO_FC_WAKE: frame clock woken by PPU usleep (%s)\n",
                  s_fc_wake_on ? "on" : "event failed");
      } }

    for (;;) {
        { const LONG64 a = fc_now();
          if (s_fc_wake_ev) WaitForSingleObject(s_fc_wake_ev, 4); else Sleep(4);
          InterlockedAdd64(&s_fc_sleep, fc_now() - a); }
        InterlockedIncrement(&s_fc_iters);
        int fired = 0;
        const LONG64 now_q = fc_now();
        while (now_q - (tick_q0 + (LONG64)(tick_k * (unsigned long long)tick_fq.QuadPart / 60ull)) >= 0
               && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                fc_present(present_guest_frame);
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) fc_drain();
            tick_k++;
            fired++;
        }
        if (fired >= 240) { tick_q0 = now_q; tick_k = 0; }   /* fell too far behind -- resync */

        ex_tick();

        /* ICO_60FPS=1: render every logic step instead of every second one.
         *
         * ICO's main loop (func_00027BC4) runs its game logic in fixed 1/60 s
         * steps and draws a frame every [0x4D1DDC] steps -- 2, i.e. 30 fps --
         * then flips every [gfx+0x384] vblanks, also 2. Cutscenes, animation
         * and physics advance per STEP, not per second (measured: forcing 15
         * fps took twice the wall time for the same cutscene), so only
         * flipping more often ran the game at 2x. Setting both to 1 draws
         * and flips once per step: 60 fps at the original game speed, and a
         * frame the PC cannot finish in time slows the game the way a dropped
         * frame did on the PS3. Movies keep their own interval (1). */
        { static int f60 = -1;
          if (f60 < 0) { const char* e = getenv("ICO_60FPS"); f60 = (e && atoi(e) > 0) ? 1 : 0;
                         if (f60) fprintf(stderr, "[ico] ICO_60FPS: 1 logic step per frame\n"); }
          if (f60) {
              /* Cutscenes, event scenes and the title advance per DRAWN frame
               * and ignore the setting above, so they would run at 2x. They
               * all hold [0xA16F00] non-zero (the title's "event in progress,
               * player input off" flag: 1 in the opening, the title, every
               * cutscene and in-game event, 0 in play -- measured over a
               * played session). While it is set, keep the original 30 fps. */
              const uint32_t want = vm_read32(0xA16F00u) ? 2u : 1u;
              const uint32_t cur = vm_read32(0x4D1DDCu);
              if ((cur == 1u || cur == 2u) && cur != want) vm_write32(0x4D1DDCu, want);
              const uint32_t gfx = vm_read32(0xB0C15Cu);
              if (gfx) {
                  /* Movies set the interval to 1 themselves: change only the
                   * title's own 2 or a 1 this code wrote, never a movie's 1. */
                  static uint32_t mine = 0;
                  const uint32_t iv = vm_read32(gfx + 0x384);
                  if (iv != want && (iv == 2u || (iv == 1u && mine == 1u))) {
                      vm_write32(gfx + 0x384, want);
                      mine = want;
                  }
              }
          } }


        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                fc_present(present_guest_frame);
                last_flip = cellGcm_flip_request_count();
            }
            fc_drain();

            int pumped;
            { const LONG64 a = fc_now(); pumped = rsx_backend_pump(); InterlockedAdd64(&s_fc_pump, fc_now() - a); }
            if (pumped != 0) {
                /* Window closed: end the process. Only stopping the present
                 * left every guest thread running with no window. */
                fprintf(stderr, "[ico] window closed -- exiting\n");
                fflush(stdout); fflush(stderr);
                /* TerminateProcess, not ExitProcess: guest threads are mid-
                 * flight and may hold locks DLL detach would wait on. */
                TerminateProcess(GetCurrentProcess(), 0);
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
                fc_present(present_guest_frame);
                last_flip = fc;
            } else if (fc == 0) {
                fc_present(rsx_backend_present);
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    if (opd == 0x004BACB0u) InterlockedIncrement(&s_calls_flip);
    else if (opd == 0x004BACF0u) InterlockedIncrement(&s_calls_vblank);
    else if (opd == ICO_USER_HANDLER_OPD) {
        InterlockedIncrement(&s_calls_user);
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        InterlockedExchange64(&s_t_user, q.QuadPart);
    }
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---------------------------------------------------------------------------
 * ico.ini
 *
 * The installer writes ico.ini beside ico.exe: one KEY=VALUE per line, ';'
 * comments. Each pair becomes an environment variable unless the variable is
 * already set, so a shell can still override any of them for one run. This
 * is how the shortcut gets the run settings (VFS root, dev_flash, the RSX
 * engine switches) without a launcher. Relative paths in ICO_LOG_FILE are
 * against the exe's folder.
 * -----------------------------------------------------------------------*/
static char s_exe_dir[MAX_PATH];

static void load_ico_ini(void)
{
    DWORD n = GetModuleFileNameA(NULL, s_exe_dir, sizeof s_exe_dir);
    if (n == 0 || n >= sizeof s_exe_dir) { s_exe_dir[0] = 0; return; }
    char* slash = strrchr(s_exe_dir, '\\');
    if (slash) *slash = 0;

    char path[MAX_PATH + 16];
    snprintf(path, sizeof path, "%s\\ico.ini", s_exe_dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        for (char* e = eq - 1; e >= p && (*e == ' ' || *e == '\t'); e--) *e = 0;
        while (*val == ' ' || *val == '\t') val++;
        size_t len = strlen(val);
        while (len && (val[len - 1] == '\n' || val[len - 1] == '\r' || val[len - 1] == ' ')) val[--len] = 0;
        if (*p && !getenv(p)) _putenv_s(p, val);
    }
    fclose(f);
}

/* ICO_LOG_FILE=<path>: send stdout/stderr to a file instead of the console,
 * and drop the console window. The shortcut the installer creates uses it. */
static int s_log_redirected = 0;
static void redirect_log(void)
{
    const char* lf = getenv("ICO_LOG_FILE");
    if (!lf || !*lf) return;
    char path[MAX_PATH * 2];
    /* absolute: a drive ("C:..."), a UNC path or a rooted one ("\\..." or "/...") */
    const bool is_abs = (lf[0] && lf[1] == ':') || lf[0] == '\\' || lf[0] == '/';
    if (is_abs) snprintf(path, sizeof path, "%s", lf);
    else snprintf(path, sizeof path, "%s\\%s", s_exe_dir, lf);
    /* Keep the previous run's log as <name>.prev.log: after a crash the
     * relaunch would otherwise overwrite the only record of it. */
    char prev[MAX_PATH * 2 + 16];
    snprintf(prev, sizeof prev, "%s", path);
    char* dot = strrchr(prev, '.');
    char* sep = strrchr(prev, '\\');
    if (dot && (!sep || dot > sep)) *dot = 0;
    strncat(prev, ".prev.log", sizeof prev - strlen(prev) - 1);
    MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    if (freopen(path, "w", stdout)) {
        _dup2(_fileno(stdout), _fileno(stderr));
        FreeConsole();
        s_log_redirected = 1;
    }
}

int main(int argc, char** argv)
{
    load_ico_ini();
    redirect_log();

    /* No ELF on the command line: the installer's layout, data\ICO.elf. */
    static char s_default_elf[MAX_PATH + 32];
    char* elf_arg[2] = { argv[0], s_default_elf };
    if (argc < 2 && s_exe_dir[0]) {
        snprintf(s_default_elf, sizeof s_default_elf, "%s\\data\\ICO.elf", s_exe_dir);
        if (GetFileAttributesA(s_default_elf) != INVALID_FILE_ATTRIBUTES) {
            argv = elf_arg;
            argc = 2;
        }
    }

    /* ICO_STDOUT_BUF=1: fully buffer stdout (1 MB). The runtime logs on hot
     * paths (_SendSignal ~80/s, SetPrepareFlip per frame), and with stdout
     * redirected each line was a write: ~24% of the main PPU thread's samples.
     * Off by default -- a killed process loses what is still buffered. */
    { const char* e = getenv("ICO_SKIP_MLAA"); s_skip_mlaa_ppu = (e && atoi(e) > 0) ? 1 : 0; }
    { const char* e = getenv("ICO_STDOUT_BUF");
      if (e && atoi(e) > 0) setvbuf(stdout, NULL, _IOFBF, 1u << 20); }
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        if (s_log_redirected)       /* no console to read that in: say it on screen */
            MessageBoxA(NULL, "data\\ICO.elf was not found next to ico.exe.\n"
                              "Run ICO PC Port Setup again to repair the installation.",
                        "ICO", MB_OK | MB_ICONERROR);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    /* ICO_60FPS: set vblanks-per-frame to 1 in the loaded image, before any
     * guest code runs. The game never writes [0x4D1DDC] -- it is 2 from the
     * ELF -- and derives its frame rate from it, (60 - 10*PAL) / [0x4D1DDC],
     * in some 230 places, some of them once at start-up. The frame clock's
     * own write lands only after those. */
    { const char* e = getenv("ICO_60FPS");
      if (e && atoi(e) > 0 && vm_read32(0x4D1DDCu) == 2u) {
          vm_write32(0x4D1DDCu, 1u);
          fprintf(stderr, "[ico] ICO_60FPS: vblanks per frame 2 -> 1 before boot\n");
      } }
    ex_init();   /* "Exit Game" in the title and pause menus */

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    {
        char sfo[1100];
        snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */
    s_orig_usleep = g_lv2_syscalls.handlers[141];
    g_lv2_syscalls.handlers[141] = ico_usleep_with_pump;

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
    { const char* wd = getenv("ICO_WATCHDOG");
      if (wd && atoi(wd) > 0)
          CreateThread(NULL, 0, ico_watchdog, (LPVOID)(uintptr_t)(atoi(wd) * 1000u), 0, NULL); }
    /* ICO_PROF=<sec>: per-thread CPU and hot RIPs every <sec> s (stubs.cpp). */
    { const char* pf = getenv("ICO_PROF");
      if (pf && atoi(pf) > 0) ico_prof_start((unsigned)atoi(pf)); }

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
