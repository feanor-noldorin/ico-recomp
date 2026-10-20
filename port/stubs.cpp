/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "ppu_recomp.h"          /* ppu_context, function_table */
#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register(uint32_t nid, const char* name, void* handler);
/* runtime/spu/spu_coherency.c: the lock every SPU GETLLAR/PUTLLC takes. */
void spu_lockline_lock(void);
void spu_lockline_unlock(void);
void spu_coh_notify_write(uint32_t ea);
/* runtime/spu/spu_workload.c: latch a SPURS task signal and wake the task. */
void spu_taskset_signal_task(uint32_t taskset_ea, uint32_t taskId);
/* libs/spurs/cellSpurs.c, called through with the raw guest values. */
s32 cellSpursCreateTaskset(void* spurs, void* taskset, uint64_t args,
                           const uint8_t* priority, uint32_t maxContention);
s32 cellSpursCreateTasksetWithAttribute(void* spurs, void* taskset, const void* attr);
s32 cellSpursCreateTask(void* taskset, void* taskId, void* elf, void* context,
                        uint32_t sizeContext, uint32_t lsPattern_ea, uint32_t argument_ea);
/* runtime/ppu/ppu_loader.cpp: lifted-code thread state and OPD resolution. */
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
int* ps3_ppu_dispatch_depth_slot(void);
int* ps3_ppu_vcall_sp_slot(void);
int  ppu_opd_resolve(uint32_t opd, uint32_t* code, uint32_t* toc);
void ps3_indirect_call(ppu_context* ctx);
/* runtime/spu/spu_channels.c: register lifted SPU functions under an image id. */
void spu_begin_image(int image_id);
/* spu/flashatrac/spu_recomp.c, lifted locally from the user's own firmware. */
void flashatrac_spu_recomp_register(void);
/* spu/msdsp_30000/spu_recomp.c: the title's MultiStream DSP (spu_0005) at 0x30000. */
void msdsp30000_spu_recomp_register(void);
}

static inline uint16_t g16(uint32_t ea) { return (uint16_t)(vm_base[ea] << 8 | vm_base[ea + 1]); }
static inline uint32_t g32(uint32_t ea) { return (uint32_t)g16(ea) << 16 | g16(ea + 2); }
static inline void     p16(uint32_t ea, uint16_t v) { vm_base[ea] = (uint8_t)(v >> 8); vm_base[ea + 1] = (uint8_t)v; }

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 * -----------------------------------------------------------------------*/

static void ico_register_overrides(void);

extern "C" void ps3_load_prx_modules(void)
{
    /* Runs before ppu_hle_init(), and the NID table answers with the FIRST
     * entry registered for a NID, so these take precedence over the runtime's. */
    ico_register_overrides();
}

/* ---------------------------------------------------------------------------
 * Taskset registry
 *
 * A CellSpursLFQueue owned by a SPURS instance (owner EA | 1) signals its
 * waiting consumer by (workload id << 8 | task id); real SPURS turns the
 * workload id into a taskset through spurs->wklInfo1[wid].arg. The runtime
 * creates every taskset with wid 0 and never publishes it there, so remember
 * which tasksets each SPURS instance owns instead.
 * -----------------------------------------------------------------------*/
static std::mutex s_ts_lock;
static struct { uint32_t taskset, spurs; } s_tasksets[32];
static unsigned s_ntasksets;

static void taskset_remember(uint64_t spurs, uint64_t taskset)
{
    std::lock_guard<std::mutex> g(s_ts_lock);
    for (unsigned i = 0; i < s_ntasksets; i++)
        if (s_tasksets[i].taskset == (uint32_t)taskset) { s_tasksets[i].spurs = (uint32_t)spurs; return; }
    if (s_ntasksets < 32) s_tasksets[s_ntasksets++] = { (uint32_t)taskset, (uint32_t)spurs };
}

static uint64_t ico_CreateTaskset(uint64_t spurs, uint64_t taskset, uint64_t args,
                                  uint64_t priority, uint64_t maxContention)
{
    s32 rc = cellSpursCreateTaskset((void*)(uintptr_t)spurs, (void*)(uintptr_t)taskset, args,
                                    (const uint8_t*)(uintptr_t)priority, (uint32_t)maxContention);
    if (rc == CELL_OK) taskset_remember(spurs, taskset);
    return (uint64_t)(int64_t)rc;
}

static uint64_t ico_CreateTasksetWithAttribute(uint64_t spurs, uint64_t taskset, uint64_t attr)
{
    s32 rc = cellSpursCreateTasksetWithAttribute((void*)(uintptr_t)spurs, (void*)(uintptr_t)taskset,
                                                 (const void*)(uintptr_t)attr);
    if (rc == CELL_OK) taskset_remember(spurs, taskset);
    return (uint64_t)(int64_t)rc;
}

/* ---------------------------------------------------------------------------
 * The SPURS "2" taskset/task API, which the runtime does not implement.
 *
 * ICO creates its renderer this way (func_001706F8): a 0x2900-byte
 * CellSpursTaskset2, then one task running the Renderer_SPU image (spu_0002)
 * with the PPU->SPU command LFQueue as its argument. Left unresolved, both
 * calls answered a fake CELL_OK, the renderer never existed, the jump-park
 * slots it fills in the RSX command buffer were never patched, no frame ever
 * reached its flip, and the main thread waited in its frames-in-flight
 * throttle (gfx[0x37C] - gfx[0x380] >= 2, func_0016FA1C) for good.
 *
 * Layouts, as ICO fills them after the Initialize call:
 *   CellSpursTasksetAttribute2 (128 B): +0x00 revision, +0x04 name,
 *     +0x08 u64 argTaskset, +0x10 u8 priority[8], +0x18 maxContention,
 *     +0x1C enableClearLs, +0x20 taskNameBuffer
 *   CellSpursTaskAttribute2 (256 B): +0x00 revision, +0x04 sizeContext,
 *     +0x08 u64 eaContext, +0x10 lsPattern (16 B), +0x20 name
 * Both map onto the version-1 creation paths the runtime already has; the
 * taskset header is the same layout, the "2" struct only adds tail fields.
 * -----------------------------------------------------------------------*/
static uint64_t ico_TasksetAttribute2Initialize(uint64_t attr, uint64_t revision)
{
    const uint32_t a = (uint32_t)attr;
    if (!a) return (uint64_t)(int64_t)(s32)0x80410911u;
    if (a & 7u) return (uint64_t)(int64_t)(s32)0x80410910u;
    memset(vm_base + a, 0, 128);
    vm_base[a + 3] = (uint8_t)(revision ? revision : 1);
    memset(vm_base + a + 0x10, 8, 8);                      /* default priority */
    vm_base[a + 0x1B] = 8;                                 /* default maxContention */
    return CELL_OK;
}

static uint64_t ico_CreateTaskset2(uint64_t spurs, uint64_t taskset, uint64_t attr)
{
    const uint32_t ts = (uint32_t)taskset, a = (uint32_t)attr;
    if (!spurs || !ts || !a) return (uint64_t)(int64_t)(s32)0x80410911u;
    if (ts & 0x7Fu) return (uint64_t)(int64_t)(s32)0x80410910u;
    const uint64_t args = (uint64_t)g32(a + 0x08) << 32 | g32(a + 0x0C);
    s32 rc = cellSpursCreateTaskset((void*)(uintptr_t)spurs, (void*)(uintptr_t)ts, args,
                                    (const uint8_t*)(uintptr_t)(a + 0x10), g32(a + 0x18));
    if (rc == CELL_OK) {
        memset(vm_base + ts + 0x1900, 0, 0x2900 - 0x1900);   /* the v1 path clears 0x1900 */
        taskset_remember(spurs, ts);
    }
    fprintf(stderr, "[ico] CreateTaskset2(spurs=0x%08X taskset=0x%08X) -> 0x%08X\n",
            (uint32_t)spurs, ts, (uint32_t)rc);
    return (uint64_t)(int64_t)rc;
}

static uint64_t ico_TaskAttribute2Initialize(uint64_t attr, uint64_t revision)
{
    const uint32_t a = (uint32_t)attr;
    if (!a) return (uint64_t)(int64_t)(s32)0x80410911u;
    if (a & 7u) return (uint64_t)(int64_t)(s32)0x80410910u;
    memset(vm_base + a, 0, 256);
    vm_base[a + 3] = (uint8_t)(revision ? revision : 1);
    return CELL_OK;
}

static uint64_t ico_CreateTask2(uint64_t taskset, uint64_t taskId, uint64_t elf,
                                uint64_t argument, uint64_t attr)
{
    const uint32_t a = (uint32_t)attr;
    uint32_t context = 0, sizeContext = 0, lsPattern = 0;
    if (a) {
        sizeContext = g32(a + 0x04);
        context     = g32(a + 0x0C);                       /* low half of u64 eaContext */
        lsPattern   = a + 0x10;
    }
    s32 rc = cellSpursCreateTask((void*)(uintptr_t)taskset, (void*)(uintptr_t)taskId,
                                 (void*)(uintptr_t)elf, (void*)(uintptr_t)context,
                                 sizeContext, lsPattern, (uint32_t)argument);
    fprintf(stderr, "[ico] CreateTask2(taskset=0x%08X elf=0x%08X ctx=0x%08X size=0x%X) -> 0x%08X\n",
            (uint32_t)taskset, (uint32_t)elf, context, sizeContext, (uint32_t)rc);
    return (uint64_t)(int64_t)rc;
}

/* Deliver a queue wakeup to waiter `id` of queue `q`, whose eaSignal is `sig`. */
static void lfqueue_signal(uint32_t q, uint32_t sig, uint16_t id)
{
    const uint32_t tid = id & 0xFFu;
    if (!(sig & 1u)) { spu_taskset_signal_task(sig, tid); return; }

    /* eaSignal = SPURS | 1 does not say which taskset consumes the queue, and
     * signalling the same task id in a sibling taskset is not harmless: ICO's
     * MultiStream mixer (taskset 0x0134B800, task 0) treats any signal as
     * "command ready" and runs one pass on a zeroed parameter block, DMAing to
     * EA 0x850... The runtime leaves wid 0 in every taskset, so the waiter id
     * cannot tell them apart either. A queue is therefore bound to the taskset
     * it signalled while that was the only one on its SPURS, and only that one
     * is signalled afterwards; an unbound queue still signals all of them. */
    static struct { uint32_t q, taskset; } s_bind[16];
    static unsigned s_nbind;
    const uint32_t spurs = sig & ~0x7Fu;
    uint32_t hits[32]; unsigned n = 0, bound = 0;
    {
        std::lock_guard<std::mutex> g(s_ts_lock);
        for (unsigned i = 0; i < s_ntasksets; i++)
            if (s_tasksets[i].spurs == spurs) hits[n++] = s_tasksets[i].taskset;
        for (unsigned i = 0; i < s_nbind; i++)
            if (s_bind[i].q == q) { bound = s_bind[i].taskset; break; }
        if (!bound && n == 1 && s_nbind < 16) {
            s_bind[s_nbind++] = { q, hits[0] };
            bound = hits[0];
            fprintf(stderr, "[ico] LFQueue q=0x%08X bound to taskset 0x%08X\n", q, bound);
        }
    }
    if (bound) { spu_taskset_signal_task(bound, tid); return; }
    { static int warned;
      if (!warned++)
          fprintf(stderr, "[ico] LFQueue signal: q=0x%08X unbound, SPURS 0x%08X owns %u tasksets; signalling task %u in each\n",
                  q, spurs, n, tid); }
    for (unsigned i = 0; i < n; i++) spu_taskset_signal_task(hits[i], tid);
}

/* ---------------------------------------------------------------------------
 * _cellSpursLFQueuePushBody(CellSpursLFQueue* q, const void* buffer, u32 isBlocking)
 *
 * The consumer is ICO's lifted SPU code (Edge zlib task, spu_0004 at LS
 * 0x9CD8 = GetPopPointer, 0xA768 = element DMA), so the 128-byte line in guest
 * memory is the interface. What that code reads, all big-endian:
 *
 *   +0x00 u16 pop1.m_h1   pops completed      +0x06 u16 pop1.m_h4  pops claimed
 *   +0x08 u16 push1.m_h5  pushes published    +0x10 u32 size, +0x14 u32 depth
 *   +0x18 u64 buffer                          +0x24 u32 direction
 *   +0x30 u16 waiter pack (A<<10 | B<<5 | C)  +0x32 u16 m_hs1[15] waiter ids
 *   +0x70 u64 eaSignal (taskset, or SPURS | 1)
 *
 * Counters run modulo 2*depth and a pointer p names slot p mod depth. A
 * consumer finding nothing registers its id at m_hs1[C mod 15], advances C
 * and waits; once signalled it advances B. The producer advances A when it
 * signals, so (C-A) mod 30 are waiting unsignalled and (A-B) mod 30 are
 * signalled but not yet back. With direction 3 (ANY2ANY) the pack lives in
 * pop1.m_h3 (+0x04) instead of +0x30 (the consumer swaps it in and out).
 *
 * Pushes are serialized here, so publication is in order and push1.m_h5 is
 * both the next free pointer and the published count. The line is changed
 * under the SPU lock-line lock, which makes it atomic against the consumer's
 * GETLLAR/PUTLLC transaction.
 * -----------------------------------------------------------------------*/
static std::mutex s_push_lock;
static void lfqueue_note(uint32_t q);

/* ICO_PROF: pushes per host thread and queue since the last report, so the
 * thread producing the frame's render jobs can be named. */
static struct { DWORD tid; uint32_t q; volatile LONG n; } s_pushers[32];
static void note_pusher(uint32_t q)
{
    const DWORD tid = GetCurrentThreadId();
    for (int i = 0; i < 32; i++) {
        if (s_pushers[i].tid == tid && s_pushers[i].q == q) { InterlockedIncrement(&s_pushers[i].n); return; }
        if (!s_pushers[i].tid) {
            if (InterlockedCompareExchange((volatile LONG*)&s_pushers[i].tid, (LONG)tid, 0) == 0) {
                s_pushers[i].q = q; InterlockedIncrement(&s_pushers[i].n); return;
            }
            i--;   /* lost the race for this slot: look at it again */
        }
    }
}
static void report_pushers(double secs)
{
    fprintf(stderr, "[ICO-PROF]   LFQueue pushes/s by host thread:");
    for (int i = 0; i < 32 && s_pushers[i].tid; i++) {
        const LONG n = InterlockedExchange(&s_pushers[i].n, 0);
        if (n) fprintf(stderr, " tid=%lu q=0x%08X:%.0f", (unsigned long)s_pushers[i].tid, s_pushers[i].q, n / secs);
    }
    fprintf(stderr, "\n");
}

static uint64_t ico_LFQueuePushBody(uint64_t queue, uint64_t buffer, uint64_t isBlocking)
{
    note_pusher((uint32_t)queue);
    const uint32_t q = (uint32_t)queue, src = (uint32_t)buffer;
    if (!q || !src) return (uint64_t)(int64_t)(s32)0x80410911u;      /* TASK_ERROR_NULL_POINTER */
    if (q & 0x7Fu)  return (uint64_t)(int64_t)(s32)0x80410910u;      /* TASK_ERROR_ALIGN */

    lfqueue_note(q);
    { static uint32_t calls_q[8];
      for (unsigned i = 0; i < 8; i++) {
          if (calls_q[i] == q) break;
          if (!calls_q[i]) {
              calls_q[i] = q;
              fprintf(stderr, "[ico] LFQueuePush first call q=0x%08X blocking=%u\n", q, (unsigned)isBlocking);
              break;
          }
      } }

    std::lock_guard<std::mutex> serial(s_push_lock);
    for (unsigned spins = 0;; spins++) {
        uint16_t waiter = 0; bool signal = false; uint32_t sig = 0;

        spu_lockline_lock();
        const uint32_t size  = g32(q + 0x10);
        const uint32_t depth = g32(q + 0x14);
        const uint32_t buf   = g32(q + 0x1C);                  /* low half of the u64 */
        if (!depth || depth > 0x7FFF || !size) {
            spu_lockline_unlock();
            return (uint64_t)(int64_t)(s32)0x80410902u;            /* TASK_ERROR_INVAL */
        }
        const uint32_t ring = depth * 2;
        const uint32_t h1 = g16(q + 0x00), h4 = g16(q + 0x06), h5 = g16(q + 0x08);
        const uint32_t used = (h5 + ring - h1) % ring;

        if (used < depth) {
            const uint32_t slot = h5 >= depth ? h5 - depth : h5;
            memcpy(vm_base + buf + slot * size, vm_base + src, size);
            const uint32_t nh5 = h5 + 1 == ring ? 0 : h5 + 1;
            p16(q + 0x08, (uint16_t)nh5);

            const uint32_t pack_ea = g32(q + 0x24) == 3 ? q + 0x04 : q + 0x30;
            const uint16_t P = g16(pack_ea);
            const uint32_t A = (P >> 10) & 31, B = (P >> 5) & 31, C = P & 31;
            const uint32_t waiting  = (C + 30 - A) % 30;
            const uint32_t signaled = (A + 30 - B) % 30;
            const uint32_t avail    = (nh5 + ring - h4) % ring;
            if (waiting && signaled < avail) {
                waiter = g16(q + 0x32 + 2 * (A % 15));
                const uint32_t nA = A == 29 ? 0 : A + 1;
                p16(pack_ea, (uint16_t)((P & 0x83FFu) | (nA << 10)));
                signal = true;
                sig = g32(q + 0x74);                           /* low half of eaSignal */
            }
            spu_coh_notify_write(q);
            spu_lockline_unlock();

            if (signal && sig) lfqueue_signal(q, sig, waiter);
            /* The first few pushes of each queue. */
            static uint32_t seen_q[8]; static unsigned seen_n[8];
            for (unsigned i = 0; i < 8; i++) {
                if (seen_q[i] && seen_q[i] != q) continue;
                seen_q[i] = q;
                if (seen_n[i]++ < 4)
                    fprintf(stderr, "[ico] LFQueuePush q=0x%08X slot=%u h5->%u%s\n", q, slot, nh5,
                            signal ? " (signalled waiter)" : "");
                break;
            }
            return CELL_OK;
        }
        spu_lockline_unlock();

        if (!isBlocking) return (uint64_t)(int64_t)(s32)0x80410901u;   /* TASK_ERROR_AGAIN */
        /* Full: a PPU producer cannot be woken by a SPURS task signal, so poll
         * for the consumer to complete pops instead of registering in m_hs2. */
        if (spins < 64) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

/* ---------------------------------------------------------------------------
 * Low-level PPU fiber contexts (cellFiberPpuContext*), which the runtime does
 * not implement -- libs/spurs/cellFiber.c has only the scheduler-level API.
 *
 * ICO runs the whole game in fibers. After loading, the main PPU thread calls
 * cellFiberPpuContextRun(fiber0, &cause, NULL, NULL), which returns only when
 * the game is over and must report cause 0; the game's scheduler
 * (func_000A6F88) moves between fibers with cellFiberPpuContextSwitch(target,
 * NULL, NULL); a fiber ends when its entry -- always the game's trampoline
 * func_000A618C, arg = (OPD << 32) | value -- returns. Unimplemented, Run
 * answered a fake CELL_OK at once and the title shut down after its loading
 * screen.
 *
 * Each guest fiber is a Windows fiber with its own host stack (lifted code
 * recurses on the host stack) and its own ppu_context: r1 at the top of the
 * guest stack the title supplied, r2/entry from the entry OPD, r3 = arg, r13
 * and thread_id inherited from the thread that ran it. The guest's 0x280-byte
 * CellFiberPpuContext is used only as the key; its layout is the firmware's
 * and nothing here writes it. Every switch saves the lifted-code thread-locals
 * of the side that leaves -- g_active_ctx, g_trampoline_fn, the indirect-call
 * depth and vcall stack pointer (the runtime exports those two slots for
 * exactly this) -- and restores the side that enters.
 * -----------------------------------------------------------------------*/
struct IcoLiftedState {
    ppu_context* active;
    void (*tramp)(void*);
    int depth, vcall;
};

static void lifted_save(IcoLiftedState& s)
{
    s.active = g_active_ctx;
    s.tramp  = g_trampoline_fn;
    s.depth  = *ps3_ppu_dispatch_depth_slot();
    s.vcall  = *ps3_ppu_vcall_sp_slot();
}

static void lifted_restore(const IcoLiftedState& s)
{
    g_active_ctx    = s.active;
    g_trampoline_fn = s.tramp;
    *ps3_ppu_dispatch_depth_slot() = s.depth;
    *ps3_ppu_vcall_sp_slot()       = s.vcall;
}

struct IcoFiber {
    ppu_context    ctx;              /* the fiber's registers (stable address) */
    uint32_t       ea = 0;           /* the guest CellFiberPpuContext */
    uint32_t       code = 0, toc = 0;
    uint64_t       arg = 0;
    uint32_t       stack_ea = 0, stack_size = 0;
    LPVOID         host = nullptr;
    bool           prepared = false, started = false, exited = false;
    IcoLiftedState saved{};          /* while suspended */
    uint32_t       from = 0;         /* fiber that last switched here (0 = the thread) */
};

static std::mutex s_fib_lock;
static std::unordered_map<uint32_t, IcoFiber*> s_fibers;

static thread_local LPVOID         t_home = nullptr;   /* this thread as a fiber */
static thread_local IcoFiber*      t_cur = nullptr;    /* running guest fiber, or none */
static thread_local IcoLiftedState t_thread_saved{};
static thread_local uint32_t       t_exit_from = 0;

static IcoFiber* fiber_find(uint32_t ea)
{
    std::lock_guard<std::mutex> g(s_fib_lock);
    auto it = s_fibers.find(ea);
    return it == s_fibers.end() ? nullptr : it->second;
}

static void (*lifted_fn(uint32_t code))(ppu_context*)
{
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == code) return function_table[i].func;
    return nullptr;
}

static void WINAPI fiber_main(LPVOID param)
{
    IcoFiber* f = static_cast<IcoFiber*>(param);
    f->started = true;
    if (auto fn = lifted_fn(f->code)) fn(&f->ctx);
    else { f->ctx.ctr = f->code; ps3_indirect_call(&f->ctx); }
    while (g_trampoline_fn) { void (*tf)(void*) = g_trampoline_fn; g_trampoline_fn = nullptr; tf(&f->ctx); }

    /* The entry returned: the fiber is over and control goes back to the
     * thread that ran it, whose cellFiberPpuContextRun now returns. */
    f->exited = true;
    t_exit_from = f->ea;
    t_cur = nullptr;
    fprintf(stderr, "[ico] fiber 0x%08X exited -> back to its thread\n", f->ea);
    SwitchToFiber(t_home);
    /* Not reached: an exited fiber is never switched to again. */
}

/* Registers for a fiber's first run, from the context that starts it. */
static void fiber_prepare(IcoFiber* f, const ppu_context* starter)
{
    if (f->prepared) return;
    memset(&f->ctx, 0, sizeof(f->ctx));
    uint32_t sp = ((f->stack_ea + f->stack_size) & ~0xFu) - 0x80u;
    memset(vm_base + sp, 0, 0x80);                     /* back chain 0, empty frame */
    f->ctx.gpr[1]  = sp;
    f->ctx.gpr[2]  = f->toc;
    f->ctx.gpr[3]  = f->arg;
    f->ctx.gpr[13] = starter ? starter->gpr[13] : 0;
    f->ctx.thread_id = starter ? starter->thread_id : 0;
    f->ctx.cia = f->code;
    f->prepared = true;
}

/* Enter fiber g on this thread. The caller has saved what it is leaving. */
static void fiber_enter(IcoFiber* g)
{
    if (g->started) lifted_restore(g->saved);
    else lifted_restore(IcoLiftedState{ &g->ctx, nullptr, 0, 0 });
    t_cur = g;
    SwitchToFiber(g->host);
}

static const uint64_t ICO_FIBER_ERR_INVAL = (uint64_t)(int64_t)(s32)0x80760002u;
static const uint64_t ICO_FIBER_ERR_PERM  = (uint64_t)(int64_t)(s32)0x80760009u;

static uint64_t ico_FiberPpuInitialize(void) { return CELL_OK; }

/* _cellFiberPpuContextAttributeInitialize(attr, sdkVersion): ICO fills the
 * name at +0x10 and a flag at +0x30 afterwards. */
static uint64_t ico_FiberPpuContextAttributeInitialize(uint64_t attr, uint64_t)
{
    if (!attr) return ICO_FIBER_ERR_INVAL;
    memset(vm_base + (uint32_t)attr, 0, 0x40);
    return CELL_OK;
}

/* cellFiberPpuContextInitialize(context, entry, arg, eaStack, sizeStack, attr) */
static uint64_t ico_FiberPpuContextInitialize(uint64_t context, uint64_t entry, uint64_t arg,
                                              uint64_t stack, uint64_t size, uint64_t)
{
    const uint32_t ea = (uint32_t)context;
    if (!ea || !entry || !stack || !size) return ICO_FIBER_ERR_INVAL;
    if (ea & 0xFu) return ICO_FIBER_ERR_INVAL;
    uint32_t code = 0, toc = 0;
    ppu_opd_resolve((uint32_t)entry, &code, &toc);
    if (!code) return ICO_FIBER_ERR_INVAL;

    IcoFiber* f = new IcoFiber();
    f->ea = ea; f->code = code; f->toc = toc; f->arg = arg;
    f->stack_ea = (uint32_t)stack; f->stack_size = (uint32_t)size;
    f->host = CreateFiberEx(64 * 1024, 64u * 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, fiber_main, f);
    if (!f->host) { delete f; return ICO_FIBER_ERR_INVAL; }
    {
        std::lock_guard<std::mutex> g(s_fib_lock);
        IcoFiber*& slot = s_fibers[ea];
        /* Re-initialising the context that is running right now: its host
         * fiber and record are in use by this very call, so they are left
         * alive (a small leak) rather than freed under it. */
        if (slot && slot != t_cur) {
            if (slot->host) DeleteFiber(slot->host);
            delete slot;
        }
        slot = f;
    }
    static int logged;
    if (logged++ < 16)
        fprintf(stderr, "[ico] fiber init ctx=0x%08X entry=0x%08X (code 0x%08X) arg=0x%016llX stack=0x%08X+0x%X\n",
                ea, (uint32_t)entry, code, (unsigned long long)arg, (uint32_t)stack, (uint32_t)size);
    return CELL_OK;
}

static uint64_t ico_FiberPpuContextFinalize(uint64_t context)
{
    std::lock_guard<std::mutex> g(s_fib_lock);
    auto it = s_fibers.find((uint32_t)context);
    if (it == s_fibers.end()) return CELL_OK;
    IcoFiber* f = it->second;
    if (f == t_cur) return ICO_FIBER_ERR_PERM;
    if (f->host) DeleteFiber(f->host);
    delete f;
    s_fibers.erase(it);
    return CELL_OK;
}

/* cellFiberPpuContextRun(context, int* cause, CellFiberPpuContext** fiberFrom, option) */
static uint64_t ico_FiberPpuContextRun(uint64_t context, uint64_t cause, uint64_t fiberFrom, uint64_t)
{
    IcoFiber* f = fiber_find((uint32_t)context);
    if (!f || f->exited) return ICO_FIBER_ERR_INVAL;
    if (t_cur) return ICO_FIBER_ERR_PERM;              /* only a thread runs a fiber */
    if (!t_home) t_home = IsThreadAFiber() ? GetCurrentFiber()
                                           : ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!t_home) return ICO_FIBER_ERR_PERM;

    fprintf(stderr, "[ico] fiber Run ctx=0x%08X from guest-tid %u\n", f->ea,
            g_active_ctx ? (unsigned)g_active_ctx->thread_id : 0u);
    lifted_save(t_thread_saved);
    fiber_prepare(f, g_active_ctx);
    f->from = 0;
    fiber_enter(f);

    /* Back on the thread: a fiber's entry returned. */
    lifted_restore(t_thread_saved);
    if ((uint32_t)cause)     vm_write32((uint32_t)cause, 0);
    if ((uint32_t)fiberFrom) vm_write32((uint32_t)fiberFrom, t_exit_from);
    fprintf(stderr, "[ico] fiber Run returned (fiber 0x%08X exited)\n", t_exit_from);
    return CELL_OK;
}

/* cellFiberPpuContextSwitch(context, CellFiberPpuContext** fiberFrom, option) */
static uint64_t ico_FiberPpuContextSwitch(uint64_t context, uint64_t fiberFrom, uint64_t)
{
    IcoFiber* f = t_cur;
    if (!f) return ICO_FIBER_ERR_PERM;                 /* only a fiber switches */
    IcoFiber* g = fiber_find((uint32_t)context);
    if (!g || g->exited) return ICO_FIBER_ERR_INVAL;
    if (g != f) {
        lifted_save(f->saved);
        fiber_prepare(g, &f->ctx);
        g->from = f->ea;
        fiber_enter(g);
        /* Resumed: someone switched back here and restored our state. */
    }
    if ((uint32_t)fiberFrom) vm_write32((uint32_t)fiberFrom, f->from);
    return CELL_OK;
}

/* For main.cpp's ICO_WATCHDOG: where every guest fiber is. */
extern "C" void ico_dump_fibers(void)
{
    std::lock_guard<std::mutex> g(s_fib_lock);
    for (auto& kv : s_fibers) {
        const IcoFiber* f = kv.second;
        fprintf(stderr, "[ICO-WD]   fiber 0x%08X %s lr=0x%08X sp=0x%08X from=0x%08X\n", f->ea,
                f->exited ? "exited" : f->started ? "started" : "new",
                (uint32_t)f->ctx.lr, (uint32_t)f->ctx.gpr[1], f->from);
        /* Guest call chain of a parked fiber: 64-bit PPU ABI with 32-bit
         * addresses, back chain at [sp], caller's LR saved at [back+16]. */
        if (f->started && !f->exited) {
            uint32_t sp = (uint32_t)f->ctx.gpr[1];
            fprintf(stderr, "[ICO-WD]     chain:");
            for (int k = 0; k < 10 && sp && sp < 0xF0000000u; k++) {
                const uint32_t back = g32(sp + 4);
                if (!back || back <= sp) break;
                fprintf(stderr, " 0x%X", g32(back + 16 + 4));
                sp = back;
            }
            fprintf(stderr, "\n");
        }
    }
}

/* For main.cpp's ICO_WATCHDOG: the counters of every queue pushed to so far. */
static uint32_t s_dump_q[8];
static void lfqueue_note(uint32_t q)
{
    for (unsigned i = 0; i < 8; i++) {
        if (s_dump_q[i] == q) return;
        if (!s_dump_q[i]) { s_dump_q[i] = q; return; }
    }
}

extern "C" void ico_dump_lfqueues(void)
{
    for (unsigned i = 0; i < 8 && s_dump_q[i]; i++) {
        const uint32_t q = s_dump_q[i];
        fprintf(stderr, "[ICO-WD]   LFQueue 0x%08X: pop1.h1=%u h4=%u push1.h5=%u h8=%u depth=%u pack30=0x%04X dir=%u\n",
                q, g16(q), g16(q + 6), g16(q + 8), g16(q + 0x0E), g32(q + 0x14), g16(q + 0x30), g32(q + 0x24));
    }
}

/* ICO's audio mixer task (spu_0003, registered as SPU image 4) loads the
 * firmware's ATRAC decoder -- /dev_flash/sys/external/flashATRAC.pic, a PIC SPU
 * ELF linked at 0x80 -- into its local store with a 0x33100 bias (.text 0x90
 * lands at LS 0x33190) and calls into it. Not lifted, the first call ended the
 * job ("branched into unlifted LS 0x33190") and the PPU then waited on the
 * mixer's event flag for good.
 *
 * spu/flashatrac/ is the user's own copy of that module, rebased with
 * tools/rebase_spu_elf.py (+0x33100) and lifted with --code-end 0x385D0 (end
 * of .text). It is firmware-derived code: it stays in this local port tree and
 * is never committed or published. Register it under image 4 so the mixer's
 * indirect branches into the decoder resolve.
 *
 * The same mixer also copies one of the title's own SPU images into a
 * MultiStream "DSP page": spu_0005 (ICO.elf file offset 0x48CC80, PIC, linked
 * at 0x80, entry 0x98) lands at LS 0x30000 and is entered at 0x30098 -- byte
 * for byte the ELF's segment. spu/msdsp_30000/ is that image rebased +0x30000
 * and lifted under its own prefix; the link-address lift stays registered as
 * image 6 for its standalone use. */
static void ico_register_spu_overlays(void)
{
    spu_begin_image(4);
    flashatrac_spu_recomp_register();
    msdsp30000_spu_recomp_register();
    spu_begin_image(0);
}

/* ---------------------------------------------------------------------------
 * _cellSpursSendSignal override (NID 0xE0A6DBE4), installed only when one of
 * these is set:
 *
 * ICO_SKIP_MLAA=1 -- complete ICO's per-frame Edge MLAA job without running
 *   it. The job is the taskset of the PPU<->SPU sync object at
 *   *(TOC - 0x7340) + 0x2C (func_00341E50 queues it: desc[0] = {seq, label EA,
 *   done EA}, done EA = obj+0x2C cleared to 0; func_00341F9C polls obj+0x2C
 *   until it is non-zero; the F5AAC0DE user handler, raised once the RSX has
 *   copied the scene RT to main memory, signals the tasks through here). With
 *   GCM_NV3089_GPU_COPY=1 the MLAA output is never read -- the scene goes RT
 *   to RT on the GPU -- so the job is pure cost: ~38 ms of PPU wait and ~60 ms
 *   of SPU time per frame (report 8an). Here the first signal of a round
 *   writes what the finished job writes (label 0x45 = seq, done word = 1) and
 *   no task is woken. MLAA is lost, which it already was.
 *
 * ICO_QUIET=1 -- deliver every other signal without the runtime's printf.
 *   ~80 lines/s from the MultiStream thread, each blocking on the redirected
 *   stdout: ~30% of that thread and ~21% of the main PPU thread (8an).
 * -----------------------------------------------------------------------*/
static int s_skip_mlaa, s_quiet_signal;
static uint64_t ico_SendSignal(uint64_t taskset, uint64_t taskId)
{
    const uint32_t ts = (uint32_t)taskset, id = (uint32_t)taskId;
    if (s_skip_mlaa && ts) {
        const uint32_t mgr = vm_read32(0x004CFEB0u - 0x7340u);
        const uint32_t obj = mgr ? mgr + 0x2Cu : 0;
        if (obj && vm_read32(obj + 0x8) == ts) {
            const uint32_t ids = vm_read32(obj + 0xC);
            const uint32_t desc = vm_read32(obj + 0x14);
            /* The tasks are not woken. The job itself is completed where the
             * PPU waits on it (main.cpp, usleep at lr 0x461EC): completing it
             * here raced the PPU queueing the next job. */
            if (desc && ids && id == vm_read32(ids)) {
                static int n = 0;
                if (n++ < 3)
                    fprintf(stderr, "[ico] MLAA job %u: tasks not signalled (ICO_SKIP_MLAA)\n",
                            vm_read32(desc + 0x0));
            }
            return CELL_OK;
        }
    }
    if (!s_quiet_signal)
        printf("[cellSpurs] _SendSignal(taskset=0x%08X id=%u)\n", ts, id);
    if (ts) spu_taskset_signal_task(ts, id);
    return CELL_OK;
}

static void ico_register_overrides(void)
{
    ico_register_spu_overlays();
    { const char* a = getenv("ICO_SKIP_MLAA"); const char* b = getenv("ICO_QUIET");
      s_skip_mlaa = a && atoi(a) > 0;
      s_quiet_signal = b && atoi(b) > 0;
      if (s_skip_mlaa || s_quiet_signal) {
          ps3_hle_register(0xE0A6DBE4u, "_cellSpursSendSignal", (void*)ico_SendSignal);
          fprintf(stderr, "[ico] _cellSpursSendSignal override: skip MLAA=%d quiet=%d\n",
                  s_skip_mlaa, s_quiet_signal);
      } }
    ps3_hle_register(0x8A85674Du, "_cellSpursLFQueuePushBody", (void*)ico_LFQueuePushBody);
    ps3_hle_register(0x52CC6C82u, "cellSpursCreateTaskset", (void*)ico_CreateTaskset);
    ps3_hle_register(0xC10931CBu, "cellSpursCreateTasksetWithAttribute",
                     (void*)ico_CreateTasksetWithAttribute);
    ps3_hle_register(0xC2ACDF43u, "_cellSpursTasksetAttribute2Initialize",
                     (void*)ico_TasksetAttribute2Initialize);
    ps3_hle_register(0x4A6465E3u, "cellSpursCreateTaskset2", (void*)ico_CreateTaskset2);
    ps3_hle_register(0x8ADADF65u, "_cellSpursTaskAttribute2Initialize",
                     (void*)ico_TaskAttribute2Initialize);
    ps3_hle_register(0xE14CA62Du, "cellSpursCreateTask2", (void*)ico_CreateTask2);
    ps3_hle_register(0x55870804u, "_cellFiberPpuInitialize", (void*)ico_FiberPpuInitialize);
    ps3_hle_register(0x31252EC3u, "_cellFiberPpuContextAttributeInitialize",
                     (void*)ico_FiberPpuContextAttributeInitialize);
    ps3_hle_register(0x72086315u, "cellFiberPpuContextInitialize", (void*)ico_FiberPpuContextInitialize);
    ps3_hle_register(0xB3A48079u, "cellFiberPpuContextFinalize", (void*)ico_FiberPpuContextFinalize);
    ps3_hle_register(0xABA1C563u, "cellFiberPpuContextRun", (void*)ico_FiberPpuContextRun);
    ps3_hle_register(0xD0066B17u, "cellFiberPpuContextSwitch", (void*)ico_FiberPpuContextSwitch);
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */

/* ---------------------------------------------------------------------------
 * ICO_PROF=<sec>: a sampling profiler for the slowdown. At ~100 Hz every other
 * thread of the process is suspended just long enough to read its RIP, then
 * resumed; nothing is allocated while a thread is suspended (it may hold the
 * heap lock). Every <sec> seconds it prints the flips in the interval and, for
 * each thread that used more than 3% of a core, its CPU share and its most
 * frequent RIPs as module RVAs -- llvm-symbolizer turns those into function
 * names (lifted guest code shows up as func_XXXXXXXX) given the build's PDB.
 * -----------------------------------------------------------------------*/
#include <tlhelp32.h>
#include <vector>
#include <algorithm>

extern "C" unsigned cellGcm_flip_request_count(void);
extern "C" void ico_usleep_report(double secs);   /* main.cpp */
static void ico_usleep_report_c(double s) { ico_usleep_report(s); }

static DWORD WINAPI ico_prof_thread(LPVOID arg)
{
    const DWORD every_ms = (DWORD)(uintptr_t)arg;
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    const uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    struct Th { HANDLE h; ULONGLONG cpu0; std::unordered_map<uint64_t, unsigned> rips; unsigned n; };
    std::unordered_map<DWORD, Th> th;
    auto cpu_of = [](HANDLE h) -> ULONGLONG {
        FILETIME c, e, k, u;
        if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
        return ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
               ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
    };
    unsigned flips0 = cellGcm_flip_request_count();
    ULONGLONG t0 = GetTickCount64(), tstart = t0;
    std::vector<DWORD> ids;
    for (unsigned tick = 0;; tick++) {
        if ((tick % 50) == 0) {          /* refresh the thread list every ~0.5 s */
            ids.clear();
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te; te.dwSize = sizeof(te);
            if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
                do { if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids.push_back(te.th32ThreadID); }
                while (Thread32Next(snap, &te));
            }
            if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
            for (DWORD id : ids)
                if (!th.count(id)) {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
                    if (h) { Th t; t.h = h; t.cpu0 = cpu_of(h); t.n = 0; th.emplace(id, std::move(t)); }
                }
        }
        for (auto& kv : th) {
            CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_FULL;
            if (SuspendThread(kv.second.h) == (DWORD)-1) continue;
            BOOL ok = GetThreadContext(kv.second.h, &c);
            /* A RIP outside the exe (ntdll, kernelbase, the driver) says only
             * "waiting"; unwind -- still suspended, so the stack is stable --
             * to the first exe frame, which says what is waiting. Tagged with
             * bit 63 so the report can mark it. */
            uint64_t key = (uint64_t)c.Rip;
            if (ok && !(c.Rip >= base && c.Rip < base + 0x40000000ull)) {
                CONTEXT u = c;
                for (int f = 0; f < 24; f++) {
                    DWORD64 ib = 0;
                    PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(u.Rip, &ib, NULL);
                    if (!rf) {                       /* leaf: return address at [rsp] */
                        if (!u.Rsp) break;
                        u.Rip = *(DWORD64*)u.Rsp; u.Rsp += 8;
                    } else {
                        PVOID hd = NULL; DWORD64 ef = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, u.Rip, rf, &u, &hd, &ef, NULL);
                    }
                    if (!u.Rip) break;
                    if (u.Rip >= base && u.Rip < base + 0x40000000ull) { key = (uint64_t)u.Rip | (1ull << 63); break; }
                }
            }
            ResumeThread(kv.second.h);
            if (ok) { kv.second.rips[key]++; kv.second.n++; }
        }
        Sleep(10);
        const ULONGLONG now = GetTickCount64();
        if (now - t0 < every_ms) continue;
        const unsigned flips = cellGcm_flip_request_count();
        const double secs = (double)(now - t0) / 1000.0;
        fprintf(stderr, "[ICO-PROF] t=%.0fs interval=%.1fs flips=+%u (%.2f fps)\n",
                (double)(now - tstart) / 1000.0, secs, flips - flips0, (flips - flips0) / secs);
        struct Row { DWORD id; double pct; };
        std::vector<Row> rows;
        for (auto& kv : th) {
            const ULONGLONG c1 = cpu_of(kv.second.h);
            const double pct = 100.0 * (double)(c1 - kv.second.cpu0) / 1e7 / secs;
            kv.second.cpu0 = c1;
            rows.push_back({kv.first, pct});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.pct > b.pct; });
        for (const Row& r : rows) {
            if (r.pct < 3.0) break;
            Th& t = th[r.id];
            std::vector<std::pair<unsigned, uint64_t>> top;
            for (auto& rp : t.rips) top.push_back({rp.second, rp.first});
            std::sort(top.begin(), top.end(), [](const std::pair<unsigned, uint64_t>& a,
                                                 const std::pair<unsigned, uint64_t>& b) { return a.first > b.first; });
            fprintf(stderr, "[ICO-PROF]   tid=%lu cpu=%.0f%% samples=%u top:", (unsigned long)r.id, r.pct, t.n);
            static int s_top = -1;   /* ICO_PROF_TOP=<n>: addresses per thread (default 5) */
            if (s_top < 0) { const char* e = getenv("ICO_PROF_TOP"); s_top = e ? atoi(e) : 5; if (s_top < 1) s_top = 5; }
            for (size_t i = 0; i < top.size() && i < (size_t)s_top; i++) {
                const uint64_t rip = top[i].second & ~(1ull << 63);
                const char* via = (top[i].second >> 63) ? "dll<" : "";
                if (rip >= base && rip < base + 0x40000000ull)
                    fprintf(stderr, " %sexe+0x%llX(%u)", via, (unsigned long long)(rip - base), top[i].first);
                else
                    fprintf(stderr, " 0x%llX(%u)", (unsigned long long)rip, top[i].first);
            }
            fprintf(stderr, "\n");
        }
        ico_usleep_report_c(secs);
        report_pushers(secs);
        for (auto& kv : th) { kv.second.rips.clear(); kv.second.n = 0; }
        fflush(stderr);
        flips0 = flips; t0 = now;
    }
    return 0;
}

extern "C" void ico_prof_start(unsigned every_sec)
{
    if (every_sec) CreateThread(NULL, 0, ico_prof_thread, (LPVOID)(uintptr_t)(every_sec * 1000u), 0, NULL);
}
