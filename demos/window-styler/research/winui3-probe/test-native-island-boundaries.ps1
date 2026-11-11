param(
 [string]$FixtureDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v37/native-boundaries-Release'),
 [ValidateSet('all','narrow','keyboard','palette')][string]$Scenario='all'
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
public static class NativeIslandBoundary {
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 [StructLayout(LayoutKind.Sequential)] public struct R {public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)] public struct P {public int x,y;}
 [StructLayout(LayoutKind.Sequential)] struct K {public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Sequential)] struct M {public int x,y;public uint data,flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)] struct U {[FieldOffset(0)] public K k;[FieldOffset(0)] public M m;}
 [StructLayout(LayoutKind.Sequential)] struct I {public uint type;public U u;}
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetPhysicalCursorPos(out P p);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(P p);
 [DllImport("user32.dll")] public static extern bool IsChild(IntPtr p,IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
 [DllImport("user32.dll")] static extern uint SendInput(uint n,I[] input,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,2000,out r)==IntPtr.Zero)throw new Exception("Fixture timeout");return unchecked((long)r.ToUInt64());}
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,p.x,p.y};}
 static void Guard(IntPtr h){if(SetThreadDpiAwarenessContext((IntPtr)(-4))==IntPtr.Zero)throw new Exception("Cannot establish physical input coordinates");uint pid;GetWindowThreadProcessId(h,out pid);if(GetForegroundWindow()!=h||Find(pid)!=h)throw new Exception("Owned fixture identity or foreground changed");foreach(var k in new[]{16,17,18,91,92})if(GetAsyncKeyState(k)<0)throw new Exception("User modifier key is held");}
 static I Key(ushort vk,ushort scan,uint flags){return new I{type=1,u=new U{k=new K{vk=vk,scan=scan,flags=flags}}};}
 static void Input(I[] values){if(SendInput((uint)values.Length,values,Marshal.SizeOf(typeof(I)))!=values.Length)throw new Exception("Input rejected");}
 public static void Type(IntPtr h,string text){Guard(h);var a=new I[text.Length*2];for(int i=0;i<text.Length;i++){a[2*i]=Key(0,text[i],4);a[2*i+1]=Key(0,text[i],6);}Input(a);}
 public static void Press(IntPtr h,ushort key,bool shift=false){Guard(h);if(shift)Input(new[]{Key(16,0,0),Key(key,0,0),Key(key,0,2),Key(16,0,2)});else Input(new[]{Key(key,0,0),Key(key,0,2)});}
 public static void Green(IntPtr h){Guard(h);var g=Geometry(h);var p=new P{x=87,y=(int)(Send(h,0x804a,3)-g[9])/2};ClientToScreen(h,ref p);if(!SetCursorPos(p.x,p.y))throw new Exception("Cannot position owned caption cursor");System.Threading.Thread.Sleep(100);P actual;if(!GetPhysicalCursorPos(out actual)||actual.x!=p.x||actual.y!=p.y)throw new Exception("Physical caption cursor mismatch");var hit=WindowFromPoint(p);if(hit!=h&&!IsChild(h,hit))throw new Exception("Owned caption is occluded");Guard(h);Input(new[]{new I{u=new U{m=new M{flags=2}}},new I{u=new U{m=new M{flags=4}}}});}
}
'@
[void][NativeIslandBoundary]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new()
function Check($value,[string]$name){
 $checks.Add([pscustomobject]@{name=$name;passed=[bool]$value})
 $checks|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputDirectory 'checks.json') -Encoding utf8
 if(-not $value){throw ('Check failed: '+$name)}
}
function Until([scriptblock]$test){
 $deadline=[DateTime]::UtcNow.AddSeconds(6)
 do{if(&$test){return $true};Start-Sleep -Milliseconds 80}while([DateTime]::UtcNow -lt $deadline)
 return $false
}
function Owner { [NativeIslandBoundary]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1') }
function Metric([int]$index){[NativeIslandBoundary]::Send($script:h,0x804a,$index)}
function Command([int]$value){Check (([NativeIslandBoundary]::Send($script:h,0x8049,$value)) -eq 0) ('fixture command '+$value)}
function Palette { @(20..34|ForEach-Object {Metric $_}) -join ',' }
function NewFixture([string]$arguments='--dark'){
 $script:fixture=Start-Process -FilePath $fixturePath -ArgumentList $arguments -WindowStyle Hidden -PassThru
 Check (Until {[NativeIslandBoundary]::Find($script:fixture.Id) -ne [IntPtr]::Zero}) 'owned fixture starts'
 $script:h=[NativeIslandBoundary]::Find($script:fixture.Id)
 [void][NativeIslandBoundary]::SetWindowPos($script:h,[IntPtr](-1),160,160,1200,720,0x40)
 [void][NativeIslandBoundary]::SetForegroundWindow($script:h)
 $laidOut=Until {(Metric 5) -gt 0 -and (Metric 6) -gt 0}
 if(-not $laidOut){
  [ordered]@{geometry=[NativeIslandBoundary]::Geometry($script:h);width=(Metric 5);height=(Metric 6);theme=(Metric 7)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'layout-failure.json') -Encoding utf8
  Shot 'layout-failure'
 }
 Check $laidOut 'owned fixture is laid out'
 Check ((Owner) -eq [IntPtr]::Zero) 'fixture has no preexisting lease'
}
function StartResearch([string]$name,[bool]$accepted=$true){
 $script:run=$name
 $script:worker=[Diagnostics.Process]::new()
 $script:worker.StartInfo.FileName=$hostPath
 $script:worker.StartInfo.Arguments=$script:h.ToInt64().ToString()+' 30'
 $script:worker.StartInfo.UseShellExecute=$false;$script:worker.StartInfo.CreateNoWindow=$true
 $script:worker.StartInfo.RedirectStandardOutput=$true;$script:worker.StartInfo.RedirectStandardError=$true
 [void]$script:worker.Start()
 $script:hostOutput=[NativeIslandHostOutput]::new($script:worker.StandardOutput);$script:stdout=$script:hostOutput.Completion;$script:stderr=$script:worker.StandardError.ReadToEndAsync()
 if($accepted){
  Check (Until {($script:hostOutput.IsReady -and (Owner) -ne [IntPtr]::Zero) -or $script:worker.HasExited}) ($name+' completes attach attempt')
  Check ($script:hostOutput.IsReady -and (Owner) -ne [IntPtr]::Zero) ($name+' attached after verification host confirms Session Apply')
 }else{
  Check ($script:worker.WaitForExit(6000) -and $script:worker.ExitCode -eq 50) ($name+' rejects unsupported geometry')
  Check ((Owner) -eq [IntPtr]::Zero) ($name+' creates no lease')
  FinishResearch 50
 }
}
function FinishResearch([int]$exitCode=0){
 Check ($script:worker.WaitForExit(6000) -and $script:worker.ExitCode -eq $exitCode) ($script:run+' host exits with expected status')
 $script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:run+'.log')) -Encoding utf8
 $script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:run+'.err')) -Encoding utf8
 $script:worker.Dispose();$script:worker=$null
}
function Detach {
 $owner=Owner
 Check ($owner -ne [IntPtr]::Zero) 'owned lease exists before detach'
 [uint32]$ownerPid=0
 [void][NativeIslandBoundary]::GetWindowThreadProcessId($owner,[ref]$ownerPid)
 Check ($ownerPid -eq $script:worker.Id) 'lease belongs to this research host'
 Check (([NativeIslandBoundary]::Send($script:h,[NativeIslandBoundary]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),$owner.ToInt64())) -eq 0x4d414357) 'rollback acknowledges success'
 Check ((Owner) -eq [IntPtr]::Zero -and (Metric 10) -eq 1) 'native title restored without a lease'
 FinishResearch
}
function CloseFixture {
 Check (([NativeIslandBoundary]::Find($script:fixture.Id)) -eq $script:h) 'close target is the owned fixture'
 [void][NativeIslandBoundary]::Send($script:h,0x10)
 Check ($script:fixture.WaitForExit(5000) -and $script:fixture.ExitCode -eq 0) 'owned fixture exits successfully'
 $script:fixture.Dispose();$script:fixture=$null
}
function Shot([string]$name,[switch]$CountLights){
 [NativeIslandCaptureBackdrop]::Place($script:h)
 $g=[NativeIslandBoundary]::Geometry($script:h)
 [void][NativeIslandBoundary]::SetCursorPos(($g[0]+$g[2])/2,($g[1]+20))
 Start-Sleep -Milliseconds 700
 [WindowStylerCaptureGuard]::Check($script:h,$g[0],$g[1],($g[2]-$g[0]),($g[3]-$g[1]))
 $b=[Drawing.Bitmap]::new(($g[2]-$g[0]),($g[3]-$g[1]));$draw=[Drawing.Graphics]::FromImage($b)
 try{
  $draw.CopyFromScreen($g[0],$g[1],0,0,$b.Size);$b.Save((Join-Path $OutputDirectory ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
  if($CountLights){
   $counts=@(0,0,0);$colors=@(0xFF5F57,0xFFBD2E,0x28C840)
   for($y=0;$y -lt [Math]::Min(80,$b.Height);$y++){for($x=0;$x -lt $b.Width;$x++){
    $rgb=$b.GetPixel($x,$y).ToArgb() -band 0xFFFFFF
    for($i=0;$i -lt 3;$i++){if($rgb -eq $colors[$i]){$counts[$i]++}}
   }}
   return $counts
  }
 }
 finally{$draw.Dispose();$b.Dispose()}
}
$foreground=[NativeIslandBoundary]::GetForegroundWindow()
$cursor=[NativeIslandBoundary+P]::new();[void][NativeIslandBoundary]::GetCursorPos([ref]$cursor)
$script:fixture=$null;$script:worker=$null;$script:h=[IntPtr]::Zero
try {
 if($Scenario -in @('all','narrow')){
 NewFixture '--dark --boundary-sizes'
 $native=[NativeIslandBoundary]::Geometry($h)
 $border=($native[2]-$native[0])-$native[6]
 [void][NativeIslandBoundary]::SetWindowPos($h,[IntPtr]::Zero,0,0,(125+$border),720,0x16)
 $narrow=[NativeIslandBoundary]::Geometry($h);$palette=Palette
 [ordered]@{before=$native;narrow=$narrow;border=$border}|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $OutputDirectory 'narrow-geometry.json') -Encoding utf8
 Check ($narrow[6] -eq 125) 'fixture is below the 126px caption boundary'
 StartResearch 'initial-too-narrow' $false
 Check (($narrow -join ',') -eq ([NativeIslandBoundary]::Geometry($h) -join ',') -and $palette -eq (Palette) -and (Metric 35) -eq 0) 'rejected attach leaves native geometry and title settings unchanged'
 [void][NativeIslandBoundary]::SetWindowPos($h,[IntPtr]::Zero,0,0,(126+$border),720,0x16)
 Start-Sleep -Milliseconds 700
 [ordered]@{geometry=[NativeIslandBoundary]::Geometry($h);bodyWidth=(Metric 5);bodyHeight=(Metric 6);bodyX=(Metric 4);bodyY=(Metric 3);bridgeWidth=(Metric 13);bridgeHeight=(Metric 14)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'minimum-geometry.json') -Encoding utf8
 StartResearch 'minimum-width'
 Check (([NativeIslandBoundary]::Geometry($h))[6] -eq 126 -and (Metric 10) -eq 0) 'minimum supported width stays attached'
 $counts=Shot 'minimum-width' -CountLights
 Check (@($counts|Where-Object {$_ -ne 212}).Count -eq 0) 'all three lights render without clipping at minimum width'
 [void][NativeIslandBoundary]::SetWindowPos($h,[IntPtr]::Zero,0,0,(125+$border),720,0x16)
 Check (Until {(Owner) -eq [IntPtr]::Zero}) 'shrinking below caption width automatically rolls back'
 Check ((Metric 10) -eq 1 -and (Metric 35) -eq 0) 'too-narrow rollback restores the native title'
 Check (($narrow -join ',') -eq ([NativeIslandBoundary]::Geometry($h) -join ',')) 'too-narrow rollback keeps host requested size'
 FinishResearch
 CloseFixture
 }

 if($Scenario -in @('all','keyboard')){
 NewFixture '--dark --interactive'
 Command 6
 Check ((Metric 40) -eq 1 -and (Metric 43) -eq 1) 'native input control holds XAML and island focus'
 [NativeIslandBoundary]::Type($h,'a')
 Check (Until {(Metric 41) -eq 1}) 'native input control receives actual keyboard input'
 [NativeIslandBoundary]::Press($h,9)
 Check (Until {(Metric 40) -eq 2}) 'native Tab moves from input control to action'
 [NativeIslandBoundary]::Press($h,13)
 Check (Until {(Metric 42) -eq 1}) 'native Enter invokes the action'
 [NativeIslandBoundary]::Press($h,9,$true)
 Check (Until {(Metric 40) -eq 1}) 'native Shift Tab returns to input control'
 StartResearch 'keyboard-focus'
 Check ((Metric 40) -eq 1 -and (Metric 43) -eq 1) 'attach preserves original input control and island focus'
 [NativeIslandBoundary]::Type($h,'b')
 Check (Until {(Metric 41) -eq 2}) 'attached input control receives actual keyboard input'
 [NativeIslandBoundary]::Press($h,9)
 Check (Until {(Metric 40) -eq 2}) 'attached Tab moves within original content'
 [NativeIslandBoundary]::Press($h,13)
 Check (Until {(Metric 42) -eq 2}) 'attached Enter invokes original action'
 [NativeIslandBoundary]::Press($h,9,$true)
 Check (Until {(Metric 40) -eq 1}) 'attached Shift Tab returns to original input control'
 [NativeIslandBoundary]::Green($h)
 Check (Until {[NativeIslandBoundary]::IsZoomed($h)}) 'green click maximizes interactive fixture'
 Start-Sleep -Milliseconds 600
 [NativeIslandBoundary]::Green($h)
 Check (Until {-not [NativeIslandBoundary]::IsZoomed($h)}) 'green click restores interactive fixture'
 Start-Sleep -Milliseconds 600
 Check ((Metric 40) -eq 1 -and (Metric 43) -eq 1) 'caption clicks retain original keyboard focus'
 [NativeIslandBoundary]::Type($h,'c')
 Check (Until {(Metric 41) -eq 3}) 'original input control receives input after caption clicks'
 Detach
 Check ((Metric 40) -eq 1 -and (Metric 43) -eq 1) 'rollback preserves original input control and island focus'
 [NativeIslandBoundary]::Type($h,'d')
 Check (Until {(Metric 41) -eq 4}) 'restored input control receives actual keyboard input'
 Shot 'keyboard-restored'
 CloseFixture
 }

 if($Scenario -in @('all','palette')){
 NewFixture
 Command 7
 $original=Palette
 Shot 'palette-before'
 StartResearch 'palette-retain'
 Detach
 Check ($original -eq (Palette)) 'all original palette values survive rollback'
 Shot 'palette-retained'
 Check ((Get-FileHash (Join-Path $OutputDirectory 'palette-before.png')).Hash -eq (Get-FileHash (Join-Path $OutputDirectory 'palette-retained.png')).Hash) 'original custom palette caption and content pixels are restored'
 StartResearch 'palette-clear'
 Command 8
 $latest=Palette
 Detach
 Check ($latest -eq (Palette)) 'host clearing all twelve colors survives rollback'
 StartResearch 'palette-set'
 Command 7
 $latest=Palette
 Detach
 Check ($latest -eq (Palette)) 'host replacing all twelve colors survives rollback'
 StartResearch 'title-options'
 Command 9
 $latest=Palette
 Detach
 Check ($latest -eq (Palette)) 'host icon height and theme changes survive rollback'
 Shot 'title-options-restored'
 CloseFixture
 NewFixture
 Command 7
 Command 11
 Check ($latest -eq (Palette)) 'independent native control has the same host title options'
 Shot 'title-options-native-control'
 Check ((Get-FileHash (Join-Path $OutputDirectory 'title-options-restored.png')).Hash -eq (Get-FileHash (Join-Path $OutputDirectory 'title-options-native-control.png')).Hash) 'changed title options match independent native rendering and content pixels'
 StartResearch 'initial-title-options'
 Detach
 Check ($latest -eq (Palette)) 'nondefault options present before attach also survive rollback'
 Shot 'initial-title-options-restored'
 Check ((Get-FileHash (Join-Path $OutputDirectory 'initial-title-options-restored.png')).Hash -eq (Get-FileHash (Join-Path $OutputDirectory 'title-options-native-control.png')).Hash) 'nondefault original title options restore native caption and content pixels'
 CloseFixture
 }
 [ordered]@{passed=$true;scenario=$Scenario;checks=$checks.Count;fixtureHash=(Get-FileHash $fixturePath).Hash;hostHash=(Get-FileHash $hostPath).Hash;scope='owned 144 DPI native WinUI fixture only'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
} catch {
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 throw
} finally {
 if($fixture -and -not $fixture.HasExited){
  $owned=[NativeIslandBoundary]::Find($fixture.Id)
  if($owned -ne [IntPtr]::Zero -and $fixture.Path -eq $fixturePath){[void][NativeIslandBoundary]::Send($owned,0x10);[void]$fixture.WaitForExit(5000)}
 }
 if($worker){if(-not $worker.HasExited){[void]$worker.WaitForExit(32000)};if($worker.HasExited){$stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.log')) -Encoding utf8;$stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.err')) -Encoding utf8};$worker.Dispose()}
 if($fixture){$fixture.Dispose()}
 [void][NativeIslandBoundary]::SetCursorPos($cursor.x,$cursor.y)
 [NativeIslandCaptureBackdrop]::Close()
 [void][NativeIslandBoundary]::SetForegroundWindow($foreground)
}
