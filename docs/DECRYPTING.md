# 🔓 Decrypting ICO.self with RPCS3

The game's executable on the disc, `PS3_GAME/USRDIR/ICO.self`, is encrypted.
The recompiler needs the decrypted ELF. This project does **not** decrypt
anything itself and ships no keys: decryption is done by **your own copy of
[RPCS3](https://rpcs3.net)**, in one of two ways.

> You need RPCS3 anyway: the installer also reads the PS3 firmware you
> installed in it (`dev_flash`), because the game's ATRAC audio codec is
> recompiled from that firmware.

---

## Option A — automatic (default)

In the installer, keep **"Decrypt automatically with my RPCS3"** selected and
point it at `rpcs3.exe`. The installer copies `ICO.self` into its own build
folder and runs:

```bash
rpcs3.exe --no-gui --decrypt <build folder>\input\ICO.self
```

RPCS3 writes `ICO.elf` next to the copy. Your dump is never modified. The
result is checked by SHA-256 against the tested US release.

## Option B — by hand

1. Open RPCS3.
2. Install the PS3 firmware if you have not yet: **File → Install Firmware**
   (`PS3UPDAT.PUP` from Sony's website).
3. **Utilities → Decrypt PS3 Binaries**.
4. Select `PS3_GAME\USRDIR\ICO.self` from your dump.
5. RPCS3 writes `ICO.elf` in the same folder.
6. In the installer, choose **"I already decrypted it (ICO.elf)"** and select
   that file. (If `ICO.elf` sits next to `ICO.self`, the installer fills it in.)

## Checking the result

| File | SHA-256 |
|---|---|
| `USRDIR/ICO.self` (US, BCUS98259) | `bef83af9cea5c98ab09f272f47c5af7da9784e771866559f53a7f83ab9971334` |
| `USRDIR/ICO.elf` (decrypted) | `ac96e07650b3e3b631c1df2995c904af8a15e6cac09f428c851c99873d9709c8` |

```powershell
Get-FileHash -Algorithm SHA256 .\ICO.elf
```

A different hash means a different release or a bad dump. Only the US release
is supported for now; the code cut points in `pipeline/ico_profile.json` are
specific to it.
