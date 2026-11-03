param([string]$ReleaseDirectory='', [string]$OutputDirectory=(Join-Path $PSScriptRoot 'out/live-v10/netui-leases'))
$ErrorActionPreference='Stop'
if(-not $ReleaseDirectory){$ReleaseDirectory=Join-Path $PSScriptRoot 'out/build-x64/Release'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
Add-Type @'
using System;using System.Collections.Generic;using System.Diagnostics;using System.Runtime.InteropServices;
public static class NetuiLeaseProbe {
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)]public struct Placement{public uint size,flags,show;public P min,max;public R normal;}
 [DllImport("user32.dll")]public static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool SetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string s);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 public static long Query(IntPtr h){UIntPtr r;SendMessageTimeout(h,RegisterWindowMessage("Macdows.WindowStyler.Query.v1"),IntPtr.Zero,IntPtr.Zero,2,1000,out r);return (long)r.ToUInt64();}
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr OpenProcess(uint a,bool inherit,int pid);
 [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr p,IntPtr a,byte[] b,int n,out IntPtr read);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr h);
 static byte[] Read(IntPtr p,long a,int n){var b=new byte[n];IntPtr size;if(!ReadProcessMemory(p,(IntPtr)a,b,n,out size)||size.ToInt64()!=n)throw new Exception("Remote read failed");return b;}
 static uint U32(IntPtr p,long a){return BitConverter.ToUInt32(Read(p,a,4),0);}
 static long U64(IntPtr p,long a){return BitConverter.ToInt64(Read(p,a,8),0);}
 static string Name(IntPtr p,long a){var bytes=new List<byte>();for(int n=0;n<160;n++){var b=Read(p,a+n,1)[0];if(b==0)break;bytes.Add(b);}return System.Text.Encoding.ASCII.GetString(bytes.ToArray());}
 public static Dictionary<long,long> Snapshot(int pid){
  var result=new Dictionary<long,long>();var p=OpenProcess(0x410,false,pid);if(p==IntPtr.Zero)throw new Exception("Cannot read target IAT");
  var names=new HashSet<string>{"GetWindowLongPtrW","GetWindowLongPtrA","GetWindowLongW","GetWindowLongA","GetWindowInfo","DwmGetWindowAttribute"};
  try{foreach(ProcessModule m in Process.GetProcessById(pid).Modules){
   long b=m.BaseAddress.ToInt64();long nt=b+U32(p,b+60);if(U32(p,nt)!=0x4550)continue;
   var optional=nt+24;if(BitConverter.ToUInt16(Read(p,optional,2),0)!=0x20b)continue;
   long directories=optional+112;
   Action<uint,uint> scan=(iat,intNames)=>{if(iat==0||intNames==0)return;for(int i=0;i<10000;i++){
    long symbol=U64(p,b+intNames+i*8);if(symbol==0)break;if(symbol<0)continue;
    if(names.Contains(Name(p,b+symbol+2)))result[b+iat+i*8]=U64(p,b+iat+i*8);
   }};
   uint imports=U32(p,directories+8);
   if(imports!=0)for(long d=b+imports;U32(p,d+12)!=0;d+=20)scan(U32(p,d+16),U32(p,d));
   uint delay=U32(p,directories+13*8);
   if(delay!=0)for(long d=b+delay;U32(p,d+4)!=0;d+=32)if((U32(p,d)&1)!=0)scan(U32(p,d+12),U32(p,d+16));
  }}finally{CloseHandle(p);}return result;
 }
 public static int Changed(Dictionary<long,long> before,Dictionary<long,long> after){int n=0;foreach(var pair in before){long value;if(!after.TryGetValue(pair.Key,out value)||value!=pair.Value)n++;}return n;}
}
'@
function WaitUntil([scriptblock]$condition){$end=[DateTime]::UtcNow.AddSeconds(5);do{if(&$condition){return $true};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $end);return $false}
$checks=[Collections.Generic.List[object]]::new()
function Check($name,$ok){$checks.Add([pscustomobject]@{name=$name;ok=[bool]$ok});if(-not $ok){throw $name}}
$inventory=Join-Path $OutputDirectory 'inventory.json'
& (Join-Path $ReleaseDirectory 'WindowAudit.exe') --direct-inventory $inventory
$windows=(Get-Content -LiteralPath $inventory -Raw -Encoding UTF8|ConvertFrom-Json).windows
$group=$windows|Where-Object {$_.framework -eq 'NetUI / Ribbon' -and $_.eligible}|Group-Object pid|Where-Object Count -ge 2|Select-Object -First 1
if(-not $group){throw 'Two NetUI windows in one process are required'}
$chosen=@($group.Group|Select-Object -First 2)
$handles=@($chosen|ForEach-Object {[IntPtr][Convert]::ToInt64($_.hwnd.Substring(2),16)})
$placements=@();$workers=@();$foreground=[NetuiLeaseProbe]::GetForegroundWindow()
try{
 foreach($h in $handles){$placement=[NetuiLeaseProbe+Placement]::new();$placement.size=[Runtime.InteropServices.Marshal]::SizeOf($placement);[void][NetuiLeaseProbe]::GetWindowPlacement($h,[ref]$placement);$placements+=@($placement);[void][NetuiLeaseProbe]::ShowWindow($h,9)}
 $baseline=[NetuiLeaseProbe]::Snapshot($chosen[0].pid)
 for($i=0;$i -lt 2;$i++){
  $workers+=@(Start-Process (Join-Path $ReleaseDirectory 'StylerTests.exe') -ArgumentList @('--inline-lease-worker',$handles[$i].ToInt64()) -WindowStyle Hidden -PassThru)
  Check "lease $i attaches original NetUI row" (WaitUntil {[NetuiLeaseProbe]::Query($handles[$i]) -eq 0x4d414357})
 }
 $during=[NetuiLeaseProbe]::Snapshot($chosen[0].pid)
 $changed=[NetuiLeaseProbe]::Changed($baseline,$during)
 Check 'shared process read view patches existing imports' ($changed -gt 0)
 $workers[0].Kill();$workers[0].WaitForExit()
 Check 'first owner exit restores only its window' (WaitUntil {[NetuiLeaseProbe]::GetProp($handles[0],'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero})
 Check 'second lease survives first owner exit' ([NetuiLeaseProbe]::Query($handles[1]) -eq 0x4d414357)
 Check 'read view remains patched while second lease exists' ([NetuiLeaseProbe]::Changed($baseline,[NetuiLeaseProbe]::Snapshot($chosen[0].pid)) -gt 0)
 [void][NetuiLeaseProbe]::ShowWindow($handles[1],3)
 Check 'remaining window maximizes' (WaitUntil {[NetuiLeaseProbe]::IsZoomed($handles[1])})
 [void][NetuiLeaseProbe]::ShowWindow($handles[1],9)
 Check 'remaining window restores and acknowledges layout' (WaitUntil {-not [NetuiLeaseProbe]::IsZoomed($handles[1]) -and [NetuiLeaseProbe]::Query($handles[1]) -eq 0x4d414357})
 $workers[1].Kill();$workers[1].WaitForExit()
 Check 'last owner exit restores last window' (WaitUntil {[NetuiLeaseProbe]::GetProp($handles[1],'Macdows.WindowStyler.Owner.v1') -eq [IntPtr]::Zero})
 Check 'last lease restores every original captured import address' (WaitUntil {[NetuiLeaseProbe]::Changed($baseline,[NetuiLeaseProbe]::Snapshot($chosen[0].pid)) -eq 0})
 [pscustomobject]@{windows=@($chosen.hwnd);originalImports=$baseline.Count;changedImports=$changed;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}finally{
 foreach($worker in $workers){if(-not $worker.HasExited){$worker.Kill();$worker.WaitForExit()}}
 Start-Sleep -Milliseconds 1000
 for($i=0;$i -lt $placements.Count;$i++){[void][NetuiLeaseProbe]::SetWindowPlacement($handles[$i],[ref]$placements[$i])}
 [void][NetuiLeaseProbe]::SetForegroundWindow($foreground)
 $checks|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'checks.json') -Encoding UTF8
}
