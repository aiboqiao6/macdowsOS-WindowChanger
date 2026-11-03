param(
    [Parameter(Mandatory=$true)][string]$Hwnd,
    [string]$Probe=(Join-Path $PSScriptRoot '..\out\framework-probe-Win32\probe\Release\TxGuiOrderHost.exe'),
    [string]$ReaderHost,
    [string]$Audit=(Join-Path $PSScriptRoot '..\out\vs-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\live-v28\order'),
    [ValidateRange(1,3)][int]$Repeat=2
)
$ErrorActionPreference='Stop'
if (-not $ReaderHost) { $ReaderHost=Join-Path (Split-Path $Probe) 'FrameworkResearchHost.exe' }
[void](New-Item -ItemType Directory -Force $OutputDirectory)
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed) { $checks.Add([pscustomobject]@{name=$Name;passed=$Passed}) }
& $Audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Initial inventory failed.' }
$before=Get-Content -Raw (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$original=@($before.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.framework -eq 'TXGuiFoundation' })
if ($original.Count -ne 1) { throw 'Select exactly one original TXGui HWND.' }
$original=$original[0]
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $ReaderHost -Active -OutputDirectory (Join-Path $OutputDirectory 'read-before')
Check 'complete original-tree baseline succeeds' ($LASTEXITCODE -eq 0)
$treeBefore=Get-Content -Raw (Join-Path $OutputDirectory 'read-before/tree.json') | ConvertFrom-Json
for ($run=1; $run -le $Repeat; ++$run) {
    $path=Join-Path (Split-Path $Probe) "txgui-order-$($original.pid).json"
    $previous=if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).LastWriteTimeUtc } else { [datetime]::MinValue }
    & $Probe $Hwnd | Out-File (Join-Path $OutputDirectory "host-$run.log") -Encoding utf8
    $code=$LASTEXITCODE
    Check "run $run bounded original-order host succeeds" ($code -eq 0)
    if (-not (Test-Path -LiteralPath $path)) { break }
    Check "run $run report is fresh" ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $previous)
    Copy-Item -LiteralPath $path -Destination (Join-Path $OutputDirectory "order-$run.json")
    $report=Get-Content -Raw $path | ConvertFrom-Json
    Check "run $run native transaction and complete-tree checks pass" `
        ($report.failures -eq 0 -and $report.checks -ge 10 -and $report.nodes -eq $treeBefore.nodes)
    foreach ($chain in @('Element','Frame')) {
        $first=$report."${chain}Before" | ConvertTo-Json -Compress
        $during=$report."${chain}During" | ConvertTo-Json -Compress
        $after=$report."${chain}After" | ConvertTo-Json -Compress
        Check "run $run $chain changes temporarily and is restored exactly" ($first -cne $during -and $first -ceq $after)
    }
    Check "run $run changes neither parents geometry pixels input nor persistent ownership" `
        ($report.orderModified -and $report.duringStatePreserved -and -not $report.parentsChanged -and
         -not $report.geometryModified -and -not $report.pixelsWritten -and -not $report.inputSent -and -not $report.persistentSession)
    Check "run $run restores both original orders" $report.restored
    if ($code -ne 0) { break }
}
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $ReaderHost -Active -OutputDirectory (Join-Path $OutputDirectory 'read-after')
Check 'complete final original-tree read succeeds' ($LASTEXITCODE -eq 0)
$treeAfter=Get-Content -Raw (Join-Path $OutputDirectory 'read-after/tree.json') | ConvertFrom-Json
function Project-Topology($Document) {
    $result=[Collections.Generic.List[object]]::new()
    function Visit($Node,[long]$Parent) {
        $result.Add([pscustomobject]@{identity=$Node.identity;parent=$Parent;type=$Node.frameworkType;
            children=@($Node.children | ForEach-Object identity);hidden=$Node.hidden;visible=$Node.visible;enabled=$Node.enabled;
            eventBindings=$Node.eventBindings;layoutOrder=$Node.layoutOrder;layoutOrientation=$Node.layoutOrientation;
            layoutItemsComplete=$Node.layoutItemsComplete})
        foreach ($child in $Node.children) { Visit $child ([long]$Node.identity) }
    }
    Visit $Document.tree 0
    return $result.ToArray()
}
Check 'full original topology typed layouts visibility and button bindings are unchanged' `
    ((Project-Topology $treeBefore | ConvertTo-Json -Depth 15 -Compress) -ceq (Project-Topology $treeAfter | ConvertTo-Json -Depth 15 -Compress))
function Project-TargetGeometry($Document,[long]$Header,[long]$Parent) {
    $result=[Collections.Generic.List[object]]::new()
    $buttons=@($Document.captionHitSamples | Select-Object -First 7 | Where-Object frameworkType -eq 'IGFButton' | ForEach-Object identity)
    function Visit($Node,[bool]$InHeader) {
        $InHeader=$InHeader -or $Node.identity -eq $Header
        if ($InHeader -or $Node.identity -eq $Parent -or $Node.identity -in $buttons) {
            $result.Add(($Node | Select-Object identity,rect,screenRect,padding,margin,hidden,visible,enabled))
        }
        foreach ($child in $Node.children) { Visit $child $InHeader }
    }
    Visit $Document.tree $false
    return $result.ToArray()
}
Check 'original affected header subtree parent and semantic caption button geometry are unchanged' `
    ((Project-TargetGeometry $treeBefore $report.originalHeader $report.originalParent | ConvertTo-Json -Depth 10 -Compress) -ceq
     (Project-TargetGeometry $treeAfter $report.originalHeader $report.originalParent | ConvertTo-Json -Depth 10 -Compress))
# Unrelated host animations can advance between separate process invocations.
# Keep the stronger byte comparison as a diagnostic. The native transaction
# still requires exact geometry/state of every node while on the GUI thread.
$fullTreeByteEqual=($treeBefore.tree | ConvertTo-Json -Depth 100 -Compress) -ceq ($treeAfter.tree | ConvertTo-Json -Depth 100 -Compress)
Check 'original hints and native hit identities are unchanged' `
    (($treeBefore.captionHitSamples | ConvertTo-Json -Depth 10 -Compress) -ceq ($treeAfter.captionHitSamples | ConvertTo-Json -Depth 10 -Compress))
& $Audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
Check 'final inventory succeeds' ($LASTEXITCODE -eq 0)
$after=Get-Content -Raw (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$current=@($after.windows | Where-Object { $_.pid -eq $original.pid -and $_.hwnd -eq $Hwnd })
Check 'original process and HWND are alive' ($current.Count -eq 1)
if ($current.Count -eq 1) {
    foreach ($field in @('style','exStyle','dpi','rect','client','clientOrigin','minimized','maximized','normalRect','dwmNcEnabled','corner')) {
        Check "original $field is restored" `
            (($original.geometry.$field | ConvertTo-Json -Compress) -ceq ($current[0].geometry.$field | ConvertTo-Json -Compress))
    }
    Check 'no owner session remains' ($current[0].owner -eq '0x0')
}
$failures=@($checks | Where-Object { -not $_.passed }).Count
[pscustomobject]@{hwnd=$Hwnd;pid=$original.pid;checks=$checks.Count;failures=$failures;fullTreeByteEqual=$fullTreeByteEqual;results=$checks} |
    ConvertTo-Json -Depth 6 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
"TXGui dual original order outer checks=$($checks.Count); failures=$failures"
if ($failures) { exit 1 }
exit 0
