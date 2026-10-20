# 📝 Changelog

All notable changes to the ICO PC port. Dates are YYYY-MM-DD.

## [0.2.1] — 2026-10-19 — installer and transparency

No change to the game itself; the recompiled game is identical to 0.2.0.

- **Reproducible installs**: `pipeline/constraints.txt` pins the exact
  versions of the recompiler's Python dependencies this release was built and
  tested with; the installer passes it to pip with `-c`.
- **Checksums**: every release now ships `SHA256SUMS.txt` next to the
  installer (`sha256sum -c SHA256SUMS.txt`).
- **AI disclosure** section in the README.

## [0.2.0] — 2026-10-19 — playable at 60 fps

### Gameplay
- **Character control**: a ps3recomp lifter fix for jump tables whose base
  is spilled to the stack (a `switch` was lifted as an indirect call) — Ico
  walks, climbs and leads Yorda.
- **60 fps at the original game speed** (`ICO_60FPS=1`, default): one logic
  step per drawn frame in play; cinematics and scripted events keep 30 fps.
- **Exit Game** at the bottom of the title and pause menus, using the game's
  own font and selection effect (built at run time from the game's texture;
  no art ships with the project). `ICO_EXIT_ITEM=0` hides it.

### Input
- **DualSense / DualShock 4** through SDL2 (USB or Bluetooth), XInput kept as
  fallback; the keyboard now works together with a controller.
- **Full-speed diagonals** (`PAD_SQUIRCLE=1`, default).

### Graphics
- **Character shadows**: the half-resolution depth + stencil copy of the
  stencil shadow pass now runs on the GPU (`RSX_ENGINE_DEPTH_BLIT=1`, default).
  Fixes the light band in the middle of the screen and the dim image.

### Stability and pacing
- PrepareFlip/FlipImmediate queue with an exact 60 Hz clock: correct frame
  time and audio sync (`GCM_PREPAREFLIP_QUEUE=1`, now set by the installer —
  0.1.0 builds could run unlocked and too fast).
- Prepare marker in the command FIFO: fixes freezes when the emulated GPU
  ran a frame ahead; the stuck-command detector no longer fires on legitimate
  waits.
- Frame submit without a full GPU wait: GPU stall 9–10 ms → 0.2–1.7 ms per frame.
- Closing the window ends the process.
- **F9 capture** for bug reports (`ICO_CAPTURE=1`, off by default): the next
  frame's render passes and the game's memory.

### Installer
- Downloads the official SDL2 2.32.10 development package (SHA-256 checked)
  and builds the controller backend; without it the game builds with XInput
  controllers only.
- `ico.ini` gains `GCM_PREPAREFLIP_QUEUE=1`, `ICO_60FPS=1` and `ICO_CAPTURE=0`.

## [0.1.0] — 2026-10-18 — first public build

The first release that boots ICO to the title menu and into the opening
cinematic and gameplay on Windows, installed from the user's own disc dump.

### Installer
- **`ICO.PC.Port.Setup.exe`**: one self-contained exe (no .NET install
  needed). Select your `PS3_GAME` folder, RPCS3 and an install folder; it
  checks your files by SHA-256, recompiles the game on your PC and creates
  desktop/Start menu shortcuts. English UI; the window fits every field
  without scrolling.
- **Game icon**: the installed `ico.exe` gets the ICO logo, cut at build time
  from the `ICON0.PNG` of the user's own copy (no game art ships with the project).
- **Two ways to get `ICO.elf`**: automatic decryption with your RPCS3
  (`rpcs3 --no-gui --decrypt`, done on a copy, the dump is never written to),
  or select a file you decrypted by hand.
- **Build tools check**: detects Visual Studio (C++, Clang, CMake) and Python;
  offers to install what is missing through winget, after asking.
- Game data is **copied** (~1.7 GB, only what ICO uses; not the 6 GB of Shadow
  of the Colossus) or, optionally, **linked** to the dump.
- **AVX2 + FMA** build on CPUs that have them, **SSE4.1** build otherwise.
- The installed game is **`ico.exe`**; `ico.ini` beside it holds the run settings; the game writes its log to
  `logs\ico.log` instead of opening a console window.

### Game (fixes since the first boot, all in this port or its ps3recomp branch)
- **Boot**: SPURS taskset v2 / LFQueue push, `cellFiberPpuContext*` on Windows
  fibers, PPU lift with `--code-end` (−760 bogus functions, −45% exe size,
  3D frame rate 15 → ~44 fps by itself).
- **SPU**: six SPU images lifted with per-image code ends; MultiStream DSP
  overlay and the firmware ATRAC codec lifted and registered; an atomic race
  in the taskset signal bits fixed (lost inter-task signals stalled frames).
- **Graphics**: shared RSX draw engine with a new Direct3D 12 backend —
  `INLINE_ARRAY` draws, masked clears, `BLEND_COLOR`, stencil, per-zeta
  persistent depth, compressed (CMP) normals, indexed triangle strips, depth
  sampled as colour (depth fog), GPU-side MLAA bounce copies. Fixes the
  black screen, the white flashes, missing geometry and the washed-out colours.
- **Performance**: PSO/VS caches keyed by shader hash, MFC engine cache,
  AVX2 SPU float helpers, skipped Edge MLAA → ~48–60 fps in the 3D scenes on
  the development PC (was < 1 fps).
- **Audio**: firmware ATRAC + DSP overlay working; the opening music plays.

### Known issues
See [Known issues](README.md#-known-issues) in the README.
