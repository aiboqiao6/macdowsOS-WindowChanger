param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot 'out\WindowChanger-v31' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
function CopyRuntime([string]$Source,[string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing built runtime: $Source" }
    $target=Join-Path $Destination ([IO.Path]::GetFileName($Source))
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $Source).Hash -eq (Get-FileHash -LiteralPath $target).Hash) { return }
    Copy-Item -LiteralPath $Source -Destination $target -Force
}
foreach ($architecture in @('x64','Win32')) {
    $build=Join-Path $PSScriptRoot "out\vs-$architecture"
    $runtime=Join-Path $build $Configuration
    $destination=if ($architecture -eq 'x64') { $OutputDirectory } else { Join-Path $OutputDirectory 'x86' }
    [void](New-Item -ItemType Directory -Force -Path $destination)
    foreach ($name in @('WindowStyler.exe','WindowInjector.exe','WindowStyleHost.exe','adapters.ini')) {
        CopyRuntime (Join-Path $runtime $name) $destination
    }
    foreach ($name in Get-Content -LiteralPath (Join-Path $build "runtime-$Configuration.txt")) {
        if ($name) { CopyRuntime (Join-Path $runtime $name) $destination }
    }
    # App-local redistributable CRT: do not assume a clean PC has the VS tools.
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Cannot locate the installed redistributable VC++ runtime.' }
    $redist=Join-Path $installation 'VC\Redist\MSVC'
    $native=if ($architecture -eq 'x64') { 'x64' } else { 'x86' }
    $candidates=@(Get-ChildItem -LiteralPath $redist -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object Name -Descending)
    $crt=$null
    foreach ($version in $candidates) {
        $path=Join-Path $version.FullName "$native\Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $path) { $crt=$path; break }
    }
    if (-not $crt) { throw "Missing app-local VC++ redistributable for $architecture." }
    Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File | ForEach-Object { CopyRuntime $_.FullName $destination }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release-v31.md') -Destination (Join-Path $OutputDirectory 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'original-frameworks.md') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\..\LICENSE.txt') -Destination $OutputDirectory -Force
[void](New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'third_party'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\minhook\LICENSE.txt') -Destination (Join-Path $OutputDirectory 'third_party\minhook-LICENSE.txt') -Force
$manifest=Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Where-Object Name -ne 'runtime-manifest.json' |
    ForEach-Object { [pscustomobject]@{file=$_.FullName.Substring($OutputDirectory.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash} }
$manifest | ConvertTo-Json -Depth 4 | Out-File (Join-Path $OutputDirectory 'runtime-manifest.json') -Encoding utf8
$archive=$OutputDirectory+'.zip'
Compress-Archive -LiteralPath $OutputDirectory -DestinationPath $archive -Force
"Runtime: $OutputDirectory"
"Archive: $archive"
