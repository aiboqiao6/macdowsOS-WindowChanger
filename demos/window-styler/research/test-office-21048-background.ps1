param(
 [string]$HostDirectory=(Join-Path $PSScriptRoot '../out/framework-probe-x64/office-21048-research/Release'),
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../out/live-v45/office-background-Release'),
 [switch]$ReadOnly,
 [ValidateSet('normal','owner-loss','resize')][string]$Scenario='normal',
 [ValidateSet('Word','Excel','PowerPoint')][string[]]$Applications=@('Word','Excel')
)
$ErrorActionPreference='Stop'
$Candidate=-not $ReadOnly
if($ReadOnly -and $Scenario -ne 'normal'){throw 'Lifecycle verification requires the isolated candidate'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory to preserve evidence'}
[void](New-Item -ItemType Directory -Path $OutputDirectory)
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type @'
using System;using System.IO;using System.Threading.Tasks;using System.Runtime.InteropServices;
public sealed class OwnedOfficeHostOutput {
 public volatile bool Ready;
 public Task<string> Completion {get;private set;}
 public OwnedOfficeHostOutput(StreamReader reader){Completion=Read(reader);}
 private async Task<string> Read(StreamReader reader){var text=new System.Text.StringBuilder();string line;while((line=await reader.ReadLineAsync().ConfigureAwait(false))!=null){text.AppendLine(line);if(line=="ready-for-owner-loss=1")Ready=true;}return text.ToString();}
}
public static class OwnedOfficeRead {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback callback,IntPtr parameter);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,System.Text.StringBuilder name,int capacity);
 public static IntPtr PowerPointFrame(uint pid){var found=IntPtr.Zero;int count=0;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var name=new System.Text.StringBuilder(256);GetClassName(h,name,name.Capacity);if(name.ToString()=="PPTFrameClass"){found=h;count++;}}return true;},IntPtr.Zero);if(count!=1)throw new Exception("Expected one owned PowerPoint frame: "+count);return found;}
 [StructLayout(LayoutKind.Sequential)]public struct R {public int l,t,r,b;}
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr z,int x,int y,int w,int t,uint f);
 [DllImport("user32.dll")]public static extern bool EnableWindow(IntPtr h,bool enabled);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string name);
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]static extern bool GetClientRect(IntPtr h,out R r);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]static extern IntPtr GetWindowLongPtr(IntPtr h,int index);
 public static string Geometry(IntPtr h){R o,c;if(!GetWindowRect(h,out o)||!GetClientRect(h,out c))throw new Exception("Owned geometry query failed");return string.Join(",",o.l,o.t,o.r,o.b,c.l,c.t,c.r,c.b,GetWindowLongPtr(h,-16).ToInt64(),GetWindowLongPtr(h,-20).ToInt64());}
}
'@
$base=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$reports=[Collections.Generic.List[object]]::new()
foreach($application in $Applications){
 $out=Join-Path $OutputDirectory $application.ToLowerInvariant()
 if(Test-Path -LiteralPath $out){throw 'Use a new evidence directory'}
 [void](New-Item -ItemType Directory -Path $out)
 $name=switch($application){'Word'{'WINWORD'}'Excel'{'EXCEL'}'PowerPoint'{'POWERPNT'}}
 $existing=@(Get-Process $name -ErrorAction SilentlyContinue|ForEach-Object Id)
 $app=$null;$document=$null;$collection=$null;$windowObject=$null;$owned=$null;$ownedPid=0;$safeToQuit=$false;$succeeded=$false
 $record=[ordered]@{application=$application;scenario=$Scenario;existingProcessCount=$existing.Count;readerExecutedTargetCode=$false;injectionPerformed=$false;visualVerification=$false}
 try {
  if($existing.Count){throw 'Existing Office processes are left untouched; close them before using this owned-instance verifier'}
  $app=New-Object -ComObject ($application+'.Application')
  $newProcesses=@(Get-Process $name -ErrorAction SilentlyContinue | Where-Object {$existing -notcontains $_.Id})
  if($newProcesses.Count -ne 1 -or ($application -ne 'PowerPoint' -and $app.UserControl)){throw 'COM instance is not one new automation-owned process; refusing to operate it'}
  [uint32]$ownerPid=$newProcesses[0].Id
  $owned=Get-Process -Id $ownerPid
  if($owned.ProcessName -ne $name){throw 'Unexpected Office process'}
  if($app.Visible){throw 'Owned Office instance is unexpectedly visible'}
  $safeToQuit=$true;$ownedPid=$ownerPid
  $app.DisplayAlerts=if($application -eq 'PowerPoint'){1}else{0}
  $record.version=$app.Version
  if($application -eq 'Word'){
   $app.AutomationSecurity=3
   $missing=[Type]::Missing
   $collection=$app.Documents
   $document=$collection.Add($missing,$false,0,$false)
   if($collection.Count -ne 1){throw 'Unexpected documents in the owned instance'}
  }elseif($application -eq 'Excel'){
   $app.AutomationSecurity=3
   $collection=$app.Workbooks
   $document=$collection.Add()
   if($collection.Count -ne 1){throw 'Unexpected workbooks in the owned instance'}
  }else{
   $app.AutomationSecurity=3
   $collection=$app.Presentations
   if($collection.Count -ne 0){$safeToQuit=$false;throw 'Unexpected presentations in the new PowerPoint instance'}
   # Public WithWindow=msoFalse creates only our blank test presentation.
   $document=$collection.Add(0)
   $record.presentationWithWindow=$false
   if($collection.Count -ne 1){throw 'Unexpected presentations in the owned instance'}
  }
  if($application -eq 'Word'){$windowObject=$document.ActiveWindow;$handle=[IntPtr]([long]$windowObject.Hwnd);[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowObject);$windowObject=$null}elseif($application -eq 'PowerPoint'){$handle=[OwnedOfficeRead]::PowerPointFrame($ownerPid)}else{$handle=[IntPtr]([long]$app.Hwnd)}
  [uint32]$verifiedPid=0;[void][OwnedOfficeRead]::GetWindowThreadProcessId($handle,[ref]$verifiedPid)
  if($verifiedPid -ne $ownerPid -or $app.Visible -or [OwnedOfficeRead]::GetForegroundWindow() -eq $handle){throw 'Owned invisible HWND identity changed or became foreground'}
  $record.pid=$ownerPid;$record.hwnd=('0x{0:x}' -f $handle.ToInt64())
  if($Candidate){
   [void][OwnedOfficeRead]::SetWindowPos($handle,[IntPtr](1),160,160,1200,720,0x10)
   [void][OwnedOfficeRead]::EnableWindow($handle,$false)
   try{$app.Visible=if($application -eq 'PowerPoint'){-1}else{$true}}finally{[void][OwnedOfficeRead]::EnableWindow($handle,$true)}
   $record.backgroundShowMethod='owned HWND disabled during public Application.Visible; no foreground activation'
   [void][OwnedOfficeRead]::SetWindowPos($handle,[IntPtr](1),160,160,1200,720,0x10)
   Start-Sleep -Milliseconds 700
   if([OwnedOfficeRead]::GetForegroundWindow() -eq $handle){throw 'Showing the owned candidate activated it'}
  }
  foreach($reader in @('office-metadata.py','office-tree.py')){
   $file=Join-Path $out ($reader.Replace('.py','.json'))
   $args=@((Join-Path $base "research/$reader"),'--hwnd',$record.hwnd,'--out',$file)
   if($reader -eq 'office-metadata.py'){$args+=@('--max-objects','1000')}
   & python @args *> (Join-Path $out ($reader+'.log'))
   if($LASTEXITCODE -ne 0){Get-Content (Join-Path $out ($reader+'.log')) -Tail 12;throw "Read-only reader failed: $reader"}
  }
  $tree=Get-Content -LiteralPath (Join-Path $out 'office-tree.json') -Raw|ConvertFrom-Json
  $metadata=Get-Content -LiteralPath (Join-Path $out 'office-metadata.json') -Raw|ConvertFrom-Json
  if(-not $tree.complete -or $tree.nodes.Count -eq 0 -or $metadata.roots.Count -eq 0){throw 'Owned Office tree is incomplete or empty'}
  if(((-not $Candidate) -and $app.Visible) -or [OwnedOfficeRead]::GetForegroundWindow() -eq $handle){throw 'Inventory changed the owned foreground state'}
  $record.profile=$tree.readOnlyProfile;$record.roots=$tree.roots.Count;$record.nodes=$tree.nodes.Count
  $record.objects=$metadata.objects.Count;$record.budgetReached=$metadata.budgetReached;$record.complete=$tree.complete
  if($Candidate){
   $beforeGeometry=[OwnedOfficeRead]::Geometry($handle)
   $hostPath=(Resolve-Path (Join-Path $HostDirectory 'Office21048BackgroundHost.exe')).Path
   $p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$hostPath;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
   $p.StartInfo.ArgumentList.Add($record.hwnd);$p.StartInfo.ArgumentList.Add($ownerPid.ToString())
   if($Scenario -eq 'owner-loss'){$p.StartInfo.ArgumentList.Add('--wait-owner-loss')}
   if($Scenario -eq 'resize'){$p.StartInfo.ArgumentList.Add('--resize-roundtrip')}
   $record.injectionAttempted=$true;$record.hostHash=(Get-FileHash $hostPath).Hash
   [void]$p.Start();$hostOutput=[OwnedOfficeHostOutput]::new($p.StandardOutput);$stdout=$hostOutput.Completion;$stderr=$p.StandardError.ReadToEndAsync()
   if($Scenario -eq 'owner-loss'){
    $readyDeadline=[DateTime]::UtcNow.AddSeconds(8)
    while(-not $hostOutput.Ready -and -not $p.HasExited -and [DateTime]::UtcNow -lt $readyDeadline){Start-Sleep -Milliseconds 50}
    if(-not $hostOutput.Ready){throw 'Owned controller did not confirm readiness for the owner-loss test'}
    $lease=[OwnedOfficeRead]::GetProp($handle,'Macdows.WindowStyler.Owner.v1')
    [uint32]$leasePid=0;[void][OwnedOfficeRead]::GetWindowThreadProcessId($lease,[ref]$leasePid)
    if($lease -eq [IntPtr]::Zero -or $leasePid -ne $p.Id -or $p.Path -ne $hostPath){throw 'Owner-loss target is not this exact owned child controller'}
    $record.readyBeforeOwnerLoss=$true
    $p.Kill()
   }
   if(-not $p.WaitForExit(20000)){throw 'Owned candidate host timeout'}
   $stdout.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $out 'candidate.log') -Encoding utf8
   $stderr.GetAwaiter().GetResult()|Set-Content -LiteralPath (Join-Path $out 'candidate.err') -Encoding utf8
   $record.candidateExitCode=$p.ExitCode;$p.Dispose()
   $restoreDeadline=[DateTime]::UtcNow.AddSeconds(8)
   while([OwnedOfficeRead]::GetProp($handle,'Macdows.WindowStyler.Owner.v1') -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $restoreDeadline){Start-Sleep -Milliseconds 50}
   $record.leaseRemoved=([OwnedOfficeRead]::GetProp($handle,'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero -and [OwnedOfficeRead]::GetProp($handle,'Macdows.WindowStyler.Inline.v4') -eq [IntPtr]::Zero)
   $record.exactGeometryRestored=($beforeGeometry -ceq [OwnedOfficeRead]::Geometry($handle))
   if(-not $record.leaseRemoved -or -not $record.exactGeometryRestored){throw 'Controller loss/exit did not restore the exact owned geometry and lease state'}
   & python (Join-Path $base 'research/office-tree.py') --hwnd $record.hwnd --out (Join-Path $out 'tree-after.json') *> (Join-Path $out 'tree-after.log')
   if($LASTEXITCODE -ne 0){throw 'Candidate rollback tree read failed'}
   $afterTree=Get-Content -LiteralPath (Join-Path $out 'tree-after.json') -Raw|ConvertFrom-Json
   $record.exactTreeRestored=($afterTree.complete -and ($tree.nodes|ConvertTo-Json -Depth 8 -Compress) -ceq ($afterTree.nodes|ConvertTo-Json -Depth 8 -Compress))
   $reportPath=Join-Path $HostDirectory "office-lease-$ownerPid.json"
   $record.leaseReportPresent=Test-Path -LiteralPath $reportPath
   if($record.leaseReportPresent){
    Copy-Item -LiteralPath $reportPath -Destination (Join-Path $out 'lease-report.json')
    $leaseReport=Get-Content -LiteralPath (Join-Path $out 'lease-report.json') -Raw|ConvertFrom-Json
    $record.cleanupFailures=$leaseReport.cleanupFailures
    if($leaseReport.active -or $leaseReport.cleanupFailures -ne 0){throw 'Owned lease report is still active or reports cleanup failures'}
   }
   $expectedExit=if($Scenario -eq 'owner-loss'){-1}else{0}
   if($record.candidateExitCode -ne $expectedExit){Get-Content (Join-Path $out 'candidate.log') -Tail 12;throw 'Candidate did not have the expected owned-controller exit status'}
   if(-not $record.leaseReportPresent){throw 'Successful candidate did not produce its lease cleanup report'}
   if(-not $record.exactTreeRestored){throw 'Candidate did not restore exact original node tree and geometry'}
   $record.injectionPerformed=$true
  }
  $succeeded=$true
 }catch{
  $record.error=$_.Exception.Message
  ($_|Out-String)+$_.ScriptStackTrace|Set-Content -LiteralPath (Join-Path $out 'failure.txt') -Encoding utf8
 }finally{
  if($safeToQuit){
   if($windowObject){[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowObject);$windowObject=$null}
   if($document){if($application -eq 'Word'){$document.Close(0)}elseif($application -eq 'PowerPoint'){$document.Saved=-1;$document.Close()}else{$document.Close($false)};[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document);$document=$null}
   if($collection){[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($collection);$collection=$null}
   if($application -eq 'Word'){$app.Quit(0)}else{$app.Quit()}
  }
  if($app){[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($app);$app=$null}
  [GC]::Collect();[GC]::WaitForPendingFinalizers()
  if($owned){$record.ownedProcessExited=$owned.WaitForExit(8000);$owned.Dispose()}
  $record.passed=$succeeded -and $record.ownedProcessExited
  $record.finished=[DateTimeOffset]::Now.ToString('o')
  $reports.Add($record)
  $record|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding utf8
  $reports|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding utf8
 }
 $record|ConvertTo-Json -Depth 5
 if(-not $record.passed){exit 1}
}
