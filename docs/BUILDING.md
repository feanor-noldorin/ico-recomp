# 📦 Building from source

The installer only wraps `pipeline/ico_pipeline.py`; you can run the same
steps by hand. Everything here runs on Windows x64.

## Prerequisites

| Tool | Notes |
|---|---|
| Git | `git clone --recursive` (the ps3recomp submodule) |
| Visual Studio 2022/2026 or Build Tools | Components: *MSVC x64 tools*, *C++ Clang compiler for Windows*, *C++ CMake tools* |
| Python 3.10+ | A venv with `external/ps3recomp/requirements.txt` |
| RPCS3 + PS3 firmware | To decrypt `ICO.self` and for `dev_flash` |
| .NET 10 SDK | Only to build the installer |

## Recompiling the game

```bash
git clone --recursive https://github.com/feanor-noldorin/ico-recomp.git
cd ico-recomp
python -m venv .venv
.venv\Scripts\pip install -r external\ps3recomp\requirements.txt
.venv\Scripts\python pipeline\ico_pipeline.py ^
    --game-dir "D:\dumps\ICO\PS3_GAME" ^
    --rpcs3 C:\RPCS3\rpcs3.exe ^
    --dev-flash C:\RPCS3\dev_flash ^
    --install C:\Games\ICO ^
    --python .venv\Scripts\python.exe
```

| Option | Meaning |
|---|---|
| `--game-dir` | `PS3_GAME` folder of the dump (or the folder containing it) |
| `--rpcs3` / `--elf` | Decrypt `ICO.self` with RPCS3, **or** use an `ICO.elf` you decrypted |
| `--dev-flash` | RPCS3's `dev_flash` (or the RPCS3 folder) |
| `--install` | Output folder: `ico.exe`, `ico.ini`, `data\`, `vfs\`, `logs\` |
| `--work` | Build folder (default `<install>\build`; safe to delete afterwards) |
| `--link-mode copy\|junction` | Copy the ~1.7 GB ICO uses (default) or link `vfs\PS3_GAME` to the dump |
| `--simd avx2\|sse41` | Instruction set of the build (default `avx2`, needs AVX2 + FMA) |
| `--vs` | Visual Studio path (default: found with `vswhere`) |
| `--jobs` | Parallel compile jobs (default: CPU count − 1) |
| `--allow-unknown` | Continue when a hash does not match the tested release (expect breakage) |

On the development PC the whole pipeline takes about 6 minutes; slower CPUs
can take 30 minutes or more.

The output lines starting with `@@` (`@@STEP i/n`, `@@PROGRESS pct`,
`@@WARN`, `@@ERROR`, `@@DONE`) are what the installer parses.

## Working on the port

`port/` is a normal CMake project. After one pipeline run, the lifted sources
are in `<work>\port\recompiled` and `<work>\port\spu`; rebuild with:

```bash
cmake --build <work>\build --parallel
```

from a *x64 Native Tools* prompt with the VS bundled `clang-cl` on `PATH`.
Edit `port/main.cpp` / `port/stubs.cpp` in the repo and copy them into
`<work>\port`, or point a CMake build of `port/` at an existing lift with
`-DRECOMP_DIR=<work>\port\recompiled -DSPU_DIR=<work>\port\spu`.

Runtime fixes belong in the ps3recomp submodule (`external/ps3recomp`,
branch `ico-port`).

## Building the installer

```powershell
powershell -ExecutionPolicy Bypass -File tools\make_release.ps1
```

This packs `pipeline\`, `port\`, `docs\` and the committed ps3recomp tree
into `build\payload.zip` (using `git archive`, so only committed files go
in), embeds it, and publishes `build\release\ICO.PC.Port.Setup.exe` with its
SHA-256. The first publish downloads the .NET runtime packs from NuGet.

For a quick test without a payload:

```powershell
dotnet run --project installer\ICOSetup -- --source . --ps3recomp external\ps3recomp
```

## What must never be committed

The `.gitignore` blocks it, but to be explicit: no game files (`*.self`,
`*.elf`, `*.psarc`, `PARAM.SFO`, dumps), no firmware (`dev_flash`, `*.pic`),
and no lifter output (`recompiled/`, `spu/`, `*.functions.json`, …) — all of
these are derived from the game or the firmware and are produced on the
user's PC.
