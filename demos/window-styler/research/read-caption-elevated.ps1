param([string]$Hwnd = '0xe014f8')
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path $PSScriptRoot '..\out\live-v16'
[void](New-Item -ItemType Directory -Force -Path $outputDirectory)
try {
    & (Get-Command python -ErrorAction Stop).Source (Join-Path $PSScriptRoot 'mfc-caption.py') --hwnd $Hwnd `
        --previous (Join-Path $PSScriptRoot '..\out\live-v15\mfc-runtime-objects.json') `
        --out (Join-Path $outputDirectory 'mfc-caption.json') 2>&1 |
        Out-File -LiteralPath (Join-Path $outputDirectory 'mfc-caption.log') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw 'Read-only caption inspection failed.' }
    'completed' | Set-Content -LiteralPath (Join-Path $outputDirectory 'mfc-caption.status')
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $outputDirectory 'mfc-caption-error.txt')
    'failed' | Set-Content -LiteralPath (Join-Path $outputDirectory 'mfc-caption.status')
    throw
}
