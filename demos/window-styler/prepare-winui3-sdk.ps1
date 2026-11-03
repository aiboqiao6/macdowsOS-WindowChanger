param([string]$RuntimeDirectory='',
 [string]$WebView2Metadata=(Join-Path $PSScriptRoot 'out/webview2-sdk/lib/Microsoft.Web.WebView2.Core.winmd'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/winui3-sdk'),[switch]$PlanOnly)
$ErrorActionPreference='Stop'
if(-not $RuntimeDirectory){
 $package=Get-AppxPackage -Name Microsoft.WindowsAppRuntime.2 | Where-Object Architecture -eq X64 | Sort-Object Version -Descending | Select-Object -First 1
 if(-not $package){throw 'Install a Windows App SDK 2 runtime before generating this optional research SDK.'}
 $RuntimeDirectory=$package.InstallLocation
}
$RuntimeDirectory=(Resolve-Path -LiteralPath $RuntimeDirectory).Path
foreach($name in @('Microsoft.UI.Xaml.winmd','Microsoft.UI.winmd')){if(-not(Test-Path -LiteralPath (Join-Path $RuntimeDirectory $name))){throw "Runtime metadata is missing: $name"}}
$WebView2Metadata=(Resolve-Path -LiteralPath $WebView2Metadata).Path
$kits=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk=Get-ChildItem -LiteralPath (Join-Path $kits 'UnionMetadata') -Directory | Where-Object {$_.Name -match '^\d+\.\d+\.\d+\.\d+$'} | Sort-Object {[version]$_.Name} -Descending | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'Windows.winmd')} | Select-Object -First 1
if(-not $sdk){throw 'Windows SDK UnionMetadata was not found.'}
$cppwinrt=Join-Path $kits ('bin/'+$sdk.Name+'/x64/cppwinrt.exe')
if(-not(Test-Path -LiteralPath $cppwinrt)){throw "cppwinrt was not found: $cppwinrt"}
$arguments=@('-input',(Join-Path $sdk.FullName 'Windows.winmd'),'-input',$RuntimeDirectory,'-input',$WebView2Metadata,'-output',$OutputDirectory)
if($PlanOnly){[ordered]@{compiler=$cppwinrt;arguments=$arguments}|ConvertTo-Json -Depth 4;return}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
& $cppwinrt @arguments
if($LASTEXITCODE -ne 0){throw "cppwinrt failed: $LASTEXITCODE"}
if(-not(Test-Path -LiteralPath (Join-Path $OutputDirectory 'winrt/Microsoft.UI.Xaml.h'))){throw 'The generated WinUI 3 projection is missing.'}
Write-Output "Generated WinUI 3 projection: $OutputDirectory"
