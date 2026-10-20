<#
.SYNOPSIS
  Builds the release installer: build\release\ICO.PC.Port.Setup.exe

.DESCRIPTION
  1. Packs the payload (pipeline\, port\, docs\, LICENSE and the committed
     ps3recomp submodule tree) into build\payload.zip. Only committed files go
     in: `git archive` never picks up local edits, game files or build output.
  2. Publishes the installer as one self-contained exe with the payload embedded.
  3. Writes the exe's SHA-256 next to it and to SHA256SUMS.txt (a release asset).

  The payload holds no game data, no lifted code and nothing from the PS3
  firmware; those are produced on the user's PC by pipeline\ico_pipeline.py.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\make_release.ps1
#>
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$stage = Join-Path $build 'payload'
$zip   = Join-Path $build 'payload.zip'
$out   = Join-Path $build 'release'

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, $out | Out-Null

# repo files: committed content only
$repoTar = Join-Path $build 'repo.tar'
git -C $root archive --format=tar -o $repoTar HEAD pipeline port docs LICENSE README.md
if ($LASTEXITCODE -ne 0) { throw 'git archive (repo) failed' }
tar -xf $repoTar -C $stage
Remove-Item $repoTar

# ps3recomp submodule: committed tree of the pinned commit
$sub = Join-Path $root 'external\ps3recomp'
if (-not (Test-Path (Join-Path $sub 'tools\ppu_lifter.py'))) { throw 'external\ps3recomp is empty: git submodule update --init' }
$subDst = Join-Path $stage 'external\ps3recomp'
New-Item -ItemType Directory -Force $subDst | Out-Null
$subTar = Join-Path $build 'ps3recomp.tar'
git -C $sub archive --format=tar -o $subTar HEAD
if ($LASTEXITCODE -ne 0) { throw 'git archive (ps3recomp) failed' }
tar -xf $subTar -C $subDst
Remove-Item $subTar

if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
'payload.zip: {0:N1} MB' -f ((Get-Item $zip).Length / 1MB)

dotnet publish (Join-Path $root 'installer\ICOSetup\ICOSetup.csproj') -c Release -o $out
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }

$exe = Join-Path $out 'ICO.PC.Port.Setup.exe'
$hash = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower()
"$hash  ICO.PC.Port.Setup.exe" | Set-Content -Encoding ascii (Join-Path $out 'ICO.PC.Port.Setup.exe.sha256')
# SHA256SUMS.txt is uploaded next to the exe in each release (sha256sum -c format)
# LF line ending: with CRLF, GNU `sha256sum -c` reads the name as "...exe"
[IO.File]::WriteAllText((Join-Path $out 'SHA256SUMS.txt'), "$hash  ICO.PC.Port.Setup.exe`n")
'{0}  ({1:N1} MB)' -f $exe, ((Get-Item $exe).Length / 1MB)
"SHA-256 $hash"
