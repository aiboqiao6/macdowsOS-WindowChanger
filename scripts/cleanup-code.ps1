param([switch]$Check)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\demos\window-styler\development-tools.ps1')
$formatter = Get-DevelopmentTool 'clang-format'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $projectRoot 'demos\window-styler'
# Keep generated SDKs, vendored code and historical research out of cleanup.
$sources = @(
    Get-ChildItem -LiteralPath $sourceRoot -File | Where-Object { $_.Extension -in '.cpp', '.h' }
    Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'sdk') -File | Where-Object { $_.Extension -in '.cpp', '.h' }
)
if ($Check) {
    & $formatter --style=file --dry-run --Werror @($sources.FullName)
    if ($LASTEXITCODE -ne 0) { throw 'Code formatting check failed. Run scripts\cleanup-code.ps1.' }
} else {
    $backupDirectory = Join-Path $projectRoot 'out\code-cleanup'
    [void](New-Item -ItemType Directory -Force -Path $backupDirectory)
    $backupPath = Join-Path $backupDirectory ('sources-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.zip')
    Compress-Archive -LiteralPath $sources.FullName -DestinationPath $backupPath
    & $formatter --style=file -i @($sources.FullName)
    if ($LASTEXITCODE -ne 0) { throw "Code cleanup failed. Original sources: $backupPath" }
    Write-Output "Source backup: $backupPath"
}
Write-Output "Formatting verified: $($sources.Count) C++ source/header files."
