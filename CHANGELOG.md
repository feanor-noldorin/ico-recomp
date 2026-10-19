# 📝 Changelog

All notable changes to the ICO PC port. Dates are YYYY-MM-DD.

## [0.1.0] — 2026-10-18 — first public build

The first release that boots ICO to the title menu and into the opening
cinematic and gameplay on Windows, installed from the user's own disc dump.

### Installer
- **`ICO.PC.Port.Setup.exe`**: one self-contained exe (no .NET install
  needed). Select your `PS3_GAME` folder, RPCS3 and an install folder; it
  checks your files by SHA-256, recompiles the game on your PC and creates
  desktop/Start menu shortcuts. English and Brazilian Portuguese.
- **Two ways to get `ICO.elf`**: automatic decryption with your RPCS3
  (`rpcs3 --no-gui --decrypt`, done on a copy, the dump is never written to),
  or select a file you decrypted by hand.
- **Build tools check**: detects Visual Studio (C++, Clang, CMake) and Python;
  offers to install what is missing through winget, after asking.
- Game data is **copied** (~1.7 GB, only what ICO uses; not the 6 GB of Shadow
  of the Colossus) or, optionally, **linked** to the dump.
- **AVX2 + FMA** build on CPUs that have them, **SSE4.1** build otherwise.
- `ico.ini` beside `ICO.exe` holds the run settings; the game writes its log to
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
