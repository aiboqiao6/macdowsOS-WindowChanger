param(
 [string]$FixtureDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v38/native-initialization-Release')
)
$ErrorActionPreference='Stop'
$fixturePath=Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
$hostPath=Join-Path (Resolve-Path -LiteralPath $HostDirectory).Path 'WinUI3NativeIslandHost.exe'
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandInitialization {
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 [StructLayout(LayoutKind.Sequential)] public struct R {public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)] public struct P {public int x,y;}
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent,EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,5000,out r)==IntPtr.Zero)throw new Exception("Fixture timeout");return unchecked((long)r.ToUInt64());}
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static int Children(IntPtr h){int n=0;EnumChildWindows(h,(c,l)=>{if(GetParent(c)==h)n++;return true;},IntPtr.Zero);return n;}
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,p.x,p.y};}
}
'@
[void][NativeIslandInitialization]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new()
function Check($value,[string]$name){
 $checks.Add([pscustomobject]@{name=$name;passed=[bool]$value})
 $checks|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputDirectory 'checks.json') -Encoding utf8
 if(-not $value){throw ('Check failed: '+$name)}
}
function Until([scriptblock]$test){
 $deadline=[DateTime]::UtcNow.AddSeconds(8)
 do{if(&$test){return $true};Start-Sleep -Milliseconds 80}while([DateTime]::UtcNow -lt $deadline)
 return $false
}
function Metric([int]$index){[NativeIslandInitialization]::Send($script:h,0x804a,$index)}
function Owner {[NativeIslandInitialization]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1')}
function Shot([string]$name){
 $g=[NativeIslandInitialization]::Geometry($script:h)
 [void][NativeIslandInitialization]::SetForegroundWindow($script:h)
 [void][NativeIslandInitialization]::SetCursorPos(($g[0]+500),($g[1]+20))
 Start-Sleep -Milliseconds 700
 $b=[Drawing.Bitmap]::new(($g[2]-$g[0]),($g[3]-$g[1]));$d=[Drawing.Graphics]::FromImage($b)
 try{$d.CopyFromScreen($g[0],$g[1],0,0,$b.Size);$b.Save((Join-Path $OutputDirectory ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$d.Dispose();$b.Dispose()}
 return (Get-FileHash (Join-Path $OutputDirectory ($name+'.png'))).Hash
}
function StartResearch([string]$name,[int]$seconds=30){
 $script:run=$name
 $script:worker=[Diagnostics.Process]::new()
 $script:worker.StartInfo.FileName=$hostPath;$script:worker.StartInfo.Arguments=$script:h.ToInt64().ToString()+' '+$seconds
 $script:worker.StartInfo.UseShellExecute=$false;$script:worker.StartInfo.CreateNoWindow=$true
 $script:worker.StartInfo.RedirectStandardOutput=$true;$script:worker.StartInfo.RedirectStandardError=$true
 [void]$script:worker.Start();$script:stdout=$script:worker.StandardOutput.ReadToEndAsync();$script:stderr=$script:worker.StandardError.ReadToEndAsync()
}
function FinishResearch([int[]]$expected){
 Check ($script:worker.WaitForExit(10000) -and $script:worker.ExitCode -in $expected) ($script:run+' host exits with expected status')
 $script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:run+'.log')) -Encoding utf8
 $script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:run+'.err')) -Encoding utf8
 $script:worker.Dispose();$script:worker=$null
}
$foreground=[NativeIslandInitialization]::GetForegroundWindow()
$cursor=[NativeIslandInitialization+P]::new();[void][NativeIslandInitialization]::GetCursorPos([ref]$cursor)
$script:fixture=$null;$script:worker=$null;$script:h=[IntPtr]::Zero
try {
 foreach($mode in @(12,14,18,13,17)){
  $name=@{12='root-replacement';13='host-close';14='nested-detach';17='system-close';18='cancel-close'}[$mode]
  $fixture=Start-Process -FilePath $fixturePath -ArgumentList '--dark' -WindowStyle Hidden -PassThru
  Check (Until {[NativeIslandInitialization]::Find($fixture.Id) -ne [IntPtr]::Zero}) ($name+' owned fixture starts')
  $h=[NativeIslandInitialization]::Find($fixture.Id)
  [void][NativeIslandInitialization]::SetWindowPos($h,[IntPtr](-1),160,160,1200,720,0x40)
  Check (Until {(Metric 5) -gt 0 -and (Metric 6) -gt 0}) ($name+' fixture is laid out')
  Check ((Owner) -eq [IntPtr]::Zero) ($name+' no preexisting lease')
  $geometry=[NativeIslandInitialization]::Geometry($h);$children=[NativeIslandInitialization]::Children($h)
  $before=Shot ($name+'-before')
  Check (([NativeIslandInitialization]::Send($h,0x8049,$mode)) -eq 0) ($name+' initialization fault armed')
  StartResearch $name
  FinishResearch $(if($mode -in @(13,17,18)){@(0,50)}else{@(50)})
  $trace=Join-Path $FixtureDirectory ('native-island-initialization-'+$fixture.Id+'.txt')
  Check (Test-Path -LiteralPath $trace) ($name+' callback trace exists')
  $traceText=Get-Content -LiteralPath $trace -Raw
  $traceText|Set-Content -LiteralPath (Join-Path $OutputDirectory ($name+'-callback.txt')) -Encoding utf8
  Check ($traceText.Contains(('mode='+$mode+' beforeLease=1'))) ($name+' fault runs inside initialization before lease publication')
  Check ($traceText.Contains('completed=1 aliveAfterCallback=1') -and -not $traceText.Contains('HRESULT=')) ($name+' callback completes before the host HWND is destroyed')
  if($mode -in @(13,17)){
   $exited=$fixture.WaitForExit(8000)
   [ordered]@{exited=$exited;exitCode=$(if($exited){$fixture.ExitCode}else{$null})}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory ($name+'-exit.json')) -Encoding utf8
   Check ($exited -and $fixture.ExitCode -eq 0) ($name+' fixture exits without a crash')
   Check (-not [NativeIslandInitialization]::IsWindow($h)) ($name+' target HWND is destroyed')
  }else{
   Check (-not $fixture.HasExited -and (Metric 44) -eq 1 -and (Metric 45) -eq 1) ($name+' owned fixture survives exactly one nested callback')
   foreach($property in @('Owner.v1','Inline.v4','Adapter.v1','Request.v1','Result.v1')){
    Check (([NativeIslandInitialization]::GetProp($h,('Macdows.WindowStyler.'+$property))) -eq [IntPtr]::Zero) ($name+' clears '+$property)
   }
   Check ((Metric 10) -eq 1 -and (Metric 35) -eq 0) ($name+' native title restored')
   Check (($geometry -join ',') -eq ([NativeIslandInitialization]::Geometry($h) -join ',')) ($name+' native geometry restored')
   Check ([NativeIslandInitialization]::Children($h) -eq $children) ($name+' temporary title island is removed')
   Check ((Metric 9) -eq 1) ($name+' current host root retained')
   if($mode -eq 12){Check ((Metric 11) -eq 1) 'replacement root is not overwritten by aborted initialization'}
   if($mode -eq 14){
    Check ((Metric 46) -eq 0) 'nested detach defers acknowledgement during Apply'
    Check ($before -eq (Shot ($name+'-restored'))) 'aborted initialization restores whole-window pixels'
   }
   if($mode -eq 18){
    Check ((Metric 47) -eq 1) 'host receives exactly one deferred close and cancels it'
    Check ($before -eq (Shot ($name+'-restored'))) 'cancelled close restores whole-window pixels'
   }
   StartResearch ($name+'-reattach') 3
   Check (Until {(Owner) -ne [IntPtr]::Zero -or $worker.HasExited}) ($name+' later attach completes')
   Check ((Owner) -ne [IntPtr]::Zero) ($name+' later attach succeeds')
   FinishResearch 0
   Check ((Owner) -eq [IntPtr]::Zero -and (Metric 10) -eq 1 -and (Metric 9) -eq 1) ($name+' later attach restores current host root')
   Check ([NativeIslandInitialization]::Find($fixture.Id) -eq $h) ($name+' close identity matches owned fixture')
   [void][NativeIslandInitialization]::Send($h,0x10)
   Check ($fixture.WaitForExit(5000) -and $fixture.ExitCode -eq 0) ($name+' fixture closes successfully')
  }
  $fixture.Dispose();$fixture=$null
 }
 [ordered]@{passed=$true;checks=$checks.Count;fixtureHash=(Get-FileHash $fixturePath).Hash;hostHash=(Get-FileHash $hostPath).Hash;scope='owned 144 DPI WinUI initialization callbacks only'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
}catch{
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 throw
}finally{
 if($worker){if(-not $worker.HasExited){[void]$worker.WaitForExit(32000)};if($worker.HasExited){$stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.log')) -Encoding utf8;$stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.err')) -Encoding utf8};$worker.Dispose()}
 if($fixture){if(-not $fixture.HasExited){$owned=[NativeIslandInitialization]::Find($fixture.Id);if($owned -ne [IntPtr]::Zero -and $fixture.Path -eq $fixturePath -and ([NativeIslandInitialization]::GetProp($owned,'Macdows.WindowStyler.Owner.v1')) -eq [IntPtr]::Zero){[void][NativeIslandInitialization]::Send($owned,0x10);[void]$fixture.WaitForExit(5000)}};$fixture.Dispose()}
 [void][NativeIslandInitialization]::SetCursorPos($cursor.x,$cursor.y)
 [void][NativeIslandInitialization]::SetForegroundWindow($foreground)
}
