param([string]$PackageDirectory,[string]$OutputDirectory,[string]$BuildPrefix='vs-auto')
$ErrorActionPreference='Stop'
if (-not $PackageDirectory) { $PackageDirectory=Join-Path $PSScriptRoot '..\out\WindowChanger-v32' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '..\out\live-v32\auto-all' }
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator verification is required.' }
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ExistingController {
 public delegate bool Callback(IntPtr w,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback callback,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetPropW(IntPtr w,string name);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr w);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessageTimeoutW(IntPtr w,uint m,UIntPtr p,IntPtr l,uint flags,uint ms,out UIntPtr r);
 public static IntPtr[] Find(uint pid) { var result=new List<IntPtr>();EnumWindows((w,p)=>{uint actual;GetWindowThreadProcessId(w,out actual);if(actual==pid&&GetPropW(w,"Macdows.WindowChanger.Primary.v3")!=IntPtr.Zero)result.Add(w);return true;},IntPtr.Zero);return result.ToArray(); }
 public static bool Automatic(IntPtr w) {return GetPropW(w,"Macdows.WindowStyler.AutoManage.v2")!=IntPtr.Zero;}
 public static void Toggle(IntPtr w) {UIntPtr r;SendMessageTimeoutW(w,0x111,new UIntPtr(108),IntPtr.Zero,3,5000,out r);}
}
'@
$saved=[Collections.Generic.List[object]]::new()
$keyPath='Software\macdowsOS\WindowChanger'
$registry=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($keyPath,$false)
$hadValue=$registry -and @($registry.GetValueNames()) -contains 'AutoManage'
$originalValue=if($hadValue) { $registry.GetValue('AutoManage',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } else { $null }
$originalKind=if($hadValue) { $registry.GetValueKind('AutoManage') } else { $null }
if($registry) { $registry.Dispose() }
$code=1
try {
    foreach($process in @(Get-Process WindowStyler -ErrorAction SilentlyContinue)) {
        foreach($window in [ExistingController]::Find([uint32]$process.Id)) {
            $automatic=[ExistingController]::Automatic($window)
            $saved.Add([pscustomobject]@{pid=$process.Id;hwnd=$window.ToInt64();automatic=$automatic;path=$process.Path})
            if($automatic) {
                [ExistingController]::Toggle($window)
                if([ExistingController]::Automatic($window)) { throw 'Could not pause the existing controller safely.' }
            }
        }
    }
    ConvertTo-Json -InputObject @($saved) -Depth 4 | Out-File (Join-Path $OutputDirectory 'existing-controllers.json') -Encoding utf8
    & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $PSScriptRoot 'verify-controller-matrix.ps1') -PackageDirectory $PackageDirectory -OutputDirectory $OutputDirectory -BuildPrefix $BuildPrefix
    $code=$LASTEXITCODE
} catch {
    $_ | Out-String | Out-File (Join-Path $OutputDirectory 'runner-error.txt') -Encoding utf8
} finally {
    $restored=$true
    foreach($entry in $saved) {
        $window=[IntPtr]$entry.hwnd
        [uint32]$pidNow=0
        [void][ExistingController]::GetWindowThreadProcessId($window,[ref]$pidNow)
        if([ExistingController]::IsWindow($window) -and $pidNow -eq $entry.pid) {
            if([ExistingController]::Automatic($window) -ne $entry.automatic) { [ExistingController]::Toggle($window) }
            $restored=$restored -and ([ExistingController]::Automatic($window) -eq $entry.automatic)
        } else { $restored=$false }
    }
    $preferenceRestored=$false
    try {
        $registry=if($hadValue) { [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($keyPath) } else {
            [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($keyPath,$true)
        }
        if($registry) {
            if($hadValue) { $registry.SetValue('AutoManage',$originalValue,$originalKind) } else { $registry.DeleteValue('AutoManage',$false) }
            $registry.Dispose()
        }
        $registry=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($keyPath,$false)
        $hasValueNow=$registry -and @($registry.GetValueNames()) -contains 'AutoManage'
        $preferenceRestored=([bool]$hasValueNow -eq [bool]$hadValue)
        if($hadValue -and $hasValueNow) {
            $valueNow=$registry.GetValue('AutoManage',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $preferenceRestored=$preferenceRestored -and $registry.GetValueKind('AutoManage') -eq $originalKind -and
                (($valueNow | ConvertTo-Json -Compress) -ceq ($originalValue | ConvertTo-Json -Compress))
        }
        if($registry) { $registry.Dispose() }
    } catch {
        $_ | Out-String | Out-File (Join-Path $OutputDirectory 'restore-error.txt') -Encoding utf8
    }
    [pscustomobject]@{existingControllers=$saved.Count;existingAutomaticStateRestored=$restored;originalPreferenceRestored=$preferenceRestored} |
        ConvertTo-Json | Out-File (Join-Path $OutputDirectory 'existing-restored.json') -Encoding utf8
    if(-not $restored -or -not $preferenceRestored) { $code=1 }
}
exit $code
