# Builds NulConnect/res/NulConnect.ico from the macOS app icon artwork.
# The macOS artwork is a full-bleed square; Windows icons get a rounded
# square with a small transparent margin. Entries are stored as PNG, which
# Windows Vista and later read natively.
#
# Usage: powershell -File tools/make-icon.ps1 [-Source <AppIcon-1024.png>]
param(
    [string]$Source = "D:\CodeSpace\NulConnect\NulConnect\Assets.xcassets\AppIcon.appiconset\AppIcon-1024.png",
    [string]$Output = "$PSScriptRoot\..\NulConnect\res\NulConnect.ico"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$entries = @()

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $margin = [Math]::Max(0.0, [Math]::Round($size * 0.06, 2))
    $extent = $size - 2 * $margin
    $radius = $extent * 0.22
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $radius
    $path.AddArc($margin, $margin, $d, $d, 180, 90)
    $path.AddArc($margin + $extent - $d, $margin, $d, $d, 270, 90)
    $path.AddArc($margin + $extent - $d, $margin + $extent - $d, $d, $d, 0, 90)
    $path.AddArc($margin, $margin + $extent - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $graphics.SetClip($path)
    $graphics.DrawImage($sourceImage, [System.Drawing.RectangleF]::new($margin, $margin, $extent, $extent))
    $graphics.ResetClip()
    $graphics.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $entries += , @{ Size = $size; Bytes = $stream.ToArray() }
    $bitmap.Dispose()
}
$sourceImage.Dispose()

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($entry in $entries) {
    $dimension = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$entry.Bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $entry.Bytes.Length
}
foreach ($entry in $entries) {
    $writer.Write($entry.Bytes)
}
$writer.Flush()
[System.IO.File]::WriteAllBytes([System.IO.Path]::GetFullPath($Output), $out.ToArray())
Write-Host "wrote $Output"
