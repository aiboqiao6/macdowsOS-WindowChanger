param([string]$ReleaseDirectory='',[string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v11/embedded-views'),[switch]$NoLease,[switch]$ReplayHostStyle)
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=if(Test-Path (Join-Path $PSScriptRoot 'StylerTests.exe')){$PSScriptRoot}else{Join-Path $PSScriptRoot 'out/build-x64/Release'}}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
trap {$_|Out-String|Set-Content (Join-Path $OutputDirectory 'failure.txt');exit 1}
Add-Type -AssemblyName System.Drawing
if (-not ('CaptureNative' -as [type])) { Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CaptureNative {

 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 public static int Children(IntPtr h){int n=0;EnumChildWindows(h,(w,p)=>{n++;return true;},IntPtr.Zero);return n;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct Scroll{public int size;public R rect;public int line,thumbTop,thumbBottom,reserved;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public uint[] state;}
 [DllImport("user32.dll")]public static extern bool GetScrollBarInfo(IntPtr h,int id,ref Scroll s);
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr d);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern int GetWindowRgn(IntPtr h,IntPtr r);
 [DllImport("gdi32.dll")]public static extern IntPtr CreateRectRgn(int l,int t,int r,int b);
 [DllImport("gdi32.dll")]public static extern int GetRgnBox(IntPtr r,out R b);
 [DllImport("gdi32.dll")]public static extern bool PtInRegion(IntPtr r,int x,int y);
 [DllImport("gdi32.dll")]public static extern bool DeleteObject(IntPtr r);
 [DllImport("dwmapi.dll")]public static extern int DwmGetWindowAttribute(IntPtr h,int n,out int v,int cb);
 [DllImport("dwmapi.dll",EntryPoint="DwmGetWindowAttribute")]public static extern int DwmRect(IntPtr h,int n,out R v,int cb);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr SetWindowLongPtr(IntPtr h,int n,IntPtr v);
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Metric(IntPtr h,int n){UIntPtr r;SendMessageTimeout(h,RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1"),(IntPtr)n,IntPtr.Zero,2,1000,out r);return unchecked((long)r.ToUInt64());}
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("kernel32.dll")]public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")]public static extern bool AttachThreadInput(uint a,uint b,bool yes);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int h2,uint flags);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string n);
 public static bool Ready(IntPtr h){UIntPtr result;return SendMessageTimeout(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"),IntPtr.Zero,IntPtr.Zero,2,1000,out result)!=IntPtr.Zero&&result.ToUInt64()==0x4d414357;}
 public static void Front(IntPtr h){uint pid;uint a=GetCurrentThreadId();uint b=GetWindowThreadProcessId(GetForegroundWindow(),out pid);AttachThreadInput(a,b,true);SetForegroundWindow(h);AttachThreadInput(a,b,false);}
}
"@
}
[void][CaptureNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
& (Join-Path $ReleaseDirectory 'WindowAudit.exe') --direct-inventory (Join-Path $OutputDirectory 'inventory.json')
$inventory=Get-Content (Join-Path $OutputDirectory 'inventory.json') -Raw -Encoding UTF8|ConvertFrom-Json
$row=$inventory.windows|Where-Object class -eq 'ApplicationFrameWindow'|Select-Object -First 1
$target=[IntPtr][Convert]::ToInt64($row.hwnd.Substring(2),16)
$p=[CaptureNative+Placement]::new();$p.size=[Runtime.InteropServices.Marshal]::SizeOf($p);[void][CaptureNative]::GetWindowPlacement($target,[ref]$p)
$wasTop=([CaptureNative]::GetWindowLongPtr($target,-20).ToInt64() -band 8) -ne 0
$front=[CaptureNative]::GetForegroundWindow();$original='';$checks=@();$worker=$null
function Pages { $r=[Windows.Automation.AutomationElement]::FromHandle($target);$cond=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem);return @($r.FindAll([Windows.Automation.TreeScope]::Descendants,$cond)|Where-Object {$_.Current.ClassName -eq 'Microsoft.UI.Xaml.Controls.NavigationViewItem'}) }
function Shot([string]$Label){
 $r=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$r)
 $bitmap=[Drawing.Bitmap]::new(($r.right-$r.left),64);$g=[Drawing.Graphics]::FromImage($bitmap);$g.CopyFromScreen($r.left,$r.top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $OutputDirectory ($Label+'.png')));$g.Dispose();$bitmap.Dispose()
 $origin=[CaptureNative+P]::new();[void][CaptureNative]::ClientToScreen($target,[ref]$origin);$left=$origin.x+[int][CaptureNative]::Metric($target,7);$y=$origin.y+[int]([CaptureNative]::Metric($target,8)+[CaptureNative]::Metric($target,10)/2);$dpi=[CaptureNative]::GetDpiForWindow($target);$lightColors=@();$screen=[Drawing.Bitmap]::new(1,1);$screenGraphics=[Drawing.Graphics]::FromImage($screen);foreach($index in 0..2){$x=$left+[int]((18+20*$index)*$dpi/96);$screenGraphics.CopyFromScreen($x,$y,0,0,$screen.Size);$lightColors+=$screen.GetPixel(0,0).ToArgb().ToString("X8").Substring(2)};$screenGraphics.Dispose();$screen.Dispose();$script:checks+=[pscustomobject]@{page=$Label;pixelLights=$lightColors;style=[CaptureNative]::GetWindowLongPtr($target,-16).ToInt64();systemButtons=([CaptureNative]::GetWindowLongPtr($target,-16).ToInt64() -band 0x80000) -ne 0;originalButtons=[CaptureNative]::Metric($target,12);attached=[CaptureNative]::Ready($target);lights=@([CaptureNative]::Metric($target,7),[CaptureNative]::Metric($target,8),[CaptureNative]::Metric($target,9),[CaptureNative]::Metric($target,10));contentLeft=[CaptureNative]::Metric($target,11)}
 $script:checks|ConvertTo-Json -Depth 4|Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}
try{
 [void][CaptureNative]::ShowWindow($target,9);[void][CaptureNative]::SetWindowPos($target,[IntPtr](-1),80,60,1500,900,0x40);[CaptureNative]::Front($target);Start-Sleep -Milliseconds 600
 if(-not $NoLease){
  $worker=Start-Process (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$target.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $OutputDirectory 'worker-error.txt')
  $end=[DateTime]::UtcNow.AddSeconds(6);while(-not [CaptureNative]::Ready($target) -and [DateTime]::UtcNow -lt $end){Start-Sleep -Milliseconds 50}
  if(-not [CaptureNative]::Ready($target)){throw 'XAML layout lease was not acknowledged.'}
 }
 $pages=Pages;foreach($node in $pages){$pat=$node.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern);if($pat.Current.IsSelected){$original=$node.Current.Name}}
 Shot '00-initial'
 $names=@($pages|ForEach-Object {$_.Current.Name})
 $i=0;foreach($name in $names){$i++;$node=Pages|Where-Object {$_.Current.Name -eq $name}|Select-Object -First 1;$pat=$node.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern);$pat.Select();if($ReplayHostStyle){[void][CaptureNative]::SetWindowLongPtr($target,-16,[IntPtr]([CaptureNative]::GetWindowLongPtr($target,-16).ToInt64() -bor 0x80000));[void][CaptureNative]::SetWindowPos($target,[IntPtr]::Zero,0,0,0,0,0x37)};Start-Sleep -Milliseconds 150;Shot ('{0:00}-{1}-early' -f $i,$name);Start-Sleep -Milliseconds 550;Shot ('{0:00}-{1}-stable' -f $i,$name)}
}finally{
 if($original){$node=Pages|Where-Object {$_.Current.Name -eq $original}|Select-Object -First 1;if($node){$node.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()}}
 if($worker -and -not $worker.HasExited){$worker.Kill();$worker.WaitForExit();$end=[DateTime]::UtcNow.AddSeconds(5);while([CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $end){Start-Sleep -Milliseconds 50}}
 [void][CaptureNative]::SetWindowPlacement($target,[ref]$p);if(-not $wasTop){[void][CaptureNative]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)};[CaptureNative]::Front($front)
}
if(@($checks|Where-Object {-not $_.attached -or ($_.pixelLights -join ',') -ne 'FF5F57,FFBD2E,28C840' -or $_.originalButtons -ne 0 -or $_.lights[2] -le 0 -or $_.contentLeft -lt $_.lights[0]+$_.lights[2]}).Count){exit 1}
