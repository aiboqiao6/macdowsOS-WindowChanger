param([string]$ReleaseDirectory=(Join-Path $PSScriptRoot 'out/build-x64/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v14/winui3-contract'))
$ErrorActionPreference='Stop'
# Load the matching shell module explicitly when launched from VS/CTest;
# an inherited PowerShell 7 module path can otherwise hide Get-FileHash in 5.1.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$ReleaseDirectory=(Resolve-Path -LiteralPath $ReleaseDirectory).Path
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Collections.Generic;
public static class WinUI3TestNative{
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr h);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr data);
 [DllImport("user32.dll")]public static extern IntPtr GetProp(IntPtr h,string name);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr extra);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,(IntPtr)l,2,2000,out r)==IntPtr.Zero)throw new Exception("message timeout");return unchecked((long)r.ToUInt64());}
 public static long Metric(IntPtr h,int n){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1"),n);}
 public static bool Ready(IntPtr h){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"))==0x4d414357;}
 public static IntPtr[] Children(IntPtr h){var result=new List<IntPtr>();EnumChildWindows(h,(w,l)=>{result.Add(w);return true;},IntPtr.Zero);return result.ToArray();}
 public static long[] Geometry(IntPtr h){R r,c;GetWindowRect(h,out r);GetClientRect(h,out c);var p=new P();ClientToScreen(h,ref p);return new long[]{r.l,r.t,r.r,r.b,c.r,c.b,p.x,p.y};}
 public static void Click(IntPtr h,int i){var p=new P{x=(int)(Metric(h,7)+(18+20*i)*GetDpiForWindow(h)/96),y=(int)(Metric(h,8)+Metric(h,10)/2)};ClientToScreen(h,ref p);SetForegroundWindow(h);SetCursorPos(p.x,p.y);System.Threading.Thread.Sleep(80);mouse_event(2,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(50);mouse_event(4,0,0,0,UIntPtr.Zero);}
}
'@
[void][WinUI3TestNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new()
function Check($pass,$name){$checks.Add([pscustomobject]@{name=$name;passed=[bool]$pass});$checks|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'checks.json') -Encoding UTF8;if(-not $pass){throw "Check failed: $name"}}
function Until([scriptblock]$test){$end=[DateTime]::UtcNow.AddSeconds(5);do{if(&$test){return $true};Start-Sleep -Milliseconds 60}while([DateTime]::UtcNow -lt $end);return $false}
function UI($h){
 $items=[Collections.Generic.List[object]]::new();$seen=@{}
 foreach($surface in @($h)+@([WinUI3TestNative]::Children($h))){
  try{$root=[Windows.Automation.AutomationElement]::FromHandle($surface);$nodes=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
   foreach($node in $nodes){$n=$node.Current.Name;$b=$node.Current.BoundingRectangle;$key="$n|$b";if($n -and -not $seen.ContainsKey($key)){$seen[$key]=$true;$items.Add([pscustomobject]@{name=$n;rect=@($b.X,$b.Y,$b.Width,$b.Height);type=$node.Current.ControlType.ProgrammaticName})}}
  }catch{}
 }
 return $items.ToArray()
}
function TextRect($h,[string]$name){$items=@(UI $h|Where-Object name -eq $name);if($items.Count -ne 1){throw "Expected one fixture text '$name', got $($items.Count)"};return $items[0].rect}
function Shot($h,$name){
 [void][WinUI3TestNative]::SetForegroundWindow($h);Start-Sleep -Milliseconds 150
 $r=[WinUI3TestNative+R]::new();[void][WinUI3TestNative]::GetWindowRect($h,[ref]$r)
 $bmp=[Drawing.Bitmap]::new($r.r-$r.l,$r.b-$r.t);$g=[Drawing.Graphics]::FromImage($bmp)
 try{$g.CopyFromScreen($r.l,$r.t,0,0,$bmp.Size);$bmp.Save((Join-Path $OutputDirectory ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$bmp.Dispose()}
 UI $h|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory ($name+'-uia.json')) -Encoding UTF8
}
function Worker($h,$name){return Start-Process (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$h.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory ($name+'-worker.log')) -RedirectStandardError (Join-Path $OutputDirectory ($name+'-worker.err'))}
$foreground=[WinUI3TestNative]::GetForegroundWindow();$cursor=[WinUI3TestNative+P]::new();[void][WinUI3TestNative]::GetCursorPos([ref]$cursor)
try{
 foreach($layout in @('plain','columns','binding')){
  $fixture=$null;$worker=$null;$h=[IntPtr]::Zero
  try{
   $start=@{FilePath=(Join-Path $ReleaseDirectory 'WinUI3InlineTarget.exe');PassThru=$true};if($layout -eq 'columns'){$start.ArgumentList='--columns'};if($layout -eq 'binding'){$start.ArgumentList='--binding'}
   $fixture=Start-Process @start
   Check (Until {$fixture.Refresh();$fixture.MainWindowHandle -ne [IntPtr]::Zero}) "$layout starts"
   $h=$fixture.MainWindowHandle;Check ([WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Sample') -ne [IntPtr]::Zero) "$layout is an owned fixture"
   [void][WinUI3TestNative]::SetWindowPos($h,[IntPtr](-1),160,160,1200,720,0x40);Start-Sleep -Milliseconds 300
   $before=[WinUI3TestNative]::Geometry($h);$children=[WinUI3TestNative]::Children($h).Count
   $text=if($layout -eq 'columns'){'Rebuilt original title content'}else{'Original title content'}
   $beforeText=TextRect $h $text;$beforeBody=TextRect $h 'WinUI 3 original layout fixture';Shot $h "$layout-before"
   $worker=Worker $h "$layout-first";Check (Until {[WinUI3TestNative]::Ready($h)}) "$layout attaches through generic injector"
   Check ([WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Adapter.v1').ToInt64() -eq 266) "$layout selects WinUI 3 backend"
   Check (($before -join ',') -eq ([WinUI3TestNative]::Geometry($h) -join ',')) "$layout first attach preserves outer/client geometry"
   Check ($children -eq [WinUI3TestNative]::Children($h).Count) "$layout adds no HWND"
   $afterText=TextRect $h $text;$afterBody=TextRect $h 'WinUI 3 original layout fixture';$scale=[WinUI3TestNative]::GetDpiForWindow($h)/96
   [ordered]@{before=$beforeText;after=$afterText;bodyBefore=$beforeBody;bodyAfter=$afterBody;scale=$scale;geometry=$before;metrics=@(7,8,9,10,11,12|ForEach-Object {[WinUI3TestNative]::Metric($h,$_)} )}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory ($layout+'-geometry.json')) -Encoding UTF8
   Shot $h "$layout-applied"
   Check ([Math]::Abs($afterText[0]-$beforeText[0]-84*$scale) -lt 2 -and $afterText[1] -eq $beforeText[1]) "$layout shifts original content within its row"
   Check (($beforeBody -join ',') -eq ($afterBody -join ',')) "$layout preserves body position"
   Check ([WinUI3TestNative]::Metric($h,12) -eq 0) "$layout hides original caption buttons";Shot $h "$layout-applied"
   [WinUI3TestNative]::Click($h,2);Check (Until {[WinUI3TestNative]::IsZoomed($h)}) "$layout native green click maximizes"
   [WinUI3TestNative]::Click($h,2);Check (Until {-not [WinUI3TestNative]::IsZoomed($h)}) "$layout native green click restores"
   [WinUI3TestNative]::Click($h,1);Check (Until {[WinUI3TestNative]::IsIconic($h)}) "$layout native yellow click minimizes";[void][WinUI3TestNative]::ShowWindow($h,9)
   for($i=0;$i -lt 12;$i++){[void][WinUI3TestNative]::Send($h,0x8028,2);[void][WinUI3TestNative]::Send($h,0x8028,5);[void][WinUI3TestNative]::Send($h,0x8028,8)}
   [ordered]@{ready=[WinUI3TestNative]::Ready($h);owner=[WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Owner.v1').ToInt64();caption=[WinUI3TestNative]::Metric($h,12);paddingResult=[WinUI3TestNative]::Send($h,0x8028,2);captionResult=[WinUI3TestNative]::Send($h,0x8028,5)}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory ($layout+'-host-state.json')) -Encoding UTF8
   Shot $h "$layout-reasserted"
   Check (Until {[WinUI3TestNative]::Ready($h) -and [WinUI3TestNative]::Metric($h,12) -eq 0}) "$layout survives host padding/caption reassertions"
   Shot $h "$layout-reasserted";$afterText=TextRect $h $text;[ordered]@{before=$beforeText;after=$afterText;scale=$scale}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory ($layout+'-padding.json')) -Encoding UTF8;Check ([Math]::Abs($afterText[0]-$beforeText[0]-104*$scale) -lt 2) "$layout reserves width once after repeated host writes"
   [void][WinUI3TestNative]::Send($h,0x8028,3);[void][WinUI3TestNative]::Send($h,0x8028,4);[void][WinUI3TestNative]::Send($h,0x8028,6)
   Shot $h "$layout-host-refresh";[ordered]@{height=[WinUI3TestNative]::Metric($h,10);top=[WinUI3TestNative]::Metric($h,8);scale=$scale}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory ($layout+'-height.json')) -Encoding UTF8
   Check (Until {[WinUI3TestNative]::Metric($h,10) -eq 28*$scale}) "$layout follows original row-height changes";Shot $h "$layout-host-refresh"
   $worker.Kill();$worker.WaitForExit();$worker=$null
   Check (Until {[WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero}) "$layout restores after controller failure"
   $restoredText=TextRect $h $text;Check ([Math]::Abs($restoredText[0]-$beforeText[0]-20*$scale) -lt 2) "$layout restores latest host padding";Shot $h "$layout-restored"
   [void][WinUI3TestNative]::Send($h,0x8028,7);$restoredText=TextRect $h $text;Check ([Math]::Abs($restoredText[0]-$beforeText[0]-40*$scale) -lt 2) "$layout host padding still reacts after detach"
   $worker=Worker $h "$layout-second";Check (Until {[WinUI3TestNative]::Ready($h)}) "$layout attaches again"
   [void][WinUI3TestNative]::Send($h,0x8028,1)
   Check (Until {[WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero}) "$layout detaches obsolete title tree"
   $worker.Kill();$worker.WaitForExit();$worker=$null
   $worker=Worker $h "$layout-rebuilt";Check (Until {[WinUI3TestNative]::Ready($h)}) "$layout discovers rebuilt title without business names";Shot $h "$layout-rebuilt"
   [WinUI3TestNative]::Click($h,0);Check (Until {-not [WinUI3TestNative]::IsWindow($h)}) "$layout native red closes only fixture"
   Check ($fixture.WaitForExit(4000)) "$layout fixture exits cleanly";Check ($fixture.ExitCode -eq 0) "$layout exit code is zero"
  }finally{
   if($worker -and -not $worker.HasExited){$worker.Kill();$worker.WaitForExit()}
   if($fixture -and -not $fixture.HasExited){[void]$fixture.CloseMainWindow();[void]$fixture.WaitForExit(4000)}
  }
 }
 foreach($scenario in @('unmarked','no-title-row','bound-column','dpi-unaware')){
  $fixture=$null;$worker=$null
  try{
   $fixture=Start-Process (Join-Path $ReleaseDirectory 'WinUI3InlineTarget.exe') -ArgumentList ('--'+$scenario) -PassThru
   Check (Until {$fixture.Refresh();$fixture.MainWindowHandle -ne [IntPtr]::Zero}) "$scenario starts"
   $h=$fixture.MainWindowHandle;Start-Sleep -Milliseconds 300;$before=[WinUI3TestNative]::Geometry($h);$children=[WinUI3TestNative]::Children($h).Count
   $worker=Worker $h $scenario;Check ($worker.WaitForExit(4500)) "$scenario rejects without waiting indefinitely"
   Check ($worker.ExitCode -ne 0) "$scenario is not reported as supported"
   Check ([WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero -and [WinUI3TestNative]::GetProp($h,'Macdows.WindowStyler.Inline.v4') -eq [IntPtr]::Zero) "$scenario creates no session"
   Check (($before -join ',') -eq ([WinUI3TestNative]::Geometry($h) -join ',') -and $children -eq [WinUI3TestNative]::Children($h).Count) "$scenario leaves geometry and HWND tree intact"
  }finally{if($worker -and -not $worker.HasExited){$worker.Kill();$worker.WaitForExit()};if($fixture -and -not $fixture.HasExited){[void]$fixture.CloseMainWindow();[void]$fixture.WaitForExit(4000)}}
 }
 [ordered]@{passed=$true;checks=$checks.Count;dpi=$scale*96;scope='owned WinUI 3 fixtures, exact runtime ABI; no user applications tested';backendHash=(Get-FileHash (Join-Path $ReleaseDirectory ((Get-Content (Join-Path $ReleaseDirectory 'adapters.ini')|Where-Object {$_ -like 'File=MacdowsWinUI3Inline*'}).Substring(5)))).Hash;fixtureHash=(Get-FileHash (Join-Path $ReleaseDirectory 'WinUI3InlineTarget.exe')).Hash}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
}catch{$_|Out-String|Set-Content (Join-Path $OutputDirectory 'failure.txt') -Encoding UTF8;throw}
finally{[void][WinUI3TestNative]::SetCursorPos($cursor.x,$cursor.y);[void][WinUI3TestNative]::SetForegroundWindow($foreground)}
