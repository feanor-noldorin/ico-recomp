# ⚙️ Run settings (`ico.ini`)

`ico.exe` reads `ico.ini` from its own folder at start-up. Every `KEY=VALUE`
line becomes an environment variable for the game **unless that variable is
already set**, so you can still override any of them for one run from a
shell. Lines starting with `;` are comments. Re-running the installer keeps
your edits (except the two paths it manages).

## Written by the installer

| Key | Default | What it does |
|---|---|---|
| `PS3_VFS_ROOT` | `<install>/vfs` | Folder holding `PS3_GAME` (your game data) and the game's saves/cache. Managed by the installer. |
| `PS3_DEV_FLASH` | your RPCS3 `dev_flash` | The PS3 firmware the game opens `flashATRAC.pic` from. Read-only. Managed by the installer. |
| `ICO_LOG_FILE` | `logs/ico.log` | Send the game's console output to this file and hide the console window. Remove the line to get the console back. |
| `PS3_TITLE` | `ICO` | Window title. |
| `GCM_FLIPCB_ONTICK` | `1` | Deliver the flip callback on the frame tick. Without it the flip handler can fire twice and the game deadlocks. |
| `GCM_PREPAREFLIP_QUEUE` | `1` | Queue the game's prepared flips and complete them on the next vblank, as the PS3 does. Gives the game its correct frame time; without it the game can run unlocked and too fast. |
| `GCM_NV3089_GPU_COPY` | `1` | Keep the game's post-processing blits on the GPU. |
| `PS3RECOMP_RSX_ENGINE` | `dispatch` | Use the shared RSX draw engine with the Direct3D 12 backend (full post chain: fog, bloom, sepia, letterbox). |
| `ICO_SKIP_MLAA` | `1` | Skip the SPU Edge MLAA job (its output is unused with the GPU copy; saves ~40 ms per frame). |
| `ICO_QUIET` | `1` | Drop the per-signal SPURS log lines (they cost ~20% of the main thread). |
| `ICO_60FPS` | `1` | Gameplay at 60 fps with the game's original speed: one logic step per drawn frame. Cinematics and scripted events keep their original 30 fps. `0` = the original 30 fps everywhere. |
| `ICO_CAPTURE` | `0` | `1` turns on the **F9** capture for bug reports: the next frame's render passes and the game's memory go to `captures\<date_time>\` (~20 MB per press). |

## Built-in defaults you can change

These are on without a line in `ico.ini`; add the line to turn one off.

| Key | Default | What it does |
|---|---|---|
| `ICO_EXIT_ITEM` | `1` | **Exit Game** at the bottom of the title and pause menus. `0` hides it. |
| `PAD_SQUIRCLE` | `1` | Map the round stick range to the square one the game expects, so diagonals keep their full speed. `0` = raw stick. |
| `RSX_ENGINE_DEPTH_BLIT` | `1` | Copy the scene's depth and stencil on the GPU for the game's stencil shadows (character shadows). `0` restores the old behaviour: no shadows, a dimmer image and a light band in the middle of the screen. |

## Diagnostics (off by default)

| Key | What it does |
|---|---|
| `ICO_WATCHDOG=<s>` | Print a state summary every `<s>` seconds (threads, flips, ring census). |
| `ICO_PROF=<s>` | Sample per-thread CPU and hot code every `<s>` seconds. `ICO_PROF_TOP=<n>` sets how many entries. |
| `ICO_STDOUT_BUF=1` | Fully buffer stdout (faster, but a crash loses the last lines). |
| `RSX_CENSUS=1` | Count RSX methods the game sends. |
| `PAD_DBG=1` | Log controller reads. |
| `PAD_TRACE=1` | Log stick values before and after the diagonal correction. |
| `D3D12ENG_LOG=1` | Draw-engine backend log; with `D3D12_DBG=1` also the D3D12 debug layer messages. |

The variables of the ps3recomp runtime itself are documented in the
[ps3recomp](https://github.com/sp00nznet/ps3recomp) docs.

## Running by hand

```powershell
cd C:\Games\ICO
.\ico.exe                      # uses data\ICO.elf and ico.ini
.\ico.exe D:\other\ICO.elf     # another ELF
```
