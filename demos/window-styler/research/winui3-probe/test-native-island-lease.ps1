param(
    [string]$FixtureDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
    [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research/Release'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v36/native-lease-Release')
)
$ErrorActionPreference='Stop'
$fixturePath=Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
$hostPath=Join-Path (Resolve-Path -LiteralPath $HostDirectory).Path 'WinUI3NativeIslandHost.exe'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory to preserve earlier evidence'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'capture-guard.ps1')
. (Join-Path $PSScriptRoot 'capture-backdrop.ps1')
. (Join-Path $PSScriptRoot 'host-readiness.ps1')
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandLeaseContract {
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
 [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int command);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,2000,out r)==IntPtr.Zero)throw new Exception("Fixture timeout");return unchecked((long)r.ToUInt64());}
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,p.x,p.y};}
}
'@
[void][NativeIslandLeaseContract]::SetProcessDpiAwarenessContext([IntPtr](-4))
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
function Owner($handle){return [NativeIslandLeaseContract]::GetProp($handle,'Macdows.WindowStyler.Owner.v1')}
function Metric($handle,[int]$index){return [NativeIslandLeaseContract]::Send($handle,0x804a,$index)}
function Send($handle,[int]$operation){
 $r=[NativeIslandLeaseContract]::Send($handle,0x8049,$operation)
 if($r -ne 0){throw ('Fixture command failed: '+$r)}
}
function TitlePixel($handle,[string]$name){
 [void][NativeIslandLeaseContract]::SetForegroundWindow($handle)
 [NativeIslandCaptureBackdrop]::Place($handle)
 Start-Sleep -Milliseconds 700
 $g=[NativeIslandLeaseContract]::Geometry($handle)
 [WindowStylerCaptureGuard]::Check($handle,$g[0],$g[1],($g[2]-$g[0]),($g[3]-$g[1]))
 $b=[Drawing.Bitmap]::new(($g[2]-$g[0]),($g[3]-$g[1]))
 $d=[Drawing.Graphics]::FromImage($b)
 try{
  $d.CopyFromScreen($g[0],$g[1],0,0,$b.Size)
  $b.Save((Join-Path $OutputDirectory ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
  return ($b.GetPixel(($g[8]-$g[0]+300),($g[9]-$g[1]+20)).ToArgb() -band 0xFFFFFF)
 }finally{$d.Dispose();$b.Dispose()}
}
$foreground=[NativeIslandLeaseContract]::GetForegroundWindow()
$fixture=$null;$worker=$null
try{
 $fixture=Start-Process -FilePath $fixturePath -ArgumentList '--dark' -WindowStyle Hidden -PassThru
 Check (Until {[NativeIslandLeaseContract]::Find($fixture.Id) -ne [IntPtr]::Zero}) 'owned fixture starts'
 $h=[NativeIslandLeaseContract]::Find($fixture.Id)
 [void][NativeIslandLeaseContract]::SetWindowPos($h,[IntPtr]::Zero,0,0,0,0,0x57)
 Check (Until {(Metric $h 5) -gt 0 -and (Metric $h 6) -gt 0}) 'owned fixture is visible and laid out'
 Start-Sleep -Milliseconds 500
 Check ((Owner $h) -eq [IntPtr]::Zero) 'fixture has no preexisting lease'
 $geometry=[NativeIslandLeaseContract]::Geometry($h)
 $style=[NativeIslandLeaseContract]::GetWindowLongPtr($h,-16)
 foreach($scenario in @('normal','owner-process-loss','minimized-owner-process-loss','root-replacement')){
  $crash=$scenario -in @('owner-process-loss','minimized-owner-process-loss')
  $worker=[Diagnostics.Process]::new()
  $worker.StartInfo.FileName=$hostPath
  $worker.StartInfo.Arguments=$h.ToInt64().ToString()+' '+$(if($crash){'30'}else{'4'})
  $worker.StartInfo.UseShellExecute=$false;$worker.StartInfo.CreateNoWindow=$true
  $worker.StartInfo.RedirectStandardOutput=$true;$worker.StartInfo.RedirectStandardError=$true
  [void]$worker.Start();$hostOutput=[NativeIslandHostOutput]::new($worker.StandardOutput);$stdout=$hostOutput.Completion;$stderr=$worker.StandardError.ReadToEndAsync()
  Check (Until {$hostOutput.IsReady -and (Owner $h) -ne [IntPtr]::Zero}) ($scenario+' attached after verification host confirms Session Apply')
  Check ((Metric $h 10) -eq 0) ($scenario+' original presenter caption removed')
  Check ((Metric $h 9) -eq 1) ($scenario+' original content root remains installed')
  if($scenario -eq 'normal'){
   Send $h 3
   Check (Until {(Metric $h 7) -eq 1}) 'host subtree switches to light without native notifications'
   Check ((TitlePixel $h 'normal-light') -eq 0xF3F3F3) 'research title follows light theme and background'
   Send $h 4
   Check (Until {(Metric $h 7) -eq 2}) 'host subtree switches to dark without native notifications'
   Check ((TitlePixel $h 'normal-dark') -eq 0x202020) 'research title follows dark theme and background'
  }
  if($scenario -eq 'minimized-owner-process-loss'){
   $viewport=@((Metric $h 5),(Metric $h 6),(Metric $h 13),(Metric $h 14))
   [void][NativeIslandLeaseContract]::ShowWindow($h,6)
   Check (Until {[NativeIslandLeaseContract]::IsIconic($h)}) 'fixture minimizes while attached'
   Check (($viewport -join ',') -eq (@((Metric $h 5),(Metric $h 6),(Metric $h 13),(Metric $h 14)) -join ',')) 'minimizing preserves original viewport'
  }
  if($crash){
   Check ($worker.MainModule.FileName -eq $hostPath) 'crash target is the owned research host'
   $worker.Kill();Check ($worker.WaitForExit(5000)) 'owned research host terminates'
  }elseif($scenario -eq 'root-replacement'){
   Send $h 5
   Check ((Metric $h 11) -eq 1) 'host installs its replacement root'
  }
  Check (Until {(Owner $h) -eq [IntPtr]::Zero}) ($scenario+' lease released')
  Check ((Metric $h 10) -eq 1) ($scenario+' original presenter caption restored')
  if($scenario -eq 'minimized-owner-process-loss'){
   Check ([NativeIslandLeaseContract]::IsIconic($h)) 'owner loss leaves the fixture minimized'
   Check (($viewport -join ',') -eq (@((Metric $h 5),(Metric $h 6),(Metric $h 13),(Metric $h 14)) -join ',')) 'minimized rollback preserves original viewport'
   [void][NativeIslandLeaseContract]::ShowWindow($h,9)
   Check (Until {-not [NativeIslandLeaseContract]::IsIconic($h) -and ($geometry -join ',') -eq ([NativeIslandLeaseContract]::Geometry($h) -join ',')}) 'restoring minimized fixture recovers original geometry'
  }
  Check (($geometry -join ',') -eq ([NativeIslandLeaseContract]::Geometry($h) -join ',')) ($scenario+' original geometry restored')
  Check ($style -eq [NativeIslandLeaseContract]::GetWindowLongPtr($h,-16)) ($scenario+' original capabilities restored')
  Check ((Metric $h 9) -eq 1) ($scenario+' host root preserved during rollback')
  if($scenario -eq 'root-replacement'){Check ((Metric $h 11) -eq 1) 'rollback retains the replacement root'}
  if(-not $crash){Check ($worker.WaitForExit(7000) -and $worker.ExitCode -eq 0) ($scenario+' research host exits successfully')}
  $stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($scenario+'.log')) -Encoding utf8
  $stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($scenario+'.err')) -Encoding utf8
  $worker.Dispose();$worker=$null
 }
 $worker=[Diagnostics.Process]::new()
 $worker.StartInfo.FileName=$hostPath
 $worker.StartInfo.Arguments=$h.ToInt64().ToString()+' 30'
 $worker.StartInfo.UseShellExecute=$false;$worker.StartInfo.CreateNoWindow=$true
 $worker.StartInfo.RedirectStandardOutput=$true;$worker.StartInfo.RedirectStandardError=$true
 [void]$worker.Start();$hostOutput=[NativeIslandHostOutput]::new($worker.StandardOutput);$stdout=$hostOutput.Completion;$stderr=$worker.StandardError.ReadToEndAsync()
 Check (Until {$hostOutput.IsReady -and (Owner $h) -ne [IntPtr]::Zero}) 'fixture-close attached after verification host confirms Session Apply'
 [void][NativeIslandLeaseContract]::Send($h,0x10)
 Check ($fixture.WaitForExit(5000) -and $fixture.ExitCode -eq 0) 'owned fixture exits successfully'
 Check (-not [NativeIslandLeaseContract]::IsWindow($h)) 'owned target HWND is destroyed'
 Check ($worker.WaitForExit(7000) -and $worker.ExitCode -eq 0) 'research host releases destroyed target session successfully'
 $stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory 'fixture-close.log') -Encoding utf8
 $stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory 'fixture-close.err') -Encoding utf8
 $worker.Dispose();$worker=$null
 [ordered]@{passed=$true;checks=$checks.Count;fixtureHash=(Get-FileHash -LiteralPath $fixturePath).Hash;hostHash=(Get-FileHash -LiteralPath $hostPath).Hash;scope='owned full-client native WinUI fixture and isolated research host only'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
}catch{
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 throw
}finally{
 if($worker){
  if(-not $worker.HasExited){[void]$worker.WaitForExit(32000)}
  if($worker.HasExited){$stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory 'unfinished-host.log') -Encoding utf8;$stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory 'unfinished-host.err') -Encoding utf8}
  $worker.Dispose()
 }
 if($fixture){if(-not $fixture.HasExited){$owned=[NativeIslandLeaseContract]::Find($fixture.Id);if($owned -ne [IntPtr]::Zero -and (Owner $owned) -eq [IntPtr]::Zero){[void][NativeIslandLeaseContract]::Send($owned,0x10);[void]$fixture.WaitForExit(5000)}};$fixture.Dispose()}
 [void][NativeIslandLeaseContract]::SetForegroundWindow($foreground)
 [NativeIslandCaptureBackdrop]::Close()
}
