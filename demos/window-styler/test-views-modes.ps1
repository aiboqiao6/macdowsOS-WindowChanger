param([Parameter(Mandatory=$true)][string]$Hwnd,[string]$ReleaseDirectory=(Join-Path $PSScriptRoot 'out/build-x64/Release'),[string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v13/views-modes'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViewsModes {
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr p);
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int h2,uint flags);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetPropW(IntPtr h,string s);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessageW(string s);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r;if(SendMessageTimeoutW(h,m,(IntPtr)w,(IntPtr)l,2,2500,out r)==IntPtr.Zero)throw new Exception("Target not responsive");return unchecked((long)r.ToUInt64());}
 public static bool Ready(IntPtr h){return Send(h,RegisterWindowMessageW("Macdows.WindowStyler.Query.v1"))==0x4d414357;}
 public static bool Fullscreen(IntPtr h){return (GetWindowLongPtrW(h,-16).ToInt64()&0xc00000)==0;}
 public static void Toggle(IntPtr h){SetForegroundWindow(h);System.Threading.Thread.Sleep(120);if(GetForegroundWindow()!=h)throw new Exception("Foreground changed; input cancelled");keybd_event(0x7a,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(60);keybd_event(0x7a,0,2,UIntPtr.Zero);}
}
'@
[void][ViewsModes]::SetProcessDpiAwarenessContext([IntPtr](-4))
$target=[IntPtr][Convert]::ToInt64($Hwnd.Substring(2),16)
if([ViewsModes]::Fullscreen($target)){throw 'Start from ordinary window mode'}
if([ViewsModes]::GetPropW($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero){throw 'Pause the existing controller first'}
New-Item -ItemType Directory -Force $OutputDirectory|Out-Null
$p=[ViewsModes+Placement]::new();$p.size=[Runtime.InteropServices.Marshal]::SizeOf($p);[void][ViewsModes]::GetWindowPlacement($target,[ref]$p)
$foreground=[ViewsModes]::GetForegroundWindow();$topmost=([ViewsModes]::GetWindowLongPtrW($target,-20).ToInt64() -band 8) -ne 0
$worker=$null;$result=[ordered]@{hwnd=$Hwnd;frames=@();passed=$false}
function Wait-State([scriptblock]$Test,[string]$Failure){$end=[DateTime]::UtcNow.AddSeconds(5);do{if(&$Test){return};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $end);throw $Failure}
function Shot([string]$Name){
 $r=[ViewsModes+R]::new();[void][ViewsModes]::GetWindowRect($target,[ref]$r);$x=[Math]::Max(0,$r.l);$y=[Math]::Max(0,$r.t);$w=[Math]::Min([ViewsModes]::GetSystemMetrics(0),$r.r)-$x;$height=[Math]::Min(150,[ViewsModes]::GetSystemMetrics(1)-$y)
 $bitmap=[Drawing.Bitmap]::new($w,$height);$g=[Drawing.Graphics]::FromImage($bitmap);$g.CopyFromScreen($x,$y,0,0,$bitmap.Size);$g.Dispose();$bitmap.Save((Join-Path $OutputDirectory ($Name+'.png')))
 $origin=[ViewsModes+P]::new();[void][ViewsModes]::ClientToScreen($target,[ref]$origin);$scale=[ViewsModes]::GetDpiForWindow($target)/96.
 $metric=[ViewsModes]::RegisterWindowMessageW('Macdows.WindowStyler.Diagnostics.v1');$top=[ViewsModes]::Send($target,$metric,8);$rowHeight=[ViewsModes]::Send($target,$metric,10)
 $colors=@();$hits=@();foreach($i in @(0,1,2)){
  $px=$origin.x+[int]((16+$i*20)*$scale);$py=$origin.y+[int]($top+$rowHeight/2)
  if($px-$x -lt 0 -or $py-$y -lt 0 -or $px-$x -ge $bitmap.Width -or $py-$y -ge $bitmap.Height){$colors+='out-of-frame'}else{$color=$bitmap.GetPixel($px-$x,$py-$y);$colors+=('{0:X2}{1:X2}{2:X2}' -f $color.R,$color.G,$color.B)}
  $hits+=[ViewsModes]::Send($target,0x84,0,($px -band 65535) -bor (($py -band 65535) -shl 16))
 };$bitmap.Dispose()
 $full=[ViewsModes]::Fullscreen($target)
 if($full){if(($colors -join ',') -eq 'FF5F57,FFBD2E,28C840' -or @($hits|Where-Object {$_ -in @(8,9,20)}).Count){throw "${Name}: caption controls remain active in fullscreen"}}
 elseif(($colors -join ',') -ne 'FF5F57,FFBD2E,28C840' -or ($hits -join ',') -ne '20,8,9'){throw "${Name}: controls did not visually and functionally resume"}
 $result.frames+=@([ordered]@{phase=$Name;colors=$colors;hits=$hits;fullscreen=[ViewsModes]::Fullscreen($target);ready=[ViewsModes]::Ready($target);owner=[ViewsModes]::GetPropW($target,'Macdows.WindowStyler.Owner.v1').ToInt64();height=[ViewsModes]::Send($target,[ViewsModes]::RegisterWindowMessageW('Macdows.WindowStyler.Diagnostics.v1'),10)})
}
try{
 [void][ViewsModes]::ShowWindow($target,9);[void][ViewsModes]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13);[void][ViewsModes]::SetForegroundWindow($target)
 $worker=Start-Process (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$target.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $OutputDirectory 'worker-error.txt')
 Wait-State {[ViewsModes]::Ready($target)} 'Attach failed';Start-Sleep -Milliseconds 200;Shot attached
 foreach($cycle in @(1,2)){
  [ViewsModes]::Toggle($target);Wait-State {[ViewsModes]::Fullscreen($target)} 'F11 did not enter fullscreen';Start-Sleep -Milliseconds 400;Shot ("fullscreen-"+$cycle)
  [ViewsModes]::Toggle($target);Wait-State {-not [ViewsModes]::Fullscreen($target)} 'F11 did not leave fullscreen';Wait-State {[ViewsModes]::Ready($target)} 'Controls did not resume after fullscreen';Start-Sleep -Milliseconds 200;Shot ("resumed-"+$cycle)
 }
 $result.passed=$true
}catch{$result.error=$_.Exception.Message;throw}finally{
 if([ViewsModes]::Fullscreen($target)){[ViewsModes]::Toggle($target);Start-Sleep -Milliseconds 400}
 if($worker -and -not $worker.HasExited){Stop-Process -Id $worker.Id};Start-Sleep -Milliseconds 1000
 [void][ViewsModes]::SetWindowPlacement($target,[ref]$p);if(-not $topmost){[void][ViewsModes]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)}
 [void][ViewsModes]::SetForegroundWindow($foreground);$result|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
}
$result|ConvertTo-Json -Depth 5
