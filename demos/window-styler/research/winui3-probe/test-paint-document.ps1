param(
 [ValidateSet('Release','Debug','All')][string]$Configuration='Release',
 [ValidateSet('normal','crash','all')][string]$Scenario='all',
 [switch]$WithoutInjection,
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-research'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../out/live-v41/paint-document')
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory to preserve earlier evidence'}
if(Get-Process mspaint -ErrorAction SilentlyContinue){throw 'Existing Paint instances are outside this owned-document test'}
$package=@(Get-AppxPackage Microsoft.Paint)
if($package.Count -ne 1){throw 'One installed Microsoft.Paint package is required'}
$paintPath=Join-Path $package[0].InstallLocation 'PaintApp/mspaint.exe'
$HostDirectory=(Resolve-Path -LiteralPath $HostDirectory).Path
[void](New-Item -ItemType Directory -Path $OutputDirectory)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing,UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class PaintDocumentInput {
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]struct K{public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Sequential)]struct M{public int x,y;public uint data,flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)]struct U{[FieldOffset(0)]public K k;[FieldOffset(0)]public M m;}
 [StructLayout(LayoutKind.Sequential)]struct I{public uint type;public U u;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern IntPtr WindowFromPoint(P p);
 [DllImport("user32.dll")]static extern bool IsChild(IntPtr p,IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int index);
 [DllImport("user32.dll")]static extern short GetAsyncKeyState(int k);
 [DllImport("user32.dll")]static extern uint SendInput(uint n,I[] input,int size);
 [DllImport("user32.dll")]static extern uint MapVirtualKey(uint code,uint map);
 [DllImport("user32.dll")]static extern int GetSystemMetrics(int index);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string n);
 [DllImport("user32.dll")]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,IntPtr.Zero,2,2000,out r)==IntPtr.Zero)throw new Exception("Owned window timeout");return unchecked((long)r.ToUInt64());}
 public static int[] Geometry(IntPtr h){R o,c;var p=new P();GetWindowRect(h,out o);GetClientRect(h,out c);ClientToScreen(h,ref p);return new[]{o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,p.x,p.y};}
 public static void Guard(IntPtr h,uint expected){uint p;GetWindowThreadProcessId(h,out p);if(!IsWindow(h)||p!=expected||GetForegroundWindow()!=h)throw new Exception("Owned Paint identity or foreground changed");foreach(var k in new[]{16,17,18,91,92})if(GetAsyncKeyState(k)<0)throw new Exception("User modifier is held");}
 static void Input(I[] a){if(SendInput((uint)a.Length,a,Marshal.SizeOf(typeof(I)))!=a.Length)throw new Exception("Input rejected");}
 static I Key(ushort k,uint flags){return new I{type=1,u=new U{k=new K{vk=k,scan=(ushort)MapVirtualKey(k,0),flags=flags}}};}
 public static void Press(IntPtr h,uint pid,ushort k){Guard(h,pid);try{Input(new[]{Key(17,0)});System.Threading.Thread.Sleep(30);Input(new[]{Key(k,0),Key(k,2)});}finally{Input(new[]{Key(17,2)});}System.Threading.Thread.Sleep(100);}
 static void Point(IntPtr h,int x,int y){var dx=(x-GetSystemMetrics(76))*65535/(GetSystemMetrics(78)-1);var dy=(y-GetSystemMetrics(77))*65535/(GetSystemMetrics(79)-1);Input(new[]{new I{u=new U{m=new M{x=dx,y=dy,flags=0xc001}}}});System.Threading.Thread.Sleep(35);var hit=WindowFromPoint(new P{x=x,y=y});if(hit!=h&&!IsChild(h,hit))throw new Exception("Owned input point is occluded");}
 public static void Park(IntPtr h,uint pid,int x,int y){Guard(h,pid);Point(h,x,y);}
 public static void Click(IntPtr h,uint pid,int x,int y){Guard(h,pid);Point(h,x,y);try{Input(new[]{new I{u=new U{m=new M{flags=2}}}});}finally{Input(new[]{new I{u=new U{m=new M{flags=4}}}});}System.Threading.Thread.Sleep(100);}
 public static void Stroke(IntPtr h,uint pid,int x,int y,int width){Guard(h,pid);Point(h,x,y);try{Input(new[]{new I{u=new U{m=new M{flags=2}}}});for(int i=1;i<=12;i++)Point(h,x+width*i/12,y);}finally{Input(new[]{new I{u=new U{m=new M{flags=4}}}});}System.Threading.Thread.Sleep(120);}
}
'@
[void][PaintDocumentInput]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks=[Collections.Generic.List[object]]::new()
$runs=[Collections.Generic.List[object]]::new()
$foreground=[PaintDocumentInput]::GetForegroundWindow()
$cursor=[PaintDocumentInput+P]::new();[void][PaintDocumentInput]::GetCursorPos([ref]$cursor)
$script:paint=$null;$script:worker=$null;$script:h=[IntPtr]::Zero;$script:toolRestored=$false
function Check($value,[string]$name){
 $checks.Add([pscustomobject]@{scenario=$script:run;name=$name;passed=[bool]$value})
 $checks|ConvertTo-Json -Depth 4|Set-Content (Join-Path $OutputDirectory 'checks.json') -Encoding utf8
 if(-not $value){throw ('Check failed: '+$name)}
}
function Until([scriptblock]$test){
 $deadline=[DateTime]::UtcNow.AddSeconds(6)
 do{if(&$test){return $true};Start-Sleep -Milliseconds 70}while([DateTime]::UtcNow -lt $deadline)
 return $false
}
function Owner { [PaintDocumentInput]::GetProp($script:h,'Macdows.WindowStyler.Owner.v1') }
function Element([string]$id){
 $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id)
 $found=$script:root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
 if($found.Count -ne 1){throw ('Expected unique owned control: '+$id)}
 return $found[0]
}
function ClickElement($el){
 $r=$el.Current.BoundingRectangle
 [PaintDocumentInput]::Click($script:h,$script:paint.Id,[int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/3))
}
function PixelHash([string]$file){
 $bitmap=[Drawing.Bitmap]::new($file)
 try{
  if($bitmap.Width -ne 960 -or $bitmap.Height -ne 540){throw 'Owned document dimensions changed'}
  $rect=[Drawing.Rectangle]::new(0,0,960,540)
  $bits=$bitmap.LockBits($rect,[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try{$bytes=[byte[]]::new([Math]::Abs($bits.Stride)*$bits.Height);[Runtime.InteropServices.Marshal]::Copy($bits.Scan0,$bytes,0,$bytes.Length);return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))}finally{$bitmap.UnlockBits($bits)}
 }finally{$bitmap.Dispose()}
}
function SaveDocument([string]$name){
 $before=(Get-Item -LiteralPath $script:document).LastWriteTimeUtc.Ticks
 [PaintDocumentInput]::Press($script:h,$script:paint.Id,83)
 Check (Until {(Get-Item -LiteralPath $script:document).LastWriteTimeUtc.Ticks -ne $before}) ($name+' saves the owned PNG')
 Start-Sleep -Milliseconds 150
 $copy=Join-Path $script:dir ($name+'.png')
 Copy-Item -LiteralPath $script:document -Destination $copy
 return PixelHash $copy
}
function ReadEditHistory {
 $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button)
 $buttons=@($script:root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition))
 $undo=@($buttons|Where-Object {$_.Current.Name -ceq '撤消'})
 $redo=@($buttons|Where-Object {$_.Current.Name -ceq '重做'})
 if($undo.Count -ne 1 -or $redo.Count -ne 1){throw 'Expected original Chinese Paint history buttons'}
 [ordered]@{undo=$undo[0].Current.IsEnabled;redo=$redo[0].Current.IsEnabled}
}
function SameHistory($expected,[string]$name){Check (($expected|ConvertTo-Json -Compress) -ceq ((ReadEditHistory)|ConvertTo-Json -Compress)) $name}
function Stroke([int]$x,[int]$y){[PaintDocumentInput]::Stroke($script:h,$script:paint.Id,([int]$script:canvas.X+$x),([int]$script:canvas.Y+$y),180)}
function Key([int]$k){[PaintDocumentInput]::Press($script:h,$script:paint.Id,$k)}
function Snapshot([string]$name){
 [PaintDocumentInput]::Guard($script:h,$script:paint.Id)
 [PaintDocumentInput]::Park($script:h,$script:paint.Id,($script:geometry[0]+700),($script:geometry[1]+20))
 Start-Sleep -Milliseconds 400
 $current=[PaintDocumentInput]::Geometry($script:h)
 foreach($part in @('window','body')){
  if($part -eq 'window'){$x=$current[0];$y=$current[1];$width=$current[2]-$current[0];$height=$current[3]-$current[1]}
  else{$x=$script:geometry[8];$y=$script:geometry[9];$width=$script:geometry[6];$height=$script:geometry[7]}
  $b=[Drawing.Bitmap]::new($width,$height);$g=[Drawing.Graphics]::FromImage($b)
  try{$g.CopyFromScreen($x,$y,0,0,$b.Size);$b.Save((Join-Path $script:dir ($name+'-'+$part+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$b.Dispose()}
 }
}
function SameShot([string]$left,[string]$right,[string]$name){Check ((Get-FileHash -LiteralPath $left).Hash -eq (Get-FileHash -LiteralPath $right).Hash) $name}
function StartResearch {
 $script:worker=[Diagnostics.Process]::new()
 $script:worker.StartInfo.FileName=$script:hostPath;$script:worker.StartInfo.Arguments=$script:h.ToInt64().ToString()+' 30'
 $script:worker.StartInfo.UseShellExecute=$false;$script:worker.StartInfo.CreateNoWindow=$true
 $script:worker.StartInfo.RedirectStandardOutput=$true;$script:worker.StartInfo.RedirectStandardError=$true
 [void]$script:worker.Start()
 $script:stdout=$script:worker.StandardOutput.ReadToEndAsync();$script:stderr=$script:worker.StandardError.ReadToEndAsync()
 Check (Until {(Owner) -ne [IntPtr]::Zero -or $script:worker.HasExited}) 'research attach completes'
 Check ((Owner) -ne [IntPtr]::Zero -and [PaintDocumentInput]::Send($script:h,[PaintDocumentInput]::RegisterWindowMessage('Macdows.WindowStyler.Query.v1')) -eq 0x4d414357) 'research lease acknowledges attachment'
 $native=(Select-String -LiteralPath (Join-Path (Split-Path $script:hostPath) 'adapters.ini') -Pattern '^File=(.+)$').Matches.Groups[1].Value
 Check ($native -eq 'WinUI3NativeIsland-0f54df5a5444.dll') 'current native revision selected'
 $script:paint.Refresh()
 $loaded=@($script:paint.Modules|Where-Object {$_.FileName -eq (Join-Path (Split-Path $script:hostPath) $native)})
 Check ($loaded.Count -eq 1) 'current native module path verified'
 $script:nativeHash=(Get-FileHash -LiteralPath $loaded[0].FileName).Hash
}
function FinishResearch([bool]$crash){
 $owner=Owner;[uint32]$ownerPid=0;[void][PaintDocumentInput]::GetWindowThreadProcessId($owner,[ref]$ownerPid)
 Check ($owner -ne [IntPtr]::Zero -and $ownerPid -eq $script:worker.Id -and $script:worker.Path -eq $script:hostPath) 'rollback controller is this owned process'
 if($crash){$script:worker.Kill()}else{Check ([PaintDocumentInput]::Send($script:h,[PaintDocumentInput]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),$owner.ToInt64()) -eq 0x4d414357) 'normal detach acknowledges restoration'}
 Check (Until {(Owner) -eq [IntPtr]::Zero}) 'original window automatically restored'
 Check ($script:worker.WaitForExit(6000) -and $script:worker.ExitCode -eq $(if($crash){-1}else{0})) 'research controller exits as expected'
 $script:stdout.GetAwaiter().GetResult()|Set-Content (Join-Path $script:dir 'worker.log') -Encoding utf8
 $script:stderr.GetAwaiter().GetResult()|Set-Content (Join-Path $script:dir 'worker.err') -Encoding utf8
 $script:worker.Dispose();$script:worker=$null
}
try {
 $configs=if($Configuration -eq 'All'){@('Release','Debug')}else{@($Configuration)}
 $modes=if($Scenario -eq 'all'){@('normal','crash')}else{@($Scenario)}
 foreach($config in $configs){foreach($mode in $modes){
  $matrix=$config+'-'+$mode;$script:hostPath=Join-Path $HostDirectory ($config+'/WinUI3NativeIslandHost.exe')
  # Establish two complete uninjected replays before loading a research DLL.
  # Paint itself can produce two different toolbar-edge rasterizations. The
  # research output must exactly equal one complete native PNG; no pixel mask,
  # numerical tolerance, or reference captured after injection is accepted.
  foreach($role in @('control','repeat','research')){
   $control=$role -ne 'research'
   $injected=-not $control -and -not $WithoutInjection
   $script:run=$matrix+$(if($role -eq 'control'){'-control'}elseif($role -eq 'repeat'){'-control-repeat'}elseif($WithoutInjection){'-native-repeat'}else{''});$script:dir=Join-Path $OutputDirectory $script:run
   [void](New-Item -ItemType Directory -Path $script:dir)
   $script:document=Join-Path $script:dir 'owned-document.png'
   $b=[Drawing.Bitmap]::new(960,540);$g=[Drawing.Graphics]::FromImage($b)
   try{$g.Clear([Drawing.Color]::FromArgb(247,241,229));$g.FillRectangle([Drawing.Brushes]::RoyalBlue,16,16,64,32);$g.FillRectangle([Drawing.Brushes]::IndianRed,880,492,64,32);$b.Save($script:document,[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$b.Dispose()}
   Copy-Item -LiteralPath $script:document -Destination (Join-Path $script:dir 'initial-document.png')
   $initialHash=PixelHash $script:document
   Check (-not(Get-Process mspaint -ErrorAction SilentlyContinue)) 'no existing Paint instance'
   $script:paint=Start-Process -FilePath $paintPath -ArgumentList ('"'+$script:document+'"') -PassThru -WindowStyle Normal
   Check (Until {$script:paint.Refresh();$script:paint.MainWindowHandle -ne [IntPtr]::Zero -and $script:paint.MainWindowTitle.Contains('owned-document.png')}) 'owned PNG opens in Paint'
   Check ($script:paint.Path -eq $paintPath) 'owned Paint executable identity verified'
   $script:h=$script:paint.MainWindowHandle
   Check ([PaintDocumentInput]::GetDpiForWindow($script:h) -eq 144 -and -not [PaintDocumentInput]::IsZoomed($script:h)) 'actual window uses the verified 144 DPI normal layout'
   $script:geometry=[PaintDocumentInput]::Geometry($script:h);$style=[PaintDocumentInput]::GetWindowLongPtr($script:h,-16).ToInt64()
   [void][PaintDocumentInput]::SetWindowPos($script:h,[IntPtr](-1),0,0,0,0,0x43)
   [void][PaintDocumentInput]::SetForegroundWindow($script:h)
   Start-Sleep -Milliseconds 600
   Check ((Owner) -eq [IntPtr]::Zero) 'initial window has no previous lease'
   $script:root=[Windows.Automation.AutomationElement]::FromHandle($script:h)
   $pencil=Element 'PencilTool';$brush=Element 'BrushesSplitButton'
   $originalBrush=$brush.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected
   Check $originalBrush 'original brush selection can be restored after the owned test'
   $script:toolRestored=$false
   ClickElement $pencil
   Check ($pencil.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -eq [Windows.Automation.ToggleState]::On) 'original pencil selected through real mouse input'
   $script:canvas=(Element 'image').Current.BoundingRectangle
   Check ($script:canvas.Width -eq 960 -and $script:canvas.Height -eq 540) 'real canvas displays original pixels at 100 percent'
   Stroke 180 160;$hashA=SaveDocument 'A'
   if($hashA -eq $initialHash){Snapshot 'failed-A';[ordered]@{canvas=$script:canvas.ToString();initialHash=$initialHash;strokeHash=$hashA;history=(ReadEditHistory)}|ConvertTo-Json|Set-Content (Join-Path $script:dir 'failed-A.json') -Encoding utf8}
   Check ($hashA -ne $initialHash) 'real pencil stroke A changes saved pixels'
   Stroke 450 240;$hashB=SaveDocument 'B'
   Check ($hashB -ne $hashA) 'second stroke B creates a distinct edit'
   Key 90;Check ((SaveDocument 'pre-attach-undo-B') -eq $hashA) 'native undo restores A'
   $history=ReadEditHistory
   Check ($history.undo -and $history.redo) 'real document has both undo and redo history before attachment'
   Snapshot 'before'
   if($injected){StartResearch}
   Check ((PixelHash $script:document) -eq $hashA) 'attachment preserves original saved document'
   SameHistory $history 'attachment preserves real history controls'
   Snapshot 'attached'
   SameShot (Join-Path $script:dir 'before-body.png') (Join-Path $script:dir 'attached-body.png') 'attachment preserves all original client pixels'
   Key 89;Check ((SaveDocument 'attached-redo-B') -eq $hashB) 'redo history survives attachment'
   Key 90;Check ((SaveDocument 'attached-undo-B') -eq $hashA) 'undo history survives attachment'
   Stroke 500 340;$hashC=SaveDocument 'C'
   Check ($hashC -ne $hashA -and $hashC -ne $hashB) 'new attached stroke C changes saved document'
   $attachedGeometry=[PaintDocumentInput]::Geometry($script:h)
   if(-not $injected){[void][PaintDocumentInput]::Send($script:h,0x112,0xf030)}else{[PaintDocumentInput]::Click($script:h,$script:paint.Id,($attachedGeometry[8]+87),($attachedGeometry[9]+20))}
   Check (Until {[PaintDocumentInput]::IsZoomed($script:h)}) 'real green button or native control maximizes'
   Start-Sleep -Milliseconds 400
   if(-not $injected){[void][PaintDocumentInput]::Send($script:h,0x112,0xf120)}else{$max=[PaintDocumentInput]::Geometry($script:h);[PaintDocumentInput]::Click($script:h,$script:paint.Id,($max[8]+87),($max[9]+12))}
   Check (Until {-not [PaintDocumentInput]::IsZoomed($script:h)}) 'real green button or native control restores'
   Start-Sleep -Milliseconds 400
   Check (($attachedGeometry -join ',') -eq ([PaintDocumentInput]::Geometry($script:h) -join ',')) 'maximize restore preserves frame geometry'
   Check ($script:canvas.ToString() -ceq (Element 'image').Current.BoundingRectangle.ToString()) 'maximize restore preserves actual canvas screen position'
   Key 90;Check ((SaveDocument 'attached-undo-C') -eq $hashA) 'undo C works after maximize restore'
   Key 89;Check ((SaveDocument 'attached-redo-C') -eq $hashC) 'redo C works after maximize restore'
   $history=ReadEditHistory;Snapshot 'before-rollback'
   if($injected){FinishResearch ($mode -eq 'crash')}
   Check ((Owner) -eq [IntPtr]::Zero) 'original native lease state restored'
   Check (($script:geometry -join ',') -eq ([PaintDocumentInput]::Geometry($script:h) -join ',')) 'rollback preserves original outer and client geometry'
   Check ([PaintDocumentInput]::GetWindowLongPtr($script:h,-16).ToInt64() -eq $style) 'rollback preserves original native capabilities'
   SameHistory $history 'rollback preserves real edit history controls'
   Snapshot 'restored'
   SameShot (Join-Path $script:dir 'before-rollback-body.png') (Join-Path $script:dir 'restored-body.png') 'rollback preserves complete current document and toolbar pixels'
   Key 90;Check ((SaveDocument 'restored-undo-C') -eq $hashA) 'undo attached edit survives rollback'
   Key 89;Check ((SaveDocument 'restored-redo-C') -eq $hashC) 'redo attached edit survives rollback'
   Stroke 200 410;$hashD=SaveDocument 'D'
   Check ($hashD -ne $hashC) 'restored original document remains editable'
   Key 90;Check ((SaveDocument 'final-document') -eq $hashC) 'undo new native stroke preserves earlier attached edits'
   Snapshot 'final'
   $final=[ordered]@{a=$hashA;b=$hashB;c=$hashC;d=$hashD;history=(ReadEditHistory)}
   $finalWindowHash=(Get-FileHash -LiteralPath (Join-Path $script:dir 'final-window.png')).Hash
   if($role -eq 'control'){
    $reference=$final;$referenceWindowHashes=[Collections.Generic.List[string]]::new();$referenceWindowHashes.Add($finalWindowHash)
   }else{
    Check (($reference|ConvertTo-Json -Compress) -ceq ($final|ConvertTo-Json -Compress)) 'saved document pixels and history match uninjected control'
    if($role -eq 'repeat'){$referenceWindowHashes.Add($finalWindowHash)}else{
     Check ($referenceWindowHashes.Count -eq 2) 'two independent native references were captured before injection'
     Check ($referenceWindowHashes.Contains($finalWindowHash)) 'complete final window exactly matches a pre-recorded uninjected replay'
    }
   }
   ClickElement $brush
   Check ($brush.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) 'original tool selection restored'
   $script:toolRestored=$true
   Check ($script:paint.Path -eq $paintPath -and $script:paint.MainWindowTitle.Contains('owned-document.png')) 'close target remains this owned document process'
   [void]$script:paint.CloseMainWindow()
   Check ($script:paint.WaitForExit(5000) -and $script:paint.ExitCode -eq 0) 'owned saved document closes without prompt or crash'
   $runs.Add([pscustomobject]@{name=$script:run;configuration=$config;role=$role;control=$control;injected=$injected;final=$final;finalWindowHash=$finalWindowHash;nativeReferenceWindowHashes=$referenceWindowHashes.ToArray();nativeHash=$(if($injected){$script:nativeHash}else{$null});hostHash=(Get-FileHash -LiteralPath $script:hostPath).Hash})
   $script:paint.Dispose();$script:paint=$null
  }
 }}
 [ordered]@{passed=$true;checks=$checks.Count;withoutInjection=[bool]$WithoutInjection;paintVersion=$package[0].Version.ToString();runs=$runs;changedTheme=$false;scope='owned PNG documents; actual Pencil, saves and undo/redo; current installed theme at 144 DPI'}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
}catch{
 ($_|Out-String)+$_.ScriptStackTrace|Set-Content (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
 throw
}finally{
 if($script:worker){
  try{if((Owner) -ne [IntPtr]::Zero){[void][PaintDocumentInput]::Send($script:h,[PaintDocumentInput]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),(Owner).ToInt64())};[void]$script:worker.WaitForExit(32000);$script:stdout.GetAwaiter().GetResult()|Set-Content (Join-Path $script:dir 'worker.log') -Encoding utf8;$script:stderr.GetAwaiter().GetResult()|Set-Content (Join-Path $script:dir 'worker.err') -Encoding utf8}finally{$script:worker.Dispose()}
 }
 if($script:paint -and -not $script:paint.HasExited){
  if($script:paint.Path -eq $paintPath -and $script:paint.MainWindowTitle.Contains('owned-document.png') -and (Owner) -eq [IntPtr]::Zero){
   try{[void][PaintDocumentInput]::SetForegroundWindow($script:h);if(-not $script:toolRestored){ClickElement (Element 'BrushesSplitButton')};Key 83;Start-Sleep -Milliseconds 400;[void]$script:paint.CloseMainWindow();[void]$script:paint.WaitForExit(5000)}catch{}
  }
  $script:paint.Dispose()
 }
 [void][PaintDocumentInput]::SetCursorPos($cursor.x,$cursor.y)
 [void][PaintDocumentInput]::SetForegroundWindow($foreground)
}
