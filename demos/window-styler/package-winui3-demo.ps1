param([string]$ReleaseDirectory=(Join-Path $PSScriptRoot 'out/vs-x64/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/WinUI3-framework-demo'))
$ErrorActionPreference='Stop'
$ReleaseDirectory=(Resolve-Path -LiteralPath $ReleaseDirectory).Path
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$registry=[IO.File]::ReadAllText((Join-Path $ReleaseDirectory 'adapters.ini'))
$section=[regex]::Match($registry,'(?ms)^\[Adapter266\]\r?\n(?<body>.*?)(?=^\[|\z)')
if(-not $section.Success){throw 'Build the optional WinUI 3 prototype first.'}
$backend=[regex]::Match($section.Groups['body'].Value,'(?m)^File=(.+)$').Groups[1].Value.Trim()
$hook=(Get-Content (Join-Path (Split-Path $ReleaseDirectory) 'runtime-Release.txt') | Where-Object {$_ -like 'MacdowsStyleHook64-*.dll'} | Select-Object -First 1).Trim()
foreach($name in @('WinUI3Demo.exe','WinUI3InlineTarget.exe','WindowStyleHost.exe','StylerTests.exe',$backend,$hook)){
 if($name -match '[\\/:]' -or -not $name){throw 'Unexpected runtime filename'}
 $source=Join-Path $ReleaseDirectory $name;$target=Join-Path $OutputDirectory $name
 if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash){continue}
 Copy-Item -LiteralPath $source -Destination $target -Force
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'adapters.ini'),"[Adapters]`r`nIds=266`r`n[Adapter266]`r`n"+$section.Groups['body'].Value,[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'test-winui3-inline.ps1') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'research/frameworks-v14.md') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'images/winui3-framework-prototype-v14.png') -Destination $OutputDirectory -Force
$readme=@'
# WinUI 3 原树演示

双击 WinUI3Demo.exe，程序会新建一个独立 WinUI 3 测试窗口，并通过进程内后端在原首行嵌入三灯。红灯关闭测试窗口，黄灯最小化，绿灯最大化/还原。关闭测试窗口后演示进程退出；结束演示进程后，目标内生命周期检查会恢复原布局。

需要本机已安装并注册 Windows App SDK 2 x64。当前仅核验 Microsoft.UI.Xaml.dll 3.2.3.0 的特定 CodeView。未知构建拒绝附加。

这是原型演示包。它仅启动并修改自有测试程序，不接管桌面现有应用。MFC/BCG、Office NetUI、TXGui 尚未完成适配。它不取代 WindowInjector-v13。

可复查测试：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\test-winui3-inline.ps1 -ReleaseDirectory . -OutputDirectory .\verification
```

测试包含 92 项断言，操作和关闭的均为新建 fixture。图示与框架研究状态见随包 frameworks-v14.md。
'@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'README.md'),$readme,[Text.UTF8Encoding]::new($true))
$evidence=Join-Path $OutputDirectory 'evidence';[void](New-Item -ItemType Directory -Force -Path $evidence)
$contract=Join-Path (Split-Path $ReleaseDirectory) 'winui3-contract'
foreach($name in @('result.json','checks.json','plain-geometry.json','binding-geometry.json','columns-geometry.json')){Copy-Item -LiteralPath (Join-Path $contract $name) -Destination $evidence -Force}
foreach($name in @('plain-before.png','plain-applied.png','binding-host-refresh.png','binding-restored.png')){Copy-Item -LiteralPath (Join-Path $contract $name) -Destination $evidence -Force}
foreach($name in @('ctest-x64-final.log','ctest-x86-final.log')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('out/live-v14/'+$name)) -Destination $evidence -Force}
$files=Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object Name -ne 'manifest.json' | ForEach-Object {[ordered]@{file=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}}
[ordered]@{kind='fixture-only WinUI 3 prototype';revision='5902747afe39';verified=$false;files=@($files)}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
Write-Output $OutputDirectory
