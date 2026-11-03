param(
    [Parameter(Mandatory = $true)][string]$Hwnd,
    [string]$Probe = (Join-Path $PSScriptRoot '..\out\framework-probe-Win32\probe\Release\FrameworkResearchHost.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\live-v15\txgui-contract'),
    [switch]$Active
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'txgui-tree-check.ps1')
if ($Active -and -not $PSBoundParameters.ContainsKey('OutputDirectory')) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\out\live-v21\txgui-readonly'
}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FrameworkReadOnlyContract {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    public delegate bool Enumerate(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetPropW(IntPtr window, string name);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr window, int slot);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr window, Enumerate callback, IntPtr data);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr w,
        IntPtr l, uint flags, uint timeout, out UIntPtr result);
    public static string Geometry(IntPtr window) {
        Rect outer, client;
        GetWindowRect(window, out outer); GetClientRect(window, out client);
        return string.Join(",", outer.left, outer.top, outer.right, outer.bottom, client.left, client.top,
            client.right, client.bottom, GetWindowLongPtrW(window,-16), GetWindowLongPtrW(window,-20),
            IsIconic(window), IsZoomed(window));
    }
    public static int Children(IntPtr window) {
        int count=0; EnumChildWindows(window,(child,data)=>{count++;return true;},IntPtr.Zero);return count;
    }
    public static bool Responding(IntPtr window) {
        UIntPtr result;return SendMessageTimeoutW(window,0,IntPtr.Zero,IntPtr.Zero,2,1500,out result)!=IntPtr.Zero;
    }
}
'@
$window = [IntPtr][Convert]::ToInt64($Hwnd.Replace('0x', ''), 16)
if (-not [FrameworkReadOnlyContract]::IsWindow($window)) { throw 'The target HWND no longer exists.' }
[uint32]$processId = 0
[void][FrameworkReadOnlyContract]::GetWindowThreadProcessId($window, [ref]$processId)
$before = [FrameworkReadOnlyContract]::Geometry($window)
$children = [FrameworkReadOnlyContract]::Children($window)
$reportPath = Join-Path (Split-Path $Probe) ('txgui-readonly-' + $processId + '.json')
$previousWrite = if (Test-Path -LiteralPath $reportPath) { (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc } else { [DateTime]::MinValue }
if ($Active) { & $Probe $Hwnd --active } else { & $Probe $Hwnd }
if ($LASTEXITCODE -ne 0) { throw 'The read-only probe failed.' }
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$treeValidation = Get-TxGuiTreeValidation $report
$checks = [ordered]@{
    freshReport = (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -gt $previousWrite
    rootFound = $report.rootFound -eq $true
    enumerationSucceeded = $report.hresult -eq 0 -and $report.nodes -gt 0
    nativeTraversalComplete = $report.treeComplete -eq $true
    allDeclaredChildrenAndIdentitiesRead = $treeValidation.passed
    explicitlyReadOnly = $report.layoutModified -eq $false
    processResponding = [FrameworkReadOnlyContract]::Responding($window)
    geometryPreserved = $before -eq [FrameworkReadOnlyContract]::Geometry($window)
    childWindowsPreserved = $children -eq [FrameworkReadOnlyContract]::Children($window)
    noOwnership = [FrameworkReadOnlyContract]::GetPropW($window, 'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero
    noInlineSession = [FrameworkReadOnlyContract]::GetPropW($window, 'Macdows.WindowStyler.Inline.v4') -eq [IntPtr]::Zero
    noRequest = [FrameworkReadOnlyContract]::GetPropW($window, 'Macdows.WindowStyler.Request.v1') -eq [IntPtr]::Zero
    noAdapterState = [FrameworkReadOnlyContract]::GetPropW($window, 'Macdows.WindowStyler.Adapter.v1') -eq [IntPtr]::Zero
}
if ($Active) {
    $frameNodes = [Collections.Generic.Dictionary[long,object]]::new()
    function Index-Frame($node) {
        if ($null -eq $node) { return }
        $frameNodes[[long]$node.identity] = $node
        foreach ($child in $node.children) { Index-Frame $child }
    }
    Index-Frame $report.tree
    $samples = @($report.captionHitSamples)
    $checks.hitProfileVerified = $report.captionHitProfileVerified -eq $true
    $checks.twentyOneNativeHitQueries = $samples.Count -eq 21
    $checks.nativeHitQueriesSucceed = @($samples | Where-Object { $_.hresult -ne 0 -or -not $_.identity }).Count -eq 0
    $missing = @($samples | Where-Object { -not $frameNodes.ContainsKey([long]$_.identity) })
    $checks.hitIdentitiesBelongToOriginalTree = $missing.Count -eq 0
    $badGeometry = @($samples | Where-Object {
        if (-not $frameNodes.ContainsKey([long]$_.identity)) { return $true }
        $r = $frameNodes[[long]$_.identity].screenRect
        $p = $_.screenPoint
        return $r.Count -ne 4 -or $p[0] -lt $r[0] -or $p[0] -ge $r[2] -or $p[1] -lt $r[1] -or $p[1] -ge $r[3]
    })
    $checks.hitPointsAreInsideValidOriginalRectangles = $badGeometry.Count -eq 0
    $topButtons = @($samples | Select-Object -First 7 | Where-Object frameworkType -eq 'IGFButton' | Select-Object -ExpandProperty identity -Unique)
    $checks.threeDistinctTopRowButtonIdentities = $topButtons.Count -eq 3
    $buttons = @($frameNodes.Values | Where-Object frameworkType -eq 'IGFButton')
    $checks.originalButtonEventProfilesVerified = $buttons.Count -gt 0 -and @($buttons | Where-Object { -not $_.eventBindings.profileVerified }).Count -eq 0
    $checks.originalButtonEventContainersComplete = @($buttons | Where-Object { -not $_.eventBindings.complete -or $_.eventBindings.errors -ne 0 }).Count -eq 0
    $checks.repeatedEventInspectionPreservesBindings = @($buttons | Where-Object { -not $_.eventBindings.stableRead }).Count -eq 0
    $topBindings = @($topButtons | ForEach-Object { $frameNodes[[long]$_].eventBindings.numeric | Where-Object id -eq 4099 })
    $checks.threeOriginalCaptionClickBindings = $topBindings.Count -eq 3
    $checks.captionClicksShareOneOriginalNotifier = @($topBindings | Select-Object -ExpandProperty sink -Unique).Count -eq 1
    $originalHints=@($samples | Select-Object -First 7 | Where-Object frameworkType -eq 'IGFButton' |
        Group-Object identity | ForEach-Object { $_.Group[0] })
    $checks.originalHintGetterProfilesVerified = $originalHints.Count -eq 3 -and @($originalHints | Where-Object { -not $_.tooltipProfileVerified }).Count -eq 0
    $checks.originalHintReadsSucceed = @($originalHints | Where-Object { $_.tooltipHresult -ne 0 }).Count -eq 0
    $checks.originalHintsProvideThreeDistinctIntents = (($originalHints.tooltipIntent | Sort-Object) -join ',') -ceq 'close-intent,maximize-or-restore-intent,minimize-intent'
}
$passed = -not @($checks.Values | Where-Object { -not $_ }).Count
[ordered]@{passed=$passed;pid=$processId;nodes=$report.nodes;checks=$checks;geometry=$before} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $OutputDirectory 'tree.json') -Force
if (-not $passed) { throw 'A read-only contract check failed; inspect result.json.' }
"TXGui read-only contract: $($checks.Count)/$($checks.Count) passed."
