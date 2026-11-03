param([string]$PackageDirectory,[string]$OutputDirectory,[switch]$RealFrameworks,[string]$BuildPrefix='vs')
$ErrorActionPreference='Stop'
if (-not $PackageDirectory) { $PackageDirectory=Join-Path $PSScriptRoot '..\out\WindowChanger-v32' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '..\out\live-v32\controller-matrix' }
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run the isolated matrix verifier as administrator.' }
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$release=Join-Path $PSScriptRoot "..\out\$BuildPrefix-x64\Release"
$audit=Join-Path $release 'WindowAudit.exe'
& $audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Initial inventory failed.' }
$before=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$runs=[Collections.Generic.List[object]]::new()
foreach ($architecture in @('x64','Win32')) { foreach ($configuration in @('Release','Debug')) {
    $directory=Join-Path $PSScriptRoot "..\out\$BuildPrefix-$architecture\$configuration"
    $destination=Join-Path $OutputDirectory "$architecture-$configuration"
    & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $PSScriptRoot 'verify-controller.ps1') -Directory $directory -OutputDirectory $destination
    $code=$LASTEXITCODE
    $result=Get-Content -Raw -Encoding UTF8 (Join-Path $destination 'result.json') | ConvertFrom-Json
    $runs.Add([pscustomobject]@{name="$architecture-$configuration";passed=($code -eq 0 -and $result.failures -eq 0 -and $result.checks -ge 46);checks=$result.checks})
} }
$cross=Join-Path $OutputDirectory 'Package-x86-targets'
& "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot 'verify-controller.ps1') -Directory $PackageDirectory -VerifierDirectory $release `
    -FixtureDirectory (Join-Path $PSScriptRoot "..\out\$BuildPrefix-Win32\Release") -OutputDirectory $cross
$code=$LASTEXITCODE
$result=Get-Content -Raw -Encoding UTF8 (Join-Path $cross 'result.json') | ConvertFrom-Json
$runs.Add([pscustomobject]@{name='Package-x64-controller-x86-targets';passed=($code -eq 0 -and $result.failures -eq 0 -and $result.checks -ge 46);checks=$result.checks})
if ($RealFrameworks) {
    foreach ($family in @('MFC / BCG Ribbon','Office NetUI','TXGuiFoundation')) {
        $targets=@($before.windows | Where-Object { $_.framework -eq $family -and $_.owner -eq '0x0' })
        if ($targets.Count -ne 1) { throw "Expected exactly one idle original window for $family." }
        $target=$targets[0]
        $name=($family -replace '[^a-zA-Z0-9]','-')
        & (Join-Path $release 'ControllerFrameworkTests.exe') $PackageDirectory $target.hwnd 2>&1 |
            Out-File (Join-Path $OutputDirectory ($name+'.log')) -Encoding utf8
        $code=$LASTEXITCODE
        $log=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory ($name+'.log'))
        $runs.Add([pscustomobject]@{name=$family;passed=($code -eq 0 -and $log -match 'checks=10; failures=0');checks=10})
    }
}
& $audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
if ($LASTEXITCODE -ne 0) { throw 'Final inventory failed.' }
$after=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$states=foreach ($original in @($before.windows | Where-Object { $_.framework -in @('MFC / BCG Ribbon','Office NetUI','TXGuiFoundation') })) {
    $current=@($after.windows | Where-Object { $_.hwnd -eq $original.hwnd -and $_.pid -eq $original.pid })
    [pscustomobject]@{hwnd=$original.hwnd;alive=($current.Count -eq 1);restored=($current.Count -eq 1 -and $current[0].owner -eq $original.owner -and
        (($original.geometry | Select-Object * -ExcludeProperty hookModules | ConvertTo-Json -Depth 6 -Compress) -ceq
         ($current[0].geometry | Select-Object * -ExcludeProperty hookModules | ConvertTo-Json -Depth 6 -Compress)))}
}
$passed=@($runs | Where-Object { -not $_.passed }).Count -eq 0 -and @($states | Where-Object { -not $_.restored }).Count -eq 0
[pscustomobject]@{passed=$passed;runs=$runs;originalWindows=$states;computerUse=$false;rebootOrLogoff=$false} |
    ConvertTo-Json -Depth 7 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
if (-not $passed) { exit 1 }
exit 0
