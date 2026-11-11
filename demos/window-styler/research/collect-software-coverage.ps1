param([string]$OutputDirectory,[string]$RuntimeDirectory,[switch]$SoftwareOnly)
$ErrorActionPreference='Stop'
if(-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '..\out\live-v33\inventory' }
if(-not $RuntimeDirectory) { $RuntimeDirectory=Join-Path $PSScriptRoot '..\out\vs-auto-x64\Release' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$uninstall=@('HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*')
$installed=@(Get-ItemProperty $uninstall -ErrorAction SilentlyContinue |
    Where-Object {$_.DisplayName -and -not $_.SystemComponent} |
    Sort-Object DisplayName -Unique | Select-Object DisplayName,DisplayVersion,Publisher,InstallLocation,DisplayIcon)
$startEntries=@(Get-StartApps | Sort-Object Name | Select-Object Name,AppID)
$packages=@(Get-AppxPackage | Where-Object {-not $_.IsFramework -and -not $_.IsResourcePackage} |
    Select-Object Name,PackageFullName,Architecture,InstallLocation)
$shortcuts=@()
$shell=New-Object -ComObject WScript.Shell
foreach($root in @([Environment]::GetFolderPath('StartMenu'),[Environment]::GetFolderPath('CommonStartMenu'))) {
    foreach($file in @(Get-ChildItem -LiteralPath $root -Filter '*.lnk' -File -Recurse -ErrorAction SilentlyContinue)) {
        try {
            $shortcut=$shell.CreateShortcut($file.FullName)
            $shortcuts+=[pscustomobject]@{name=$file.BaseName;target=$shortcut.TargetPath;arguments=$shortcut.Arguments}
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut)
        } catch { Write-Warning ('Cannot read shortcut: '+$file.FullName) }
    }
}
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
$software=[pscustomobject]@{capturedAt=(Get-Date).ToString('o');installed=$installed;startEntries=$startEntries;packages=$packages;shortcuts=$shortcuts}
$software | ConvertTo-Json -Depth 6 | Out-File (Join-Path $OutputDirectory 'software.json') -Encoding utf8
if($SoftwareOnly) {
    # Installed-software discovery needs no elevation or target-process access.
    # Keep unavailable window evidence unknown, never count it as zero or pass.
    $summary=[pscustomobject]@{installedRecords=$installed.Count;startEntries=$startEntries.Count;packages=$packages.Count;shortcuts=$shortcuts.Count;
        windows=$null;candidateWindows=$null;windowInventorySkipped=$true;
        note='Software-only discovery; no window, injection, layout, or theme verification was performed.'}
    $summary | ConvertTo-Json | Out-File (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
    $summary
    return
}
$audit=(Resolve-Path (Join-Path $RuntimeDirectory 'WindowAudit.exe')).Path
$inventoryPath=Join-Path $OutputDirectory 'windows.json'
$arguments='--direct-inventory "'+$inventoryPath+'"'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $process=Start-Process -FilePath $audit -ArgumentList $arguments -WindowStyle Hidden -PassThru
} else {
    $process=Start-Process -FilePath $audit -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru
}
if(-not $process.WaitForExit(30000)) { throw 'Read-only window inventory timed out; its process was not terminated.' }
if($process.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $inventoryPath)) { throw 'Window inventory failed.' }
$windows=Get-Content -LiteralPath $inventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json
$summary=[pscustomobject]@{installedRecords=$installed.Count;startEntries=$startEntries.Count;packages=$packages.Count;shortcuts=$shortcuts.Count;
    windows=@($windows.windows).Count;candidateWindows=@($windows.windows | Where-Object eligible).Count;
    note='Inventory is not an adaptation pass. Each launched window and each actual theme/layout still needs verification.'}
$summary | ConvertTo-Json | Out-File (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
$summary
