param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'out\live-v5'))
$ErrorActionPreference='Stop'
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
trap { $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt'); exit 1 }
& (Join-Path $PSScriptRoot 'out\build-x64\Release\WindowAudit.exe') --direct-inventory (Join-Path $OutputDirectory 'inventory.json')
$inventory=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'inventory.json') | ConvertFrom-Json
$modules=@()
foreach($entry in $inventory.windows){
 try {$p=Get-Process -Id $entry.pid -ErrorAction Stop;$modules+=@{pid=$entry.pid;title=$entry.title;modules=@($p.Modules | Select-Object ModuleName,FileName)}} catch {$modules+=@{pid=$entry.pid;error=$_.Exception.Message}}
}
$modules | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'modules.json') -Encoding UTF8
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;using System.Collections.Generic;
public static class LiveNative {
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 [DllImport("user32.dll")]public static extern bool SetProcessDpiAwarenessContext(IntPtr d);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")]public static extern int GetSystemMetrics(int n);
 public static string[] Children(long h){var a=new List<string>();EnumChildWindows((IntPtr)h,(w,l)=>{var b=new StringBuilder(256);GetClassName(w,b,256);a.Add(w.ToInt64().ToString("x")+" "+b);return true;},IntPtr.Zero);return a.ToArray();}
}
'@
[void][LiveNative]::SetProcessDpiAwarenessContext([IntPtr](-4))
$children=@();foreach($entry in $inventory.windows){$children+=@{hwnd=$entry.hwnd;children=[LiveNative]::Children([Convert]::ToInt64($entry.hwnd.Substring(2),16))}}
$children | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'children.json') -Encoding UTF8
$bmp=New-Object Drawing.Bitmap([LiveNative]::GetSystemMetrics(0)),([LiveNative]::GetSystemMetrics(1));$g=[Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen(0,0,0,0,$bmp.Size);$bmp.Save((Join-Path $OutputDirectory 'desktop.png'));$g.Dispose();$bmp.Dispose()
'done' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'done.txt')
