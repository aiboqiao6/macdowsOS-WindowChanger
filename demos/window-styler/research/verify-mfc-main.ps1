param(
    [Parameter(Mandatory=$true)][string]$Hwnd,
    [string]$OutputDirectory,
    [string]$PackageDirectory
)
$ErrorActionPreference='Stop'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '..\out\live-v30\mfc-main' }
if (-not $PackageDirectory) { $PackageDirectory=Join-Path $PSScriptRoot '..\out\WindowChanger-original-frameworks-v30' }
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this bounded verifier from an administrator PowerShell. It does not elevate itself.'
}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$audit=Join-Path $PSScriptRoot '..\out\vs-x64\Release\WindowAudit.exe'
& $audit --direct-inventory (Join-Path $OutputDirectory 'before.json')
if ($LASTEXITCODE -ne 0) { throw 'Cannot capture the initial elevated inventory.' }
$before=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'before.json') | ConvertFrom-Json
$targets=@($before.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.framework -eq 'MFC / BCG Ribbon' })
if ($targets.Count -ne 1 -or $targets[0].owner -ne '0x0') { throw 'Select one idle original MFC/BCG Ribbon HWND.' }
$original=$targets[0]
$runs=[Collections.Generic.List[object]]::new()
$cases=@(
    @{name='Release';path=(Join-Path $PSScriptRoot '..\out\vs-x64\Release\OriginalFrameworkContract.exe')},
    @{name='Debug';path=(Join-Path $PSScriptRoot '..\out\vs-x64\Debug\OriginalFrameworkContract.exe')},
    @{name='Package';path=(Join-Path $PackageDirectory 'OriginalFrameworkContract.exe')}
)
foreach ($case in $cases) {
    & $case.path $Hwnd 2>&1 | Out-File (Join-Path $OutputDirectory ($case.name+'.log')) -Encoding utf8
    $code=$LASTEXITCODE
    $log=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory ($case.name+'.log'))
    $runs.Add([pscustomobject]@{configuration=$case.name;exitCode=$code;passed=($code -eq 0 -and $log -match 'checks=10; failures=0')})
    if ($code -ne 0) { break }
}
& $audit --direct-inventory (Join-Path $OutputDirectory 'after.json')
if ($LASTEXITCODE -ne 0) { throw 'Cannot capture the final elevated inventory.' }
$after=Get-Content -Raw -Encoding UTF8 (Join-Path $OutputDirectory 'after.json') | ConvertFrom-Json
$current=@($after.windows | Where-Object { $_.hwnd -eq $Hwnd -and $_.pid -eq $original.pid })
$restored=$current.Count -eq 1 -and $current[0].owner -eq '0x0' -and
    (($original.geometry | Select-Object * -ExcludeProperty hookModules | ConvertTo-Json -Depth 6 -Compress) -ceq
     ($current[0].geometry | Select-Object * -ExcludeProperty hookModules | ConvertTo-Json -Depth 6 -Compress))
$passed=$runs.Count -eq 3 -and @($runs | Where-Object { -not $_.passed }).Count -eq 0 -and $restored
[pscustomobject]@{hwnd=$Hwnd;pid=$original.pid;runs=$runs;originalWindowRestored=$restored;passed=$passed;computerUse=$false;inputSent=$false} |
    ConvertTo-Json -Depth 6 | Out-File (Join-Path $OutputDirectory 'result.json') -Encoding utf8
if (-not $passed) { exit 1 }
exit 0
