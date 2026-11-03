param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'out\WindowInjector-v13'))
$ErrorActionPreference='Stop'
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
function CopyRuntime([string]$source,[string]$directory){
 $target=Join-Path $directory ([IO.Path]::GetFileName($source))
 # A restored target still pins its injection DLL. Identical files need no
 # overwrite; changed source revisions have a different content-addressed name.
 if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash){return}
 Copy-Item -LiteralPath $source -Destination $target -Force
}
function CopyBuild([string]$architecture,[string]$destination){
 [void](New-Item -ItemType Directory -Path $destination -Force)
 $build=Join-Path $PSScriptRoot "out\vs-$architecture";$release=Join-Path $build 'Release'
 foreach($file in @('WindowInjector.exe','WindowStyler.exe','WindowStyleHost.exe','StyleTarget.exe','WindowAudit.exe','StylerTests.exe','adapters.ini')){CopyRuntime (Join-Path $release $file) $destination}
 foreach($file in @('screenshot-check.ps1','screenshot-all.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $destination -Force}
 foreach($file in Get-Content (Join-Path $build 'runtime-Release.txt')){if($file){CopyRuntime (Join-Path $release $file) $destination}}
 Copy-Item -LiteralPath (Join-Path $build 'wpf-target\WpfInlineTarget.exe') -Destination $destination -Force
}
CopyBuild 'x64' $OutputDirectory
CopyBuild 'Win32' (Join-Path $OutputDirectory 'x86')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'out\vs-x64\wpf-target-core') -Destination $OutputDirectory -Recurse -Force
$release=Join-Path $PSScriptRoot 'out\vs-x64\Release'
Copy-Item -LiteralPath (Join-Path $release 'QtInlineTargets.exe') -Destination $OutputDirectory -Force
Get-ChildItem -LiteralPath $release -File | Where-Object {$_.Name -like 'Qt6*.dll' -or $_.Name -eq 'D3Dcompiler_47.dll' -or $_.Name -eq 'qt.conf'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Force}
foreach($folder in @('qml','platforms','styles','imageformats','iconengines','generic','networkinformation','tls')){if(Test-Path -LiteralPath (Join-Path $release $folder)){Copy-Item -LiteralPath (Join-Path $release $folder) -Destination $OutputDirectory -Recurse -Force}}
foreach($file in @('README.md','compatibility-v11-2026-10-31.md','CMakeLists.txt','build.ps1','session.cpp','session.h','inline.cpp','inline.h','hook.cpp','adapter-registry.cpp','adapter-registry.h','framework.cpp','framework.h','module-capabilities.h','applicationframe-inline.cpp','uwp-inset.cpp','embedded-frame.h','test-embedded-views.ps1','compatibility-v8-2026-10-31.md','compatibility-v9-2026-10-31.md','compatibility-v10-2026-10-31.md','xaml-abi.h','xaml-roots.h','xaml-inline.cpp','xaml-inline-target.cpp','pe-identity.h','adapter-api.h','protocol.h','chrome.h','chrome.cpp','nonclient-frame.h','native-style-view.h','test-netui-leases.ps1','test-controller-direct.ps1','test-live-input.ps1','test-taskmanager-views.ps1','qt-inline.cpp','wpf-inline.cs','tests.cpp','target.cpp')){if(Test-Path -LiteralPath (Join-Path $PSScriptRoot $file)){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $OutputDirectory -Force}}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'sdk') -Destination $OutputDirectory -Recurse -Force
# Keep the framework backend and its dependencies reviewable in the package.
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object {$_.Extension -in @('.cpp','.h','.cs','.js','.qml','.manifest','.ps1','.def')} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Force}
foreach($folder in @('wpf','wpf-inline-core','wpf-target-core')){
 $source=Join-Path $PSScriptRoot $folder
 if(Test-Path -LiteralPath $source){Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {$_.FullName -notmatch '\\(bin|obj)\\' -and $_.Extension -in @('.cs','.csproj','.xaml','.json')} | ForEach-Object {
  $relative=$_.FullName.Substring($source.Length+1);$target=Join-Path (Join-Path $OutputDirectory $folder) $relative
  [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force);Copy-Item -LiteralPath $_.FullName -Destination $target -Force
 }}
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'compatibility-v12-2026-11-01.md') -Destination $OutputDirectory -Force
[void](New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'images') -Force)
foreach($image in @('window-injector-v12.png','window-injector-v11.png','window-injector-v10.png','window-injector-v9.png','window-injector-v8.png','window-frame-v8.png')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('images\'+$image)) -Destination (Join-Path $OutputDirectory 'images') -Force}
Copy-Item -LiteralPath (Join-Path $release 'XamlInlineTarget.exe') -Destination $OutputDirectory -Force
# Experimental injection probes are deliberately excluded from the distribution.
$researchTarget=Join-Path $OutputDirectory 'research'
[void](New-Item -ItemType Directory -Path $researchTarget -Force)
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'research') -File | Where-Object {$_.Extension -in @('.md','.py')} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $researchTarget -Force}
# Include the latest verification records when they exist in this checkout.
$evidence=Join-Path $OutputDirectory 'evidence'
[void](New-Item -ItemType Directory -Path $evidence -Force)
foreach($name in @('release-screenshots','input-final','taskmanager-views','replay-v9','replay-v10','views-v9','netui-final')){
 $source=Join-Path $PSScriptRoot ('out/live-v10/'+$name)
 if(Test-Path -LiteralPath $source){$target=Join-Path $evidence $name;[void](New-Item -ItemType Directory -Path $target -Force);Get-ChildItem -LiteralPath $source|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force}}
}
foreach($name in @('ctest-x64-final.log','ctest-x86-final.log','startup.json','running-inventory.json','title-contact.png','window-contact.png','taskmanager-views.png','controlpanel-native-behavior.json','controller-liveness.json','final-checks.json')){
 $source=Join-Path $PSScriptRoot ('out/live-v10/'+$name)
 if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $evidence -Force}
}
$controllerRecords=Join-Path $PSScriptRoot 'out/controller-direct-v10'
if(Test-Path -LiteralPath $controllerRecords){$target=Join-Path $evidence 'controller';[void](New-Item -ItemType Directory -Path $target -Force);Get-ChildItem -LiteralPath $controllerRecords|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force}}
foreach($name in @('README.md','compatibility-v10-2026-10-31.md')){
 $path=Join-Path $OutputDirectory $name;$body=[IO.File]::ReadAllText($path,[Text.Encoding]::UTF8)
 $body=$body.Replace('out/live-v10/release-screenshots','evidence/release-screenshots').Replace('out/live-v10/input-final','evidence/input-final').Replace('out/live-v10/ctest-x64-final.log','evidence/ctest-x64-final.log').Replace('out/live-v10/startup.json','evidence/startup.json').Replace('out/controller-direct-v10/checks.json','evidence/controller/checks.json')
 $body=$body.Replace('out/live-v10/','evidence/')
 [IO.File]::WriteAllText($path,$body,[Text.UTF8Encoding]::new($true))
}
$thirdParty=Join-Path $OutputDirectory 'third_party'
[void](New-Item -ItemType Directory -Path $thirdParty -Force)
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party/minhook') -Destination $thirdParty -Recurse -Force
foreach($name in @('input-final','input-all-final','views-final','release-screenshots')){
 $source=Join-Path $PSScriptRoot ('out/live-v11/'+$name)
 if(Test-Path -LiteralPath $source){$target=Join-Path $evidence ('v11/'+$name);[void](New-Item -ItemType Directory -Path $target -Force);Get-ChildItem -LiteralPath $source|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force}}
}
foreach($name in @('ctest-x64-final.log','ctest-x86-final.log','edge-symbol-check.json','startup.json','running-inventory.json','controller-liveness.json')){
 $source=Join-Path $PSScriptRoot ('out/live-v11/'+$name)
 if(Test-Path -LiteralPath $source){$target=Join-Path $evidence 'v11';[void](New-Item -ItemType Directory -Path $target -Force);Copy-Item -LiteralPath $source -Destination $target -Force}
}
$controllerRecords=Join-Path $PSScriptRoot 'out/controller-direct-v11'
if(Test-Path -LiteralPath $controllerRecords){$target=Join-Path $evidence 'v11/controller';[void](New-Item -ItemType Directory -Path $target -Force);Copy-Item -LiteralPath (Join-Path $controllerRecords 'checks.json') -Destination $target -Force}
$doc=Join-Path $OutputDirectory 'compatibility-v11-2026-10-31.md'
$docBody=[IO.File]::ReadAllText($doc,[Text.Encoding]::UTF8).Replace('out/live-v11/settings-final/','evidence/v11/release-screenshots/settings/').Replace('out/live-v11/','evidence/v11/')
[IO.File]::WriteAllText($doc,$docBody,[Text.UTF8Encoding]::new($true))
foreach($name in @('edge-release','bootstrap-input','package-verified','edge-final','input-diagnostics','input-verified','input-final','input-retry','input-settled','package-input','package-diagnostic-2','origin-input')){
 $source=Join-Path $PSScriptRoot ('out/live-v12/'+$name)
 if(Test-Path -LiteralPath $source){
  $target=Join-Path $evidence ('v12/'+$name);[void](New-Item -ItemType Directory -Path $target -Force)
  # Only title crops and metrics from user windows enter the distribution.
  Get-ChildItem -LiteralPath $source -File | Where-Object {$_.Extension -in @('.json','.txt') -or ($_.Extension -eq '.png' -and ($name -notin @('edge-final','edge-release') -or $_.Name -like '*-title.png'))} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $target -Force}
 }
}
foreach($name in @('build-bootstrap.log','build-x86-bootstrap.log','ctest-x64-bootstrap.log','ctest-x86-bootstrap.log','bootstrap-test.log','bootstrap-input.log','edge-release.log','package-verified.log','internal-hook-probe.json','hook-health.json','hook-path-probe-2.json','package-ctest-x64.log','package-ctest-x86.log','package-wpf-x86.log','package-checks.json','build-final.log','build-x86-final.log','ctest-x64-final.log','ctest-x86-final.log','edge-final.log','input-verified.log','input-diagnostics.log','input-final.log','input-retry.log','input-settled.log','package-input.log','startup.json')){
 $source=Join-Path $PSScriptRoot ('out/live-v12/'+$name)
 if(Test-Path -LiteralPath $source){$target=Join-Path $evidence 'v12';[void](New-Item -ItemType Directory -Path $target -Force);Copy-Item -LiteralPath $source -Destination $target -Force}
}
foreach($name in @('README.md','compatibility-v12-2026-11-01.md')){
 $doc=Join-Path $OutputDirectory $name
 $docBody=[IO.File]::ReadAllText($doc,[Text.Encoding]::UTF8).Replace('out/live-v12/','evidence/v12/').Replace('out/WindowInjector-v12.zip','../WindowInjector-v12.zip')
 [IO.File]::WriteAllText($doc,$docBody,[Text.UTF8Encoding]::new($true))
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'compatibility-v13-2026-11-01.md') -Destination $OutputDirectory -Force
foreach($name in @('window-injector-v13.png','window-desktop-v13.png')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('images/'+$name)) -Destination (Join-Path $OutputDirectory 'images') -Force}
$v13=Join-Path $evidence 'v13';[void](New-Item -ItemType Directory -Path $v13 -Force)
foreach($name in @('chrome-desktop-input5','edge-regression5','chrome-package-final','edge-package-final','taskmanager-views-replay-final','chromium-fixture-test5','chrome-modes-repaired','edge-modes-repaired','final-desktop')){
 $source=Join-Path $PSScriptRoot ('out/live-v13/'+$name)
 if(Test-Path -LiteralPath $source){
  foreach($file in Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {$_.Extension -in @('.json','.txt','.md') -or ($_.Extension -eq '.png' -and ($name -notin @('final-desktop','taskmanager-views-replay-final') -or $_.Name -like '*-title.png'))}){
   $relative=$file.FullName.Substring($source.Length+1);$target=Join-Path (Join-Path $v13 $name) $relative
   [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force);Copy-Item -LiteralPath $file.FullName -Destination $target -Force
  }
 }
}
foreach($name in @('scope-report.json','winui3-experimental-failures.json','ctest-x64-final.log','ctest-x86-final.log')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('out/live-v13/'+$name)) -Destination $v13 -Force}
foreach($name in @('README.md','compatibility-v13-2026-11-01.md')){
 $path=Join-Path $OutputDirectory $name;$body=[IO.File]::ReadAllText($path)
 [IO.File]::WriteAllText($path,$body.Replace('out/live-v13/','evidence/v13/'),[Text.UTF8Encoding]::new($true))
}
Write-Output $OutputDirectory
