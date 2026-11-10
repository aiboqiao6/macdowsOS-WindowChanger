param(
    [string]$FixtureDirectory = (Join-Path $PSScriptRoot '../../out/framework-probe-x64/winui-native-fixture/Release'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../out/live-v36/native-island-Release'),
    [switch]$PreserveRoot,
    [switch]$TitleIsland
)
$ErrorActionPreference = 'Stop'
$fixturePath = Join-Path (Resolve-Path -LiteralPath $FixtureDirectory).Path 'WinUI3NativeIslandFixture.exe'
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandContract {
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 [StructLayout(LayoutKind.Sequential)] public struct R {public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)] public struct P {public int x,y;}
 [StructLayout(LayoutKind.Sequential)] public struct T {public uint size;public R rect;public uint s0,s1,s2,s3,s4,s5;public R r0,r1,r2,r3,r4,r5;}
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref P p);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(P p);
 [DllImport("user32.dll")] public static extern bool IsChild(IntPtr parent,IntPtr h);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r;if(SendMessageTimeout(h,m,(IntPtr)w,(IntPtr)l,2,2000,out r)==IntPtr.Zero)throw new Exception("Fixture message timeout");return unchecked((long)r.ToUInt64());}
 public static IntPtr Find(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&GetProp(h,"Macdows.Research.NativeIsland.Owned")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static void Click(IntPtr h,int i){var p=new P{x=(int)((18+20*i)*GetDpiForWindow(h)/96),y=(int)Send(h,0x804a,2)/2};ClientToScreen(h,ref p);SetForegroundWindow(h);SetCursorPos(p.x,p.y);System.Threading.Thread.Sleep(100);var target=WindowFromPoint(p);if(target!=h&&!IsChild(h,target))throw new Exception("Owned title click was occluded");mouse_event(2,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(50);mouse_event(4,0,0,0,UIntPtr.Zero);}
 public static int[] Body(IntPtr h){return new[]{(int)Send(h,0x804a,4),(int)Send(h,0x804a,3),(int)Send(h,0x804a,5),(int)Send(h,0x804a,6)};}
 public static bool HiddenDisabled(IntPtr h){var t=new T();t.size=(uint)Marshal.SizeOf(t);var p=Marshal.AllocHGlobal((int)t.size);try{Marshal.StructureToPtr(t,p,false);Send(h,0x33f,0,p.ToInt64());t=(T)Marshal.PtrToStructure(p,typeof(T));foreach(uint s in new[]{t.s2,t.s3,t.s5})if((s&0x18001)!=0x18001)return false;foreach(R r in new[]{t.r2,t.r3,t.r5})if(r.r>r.l&&r.b>r.t)return false;return true;}finally{Marshal.FreeHGlobal(p);}}
}
'@
[void][NativeIslandContract]::SetProcessDpiAwarenessContext([IntPtr](-4))
$checks = [Collections.Generic.List[object]]::new()
function Check($value, [string]$name) {
    $checks.Add([pscustomobject]@{name=$name;passed=[bool]$value})
    $checks | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'checks.json') -Encoding utf8
    if (-not $value) { throw "Check failed: $name" }
}
function Until([scriptblock]$test) {
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do { if (&$test) { return $true }; Start-Sleep -Milliseconds 80 } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Send($handle, [long]$operation) {
    $r = [NativeIslandContract]::Send($handle, 0x8049, $operation)
    if ($r -ne 0) { throw ('Fixture command '+$operation+' failed: '+$r) }
    Start-Sleep -Milliseconds 500
}
function Snapshot($handle, [string]$name) {
    [void][NativeIslandContract]::SetForegroundWindow($handle)
    # IsZoomed becomes true before DWM finishes its maximize animation. Wait
    # for composition before comparing edge pixels with a native baseline.
    Start-Sleep -Milliseconds 1000
    $outer = [NativeIslandContract+R]::new()
    [void][NativeIslandContract]::GetWindowRect($handle, [ref]$outer)
    $body = [NativeIslandContract]::Body($handle)
    foreach ($part in @('body','window')) {
        if ($part -eq 'body') { $r=$body } else { $r=@($outer.l,$outer.t,($outer.r-$outer.l),($outer.b-$outer.t)) }
        $bitmap = [Drawing.Bitmap]::new($r[2],$r[3])
        $draw = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $draw.CopyFromScreen($r[0],$r[1],0,0,$bitmap.Size)
            $bitmap.Save((Join-Path $OutputDirectory ($name+'-'+$part+'.png')),[Drawing.Imaging.ImageFormat]::Png)
            if ($part -eq 'window') { $titlePixel=$bitmap.GetPixel(300,30).ToArgb() -band 0xFFFFFF }
            if ($part -eq 'window') {
                $captionRect=[Drawing.Rectangle]::new(16,6,($bitmap.Width-32),30)
                $caption=$bitmap.Clone($captionRect,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
                try { $caption.Save((Join-Path $OutputDirectory ($name+'-caption.png')),[Drawing.Imaging.ImageFormat]::Png) }
                finally { $caption.Dispose() }
            }
        } finally { $draw.Dispose();$bitmap.Dispose() }
    }
    return [pscustomobject]@{
        body=$body;outer=@($outer.l,$outer.t,$outer.r,$outer.b)
        hash=(Get-FileHash -LiteralPath (Join-Path $OutputDirectory ($name+'-body.png'))).Hash
        theme=[NativeIslandContract]::Send($handle,0x804a,7)
        titleTheme=[NativeIslandContract]::Send($handle,0x804a,12)
        titlePixel=$titlePixel
        captionHash=(Get-FileHash -LiteralPath (Join-Path $OutputDirectory ($name+'-caption.png'))).Hash
    }
}
function LightPixels([string]$name) {
    $bitmap=[Drawing.Bitmap]::new((Join-Path $OutputDirectory ($name+'-window.png')))
    $counts=@(0,0,0);$colors=@(0xFF5F57,0xFFBD2E,0x28C840)
    try {
        for($y=0;$y -lt 100;$y++){for($x=0;$x -lt 160;$x++){
            $rgb=$bitmap.GetPixel($x,$y).ToArgb() -band 0xFFFFFF
            for($i=0;$i -lt 3;$i++){if($rgb -eq $colors[$i]){$counts[$i]++}}
        }}
    } finally { $bitmap.Dispose() }
    return $counts
}
$foreground=[NativeIslandContract]::GetForegroundWindow()
$cursor=[NativeIslandContract+P]::new()
[void][NativeIslandContract]::GetCursorPos([ref]$cursor)
$fixtures=[Collections.Generic.List[object]]::new()
try {
    foreach ($theme in @('dark','light')) {
        $process=$null
        try {
            $arguments='--'+$theme+$(if($PreserveRoot){' --preserve-root'}else{''})+$(if($TitleIsland){' --title-island'}else{''})
            $process=Start-Process -FilePath $fixturePath -ArgumentList $arguments -WindowStyle Hidden -PassThru
            Check (Until {[NativeIslandContract]::Find($process.Id) -ne [IntPtr]::Zero}) "$theme owned fixture starts"
            $h=[NativeIslandContract]::Find($process.Id)
            Check ([NativeIslandContract]::GetProp($h,'Macdows.Research.NativeIsland.Owned') -ne [IntPtr]::Zero) "$theme owned marker"
            [void][NativeIslandContract]::SetWindowPos($h,[IntPtr](-1),160,160,1200,720,0x40)
            Start-Sleep -Milliseconds 700
            $before=Snapshot $h ($theme+'-before')
            $stable=Snapshot $h ($theme+'-stable')
            Check ($before.hash -eq $stable.hash) "$theme original body pixels stable"
            Send $h 1
            $after=Snapshot $h ($theme+'-attached')
            [ordered]@{before=$before;attached=$after;delta=[NativeIslandContract]::Send($h,0x804a,2)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory ($theme+'-geometry.json')) -Encoding utf8
            Check ([NativeIslandContract]::Send($h,0x804a,1) -eq 1) "$theme frame attached"
            Check (($before.outer -join ',') -eq ($after.outer -join ',')) "$theme outer geometry preserved"
            Check (($before.body -join ',') -eq ($after.body -join ',')) "$theme original body screen geometry preserved"
            Check ($before.hash -eq $after.hash) "$theme original body pixels preserved"
            Check ([NativeIslandContract]::HiddenDisabled($h)) "$theme old caption buttons invisible unavailable offscreen and empty"
            Check ([NativeIslandContract]::Send($h,0x804a,10) -eq 0) "$theme presenter caption removed"
            Check (@(LightPixels ($theme+'-attached') | Where-Object {$_ -lt 10}).Count -eq 0) "$theme all three lights rendered"
            Check ($after.titleTheme -eq $after.theme) "$theme title follows the original subtree actual theme"
            Check ($after.titlePixel -eq $(if($after.theme -eq 1){0xF3F3F3}else{0x202020})) "$theme title background matches original subtree"
            foreach ($hit in @(8,9,20)) {
                [void][NativeIslandContract]::Send($h,0xa1,$hit)
                [void][NativeIslandContract]::Send($h,0xa2,$hit)
                [void][NativeIslandContract]::Send($h,0xa3,$hit)
            }
            Check ([NativeIslandContract]::IsWindow($h) -and -not [NativeIslandContract]::IsIconic($h) -and -not [NativeIslandContract]::IsZoomed($h)) "$theme old caption input cannot close minimize or maximize"
            Send $h 2
            $firstRestored=Snapshot $h ($theme+'-first-restored')
            Check ($before.captionHash -eq $firstRestored.captionHash -and $before.hash -eq $firstRestored.hash) "$theme original caption glyphs colors and body pixels restored"
            Send $h 1
            [void][NativeIslandContract]::SetWindowPos($h,[IntPtr]::Zero,0,0,1500,840,0x16)
            Start-Sleep -Milliseconds 500
            $resized=Snapshot $h ($theme+'-resized-attached')
            Send $h 2
            $nativeResized=Snapshot $h ($theme+'-resized-native')
            Check (($resized.body -join ',') -eq ($nativeResized.body -join ',') -and $resized.hash -eq $nativeResized.hash) "$theme resize preserves native body layout and pixels"
            Check ([NativeIslandContract]::Send($h,0x804a,10) -eq 1) "$theme presenter restored"
            Send $h 1
            Send $h $(if($theme -eq 'dark'){3}else{4})
            $switched=Snapshot $h ($theme+'-theme-switched')
            Check ($switched.theme -ne $after.theme) "$theme original subtree switches actual theme"
            Check ($switched.titleTheme -eq $switched.theme) "$theme title follows actual theme changes without host notifications"
            Check ($switched.titlePixel -eq $(if($switched.theme -eq 1){0xF3F3F3}else{0x202020})) "$theme title follows original background changes"
            Check (@(LightPixels ($theme+'-theme-switched') | Where-Object {$_ -lt 10}).Count -eq 0) "$theme switched title retains three lights"
            Send $h 2
            $nativeSwitched=Snapshot $h ($theme+'-theme-native')
            Check ($switched.hash -eq $nativeSwitched.hash -and ($switched.body -join ',') -eq ($nativeSwitched.body -join ',')) "$theme theme switch preserves native body layout and pixels"
            Send $h 1
            [void][NativeIslandContract]::Send($h,0x112,0xf030)
            Check (Until {[NativeIslandContract]::IsZoomed($h)}) "$theme original maximize command works"
            $maximized=Snapshot $h ($theme+'-maximized-attached')
            Send $h 2
            $nativeMaximized=Snapshot $h ($theme+'-maximized-native')
            [ordered]@{attached=$maximized;native=$nativeMaximized} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory ($theme+'-maximized.json')) -Encoding utf8
            Check (($maximized.body -join ',') -eq ($nativeMaximized.body -join ',') -and $maximized.hash -eq $nativeMaximized.hash) "$theme maximized body matches native screen geometry and pixels"
            [void][NativeIslandContract]::Send($h,0x112,0xf120)
            Send $h 1
            Send $h 2
            Check ([NativeIslandContract]::Send($h,0x804a,1) -eq 0 -and [NativeIslandContract]::Send($h,0x804a,9) -eq 1) "$theme original root restored after repeated transitions"
            Send $h 1
            [NativeIslandContract]::Click($h,2)
            Check (Until {[NativeIslandContract]::IsZoomed($h)}) "$theme new green button click maximizes"
            [NativeIslandContract]::Click($h,2)
            Check (Until {-not [NativeIslandContract]::IsZoomed($h)}) "$theme new green button click restores"
            [NativeIslandContract]::Click($h,1)
            Check (Until {[NativeIslandContract]::IsIconic($h)}) "$theme new yellow button click minimizes"
            [void][NativeIslandContract]::Send($h,0x112,0xf120)
            Start-Sleep -Milliseconds 500
            [NativeIslandContract]::Click($h,0)
            Check (Until {-not [NativeIslandContract]::IsWindow($h)}) "$theme new red button closes only owned fixture"
            Check ($process.WaitForExit(5000)) "$theme owned fixture exits"
            Check ($process.ExitCode -eq 0) "$theme owned fixture exits successfully"
            $fixtures.Add([pscustomobject]@{theme=$theme;pid=$process.Id;passed=$true})
        } finally {
            if($process -and -not $process.HasExited){
                $owned=[NativeIslandContract]::Find($process.Id)
                if($owned -ne [IntPtr]::Zero){[void][NativeIslandContract]::Send($owned,0x10)}
                [void]$process.WaitForExit(5000)
            }
            if($process){$process.Dispose()}
        }
    }
    [ordered]@{passed=$true;checks=$checks.Count;fixtures=$fixtures;fixtureHash=(Get-FileHash -LiteralPath $fixturePath).Hash;scope='owned native full-client WinUI islands only; no installed applications or production adapter'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
} catch {
    ($_|Out-String)+$_.ScriptStackTrace | Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt') -Encoding utf8
    throw
} finally {
    [void][NativeIslandContract]::SetCursorPos($cursor.x,$cursor.y)
    [void][NativeIslandContract]::SetForegroundWindow($foreground)
}
