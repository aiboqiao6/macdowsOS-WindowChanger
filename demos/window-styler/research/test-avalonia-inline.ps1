param(
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$FixtureExecutable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$DotNetExecutable = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class AvaloniaTestNative {
 [StructLayout(LayoutKind.Sequential)] public struct R {public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)] public struct P {public int x,y;}
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out P p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Send(IntPtr h,uint m,long w=0,long l=0){UIntPtr r; if(SendMessageTimeout(h,m,(IntPtr)w,(IntPtr)l,2,3000,out r)==IntPtr.Zero)throw new Exception("Window message timed out");return unchecked((long)r.ToUInt64());}
 public static long Metric(IntPtr h,int n,int b=0){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1"),n,b);}
 public static bool Ready(IntPtr h){return Send(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"))==0x4d414357;}
}
'@
[void][AvaloniaTestNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
$foreground=[AvaloniaTestNative]::GetForegroundWindow()
$cursor=[AvaloniaTestNative+P]::new();[void][AvaloniaTestNative]::GetCursorPos([ref]$cursor)
$results=[Collections.Generic.List[object]]::new()
function Until([string]$Name,[scriptblock]$Test) {
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do { try {if (& $Test) {$script:checks.Add($Name);return}} catch {} ; Start-Sleep -Milliseconds 100 } while([DateTime]::UtcNow -lt $deadline)
    throw "Assertion failed: $Name"
}
function Status {Get-Content -LiteralPath (Join-Path $out "status.json") -Raw | ConvertFrom-Json}
function Command($Data) {$script:sequence++;$Data.id=$script:sequence;$Data|ConvertTo-Json -Compress|Set-Content -LiteralPath (Join-Path $out "command.json") -Encoding UTF8}
function ClickPoint([int]$X,[int]$Y) {
    [void][AvaloniaTestNative]::SetForegroundWindow($h);Start-Sleep -Milliseconds 150
    [void][AvaloniaTestNative]::SetCursorPos($X,$Y);[AvaloniaTestNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 70;[AvaloniaTestNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}
function ClickLight([int]$Index) {ClickPoint ([AvaloniaTestNative]::Metric($h,16,$Index)) ([AvaloniaTestNative]::Metric($h,17,$Index))}
function Attach {
    $script:worker=Start-Process -FilePath (Join-Path $RuntimeDirectory "StylerTests.exe") -ArgumentList @("--inline-lease-worker",$h.ToInt64()) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $out ("worker-"+$sequence+".log"))
    Until "original-tree attachment acknowledged" {[AvaloniaTestNative]::Ready($h)}
}
function StopWorker {if($script:worker -and -not $script:worker.HasExited){$script:worker.Kill();$script:worker.WaitForExit()};$script:worker=$null}
function Capture([string]$Name) {
    [void][AvaloniaTestNative]::SetForegroundWindow($h);Start-Sleep -Milliseconds 400
    $r=[AvaloniaTestNative+R]::new();[void][AvaloniaTestNative]::GetWindowRect($h,[ref]$r)
    $bitmap=[Drawing.Bitmap]::new(($r.right-$r.left),($r.bottom-$r.top));$graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {$graphics.CopyFromScreen($r.left,$r.top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $out ($Name+".png")),[Drawing.Imaging.ImageFormat]::Png)} finally {$graphics.Dispose();$bitmap.Dispose()}
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
try { foreach($theme in @("dark","light")) {
    $out=Join-Path $OutputDirectory $theme;New-Item -ItemType Directory -Path $out -Force|Out-Null
    $sequence=0;$checks=[Collections.Generic.List[string]]::new();$fixture=$null;$worker=$null
    try {
        $fixtureArguments=@(('--fixture-output="'+$out+'"'),("--fixture-"+$theme))
        $fixtureProgram=$FixtureExecutable
        if($DotNetExecutable){$fixtureProgram=$DotNetExecutable;$fixtureArguments=@(('"'+$FixtureExecutable+'"'))+$fixtureArguments}
        $fixture=Start-Process -FilePath $fixtureProgram -ArgumentList $fixtureArguments -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $out "fixture-error.log")
        Until "fixture rendered" {Test-Path (Join-Path $out "ready.json")}
        $ready=Get-Content (Join-Path $out "ready.json") -Raw|ConvertFrom-Json;$h=[IntPtr]([long]$ready.hwnd)
        Until "fixture status available" {(Status).state -eq "Normal"}
        $original=Status;Capture "before";Attach
        Until "old buttons hidden and disabled" {$s=Status;-not $s.originalVisible -and -not $s.originalEnabled -and -not $s.originalHitTest -and -not $s.originalFocusable -and @($s.originalButtonHitTest|Where-Object {$_}).Count -eq 0}
        Until "all lights enabled and lane inset" {$s=Status;$s.enabled.Count -eq 3 -and @($s.enabled|Where-Object {-not $_}).Count -eq 0 -and $s.laneLeft -eq 84}
        Until "adapter and input diagnostics match" {[AvaloniaTestNative]::Metric($h,6) -eq 280 -and [AvaloniaTestNative]::Metric($h,13) -eq 21 -and [AvaloniaTestNative]::Metric($h,12) -eq 0 -and [AvaloniaTestNative]::Metric($h,15) -eq 0}
        Capture "attached"
        foreach($hit in @(8,9,20)){[void][AvaloniaTestNative]::Send($h,0xa1,$hit);[void][AvaloniaTestNative]::Send($h,0xa2,$hit)}
        foreach($center in $original.originalCenters){ClickPoint $center.point.X $center.point.Y}
        Start-Sleep -Milliseconds 300
        Until "old native messages and old button positions inactive" {$s=Status;$s.state -eq "Normal" -and $s.closeRequests -eq 0}
        ClickLight 2;Until "native mouse maximizes" {[AvaloniaTestNative]::IsZoomed($h)}
        ClickLight 2;Until "native mouse restores" {-not [AvaloniaTestNative]::IsZoomed($h)}
        ClickLight 1;Until "native mouse minimizes" {[AvaloniaTestNative]::IsIconic($h)}
        Command @{action="restore"};Until "fixture restores after minimize" {-not [AvaloniaTestNative]::IsIconic($h)}
        ClickLight 0;Until "close follows original cancelable Closing event" {(Status).closeRequests -eq 1}
        Command @{action="capabilities";minimize=$false;maximize=$false}
        Until "capabilities disable lights" {$s=Status;$s.enabled.Count -eq 3 -and $s.enabled[0] -and -not $s.enabled[1] -and -not $s.enabled[2]}
        ClickLight 2;ClickLight 1
        Until "disabled mouse input rejected" {(Status).state -eq "Normal"}
        Command @{action="theme";value=$(if($theme -eq "dark"){"light"}else{"dark"})}
        $expectedGray=if($theme -eq "dark"){"#ffd0d0cf"}else{"#ff808080"}
        Until "disabled colors follow opposite theme" {$s=Status;$s.lightColors.Count -eq 3 -and $s.lightColors[1].ToLower() -eq $expectedGray -and $s.lightColors[2].ToLower() -eq $expectedGray}
        Capture "opposite-theme-disabled"
        Command @{action="capabilities";minimize=$true;maximize=$true}
        Until "capabilities restored" {@((Status).enabled|Where-Object {-not $_}).Count -eq 0}
        Command @{action="refresh"}
        Until "host updates cannot expose old controls" {$s=Status;$s.lastCommand -eq $sequence -and -not $s.originalVisible -and -not $s.originalEnabled -and -not $s.originalHitTest -and $s.laneLeft -eq 84}
        StopWorker
        Until "crash removes ownership" {[AvaloniaTestNative]::GetProp($h,"Macdows.WindowStyler.Owner.v1") -eq [IntPtr]::Zero}
        Until "crash restores latest host properties and content" {$s=Status;$s.originalVisible -and $s.originalEnabled -and $s.originalHitTest -and $s.laneLeft -eq 12 -and $s.enabled.Count -eq 0 -and $s.Text -eq "Keep this original text"}
        Capture "crash-restored"
        Attach
        Until "second attachment has settled" {$s=Status;$s.laneLeft -eq 96 -and -not $s.originalVisible -and $s.enabled.Count -eq 3}
        Command @{action="rebuild"}
        Until "tree replacement safely detaches" {[AvaloniaTestNative]::GetProp($h,"Macdows.WindowStyler.Owner.v1") -eq [IntPtr]::Zero}
        Until "replacement tree stays complete" {$s=Status;$s.rebuilt -eq 1 -and $s.laneLeft -eq 0 -and $s.originalVisible -and $s.enabled.Count -eq 0 -and $s.Text -eq "Keep this original text"}
        StopWorker;Attach
        [void][AvaloniaTestNative]::Send($h,[AvaloniaTestNative]::RegisterWindowMessage("Macdows.WindowStyler.Detach.v1"))
        Until "explicit detach restores original controls" {$s=Status;$s.originalVisible -and $s.originalEnabled -and $s.originalHitTest -and $s.laneLeft -eq 0 -and $s.enabled.Count -eq 0}
        $results.Add(@{theme=$theme;checks=$checks.ToArray();avalonia=$ready.avalonia;fluent=$ready.fluent;passed=$true})
    } finally {
        $checks|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out "checks.json") -Encoding UTF8
        @{fixturePid=$fixture.Id;fixtureExited=$fixture.HasExited;fixtureExitCode=$(if($fixture.HasExited){$fixture.ExitCode}else{$null});workerPid=$worker.Id}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out "process-state.json") -Encoding UTF8
        StopWorker;if($fixture -and -not $fixture.HasExited){Command @{action="close"};if(-not $fixture.WaitForExit(5000)){$fixture.Kill();$fixture.WaitForExit()}}
    }
    $results|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory "progress.json") -Encoding UTF8
 };$results|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory "done.json") -Encoding UTF8
} catch {$_|Out-String|Set-Content -LiteralPath (Join-Path $OutputDirectory "error.txt") -Encoding UTF8;throw}
finally {[void][AvaloniaTestNative]::SetCursorPos($cursor.x,$cursor.y);[void][AvaloniaTestNative]::SetForegroundWindow($foreground)}
