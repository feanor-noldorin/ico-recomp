<#
.SYNOPSIS
  Builds ico.exe's icon from the user's own PS3_GAME\ICON0.PNG.

.DESCRIPTION
  The collection's ICON0.PNG stacks several logos on a transparent
  background (ICO on top, then "Collection", then Shadow of the
  Colossus). The rows holding opaque pixels form bands; -Band picks one
  (0 = the ICO logo). Its bounding box is centred on a transparent square
  with a small margin and written as a multi-size .ico (PNG entries,
  16-256 px). Nothing here ships with the project: the art comes from the
  user's copy at build time.

.EXAMPLE
  powershell -NoProfile -File make_icon.ps1 -Source ...\PS3_GAME\ICON0.PNG -Out ico.ico -Band 0
#>
param(
    [Parameter(Mandatory)] [string]$Source,
    [Parameter(Mandatory)] [string]$Out,
    [int]$Band = 0,
    [int]$AlphaMin = 40
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Bitmap]::FromFile($Source)
try {
    # rows with any opaque pixel -> bands
    $opaque = New-Object bool[] $src.Height
    for ($y = 0; $y -lt $src.Height; $y++) {
        for ($x = 0; $x -lt $src.Width; $x++) {
            if ($src.GetPixel($x, $y).A -gt $AlphaMin) { $opaque[$y] = $true; break }
        }
    }
    $bands = @(); $start = -1
    for ($y = 0; $y -le $src.Height; $y++) {
        $on = ($y -lt $src.Height) -and $opaque[$y]
        if ($on -and $start -lt 0) { $start = $y }
        elseif (-not $on -and $start -ge 0) { $bands += ,@($start, ($y - 1)); $start = -1 }
    }
    if ($bands.Count -eq 0) { throw "ICON0.PNG has no opaque pixels" }
    if ($Band -ge $bands.Count) { $Band = 0 }
    $y0 = $bands[$Band][0]; $y1 = $bands[$Band][1]

    $x0 = $src.Width; $x1 = -1
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = 0; $x -lt $src.Width; $x++) {
            if ($src.GetPixel($x, $y).A -gt $AlphaMin) { if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x } }
        }
    }
    $w = $x1 - $x0 + 1; $h = $y1 - $y0 + 1
    $side = [int]([Math]::Max($w, $h) * 1.08)          # ~4% margin each side
    $square = New-Object System.Drawing.Bitmap $side, $side, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($square)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle ([int](($side - $w) / 2)), ([int](($side - $h) / 2)), $w, $h),
                 (New-Object System.Drawing.Rectangle $x0, $y0, $w, $h), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
} finally {
    $src.Dispose()
}

$sizes = 256, 128, 64, 48, 32, 24, 16
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'; $g.SmoothingMode = 'HighQuality'
    $g.DrawImage($square, 0, 0, $s, $s); $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $pngs += , ($ms.ToArray())
}
$square.Dispose()

# ICONDIR + ICONDIRENTRY[] + PNG images
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $d = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $bw.Write([byte]$d); $bw.Write([byte]$d); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$pngs[$i].Length); $bw.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($Out, $ms.ToArray())
"icon: band $Band of $($bands.Count) (x $x0-$x1, y $y0-$y1) -> $Out"
