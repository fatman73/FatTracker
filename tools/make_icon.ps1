# Generates res/icon.ico (16/32/48/256 px, PNG-compressed entries) for FatTracker.
# Design: grey bevelled panel, black screen, blue "FT", three green VU bars.
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot '..\res\icon.ico'

function New-IconPng([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)
    $e = [Math]::Max(1, [int]($s / 16))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xB8, 0xBC, 0xC4))), 0, 0, $s, $s)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xEE, 0xF1, 0xF5))), 0, 0, $s, $e)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xEE, 0xF1, 0xF5))), 0, 0, $e, $s)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x5A, 0x60, 0x6B))), 0, $s - $e, $s, $e)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x5A, 0x60, 0x6B))), $s - $e, 0, $e, $s)
    $m = [int]($s * 0.12)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 4, 6, 10))), $m, $m, $s - 2 * $m, $s - 2 * $m)
    $green = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x2F, 0xE0, 0x6A))
    $bw = [Math]::Max(1, [int]($s * 0.09))
    $base = $s - $m - [Math]::Max(1, [int]($s * 0.06))
    $heights = 0.30, 0.45, 0.22
    for ($i = 0; $i -lt 3; $i++) {
        $h = [int]($s * $heights[$i])
        $x = [int]($s * 0.58) + $i * ($bw + [Math]::Max(1, [int]($s * 0.03)))
        $g.FillRectangle($green, $x, $base - $h, $bw, $h)
    }
    $fontSize = [Math]::Max(5, $s * 0.36)
    $font = New-Object System.Drawing.Font 'Consolas', $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $blue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x3F, 0x6F, 0xE8))
    $g.DrawString('FT', $font, $blue, [single]($m * 0.9), [single]($m * 0.8))
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $ms.ToArray()
}

$sizes = 16, 32, 48, 256
$pngs = @(); foreach ($s in $sizes) { $pngs += , (New-IconPng $s) }
$fs = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
[IO.File]::WriteAllBytes($out, $fs.ToArray())
Write-Host "wrote $out ($($fs.Length) bytes)"
