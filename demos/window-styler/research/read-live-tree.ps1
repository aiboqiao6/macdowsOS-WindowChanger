param([Parameter(Mandatory=$true)][string]$Frame,[Parameter(Mandatory=$true)][string]$Content,[Parameter(Mandatory=$true)][string]$Probe)
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ResearchPlacement{
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr h);
}
'@
[void][ResearchPlacement]::SetProcessDpiAwarenessContext([IntPtr](-4))
$target=[IntPtr][Convert]::ToInt64($Frame.Substring(2),16)
$p=[ResearchPlacement+Placement]::new();$p.size=[Runtime.InteropServices.Marshal]::SizeOf($p)
if(-not [ResearchPlacement]::GetWindowPlacement($target,[ref]$p)){throw 'Cannot save placement'}
$foreground=[ResearchPlacement]::GetForegroundWindow()
try{
 [void][ResearchPlacement]::ShowWindow($target,9)
 Start-Sleep -Milliseconds 600
 & $Probe $Content
 $code=$LASTEXITCODE
 Write-Output "Read-only tree probe exit: $code"
}finally{
 [void][ResearchPlacement]::SetWindowPlacement($target,[ref]$p)
 [void][ResearchPlacement]::SetForegroundWindow($foreground)
}
exit $code
