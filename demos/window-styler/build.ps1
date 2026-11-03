param(
    [ValidateSet('x64', 'Win32')][string]$Architecture = 'x64',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('Build', 'Rebuild', 'Clean')][string]$Action = 'Build',
    [string]$BuildDirectory,
    [switch]$GenerateOnly,
    [switch]$Run,
    [switch]$Test,
    [string]$QtRoot,
    [switch]$WpfSamples
)
$ErrorActionPreference = 'Stop'
$demoRoot = $PSScriptRoot
. (Join-Path $demoRoot 'development-tools.ps1')
$cmakePath = Get-DevelopmentTool 'cmake'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $demoRoot "out\vs-$Architecture" }
$buildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
if ($Action -eq 'Clean') {
    if (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt')) {
        & $cmakePath --build $buildDirectory --config $Configuration --target clean
        if ($LASTEXITCODE -ne 0) { throw 'Clean failed.' }
    }
    return
}
$configureOptions = @('-DSTYLER_QT_SAMPLES=OFF', '-DSTYLER_WPF_SAMPLES=OFF')
if ($QtRoot) { $configureOptions += @('-DSTYLER_QT_SAMPLES=ON', "-DCMAKE_PREFIX_PATH=$QtRoot") }
if ($WpfSamples) { $configureOptions += '-DSTYLER_WPF_SAMPLES=ON' }
& $cmakePath -S $demoRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A $Architecture @configureOptions
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
Write-Output "Visual Studio solution: $(Join-Path $buildDirectory 'MacdowsWindowStyler.sln')"
if ($GenerateOnly) { return }
$buildOptions = @('--build', $buildDirectory, '--config', $Configuration, '--parallel')
if ($Action -eq 'Rebuild') { $buildOptions += '--clean-first' }
& $cmakePath @buildOptions
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($QtRoot) {
    # Deploy only for the ordinary sample process. The hook loads this runtime
    # plugin only after confirming matching Qt libraries are already in target.
    $qtConfiguration = '--' + $Configuration.ToLowerInvariant()
    & (Join-Path $QtRoot 'bin\windeployqt.exe') $qtConfiguration --no-translations --no-compiler-runtime --no-opengl-sw --qmldir $demoRoot (Join-Path $buildDirectory "$Configuration\QtInlineTargets.exe")
    if ($LASTEXITCODE -ne 0) { throw 'Qt sample runtime deployment failed.' }
}
if ($Test) {
    $originalPath = $env:PATH
    if ($QtRoot) { $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH }
    try {
        & (Join-Path (Split-Path $cmakePath) 'ctest.exe') --test-dir $buildDirectory -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
    } finally { $env:PATH = $originalPath }
}
if ($Run) { Start-Process -FilePath (Join-Path $buildDirectory "$Configuration\WindowStyler.exe") -Verb RunAs }
