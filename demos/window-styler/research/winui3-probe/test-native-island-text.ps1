param(
 [string]$FixtureDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v39/native-text-Release'),
 [ValidateSet('all','dark-normal','dark-crash','light-normal','light-crash')][string]$Scenario='all'
)
$ErrorActionPreference='Stop'
$fixturePath=Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
$hostPath=Join-Path (Resolve-Path -LiteralPath $HostDirectory).Path 'WinUI3NativeIslandHost.exe'
if((Test-Path (Join-Path $OutputDirectory 'checks.json')) -or (Test-Path (Join-Path $OutputDirectory 'result.json'))){throw 'Use a new output directory to preserve earlier evidence'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandText {
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
 public static void Guard(IntPtr h){if(GetForegroundWindow()!=h)throw new Exception("Owned fixture is not foreground");uint pid;GetWindowThreadProcessId(h,out pid);if(Find(pid)!=h)throw new Exception("Input target is not owned");foreach(var k in new[]{16,17,18,91,92})if(GetAsyncKeyState(k)<0)throw new Exception("User modifier key is held");}
 // WinUI text selection needs the physical modifier scan code. Navigation keys
 // also carry the extended-key flag, matching a real keyboard's event sequence.
 static I Key(ushort vk,ushort scan,uint flags){if(vk!=0){scan=(ushort)MapVirtualKey(vk,0);if(vk>=33&&vk<=46)flags|=1;}return new I{type=1,u=new U{k=new K{vk=vk,scan=scan,flags=flags}}};}
 static void Input(I[] values){if(SendInput((uint)values.Length,values,Marshal.SizeOf(typeof(I)))!=values.Length)throw new Exception("Input rejected");}
 public static void Type(IntPtr h,string text){Guard(h);var a=new I[text.Length*2];for(int i=0;i<text.Length;i++){a[2*i]=Key(0,text[i],4);a[2*i+1]=Key(0,text[i],6);}Input(a);}
 public static void Press(IntPtr h,ushort key,bool ctrl=false,bool shift=false){Guard(h);try{if(ctrl)Input(new[]{Key(17,0,0)});if(shift)Input(new[]{Key(16,0,0)});System.Threading.Thread.Sleep(30);Input(new[]{Key(key,0,0),Key(key,0,2)});System.Threading.Thread.Sleep(80);}finally{if(shift)Input(new[]{Key(16,0,2)});if(ctrl)Input(new[]{Key(17,0,2)});}}
 public static void Green(IntPtr h){Guard(h);var g=Geometry(h);var p=new P{x=87,y=(int)(Send(h,0x804a,3)-g[9])/2};ClientToScreen(h,ref p);SetCursorPos(p.x,p.y);System.Threading.Thread.Sleep(100);var hit=WindowFromPoint(p);if(hit!=h&&!IsChild(h,hit))throw new Exception("Owned caption is occluded");Input(new[]{new I{u=new U{m=new M{flags=2}}},new I{u=new U{m=new M{flags=4}}}});}
}
'@
[void][NativeIslandText]::SetProcessDpiAwarenessContext([IntPtr](-4))
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
function Metric([int]$i){[NativeIslandText]::Send($script:h,0x804a,$i)}
function Owner { [NativeIslandText]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1') }
function Command([int]$v){Check (([NativeIslandText]::Send($script:h,0x8049,$v)) -eq 0) ('fixture command '+$v)}
function TextValue {
 $len=Metric 51
 if($len -lt 0 -or $len -gt 1024){throw 'Unexpected owned TextBox length'}
 $s=[Text.StringBuilder]::new()
 for($i=0;$i -lt $len;$i++){[void]$s.Append([char](Metric (0x1000+$i)))}
 $s.ToString()
}
function ExpectText([string]$expected,[string]$name){Check (Until {(TextValue) -ceq $expected}) $name}
function Press([int]$key,[bool]$ctrl=$false,[bool]$shift=$false){[NativeIslandText]::Press($script:h,$key,$ctrl,$shift);Start-Sleep -Milliseconds 80}
function TextInput([string]$s){[NativeIslandText]::Type($script:h,$s);Start-Sleep -Milliseconds 100}
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
 [NativeIslandText]::Guard($script:h)
 Check ((Metric 40) -eq 2 -and (Metric 50) -eq 0) 'screenshot has action focus without blinking caret'
 $g=[NativeIslandText]::Geometry($script:h)
 [void][NativeIslandText]::SetCursorPos(($g[0]+$g[2])/2,($g[1]+20))
 Start-Sleep -Milliseconds 700
 $b=[Drawing.Bitmap]::new($g[2]-$g[0],$g[3]-$g[1]);$d=[Drawing.Graphics]::FromImage($b)
 try{$d.CopyFromScreen($g[0],$g[1],0,0,$b.Size);$b.Save((Join-Path $OutputDirectory ($run+'-'+$name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$d.Dispose();$b.Dispose()}
}
function TitlePixel {
 $g=[NativeIslandText]::Geometry($script:h)
 $b=[Drawing.Bitmap]::new(1,1);$d=[Drawing.Graphics]::FromImage($b)
 try{$d.CopyFromScreen(($g[8]+300),($g[9]+20),0,0,$b.Size);return ($b.GetPixel(0,0).ToArgb() -band 0xFFFFFF)}finally{$d.Dispose();$b.Dispose()}
}
function StartResearch {
 $script:worker=[Diagnostics.Process]::new()
 $script:worker.StartInfo.FileName=$hostPath;$script:worker.StartInfo.Arguments=$script:h.ToInt64().ToString()+' 30'
 $script:worker.StartInfo.UseShellExecute=$false;$script:worker.StartInfo.CreateNoWindow=$true
 $script:worker.StartInfo.RedirectStandardOutput=$true;$script:worker.StartInfo.RedirectStandardError=$true
 [void]$script:worker.Start()
 $script:stdout=$script:worker.StandardOutput.ReadToEndAsync();$script:stderr=$script:worker.StandardError.ReadToEndAsync()
 Check (Until {(Owner) -ne [IntPtr]::Zero -or $script:worker.HasExited}) 'research attach completes'
 Check ((Owner) -ne [IntPtr]::Zero -and (Metric 10) -eq 0 -and (Metric 9) -eq 1) 'external research owns lease and original root stays installed'
}
function FinishResearch([int]$expected){
 Check ($script:worker.WaitForExit(6000) -and $script:worker.ExitCode -eq $expected) 'research host has expected exit status'
 $script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.log')) -Encoding utf8
 $script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.err')) -Encoding utf8
 $script:worker.Dispose();$script:worker=$null
}
function RestoreResearch([bool]$crash){
 $owner=Owner;[uint32]$ownerPid=0
 [void][NativeIslandText]::GetWindowThreadProcessId($owner,[ref]$ownerPid)
 Check ($owner -ne [IntPtr]::Zero -and $ownerPid -eq $script:worker.Id -and $script:worker.Path -eq $hostPath) 'rollback target is this owned research controller'
 if($crash){$script:worker.Kill();Check (Until {(Owner) -eq [IntPtr]::Zero}) 'controller process loss automatically restores native title';FinishResearch -1}
 else{Check (([NativeIslandText]::Send($script:h,[NativeIslandText]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),$owner.ToInt64())) -eq 0x4d414357) 'normal detach acknowledges completed restoration';FinishResearch 0}
}
$foreground=[NativeIslandText]::GetForegroundWindow()
$cursor=[NativeIslandText+P]::new();[void][NativeIslandText]::GetCursorPos([ref]$cursor)
$script:fixture=$null;$script:worker=$null;$script:h=[IntPtr]::Zero
try {
 foreach($matrix in @('dark-normal','dark-crash','light-normal','light-crash')){
  if($Scenario -ne 'all' -and $Scenario -ne $matrix){continue}
  # Replay the same edits, native maximize/restore and theme cycle in a separate,
  # never-injected window. Standard TextBox rounded edges can be rerasterized by
  # that cycle even without injection; compare the complete final images exactly.
  foreach($nativeControl in @($true,$false)){
  $script:run=$matrix+$(if($nativeControl){'-control'}else{''});$light=$matrix.StartsWith('light');$crash=$matrix.EndsWith('crash')
  $script:fixture=Start-Process -FilePath $fixturePath -ArgumentList ('--text-editor --'+$(if($light){'light'}else{'dark'})) -WindowStyle Hidden -PassThru
  Check (Until {[NativeIslandText]::Find($script:fixture.Id) -ne [IntPtr]::Zero}) 'owned text fixture starts'
  $script:h=[NativeIslandText]::Find($script:fixture.Id)
  Check ($script:fixture.Path -eq $fixturePath) 'owned fixture executable verified'
  [void][NativeIslandText]::SetWindowPos($script:h,[IntPtr](-1),160,160,1200,720,0x40)
  [void][NativeIslandText]::SetForegroundWindow($script:h)
  Check (Until {(Metric 57) -eq 720 -and (Metric 58) -eq 240 -and (Metric 54) -eq 1}) 'standard TextBox is loaded at expected 144 DPI size'
  Check ((Owner) -eq [IntPtr]::Zero -and (Metric 55) -eq 0) 'native fixture has no lease or TextBox unload'
  Command 21
  Check (Until {(Metric 50) -eq 1 -and (Metric 43) -eq 1}) 'native TextBox has actual XAML and island focus'
  Press 65 $true;TextInput 'Alpha beta'
  ExpectText 'Alpha beta' 'native Ctrl A and typing replace document'
  SelectFirst 5
  Check ((Metric 52) -eq 0 -and (Metric 53) -eq 5) 'native keyboard selects first five characters'
  TextInput 'Gamma'
  ExpectText 'Gamma beta' 'native selection replacement edits real TextBox'
  Press 90 $true
  ExpectText 'Alpha beta' 'native Ctrl Z restores selection replacement'
  Check ((Metric 59) -eq 1 -and (Metric 60) -eq 1) 'native undo and redo stacks are both populated'
  SelectFirst 5
  ActionFocus
  Shot 'native-before'
  EditorFocus
  $before=SaveState 'before-attach';$geometry=[NativeIslandText]::Geometry($script:h)
  if($nativeControl){Check ((Owner) -eq [IntPtr]::Zero) 'native reference remains uninjected'}
  else{
   StartResearch
   SameState $before 'unchanged-attach-preserves-editor-state'
   RestoreResearch $false
   SameState $before 'unchanged-rollback-preserves-editor-state'
   ActionFocus;Shot 'unchanged-native-restored'
   Check ((Get-FileHash (Join-Path $OutputDirectory ($run+'-native-before.png'))).Hash -eq (Get-FileHash (Join-Path $OutputDirectory ($run+'-unchanged-native-restored.png'))).Hash) 'unchanged transaction restores complete original window pixels'
   EditorFocus
   StartResearch
   SameState $before 'attach-preserves-editor-state'
  }
  $attachedGeometry=[NativeIslandText]::Geometry($script:h)
  Press 89 $true
  ExpectText 'Gamma beta' 'Ctrl Y after attach replays native edit history'
  Press 90 $true
  ExpectText 'Alpha beta' 'Ctrl Z after attach reverses native edit history'
  SelectFirst 5;TextInput 'Window'
  ExpectText 'Window beta' 'attached keyboard selection replacement works'
  Press 35 $true;Press 13;TextInput ([string][char]0x7A97+[char]0x53E3+' '+[char]0x03A9)
  # Standard WinUI TextBox uses CR as the stored paragraph separator.
  $multiline="Window beta`r"+[char]0x7A97+[char]0x53E3+' '+[char]0x03A9
  ExpectText $multiline 'attached Enter and Unicode SendInput create multiline text'
  Press 8
  $backspace="Window beta`r"+[char]0x7A97+[char]0x53E3+' '
  ExpectText $backspace 'attached Backspace removes final Unicode character'
  Press 36 $true;Press 46
  $edited="indow beta`r"+[char]0x7A97+[char]0x53E3+' '
  ExpectText $edited 'attached Delete removes character at document start'
  SelectFirst 5
  $selected=SaveState 'before-caption-clicks'
  if($nativeControl){[void][NativeIslandText]::Send($script:h,0x112,0xF030)}else{[NativeIslandText]::Green($script:h)}
  Check (Until {[NativeIslandText]::IsZoomed($script:h)}) $(if($nativeControl){'native system command maximizes reference'}else{'real green click maximizes text fixture'})
  Start-Sleep -Milliseconds 400
  if($nativeControl){[void][NativeIslandText]::Send($script:h,0x112,0xF120)}else{[NativeIslandText]::Green($script:h)}
  Check (Until {-not [NativeIslandText]::IsZoomed($script:h)}) $(if($nativeControl){'native system command restores reference'}else{'real green click restores text fixture'})
  Start-Sleep -Milliseconds 400
  SameState $selected 'caption-clicks-preserve-editor-state'
  Check (($attachedGeometry -join ',') -eq ([NativeIslandText]::Geometry($script:h) -join ',')) 'maximize restore retains pre-click outer and client geometry'
  $selected=State
  Command $(if($light){4}else{3})
  Check (Until {(Metric 61) -eq $(if($light){2}else{1})}) 'TextBox follows host subtree opposite theme'
  SameState $selected 'theme-switch-preserves-editor-state'
  ActionFocus;Shot 'attached-opposite-theme'
  if(-not $nativeControl){Check ((TitlePixel) -eq $(if($light){0x202020}else{0xF3F3F3})) 'independent title follows opposite theme pixels'}
  Press 13
  Check (Until {(Metric 42) -eq 1}) 'Enter invokes original action while attached'
  EditorFocus
  Command $(if($light){3}else{4})
  Check (Until {(Metric 61) -eq $(if($light){1}else{2})}) 'TextBox follows host subtree original theme'
  SameState $selected 'round-trip-theme-and-action-preserve-editor-state'
  # A distinct full-selection replacement creates a known final undo boundary.
  Press 65 $true;TextInput 'Alpha beta'
  ExpectText 'Alpha beta' 'final selection replacement prepares rollback baseline'
  SelectFirst 5;ActionFocus
  $rollback=SaveState 'before-rollback'
  Shot 'attached-original-theme'
  if(-not $nativeControl){
   Check ((TitlePixel) -eq $(if($light){0xF3F3F3}else{0x202020})) 'independent title returns to original theme pixels'
   RestoreResearch $crash
  }
  Check ((Owner) -eq [IntPtr]::Zero -and (Metric 10) -eq 1 -and (Metric 9) -eq 1) 'rollback restores native title and preserves original root'
  SameState $rollback 'rollback-preserves-editor-state'
  Check (($geometry -join ',') -eq ([NativeIslandText]::Geometry($script:h) -join ',')) 'rollback restores native outer and client geometry'
  Shot 'native-restored'
  if($nativeControl){$reference=SaveState 'native-reference'}else{
   SameState $reference 'final-editor-state-matches-independent-native-control'
   Check ((Get-FileHash (Join-Path $OutputDirectory ($matrix+'-control-native-restored.png'))).Hash -eq (Get-FileHash (Join-Path $OutputDirectory ($run+'-native-restored.png'))).Hash) 'complete final window pixels match independent native control without masks or crops'
  }
  EditorFocus
  Press 90 $true
  ExpectText $edited 'Ctrl Z after rollback preserves attached multiline edit history'
  Press 89 $true
  ExpectText 'Alpha beta' 'Ctrl Y after rollback preserves attached redo history'
  Press 35 $true;TextInput '!'
  ExpectText 'Alpha beta!' 'restored TextBox accepts further real input'
  Check ((Metric 54) -eq 1 -and (Metric 55) -eq 0) 'TextBox was never unloaded or reloaded throughout transaction'
  Check ([NativeIslandText]::Find($script:fixture.Id) -eq $script:h -and $script:fixture.Path -eq $fixturePath) 'close target remains owned fixture'
  [void][NativeIslandText]::Send($script:h,0x10)
  Check ($script:fixture.WaitForExit(5000) -and $script:fixture.ExitCode -eq 0) 'owned fixture closes without framework exception'
  Check (-not(Test-Path (Join-Path $FixtureDirectory ('native-island-error-'+$script:fixture.Id+'.txt')))) 'owned fixture produced no HRESULT or XAML error log'
  $script:fixture.Dispose();$script:fixture=$null
  }
 }
 [ordered]@{passed=$true;scenario=$Scenario;checks=$checks.Count;fixtureHash=(Get-FileHash $fixturePath).Hash;hostHash=(Get-FileHash $hostPath).Hash;scope='owned standard TextBox; 144 DPI; Unicode SendInput is not IME verification'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
} catch {
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 if($script:fixture -and -not $script:fixture.HasExited){try{[void](SaveState 'failure-state')}catch{}}
 throw
} finally {
 if($script:fixture -and -not $script:fixture.HasExited){
  $owned=[NativeIslandText]::Find($script:fixture.Id)
  if($owned -ne [IntPtr]::Zero -and $script:fixture.Path -eq $fixturePath){[void][NativeIslandText]::Send($owned,0x10);[void]$script:fixture.WaitForExit(5000)}
 }
 if($script:worker){if(-not $script:worker.HasExited){[void]$script:worker.WaitForExit(32000)};if($script:worker.HasExited){$script:stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.log')) -Encoding utf8;$script:stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $OutputDirectory ($run+'.err')) -Encoding utf8};$script:worker.Dispose()}
 if($script:fixture){$script:fixture.Dispose()}
 [void][NativeIslandText]::SetCursorPos($cursor.x,$cursor.y)
 [void][NativeIslandText]::SetForegroundWindow($foreground)
}
