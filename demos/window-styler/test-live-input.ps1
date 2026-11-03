param([string]$ReleaseDirectory='', [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v10/input'),[string[]]$Names=@())
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=if(Test-Path (Join-Path $PSScriptRoot 'StylerTests.exe')){$PSScriptRoot}else{Join-Path $PSScriptRoot 'out/build-x64/Release'}}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'failure.txt')){Remove-Item -LiteralPath (Join-Path $OutputDirectory 'failure.txt')}
trap {$_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt');exit 1}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class InputNative {
[StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
[StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
[StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
[DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr h);
[DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
[DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int h2,uint flags);
[DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
[DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
[DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
[DllImport("user32.dll")]public static extern IntPtr ChildWindowFromPointEx(IntPtr h,P p,uint flags);
[DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
[DllImport("user32.dll")]public static extern bool ScreenToClient(IntPtr h,ref P p);
[DllImport("user32.dll")]public static extern bool GetCursorPos(out P p);
[DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(P p);
[DllImport("user32.dll")]public static extern IntPtr GetParent(IntPtr h);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
[DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
[DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint data,UIntPtr extra);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string s);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string s);
[DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r;SendMessageTimeout(h,m,(IntPtr)w,(IntPtr)l,2,1500,out r);return unchecked((long)r.ToUInt64());}
public static long Metric(IntPtr h,int n){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1"),n);}
public static bool Ready(IntPtr h){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"))==0x4d414357;}
public static string PointerTarget(IntPtr h,int button){int dpi=(int)GetDpiForWindow(h);var p=new P{x=(int)Metric(h,7)+(18+20*button)*dpi/96,y=(int)(Metric(h,8)+Metric(h,10)/2)};ClientToScreen(h,ref p);if(Metric(h,10)<=0){R r;GetWindowRect(h,out r);p.y=r.t+(int)(Metric(h,4)+Metric(h,5))/2;}var result=p.x+","+p.y+" foreground="+GetForegroundWindow().ToInt64().ToString("x");for(var w=WindowFromPoint(p);w!=IntPtr.Zero;w=GetParent(w)){var name=new System.Text.StringBuilder(256);GetClassName(w,name,256);result+=" -> "+w.ToInt64().ToString("x")+" "+name+" hit="+Send(w,0x84,0,(long)(ushort)p.x|((long)(ushort)p.y<<16));}return result;}
public static void ClickPointer(IntPtr h,int button){if(!IsWindow(h)||!Ready(h))throw new InvalidOperationException("Target no longer ready; pointer input cancelled");int dpi=(int)GetDpiForWindow(h);var p=new P{x=(int)Metric(h,7)+(18+20*button)*dpi/96,y=(int)(Metric(h,8)+Metric(h,10)/2)};ClientToScreen(h,ref p);if(Metric(h,10)<=0){R r;GetWindowRect(h,out r);p.y=r.t+(int)(Metric(h,4)+Metric(h,5))/2;}P saved;GetCursorPos(out saved);SetCursorPos(p.x,p.y);System.Threading.Thread.Sleep(100);mouse_event(2,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(60);mouse_event(4,0,0,0,UIntPtr.Zero);SetCursorPos(saved.x,saved.y);}
public static void Click(IntPtr h,int button){int dpi=(int)GetDpiForWindow(h);bool inline=Metric(h,10)>0;var p=new P{x=(int)Metric(h,7)+(18+20*button)*dpi/96,y=(int)(Metric(h,8)+Metric(h,10)/2)};ClientToScreen(h,ref p);if(!inline){R r;GetWindowRect(h,out r);p.y=r.t+(int)(Metric(h,4)+Metric(h,5))/2;long screen=(long)(ushort)p.x|((long)(ushort)p.y<<16);Send(h,0xa1,button==0?20:button==1?8:9,screen);ScreenToClient(h,ref p);Send(h,0x202,0,(long)(ushort)p.x|((long)(ushort)p.y<<16));return;}IntPtr receiver=h;for(int i=0;i<12;i++){var q=p;ScreenToClient(receiver,ref q);var child=ChildWindowFromPointEx(receiver,q,7);if(child==IntPtr.Zero||child==receiver)break;receiver=child;}ScreenToClient(receiver,ref p);long xy=(long)(ushort)p.x|((long)(ushort)p.y<<16);Send(receiver,0x200,0,xy);Send(receiver,0x201,1,xy);Send(receiver,0x202,0,xy);}
}
'@
[void][InputNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
function Wait-State([scriptblock]$Test){$end=[DateTime]::UtcNow.AddSeconds(3);do {if(&$Test){return $true};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $end);return $false}
function Invoke-Control($target,[string]$Name){
 $root=[Windows.Automation.AutomationElement]::FromHandle($target)
 $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$Name)
 $nodes=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
 $rect=[InputNative+R]::new();[void][InputNative]::GetWindowRect($target,[ref]$rect)
 foreach($node in $nodes){$bounds=$node.Current.BoundingRectangle;if($bounds.Left -ge $rect.l -and $bounds.Left -lt $rect.l+150 -and $bounds.Top -lt $rect.t+110 -and $bounds.Width -gt 0){$pattern=$null;if($node.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke();return $true}}}
 return $false
}
$foreground=[InputNative]::GetForegroundWindow();$results=@()
$inventoryFile=Join-Path $OutputDirectory 'inventory.json'
& (Join-Path $ReleaseDirectory 'WindowAudit.exe') --direct-inventory $inventoryFile
$inventory=Get-Content $inventoryFile -Raw -Encoding UTF8|ConvertFrom-Json
$entries=@()
foreach($window in $inventory.windows){
 if(-not $window.eligible -or $window.class -eq 'Macdows.WindowStyler.Controller.v1'){continue}
 $name='';$max='Maximize or restore window';$restore=$max;$min='Minimize window'
 if($window.class -eq 'CASCADIA_HOSTING_WINDOW_CLASS'){$name='terminal'}
 elseif($window.class -eq 'TaskManagerWindow'){$name='taskmanager'}
 elseif($window.process -match '\\QQ.exe$'){$name='qq';$max='最大化';$restore='撤销最大化';$min='最小化'}
 elseif($window.process -match '\\ChatGPT.exe$'){$name='codex'}
 elseif($window.framework -eq 'WPF'){$name=if($window.process -match '\\devenv.exe$'){'visual-studio'}else{'tuning'};$max='最大化或还原窗口';$restore=$max;$min='最小化窗口'}
 elseif($window.framework -eq 'Console host'){$name='console'}
 elseif($window.framework -eq 'NetUI / Ribbon'){$name='netui'}
 elseif($window.framework -eq 'Win32 Shell'){$name='control-panel'}
 elseif($window.class -eq 'ApplicationFrameWindow'){$name='application-frame';$max='最大化';$restore='向下还原';$min='最小化'}
 elseif($window.framework -eq 'Win32'){$name='native'}
 if($name){$entries+=@{name=$name;hwnd=[Convert]::ToInt64($window.hwnd.Substring(2),16);max=$max;restore=$restore;min=$min}}
}
foreach($entry in $entries){
 if($Names.Count -and $entry.name -notin $Names){continue}
 $target=[IntPtr]$entry.hwnd;$placement=[InputNative+Placement]::new();$placement.size=[Runtime.InteropServices.Marshal]::SizeOf($placement);[void][InputNative]::GetWindowPlacement($target,[ref]$placement)
 $wasTopmost=([InputNative]::GetWindowLongPtr($target,-20).ToInt64() -band 8) -ne 0
 $worker=$null;$result=[ordered]@{name=$entry.name;hwnd=('0x{0:x}' -f $entry.hwnd);attached=$false;greenMaximizes=$false;greenRestores=$false;yellowMinimizes=$false;restored=$false;input='Win32 mouse messages'}
 try {
  [void][InputNative]::ShowWindow($target,9);[void][InputNative]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13);[void][InputNative]::SetForegroundWindow($target);Start-Sleep -Milliseconds 200
  $worker=Start-Process (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$target.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $OutputDirectory ($entry.name+'-'+$entry.hwnd+'-error.txt'))
  $result.attached=Wait-State {[InputNative]::Ready($target)}
  if($result.attached){
   $xamlPointer=[InputNative]::Metric($target,13) -in @(0,4,8)
   $strictPointer=[InputNative]::Metric($target,13) -eq 8
   $result.pointerTarget=[InputNative]::PointerTarget($target,2)
   if($xamlPointer){$result.input='native pointer';[InputNative]::ClickPointer($target,2)}else{[InputNative]::Click($target,2)};$result.greenMaximizes=Wait-State {[InputNative]::IsZoomed($target)}
   if(-not $result.greenMaximizes -and -not $strictPointer){if(Invoke-Control $target $entry.max){$result.input='UI Automation Invoke';$result.greenMaximizes=Wait-State {[InputNative]::IsZoomed($target)}}}
   if($result.greenMaximizes){
    Start-Sleep -Milliseconds 400
    $result.aliveAfterMaximize=[InputNative]::IsWindow($target);$result.restorePointerTarget=[InputNative]::PointerTarget($target,2)
    if($xamlPointer){[InputNative]::ClickPointer($target,2)}elseif($result.input -eq 'UI Automation Invoke'){[void](Invoke-Control $target $entry.restore)}else{[InputNative]::Click($target,2)}
    $result.greenRestores=Wait-State {[InputNative]::IsWindow($target) -and -not [InputNative]::IsZoomed($target)}
    if(-not $result.greenRestores){[void][InputNative]::SetForegroundWindow($target);[InputNative]::ClickPointer($target,2);$result.greenRestores=Wait-State {[InputNative]::IsWindow($target) -and -not [InputNative]::IsZoomed($target)};if($result.greenRestores){$result.input+=' + native pointer restore'}}
    if(-not $result.greenRestores -and -not $strictPointer){foreach($label in @($entry.restore,'向下还原','恢复')){if(Invoke-Control $target $label){$result.input+=' + UIA restore';$result.greenRestores=Wait-State {[InputNative]::IsWindow($target) -and -not [InputNative]::IsZoomed($target)};if($result.greenRestores){break}}}}
   }
   if(-not $result.greenRestores){[void][InputNative]::Send($target,0x112,0xf120)}
   Start-Sleep -Milliseconds 300
   $result.yellowPointerTarget=[InputNative]::PointerTarget($target,1);$result.readyBeforeYellow=[InputNative]::Ready($target);$result.styleBeforeYellow=[InputNative]::GetWindowLongPtr($target,-16).ToInt64();if($xamlPointer){[InputNative]::ClickPointer($target,1)}elseif($result.input -eq 'UI Automation Invoke'){[void](Invoke-Control $target $entry.min)}else{[InputNative]::Click($target,1)}
   $result.yellowMinimizes=Wait-State {[InputNative]::IsIconic($target)}
   if(-not $result.yellowMinimizes){[void][InputNative]::SetForegroundWindow($target);[InputNative]::ClickPointer($target,1);$result.yellowMinimizes=Wait-State {[InputNative]::IsIconic($target)};if($result.yellowMinimizes){$result.input+=' + native pointer minimize'}elseif(-not $strictPointer){if(Invoke-Control $target $entry.min){$result.input+=' + UIA minimize';$result.yellowMinimizes=Wait-State {[InputNative]::IsIconic($target)}}}}
   [void][InputNative]::ShowWindow($target,9)
  }
 }catch{$result.error=$_.Exception.Message}finally{
  if($worker -and -not $worker.HasExited){$worker.Kill();$worker.WaitForExit()}
  $result.alive=[InputNative]::IsWindow($target);$result.restored=Wait-State {[InputNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero}
  [void][InputNative]::SetWindowPlacement($target,[ref]$placement)
  if(-not $wasTopmost){[void][InputNative]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)}
  $results+=[pscustomobject]$result
  $results|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
 }
}
[void][InputNative]::SetForegroundWindow($foreground)
if(@($results|Where-Object {-not ($_.attached -and $_.greenMaximizes -and $_.greenRestores -and $_.yellowMinimizes -and $_.restored)}).Count){exit 1}
