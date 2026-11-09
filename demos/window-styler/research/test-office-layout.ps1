param(
    [string]$Hwnd,
    [string]$Probe = (Join-Path $PSScriptRoot '..\out\framework-probe-x64\probe\Release\OfficeLayoutHost.exe'),
    [string]$Audit = (Join-Path $PSScriptRoot '..\out\build-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\live-v18\office-layout'),
    [ValidateRange(1, 3)][int]$Repeat = 2,
    [switch]$Lease
)
$ErrorActionPreference = 'Stop'
if ($Lease) {
    if (-not $PSBoundParameters.ContainsKey('Probe')) {
        $Probe = Join-Path $PSScriptRoot '..\out\framework-probe-x64\probe\Release\OfficeLeaseHost.exe'
    }
    if (-not $PSBoundParameters.ContainsKey('OutputDirectory')) {
        $OutputDirectory = Join-Path $PSScriptRoot '..\out\live-v21\office-lease'
    }
}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
& $Audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Cannot read initial inventory.' }
$before = Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$targets = @($before.windows | Where-Object framework -eq 'Office NetUI')
if ($Hwnd) { $targets = @($targets | Where-Object hwnd -eq $Hwnd) }
if ($targets.Count -ne 1) { throw 'Select exactly one Office NetUI HWND.' }
$target = $targets[0]
$Hwnd = $target.hwnd
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OfficeLayoutReadback {
    public delegate bool Enum(IntPtr w, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w, Enum callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr w);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr w, string name);
    public static int Children(IntPtr w) { int n=0; EnumChildWindows(w,(child,data)=>{++n;return true;},IntPtr.Zero);return n; }
}
'@
$window = [IntPtr][Convert]::ToInt64($Hwnd.Substring(2), 16)
$originalChildren = [OfficeLayoutReadback]::Children($window)
$checks = [Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Passed) { $checks.Add([pscustomobject]@{ name=$Name; passed=$Passed }) }
$tree = Join-Path $PSScriptRoot 'office-tree.py'
& python $tree --hwnd $Hwnd --out (Join-Path $OutputDirectory 'tree-before.json')
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the original Node tree.' }
$treeBefore = Get-Content -Raw (Join-Path $OutputDirectory 'tree-before.json') | ConvertFrom-Json
Check 'original Node tree is structurally complete' $treeBefore.complete
for ($run=1; $run -le $Repeat; ++$run) {
    & $Probe $Hwnd | Out-File (Join-Path $OutputDirectory "host-$run.log") -Encoding UTF8
    $code = $LASTEXITCODE
    Check "layout host $run succeeds" ($code -eq 0)
    $prefix = if ($Lease) { "office-lease-$($target.pid)" } else { "office-layout-$($target.pid)" }
    $folder = Split-Path $Probe -Parent
    $reportPath = Join-Path $folder ($prefix+'.json')
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $reportPath)) { break }
    Copy-Item -LiteralPath $reportPath -Destination (Join-Path $OutputDirectory "transaction-$run.json")
    foreach ($stage in $(if ($Lease) { @('paint-0','paint-1','paint-2','paint-3') } else { @('before','painted','maximized','restored') })) {
        $source = Join-Path $folder "$prefix-$stage.bmp"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $OutputDirectory "$run-$stage.bmp") }
    }
    $report = Get-Content -Raw $reportPath | ConvertFrom-Json
    if ($Lease) {
        $hostResult = Get-Content (Join-Path $OutputDirectory "host-$run.log") | Select-Object -Last 1 | ConvertFrom-Json
        Check "run $run passes all persistent native checks" ($hostResult.failures -eq 0 -and $hostResult.checks -ge 30)
        Check "run $run finishes after controller crash recovery" (-not $report.active -and $report.reason -eq 'owner-lost')
        Check "run $run restores every retained property without skips" ($report.cleanupFailures -eq 0 -and $report.skippedDestroyedOrHostChanged -eq 0 -and $report.savedProperties -in @(15,16))
    } else {
        Check "run $run passes all native checks" ($report.failures -eq 0 -and $report.checks -ge 120)
        Check "run $run exercises native maximize restore and minimize" ($report.maximizeInput -and $report.restoreInput -and $report.minimizeInput)
        Check "run $run restores its original Node and style state" ($report.restored -and -not $report.persistentSession)
    }
}
& python $tree --hwnd $Hwnd --out (Join-Path $OutputDirectory 'tree-after.json')
Check 'final tree read succeeds' ($LASTEXITCODE -eq 0)
$treeAfter = Get-Content -Raw (Join-Path $OutputDirectory 'tree-after.json') | ConvertFrom-Json
Check 'final Node tree is structurally complete' $treeAfter.complete
Check 'original Node identities parents order and helper geometry are unchanged' `
    (($treeBefore.nodes | ConvertTo-Json -Depth 8 -Compress) -ceq ($treeAfter.nodes | ConvertTo-Json -Depth 8 -Compress))
& $Audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
Check 'final inventory succeeds' ($LASTEXITCODE -eq 0)
$after = Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$current = @($after.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.pid -eq $target.pid })
Check 'original Office process and HWND remain alive' ($current.Count -eq 1)
if ($current.Count -eq 1) {
    foreach ($field in @('style','exStyle','dpi','rect','client','clientOrigin','minimized','maximized','normalRect','dwmNcEnabled','corner')) {
        Check "original $field is restored" `
            (($target.geometry.$field | ConvertTo-Json -Compress) -ceq ($current[0].geometry.$field | ConvertTo-Json -Compress))
    }
    Check 'no Owner session remains' ($current[0].owner -eq '0x0')
}
foreach ($name in @('Macdows.WindowStyler.NetUI.SystemMenu.v1','Macdows.WindowStyler.NetUI.CaptionBoxes.v1',
    'Macdows.WindowStyler.NetUI.PreviousCaptionBoxes.v1','Macdows.WindowStyler.NetUI.RestoreCaptionBoxes.v1',
    'Macdows.WindowStyler.NativeMenu.Previous','Macdows.WindowStyler.NativeMenu.Internal',
    'Macdows.WindowStyler.NetUI.ButtonWidth','Macdows.WindowStyler.NetUI.ButtonTop',
    'Macdows.WindowStyler.NetUI.ButtonRight','Macdows.WindowStyler.NetUI.ButtonHeight')) {
    Check "temporary property removed: $name" ([OfficeLayoutReadback]::GetProp($window,$name) -eq [IntPtr]::Zero)
}
Check 'original child HWND count is restored' ([OfficeLayoutReadback]::Children($window) -eq $originalChildren)
Check 'Office is responsive' (-not [OfficeLayoutReadback]::IsHungAppWindow($window))
$failures = @($checks | Where-Object { -not $_.passed }).Count
[pscustomobject]@{ hwnd=$Hwnd; pid=$target.pid; checks=$checks.Count; failures=$failures; results=$checks } |
    ConvertTo-Json -Depth 8 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
"checks=$($checks.Count); failures=$failures"
if ($failures) { exit 1 }
