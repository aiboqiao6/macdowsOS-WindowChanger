# Screen pixels are evidence only while the requested window is foreground and
# no visible top-level window above it intersects the captured rectangle.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WindowStylerCaptureGuard {
 [StructLayout(LayoutKind.Sequential)] struct R {public int l,t,r,b;}
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] static extern IntPtr GetTopWindow(IntPtr parent);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h,uint command);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h,uint attribute,out uint value,int bytes);
 [DllImport("dwmapi.dll")] static extern int DwmFlush();
 public static void Check(IntPtr target,int x,int y,int width,int height) {
  if(width<=0||height<=0||GetForegroundWindow()!=target)throw new Exception("Capture target lost foreground or has invalid bounds");
  if(DwmFlush()<0)throw new Exception("Cannot synchronize screen capture with DWM");
  long right=(long)x+width,bottom=(long)y+height;
  R targetBounds;
  if(!GetWindowRect(target,out targetBounds)||x<targetBounds.l||y<targetBounds.t||right>targetBounds.r||bottom>targetBounds.b)throw new Exception("Capture rectangle no longer fits the target window");
  bool found=false;
  int budget=4096;
  for(var above=GetTopWindow(IntPtr.Zero);above!=IntPtr.Zero&&budget-->0;above=GetWindow(above,2)) {
   if(above==target){found=true;break;}
   if(!IsWindowVisible(above)||IsIconic(above))continue;
   uint cloaked;
   if(DwmGetWindowAttribute(above,14,out cloaked,sizeof(uint))>=0&&cloaked!=0)continue;
   R bounds;
   if(!GetWindowRect(above,out bounds))throw new Exception("Cannot validate capture occlusion");
   if(bounds.l<right&&x<bounds.r&&bounds.t<bottom&&y<bounds.b) {
    uint pid;GetWindowThreadProcessId(above,out pid);
    throw new Exception("Capture is occluded by HWND 0x"+above.ToInt64().ToString("x")+" PID "+pid);
   }
  }
  if(!found||GetForegroundWindow()!=target)throw new Exception("Capture window changed during visibility validation");
 }
}
'@
