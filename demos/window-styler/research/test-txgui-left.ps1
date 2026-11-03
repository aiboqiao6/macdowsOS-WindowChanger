param(
    [Parameter(Mandatory=$true)][string]$Hwnd,
    [switch]$Lease,
    [string]$ProbeDirectory=(Join-Path $PSScriptRoot '..\out\framework-probe-Win32\probe\Release'),
    [string]$Audit=(Join-Path $PSScriptRoot '..\out\vs-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\live-v30\left'),
    [ValidateRange(1,3)][int]$Repeat=1
)
$ErrorActionPreference='Stop'
[void](New-Item -ItemType Directory -Force $OutputDirectory)
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed) { $checks.Add([pscustomobject]@{name=$Name;passed=$Passed}) }
& $Audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Initial inventory failed.' }
$before=Get-Content -Raw (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$original=@($before.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.framework -eq 'TXGuiFoundation' })
if ($original.Count -ne 1) { throw 'Select exactly one original TXGui HWND.' }
$original=$original[0]
$reader=Join-Path $ProbeDirectory 'FrameworkResearchHost.exe'
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $reader -Active -OutputDirectory (Join-Path $OutputDirectory 'read-before')
if ($LASTEXITCODE -ne 0) { throw 'Complete initial read failed; do not start a transaction.' }
$treeBefore=Get-Content -Raw (Join-Path $OutputDirectory 'read-before/tree.json') | ConvertFrom-Json
$plan=& (Join-Path $PSScriptRoot 'txgui-left-plan.ps1') -TreePath (Join-Path $OutputDirectory 'read-before/tree.json') | ConvertFrom-Json
Check 'complete semantic layout proposal fits original left header and content' ($plan.candidateFits -and $plan.inputTreeComplete)
$affected=[Collections.Generic.HashSet[long]]::new()
$titleId=[long]$plan.existingTitleFrame
$headerId=[long]$plan.originalHeader
function Affected($Node,[bool]$Inside) {
    $Inside=$Inside -or $Node.identity -eq $titleId -or $Node.identity -eq $headerId
    if ($Inside) { [void]$affected.Add([long]$Node.identity) }
    foreach ($child in $Node.children) { Affected $child $Inside }
}
Affected $treeBefore.tree $false
function Project($Document) {
    $result=[Collections.Generic.List[object]]::new()
    function Visit($Node,[long]$Parent) {
        $fixed=$affected.Contains([long]$Node.identity)
        $result.Add([pscustomobject]@{identity=$Node.identity;parent=$Parent;type=$Node.frameworkType;
            children=@($Node.children | ForEach-Object identity);
            hidden=$(if ($fixed) { $Node.hidden });visible=$(if ($fixed) { $Node.visible });enabled=$(if ($fixed) { $Node.enabled });
            events=$Node.eventBindings;layoutOrder=$Node.layoutOrder;layoutOrientation=$Node.layoutOrientation;
            rect=$(if ($fixed) { $Node.rect });screenRect=$(if ($fixed) { $Node.screenRect });
            padding=$(if ($fixed) { $Node.padding });margin=$(if ($fixed) { $Node.margin })})
        foreach ($child in $Node.children) { Visit $child ([long]$Node.identity) }
    }
    Visit $Document.tree 0
    return $result.ToArray()
}
$baselineProjection=Project $treeBefore | ConvertTo-Json -Depth 16 -Compress
for ($run=1; $run -le $Repeat; ++$run) {
    $prefix=if ($Lease) { 'txgui-left-lease' } else { 'txgui-left' }
    $probe=Join-Path $ProbeDirectory $(if ($Lease) { 'TxGuiLeftLeaseHost.exe' } else { 'TxGuiLeftHost.exe' })
    $path=Join-Path $ProbeDirectory "$prefix-$($original.pid).json"
    $previous=if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).LastWriteTimeUtc } else { [datetime]::MinValue }
    & $probe $Hwnd | Out-File (Join-Path $OutputDirectory "host-$run.log") -Encoding utf8
    $code=$LASTEXITCODE
    Check "run $run original-tree left-light host succeeds" ($code -eq 0)
    if (-not (Test-Path -LiteralPath $path)) { Check "run $run has report" $false; break }
    Check "run $run report is fresh" ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $previous)
    Copy-Item -LiteralPath $path -Destination (Join-Path $OutputDirectory "native-$run.json")
    $report=Get-Content -Raw $path | ConvertFrom-Json
    if ($Lease) {
        $log=Get-Content -Raw (Join-Path $OutputDirectory "host-$run.log")
        Check "run $run persistent actions undo held input and crash checks all pass" ($log -match 'checks=55; failures=0')
        Check "run $run final native cleanup succeeds without layout timer" `
            (-not $report.active -and $report.cleanupFailures -eq 0 -and -not $report.layoutFollowTimer -and -not $report.closeInputGenerated)
        Get-ChildItem -LiteralPath $ProbeDirectory -Filter "txgui-left-lease-$($original.pid)-paint-*.bmp" |
            Copy-Item -Destination $OutputDirectory
    } else {
        Check "run $run complete real original layout drawing and rollback checks pass" `
            ($report.checks -ge 26 -and $report.failures -eq 0 -and $report.nodes -eq $treeBefore.nodes -and $report.restored -and $report.layoutModified)
        Check "run $run preserves all original nodes while moving only supported header and Buttons" (@($report.mismatches).Count -eq 0)
        Check "run $run paints actual three original Buttons without input" `
            (@($report.lights | Where-Object { $_.paints -gt 0 -and $_.rejected -eq 0 }).Count -eq 3 -and -not $report.inputSent)
        foreach ($side in @('left','right')) {
            foreach ($phase in @('before','painted','after')) {
                Copy-Item -LiteralPath (Join-Path $ProbeDirectory "$prefix-$($original.pid)-$side-$phase.bmp") `
                    -Destination (Join-Path $OutputDirectory "$side-$phase-$run.bmp")
            }
        }
    }
    & (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $reader -Active -OutputDirectory (Join-Path $OutputDirectory "read-after-$run")
    Check "run $run complete final original-tree read succeeds" ($LASTEXITCODE -eq 0)
    $treeAfter=Get-Content -Raw (Join-Path $OutputDirectory "read-after-$run/tree.json") | ConvertFrom-Json
    Check "run $run independent full topology typed orders bindings and affected state geometry match" `
        ($baselineProjection -ceq (Project $treeAfter | ConvertTo-Json -Depth 16 -Compress))
    # Outside the modified top-row/header subtrees, the application owns live
    # business visibility and rolling animation. Retain the complete raw data
    # and equality diagnostic; do not write those values or claim them fixed.
    [pscustomobject]@{fullTreeByteEqual=(($treeBefore.tree | ConvertTo-Json -Depth 80 -Compress) -ceq
        ($treeAfter.tree | ConvertTo-Json -Depth 80 -Compress));affectedNodes=$affected.Count;
        scopedProjectionEqual=($baselineProjection -ceq (Project $treeAfter | ConvertTo-Json -Depth 16 -Compress))} |
        ConvertTo-Json | Out-File (Join-Path $OutputDirectory "tree-comparison-$run.json") -Encoding utf8
    Check "run $run original caption semantics and native hits match" `
        (($treeBefore.captionHitSamples | ConvertTo-Json -Depth 10 -Compress) -ceq ($treeAfter.captionHitSamples | ConvertTo-Json -Depth 10 -Compress))
    if ($code -ne 0) { break }
}
& $Audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
Check 'final inventory succeeds' ($LASTEXITCODE -eq 0)
$after=Get-Content -Raw (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$current=@($after.windows | Where-Object { $_.pid -eq $original.pid -and $_.hwnd -eq $Hwnd })
Check 'original process and HWND survive' ($current.Count -eq 1)
if ($current.Count -eq 1) {
    foreach ($field in @('style','exStyle','dpi','rect','client','clientOrigin','minimized','maximized','normalRect','dwmNcEnabled','corner')) {
        Check "original $field restored" (($original.geometry.$field | ConvertTo-Json -Compress) -ceq ($current[0].geometry.$field | ConvertTo-Json -Compress))
    }
    Check 'no owner session remains' ($current[0].owner -eq '0x0')
}
$failures=@($checks | Where-Object { -not $_.passed }).Count
[pscustomobject]@{hwnd=$Hwnd;pid=$original.pid;lease=[bool]$Lease;checks=$checks.Count;failures=$failures;results=$checks} |
    ConvertTo-Json -Depth 7 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
"TXGui combined original left lights outer checks=$($checks.Count); failures=$failures; lease=$Lease"
if ($failures) { exit 1 }
exit 0
