#!/usr/bin/env python3
"""ico_pipeline.py -- turn the user's own copy of ICO into a native Windows build.

The installer runs this script; it can also be run by hand. It never ships or
downloads game data: every input below comes from the user's machine.

Inputs
  --game-dir   the PS3_GAME folder of the user's disc dump
  --elf        the decrypted USRDIR/ICO.elf (or --rpcs3 to decrypt ICO.self
               with the user's own RPCS3: `rpcs3 --no-gui --decrypt`)
  --dev-flash  RPCS3's dev_flash folder (the user's installed PS3 firmware;
               the ATRAC codec flashATRAC.pic is lifted from it)

Steps (each prints a machine-readable "@@STEP i/n name" line for the installer)
  1  check inputs (SHA-256 against pipeline/ico_profile.json)
  2  stage game files into <install>/vfs/PS3_GAME (copy or junction)
  3  ppu_loader      -> function table, imports
  4  ppu_lifter      -> recompiled PPU C++ (--code-end from the profile)
  5  gen_hle_nids    -> firmware NID -> HLE handler table
  6  SPU images      -> extract, lift each with its --code-end
  7  SPU overlays    -> MultiStream DSP (+0x30000) and flashATRAC (+0x33100)
  8  SPU registry    -> build_spu_workloads --skip-lift
  9  configure       -> CMake + Ninja + clang-cl (Visual Studio toolchain)
  10 compile
  11 install         -> ico.exe + ico.ini
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
STEPS = ["Checking your files", "Preparing game data", "Loading ICO.elf",
         "Recompiling PPU code", "Generating HLE table", "Recompiling SPU programs",
         "Recompiling SPU overlays", "Registering SPU programs", "Configuring build",
         "Compiling (this is the long part)", "Installing"]


class PipelineError(Exception):
    pass


# ---------------------------------------------------------------- reporting --

def step(i: int) -> None:
    print(f"@@STEP {i}/{len(STEPS)} {STEPS[i - 1]}", flush=True)


def progress(pct: float) -> None:
    print(f"@@PROGRESS {pct:.1f}", flush=True)


def log(msg: str) -> None:
    print(msg, flush=True)


def warn(msg: str) -> None:
    print(f"@@WARN {msg}", flush=True)


def rmtree(p: Path) -> None:
    """shutil.rmtree that survives Windows' transient locks (antivirus, indexer)
    and read-only files: retry a few times before giving up."""
    def onerr(func, path, *_):
        try:
            os.chmod(path, stat.S_IWRITE)
            func(path)
        except OSError:
            pass
    for attempt in range(6):
        if not p.exists():
            return
        shutil.rmtree(p, onerror=onerr)
        if not p.exists():
            return
        time.sleep(0.5 * (attempt + 1))
    raise PipelineError(f"Could not delete {p}: a file in it is in use. Close any program using it and retry.")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(cmd: list, cwd: Path | None = None, env: dict | None = None,
        on_line=None) -> None:
    """Run a command, streaming its output; raise on failure."""
    log("> " + " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd))
    p = subprocess.Popen([str(c) for c in cmd], cwd=cwd, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding="utf-8", errors="replace",
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    assert p.stdout
    for line in p.stdout:
        line = line.rstrip()
        log(line)
        if on_line:
            on_line(line)
    if p.wait() != 0:
        raise PipelineError(f"{Path(str(cmd[1] if len(cmd) > 1 else cmd[0])).name} failed (exit {p.returncode})")


# ------------------------------------------------------------------ toolchain --

def find_vs(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise PipelineError("Visual Studio Build Tools were not found (no vswhere.exe).")
    out = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                          "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                          "Microsoft.VisualStudio.Component.VC.Llvm.Clang",
                          "Microsoft.VisualStudio.Component.VC.CMake.Project",
                          "-property", "installationPath"],
                         capture_output=True, text=True).stdout.strip().splitlines()
    if not out:
        raise PipelineError("Visual Studio with the C++ tools and the Clang component was not found.")
    return Path(out[0])


def vs_env(vs: Path) -> dict:
    """The environment vcvars64.bat produces, plus the bundled clang-cl."""
    vcvars = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.exists():
        raise PipelineError(f"{vcvars} is missing.")
    out = subprocess.run(f'cmd /s /c ""{vcvars}" >nul && set"', capture_output=True,
                         text=True, shell=True).stdout
    env = {}
    for line in out.splitlines():
        k, sep, v = line.partition("=")
        if sep:
            env[k] = v
    if "VCToolsInstallDir" not in env:
        raise PipelineError("vcvars64.bat did not set up the compiler environment.")
    llvm = vs / "VC" / "Tools" / "Llvm" / "x64" / "bin"
    env["PATH"] = f"{llvm};{env.get('PATH', env.get('Path', ''))}"
    return env


# ------------------------------------------------------------------- stages --

def check_inputs(a, prof) -> Path:
    gd = a.game_dir
    if (gd / "PS3_GAME").is_dir():          # the user picked the dump root
        gd = gd / "PS3_GAME"
    for rel in prof["game_files"]["required"]:
        if not (gd / rel).exists():
            raise PipelineError(f"{rel} was not found in {gd}. Select the PS3_GAME folder of your ICO dump.")
    a.game_dir = gd

    work_elf = a.work / "input" / "ICO.elf"
    work_elf.parent.mkdir(parents=True, exist_ok=True)
    if a.elf:
        shutil.copyfile(a.elf, work_elf)
    else:
        if not a.rpcs3:
            raise PipelineError("No decrypted ICO.elf and no RPCS3 to decrypt ICO.self with.")
        self_src = gd / prof["game_files"]["self"]
        if not self_src.exists():
            raise PipelineError(f"{self_src} is missing.")
        stage = a.work / "input" / "ICO.self"
        shutil.copyfile(self_src, stage)    # decrypt a copy: never write into the dump
        log("Decrypting ICO.self with your RPCS3 ...")
        run([a.rpcs3, "--no-gui", "--decrypt", stage])
        if not work_elf.exists():
            raise PipelineError("RPCS3 did not produce ICO.elf. Decrypt it by hand (see docs/DECRYPTING.md).")
    got = sha256(work_elf)
    if got != prof["game_files"]["elf_sha256"]:
        msg = (f"ICO.elf SHA-256 is {got}, not the tested US release "
               f"({prof['game_files']['elf_sha256']}).")
        if not a.allow_unknown:
            raise PipelineError(msg + " Only the US release (BCUS98259) is supported.")
        warn(msg)

    df = a.dev_flash
    if (df / "dev_flash").is_dir():         # the user picked the RPCS3 folder
        df = df / "dev_flash"
    a.dev_flash = df
    for ov in prof["spu_overlays"]:
        if ov["source"].startswith("dev_flash:"):
            p = df / ov["source"].split(":", 1)[1]
            if not p.exists():
                raise PipelineError(f"{p} is missing. Install the PS3 firmware in RPCS3 (File > Install Firmware).")
            if ov.get("sha256") and sha256(p) != ov["sha256"]:
                warn(f"{p.name} differs from firmware {ov['firmware']}; audio may not work.")
    return work_elf


def is_link(p: Path) -> bool:
    """True for a symlink or an NTFS junction (os.path.isjunction is 3.12+)."""
    if p.is_symlink():
        return True
    try:
        return bool(os.lstat(p).st_file_attributes & 0x400)   # FILE_ATTRIBUTE_REPARSE_POINT
    except (OSError, AttributeError):
        return False


def stage_game(a, prof) -> None:
    dst = a.install / "vfs" / "PS3_GAME"
    dst.parent.mkdir(parents=True, exist_ok=True)
    if is_link(dst):
        os.rmdir(dst)                       # removes the link only, never the dump behind it
    if a.link_mode == "junction":
        if dst.exists():
            raise PipelineError(f"{dst} already holds copied game files; choose 'copy' or remove it first.")
        subprocess.run(["cmd", "/c", "mklink", "/J", str(dst), str(a.game_dir)], check=True,
                       capture_output=True)
        log(f"Linked {dst} -> {a.game_dir}")
        return
    items = prof["game_files"]["copy"]
    files = []
    for rel in items:
        src = a.game_dir / rel
        if src.is_dir():
            files += [p for p in src.rglob("*") if p.is_file()]
        elif src.exists():
            files.append(src)
    total = sum(p.stat().st_size for p in files) or 1
    done = 0
    for p in files:
        rel = p.relative_to(a.game_dir)
        out = dst / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        if not (out.exists() and out.stat().st_size == p.stat().st_size):
            shutil.copyfile(p, out)
        done += p.stat().st_size
        progress(100.0 * done / total)
    log(f"Copied {len(files)} files ({total / 2**30:.2f} GB) to {dst}")


def lift_ppu(a, prof, py, tools, elf, port) -> None:
    out = a.work / "out"
    run([py, tools / "ppu_loader.py", elf, "-o", out])
    step(4)
    rec = port / "recompiled"
    if rec.exists():
        rmtree(rec)
    run([py, tools / "ppu_lifter.py", elf,
         "--functions", out / "ICO.functions.json",
         "--hle-stubs", out / "ICO.imports.json",
         "--code-end", prof["ppu"]["code_end"],
         "-o", rec])
    step(5)
    run([py, tools / "gen_hle_nids.py", "--all", "--out", rec / "ppu_hle_nids.cpp"])


_INC_RE = re.compile(r'#include\s+"(?:\.\./)+runtime/spu/([A-Za-z0-9_]+\.h)"')


def debase_includes(d: Path) -> None:
    """Same rewrite build_spu_workloads.py applies: bare runtime/spu includes."""
    for f in d.glob("*.[ch]"):
        txt = f.read_text(encoding="utf-8", errors="ignore")
        new = _INC_RE.sub(r'#include "\1"', txt)
        if new != txt:
            f.write_text(new, encoding="utf-8", newline="\n")


def spu_lift(py, tools, elf: Path, outdir: Path, prefix: str, code_end, extra) -> None:
    if outdir.exists():
        rmtree(outdir)
    outdir.mkdir(parents=True)
    cmd = [py, tools / "spu_lifter.py", elf, "--auto-functions", elf,
           "--symbol-prefix", prefix, "-o", outdir, "--report", outdir / "lift.json"]
    if code_end:
        cmd += ["--code-end", code_end]
    if extra:
        cmd += ["--extra-funcs", extra]
    run(cmd)
    debase_includes(outdir)


def lift_spu(a, prof, py, tools, elf, port) -> None:
    imgs = a.work / "spu_images"
    if imgs.exists():
        rmtree(imgs)
    run([py, tools / "extract_spu_images.py", elf, "--output", imgs])
    spu = port / "spu"
    spu.mkdir(exist_ok=True)
    want = prof["spu_images"]
    for i, img in enumerate(want):
        f = imgs / f"{img['name']}.elf"
        if not f.exists():
            raise PipelineError(f"SPU image {img['name']} was not found in ICO.elf.")
        if sha256(f) != img["sha256"]:
            warn(f"SPU image {img['name']} differs from the tested release.")
        spu_lift(py, tools, f, spu / img["name"], img["name"] + "_", img["code_end"], None)
        progress(100.0 * (i + 1) / len(want))
    extra = sorted(set(p.stem for p in imgs.glob("*.elf")) - {w["name"] for w in want})
    for name in extra:                      # keep the registry's image set exact
        (imgs / f"{name}.elf").unlink()
        warn(f"Ignoring unexpected SPU image {name}.")

    step(7)
    reb = a.work / "spu_rebased"
    reb.mkdir(exist_ok=True)
    for ov in prof["spu_overlays"]:
        kind, ref = ov["source"].split(":", 1)
        src = imgs / f"{ref}.elf" if kind == "spu_image" else a.dev_flash / ref
        tmp = reb / f"{ov['dir']}.elf"
        run([py, tools / "rebase_spu_elf.py", src, tmp, ov["rebase"]])
        spu_lift(py, tools, tmp, spu / ov["dir"], ov["prefix"], ov["code_end"], ov["extra_funcs"])

    step(8)
    run([py, tools / "build_spu_workloads.py", "--images", imgs, "--lifted", spu,
         "--skip-lift", "--out", spu / "spu_workloads.c",
         "--register-fn", "port_spu_register_all", "--constructor", "--title", "ICO"])


def make_icon(a, prof, port) -> None:
    """ico.exe's icon, made from the user's own PS3_GAME/ICON0.PNG (the ICO logo
    band of the collection art). The project ships no game art; without the
    PNG, or if the conversion fails, the exe is built without an icon."""
    ic = prof.get("icon") or {}
    src = a.game_dir / ic.get("source", "ICON0.PNG")
    out = port / "ico.ico"
    try:
        if not src.exists():
            raise PipelineError(f"{src.name} not found")
        run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", HERE / "make_icon.ps1",
             "-Source", src, "-Out", out, "-Band", str(ic.get("band", 0))])
    except Exception as e:                  # noqa: BLE001 -- an icon is never worth failing the install
        warn(f"Could not make the game icon ({e}); ico.exe will have the default icon.")
        rc = port / "app.rc"
        rc.write_text("".join(l for l in rc.read_text(encoding="utf-8").splitlines(True)
                              if " ICON " not in l), encoding="utf-8")


def fetch_sdl2(a, prof) -> Path | None:
    """The official SDL2 development package (controller backend: DualSense,
    DualShock 4). Downloaded once into the build folder and checked by SHA-256;
    --sdl2-zip uses a copy you already have. Returns SDL2's cmake/ folder, or
    None to build with XInput controllers only when it cannot be had."""
    s = prof.get("sdl2")
    if not s:
        return None
    deps = a.work / "deps"
    deps.mkdir(parents=True, exist_ok=True)
    zp = a.sdl2_zip or deps / s["file"]
    try:
        if not zp.exists() or sha256(zp) != s["sha256"]:
            if a.sdl2_zip:
                raise PipelineError(f"{zp} is not {s['file']} (SHA-256 differs).")
            log(f"Downloading {s['file']} ({s['size_mb']} MB) from {s['url']}")
            tmp = zp.with_suffix(".part")
            with urllib.request.urlopen(s["url"], timeout=120) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f)
            if sha256(tmp) != s["sha256"]:
                tmp.unlink()
                raise PipelineError(f"{s['file']}: SHA-256 does not match the expected value.")
            tmp.replace(zp)
        out = deps / s["dir"]
        if not (out / "cmake").exists():
            with zipfile.ZipFile(zp) as z:
                z.extractall(deps)
        return out / "cmake"
    except Exception as e:                  # noqa: BLE001 -- controllers still work through XInput
        warn(f"SDL2 is not available ({e}); building with XInput controllers only "
             "(no DualSense / DualShock 4).")
        return None


def build(a, port, env, sdl2_cmake: Path | None) -> Path:
    bld = a.work / "build"
    flags = "/O2 /Ob2 /DNDEBUG /clang:-msse4.1"
    if a.simd == "avx2":                    # AVX2 + FMA fast paths (runtime/spu/spu_helpers.h)
        flags += " /clang:-mfma /clang:-mavx2"
    run(["cmake", "-S", port, "-B", bld, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl",
         f"-DPS3RECOMP_DIR={a.ps3recomp}",
         f"-DPython3_EXECUTABLE={a.python}",
         "-DPORT_SPU_INPUTS=",
         f"-DCMAKE_C_FLAGS_RELEASE={flags}",
         f"-DCMAKE_CXX_FLAGS_RELEASE={flags}",
         *([f"-DPS3RECOMP_PAD_SDL2=ON", f"-DSDL2_DIR={sdl2_cmake}"] if sdl2_cmake
           else ["-DPS3RECOMP_PAD_SDL2=OFF"])], env=env)

    step(10)
    pat = re.compile(r"^\[(\d+)/(\d+)\]")

    def on_line(line):
        m = pat.match(line)
        if m:
            progress(100.0 * int(m.group(1)) / max(1, int(m.group(2))))
    run(["cmake", "--build", bld, "--parallel", str(a.jobs)], env=env, on_line=on_line)
    exe = bld / "ico.exe"
    if not exe.exists():
        raise PipelineError("The build finished but ico.exe was not produced.")
    return exe


def install(a, prof, exe, elf) -> None:
    a.install.mkdir(parents=True, exist_ok=True)
    for f in a.install.glob("*.exe"):     # an older install named it ICO.exe; keep one exact name
        if f.name.lower() == "ico.exe" and f.name != "ico.exe":
            f.unlink()
    shutil.copyfile(exe, a.install / "ico.exe")
    dll = exe.parent / "SDL2.dll"           # the controller backend, when built with it
    if dll.exists():
        shutil.copyfile(dll, a.install / "SDL2.dll")
    data = a.install / "data"
    data.mkdir(exist_ok=True)
    shutil.copyfile(elf, data / "ICO.elf")
    (a.install / "logs").mkdir(exist_ok=True)
    ini = a.install / "ico.ini"
    values = {"PS3_VFS_ROOT": (a.install / "vfs").as_posix(),
              "PS3_DEV_FLASH": a.dev_flash.as_posix(),
              "ICO_LOG_FILE": "logs/ico.log", **prof["runtime_env"]}
    managed = {"PS3_VFS_ROOT", "PS3_DEV_FLASH"}  # always rewritten from this install
    if ini.exists():                        # an update keeps the user's own edits
        for line in ini.read_text(encoding="utf-8").splitlines():
            k, sep, v = line.partition("=")
            if sep and not line.lstrip().startswith(";") and k.strip() not in managed:
                values[k.strip()] = v.strip()
    ini.write_text("; ICO PC port settings. Each KEY=VALUE becomes an environment variable\n"
                   "; for the game unless it is already set. See docs/ENVIRONMENT.md.\n" +
                   "".join(f"{k}={v}\n" for k, v in values.items()), encoding="utf-8")
    (a.install / "install.json").write_text(json.dumps({
        "version": a.version, "installed": time.strftime("%Y-%m-%d %H:%M:%S"),
        "game_dir": str(a.game_dir), "dev_flash": str(a.dev_flash),
        "link_mode": a.link_mode, "simd": a.simd}, indent=2), encoding="utf-8")
    log(f"Installed to {a.install}")


# --------------------------------------------------------------------- main --

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-dir", type=Path, required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--elf", type=Path, help="decrypted ICO.elf")
    g.add_argument("--rpcs3", type=Path, help="rpcs3.exe used to decrypt ICO.self")
    ap.add_argument("--dev-flash", type=Path, required=True)
    ap.add_argument("--install", type=Path, required=True, help="installation folder")
    ap.add_argument("--work", type=Path, help="build folder (default <install>/build)")
    ap.add_argument("--ps3recomp", type=Path, default=HERE.parent / "external" / "ps3recomp")
    ap.add_argument("--port", type=Path, default=HERE.parent / "port")
    ap.add_argument("--python", type=Path, default=Path(sys.executable),
                    help="interpreter with ps3recomp's requirements installed")
    ap.add_argument("--vs", help="Visual Studio installation path (default: vswhere)")
    ap.add_argument("--sdl2-zip", type=Path, help="SDL2-devel-*-VC.zip you already have (else it is downloaded)")
    ap.add_argument("--link-mode", choices=["copy", "junction"], default="copy")
    ap.add_argument("--simd", choices=["avx2", "sse41"], default="avx2",
                    help="avx2 needs an AVX2+FMA CPU (Haswell / Zen or newer)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--allow-unknown", action="store_true", help="continue on hash mismatch")
    ap.add_argument("--version", default="dev")
    a = ap.parse_args()
    a.install = a.install.resolve()
    a.work = (a.work or a.install / "build").resolve()
    a.ps3recomp = a.ps3recomp.resolve()
    prof = json.loads((HERE / "ico_profile.json").read_text(encoding="utf-8"))
    tools = a.ps3recomp / "tools"
    py = a.python
    t0 = time.time()
    try:
        if not (tools / "ppu_lifter.py").exists():
            raise PipelineError(f"ps3recomp was not found at {a.ps3recomp}.")
        step(1)
        env = vs_env(find_vs(a.vs))
        elf = check_inputs(a, prof)
        step(2)
        stage_game(a, prof)
        step(3)
        port = a.work / "port"              # a private copy: the lift is written into it
        if port.exists():
            rmtree(port)
        shutil.copytree(a.port, port)
        lift_ppu(a, prof, py, tools, elf, port)
        step(6)
        lift_spu(a, prof, py, tools, elf, port)
        step(9)
        make_icon(a, prof, port)
        exe = build(a, port, env, fetch_sdl2(a, prof))
        step(11)
        install(a, prof, exe, elf)
    except PipelineError as e:
        print(f"@@ERROR {e}", flush=True)
        return 1
    except Exception as e:                  # noqa: BLE001 -- report anything to the installer
        print(f"@@ERROR {type(e).__name__}: {e}", flush=True)
        return 1
    print(f"@@DONE {time.time() - t0:.0f}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
