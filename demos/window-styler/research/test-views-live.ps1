param([string]$Hwnd='0x59047c',[Parameter(Mandatory=$true)][string]$Probe,[Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$Transitions,[switch]$RepairOnly)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViewsLive {
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int h2,uint flags);
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static int Count(IntPtr h){int n=0;EnumChildWindows(h,(w,l)=>{n++;return true;},IntPtr.Zero);return n;}
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,UIntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Hit(IntPtr h,int x,int y){UIntPtr r;return SendMessageTimeoutW(h,0x84,UIntPtr.Zero,(IntPtr)((x&65535)|((y&65535)<<16)),2,1000,out r)!=IntPtr.Zero?(long)r.ToUInt64():-1;}
}
'@
[void][ViewsLive]::SetProcessDpiAwarenessContext([IntPtr](-4))
$target=[IntPtr][Convert]::ToInt64($Hwnd.Substring(2),16)
$placement=[ViewsLive+Placement]::new();$placement.size=[Runtime.InteropServices.Marshal]::SizeOf($placement)
if(-not [ViewsLive]::GetWindowPlacement($target,[ref]$placement)){throw 'Cannot save placement'}
$foreground=[ViewsLive]::GetForegroundWindow()
$topmost=([ViewsLive]::GetWindowLongPtrW($target,-20).ToInt64() -band 8) -ne 0
[void](New-Item -ItemType Directory -Force $OutputDirectory)
$result=[ordered]@{hwnd=$Hwnd;frames=[ordered]@{}}
function Capture([string]$Name){
 if(-not [ViewsLive]::IsWindow($target)){throw 'Target no longer exists'}
 $r=[ViewsLive+R]::new();$c=[ViewsLive+R]::new();[void][ViewsLive]::GetWindowRect($target,[ref]$r);[void][ViewsLive]::GetClientRect($target,[ref]$c)
 $p=[ViewsLive+P]::new();[void][ViewsLive]::ClientToScreen($target,[ref]$p);$scale=[ViewsLive]::GetDpiForWindow($target)/96.
 $x=[Math]::Max(0,$r.l);$y=[Math]::Max(0,$r.t);$w=[Math]::Min(1800,$r.r-$x);$height=[int][Math]::Ceiling(85*$scale)
 $b=[Drawing.Bitmap]::new($w,$height);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen($x,$y,0,0,$b.Size);$g.Dispose()
 $b.Save((Join-Path $OutputDirectory ($Name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
 $hits=@();$colors=@();foreach($index in @(0,1,2)){
  $sx=$p.x+[int]((16+$index*20)*$scale);$sy=$p.y+[int](20.5*$scale)
  $hits+=[ViewsLive]::Hit($target,$sx,$sy)
  $color=$b.GetPixel(($sx-$x),($sy-$y));$colors+=('{0:X2}{1:X2}{2:X2}' -f $color.R,$color.G,$color.B)
 }
 $b.Dispose();$result.frames[$Name]=[ordered]@{rect=@($r.l,$r.t,$r.r,$r.b);client=@($c.r,$c.b);children=[ViewsLive]::Count($target);hits=$hits;colors=$colors}
}
try{
 [void][ViewsLive]::ShowWindow($target,9)
 [void][ViewsLive]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13)
 [void][ViewsLive]::SetForegroundWindow($target)
 Start-Sleep -Milliseconds 800
 Capture before
 if($RepairOnly){& $Probe $Hwnd 4;if($LASTEXITCODE -ne 0){throw "Original layout request failed: $LASTEXITCODE"};Start-Sleep -Milliseconds 500;Capture original-layout;return}
 & $Probe $Hwnd 1
 if($LASTEXITCODE -ne 0){throw "Read probe failed: $LASTEXITCODE"}
 $worker=Start-Process -FilePath (Resolve-Path $Probe).Path -ArgumentList @($Hwnd,'2') -WindowStyle Hidden -PassThru
 Start-Sleep -Milliseconds 1200
 Capture after
 Start-Sleep -Milliseconds 1600
 Capture stable
 if($Transitions){
  foreach($cycle in 1..2){
   [void][ViewsLive]::ShowWindow($target,3)
   Start-Sleep -Milliseconds 700
   Capture ("maximized-"+$cycle)
   [void][ViewsLive]::ShowWindow($target,9)
   Start-Sleep -Milliseconds 700
   Capture ("unmaximized-"+$cycle)
  }
  $r=[ViewsLive+R]::new();[void][ViewsLive]::GetWindowRect($target,[ref]$r)
  [void][ViewsLive]::SetWindowPos($target,[IntPtr]::Zero,0,0,($r.r-$r.l-160),($r.b-$r.t-60),0x16)
  Start-Sleep -Milliseconds 500
  Capture resized
  [void][ViewsLive]::SetWindowPos($target,[IntPtr]::Zero,0,0,($r.r-$r.l),($r.b-$r.t),0x16)
  Start-Sleep -Milliseconds 500
  Capture size-restored
 }
 & $Probe $Hwnd 3
 if($LASTEXITCODE -ne 0){throw "Detach probe failed: $LASTEXITCODE"}
 Start-Sleep -Milliseconds 500
 Capture restored
 $result.alive=[ViewsLive]::IsWindow($target)
}finally{
 if([ViewsLive]::IsWindow($target)){
  & $Probe $Hwnd 3
  if(-not $topmost){[void][ViewsLive]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)}
  [void][ViewsLive]::SetWindowPlacement($target,[ref]$placement)
 }
 [void][ViewsLive]::SetForegroundWindow($foreground)
 $result|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory result.json) -Encoding UTF8
}
$result|ConvertTo-Json -Depth 8
