param([Parameter(Mandatory=$true)][string]$Hwnd,[string]$Name='window',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out\live-v10\final-screenshots'),
 [string]$ReleaseDirectory='', [switch]$CaptureOnly,[switch]$HostRefresh)
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=if(Test-Path (Join-Path $PSScriptRoot 'StylerTests.exe')){$PSScriptRoot}else{Join-Path $PSScriptRoot 'out/build-x64/Release'}}
$release=$ReleaseDirectory
$directory=Join-Path $OutputDirectory $Name
[void](New-Item -ItemType Directory -Path $directory -Force)
Add-Type -AssemblyName System.Drawing
if (-not ('CaptureNative' -as [type])) { Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CaptureNative {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 public static int Children(IntPtr h){int n=0;EnumChildWindows(h,(w,p)=>{n++;return true;},IntPtr.Zero);return n;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct TitleInfo{public uint size;public R title;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public uint[] state;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public R[] rects;}
 [StructLayout(LayoutKind.Sequential)]public struct Scroll{public int size;public R rect;public int line,thumbTop,thumbBottom,reserved;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public uint[] state;}
 [DllImport("user32.dll")]public static extern bool GetScrollBarInfo(IntPtr h,int id,ref Scroll s);
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr d);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern int GetWindowRgn(IntPtr h,IntPtr r);
 [DllImport("gdi32.dll")]public static extern IntPtr CreateRectRgn(int l,int t,int r,int b);
 [DllImport("gdi32.dll")]public static extern int GetRgnBox(IntPtr r,out R b);
 [DllImport("gdi32.dll")]public static extern bool PtInRegion(IntPtr r,int x,int y);
 [DllImport("gdi32.dll")]public static extern bool DeleteObject(IntPtr r);
 [DllImport("dwmapi.dll")]public static extern int DwmGetWindowAttribute(IntPtr h,int n,out int v,int cb);
 [DllImport("dwmapi.dll",EntryPoint="DwmGetWindowAttribute")]public static extern int DwmRect(IntPtr h,int n,out R v,int cb);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern IntPtr SetWindowLongPtr(IntPtr h,int n,IntPtr value);
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll",EntryPoint="SetWindowPlacement")]static extern bool PlacementNative(IntPtr h,ref Placement p);
 public static bool SetWindowPlacement(IntPtr h,ref Placement p){var queued=p;queued.flags|=4;return PlacementNative(h,ref queued);}
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll")]public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Metric(IntPtr h,int n,int button=0){UIntPtr r;SendMessageTimeout(h,RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1"),(IntPtr)n,(IntPtr)button,2,1000,out r);return unchecked((long)r.ToUInt64());}
 public static bool LightCenter(IntPtr h,int kind,int index,out P p){p=new P();string name=kind==16?"Macdows.MfcPaintProbe.Metrics.v1":kind==19?"Macdows.OfficeLease.Metrics.v1":kind==20?"Macdows.TXGuiLeftLease.Metrics.v1":null;if(kind==0||kind==21){p.x=(int)Metric(h,16,index);p.y=(int)Metric(h,17,index);return p.x!=0||p.y!=0;}if(name==null)return false;UIntPtr x,y;uint message=RegisterWindowMessage(name);if(SendMessageTimeout(h,message,(IntPtr)(10+index*2),IntPtr.Zero,2,1000,out x)==IntPtr.Zero||SendMessageTimeout(h,message,(IntPtr)(11+index*2),IntPtr.Zero,2,1000,out y)==IntPtr.Zero)return false;p.x=unchecked((int)x.ToUInt64());p.y=unchecked((int)y.ToUInt64());return p.x!=0||p.y!=0;}
 public static int NativeCaptionButtons(IntPtr h){var info=new TitleInfo{state=new uint[6],rects=new R[6]};info.size=(uint)Marshal.SizeOf(info);var data=Marshal.AllocHGlobal((int)info.size);try{Marshal.StructureToPtr(info,data,false);UIntPtr result;if(SendMessageTimeout(h,0x33f,IntPtr.Zero,data,2,1000,out result)==IntPtr.Zero)return -1;info=(TitleInfo)Marshal.PtrToStructure(data,typeof(TitleInfo));int count=0;foreach(int i in new[]{2,3,5}){var r=info.rects[i];if((info.state[i]&0x18001)==0&&r.right>r.left&&r.bottom>r.top)count++;}return count;}finally{Marshal.FreeHGlobal(data);}}
 [DllImport("user32.dll",EntryPoint="ShowWindowAsync")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetSystemMenu(IntPtr h,bool revert);
 [DllImport("user32.dll")]public static extern uint GetMenuState(IntPtr menu,uint id,uint flags);
 public static bool[] Capabilities(IntPtr h){long s=GetWindowLongPtr(h,-16).ToInt64();bool enabled=IsWindowEnabled(h);IntPtr menu=GetSystemMenu(h,false);uint close=menu==IntPtr.Zero?uint.MaxValue:GetMenuState(menu,0xf060,0);return new[]{enabled&&(s&0x80000)!=0&&(close==uint.MaxValue||(close&3)==0),enabled&&(s&0x20000)!=0,enabled&&(s&0x10000)!=0};}
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("kernel32.dll")]public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")]public static extern bool AttachThreadInput(uint a,uint b,bool yes);
 [DllImport("user32.dll",EntryPoint="SetWindowPos")]static extern bool PositionNative(IntPtr h,IntPtr after,int x,int y,int w,int h2,uint flags);
 public static bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int h2,uint flags){return PositionNative(h,after,x,y,w,h2,flags|0x4000);}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string n);
 public static bool Ready(IntPtr h){UIntPtr result;return SendMessageTimeout(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"),IntPtr.Zero,IntPtr.Zero,2,1000,out result)!=IntPtr.Zero&&result.ToUInt64()==0x4d414357;}
 public static void Front(IntPtr h){SetForegroundWindow(h);}
}
"@
}
[void][CaptureNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
$screenWidth=[CaptureNative]::GetSystemMetrics(0);$screenHeight=[CaptureNative]::GetSystemMetrics(1)
$target=[IntPtr][Convert]::ToInt64($Hwnd.Substring(2),16)
if(-not $CaptureOnly -and [CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero){throw 'Window already has an owner; its existing lease was not modified.'}
$placement=[CaptureNative+Placement]::new();$placement.size=[Runtime.InteropServices.Marshal]::SizeOf($placement)
[void][CaptureNative]::GetWindowPlacement($target,[ref]$placement)
$originalBounds=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$originalBounds)
$foreground=[CaptureNative]::GetForegroundWindow()
$wasTopmost=([CaptureNative]::GetWindowLongPtr($target,-20).ToInt64() -band 8) -ne 0
[pscustomobject]@{hwnd=$Hwnd;placement=$placement;bounds=$originalBounds;wasTopmost=$wasTopmost;foreground=$foreground.ToInt64()} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $directory 'original-placement.json') -Encoding UTF8
$worker=$null
$leaseOwner=[IntPtr]::Zero
$result=[ordered]@{hwnd=$Hwnd;name=$Name;adapter=0;inlineKind=0;attached=$false;noLegacySurfaces=$false;clientPreserved=$false;outerPreserved=$false;restored=$false;frames=[ordered]@{}}
function Shot([string]$Label){
 $rect=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$rect)
 $region=[CaptureNative]::CreateRectRgn(0,0,0,0);$kind=[CaptureNative]::GetWindowRgn($target,$region);$box=[CaptureNative+R]::new();$corners=@();$middles=@()
 if($kind -gt 0){
  [void][CaptureNative]::GetRgnBox($region,[ref]$box)
  $points=@(@(($box.left+1),($box.top+1)),@(($box.right-2),($box.top+1)),@(($box.left+1),($box.bottom-2)),@(($box.right-2),($box.bottom-2)))
  foreach($p in $points){$corners+=[CaptureNative]::PtInRegion($region,$p[0],$p[1])}
  $cx=[int](($box.left+$box.right)/2);$cy=[int](($box.top+$box.bottom)/2)
  $points=@(@($cx,($box.top+1)),@($cx,($box.bottom-2)),@(($box.left+1),$cy),@(($box.right-2),$cy))
  foreach($p in $points){$middles+=[CaptureNative]::PtInRegion($region,$p[0],$p[1])}
 }
 [void][CaptureNative]::DeleteObject($region);$nc=0;$corner=0;[void][CaptureNative]::DwmGetWindowAttribute($target,1,[ref]$nc,4);[void][CaptureNative]::DwmGetWindowAttribute($target,33,[ref]$corner,4)
 $result.frames[$Label]=[ordered]@{regionType=$kind;regionBox=@($box.left,$box.top,$box.right,$box.bottom);cornerInside=$corners;edgeInside=$middles;ncEnabled=$nc;cornerPreference=$corner;style=[CaptureNative]::GetWindowLongPtr($target,-16).ToInt64();rect=@($rect.left,$rect.top,$rect.right,$rect.bottom)}
 $bars=@();foreach($id in @(-5,-6)){$s=[CaptureNative+Scroll]::new();$s.size=[Runtime.InteropServices.Marshal]::SizeOf($s);if([CaptureNative]::GetScrollBarInfo($target,$id,[ref]$s) -and ($s.state[0] -band 0x8000) -eq 0){$bars+=,@(($s.rect.left-$rect.left),($s.rect.top-$rect.top),($s.rect.right-$rect.left),($s.rect.bottom-$rect.top))}}
 $result.frames[$Label].scrollbars=$bars
 $visual=[CaptureNative+R]::new();[void][CaptureNative]::DwmRect($target,9,[ref]$visual,16)
 $result.frames[$Label].visibleFrame=@(($visual.left-$rect.left),($visual.top-$rect.top),($visual.right-$rect.left),($visual.bottom-$rect.top))
 $x=[Math]::Max(0,$rect.left);$y=[Math]::Max(0,$rect.top);$w=[Math]::Min($screenWidth,$rect.right)-$x;$h=[Math]::Min($screenHeight,$rect.bottom)-$y
 if($w -le 0 -or $h -le 0){throw "窗口 $Hwnd 已关闭或当前没有可截图的屏幕区域。"}
 $bitmap=[Drawing.Bitmap]::new($w,$h);$g=[Drawing.Graphics]::FromImage($bitmap);$g.CopyFromScreen($x,$y,0,0,$bitmap.Size)
 $bitmap.Save((Join-Path $directory ($Label+'.png')),[Drawing.Imaging.ImageFormat]::Png)
 if($Label -in @('after','stable','maximized','unmaximized','restored')) {
  $dpi=[CaptureNative]::GetDpiForWindow($target);$colors=@()
  if($Label -eq 'restored' -and $result.Contains('lightsOffset')){
   $left=$rect.left+$result.lightsOffset[0];$cy=$rect.top+$result.lightsOffset[1]
  }elseif($result.inlineKind -gt 0){
   $origin=[CaptureNative+P]::new();[void][CaptureNative]::ClientToScreen($target,[ref]$origin)
   $left=$origin.x+[CaptureNative]::Metric($target,7);$top=$origin.y+[CaptureNative]::Metric($target,8);$cy=[int]($top+[CaptureNative]::Metric($target,10)/2)
  }else{
   $origin=[CaptureNative+P]::new();[void][CaptureNative]::ClientToScreen($target,[ref]$origin)
   $left=$origin.x;$cy=[int]($rect.top+([CaptureNative]::Metric($target,4)+[CaptureNative]::Metric($target,5))/2)
  }
  if($Label -eq 'unmaximized'){$result.lightsOffset=@(($left-$rect.left),($cy-$rect.top))}
  $offsets=@()
  foreach($index in @(0,1,2)){
   $point=[CaptureNative+P]::new()
   if($Label -eq 'restored' -and $result.Contains('buttonOffsets')){$px=$rect.left+$result.buttonOffsets[$index][0]-$x;$py=$rect.top+$result.buttonOffsets[$index][1]-$y}
   elseif([CaptureNative]::LightCenter($target,$result.inlineKind,$index,[ref]$point)){$px=$point.x-$x;$py=$point.y-$y}
   else{$px=$left+[int]((18+$index*20)*$dpi/96)-$x;$py=$cy-$y}
   $offsets+=,@(($px+$x-$rect.left),($py+$y-$rect.top))
   if($px -ge 0 -and $px -lt $w -and $py -ge 0 -and $py -lt $h){
    # Office reports the original command's hit rectangle center. Its glyph is
    # left-aligned text, so validate actual colored pixels inside a bounded
    # neighborhood rather than assuming the glyph fills the hit center.
    $sampled=$false
    if($result.inlineKind -eq 19 -and $Label -ne 'restored'){
     $rgb=@('FF5F57','FFBD2E','28C840')[$index];$radius=[int](8*$dpi/96);$matches=0;$sumX=0;$sumY=0
     for($sy=[Math]::Max(0,$py-$radius);$sy -le [Math]::Min($h-1,$py+$radius);$sy++){for($sx=[Math]::Max(0,$px-$radius);$sx -le [Math]::Min($w-1,$px+$radius);$sx++){
      $c=$bitmap.GetPixel($sx,$sy);if(('{0:X2}{1:X2}{2:X2}' -f $c.R,$c.G,$c.B) -eq $rgb){$matches++;$sumX+=$sx;$sumY+=$sy}
     }}
     if($matches -ge 3){$colors+=$rgb;$sampled=$true;$offsets[$index]=@(([int]($sumX/$matches)+$x-$rect.left),([int]($sumY/$matches)+$y-$rect.top))}
    }
    if(-not $sampled){$color=$bitmap.GetPixel($px,$py);$colors+=('{0:X2}{1:X2}{2:X2}' -f $color.R,$color.G,$color.B)}
   }
  }
  $result.frames[$Label].buttonCenters=$offsets
  if($Label -eq 'unmaximized'){$result.buttonOffsets=$offsets}
  if($Label -eq 'after'){$result.firstFrameLights=$colors}elseif($Label -eq 'stable'){$result.pixelLights=$colors}else{$result[$Label+'Lights']=$colors}
 }
 $crop=$bitmap.Clone([Drawing.Rectangle]::new(0,0,$w,[Math]::Min(150,$h)),$bitmap.PixelFormat);$crop.Save((Join-Path $directory ($Label+'-title.png')),[Drawing.Imaging.ImageFormat]::Png)
 $crop.Dispose();$g.Dispose();$bitmap.Dispose()
}
try {
 if(-not $CaptureOnly){[void][CaptureNative]::ShowWindow($target,9)}
 $width=[Math]::Min(1230,$screenWidth-100);$height=[Math]::Min(750,$screenHeight-250)
 if(([CaptureNative]::GetWindowLongPtr($target,-16).ToInt64() -band 0x40000) -eq 0){$width=$originalBounds.right-$originalBounds.left;$height=$originalBounds.bottom-$originalBounds.top}
 if(-not $CaptureOnly){[void][CaptureNative]::SetWindowPos($target,[IntPtr]::Zero,[Math]::Min(320,[int](($screenWidth-$width)/2)),[Math]::Min(200,100+$screenHeight-$height-250),$width,$height,0x10)}
 # Foreground activation can be denied while the user is typing. Keep the
 # audited surface unobscured for capture, then restore its original z band.
 [void][CaptureNative]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13)
 [CaptureNative]::Front($target);Start-Sleep -Milliseconds 650;Shot 'before';
 $baseline=[CaptureNative+R]::new();[void][CaptureNative]::GetClientRect($target,[ref]$baseline)
 $baselineOuter=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$baselineOuter)
 $result.capabilities=[CaptureNative]::Capabilities($target)
 $originalChildren=[CaptureNative]::Children($target)
 if(-not $CaptureOnly){
 $watch=[Diagnostics.Stopwatch]::StartNew()
 $worker=Start-Process -FilePath (Join-Path $release 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$target.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $directory 'worker-error.txt')
 $deadline=[DateTime]::UtcNow.AddSeconds(5)
 do {if($worker.HasExited){break};if([CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero -and [CaptureNative]::Ready($target)){break};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $deadline)
 $result.attachAcknowledgementMs=$watch.ElapsedMilliseconds
 }
 $result.attached=([CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero -and [CaptureNative]::Ready($target))
 if($result.attached -and -not $CaptureOnly){$leaseOwner=[CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1')}
 if($worker -and $worker.HasExited){
  $result.workerExitCode=$worker.ExitCode
  # Redirected descendants can retain the writer after the worker exits. Read
  # the captured diagnostic while permitting those handles to finish normally.
  $stream=[IO.FileStream]::new((Join-Path $directory 'worker-error.txt'),[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
  $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
  try{$result.attachError=$reader.ReadToEnd()}finally{$reader.Dispose()}
 }
 $result.adapter=[CaptureNative]::GetProp($target,'Macdows.WindowStyler.Adapter.v1').ToInt64()
 $result.inlineKind=[CaptureNative]::GetProp($target,'Macdows.WindowStyler.Inline.v4').ToInt64()
 if($result.attached -and $result.inlineKind -in @(0,5)){$result.originalNativeButtonsAvailable=[CaptureNative]::NativeCaptionButtons($target)}
 if($result.attached -and $result.inlineKind -in @(1,2,3,4)){$result.originalFrameworkButtonsEnabled=[CaptureNative]::Metric($target,15)}
 $result.noLegacySurfaces=([CaptureNative]::GetProp($target,'Macdows.WindowStyler.Header.v3') -eq [IntPtr]::Zero -and [CaptureNative]::GetProp($target,'Macdows.WindowStyler.ButtonMask.v3') -eq [IntPtr]::Zero)
 [CaptureNative]::Front($target);Start-Sleep -Milliseconds 100;Shot 'after'
 Start-Sleep -Milliseconds 600;Shot 'stable'
 if($HostRefresh -and $result.attached){
  $events=@();foreach($event in @('style-reassert','theme-notification')){
   if($event -eq 'style-reassert'){[void][CaptureNative]::SetWindowLongPtr($target,-16,[IntPtr](([CaptureNative]::GetWindowLongPtr($target,-16).ToInt64() -band (-bnot 0xb0000)) -bor ($result.frames.before.style -band 0xb0000)));[void][CaptureNative]::SetWindowPos($target,[IntPtr]::Zero,0,0,0,0,0x37)}
   else{$reply=[UIntPtr]::Zero;[void][CaptureNative]::SendMessageTimeout($target,0x31a,[IntPtr]::Zero,[IntPtr]::Zero,2,2000,[ref]$reply)}
   Start-Sleep -Milliseconds 200;Shot $event
   $events+=[pscustomobject]@{event=$event;ready=[CaptureNative]::Ready($target);originalButtons=$(if($result.inlineKind -in @(0,5)){[CaptureNative]::NativeCaptionButtons($target)}else{[CaptureNative]::Metric($target,12)});originalEnabled=$(if($result.inlineKind -in @(1,2,3,4)){[CaptureNative]::Metric($target,15)}else{0});style=[CaptureNative]::GetWindowLongPtr($target,-16).ToInt64()}
  }
  $result.refreshEvents=$events;$result.refreshPassed=@($events|Where-Object {-not $_.ready -or $_.originalButtons -ne 0 -or $_.originalEnabled -ne 0}).Count -eq 0
 }
 $applied=[CaptureNative+R]::new();[void][CaptureNative]::GetClientRect($target,[ref]$applied)
 $appliedOuter=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$appliedOuter)
 $result.clientPreserved=($baseline.right -eq $applied.right -and $baseline.bottom -eq $applied.bottom)
 $result.outerPreserved=($baselineOuter.left -eq $appliedOuter.left -and $baselineOuter.top -eq $appliedOuter.top -and $baselineOuter.right -eq $appliedOuter.right -and $baselineOuter.bottom -eq $appliedOuter.bottom)
 $result.noNewChildHwnd=($originalChildren -eq [CaptureNative]::Children($target))
 if($result.inlineKind -gt 0){$result.originalButtonsVisible=[CaptureNative]::Metric($target,12);$result.tabsLeft=[CaptureNative]::Metric($target,11);$result.rowHeight=[CaptureNative]::Metric($target,10)}
 $result.baselineClient=@($baseline.right,$baseline.bottom);$result.appliedClient=@($applied.right,$applied.bottom)
 $result.baselineOuter=@($baselineOuter.left,$baselineOuter.top,$baselineOuter.right,$baselineOuter.bottom);$result.appliedOuter=@($appliedOuter.left,$appliedOuter.top,$appliedOuter.right,$appliedOuter.bottom)
 if($result.attached -and -not $CaptureOnly -and $result.capabilities[2]){
  [void][CaptureNative]::ShowWindow($target,3);Start-Sleep -Milliseconds 450;Shot 'maximized'
  $result.maximized=[CaptureNative]::IsZoomed($target)
  [void][CaptureNative]::ShowWindow($target,9);Start-Sleep -Milliseconds 450;Shot 'unmaximized'
  $result.unmaximized=-not [CaptureNative]::IsZoomed($target)
 }
 elseif($result.attached -and -not $CaptureOnly){Shot 'unmaximized';$result.maximizeSkipped='Original window has no maximize capability.'}
 # Maximize/restore can change host placement (e.g. Electron DPI rounding).
 # Compare detach against the actual placement immediately before detach.
 $rollbackClient=[CaptureNative+R]::new();[void][CaptureNative]::GetClientRect($target,[ref]$rollbackClient)
 $rollbackOuter=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$rollbackOuter)
 $expectedRollbackOuter=@(($rollbackOuter.left-($appliedOuter.left-$baselineOuter.left)),($rollbackOuter.top-($appliedOuter.top-$baselineOuter.top)),($rollbackOuter.right-($appliedOuter.right-$baselineOuter.right)),($rollbackOuter.bottom-($appliedOuter.bottom-$baselineOuter.bottom)))
 $result.preDetachClient=@($rollbackClient.right,$rollbackClient.bottom);$result.preDetachOuter=@($rollbackOuter.left,$rollbackOuter.top,$rollbackOuter.right,$rollbackOuter.bottom);$result.expectedRestoredOuter=$expectedRollbackOuter
}finally {
 # Process.Kill can race an exiting worker. Always finish window restoration;
 # if the owned worker is still alive, request detach from its exact lease.
 $cleanupFailure=$null
 try {if($worker -and -not $worker.HasExited){$worker.Kill();if(-not $worker.WaitForExit(5000)){throw 'Owned screenshot worker did not exit.'}}}
 catch {
  $worker.Refresh()
  if(-not $worker.HasExited){
   $cleanupFailure=$_.Exception.Message
   if($leaseOwner -ne [IntPtr]::Zero -and [CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -eq $leaseOwner){
    $reply=[UIntPtr]::Zero;[void][CaptureNative]::SendMessageTimeout($target,[CaptureNative]::RegisterWindowMessage('Macdows.WindowStyler.Detach.v1'),$leaseOwner,[IntPtr]::Zero,2,2000,[ref]$reply)
   }
  }
 }
 Start-Sleep -Milliseconds 1200
 $result.restored=([CaptureNative]::GetProp($target,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero -and [CaptureNative]::GetProp($target,'Macdows.WindowStyler.Inline.v4') -eq [IntPtr]::Zero)
 try {
  $restoredClient=[CaptureNative+R]::new();[void][CaptureNative]::GetClientRect($target,[ref]$restoredClient)
  $restoredOuter=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$restoredOuter)
  $result.restoredGeometry=($rollbackClient -and $expectedRollbackOuter -and $rollbackClient.right -eq $restoredClient.right -and $rollbackClient.bottom -eq $restoredClient.bottom -and $expectedRollbackOuter[0] -eq $restoredOuter.left -and $expectedRollbackOuter[1] -eq $restoredOuter.top -and $expectedRollbackOuter[2] -eq $restoredOuter.right -and $expectedRollbackOuter[3] -eq $restoredOuter.bottom)
  $result.restoredClient=@($restoredClient.right,$restoredClient.bottom);$result.restoredOuter=@($restoredOuter.left,$restoredOuter.top,$restoredOuter.right,$restoredOuter.bottom)
  if($result.restored){Shot 'restored'}
 } finally {
  [void][CaptureNative]::SetWindowPlacement($target,[ref]$placement)
  if(-not $wasTopmost){[void][CaptureNative]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)}
  Start-Sleep -Milliseconds 250
  $finalPlacement=[CaptureNative+Placement]::new();$finalPlacement.size=$placement.size;[void][CaptureNative]::GetWindowPlacement($target,[ref]$finalPlacement)
  $finalBounds=[CaptureNative+R]::new();[void][CaptureNative]::GetWindowRect($target,[ref]$finalBounds)
  $result.originalPlacementRestored=($finalPlacement.show -eq $placement.show -and $finalPlacement.normal.left -eq $placement.normal.left -and $finalPlacement.normal.top -eq $placement.normal.top -and $finalPlacement.normal.right -eq $placement.normal.right -and $finalPlacement.normal.bottom -eq $placement.normal.bottom)
  # Responsive layouts can update their minimum height after the original
  # width returns. Reapply the saved placement after that layout has settled.
  $attempt=1
  while(-not $result.originalPlacementRestored -and $attempt -lt 3){
   $attempt++;[void][CaptureNative]::SetWindowPlacement($target,[ref]$placement);Start-Sleep -Milliseconds 350
   [void][CaptureNative]::GetWindowPlacement($target,[ref]$finalPlacement)
   $result.originalPlacementRestored=($finalPlacement.show -eq $placement.show -and $finalPlacement.normal.left -eq $placement.normal.left -and $finalPlacement.normal.top -eq $placement.normal.top -and $finalPlacement.normal.right -eq $placement.normal.right -and $finalPlacement.normal.bottom -eq $placement.normal.bottom)
  }
  if(-not $result.originalPlacementRestored -and $placement.show -eq 1){
   # SetWindowPlacement enforces a framework's current tracking minimum. A
   # responsive host may now report a larger minimum than its saved size.
   # Restore the exact pre-verification screen bounds without activating it.
   [void][CaptureNative]::SetWindowPos($target,[IntPtr]::Zero,$originalBounds.left,$originalBounds.top,($originalBounds.right-$originalBounds.left),($originalBounds.bottom-$originalBounds.top),0x14)
   Start-Sleep -Milliseconds 350;[void][CaptureNative]::GetWindowPlacement($target,[ref]$finalPlacement)
   $result.originalPlacementRestored=($finalPlacement.show -eq $placement.show -and $finalPlacement.normal.left -eq $placement.normal.left -and $finalPlacement.normal.top -eq $placement.normal.top -and $finalPlacement.normal.right -eq $placement.normal.right -and $finalPlacement.normal.bottom -eq $placement.normal.bottom)
  }
  $result.placementAttempts=$attempt;$result.finalPlacement=$finalPlacement
  $result.topmostRestored=(([CaptureNative]::GetWindowLongPtr($target,-20).ToInt64() -band 8) -ne 0) -eq $wasTopmost
  $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'result.json') -Encoding UTF8
  [CaptureNative]::Front($foreground)
 }
 if($cleanupFailure){throw ('Screenshot worker cleanup failed after window restoration: '+$cleanupFailure)}
}
