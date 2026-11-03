# Shared by the standalone demo build and repository cleanup scripts.
function Get-DevelopmentTool {
    param([Parameter(Mandatory = $true)][ValidateSet('cmake', 'clang-format')][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Install Visual Studio 2022 with Desktop development with C++.'
    }
    $installation = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Visual Studio 2022 C++ tools were not found.' }

    $relativePath = if ($Name -eq 'cmake') {
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    } else {
        'VC\Tools\Llvm\x64\bin\clang-format.exe'
    }
    $toolPath = Join-Path $installation $relativePath
    if (-not (Test-Path -LiteralPath $toolPath)) {
        throw "Visual Studio tool is missing: $toolPath. Install its C++ CMake/LLVM tools component."
    }
    return $toolPath
}
