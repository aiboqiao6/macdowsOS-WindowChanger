param([Parameter(Mandatory=$true)][string]$Hwnd,
 [string]$ReleaseDirectory=(Join-Path $PSScriptRoot 'out/build-x64/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v12/views-final'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing,UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViewsVerify {
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr p);
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int h2,uint flags);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(P p);
 [DllImport("user32.dll")]public static extern IntPtr GetAncestor(IntPtr h,uint f);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr extra);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr SetWindowLongPtrW(IntPtr h,int n,IntPtr v);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetPropW(IntPtr h,string s);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 public static int Children(IntPtr h){int n=0;EnumChildWindows(h,(w,l)=>{n++;return true;},IntPtr.Zero);return n;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessageW(string n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r;if(SendMessageTimeoutW(h,m,(IntPtr)w,(IntPtr)l,2,3500,out r)==IntPtr.Zero)throw new InvalidOperationException("Window did not respond");return unchecked((long)r.ToUInt64());}
 public static long Metric(IntPtr h,int n){return Send(h,RegisterWindowMessageW("Macdows.WindowStyler.Diagnostics.v1"),n);}
 public static bool Ready(IntPtr h){return IsWindow(h)&&Send(h,RegisterWindowMessageW("Macdows.WindowStyler.Query.v1"))==0x4d414357;}
 public static long Hit(IntPtr h,P p){return Send(h,0x84,0,(p.x&65535)|((p.y&65535)<<16));}
 public static void Click(IntPtr h,int index){
  if(index!=1&&index!=2)throw new InvalidOperationException("Only yellow and green are tested");
  if(!Ready(h))throw new InvalidOperationException("Target is not ready");
  int dpi=(int)GetDpiForWindow(h);P p=new P{x=(int)Metric(h,7)+(16+index*20)*dpi/96,y=(int)(Metric(h,8)+Metric(h,10)/2)};ClientToScreen(h,ref p);
  if(GetAncestor(WindowFromPoint(p),2)!=h||Hit(h,p)!=(index==1?8:9))throw new InvalidOperationException("Pointer target/action mismatch");
  P saved;GetCursorPos(out saved);try{SetForegroundWindow(h);SetCursorPos(p.x,p.y);System.Threading.Thread.Sleep(120);P actual;GetCursorPos(out actual);Console.WriteLine("pointer index="+index+" expected="+p.x+","+p.y+" actual="+actual.x+","+actual.y+" foreground="+GetForegroundWindow()+" target="+h);if(actual.x!=p.x||actual.y!=p.y||GetForegroundWindow()!=h)throw new InvalidOperationException("Pointer/foreground did not settle");mouse_event(2,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(60);mouse_event(4,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(120);}finally{SetCursorPos(saved.x,saved.y);}
 }
}
'@
[void][ViewsVerify]::SetProcessDpiAwarenessContext([IntPtr](-4))
$target=[IntPtr][Convert]::ToInt64($Hwnd.Substring(2),16)
if(-not [ViewsVerify]::IsWindow($target)){throw 'Target does not exist'}
if([ViewsVerify]::GetPropW($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero){throw 'Target already has a controller; pause it before this isolated check'}
[void](New-Item -ItemType Directory -Force $OutputDirectory)
$placement=[ViewsVerify+Placement]::new();$placement.size=[Runtime.InteropServices.Marshal]::SizeOf($placement)
if(-not [ViewsVerify]::GetWindowPlacement($target,[ref]$placement)){throw 'Cannot save placement'}
$foreground=[ViewsVerify]::GetForegroundWindow();$topmost=([ViewsVerify]::GetWindowLongPtrW($target,-20).ToInt64() -band 8) -ne 0
$worker=$null;$owner=[IntPtr]::Zero
$result=[ordered]@{hwnd=$Hwnd;frames=[ordered]@{};checks=[ordered]@{};passed=$false}
function Wait-For([scriptblock]$Test,[string]$Failure){$end=[DateTime]::UtcNow.AddSeconds(5);do{if(&$Test){return};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $end);throw $Failure}
function Tabs {
 $root=[Windows.Automation.AutomationElement]::FromHandle($target)
 $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TabItem)
 $items=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
 $r=[ViewsVerify+R]::new();[void][ViewsVerify]::GetWindowRect($target,[ref]$r);$limit=$r.t+64*[ViewsVerify]::GetDpiForWindow($target)/96.
 $ids=@();foreach($item in $items){$b=$item.Current.BoundingRectangle;if(-not $item.Current.IsOffscreen -and $b.Width -gt 0 -and $b.Height -gt 0 -and $b.Top -lt $limit){$ids+=($item.GetRuntimeId()-join '.')}}
 return ,$ids
}
function Capture([string]$Name,[bool]$Applied){
 $r=[ViewsVerify+R]::new();$c=[ViewsVerify+R]::new();[void][ViewsVerify]::GetWindowRect($target,[ref]$r);[void][ViewsVerify]::GetClientRect($target,[ref]$c)
 $origin=[ViewsVerify+P]::new();[void][ViewsVerify]::ClientToScreen($target,[ref]$origin);$scale=[ViewsVerify]::GetDpiForWindow($target)/96.
 $x=[Math]::Max(0,$r.l);$y=[Math]::Max(0,$r.t);$w=[Math]::Min([ViewsVerify]::GetSystemMetrics(0),$r.r)-$x;$height=[int][Math]::Ceiling(85*$scale)
 $b=[Drawing.Bitmap]::new($w,$height);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen($x,$y,0,0,$b.Size);$g.Dispose();$b.Save((Join-Path $OutputDirectory ($Name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
 $hits=@();$colors=@();$right=@();$cy=$origin.y+[int](20.5*$scale)
 if($Applied){$cy=$origin.y+[int]([ViewsVerify]::Metric($target,8)+[ViewsVerify]::Metric($target,10)/2)}
 foreach($index in @(0,1,2)){
  $p=[ViewsVerify+P]::new();$p.x=$origin.x+[int]((16+$index*20)*$scale);$p.y=$cy;$hits+=[ViewsVerify]::Hit($target,$p)
  $color=$b.GetPixel(($p.x-$x),($p.y-$y));$colors+=('{0:X2}{1:X2}{2:X2}' -f $color.R,$color.G,$color.B)
  $p.x=$origin.x+$c.r-[int]((23+46*$index)*$scale);$right+=[ViewsVerify]::Hit($target,$p)
 }
 $b.Dispose();$tabs=Tabs
 $result.frames[$Name]=[ordered]@{rect=@($r.l,$r.t,$r.r,$r.b);client=@($c.r,$c.b);children=[ViewsVerify]::Children($target);hits=$hits;colors=$colors;rightHits=$right;tabs=$tabs}
 if($Applied){
  if(-not [ViewsVerify]::Ready($target)){throw "${Name}: adapter not ready"}
  if(($hits -join ',') -ne '20,8,9'){throw "${Name}: incorrect native actions"}
  if(($colors -join ',') -ne 'FF5F57,FFBD2E,28C840'){throw "${Name}: lights missing"}
  if(@($right|Where-Object {$_ -in @(8,9,20)}).Count){throw "${Name}: native caption actions remain on the right"}
  if(($tabs -join ',') -ne ($baselineTabs -join ',')){throw "${Name}: original top-row tabs disappeared or were replaced"}
  if([ViewsVerify]::Children($target) -ne $baselineChildren){throw "${Name}: HWND tree changed"}
 }
}
try{
 [void][ViewsVerify]::ShowWindow($target,9);[void][ViewsVerify]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13);[void][ViewsVerify]::SetForegroundWindow($target);Start-Sleep -Milliseconds 650
 $baselineTabs=Tabs;if(-not $baselineTabs.Count){throw 'No standard top-row tab is available for this check'}
 $baselineChildren=[ViewsVerify]::Children($target);$style=[ViewsVerify]::GetWindowLongPtrW($target,-16)
 Capture before $false
 $watch=[Diagnostics.Stopwatch]::StartNew()
 $worker=Start-Process -FilePath (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$target.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $OutputDirectory worker-error.txt)
 Wait-For {[ViewsVerify]::Ready($target)} 'Adapter did not acknowledge'
 $result.attachAcknowledgementMs=$watch.ElapsedMilliseconds
 $owner=[ViewsVerify]::GetPropW($target,'Macdows.WindowStyler.Owner.v1')
 if([ViewsVerify]::Metric($target,13) -ne 9){throw 'Wrong rendering backend'}
 Start-Sleep -Milliseconds 100;Capture attached $true
 [ViewsVerify]::Click($target,2);Wait-For {[ViewsVerify]::IsZoomed($target)} 'Green did not maximize';Start-Sleep -Milliseconds 350;Capture green-maximize $true
 [ViewsVerify]::Click($target,2);Wait-For {-not [ViewsVerify]::IsZoomed($target)} 'Green did not restore';Start-Sleep -Milliseconds 350;Capture green-restore $true
 [ViewsVerify]::Click($target,1);Wait-For {[ViewsVerify]::IsIconic($target)} 'Yellow did not minimize'
 $result.checks.yellowMinimizes=$true;[void][ViewsVerify]::ShowWindow($target,9);Start-Sleep -Milliseconds 500;Capture yellow-restore $true
 $r=[ViewsVerify+R]::new();[void][ViewsVerify]::GetWindowRect($target,[ref]$r)
 foreach($step in @(1,2,3,0)){
  [void][ViewsVerify]::SetWindowPos($target,[IntPtr]::Zero,0,0,($r.r-$r.l-80*$step),($r.b-$r.t-20*$step),0x16)
  Start-Sleep -Milliseconds 180;Capture ("resize-"+$step) $true
 }
 [void][ViewsVerify]::Send($target,0x31a);Start-Sleep -Milliseconds 250;Capture theme-notification $true
 [void][ViewsVerify]::SetWindowLongPtrW($target,-16,$style);[void][ViewsVerify]::SetWindowPos($target,[IntPtr]::Zero,0,0,0,0,0x37)
 Start-Sleep -Milliseconds 250;Capture style-reassert $true
 $message=[ViewsVerify]::RegisterWindowMessageW('Macdows.WindowStyler.Host.v2')
 foreach($cycle in @(1,2)){
  if([ViewsVerify]::Send($owner,$message,2,$target.ToInt64()) -ne 1){throw 'Session restore failed'}
  Wait-For {[ViewsVerify]::GetPropW($target,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero} 'Restore lease remained'
  Start-Sleep -Milliseconds 180;Capture ("detached-"+$cycle) $false
  if([ViewsVerify]::Send($owner,$message,1,$target.ToInt64()) -ne 1){throw 'Reapply in the same session failed'}
  Wait-For {[ViewsVerify]::Ready($target)} 'Reapply did not acknowledge'
  Start-Sleep -Milliseconds 180;Capture ("reapplied-"+$cycle) $true
 }
 $result.checks.greenMaximizes=$true;$result.checks.greenRestores=$true;$result.checks.repeatApply=$true
 [uint32]$ownerProcess=0;[void][ViewsVerify]::GetWindowThreadProcessId($owner,[ref]$ownerProcess)
 $helper=Get-Process -Id $ownerProcess
 if($helper.Path -ne (Resolve-Path (Join-Path $ReleaseDirectory 'WindowStyleHost.exe')).Path){throw 'Unexpected helper identity'}
 Stop-Process -Id $ownerProcess
 Wait-For {[ViewsVerify]::GetPropW($target,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero} 'Owner-death rollback failed'
 Start-Sleep -Milliseconds 200;Capture owner-death-restored $false
 $f=$result.frames['owner-death-restored'];$before=$result.frames.before
 $result.checks.geometryRestored=(($f.rect-join ',') -eq ($before.rect-join ',') -and ($f.client-join ',') -eq ($before.client-join ','))
 $result.checks.tabsRestored=(($f.tabs-join ',') -eq ($baselineTabs-join ','))
 $result.checks.rightButtonsRestored=(($f.rightHits-join ',') -eq ($before.rightHits-join ','))
 $result.checks.ownerDeathRestores=$true;$result.checks.alive=[ViewsVerify]::IsWindow($target)
 $result.passed=@($result.checks.Values|Where-Object {-not $_}).Count -eq 0
 if(-not $result.passed){throw 'A final rollback check failed'}
}catch{$result.error=$_.Exception.Message;$result.ownerAtFailure=[ViewsVerify]::GetPropW($target,'Macdows.WindowStyler.Owner.v1').ToInt64();$result.kindAtFailure=[ViewsVerify]::Metric($target,13);$result.workerExitedAtFailure=if($worker){$worker.HasExited}else{$null};throw}finally{
 if($owner -ne [IntPtr]::Zero -and [ViewsVerify]::IsWindow($owner)){[void][ViewsVerify]::Send($owner,[ViewsVerify]::RegisterWindowMessageW('Macdows.WindowStyler.Host.v2'),2,$target.ToInt64())}
 if($worker -and -not $worker.HasExited){Stop-Process -Id $worker.Id}
 if([ViewsVerify]::IsWindow($target)){if(-not $topmost){[void][ViewsVerify]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)};[void][ViewsVerify]::SetWindowPlacement($target,[ref]$placement)}
 [void][ViewsVerify]::SetForegroundWindow($foreground)
 $result|ConvertTo-Json -Depth 10|Set-Content (Join-Path $OutputDirectory result.json) -Encoding UTF8
}
$result|ConvertTo-Json -Depth 10
