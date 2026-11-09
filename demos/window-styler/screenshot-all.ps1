param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'out\live-v10\final-screenshots'),
 [string]$ReleaseDirectory='', [string]$Hwnd,[switch]$CaptureUnsupported,[switch]$HostRefresh)
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=if(Test-Path (Join-Path $PSScriptRoot 'WindowAudit.exe')){$PSScriptRoot}else{Join-Path $PSScriptRoot 'out/build-x64/Release'}}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$inventoryPath=Join-Path $OutputDirectory 'inventory-screenshots.json'
& (Join-Path $ReleaseDirectory 'WindowAudit.exe') --direct-inventory $inventoryPath
$inventory=Get-Content -LiteralPath $inventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json
$outcomes=[Collections.Generic.List[object]]::new();$lines=[Collections.Generic.List[string]]::new()
$lines.Add('# 通用框架 · 逐窗口截图与刷新检查')
$lines.Add('使用原生屏幕复制采集实际合成画面，不使用 Computer Use。只尝试进程内适配器，不创建跟随窗口。未适配的程序明确列为 unsupported，不能计为通过。临时前置和调整截图窗口后恢复原始位置、状态和前台。')
# Native GDI composition can round a channel by one value. Match each ordered
# center pixel within 2/255; an unrelated background still fails.
function TestLights($colors,$capabilities){
 if($colors.Count -ne 3){return $false}
 $expected=@('FF5F57','FFBD2E','28C840')
 for($i=0;$i -lt 3;$i++){
  if($colors[$i] -in @('808080','D0D0CF') -and $capabilities.Count -eq 3 -and -not $capabilities[$i]){continue}
  for($c=0;$c -lt 6;$c+=2){if([Math]::Abs([Convert]::ToInt32($colors[$i].Substring($c,2),16)-[Convert]::ToInt32($expected[$i].Substring($c,2),16)) -gt 2){return $false}}
 }
 return $true
}
$index=0
foreach($window in $inventory.windows){
 if($Hwnd -and $window.hwnd -ne $Hwnd){continue}
 if(-not $window.eligible -or $window.class -eq 'Macdows.WindowStyler.Controller.v1'){
  $reason=if($window.class -eq 'Macdows.WindowStyler.Controller.v1'){'截图控制器自身'}else{$window.reason}
  $directory=''
  $excluded=$window.geometry.cloaked -ne 0 -or $reason -match '桌面、任务栏|工具 / 托盘|零面积辅助窗口|控制器自身|截图控制器自身'
  if($CaptureUnsupported -and -not $excluded){
   $index++;$directory=('baseline-{0:d2}-{1}' -f $index,$window.hwnd)
   try {& (Join-Path $PSScriptRoot 'screenshot-check.ps1') -Hwnd $window.hwnd -Name $directory -OutputDirectory $OutputDirectory -ReleaseDirectory $ReleaseDirectory -CaptureOnly}catch{$reason+="；原始画面采集失败：$($_.Exception.Message)"}
  }
  $excluded=$window.geometry.cloaked -ne 0 -or $reason -match '桌面、任务栏|工具 / 托盘|零面积辅助窗口|控制器自身|截图控制器自身'
  $outcomes.Add([pscustomobject]@{hwnd=$window.hwnd;title=$window.title;framework=$window.framework;runtime=$window.runtime;status=$(if($excluded){'excluded'}else{'unsupported'});reason=$reason;directory=$directory})
  $lines.Add("`n## $($window.title) ($($window.hwnd))`n`n框架：$($window.framework)，运行时：$($window.runtime)。未接管：$reason")
  if($directory){$lines.Add("[原始窗口画面]($directory/before.png)（未应用修改，不能计为适配通过）")}
  continue
 }
 $index++;$name=('app-{0:d2}-{1}' -f $index,$window.hwnd)
 try {
  & (Join-Path $PSScriptRoot 'screenshot-check.ps1') -Hwnd $window.hwnd -Name $name -OutputDirectory $OutputDirectory -ReleaseDirectory $ReleaseDirectory -HostRefresh:$HostRefresh
  $result=Get-Content -LiteralPath (Join-Path $OutputDirectory "$name\result.json") -Raw -Encoding UTF8 | ConvertFrom-Json
  $visible=TestLights $result.pixelLights $result.capabilities
  $firstVisible=TestLights $result.firstFrameLights $result.capabilities
  $inlineGeometry=($result.inlineKind -eq 0 -or ($result.outerPreserved -and $result.originalButtonsVisible -eq 0))
  $cycleVisible=if($result.capabilities[2]){(TestLights $result.maximizedLights $result.capabilities) -and (TestLights $result.unmaximizedLights $result.capabilities) -and $result.maximized -and $result.unmaximized}else{TestLights $result.unmaximizedLights $result.capabilities}
  $shape=$true
  if($result.attached -and $result.inlineKind -eq 0){
   foreach($phase in @('after','stable','unmaximized')){$f=$result.frames.$phase;if($f.regionType -ne 3 -or @($f.cornerInside|Where-Object {$_}).Count -ne 0 -or @($f.edgeInside|Where-Object {$_}).Count -ne 4){$shape=$false}}
   if($result.capabilities[2] -and ($result.frames.maximized.regionType -ne 2 -or @($result.frames.maximized.cornerInside|Where-Object {$_}).Count -ne 4)){$shape=$false}
  }
  if($result.attached -and $result.inlineKind -eq 6){$phases=@('after','stable','unmaximized');if($result.capabilities[2]){$phases+='maximized'};foreach($phase in $phases){if($result.frames.$phase.ncEnabled -ne $result.frames.before.ncEnabled -or $result.frames.$phase.regionType -ne $result.frames.before.regionType){$shape=$false}}}
  $rollbackFrame=$result.frames.before.ncEnabled -eq $result.frames.restored.ncEnabled -and $result.frames.before.cornerPreference -eq $result.frames.restored.cornerPreference -and $result.frames.before.style -eq $result.frames.restored.style -and $result.frames.before.regionType -eq $result.frames.restored.regionType
  $result|Add-Member -NotePropertyName shapeChecks -NotePropertyValue $shape
  $result|Add-Member -NotePropertyName frameRestored -NotePropertyValue $rollbackFrame
  $rollbackPixels=-not (TestLights $result.restoredLights $result.capabilities)
  $result|Add-Member -NotePropertyName buttonPixelsCleared -NotePropertyValue $rollbackPixels
  $originalInput=($result.inlineKind -notin @(0,5) -or $result.originalNativeButtonsAvailable -eq 0) -and ($result.inlineKind -notin @(1,2,3,4) -or $result.originalFrameworkButtonsEnabled -eq 0)
  $status=if(-not $result.attached){'unsupported-layout'}elseif($result.noLegacySurfaces -and $result.noNewChildHwnd -and $inlineGeometry -and $result.clientPreserved -and $result.restored -and $result.restoredGeometry -and $result.originalPlacementRestored -and $result.topmostRestored -and $visible -and $firstVisible -and $cycleVisible -and $shape -and $rollbackFrame -and $rollbackPixels -and $originalInput -and (-not $HostRefresh -or $result.refreshPassed)){'captured'}else{'failed'}
  $reason=if($status -eq 'unsupported-layout'){if($result.attachError){$result.attachError.Trim()}else{'未完成附加；运行时、布局及注入错误需按日志区分。'}}else{''}
  $outcomes.Add([pscustomobject]@{hwnd=$window.hwnd;title=$window.title;framework=$window.framework;runtime=$window.runtime;status=$status;reason=$reason;directory=$name;checks=$result})
  $lines.Add("`n## $($window.title) ($($window.hwnd))`n`n框架：$($window.framework)，运行时：$($window.runtime)。结果：$status；$reason 适配器 $($result.adapter)，内嵌类型 $($result.inlineKind)；客户区保持 $($result.clientPreserved)；外框保持 $($result.outerPreserved)；已还原 $($result.restoredGeometry)；应用后约 100ms 画面颜色 $($result.firstFrameLights -join ',')。`n`n[应用前]($name/before.png) · [首次截图]($name/after.png) · [稳定后]($name/stable.png) · [还原后]($name/restored.png)`n`n![应用前]($name/before-title.png)`n`n![应用后]($name/after-title.png)")
 }catch{
  $outcomes.Add([pscustomobject]@{hwnd=$window.hwnd;title=$window.title;status='failed';reason=$_.Exception.Message})
  $lines.Add("`n## $($window.title)`n`n检查失败：$($_.Exception.Message)")
 }
}
$failed=@($outcomes|Where-Object status -eq 'failed').Count;$captured=@($outcomes|Where-Object status -eq 'captured').Count;$unsupported=@($outcomes|Where-Object {$_.status -like 'unsupported*'}).Count;$excluded=@($outcomes|Where-Object status -eq 'excluded').Count
[pscustomobject]@{windows=$outcomes;captured=$captured;failed=$failed;unsupported=$unsupported;excluded=$excluded;integrity=$inventory.controllerIntegrity} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.json') -Encoding UTF8
$lines -join "`n`n" | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.md') -Encoding UTF8
"直接注入截图通过 $captured；失败 $failed；未适配 $unsupported；排除系统/后台窗口 $excluded（未计为通过）。" | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.txt') -Encoding UTF8
if($failed){exit 1}
