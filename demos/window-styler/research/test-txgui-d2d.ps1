param(
    [Parameter(Mandatory=$true)][string]$Hwnd,
    [string]$Probe=(Join-Path $PSScriptRoot '..\out\framework-probe-Win32\probe\Release\TxGuiD2DDrawHost.exe'),
    [string]$ReaderHost,
    [string]$Audit=(Join-Path $PSScriptRoot '..\out\vs-x64\Release\WindowAudit.exe'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\live-v26\txgui-d2d'),
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
$targets=@($before.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.framework -eq 'TXGuiFoundation' })
if ($targets.Count -ne 1) { throw 'Select exactly one original TXGui HWND.' }
$original=$targets[0]
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $ReaderHost -Active -OutputDirectory (Join-Path $OutputDirectory 'read-before')
Check 'initial original-tree read succeeds' ($LASTEXITCODE -eq 0)
$treeBefore=Get-Content -Raw (Join-Path $OutputDirectory 'read-before/tree.json') | ConvertFrom-Json
for ($run=1; $run -le $Repeat; ++$run) {
    $reportPath=Join-Path (Split-Path $Probe) "txgui-d2d-drawing-$($original.pid).json"
    $previousWrite=if (Test-Path -LiteralPath $reportPath) { (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc } else { [datetime]::MinValue }
    & $Probe $Hwnd | Out-File (Join-Path $OutputDirectory "host-$run.log") -Encoding utf8
    $code=$LASTEXITCODE
    Check "bounded original-position D2D host $run succeeds" ($code -eq 0)
    if (-not (Test-Path -LiteralPath $reportPath)) { break }
    Check "run $run report is fresh" ((Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -gt $previousWrite)
    Copy-Item -LiteralPath $reportPath -Destination (Join-Path $OutputDirectory "drawing-$run.json")
    $stem=Join-Path (Split-Path $Probe) "txgui-d2d-drawing-$($original.pid)"
    foreach ($phase in @('before','painted','restored')) {
        Copy-Item -LiteralPath "$stem-$phase.bmp" -Destination (Join-Path $OutputDirectory "$phase-$run.bmp")
    }
    $report=Get-Content -Raw $reportPath | ConvertFrom-Json
    Check "run $run passes native drawing and undo checks" ($report.failures -eq 0 -and $report.checks -ge 48)
    Check "run $run replaces all three original content glyphs with filled D2D lights" `
        (@($report.lights).Count -eq 3 -and @($report.lights | Where-Object { $_.paints -le 0 -or $_.rejected -ne 0 -or $_.stateChanges -ne 0 -or $_.originalGlyphPixels -le 0 -or $_.remainingGlyphPixels -ne 0 -or $_.circlePixels -le 0 -or $_.antialiasPixels -le 0 }).Count -eq 0)
    Check "run $run changes pixels but sends no input or layout or synthetic paint" `
        ($report.pixelsWritten -and -not $report.layoutModified -and -not $report.inputSent -and -not $report.syntheticNotifications -and -not $report.persistentSession)
    Check "run $run restores all original caption pixels filters geometry and subscriptions" $report.restored
    if ($code -ne 0) { break }
}
& (Join-Path $PSScriptRoot 'test-framework-readonly.ps1') -Hwnd $Hwnd -Probe $ReaderHost -Active -OutputDirectory (Join-Path $OutputDirectory 'read-after')
Check 'final original-tree read succeeds' ($LASTEXITCODE -eq 0)
$treeAfter=Get-Content -Raw (Join-Path $OutputDirectory 'read-after/tree.json') | ConvertFrom-Json
function Project-Tree($Document) {
    $result=[Collections.Generic.List[object]]::new()
    function Visit($Node,[long]$Parent) {
        if ($null -eq $Node) { return }
        $result.Add([pscustomobject]@{identity=$Node.identity;parent=$Parent;type=$Node.frameworkType;children=@($Node.children | Where-Object { $null -ne $_ } | ForEach-Object identity)})
        foreach ($child in $Node.children) { Visit $child ([long]$Node.identity) }
    }
    Visit $Document.tree 0
    return $result.ToArray()
}
Check 'all original node identities parents and child order are preserved' `
    ((Project-Tree $treeBefore | ConvertTo-Json -Depth 6 -Compress) -ceq (Project-Tree $treeAfter | ConvertTo-Json -Depth 6 -Compress))
function Project-Caption($Document) {
    $ids=@($Document.captionHitSamples | Select-Object -First 7 | Where-Object frameworkType -eq 'IGFButton' | ForEach-Object identity | Select-Object -Unique)
    $result=[Collections.Generic.List[object]]::new()
    function Visit($Node) {
        if ($null -eq $Node) { return }
        if ($Node.identity -in $ids) { $result.Add(($Node | Select-Object identity,rect,screenRect,hidden,visible,enabled,eventBindings)) }
        foreach ($child in $Node.children) { Visit $child }
    }
    Visit $Document.tree
    return $result.ToArray()
}
Check 'original caption geometry visibility and event subscriptions are preserved' `
    ((Project-Caption $treeBefore | ConvertTo-Json -Depth 12 -Compress) -ceq (Project-Caption $treeAfter | ConvertTo-Json -Depth 12 -Compress))
Check 'original tooltip hints and hit identities are preserved' `
    (($treeBefore.captionHitSamples | ConvertTo-Json -Depth 6 -Compress) -ceq ($treeAfter.captionHitSamples | ConvertTo-Json -Depth 6 -Compress))
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
[pscustomobject]@{hwnd=$Hwnd;pid=$original.pid;checks=$checks.Count;failures=$failures;results=$checks} |
    ConvertTo-Json -Depth 6 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
"TXGui original-position D2D outer checks=$($checks.Count); failures=$failures"
if ($failures) { exit 1 }
