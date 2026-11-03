param(
    [string]$Hwnd = '0xe014f8',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\live-v15')
)
$ErrorActionPreference = 'Stop'
$python = (Get-Command python -ErrorAction Stop).Source
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
try {
    & $python (Join-Path $PSScriptRoot 'mfc-objects.py') --hwnd $Hwnd --out (Join-Path $OutputDirectory 'mfc-runtime-objects.json') 2>&1 |
        Out-File -LiteralPath (Join-Path $OutputDirectory 'mfc-runtime-scan.log') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Read-only MFC scan failed: $LASTEXITCODE" }
    'completed' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'mfc-scan.status')
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'mfc-scan-error.txt')
    'failed' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'mfc-scan.status')
    throw
}
