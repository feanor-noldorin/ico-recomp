# 🏰 ICO — PC Port (Static Recompilation)

**English** · [Português (Brasil)](README.pt-BR.md)

> **Status — v0.1.0 (alpha):** boots to the title menu, plays the opening
> cinematic and reaches gameplay on Windows at ~48–60 fps in the 3D scenes
> (development PC). Expect bugs. See [Current status](#-current-status) and
> [Known issues](#-known-issues).

A native Windows build of **ICO** (PS3, *ICO & Shadow of the Colossus
Collection*), made by statically recompiling the game's PowerPC (PPU) and
SPU code to C/C++ with [ps3recomp](https://github.com/sp00nznet/ps3recomp).

**No game files are included.** You select your own disc dump and the
installer recompiles it into a PC game **on your machine**.

---

## 📺 What's in the box

| Component | What it is |
|---|---|
| **`ICO.PC.Port.Setup.exe`** | The installer ([Releases](../../releases)). One self-contained exe: select your dump, it checks, decrypts (via your RPCS3), recompiles and installs. |
| `pipeline/` | `ico_pipeline.py` — the recompilation steps the installer runs, and `ico_profile.json` with the tested hashes and code cut points. |
| `port/` | The ICO-specific runner (`main.cpp`), HLE overrides (`stubs.cpp`) and CMake project. |
| `external/ps3recomp` | Submodule: the ps3recomp toolkit, on the branch with this port's runtime fixes. |
| `installer/` | The installer's source (C#, .NET, WinForms). |
| `docs/` | [Decrypting](docs/DECRYPTING.md), [run settings](docs/ENVIRONMENT.md), [building from source](docs/BUILDING.md). |

---

## 🚦 Current status

| Area | State | Notes |
|---|---|---|
| Boot → title menu | ✅ Works | Fibers, SPURS v2 tasksets, LFQueue, SPU overlays all stable. |
| Opening cinematic | ✅ Works | Full post chain: depth fog, sepia, bloom, letterbox. |
| Gameplay | 🟡 Reached | Long sessions are not tested yet. |
| Graphics (D3D12) | 🟡 Good | No known geometry bugs; no anti-aliasing (MLAA is skipped). |
| Audio | 🟡 Mostly | Music and effects play; short glitches can happen when a track starts. |
| Controller | 🟡 Works in menus | XInput (Xbox-compatible) pads. Character control in gameplay still needs confirming. |
| Keyboard | 🟡 Basic | A fixed fallback map when no controller is connected (see [Controls](#-controls)). Full keyboard + mouse is planned. |
| Performance | 🟢 ~48–60 fps | 3D scenes on the development PC with the AVX2 build. |
| Saves | ⚪ Untested | Stored under `<install>\vfs`. |
| Graphics settings menu | ❌ Not yet | Fixed 1280×720 window. Planned: resolution, vsync, AF, FPS limit, upscaling. |

Legend: ✅ working · 🟢 good · 🟡 partial · ⚪ untested · ❌ missing

---

## ⬇️ Installing

### Requirements

| You need | Why | Notes |
|---|---|---|
| **Windows 10/11 x64** | | |
| **A GPU with Direct3D 12** | Rendering | |
| **CPU with SSE4.1** | | AVX2 + FMA (Intel Haswell / AMD Zen or newer) gets the faster build. |
| **Your ICO disc dump** | Game data | *ICO & Shadow of the Colossus Collection*, **USA (BCUS98259)**, the `PS3_GAME` folder. |
| **[RPCS3](https://rpcs3.net) with the PS3 firmware installed** | Decrypts `ICO.self`; its `dev_flash` provides the ATRAC audio codec | **File → Install Firmware** in RPCS3. Tested with firmware 4.91. |
| Visual Studio Build Tools (C++, Clang, CMake) | Compiles the game | The installer offers to install it (≈6 GB, through winget). |
| Python 3.10+ | Runs the recompiler | The installer offers to install it. |
| ~5 GB free disk | Build + copied data | ~3 GB if you link to the dump instead of copying. |

### Steps

1. **Dump your disc** with your own PS3 (e.g. with multiMAN or a Blu-ray
   drive that reads PS3 discs). You need the `PS3_GAME` folder.
2. **Install RPCS3** and its **PS3 firmware** (File → Install Firmware).
3. **Download `ICO.PC.Port.Setup.exe`** from [Releases](../../releases) and run it.
4. In the installer:
   1. **Your game** — select the `PS3_GAME` folder (or the dump folder that contains it).
   2. **Decrypt ICO.self** — keep *"Decrypt automatically with my RPCS3"* and point it
      at `rpcs3.exe`, **or** choose *"I already decrypted it"* and select your `ICO.elf`
      ([how to decrypt by hand](docs/DECRYPTING.md)).
   3. **PS3 firmware** — the `dev_flash` folder of RPCS3 (filled in automatically
      when it sits next to `rpcs3.exe`).
   4. **Installation folder** — a short path, e.g. `C:\Games`. Setup creates `ICO` inside it.
   5. **Build tools** — if anything shows ✖, click **Install...** and confirm.
5. Click **Install**. The first install takes **6–30 minutes** depending on the PC; most of
   it is compiling. *Show details* shows the full log, also saved to
   `<install>\logs\install.log`.
6. Click **Play**, or use the **ICO** shortcut on the desktop / Start menu. The installed game is `ico.exe`.

To **update**, run the new installer and select the same installation
folder. Your `ico.ini` edits and saves are kept.
To **uninstall**, delete the installation folder and the shortcuts.

---

## 🎮 Controls

An XInput (Xbox-compatible) controller works out of the box. Without one,
the keyboard fallback is:

| PS3 | Keyboard | | PS3 | Keyboard |
|---|---|---|---|---|
| D-pad / left stick | Arrow keys | | L1 / R1 | Q / W |
| ✕ Cross | Z | | L2 / R2 | 1 / 2 |
| ◯ Circle | X | | Start | Enter |
| ▢ Square | A | | Select | Tab |
| △ Triangle | S | | | |

Run settings live in `ico.ini` next to `ico.exe`; see
[docs/ENVIRONMENT.md](docs/ENVIRONMENT.md). The game's log is
`<install>\logs\ico.log` — attach it to bug reports.

---

## 🐞 Known issues

- **Closing the window may not end the process.** If `ico.exe` stays in Task
  Manager after you close it, end it there before starting again.
- **Rare crash at start-up**: in about 1 of 3 test launches the game closed
  within its first seconds (Windows reports `CoreMessaging.dll`, exception
  `0xe0464645`). Start it again; it is being investigated.
- **Only the US release** (BCUS98259) is supported. Other regions fail the
  hash check: the code cut points are specific to this executable.
- **Short audio glitches** can happen when a music track starts.
- **No anti-aliasing**: the game's SPU Edge MLAA is skipped for speed
  (`ICO_SKIP_MLAA=1`); a PC AA/upscaler is planned.
- **Fixed 1280×720** window, no graphics menu yet.
- **One CPU core stays busy** while the game runs (the SPU renderer polls for work).
- **The first install is long** (6–30 min) and needs ~6 GB of Visual Studio
  Build Tools. A future release may ship more of the runtime prebuilt.
- **Only Windows** (Direct3D 12).

Found something else? [Open an issue](../../issues) with your
`logs\ico.log` and `logs\install.log`.

---

## 🆕 What's new in 0.1.0

First public build. Highlights (full list in [CHANGELOG.md](CHANGELOG.md)):

- **Installer** in the style of other recomp ports: pick your dump, automatic
  RPCS3 decryption or your own `ICO.elf`, build-tools check, progress bar, shortcuts.
- **Graphics**: the Direct3D 12 backend of the shared RSX draw engine — no
  more black screen, white flashes, missing geometry or washed-out colours.
- **Performance**: < 1 fps → ~48–60 fps in 3D (shader caches, PPU
  re-lift with `--code-end`, AVX2 SPU math, MLAA skipped).
- **Audio**: firmware ATRAC codec and the MultiStream DSP overlay recompiled.

---

## 🛠️ How it works

```
 your PS3_GAME ──► ICO.self ──(your RPCS3 --decrypt)──► ICO.elf
                                                         │
             ┌───────────────────────────────────────────┤
             ▼                                           ▼
   ppu_loader + ppu_lifter                       extract_spu_images
   (--code-end 0x3BC7B4)                         spu_lifter × 6 (per-image --code-end)
   gen_hle_nids --all                            + MultiStream DSP overlay (+0x30000)
             │                                   + flashATRAC.pic from YOUR dev_flash (+0x33100)
             │                                   build_spu_workloads
             └──────────────┬────────────────────────────┘
                            ▼
         CMake + Ninja + clang-cl  ◄── port/ (main.cpp, stubs.cpp) + ps3recomp runtime
                            ▼
                <install>\ico.exe  +  ico.ini  +  vfs\PS3_GAME (your data)
```

| Stage | Tool (ps3recomp `tools/`) | Output |
|---|---|---|
| Load | `ppu_loader.py` | function table, imports, segments |
| PPU lift | `ppu_lifter.py --code-end 0x3BC7B4` | ~64 MB of C++ (2 chunks) |
| HLE table | `gen_hle_nids.py --all` | firmware NID → handler map |
| SPU | `extract_spu_images.py`, `spu_lifter.py`, `rebase_spu_elf.py`, `build_spu_workloads.py` | 6 images + 2 overlays, registry |
| Build | CMake / Ninja / clang-cl | `ico.exe` |

Everything the pipeline needs to know about the supported release — file
hashes, the PPU/SPU code ends, SPU overlay bases and extra entry points, the
run settings — is in [`pipeline/ico_profile.json`](pipeline/ico_profile.json).

---

## 📦 Building from source

For developers. See [docs/BUILDING.md](docs/BUILDING.md) for the details.

```bash
git clone --recursive https://github.com/feanor-noldorin/ico-recomp.git
cd ico-recomp
python -m venv .venv
.venv\Scripts\pip install -r external\ps3recomp\requirements.txt
.venv\Scripts\python pipeline\ico_pipeline.py --game-dir "D:\dumps\ICO\PS3_GAME" --rpcs3 C:\RPCS3\rpcs3.exe --dev-flash C:\RPCS3\dev_flash --install C:\Games\ICO --python .venv\Scripts\python.exe
```

Building the installer exe: `powershell -File tools\make_release.ps1`.

---

## 🗺️ Roadmap

1. Confirm gameplay end to end (controls, saves, long sessions).
2. Keyboard + mouse, alongside the controller.
3. Graphics settings menu: resolution, vsync, anisotropic filtering, FPS limiter.
4. Modern AA / upscaling (FSR, DLSS).
5. Upstream the runtime fixes to ps3recomp.

---

## ⚖️ Legal

This repository and its releases contain **no game code, game assets, PS3
firmware or keys**. You must own the game and dump it yourself. The game is
recompiled on your PC from your files; the result is for your personal use
and must not be redistributed. Decryption is done by your own copy of RPCS3.

ICO is © Sony Interactive Entertainment. This project is not affiliated with
or endorsed by Sony or Team ICO / genDESIGN. The code in this repository is
under the [MIT License](LICENSE). The icon of the installed `ico.exe` is not
part of this repository either: the installer cuts it from the `ICON0.PNG` of
your own copy.

---

## 🔗 Related projects

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) (upstream; this port uses the [`ico-port` branch of the fork](https://github.com/feanor-noldorin/ps3recomp/tree/ico-port)) — the PS3 static recompilation toolkit this port is built on.
- [shadowofthecolossus](https://github.com/sp00nznet/shadowofthecolossus) — sister port from the same collection.
- [saintsrow2](https://github.com/sp00nznet/saintsrow2) — another ps3recomp port.
- [RPCS3](https://rpcs3.net) — PS3 emulator; used here to decrypt the executable and provide the firmware.
