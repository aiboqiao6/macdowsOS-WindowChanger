# GetWindowRect includes invisible resize margins. Keep the complete screenshot,
# and place a real opaque owned window below it so those margins do not sample
# changing desktop pixels. No target properties or captured pixels are rewritten.
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NativeIslandCaptureBackdrop {
 [StructLayout(LayoutKind.Sequential)]struct R{public int l,t,r,b;}
 delegate IntPtr Proc(IntPtr h,uint message,IntPtr w,IntPtr l);
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]struct W {public uint style;public Proc procedure;public int clsExtra,windowExtra;public IntPtr instance,icon,cursor,brush;public string menu,name;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern ushort RegisterClass(ref W value);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr CreateWindowEx(uint extended,string cls,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr parameter);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr DefWindowProc(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")]static extern bool UpdateWindow(IntPtr h);
 [DllImport("user32.dll")]static extern bool DestroyWindow(IntPtr h);
 [DllImport("gdi32.dll")]static extern IntPtr CreateSolidBrush(uint color);
 static readonly Proc procedure=DefWindowProc;
 static IntPtr window;static bool registered;
 public static void Place(IntPtr target) {
  if(GetForegroundWindow()!=target)throw new Exception("Cannot place capture background after target lost foreground");
  R rect;if(!GetWindowRect(target,out rect)||rect.r<=rect.l||rect.b<=rect.t)throw new Exception("Invalid capture background bounds");
  const string cls="Macdows.Research.CaptureBackdrop";
  if(!registered){var value=new W{procedure=procedure,brush=CreateSolidBrush(0x202020),name=cls};if(value.brush==IntPtr.Zero||RegisterClass(ref value)==0)throw new Exception("Cannot register owned capture background");registered=true;}
  if(window==IntPtr.Zero){window=CreateWindowEx(0x08000088,cls,cls,0x80000000,rect.l,rect.t,rect.r-rect.l,rect.b-rect.t,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);if(window==IntPtr.Zero)throw new Exception("Cannot create owned capture background");}
  if(!SetWindowPos(window,target,rect.l,rect.t,rect.r-rect.l,rect.b-rect.t,0x50))throw new Exception("Cannot position owned capture background below target");
  UpdateWindow(window);
  if(GetForegroundWindow()!=target)throw new Exception("Capture foreground changed while placing background");
 }
 public static void Close(){if(window!=IntPtr.Zero){if(!DestroyWindow(window))throw new Exception("Cannot close owned capture background");window=IntPtr.Zero;}}
}
'@
