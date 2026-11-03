param(
    [string]$Hwnd = '0x290976',
    [string]$Output = (Join-Path $PSScriptRoot '..\out\live-v16\office-active.json')
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OfficeResearchPlacement {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Placement {
        public uint length, flags, show;
        public Point minimum, maximum;
        public Rect normal;
    }
    [DllImport("user32.dll")] public static extern bool GetWindowPlacement(IntPtr window, ref Placement placement);
    [DllImport("user32.dll")] public static extern bool SetWindowPlacement(IntPtr window, ref Placement placement);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$window = [IntPtr][Convert]::ToInt64($Hwnd.Replace('0x', ''), 16)
$placement = [OfficeResearchPlacement+Placement]::new()
$placement.length = [Runtime.InteropServices.Marshal]::SizeOf($placement)
if (-not [OfficeResearchPlacement]::GetWindowPlacement($window, [ref]$placement)) { throw 'Cannot save target placement.' }
$foreground = [OfficeResearchPlacement]::GetForegroundWindow()
$dpi = [OfficeResearchPlacement]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    [void][OfficeResearchPlacement]::ShowWindow($window, 9)
    Start-Sleep -Milliseconds 650
    & (Get-Command python -ErrorAction Stop).Source (Join-Path $PSScriptRoot 'office-metadata.py') `
        --hwnd $Hwnd --max-objects 1000 --out $Output
    if ($LASTEXITCODE -ne 0) { throw 'Active Office metadata read failed.' }
} finally {
    if (-not [OfficeResearchPlacement]::SetWindowPlacement($window, [ref]$placement)) { throw 'Cannot restore original placement.' }
    [void][OfficeResearchPlacement]::SetForegroundWindow($foreground)
    if ($dpi -ne [IntPtr]::Zero) { [void][OfficeResearchPlacement]::SetThreadDpiAwarenessContext($dpi) }
}
