param([string]$Directory,[string]$FixtureDirectory,[string]$OutputDirectory,[string]$VerifierDirectory)
$ErrorActionPreference='Stop'
if (-not $Directory) { $Directory=Join-Path $PSScriptRoot '..\out\vs-x64\Release' }
if (-not $VerifierDirectory) { $VerifierDirectory=$Directory }
if (-not $FixtureDirectory) { $FixtureDirectory=$VerifierDirectory }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '..\out\live-v31\controller-Release' }
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run the isolated controller verifier as administrator.' }
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$test=Join-Path $VerifierDirectory 'ControllerLifecycleTests.exe'
$controller=Join-Path $Directory 'WindowStyler.exe'
$fixture=Join-Path $FixtureDirectory 'StyleTarget.exe'
try {
    & $test $controller $fixture $OutputDirectory 2>&1 | Out-File (Join-Path $OutputDirectory 'native.log') -Encoding utf8
    $code=$LASTEXITCODE
    if ($code -ne 0) { exit $code }
    $result=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'result.json') | ConvertFrom-Json
    if ($result.failures -ne 0) { exit 1 }
} catch {
    $_ | Out-String | Out-File (Join-Path $OutputDirectory 'runner-error.txt') -Encoding utf8
    exit 1
}
exit 0
