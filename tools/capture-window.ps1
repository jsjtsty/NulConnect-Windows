# Captures a top-level window (including Direct2D/DWM content) to a PNG.
# Usage: powershell -File tools/capture-window.ps1 -ClassName NulConnect.MainWindow -Output shot.png
param(
    [string]$ClassName = "NulConnect.MainWindow",
    [string]$Output = "window.png"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

[Win]::SetProcessDPIAware() | Out-Null
$hwnd = [Win]::FindWindow($ClassName, [NullString]::Value)
if ($hwnd -eq [IntPtr]::Zero) { throw "window $ClassName not found" }
$rect = New-Object Win+RECT
[Win]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
[Win]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 400
# Copy from the screen so the DWM backdrop (Mica/acrylic) is included.
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
$bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Host "saved $Output ($width x $height)"
