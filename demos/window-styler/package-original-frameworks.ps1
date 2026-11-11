param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$OutputDirectory,
    [string]$X64BuildDirectory,
    [string]$X86BuildDirectory
)
$ErrorActionPreference='Stop'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot 'out\WindowChanger-v42' }
if (-not $X64BuildDirectory) { $X64BuildDirectory=Join-Path $PSScriptRoot 'out\vs-x64' }
if (-not $X86BuildDirectory) { $X86BuildDirectory=Join-Path $PSScriptRoot 'out\vs-Win32' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0) {
    throw 'Use a new or empty package directory so previous runtime files cannot enter this package.'
}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
function CopyRuntime([string]$Source,[string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing built runtime: $Source" }
    $target=Join-Path $Destination ([IO.Path]::GetFileName($Source))
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $Source).Hash -eq (Get-FileHash -LiteralPath $target).Hash) { return }
    Copy-Item -LiteralPath $Source -Destination $target -Force
}
$features=[Collections.Generic.List[object]]::new()
foreach ($architecture in @('x64','Win32')) {
    $build=if ($architecture -eq 'x64') { $X64BuildDirectory } else { $X86BuildDirectory }
    $runtime=Join-Path $build $Configuration
    $destination=if ($architecture -eq 'x64') { $OutputDirectory } else { Join-Path $OutputDirectory 'x86' }
    [void](New-Item -ItemType Directory -Force -Path $destination)
    foreach ($name in @('WindowStyler.exe','WindowInjector.exe','WindowStyleHost.exe','adapters.ini')) {
        CopyRuntime (Join-Path $runtime $name) $destination
    }
    foreach ($name in Get-Content -LiteralPath (Join-Path $build "runtime-$Configuration.txt")) {
        if ($name) { CopyRuntime (Join-Path $runtime $name) $destination }
    }
    $registry=Get-Content -LiteralPath (Join-Path $destination 'adapters.ini') -Raw
    $idLine=[regex]::Match($registry,'(?m)^Ids=([^\r\n]+)')
    if (-not $idLine.Success) { throw "Empty adapter registry for $architecture" }
    $uniqueIds=[Collections.Generic.HashSet[int]]::new()
    $ids=@(foreach ($text in $idLine.Groups[1].Value.Split(',')) {
        [int]$idValue=0
        if (-not [int]::TryParse($text,[ref]$idValue) -or $idValue -lt 256 -or -not $uniqueIds.Add($idValue)) {
            throw ('Invalid or duplicate adapter id for {0}: {1}' -f $architecture,$text)
        }
        $idValue
    })
    foreach ($entry in [regex]::Matches($registry,'(?m)^File=([^\r\n]+)')) {
        $file=$entry.Groups[1].Value
        if ([IO.Path]::GetFileName($file) -cne $file -or -not (Test-Path -LiteralPath (Join-Path $destination $file) -PathType Leaf)) { throw "Invalid or missing registered adapter: $file" }
    }
    $features.Add([pscustomobject]@{architecture=$architecture;adapterIds=$ids;qt=($ids -contains 256);winui3Native=($ids -contains 288)})
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
$features | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'runtime-features.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release-v42.md') -Destination (Join-Path $OutputDirectory 'README.md') -Force
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
