param([string]$ReleaseDirectory='',[string]$OutputDirectory=(Join-Path $PSScriptRoot 'out\controller-direct-v10'))
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=if(Test-Path (Join-Path $PSScriptRoot 'WindowStyler.exe')){$PSScriptRoot}else{Join-Path $PSScriptRoot 'out/WindowInjector-v10'}}
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run as administrator.'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class DirectProbe {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc e,IntPtr p);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string s);
 [DllImport("user32.dll")]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,UIntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static IntPtr[] Windows(uint pid){var a=new List<IntPtr>();EnumWindows((h,p)=>{uint n;GetWindowThreadProcessId(h,out n);if(n==pid)a.Add(h);return true;},IntPtr.Zero);return a.ToArray();}
 public static void Send(IntPtr h,uint m,uint w){UIntPtr r;SendMessageTimeout(h,m,new UIntPtr(w),IntPtr.Zero,2,5000,out r);}
}
'@
$checks=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new();$ui=$null;$cli=$null
function Check($name,$ok){$checks.Add([pscustomobject]@{name=$name;ok=[bool]$ok});Write-Output ((@('FAIL ','PASS '))[[int][bool]$ok]+$name)}
function WaitUntil([scriptblock]$condition){$deadline=[DateTime]::UtcNow.AddSeconds(10);do{if(& $condition){return $true};Start-Sleep -Milliseconds 75}while([DateTime]::UtcNow -lt $deadline);return [bool](& $condition)}
function Samples($p){return @([DirectProbe]::Windows($p.Id)|Where-Object {[DirectProbe]::GetProp($_,'Macdows.WindowStyler.Sample') -ne [IntPtr]::Zero})}
function Attached($h){return [DirectProbe]::GetProp($h,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero}
try{
 $before=@(Get-Process QtInlineTargets -ErrorAction SilentlyContinue|ForEach-Object Id)
 $ui=Start-Process (Join-Path $ReleaseDirectory 'WindowStyler.exe') -PassThru
 Check 'Administrator controller starts' (WaitUntil {$ui.Refresh();$ui.MainWindowHandle -ne [IntPtr]::Zero})
 $h=$ui.MainWindowHandle
 [DirectProbe]::Send($h,0x111,101)
 $script:qt=$null
 Check 'Sample button starts deployed Qt app without a Qt PATH' (WaitUntil {$script:qt=Get-Process QtInlineTargets -ErrorAction SilentlyContinue|Where-Object {$_.Id -notin $before}|Select-Object -First 1;return $null -ne $script:qt})
 if(-not $script:qt){throw 'Sample launcher failed'}
 $owned.Add($script:qt)
 $script:tabs=@();Check 'Widgets and Quick original layouts are automatically injected' (WaitUntil {$script:tabs=Samples $script:qt;return $script:tabs.Count -eq 2 -and @($script:tabs|Where-Object {[DirectProbe]::GetProp($_,'Macdows.WindowStyler.Inline.v4') -ne [IntPtr]::Zero}).Count -eq 2})
 $native=Start-Process (Join-Path $ReleaseDirectory 'StyleTarget.exe') -PassThru;$owned.Add($native)
 $x86=Start-Process (Join-Path $ReleaseDirectory 'x86\StyleTarget.exe') -PassThru;$owned.Add($x86)
 $script:nativeWindows=@();$script:x86Windows=@()
 Check 'Same x64 controller injects native x64 and x86 targets' (WaitUntil {$script:nativeWindows=Samples $native;$script:x86Windows=Samples $x86;return $script:nativeWindows.Count -eq 1 -and $script:x86Windows.Count -eq 1 -and (Attached $script:nativeWindows[0]) -and (Attached $script:x86Windows[0])})
 $script:wpfWindows=@()
 foreach($file in @('WpfInlineTarget.exe','x86\WpfInlineTarget.exe','wpf-target-core\WpfInlineCoreTarget.exe')){
  $wpf=Start-Process (Join-Path $ReleaseDirectory $file) -PassThru;$owned.Add($wpf);$script:currentWpf=@()
  Check "Packaged WPF original layout is automatically injected: $file" (WaitUntil {$script:currentWpf=Samples $wpf;return $script:currentWpf.Count -eq 1 -and [DirectProbe]::GetProp($script:currentWpf[0],'Macdows.WindowStyler.Inline.v4').ToInt64() -eq 3})
  $script:wpfWindows+=@($script:currentWpf)
 }
 $script:xamlWindows=@()
 foreach($hybrid in @($false,$true)){
  $xaml=if($hybrid){Start-Process (Join-Path $ReleaseDirectory 'XamlInlineTarget.exe') -ArgumentList '--hybrid' -PassThru}else{Start-Process (Join-Path $ReleaseDirectory 'XamlInlineTarget.exe') -PassThru}
  $owned.Add($xaml);$script:currentXaml=@()
  Check "Packaged XAML original layout is automatically injected (native caption: $hybrid)" (WaitUntil {$script:currentXaml=Samples $xaml;return $script:currentXaml.Count -eq 1 -and [DirectProbe]::GetProp($script:currentXaml[0],'Macdows.WindowStyler.Inline.v4').ToInt64() -eq 4})
  $script:xamlWindows+=@($script:currentXaml)
 }
 $all=@($script:tabs)+@($script:nativeWindows)+@($script:x86Windows)+@($script:wpfWindows)+@($script:xamlWindows)
 [DirectProbe]::Send($h,0x111,105)
 Check 'Pause and restore all detaches both architectures and Qt layouts' (WaitUntil {@($all|Where-Object {Attached $_}).Count -eq 0})
 Check 'Auto management is paused' ([DirectProbe]::GetProp($h,'Macdows.WindowStyler.AutoManage.v2') -eq [IntPtr]::Zero)
 $hex='0x'+$script:x86Windows[0].ToInt64().ToString('x')
 $cli=Start-Process (Join-Path $ReleaseDirectory 'WindowInjector.exe') -ArgumentList @('--apply',$hex,'--duration','3') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'cli.txt') -RedirectStandardError (Join-Path $OutputDirectory 'cli-error.txt')
 Check 'Generic x64 command-line injector attaches x86 target' (WaitUntil {Attached $script:x86Windows[0]})
 $owner=[DirectProbe]::GetProp($script:x86Windows[0],'Macdows.WindowStyler.Owner.v1');[uint32]$ownerPid=0;[void][DirectProbe]::GetWindowThreadProcessId($owner,[ref]$ownerPid)
 $helper=Get-Process -Id $ownerPid -ErrorAction SilentlyContinue
 Check 'Architecture broker uses the packaged x86 helper' ($helper -and $helper.Path -eq (Join-Path $ReleaseDirectory 'x86\WindowStyleHost.exe'))
 $cli.WaitForExit(10000)|Out-Null
 $cli.Refresh()
 $clean=WaitUntil {-not (Attached $script:x86Windows[0])}
 Write-Output "CLI exited=$($cli.HasExited), code=$($cli.ExitCode), detached=$clean"
 Check 'Command-line lease expiry restores x86 target' ($cli.HasExited -and $clean)
 [DirectProbe]::Send($h,0x111,108)
 Check 'Resume reapplies all supported layouts' (WaitUntil {@($all|Where-Object {Attached $_}).Count -eq $all.Count})
 $ui.Kill();$ui.WaitForExit()
 Check 'Controller crash restores Qt and native layouts across architectures' (WaitUntil {@($all|Where-Object {Attached $_}).Count -eq 0})
}finally{
 if($cli -and -not $cli.HasExited){$cli.Kill();$cli.WaitForExit()}
 if($ui -and -not $ui.HasExited){[DirectProbe]::Send($ui.MainWindowHandle,0x10,0);$ui.WaitForExit(5000)|Out-Null}
 foreach($process in $owned){if(-not $process.HasExited){foreach($window in (Samples $process)){[DirectProbe]::Send($window,0x10,0)};if(-not $process.WaitForExit(3000)){$process.Kill()}}}
 $checks | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'checks.json') -Encoding UTF8
}
if(@($checks|Where-Object {-not $_.ok}).Count){exit 1}
