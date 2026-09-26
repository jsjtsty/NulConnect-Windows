# Posts a left click to a window without moving the real cursor.
# X/Y are pixel offsets inside the window rectangle (as in a capture from
# capture-window.ps1).
param(
    [string]$ClassName = "NulConnect.MainWindow",
    [int]$X,
    [int]$Y,
    [switch]$Right
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Clicker {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
[Clicker]::SetProcessDPIAware() | Out-Null
$hwnd = [Clicker]::FindWindow($ClassName, [NullString]::Value)
if ($hwnd -eq [IntPtr]::Zero) { throw "window $ClassName not found" }
$rect = New-Object Clicker+RECT
[Clicker]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$point = New-Object Clicker+POINT
$point.X = $rect.Left + $X
$point.Y = $rect.Top + $Y
[Clicker]::ScreenToClient($hwnd, [ref]$point) | Out-Null
$lParam = [IntPtr](($point.Y -shl 16) -bor ($point.X -band 0xFFFF))
$down = if ($Right) { 0x0204 } else { 0x0201 }
$up = if ($Right) { 0x0205 } else { 0x0202 }
[Clicker]::PostMessage($hwnd, 0x0200, [IntPtr]0, $lParam) | Out-Null
[Clicker]::PostMessage($hwnd, $down, [IntPtr]1, $lParam) | Out-Null
[Clicker]::PostMessage($hwnd, $up, [IntPtr]0, $lParam) | Out-Null
Write-Host "clicked client ($($point.X), $($point.Y))"
