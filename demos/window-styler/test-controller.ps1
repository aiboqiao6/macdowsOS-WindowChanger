param(
    [string]$ReleaseDirectory = (Join-Path $PSScriptRoot 'out\build-x64\Release'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'out\controller-audit-v3')
)
$ErrorActionPreference = 'Stop'
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this integration check as administrator.' }
$ReleaseDirectory = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ControllerProbe {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint message, UIntPtr param,
        IntPtr data, uint flags, uint timeout, out UIntPtr result);
    public static IntPtr[] Windows(uint pid) {
        var windows = new List<IntPtr>();
        EnumWindows((h,p) => { uint current; GetWindowThreadProcessId(h, out current);
            if (pid == current) windows.Add(h); return true; }, IntPtr.Zero);
        return windows.ToArray();
    }
    public static bool Send(IntPtr h, uint message, uint param) {
        UIntPtr result; return SendMessageTimeout(h, message, new UIntPtr(param), IntPtr.Zero, 2, 3000, out result) != IntPtr.Zero;
    }
}
'@
$checks = [Collections.Generic.List[object]]::new()
$samples = [Collections.Generic.List[Diagnostics.Process]]::new()
$controller = $null
$controllerWindow = [IntPtr]::Zero
function Check([string]$Name, [bool]$Ok) {
    $checks.Add([pscustomobject]@{name=$Name;ok=$Ok})
    Write-Output ((@('FAIL ', 'PASS '))[[int]$Ok] + $Name)
}
function WindowHandle($Entry) { return [IntPtr]([Convert]::ToInt64($Entry.hwnd.Substring(2),16)) }
function Inventory([string]$Name) {
    $path = Join-Path $OutputDirectory ($Name + '.json')
    & (Join-Path $ReleaseDirectory 'WindowAudit.exe') --inventory $path
    if ($LASTEXITCODE -ne 0) { throw 'Inventory process failed' }
    return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json)
}
function Wait-Until([scriptblock]$Condition, [int]$Timeout = 8000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($Timeout)
    do { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $deadline)
    return [bool](& $Condition)
}
function Owned($Entry) { return [ControllerProbe]::GetProp((WindowHandle $Entry), 'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero }
function Samples($Process) {
    return @([ControllerProbe]::Windows($Process.Id) | Where-Object {
        [ControllerProbe]::GetProp($_, 'Macdows.WindowStyler.Sample') -ne [IntPtr]::Zero
    })
}
function New-Sample([string]$Arguments = '') {
    $options = @{FilePath=(Join-Path $ReleaseDirectory 'StyleTarget.exe');PassThru=$true}
    if ($Arguments) { $options.ArgumentList=$Arguments }
    $sample = Start-Process @options
    $samples.Add($sample)
    return $sample
}
try {
    if (Get-Process WindowStyler -ErrorAction SilentlyContinue) { throw 'Close the existing controller before this check.' }
    $first = New-Sample
    if (-not (Wait-Until { (Samples $first).Count -eq 1 })) { throw 'First sample did not initialize' }
    $firstWindow = (Samples $first)[0]
    Start-Sleep -Milliseconds 400
    $before = Inventory 'before'
    Check 'Harness runs with high integrity' ($before.controllerIntegrity -eq 'high')
    $eligible = @($before.windows | Where-Object eligible)
    $controller = Start-Process -FilePath (Join-Path $ReleaseDirectory 'WindowStyler.exe') -PassThru
    if (-not (Wait-Until { $controller.Refresh(); $controller.MainWindowHandle -ne [IntPtr]::Zero })) { throw 'Controller did not initialize' }
    $controllerWindow = $controller.MainWindowHandle
    Check 'Automatic takeover is enabled at startup' ([ControllerProbe]::GetProp($controllerWindow, 'Macdows.WindowStyler.AutoManage.v2').ToInt64() -eq 1)
    Check 'All eligible existing application windows are enrolled' (Wait-Until { @($eligible | Where-Object { -not (Owned $_) }).Count -eq 0 })
    $active = Inventory 'active'
    $ui = $active.windows | Where-Object pid -eq $controller.Id
    Check 'Controller has high integrity' ($ui.integrity -eq 'high')
    Check 'Managed count matches current eligible baseline' (Wait-Until { [ControllerProbe]::GetProp($controllerWindow, 'Macdows.WindowStyler.ManagedCount.v2').ToInt64() -eq $eligible.Count })
    foreach ($entry in $eligible) {
        $now = $active.windows | Where-Object { $_.hwnd -eq $entry.hwnd -and $_.pid -eq $entry.pid }
        $same = $null -ne $now
        foreach ($key in @('client','minimized','maximized','style','exStyle')) {
            $same = $same -and (($entry.geometry.$key | ConvertTo-Json -Compress) -eq ($now.geometry.$key | ConvertTo-Json -Compress))
        }
        Check ('Automatic takeover retains client size/state/styles: ' + $entry.hwnd) $same
    }
    [void][ControllerProbe]::Send($controllerWindow, 0x111, 108)
    Check 'Pause is reflected immediately' ([ControllerProbe]::GetProp($controllerWindow, 'Macdows.WindowStyler.AutoManage.v2') -eq [IntPtr]::Zero)
    $second = New-Sample
    if (-not (Wait-Until { (Samples $second).Count -eq 1 })) { throw 'Second sample did not initialize' }
    $secondWindow = (Samples $second)[0]
    Start-Sleep -Milliseconds 2300
    Check 'New window remains untouched while paused' ([ControllerProbe]::GetProp($secondWindow, 'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero)
    [void][ControllerProbe]::Send($controllerWindow, 0x111, 108)
    Check 'Resume enrolls the new window' (Wait-Until { [ControllerProbe]::GetProp($secondWindow, 'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero })
    [void][ControllerProbe]::Send($controllerWindow, 0x111, 104)
    Start-Sleep -Milliseconds 2300
    Check 'Selected restore is not immediately enrolled again' ([ControllerProbe]::GetProp($firstWindow, 'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero)
    Check 'Selected restore preserves other leases' ([ControllerProbe]::GetProp($secondWindow, 'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero)
    [void][ControllerProbe]::Send($controllerWindow, 0x111, 105)
    Start-Sleep -Milliseconds 1500
    Check 'Restore all pauses automatic takeover' ([ControllerProbe]::GetProp($controllerWindow, 'Macdows.WindowStyler.AutoManage.v2') -eq [IntPtr]::Zero)
    $restored = Inventory 'restored'
    foreach ($entry in $eligible) {
        $now = $restored.windows | Where-Object { $_.hwnd -eq $entry.hwnd -and $_.pid -eq $entry.pid }
        Check ('Restore all exactly restores original snapshot: ' + $entry.hwnd) ((($entry.geometry | Select-Object * -ExcludeProperty hookModules) | ConvertTo-Json -Compress) -eq (($now.geometry | Select-Object * -ExcludeProperty hookModules) | ConvertTo-Json -Compress))
        Check ('Restore all clears ownership: ' + $entry.hwnd) (-not (Owned $entry))
    }
    $variants = New-Sample '--variants'
    Check 'Five real window models initialize' (Wait-Until { (Samples $variants).Count -eq 5 })
    [void][ControllerProbe]::Send($controllerWindow, 0x111, 108)
    Check 'Resume manages all five newly created models' (Wait-Until { @((Samples $variants) | Where-Object { [ControllerProbe]::GetProp($_,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero }).Count -eq 0 })
    [void][ControllerProbe]::Send($controllerWindow, 0x10, 0)
    Check 'Controller closes normally' ($controller.WaitForExit(6000))
    $closed = Inventory 'closed'
    foreach ($entry in $eligible) {
        $now = $closed.windows | Where-Object { $_.hwnd -eq $entry.hwnd -and $_.pid -eq $entry.pid }
        Check ('Normal exit restores original snapshot: ' + $entry.hwnd) ((($entry.geometry | Select-Object * -ExcludeProperty hookModules) | ConvertTo-Json -Compress) -eq (($now.geometry | Select-Object * -ExcludeProperty hookModules) | ConvertTo-Json -Compress))
        Check ('Normal exit clears ownership: ' + $entry.hwnd) (-not (Owned $entry))
    }
} catch {
    Check ('Exception: ' + $_.Exception.Message) $false
} finally {
    if ($controllerWindow -ne [IntPtr]::Zero -and $controller -and -not $controller.HasExited) {
        [void][ControllerProbe]::Send($controllerWindow, 0x10, 0)
        [void]$controller.WaitForExit(6000)
    }
    foreach ($sample in $samples) {
        foreach ($window in (Samples $sample)) { [void][ControllerProbe]::Send($window, 0x10, 0) }
        [void]$sample.WaitForExit(4000)
    }
    $result = [pscustomobject]@{checks=$checks;passed=@($checks | Where-Object ok).Count;failed=@($checks | Where-Object { -not $_.ok }).Count}
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
}
if ($result.failed) { exit 1 }
