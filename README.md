# 🏰 ICO — PC Port (Static Recompilation)

**English** · [Português (Brasil)](README.pt-BR.md)

> **Status — v0.2.4 (alpha):** boots to the title menu, plays the opening
> cinematic and is playable: Ico walks, climbs and leads Yorda at **60 fps
> with the game's original speed** on the development PC (cinematics stay at
> their original 30). DualSense / DualShock 4 and XInput controllers work.
> Expect bugs. See [Current status](#-current-status) and
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
| Boot → title menu | ✅ Works | Fibers, SPURS v2 tasksets, LFQueue, SPU overlays all stable. The title menu has **Exit Game**. |
| Opening cinematic | ✅ Works | Full post chain: depth fog, sepia, bloom, letterbox. Cinematics run at their original 30 fps. |
| Gameplay | 🟢 Playable | Character control, pause menu, Options, Return to title, Exit Game. Long sessions are not tested yet. |
| Frame rate | 🟢 60 fps | Gameplay at 60 fps with the original game speed (`ICO_60FPS=1`, default); 30 fps in cinematics and scripted events. |
| Graphics (D3D12) | 🟢 Good | Character shadows render. No anti-aliasing (MLAA is skipped). |
| Audio | 🟡 Partial | Music plays; some sound effects are missing. |
| Controller | ✅ Works | DualSense and DualShock 4 (USB or Bluetooth, through SDL2) and XInput (Xbox-compatible) pads. |
| Keyboard | 🟡 Basic | A fixed map that works together with a controller (see [Controls](#-controls)). Full keyboard + mouse is planned. |
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
| Internet during install | Downloads [SDL2](https://www.libsdl.org) 2.32.10 (7 MB, SHA-256 checked) | The controller backend for DualSense / DualShock 4. Without it the game builds with XInput controllers only. |

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

**Controllers:** DualSense (PS5) and DualShock 4 (PS4) work natively, over
USB or Bluetooth, through SDL2 — with the PlayStation button layout the game
was made for. XInput (Xbox-compatible) controllers work too. A DualShock 3
needs a Windows driver that exposes it as a standard controller. Stick
diagonals keep their full speed (`PAD_SQUIRCLE=1`, default).

**Keyboard:** always active, alongside any controller:

| PS3 | Keyboard | | PS3 | Keyboard |
|---|---|---|---|---|
| D-pad / left stick | Arrow keys | | L1 / R1 | Q / W |
| ✕ Cross | Z | | L2 / R2 | 1 / 2 |
| ◯ Circle | X | | Start | Enter |
| ▢ Square | A | | Select | Tab |
| △ Triangle | S | | | |

**Exit Game** is at the bottom of the title menu and of the pause menu.

Run settings live in `ico.ini` next to `ico.exe`; see
[docs/ENVIRONMENT.md](docs/ENVIRONMENT.md). The game's log is
`<install>\logs\ico.log` — attach it to bug reports. The previous run's log
is kept as `ico.prev.log`, so a crash log survives a relaunch.

---

## 🐞 Known issues

- **Long black screen before the first cinematic** (~25 s after the logos):
  the game is loading on the SPU; it is not frozen.
- **Some sound effects are missing**; the music plays.
- **A black screen after skipping a cinematic** can last a few seconds.
- **Moving or resizing the window can break the picture.** Keep the window
  in place for now.
- **Rare crash at start-up** (Windows reports `CoreMessaging.dll`, exception
  `0xe0464645`) was seen in early tests. Start the game again.
- **Only the US release** (BCUS98259) is supported. Other regions fail the
  hash check: the code cut points are specific to this executable.
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

## 🆕 What's new in 0.2.0

Highlights (full list in [CHANGELOG.md](CHANGELOG.md)):

- **60 fps gameplay at the original game speed**; cinematics keep their 30 fps.
- **DualSense / DualShock 4** support (SDL2); the keyboard works together with
  a controller; stick diagonals at full speed.
- **Ico walks**: a recompiler fix for jump tables restored character control.
- **Exit Game** in the title and pause menus, drawn with the game's own font
  and selection effect.
- **Shadows** under the characters; fixes the light band in the middle of the
  screen and the overall dim image.
- **No more freezes** in the frame pacing; closing the window ends the game.
- The installer builds with the correct frame pacing (0.1.0 builds could run
  unlocked and too fast).

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
.venv\Scripts\pip install -r external\ps3recomp\requirements.txt -c pipeline\constraints.txt
.venv\Scripts\python pipeline\ico_pipeline.py --game-dir "D:\dumps\ICO\PS3_GAME" --rpcs3 C:\RPCS3\rpcs3.exe --dev-flash C:\RPCS3\dev_flash --install C:\Games\ICO --python .venv\Scripts\python.exe
```

Building the installer exe: `powershell -File tools\make_release.ps1`.

---

## 🗺️ Roadmap

1. Long play sessions, saves, the missing sound effects.
2. Display scaling to the monitor's resolution, then modern AA / upscaling (FSR, DLSS).
3. Keyboard + mouse, alongside the controller.
4. Graphics settings menu: resolution, vsync, anisotropic filtering, FPS limiter.
5. Upstream the runtime fixes to ps3recomp.

---

## 🤖 AI disclosure

This port was developed with substantial help from an AI assistant
(Anthropic's Claude, in Claude Code): reverse engineering of the game's code
and data, the runtime fixes, the tooling, the installer and this
documentation. A person — the maintainer — directed the work, reviewed every
change and decided what was published. Fixes were checked in the running game,
with automated boot tests and by the maintainer playing it, before each
release. No game code or assets were generated or included.

---

## ⚖️ Legal

This repository and its releases contain **no game code, game assets, PS3
firmware or keys**. You must own the game and dump it yourself. The game is
recompiled on your PC from your files; the result is for your personal use
and must not be redistributed. Decryption is done by your own copy of RPCS3.

ICO is © Sony Interactive Entertainment. This project is not affiliated with
or endorsed by Sony or Team ICO / genDESIGN. The code in this repository is
under the [MIT License](LICENSE). The installer also carries the
[ps3recomp](https://github.com/sp00nznet/ps3recomp) toolkit, MIT License,
Copyright (c) 2026 Ned Heller / sp00nznet; its licence is in
`external/ps3recomp/LICENSE`. The icon of the installed `ico.exe` is not
part of this repository either: the installer cuts it from the `ICON0.PNG` of
your own copy, and the "Exit Game" label is assembled at run time from the
letters of your copy's own menu texture. [SDL2](https://www.libsdl.org) (zlib
license) is downloaded from its official release during install.

---

## 🔗 Related projects

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) (upstream; this port uses the [`ico-port-v0.18` branch of the fork](https://github.com/feanor-noldorin/ps3recomp/tree/ico-port-v0.18), based on v0.18.0) — the PS3 static recompilation toolkit this port is built on.
- [shadowofthecolossus](https://github.com/sp00nznet/shadowofthecolossus) — sister port from the same collection.
- [saintsrow2](https://github.com/sp00nznet/saintsrow2) — another ps3recomp port.
- [RPCS3](https://rpcs3.net) — PS3 emulator; used here to decrypt the executable and provide the firmware.
