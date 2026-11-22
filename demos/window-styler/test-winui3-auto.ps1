param(
 [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
 [Parameter(Mandatory=$true)][string]$FixtureDirectory,
 [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$runtime=(Resolve-Path -LiteralPath $RuntimeDirectory).Path
$fixturePath=Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
$controllerPath=Join-Path $runtime 'WindowStyler.exe'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new evidence directory'}
[void](New-Item -ItemType Directory -Path $OutputDirectory)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'research/winui3-probe/capture-guard.ps1')
. (Join-Path $PSScriptRoot 'research/winui3-probe/capture-backdrop.ps1')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class NativeAutomaticContract {
 public delegate bool EnumProc(IntPtr h,IntPtr d);
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]struct M{public int x,y;public uint data,flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)]struct U{[FieldOffset(0)]public M m;}
 [StructLayout(LayoutKind.Sequential)]struct I{public uint type;public U u;}
 [StructLayout(LayoutKind.Sequential)]struct Title{public int size;public R bar;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public uint[] state;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public R[] rect;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc fn,IntPtr d);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder n,int size);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string n);
 [DllImport("user32.dll")]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]static extern short GetAsyncKeyState(int key);
 [DllImport("user32.dll")]static extern IntPtr WindowFromPoint(P point);
 [DllImport("user32.dll")]public static extern bool GetPhysicalCursorPos(out P point);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern uint SendInput(uint count,I[] input,int size);
 public static IntPtr Find(uint pid,bool fixture){IntPtr result=IntPtr.Zero;EnumWindows((h,d)=>{uint p;GetWindowThreadProcessId(h,out p);var n=new StringBuilder(256);GetClassName(h,n,256);if(p==pid&&(fixture?GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero:n.ToString()=="Macdows.WindowStyler.Controller.v1")){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,3000,out r)==IntPtr.Zero)throw new Exception("Owned window timeout");return unchecked((long)r.ToUInt64());}
 public static void Activate(IntPtr h,uint pid,bool initial=false){
  if(Find(pid,true)!=h)throw new Exception("Activation target is not the owned fixture");
  SetForegroundWindow(h);Send(h,0);
  if(GetForegroundWindow()!=h&&initial){
   // Only this owned, never-injected fixture's empty native caption is eligible.
   // Do not use this fallback for Office, custom captions or an attached target.
   if(SetThreadDpiAwarenessContext((IntPtr)(-4))==IntPtr.Zero||GetProp(h,"Macdows.WindowStyler.Owner.v1")!=IntPtr.Zero)throw new Exception("Initial activation requires an uninjected owned fixture");
   foreach(var key in new[]{1,2,4,5,6,16,17,18,91,92})if(GetAsyncKeyState(key)<0)throw new Exception("User input is held");
   R bounds;if(!GetWindowRect(h,out bounds)||bounds.r-bounds.l<400)throw new Exception("Invalid owned activation bounds");
   var point=new P{x=(bounds.l+bounds.r)/2,y=bounds.t+20};
   var packed=unchecked((uint)((point.x&65535)|((point.y&65535)<<16)));UIntPtr hit;
   if(SendMessageTimeout(h,0x84,IntPtr.Zero,(IntPtr)(long)packed,2,2000,out hit)==IntPtr.Zero||hit.ToUInt64()!=2)throw new Exception("Initial activation point is not native caption");
   if(!SetCursorPos(point.x,point.y))throw new Exception("Cannot place owned activation cursor");
   P actual;if(!GetPhysicalCursorPos(out actual)||actual.x!=point.x||actual.y!=point.y||WindowFromPoint(point)!=h||Find(pid,true)!=h)throw new Exception("Owned activation point moved or is occluded");
   foreach(var key in new[]{1,2,4,5,6,16,17,18,91,92})if(GetAsyncKeyState(key)<0)throw new Exception("User input changed before activation");
   var values=new[]{new I{u=new U{m=new M{flags=2}}},new I{u=new U{m=new M{flags=4}}}};
   if(SendInput(2,values,Marshal.SizeOf(typeof(I)))!=2)throw new Exception("Owned activation input rejected");
  }
  var deadline=DateTime.UtcNow.AddSeconds(2);
  while(GetForegroundWindow()!=h&&DateTime.UtcNow<deadline)System.Threading.Thread.Sleep(20);
  if(Find(pid,true)!=h||GetForegroundWindow()!=h)throw new Exception("Owned fixture activation denied; no input sent");
 }
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.r,c.b,p.x,p.y};}
 public static bool OldButtonsHidden(IntPtr h){var v=new Title{size=Marshal.SizeOf(typeof(Title)),state=new uint[6],rect=new R[6]};var p=Marshal.AllocHGlobal(v.size);try{Marshal.StructureToPtr(v,p,false);UIntPtr r;if(SendMessageTimeout(h,0x33f,IntPtr.Zero,p,2,3000,out r)==IntPtr.Zero)return false;v=(Title)Marshal.PtrToStructure(p,typeof(Title));foreach(var i in new[]{2,3,5})if((v.state[i]&0x18001)!=0x18001||v.rect[i].l!=0||v.rect[i].t!=0||v.rect[i].r!=0||v.rect[i].b!=0)return false;return true;}finally{Marshal.FreeHGlobal(p);}}
}
'@
[void][NativeAutomaticContract]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new();$runs=[Collections.Generic.List[object]]::new()
$foreground=[NativeAutomaticContract]::GetForegroundWindow()
$cursor=[NativeAutomaticContract+P]::new();if(-not [NativeAutomaticContract]::GetPhysicalCursorPos([ref]$cursor)){throw 'Cannot save cursor position'}
$fixture=$null;$controller=$null;$scopeId=$null;$scopePath=$null;$scopeOwned=$false;$h=[IntPtr]::Zero;$owner=[IntPtr]::Zero
$testMessage=[NativeAutomaticContract]::RegisterWindowMessage('Macdows.WindowChanger.Test.v3')
function Check($value,$name){$checks.Add([pscustomobject]@{scenario=$script:scenario;name=$name;passed=[bool]$value});$checks|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'checks.json') -Encoding utf8;if(-not $value){throw ('Check failed: '+$name)}}
function Until([scriptblock]$test){$end=[DateTime]::UtcNow.AddSeconds(12);do{if(&$test){return $true};Start-Sleep -Milliseconds 80}while([DateTime]::UtcNow -lt $end);return $false}
function Owner{[NativeAutomaticContract]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1')}
function Managed{
 $lease=Owner
 return $lease -ne [IntPtr]::Zero -and [NativeAutomaticContract]::GetProp($lease,'Macdows.WindowStyler.Host.Parent.v2') -eq $script:owner
}
function Metric([int]$n){[NativeAutomaticContract]::Send($script:owner,$testMessage,$n)}
function Snapshot($name){
 [NativeAutomaticContract]::Activate($script:h,$fixture.Id);Start-Sleep -Milliseconds 450
 [NativeIslandCaptureBackdrop]::Place($script:h);Start-Sleep -Milliseconds 100
 Check ([NativeAutomaticContract]::GetForegroundWindow() -eq $script:h) ('owned foreground for '+$name)
 $g=[NativeAutomaticContract]::Geometry($script:h)
 foreach($part in @('window','body')){
  if($part -eq 'window'){$x=$g[0];$y=$g[1];$w=$g[2]-$g[0];$t=$g[3]-$g[1]}else{$x=$script:geometry[6];$y=$script:geometry[7];$w=$script:geometry[4];$t=$script:geometry[5]}
  [WindowStylerCaptureGuard]::Check($script:h,$x,$y,$w,$t)
  $b=[Drawing.Bitmap]::new($w,$t);$dc=[Drawing.Graphics]::FromImage($b)
  try{$dc.CopyFromScreen($x,$y,0,0,$b.Size);$b.Save((Join-Path $script:dir ($name+'-'+$part+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$dc.Dispose();$b.Dispose()}
 }
}
function RemoveScope{
 if($script:scopeOwned -and $script:scopeId -and $script:scopeId -match '^winui-auto-[0-9a-f]{32}$' -and $script:scopePath -ceq ('Software\macdowsOS\WindowChanger\Tests\'+$script:scopeId)){[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($script:scopePath,$false);$script:scopeOwned=$false}
}
try{
 foreach($script:scenario in @('normal','crash')){
  [NativeIslandCaptureBackdrop]::Close()
  $script:dir=Join-Path $OutputDirectory $script:scenario;[void](New-Item -ItemType Directory -Path $script:dir)
  $script:scopeId='winui-auto-'+[Guid]::NewGuid().ToString('N');$script:scopePath='Software\macdowsOS\WindowChanger\Tests\'+$script:scopeId
  $existing=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($scopePath)
  if($existing){$existing.Dispose();throw 'Test namespace already exists'}
  $script:scopeOwned=$true
  $fixture=Start-Process -FilePath $fixturePath -ArgumentList '--dark' -PassThru -WindowStyle Hidden
  Check (Until {[NativeAutomaticContract]::Find($fixture.Id,$true) -ne [IntPtr]::Zero}) 'owned fixture starts'
  $script:h=[NativeAutomaticContract]::Find($fixture.Id,$true)
  [void][NativeAutomaticContract]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,0x43)
  [NativeAutomaticContract]::Activate($h,$fixture.Id,$true);Start-Sleep -Milliseconds 600
  Check ((Owner) -eq [IntPtr]::Zero) 'fixture has no previous controller'
  $script:geometry=[NativeAutomaticContract]::Geometry($h);Snapshot 'before'
  $controller=[Diagnostics.Process]::new();$controller.StartInfo.FileName=$controllerPath
  foreach($argument in @('--background','--test-mode',$scopeId,'--scope-pid',$fixture.Id.ToString())){$controller.StartInfo.ArgumentList.Add($argument)}
  # Test exactly the built EXE at the caller's integrity, without editing its
  # manifest or requesting UAC. The setting is private to this child process.
  $controller.StartInfo.UseShellExecute=$false;$controller.StartInfo.Environment['__COMPAT_LAYER']='RunAsInvoker'
  [void]$controller.Start()
  Check (Until {$script:owner=[NativeAutomaticContract]::Find($controller.Id,$false);$script:owner -ne [IntPtr]::Zero}) 'actual production controller starts in isolated test scope'
  Check ((Metric 14) -eq $fixture.Id) 'test controller is restricted to the owned PID'
  Check ((Metric 6) -eq 1) 'first use enables automatic management'
  Check (Until {Managed}) 'default automatic discovery attaches owned WinUI window through its guarded host'
  [uint32]$leasePid=0;[void][NativeAutomaticContract]::GetWindowThreadProcessId((Owner),[ref]$leasePid)
  $leaseProcess=Get-Process -Id $leasePid
  Check ($leaseProcess.Path -ceq (Join-Path $runtime 'WindowStyleHost.exe')) 'target lease belongs to the production helper from this runtime'
  Check ([NativeAutomaticContract]::GetProp($h,'Macdows.WindowStyler.Adapter.v1').ToInt64() -eq 288) 'automatic discovery selects native WinUI backend'
  Check ([NativeAutomaticContract]::OldButtonsHidden($h)) 'original native buttons are hidden unavailable and empty'
  Check ((Metric 5) -eq 1 -and (Metric 10) -ge 1) 'actual controller has a registered tray icon'
  $trayCount=Metric 10;Check ((Metric 4) -eq 1 -and (Metric 10) -gt $trayCount) 'tray icon is re-added after taskbar notification'
  [void](Metric 2);Check ([NativeAutomaticContract]::IsWindowVisible($owner)) 'tray show action opens actual controller'
  [void][NativeAutomaticContract]::Send($owner,0x10)
  Check (-not $controller.HasExited -and -not [NativeAutomaticContract]::IsWindowVisible($owner) -and (Metric 5) -eq 1) 'close hides controller and preserves tray management'
  Snapshot 'attached'
  Check ((Get-FileHash (Join-Path $dir 'before-body.png')).Hash -eq (Get-FileHash (Join-Path $dir 'attached-body.png')).Hash) 'automatic attachment preserves complete body PNG'
  [void][NativeAutomaticContract]::Send($owner,0x111,105)
  Check (Until {(Owner) -eq [IntPtr]::Zero}) 'restore-all removes actual target lease'
  Check ((Metric 6) -eq 0) 'manual restore pauses automatic discovery'
  Snapshot 'paused'
  Check ((Get-FileHash (Join-Path $dir 'before-window.png')).Hash -eq (Get-FileHash (Join-Path $dir 'paused-window.png')).Hash) 'manual restore reproduces complete original window'
  [void][NativeAutomaticContract]::Send($owner,0x111,108)
  Check ((Metric 6) -eq 1 -and (Until {Managed})) 'resume re-detects supported window without exclusion list'
  $fixture.Refresh();$loaded=@($fixture.Modules|Where-Object {$_.FileName -like ($runtime+'\MacdowsWinUI3Native-*.dll')})
  Check ($loaded.Count -eq 1) 'target loaded production native module from this runtime'
  $nativeHash=(Get-FileHash -LiteralPath $loaded[0].FileName).Hash
  Check ($controller.Path -ceq $controllerPath -and $controller.Id -gt 0 -and (Managed)) 'exit target is this owned controller and its guarded lease'
  if($scenario -eq 'crash'){$controller.Kill()}else{[void](Metric 9)}
  Check ($controller.WaitForExit(6000) -and $controller.ExitCode -eq $(if($scenario -eq 'crash'){-1}else{0})) 'controller exits with expected status'
  Check (Until {(Owner) -eq [IntPtr]::Zero}) 'target restores after actual controller exit'
  Check (($geometry -join ',') -eq ([NativeAutomaticContract]::Geometry($h) -join ',')) 'actual controller exit restores exact geometry'
  Snapshot 'restored'
  Check ((Get-FileHash (Join-Path $dir 'before-window.png')).Hash -eq (Get-FileHash (Join-Path $dir 'restored-window.png')).Hash) 'actual controller exit reproduces complete original window'
  Check ($fixture.Path -ceq $fixturePath -and [NativeAutomaticContract]::GetProp($h,'Macdows.Research.NativeIsland.Owned') -ne [IntPtr]::Zero) 'final target is still the owned fixture'
  [void][NativeAutomaticContract]::Send($h,0x10)
  Check ($fixture.WaitForExit(5000) -and $fixture.ExitCode -eq 0) 'owned fixture closes normally'
  $runs.Add([pscustomobject]@{scenario=$scenario;nativeModule=$loaded[0].FileName;nativeHash=$nativeHash;controllerHash=(Get-FileHash -LiteralPath $controllerPath).Hash;fixtureHash=(Get-FileHash -LiteralPath $fixturePath).Hash})
  $controller.Dispose();$controller=$null;$fixture.Dispose();$fixture=$null;RemoveScope;$scopeId=$null
 }
 [ordered]@{passed=$true;checks=$checks.Count;runs=$runs;launchMode='actual EXE with per-child RunAsInvoker; scoped same-integrity verification';elevatedStartupVerified=$false}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
}catch{($_|Out-String)+$_.ScriptStackTrace|Set-Content (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8;throw}
finally{
 if($controller){if(-not $controller.HasExited -and $controller.Path -ceq $controllerPath -and $owner -ne [IntPtr]::Zero){try{[void](Metric 9);[void]$controller.WaitForExit(7000)}catch{}};$controller.Dispose()}
 if($fixture){if(-not $fixture.HasExited -and $fixture.Path -ceq $fixturePath -and (Owner) -eq [IntPtr]::Zero -and [NativeAutomaticContract]::GetProp($h,'Macdows.Research.NativeIsland.Owned') -ne [IntPtr]::Zero){try{[void][NativeAutomaticContract]::Send($h,0x10);[void]$fixture.WaitForExit(5000)}catch{}};$fixture.Dispose()}
 RemoveScope
 [void][NativeAutomaticContract]::SetForegroundWindow($foreground)
 [NativeIslandCaptureBackdrop]::Close()
 [void][NativeAutomaticContract]::SetCursorPos($cursor.x,$cursor.y)
}
