param(
 [string]$FixtureDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v40/native-rebuild-Release'),
 [ValidateSet('all','root','source','gap')][string]$Scenario='all'
)
$ErrorActionPreference='Stop'
$fixturePath=Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
$hostPath=Join-Path (Resolve-Path -LiteralPath $HostDirectory).Path 'WinUI3NativeIslandHost.exe'
if((Test-Path (Join-Path $OutputDirectory 'checks.json')) -or (Test-Path (Join-Path $OutputDirectory 'result.json'))){throw 'Use a new output directory to preserve earlier evidence'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'capture-guard.ps1')
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandRebuild {
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
 [DllImport("user32.dll")] public static extern bool GetPhysicalCursorPos(out P p);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(P p);
 [DllImport("user32.dll")] public static extern bool IsChild(IntPtr p,IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
 [DllImport("user32.dll")] static extern uint SendInput(uint n,I[] input,int size);
 [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code,uint map);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,2000,out r)==IntPtr.Zero)throw new Exception("Fixture timeout");return unchecked((long)r.ToUInt64());}
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,p.x,p.y};}
 public static void OwnedForeground(IntPtr h){var actual=GetForegroundWindow();if(actual!=h){uint pid;GetWindowThreadProcessId(actual,out pid);throw new Exception("Owned fixture is not foreground: HWND 0x"+actual.ToInt64().ToString("x")+" PID "+pid);}uint ownedPid;GetWindowThreadProcessId(h,out ownedPid);if(Find(ownedPid)!=h)throw new Exception("Input target is not owned");}
 public static void Guard(IntPtr h){if(SetThreadDpiAwarenessContext((IntPtr)(-4))==IntPtr.Zero)throw new Exception("Cannot establish physical input coordinates");OwnedForeground(h);foreach(var k in new[]{16,17,18,91,92})if(GetAsyncKeyState(k)<0)throw new Exception("User modifier key is held");}
 // WinUI text selection needs the physical modifier scan code. Navigation keys
 // also carry the extended-key flag, matching a real keyboard's event sequence.
 static I Key(ushort vk,ushort scan,uint flags){if(vk!=0){scan=(ushort)MapVirtualKey(vk,0);if(vk>=33&&vk<=46)flags|=1;}return new I{type=1,u=new U{k=new K{vk=vk,scan=scan,flags=flags}}};}
 static void Input(I[] values){if(SendInput((uint)values.Length,values,Marshal.SizeOf(typeof(I)))!=values.Length)throw new Exception("Input rejected");}
 public static void Type(IntPtr h,string text){Guard(h);var a=new I[text.Length*2];for(int i=0;i<text.Length;i++){a[2*i]=Key(0,text[i],4);a[2*i+1]=Key(0,text[i],6);}Input(a);}
 public static void Press(IntPtr h,ushort key,bool ctrl=false,bool shift=false){Guard(h);try{if(ctrl)Input(new[]{Key(17,0,0)});if(shift)Input(new[]{Key(16,0,0)});System.Threading.Thread.Sleep(30);OwnedForeground(h);Input(new[]{Key(key,0,0),Key(key,0,2)});System.Threading.Thread.Sleep(80);}finally{if(shift)Input(new[]{Key(16,0,2)});if(ctrl)Input(new[]{Key(17,0,2)});}}
 public static void Green(IntPtr h){Guard(h);var g=Geometry(h);var p=new P{x=87,y=(int)(Send(h,0x804a,3)-g[9])/2};ClientToScreen(h,ref p);if(!SetCursorPos(p.x,p.y))throw new Exception("Cannot position owned caption cursor");System.Threading.Thread.Sleep(100);P actual;if(!GetPhysicalCursorPos(out actual)||actual.x!=p.x||actual.y!=p.y)throw new Exception("Physical caption cursor mismatch: expected "+p.x+","+p.y+" actual "+actual.x+","+actual.y);var hit=WindowFromPoint(p);if(hit!=h&&!IsChild(h,hit))throw new Exception("Owned caption is occluded");Guard(h);Input(new[]{new I{u=new U{m=new M{flags=2}}},new I{u=new U{m=new M{flags=4}}}});}
}
'@
[void][NativeIslandRebuild]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new()
function Check($value,[string]$name){
 $checks.Add([pscustomobject]@{scenario=$script:run;name=$name;passed=[bool]$value})
 $checks|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputDirectory 'checks.json') -Encoding utf8
 if(-not $value){throw ('Check failed: '+$name)}
}
function Until([scriptblock]$test){
 $deadline=[DateTime]::UtcNow.AddSeconds(6)
 do{if(&$test){return $true};Start-Sleep -Milliseconds 60}while([DateTime]::UtcNow -lt $deadline)
 return $false
}
function Metric([int]$i){[NativeIslandRebuild]::Send($script:h,0x804a,$i)}
function Viewport { @(3,4,5,6,13,14)|ForEach-Object {Metric $_} }
function Owner { [NativeIslandRebuild]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1') }
function Command([int]$v){Check (([NativeIslandRebuild]::Send($script:h,0x8049,$v)) -eq 0) ('fixture command '+$v)}
function TextValue {
 $len=Metric 51
 if($len -lt 0 -or $len -gt 1024){throw 'Unexpected owned TextBox length'}
 $s=[Text.StringBuilder]::new()
 for($i=0;$i -lt $len;$i++){[void]$s.Append([char](Metric (0x1000+$i)))}
 $s.ToString()
}
function ExpectText([string]$expected,[string]$name){Check (Until {(TextValue) -ceq $expected}) $name}
function Press([int]$key,[bool]$ctrl=$false,[bool]$shift=$false){[NativeIslandRebuild]::Press($script:h,$key,$ctrl,$shift);Start-Sleep -Milliseconds 80}
function TextInput([string]$s){[NativeIslandRebuild]::Type($script:h,$s);Start-Sleep -Milliseconds 100}
function SelectFirst([int]$n){Press 36 $true;for($i=0;$i -lt $n;$i++){Press 39 $false $true}}
function State {
 [ordered]@{text=(TextValue);start=(Metric 52);length=(Metric 53);undo=(Metric 59);redo=(Metric 60);focus=(Metric 50);islandFocus=(Metric 43);loads=(Metric 54);unloads=(Metric 55);changes=(Metric 56);width=(Metric 57);height=(Metric 58)}
}
function SaveState([string]$name){$s=State;$s|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'-'+$name+'.json')) -Encoding utf8;return $s}
function SameState($before,[string]$name){$after=SaveState $name;Check (($before|ConvertTo-Json -Compress) -ceq ($after|ConvertTo-Json -Compress)) $name}
function ActionFocus {
 Press 9
 Check (Until {(Metric 40) -eq 2 -and (Metric 50) -eq 0}) 'Tab reaches original action'
}
function EditorFocus {
 Press 9 $false $true
 Check (Until {(Metric 50) -eq 1 -and (Metric 43) -eq 1}) 'Shift Tab returns to original TextBox'
}
function Shot([string]$name){
 [NativeIslandRebuild]::Guard($script:h)
 Check ((Metric 40) -eq 2 -and (Metric 50) -eq 0) 'screenshot has action focus without blinking caret'
 $g=[NativeIslandRebuild]::Geometry($script:h)
 [void][NativeIslandRebuild]::SetCursorPos(($g[0]+$g[2])/2,($g[1]+20))
 Start-Sleep -Milliseconds 700
 [NativeIslandRebuild]::Guard($script:h)
 [WindowStylerCaptureGuard]::Check($script:h,$g[0],$g[1],($g[2]-$g[0]),($g[3]-$g[1]))
 $b=[Drawing.Bitmap]::new($g[2]-$g[0],$g[3]-$g[1]);$d=[Drawing.Graphics]::FromImage($b)
 try{$d.CopyFromScreen($g[0],$g[1],0,0,$b.Size);$b.Save((Join-Path $OutputDirectory ($run+'-'+$name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$d.Dispose();$b.Dispose()}
}
function StartResearch([string]$name){
 $script:lease=$script:run+'-'+$name
 $script:worker=[Diagnostics.Process]::new()
 $script:worker.StartInfo.FileName=$hostPath;$script:worker.StartInfo.Arguments=$script:h.ToInt64().ToString()+' 30'
 $script:worker.StartInfo.UseShellExecute=$false;$script:worker.StartInfo.CreateNoWindow=$true
 $script:worker.StartInfo.RedirectStandardOutput=$true;$script:worker.StartInfo.RedirectStandardError=$true
 [void]$script:worker.Start()
 $script:stdout=$script:worker.StandardOutput.ReadToEndAsync();$script:stderr=$script:worker.StandardError.ReadToEndAsync()
 Check (Until {$script:worker.HasExited -or ((Owner) -ne [IntPtr]::Zero -and ([NativeIslandRebuild]::Send($script:h,[NativeIslandRebuild]::RegisterWindowMessage('Macdows.WindowStyler.Query.v1'))) -eq 0x4d414357)}) 'research attach completes'
 Check (-not $script:worker.HasExited -and ([NativeIslandRebuild]::Send($script:h,[NativeIslandRebuild]::RegisterWindowMessage('Macdows.WindowStyler.Query.v1'))) -eq 0x4d414357) 'loaded caption confirms input readiness before test interaction'
 Check ((Owner) -ne [IntPtr]::Zero -and (Metric 10) -eq 0 -and (Metric 9) -eq 1) 'external research owns lease and original root stays installed'
}
function FinishResearch([int]$expected){
 Check ($script:worker.WaitForExit(6000) -and $script:worker.ExitCode -eq $expected) 'research host has expected exit status'
 $script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:lease+'.log')) -Encoding utf8
 $script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:lease+'.err')) -Encoding utf8
 $script:worker.Dispose();$script:worker=$null
}
function RestoreResearch([bool]$crash){
 $owner=Owner;[uint32]$ownerPid=0
 [void][NativeIslandRebuild]::GetWindowThreadProcessId($owner,[ref]$ownerPid)
 Check ($owner -ne [IntPtr]::Zero -and $ownerPid -eq $script:worker.Id -and $script:worker.Path -eq $hostPath) 'rollback target is this owned research controller'
 if($crash){$script:worker.Kill();Check (Until {(Owner) -eq [IntPtr]::Zero}) 'controller process loss automatically restores native title';FinishResearch -1}
 else{Check (([NativeIslandRebuild]::Send($script:h,[NativeIslandRebuild]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),$owner.ToInt64())) -eq 0x4d414357) 'normal detach acknowledges completed restoration';FinishResearch 0}
}
$foreground=[NativeIslandRebuild]::GetForegroundWindow()
$cursor=[NativeIslandRebuild+P]::new();[void][NativeIslandRebuild]::GetCursorPos([ref]$cursor)
$script:fixture=$null;$script:worker=$null;$script:h=[IntPtr]::Zero

try {
 foreach($theme in @('dark','light')){
  foreach($mode in @('root','source','gap')){
   if($Scenario -ne 'all' -and $Scenario -ne $mode){continue}
   $matrix=$theme+'-'+$mode
   foreach($nativeControl in @($true,$false)){
    $script:run=$matrix+$(if($nativeControl){'-control'}else{''})
    $script:fixture=Start-Process -FilePath $fixturePath -ArgumentList ('--text-editor --'+$theme) -WindowStyle Hidden -PassThru
    Check (Until {[NativeIslandRebuild]::Find($script:fixture.Id) -ne [IntPtr]::Zero}) 'owned rebuild fixture starts'
    $script:h=[NativeIslandRebuild]::Find($script:fixture.Id)
    Check ($script:fixture.Path -eq $fixturePath) 'fixture executable identity verified'
    [void][NativeIslandRebuild]::SetWindowPos($script:h,[IntPtr](-1),160,160,1200,720,0x40)
    [void][NativeIslandRebuild]::SetForegroundWindow($script:h)
    Check (Until {(Metric 57) -eq 720 -and (Metric 58) -eq 240 -and (Metric 54) -eq 1}) 'native standard editor is loaded at 144 DPI'
    Check ((Owner) -eq [IntPtr]::Zero -and (Metric 55) -eq 0) 'initial fixture is native and has no unload'
    $geometry=[NativeIslandRebuild]::Geometry($script:h)
    $nativeViewport=Viewport
    $sourceWindow=Metric 71
    Check ($sourceWindow -ne 0 -and (Metric 70) -eq 1 -and (Metric 11) -eq 0) 'initial island identity and generations recorded'
    Command 21
    Press 65 $true;TextInput ('Retired editor '+$mode)
    ExpectText ('Retired editor '+$mode) 'old editor receives actual keyboard edits'
    SelectFirst 3
    $oldState=SaveState 'before-rebuild-attach'
    if(-not $nativeControl){StartResearch 'old-island';SameState $oldState 'old-attach-preserves-editor-state'}
    Command (@{root=22;source=23;gap=24}[$mode])
    if($mode -eq 'gap'){Check ((Metric 74) -eq 1 -and (Metric 71) -eq 0) 'closed island leaves a real interval without a source'}
    if(-not $nativeControl){
     Check (Until {(Owner) -eq [IntPtr]::Zero -or $script:fixture.HasExited}) 'host rebuild releases old lease'
     Check (-not $script:fixture.HasExited) 'host rebuild does not terminate fixture'
     FinishResearch 0
    }
    Check (Until {(Metric 11) -eq 1 -and (Metric 74) -eq 0 -and (Metric 54) -eq 1 -and (Metric 57) -eq 720}) 'new original editor is loaded after host rebuild'
    Check ((Owner) -eq [IntPtr]::Zero -and (Metric 10) -eq 1 -and (Metric 9) -eq 1) 'native title and host replacement root are retained'
    foreach($property in @('Macdows.WindowStyler.Inline.v4','Macdows.WindowStyler.Adapter.v1')){
     Check ([NativeIslandRebuild]::GetProp($script:h,$property) -eq [IntPtr]::Zero) ('old property cleared: '+$property)
    }
    Check (($geometry -join ',') -eq ([NativeIslandRebuild]::Geometry($script:h) -join ',')) 'host rebuild restores outer and client geometry'
    Check (Until {($nativeViewport -join ',') -eq ((Viewport) -join ',')}) 'host rebuild restores current island viewport and original content position'
    Check ((Metric 72) -eq 1 -and (Metric 73) -eq 1 -and (Metric 55) -eq 0) 'retired editor unloads once and replacement editor stays loaded'
    Check ((Metric 70) -eq $(if($mode -eq 'root'){1}else{2})) 'source generation matches root-only or island replacement'
    Check ((Metric 71) -ne 0 -and ($mode -ne 'root' -or (Metric 71) -eq $sourceWindow)) 'current bridge exists and root replacement retains original bridge'
    ExpectText 'Original document' 'host replacement editor is not overwritten by retired text'
    Check ((Metric 59) -eq 0 -and (Metric 60) -eq 0) 'replacement editor has its own clean history'
    Command 21
    SelectFirst 3;ActionFocus
    $rebuilt=SaveState 'rebuilt-native'
    Shot 'rebuilt-native'
    if($nativeControl){$referenceRebuilt=$rebuilt}else{
     SameState $referenceRebuilt 'rebuilt-state-matches-uninjected-control'
     Check ((Get-FileHash (Join-Path $OutputDirectory ($matrix+'-control-rebuilt-native.png'))).Hash -eq (Get-FileHash (Join-Path $OutputDirectory ($run+'-rebuilt-native.png'))).Hash) 'complete rebuilt window pixels match native control'
    }
    EditorFocus
    Press 65 $true;TextInput ('Replacement editor '+$mode)
    ExpectText ('Replacement editor '+$mode) 'new editor accepts actual input'
    foreach($recovery in @('normal','crash')){
     SelectFirst 3
     $before=SaveState ($recovery+'-before')
     if(-not $nativeControl){StartResearch ('new-island-'+$recovery);SameState $before ($recovery+'-reattach-preserves-new-editor')}
     $attachedGeometry=[NativeIslandRebuild]::Geometry($script:h)
     if($nativeControl){[void][NativeIslandRebuild]::Send($script:h,0x112,0xF030)}else{[NativeIslandRebuild]::Green($script:h)}
     Check (Until {[NativeIslandRebuild]::IsZoomed($script:h)}) ($recovery+' maximize succeeds')
     Start-Sleep -Milliseconds 400
     if($nativeControl){[void][NativeIslandRebuild]::Send($script:h,0x112,0xF120)}else{[NativeIslandRebuild]::Green($script:h)}
     Check (Until {-not [NativeIslandRebuild]::IsZoomed($script:h)}) ($recovery+' restore succeeds')
     Start-Sleep -Milliseconds 400
     SameState $before ($recovery+'-caption-preserves-new-editor')
     Check (($attachedGeometry -join ',') -eq ([NativeIslandRebuild]::Geometry($script:h) -join ',')) ($recovery+' geometry survives maximize restore')
     Press 90 $true
     ExpectText 'Original document' ($recovery+' undo uses replacement editor history')
     Press 89 $true
     ExpectText ('Replacement editor '+$mode) ($recovery+' redo uses replacement editor history')
     SelectFirst 3;ActionFocus
     $rollback=SaveState ($recovery+'-before-rollback')
     if(-not $nativeControl){RestoreResearch ($recovery -eq 'crash')}
     SameState $rollback ($recovery+'-rollback-preserves-new-editor')
     Check ((Owner) -eq [IntPtr]::Zero -and (Metric 10) -eq 1 -and (Metric 9) -eq 1) ($recovery+' new island returns to native title')
     Check ((Metric 54) -eq 1 -and (Metric 55) -eq 0 -and (Metric 73) -eq 1) ($recovery+' new editor is never reloaded and old editor stays retired')
     Check (Until {($nativeViewport -join ',') -eq ((Viewport) -join ',')}) ($recovery+' current island viewport and content position are restored')
     Shot ($recovery+'-restored')
     EditorFocus
    }
    ActionFocus
    $final=SaveState 'final'
    Shot 'final'
    if($nativeControl){$referenceFinal=$final}else{
     SameState $referenceFinal 'final-state-matches-uninjected-control'
     Check ((Get-FileHash (Join-Path $OutputDirectory ($matrix+'-control-final.png'))).Hash -eq (Get-FileHash (Join-Path $OutputDirectory ($run+'-final.png'))).Hash) 'complete final window pixels match native control'
    }
    Check ([NativeIslandRebuild]::Find($script:fixture.Id) -eq $script:h -and $script:fixture.Path -eq $fixturePath) 'close target remains this owned fixture'
    [void][NativeIslandRebuild]::Send($script:h,0x10)
    Check ($script:fixture.WaitForExit(5000) -and $script:fixture.ExitCode -eq 0) 'rebuilt fixture closes without framework exception'
    Check (-not(Test-Path (Join-Path $FixtureDirectory ('native-island-error-'+$script:fixture.Id+'.txt')))) 'fixture has no HRESULT or XAML error log'
    $script:fixture.Dispose();$script:fixture=$null
   }
  }
 }
 [ordered]@{passed=$true;scenario=$Scenario;checks=$checks.Count;fixtureHash=(Get-FileHash $fixturePath).Hash;hostHash=(Get-FileHash $hostPath).Hash;scope='owned 144 DPI standard TextBox; root replacement, immediate and delayed source recreation; not actual application rebuild verification'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
} catch {
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 if($script:fixture -and -not $script:fixture.HasExited){try{[void](SaveState 'failure-state')}catch{}}
 if($script:fixture -and -not $script:fixture.HasExited){try{
  if([NativeIslandRebuild]::Find($script:fixture.Id) -eq $script:h){
   $g=[NativeIslandRebuild]::Geometry($script:h)
   $b=[Drawing.Bitmap]::new($g[2]-$g[0],$g[3]-$g[1]);$d=[Drawing.Graphics]::FromImage($b)
   try{$d.CopyFromScreen($g[0],$g[1],0,0,$b.Size);$b.Save((Join-Path $OutputDirectory ($run+'-failure-window.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$d.Dispose();$b.Dispose()}
  }
 }catch{}}
 throw
} finally {
 if($script:fixture -and -not $script:fixture.HasExited){
  $owned=[NativeIslandRebuild]::Find($script:fixture.Id)
  if($owned -ne [IntPtr]::Zero -and $script:fixture.Path -eq $fixturePath){[void][NativeIslandRebuild]::Send($owned,0x10);[void]$script:fixture.WaitForExit(5000)}
 }
 if($script:worker){if(-not $script:worker.HasExited){[void]$script:worker.WaitForExit(32000)};if($script:worker.HasExited){$script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:lease+'.log')) -Encoding utf8;$script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($script:lease+'.err')) -Encoding utf8};$script:worker.Dispose()}
 if($script:fixture){$script:fixture.Dispose()}
 [void][NativeIslandRebuild]::SetCursorPos($cursor.x,$cursor.y)
 [void][NativeIslandRebuild]::SetForegroundWindow($foreground)
}
