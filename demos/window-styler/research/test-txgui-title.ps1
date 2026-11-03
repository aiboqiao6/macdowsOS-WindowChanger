param(
    [Parameter(Mandatory=$true)][string]$Hwnd,
    [string]$Probe=(Join-Path $PSScriptRoot '..\out\framework-probe-Win32\probe\Release\TxGuiTitleHost.exe'),
    [string]$ReaderHost,
    [string]$Audit=(Join-Path $PSScriptRoot '..\out\vs-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\live-v29\title'),
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
$blocked=$false
for ($run=1; $run -le $Repeat; ++$run) {
    $path=Join-Path (Split-Path $Probe) "txgui-title-$($original.pid).json"
    $previous=if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).LastWriteTimeUtc } else { [datetime]::MinValue }
    & $Probe $Hwnd | Out-File (Join-Path $OutputDirectory "host-$run.log") -Encoding utf8
    $code=$LASTEXITCODE
    if (-not (Test-Path -LiteralPath $path)) { Check "run $run has a report" $false; break }
    Check "run $run report is fresh" ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $previous)
    Copy-Item -LiteralPath $path -Destination (Join-Path $OutputDirectory "title-$run.json")
    $report=Get-Content -Raw $path | ConvertFrom-Json
    if (-not $report.pixelPreflight -and -not $report.titleExpanded -and @($report.failedChecks).Count -eq 1 -and
        $report.failedChecks[0] -ceq 'unoccluded-original-header-and-caption-pixels-stable-before-transaction') {
        $blocked=$true
        Check "run $run blocked before any layout write" ($report.parentsChanged -eq $false -and $report.inputSent -eq $false)
        break
    }
    Check "run $run original-title expansion host succeeds" ($code -eq 0)
    Check "run $run native layout and pixel checks pass" `
        ($report.failures -eq 0 -and $report.checks -ge 16 -and $report.nodes -eq $treeBefore.nodes -and $report.restored)
    Check "run $run expands left without new height and exactly restores original bounds" `
        ($report.titleExpanded -and $report.during[0] -eq 0 -and $report.during[1] -eq $report.before[1] -and
         $report.during[2] -eq $report.before[2] -and $report.during[3] -eq $report.before[3] -and
         ($report.before | ConvertTo-Json -Compress) -ceq ($report.after | ConvertTo-Json -Compress))
    Check "run $run uses no reparent custom drawing input or persistent session" `
        (-not $report.parentsChanged -and -not $report.inputSent -and -not $report.customPixelsWritten -and -not $report.persistentSession)
    $stem=Join-Path (Split-Path $Probe) "txgui-title-$($original.pid)"
    foreach ($side in @('left','right')) {
        foreach ($phase in @('before','during','after')) {
            Copy-Item -LiteralPath "$stem-$side-$phase.bmp" -Destination (Join-Path $OutputDirectory "$side-$phase-$run.bmp")
        }
    }
    if ($code -ne 0) { break }
}
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $ReaderHost -Active -OutputDirectory (Join-Path $OutputDirectory 'read-after')
Check 'complete final original-tree read succeeds' ($LASTEXITCODE -eq 0)
$treeAfter=Get-Content -Raw (Join-Path $OutputDirectory 'read-after/tree.json') | ConvertFrom-Json
function Project($Document) {
    $result=[Collections.Generic.List[object]]::new()
    function Visit($Node,[long]$Parent,[bool]$Title) {
        $Title=$Title -or $Node.identity -eq $report.originalTitle
        $result.Add([pscustomobject]@{identity=$Node.identity;parent=$Parent;type=$Node.frameworkType;
            children=@($Node.children | ForEach-Object identity);hidden=$Node.hidden;visible=$Node.visible;enabled=$Node.enabled;
            events=$Node.eventBindings;layoutOrder=$Node.layoutOrder;layoutOrientation=$Node.layoutOrientation;
            rect=$(if ($Title) { $Node.rect });screenRect=$(if ($Title) { $Node.screenRect });
            padding=$(if ($Title) { $Node.padding });margin=$(if ($Title) { $Node.margin })})
        foreach ($child in $Node.children) { Visit $child ([long]$Node.identity) $Title }
    }
    Visit $Document.tree 0 $false
    return $result.ToArray()
}
Check 'full original topology bindings and affected title subtree geometry are unchanged' `
    ((Project $treeBefore | ConvertTo-Json -Depth 15 -Compress) -ceq (Project $treeAfter | ConvertTo-Json -Depth 15 -Compress))
Check 'original caption hints and native hit identities are unchanged' `
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
[pscustomobject]@{hwnd=$Hwnd;pid=$original.pid;blocked=$blocked;checks=$checks.Count;failures=$failures;results=$checks} |
    ConvertTo-Json -Depth 6 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
"TXGui original-title expansion outer checks=$($checks.Count); failures=$failures; blocked=$blocked"
if ($failures) { exit 1 }
if ($blocked) { exit 2 }
exit 0
