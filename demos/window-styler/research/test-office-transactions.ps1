param(
    [string]$Hwnd,
    [string]$Probe = (Join-Path $PSScriptRoot '..\out\framework-probe-x64\probe\Release\OfficeTransactionHost.exe'),
    [string]$Audit = (Join-Path $PSScriptRoot '..\out\build-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\live-v17\office-contract'),
    [ValidateRange(1, 5)][int]$Repeat = 2
)
$ErrorActionPreference = 'Stop'
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
& $Audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Cannot capture initial window inventory.' }
$before = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$candidates = @($before.windows | Where-Object { $_.framework -eq 'Office NetUI' })
if ($Hwnd) {
    $value = [Convert]::ToUInt64(($Hwnd -replace '^0x', ''), 16)
    $candidates = @($candidates | Where-Object { [Convert]::ToUInt64($_.hwnd.Substring(2), 16) -eq $value })
}
if ($candidates.Count -ne 1) { throw 'Exactly one current Office NetUI HWND must be selected.' }
$target = $candidates[0]
$Hwnd = $target.hwnd
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OfficeTransactionReadback {
    public delegate bool Enumerate(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr window, Enumerate callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr window);
    public static int Children(IntPtr window) {
        int count = 0;
        EnumChildWindows(window, (child, data) => { ++count; return true; }, IntPtr.Zero);
        return count;
    }
}
'@
$window = [IntPtr][Convert]::ToInt64($Hwnd.Substring(2), 16)
$children = [OfficeTransactionReadback]::Children($window)
$checks = [Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Passed) {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Passed })
}
$python = (Get-Command python -ErrorAction Stop).Source
$treeReader = Join-Path $PSScriptRoot 'office-tree.py'
& $python $treeReader --hwnd $Hwnd --out (Join-Path $OutputDirectory 'tree-before.json')
if ($LASTEXITCODE -ne 0) { throw 'Initial Office tree read failed.' }
$treeBefore = Get-Content -Raw -LiteralPath (Join-Path $OutputDirectory 'tree-before.json') | ConvertFrom-Json
Check 'initial Node tree is complete and structurally valid' $treeBefore.complete
for ($iteration = 1; $iteration -le $Repeat; ++$iteration) {
    & $Probe $Hwnd | Out-File -LiteralPath (Join-Path $OutputDirectory "host-$iteration.log") -Encoding UTF8
    $probeExit = $LASTEXITCODE
    Check "transaction host $iteration exits successfully" ($probeExit -eq 0)
    $source = Join-Path (Split-Path $Probe -Parent) "office-transactions-$($target.pid).json"
    if ($probeExit -ne 0 -or -not (Test-Path -LiteralPath $source)) { break }
    Copy-Item -LiteralPath $source -Destination (Join-Path $OutputDirectory "transaction-$iteration.json")
    $report = Get-Content -Raw -LiteralPath $source | ConvertFrom-Json
    Check "transaction $iteration passes all 49 internal checks" ($report.checks -eq 49 -and $report.failures -eq 0)
    Check "transaction $iteration writes and restores twelve original properties" ($report.propertiesWritten -eq 12 -and $report.localValuesRestored)
    Check "transaction $iteration publishes no persistent session" (-not $report.sessionPublished)
}
& $python $treeReader --hwnd $Hwnd --out (Join-Path $OutputDirectory 'tree-after.json')
Check 'final tree reader succeeds' ($LASTEXITCODE -eq 0)
$treeAfter = Get-Content -Raw -LiteralPath (Join-Path $OutputDirectory 'tree-after.json') | ConvertFrom-Json
Check 'final Node tree is complete and structurally valid' $treeAfter.complete
Check 'original Node objects, parent links, child order and helper geometry are unchanged' `
    (($treeBefore.nodes | ConvertTo-Json -Depth 8 -Compress) -ceq ($treeAfter.nodes | ConvertTo-Json -Depth 8 -Compress))
& $Audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
Check 'final window inventory succeeds' ($LASTEXITCODE -eq 0)
$after = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$current = @($after.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.pid -eq $target.pid })
Check 'original application and HWND remain alive' ($current.Count -eq 1)
if ($current.Count -eq 1) {
    foreach ($field in @('style', 'exStyle', 'dpi', 'rect', 'client', 'clientOrigin', 'minimized', 'maximized', 'normalRect')) {
        Check "original $field is preserved" `
            (($target.geometry.$field | ConvertTo-Json -Compress) -ceq ($current[0].geometry.$field | ConvertTo-Json -Compress))
    }
    Check 'no owner session remains attached' ($current[0].owner -eq '0x0')
}
Check 'original child HWND count is unchanged' ([OfficeTransactionReadback]::Children($window) -eq $children)
Check 'application is responsive after rollback' (-not [OfficeTransactionReadback]::IsHungAppWindow($window))
$failures = @($checks | Where-Object { -not $_.passed }).Count
$result = [pscustomobject]@{
    hwnd = $Hwnd; pid = $target.pid; iterations = $Repeat
    checks = $checks.Count; failures = $failures; results = $checks
    layoutMoved = $false; renderVerified = $false
}
$result | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
"checks=$($checks.Count); failures=$failures"
if ($failures) { exit 1 }
