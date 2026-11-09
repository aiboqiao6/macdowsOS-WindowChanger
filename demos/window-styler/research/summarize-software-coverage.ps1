param([string]$InventoryDirectory,[string]$CaptureDirectory)
$ErrorActionPreference='Stop'
if(-not $InventoryDirectory) { $InventoryDirectory=Join-Path $PSScriptRoot '..\out\live-v33\inventory-final' }
if(-not $CaptureDirectory) { $CaptureDirectory=Join-Path $PSScriptRoot '..\out\live-v33\desktop-final' }
$software=Get-Content -LiteralPath (Join-Path $InventoryDirectory 'software.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$inventory=Get-Content -LiteralPath (Join-Path $CaptureDirectory 'inventory-screenshots.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$captures=Get-Content -LiteralPath (Join-Path $CaptureDirectory 'report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function PathKey([string]$Path) {
    if(-not $Path) { return '' }
    try { return [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Path).Trim().Trim('"')).TrimEnd('\').ToLowerInvariant() } catch { return '' }
}
$rows=foreach($record in $software.installed) {
    $directory=PathKey $record.InstallLocation
    $icon=''
    if($record.DisplayIcon -match '^"?(.+?\.exe)"?(?:,\s*-?\d+)?$') { $icon=PathKey $Matches[1] }
    $targets=@($inventory.windows | Where-Object {
        $path=PathKey $_.process
        $path -and (($directory -and $path.StartsWith($directory+'\')) -or ($icon -and $path -eq $icon))
    })
    $results=@(foreach($target in $targets) { $captures.windows | Where-Object {$_.hwnd -eq $target.hwnd -and $_.status -ne 'excluded'} })
    $passed=@($results | Where-Object status -eq 'captured').Count
    [pscustomobject]@{software=$record.DisplayName;version=$record.DisplayVersion;
        status=$(if(-not $results.Count){'no-window-verified'}elseif($passed -eq $results.Count){'current-windows-captured'}else{'partial-or-failed'});
        windows=$results.Count;captured=$passed;unverified=$results.Count-$passed;
        bothThemesVerified=$false;allWindowTypesVerified=$false}
}
$rows | Export-Csv -LiteralPath (Join-Path $CaptureDirectory 'installed-coverage.csv') -NoTypeInformation -Encoding utf8
$packageRows=@(foreach($record in $software.packages) {
    $directory=PathKey $record.InstallLocation
    $targets=@($inventory.windows | Where-Object {
        $path=PathKey $_.process
        $directory -and $path -and $path.StartsWith($directory+'\')
    })
    $results=@(foreach($target in $targets) { $captures.windows | Where-Object {$_.hwnd -eq $target.hwnd -and $_.status -ne 'excluded'} })
    $passed=@($results | Where-Object status -eq 'captured').Count
    [pscustomobject]@{package=$record.Name;identity=$record.PackageFullName;
        status=$(if(-not $results.Count){'no-window-verified'}elseif($passed -eq $results.Count){'current-windows-captured'}else{'partial-or-failed'});
        windows=$results.Count;captured=$passed;unverified=$results.Count-$passed;
        bothThemesVerified=$false;allWindowTypesVerified=$false}
})
$packageRows | Export-Csv -LiteralPath (Join-Path $CaptureDirectory 'package-coverage.csv') -NoTypeInformation -Encoding utf8
$unverified=@($rows | Where-Object status -ne 'current-windows-captured').Count
$summary=[pscustomobject]@{installedRecords=@($rows).Count;startEntries=@($software.startEntries).Count;
    packageRecords=@($software.packages).Count;installedRecordsWithCapturedWindows=@($rows | Where-Object status -eq 'current-windows-captured').Count;
    installedRecordsWithoutCompleteCurrentWindowEvidence=$unverified;
    capturedWindows=$captures.captured;failedWindows=$captures.failed;unattachedWindows=$captures.unsupported;
    allInstalledSoftwareVerified=$false;
    note='A captured window does not verify all dialogs, document states, both themes, DPI settings, or every installed application. Records without windows are kept, not counted as passes.';
    installed=$rows;packages=$packageRows}
$summary | ConvertTo-Json -Depth 7 | Out-File (Join-Path $CaptureDirectory 'installed-coverage.json') -Encoding utf8
$summary | Select-Object * -ExcludeProperty installed,packages
