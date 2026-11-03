using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Text.RegularExpressions;
using System.Windows.Data;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Automation;
using System.Windows.Threading;
using Path=System.IO.Path;

namespace Macdows.WpfInline {
 public static class Entry {
#if NET8_0_OR_GREATER
  [UnmanagedCallersOnly]public static int CoreProbe(IntPtr value,int length){return Probe(Marshal.PtrToStringUni(value,length));}
  [UnmanagedCallersOnly]public static int CoreAttach(IntPtr value,int length){return Attach(Marshal.PtrToStringUni(value,length));}
#endif
  static readonly Dictionary<IntPtr,Lease> leases=new Dictionary<IntPtr,Lease>();
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern uint RegisterWindowMessage(string s);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool SetProp(IntPtr h,string s,IntPtr v);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr RemoveProp(IntPtr h,string s);
  [DllImport("user32.dll")]static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
  [DllImport("kernel32.dll")]static extern uint WaitForSingleObject(IntPtr h,uint timeout);
  [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr h);
  [DllImport("user32.dll")]static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")]static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")]static extern IntPtr SetCapture(IntPtr h);
  [DllImport("user32.dll")]static extern IntPtr GetCapture();
  [DllImport("user32.dll")]static extern bool ReleaseCapture();
  [StructLayout(LayoutKind.Sequential)]struct P {public int x,y;}
  [DllImport("user32.dll")]static extern bool ClientToScreen(IntPtr h,ref P p);
  const string Owner="Macdows.WindowStyler.Owner.v1",Inline="Macdows.WindowStyler.Inline.v4",Adapter="Macdows.WindowStyler.Adapter.v1";
  static readonly uint Query=RegisterWindowMessage("Macdows.WindowStyler.Query.v1"),Detach=RegisterWindowMessage("Macdows.WindowStyler.Detach.v1"),Diagnostic=RegisterWindowMessage("Macdows.WindowStyler.Diagnostics.v1");
  const int Ack=0x4D414357;
  static HwndSource Source(string value) { return HwndSource.FromHwnd(new IntPtr(long.Parse(value.Split('|')[0]))); }
  static void Walk(DependencyObject item, StringBuilder text, int depth) {
   if(item==null || depth>18)return;
   FrameworkElement e=item as FrameworkElement;
   if(e!=null && e.IsVisible) {
    Point p=new Point();try{p=e.TranslatePoint(new Point(),(UIElement)PresentationSource.FromVisual(e).RootVisual);}catch{}
    if(p.Y<85)text.AppendLine(new string(' ',depth)+e.GetType().FullName+" Name="+e.Name+" xy="+p+" size="+e.ActualWidth+","+e.ActualHeight+" grid="+Grid.GetColumn(e)+","+Grid.GetRow(e)+" action="+Action(e)+" automation="+AutomationProperties.GetName(e)+" tooltip="+(e.ToolTip as string));
   }
   int count=VisualTreeHelper.GetChildrenCount(item);for(int n=0;n<count;n++)Walk(VisualTreeHelper.GetChild(item,n),text,depth+1);
  }
  static FrameworkElement Find(DependencyObject root,string name) {
   if(root==null)return null;
   var e=root as FrameworkElement;if(e!=null && e.Name==name)return e;
   for(int n=0;n<VisualTreeHelper.GetChildrenCount(root);n++){var found=Find(VisualTreeHelper.GetChild(root,n),name);if(found!=null)return found;}return null;
  }
  static bool Layout(HwndSource source,out FrameworkElement title,out FrameworkElement buttons,out Grid grid){
   title=null;buttons=null;grid=null;
   var controls=new List<FrameworkElement>();CollectControls(source.RootVisual,source.RootVisual as UIElement,controls,0);
   foreach(var close in controls){
    if(Action(close)!=0)continue;
    for(DependencyObject group=VisualTreeHelper.GetParent(close);group!=null;group=VisualTreeHelper.GetParent(group)){
     var panel=group as FrameworkElement;if(panel==null||panel.ActualHeight>64||panel.ActualWidth>240)break;
     bool min=false,max=false;foreach(var control in controls)if(Descendant(control,group)){min|=Action(control)==1;max|=Action(control)==2;}
     if(!min||!max)continue;
     FrameworkElement selected=null;Grid selectedGrid=null;
     for(DependencyObject row=VisualTreeHelper.GetParent(group);row!=null;row=VisualTreeHelper.GetParent(row)){
      var element=row as FrameworkElement;if(element==null||element.ActualHeight>64)break;
      var parent=VisualTreeHelper.GetParent(row) as Grid;
      if(parent!=null&&element.ActualHeight>=24&&element.ActualWidth>=300){selected=element;selectedGrid=parent;}
     }
     if(selected!=null){title=selected;buttons=panel;grid=selectedGrid;return true;}
    }
   }
   return false;
  }
  static bool Descendant(DependencyObject node,DependencyObject ancestor){for(var p=node;p!=null;p=VisualTreeHelper.GetParent(p))if(p==ancestor)return true;return false;}
  static int Action(FrameworkElement e){
   var b=e as ButtonBase;if(b==null)return -1;
   if(b.Command==SystemCommands.CloseWindowCommand)return 0;if(b.Command==SystemCommands.MinimizeWindowCommand)return 1;if(b.Command==SystemCommands.MaximizeWindowCommand||b.Command==SystemCommands.RestoreWindowCommand)return 2;
   string command=b.Command is RoutedCommand?((RoutedCommand)b.Command).Name:"";
   string label=e.Name+" "+AutomationProperties.GetName(e)+" "+command+" "+(e.ToolTip as string);
   if(Regex.IsMatch(label,"close|关闭",RegexOptions.IgnoreCase))return 0;if(Regex.IsMatch(label,"minimi|最小化",RegexOptions.IgnoreCase))return 1;if(Regex.IsMatch(label,"maximi|restore|最大化|还原",RegexOptions.IgnoreCase))return 2;return -1;
  }
  static void CollectControls(DependencyObject node,UIElement root,List<FrameworkElement> output,int depth){
   if(node==null||root==null||depth>24)return;var e=node as FrameworkElement;
   if(e is ButtonBase&&e.IsVisible&&Action(e)>=0){try{var p=e.TranslatePoint(new Point(),root);if(p.Y>=-4&&p.Y<64&&e.ActualHeight<=64&&e.ActualWidth<=100)output.Add(e);}catch{}}
   for(int i=0;i<VisualTreeHelper.GetChildrenCount(node);i++)CollectControls(VisualTreeHelper.GetChild(node,i),root,output,depth+1);
  }
  static FrameworkElement FindType(DependencyObject root,string type){
   if(root==null)return null;if(root.GetType().FullName==type)return root as FrameworkElement;
   for(int n=0;n<VisualTreeHelper.GetChildrenCount(root);n++){var found=FindType(VisualTreeHelper.GetChild(root,n),type);if(found!=null)return found;}return null;
  }
  public static int Probe(string value) {
   try {
    HwndSource s=Source(value);if(s==null || !s.Dispatcher.CheckAccess())return 50;
    if(File.Exists(Path.Combine(Path.GetDirectoryName(typeof(Entry).Assembly.Location),"layout-diagnostics.enabled"))){var text=new StringBuilder();text.AppendLine("Domain="+AppDomain.CurrentDomain.FriendlyName);Walk(s.RootVisual,text,0);File.WriteAllText(Path.Combine(Path.GetDirectoryName(typeof(Entry).Assembly.Location),"wpf-tree-"+System.Diagnostics.Process.GetCurrentProcess().Id+".txt"),text.ToString());}
    FrameworkElement title,buttons;Grid grid;return Layout(s,out title,out buttons,out grid)?0:50;
   }catch(Exception e){try{File.WriteAllText(Path.Combine(Path.GetDirectoryName(typeof(Entry).Assembly.Location),"wpf-error.txt"),e.ToString());}catch{}return 50;}
  }
  sealed class Saved {
   readonly DependencyObject target;readonly DependencyProperty property;object local;BindingBase binding;
   DependencyPropertyDescriptor descriptor;EventHandler changed;bool enforcing;
   public Saved(DependencyObject t,DependencyProperty p){target=t;property=p;local=t.ReadLocalValue(p);binding=BindingOperations.GetBindingBase(t,p);}
   public void Restore(){if(binding!=null)BindingOperations.SetBinding(target,property,binding);else if(local==DependencyProperty.UnsetValue)target.ClearValue(property);else target.SetValue(property,local);}
   public void Watch(Func<object,object> apply){descriptor=DependencyPropertyDescriptor.FromProperty(property,target.GetType());changed=(s,e)=>{if(enforcing)return;local=target.ReadLocalValue(property);binding=BindingOperations.GetBindingBase(target,property);enforcing=true;try{target.SetValue(property,apply(target.GetValue(property)));}finally{enforcing=false;}};descriptor.AddValueChanged(target,changed);}
   public void Stop(){if(descriptor!=null){descriptor.RemoveValueChanged(target,changed);descriptor=null;}}
  }
  sealed class Lease {
   public HwndSource source;public FrameworkElement title,buttons;public Grid grid;public Canvas lights;public IntPtr window,owner,process;public long adapter;public DispatcherTimer timer;
   public readonly List<Saved> saved=new List<Saved>();public bool restoring;int pressed=-1;
   int HitButton(IntPtr point){int x=unchecked((short)(point.ToInt64()&65535)),y=unchecked((short)((point.ToInt64()>>16)&65535));double scale=source.CompositionTarget.TransformToDevice.M11;double px=(x-Metric(7))/scale,py=(y-Metric(8))/scale;if(py<0||py>=lights.ActualHeight||px<8||px>=68)return -1;return (int)((px-8)/20);}
   public void Command(int action){PostMessage(window,0x112,new IntPtr(action==0?0xF060:action==1?0xF020:IsZoomed(window)?0xF120:0xF030),IntPtr.Zero);}
   public IntPtr Hook(IntPtr h,int message,IntPtr w,IntPtr l,ref bool handled){
    uint m=unchecked((uint)message);
    if(m==Query){handled=true;return new IntPtr(Ack);}
    if(m==Detach){handled=true;if(w!=IntPtr.Zero && w!=owner)return IntPtr.Zero;Restore();return new IntPtr(Ack);}
    if(m==Diagnostic){handled=true;return new IntPtr(Metric(w.ToInt32()));}
    if(m==0x201){int hit=HitButton(l);if(hit>=0){pressed=hit;SetCapture(window);handled=true;return IntPtr.Zero;}}
    if(m==0x202 && pressed>=0){int hit=HitButton(l),action=pressed;pressed=-1;if(GetCapture()==window)ReleaseCapture();if(hit==action)Command(action);handled=true;return IntPtr.Zero;}
    if(m==0x215 || m==0x1F)pressed=-1;
    if(m==0x84 && lights!=null){int x=unchecked((short)(l.ToInt64()&65535)),y=unchecked((short)((l.ToInt64()>>16)&65535));var r=lights.PointToScreen(new Point());var scale=source.CompositionTarget.TransformToDevice;if(x>=r.X && x<r.X+84*scale.M11 && y>=r.Y && y<r.Y+lights.ActualHeight*scale.M22){handled=true;return new IntPtr(1);}}
    return IntPtr.Zero;
   }
   public long Metric(int n){
    if(lights==null)return 0;P origin=new P();ClientToScreen(window,ref origin);Point p=lights.PointToScreen(new Point());var scale=source.CompositionTarget.TransformToDevice;
    if(n==6)return adapter;if(n==7)return (long)Math.Round(p.X-origin.x);if(n==8)return (long)Math.Round(p.Y-origin.y);if(n==9)return (long)Math.Round(lights.ActualWidth*scale.M11);if(n==10)return (long)Math.Round(lights.ActualHeight*scale.M22);
    if(n==11)return (long)Math.Round(title.TranslatePoint(new Point(),(UIElement)source.RootVisual).X*scale.M11);if(n==12)return buttons.IsVisible?1:0;if(n==13)return 3;return 0;
   }
   public void Disposed(object sender,EventArgs e){Restore();}
   public void Restore(){
    if(restoring)return;restoring=true;if(pressed>=0 && GetCapture()==window)ReleaseCapture();pressed=-1;if(timer!=null)timer.Stop();
    foreach(var item in saved)item.Stop();
    if(title!=null)title.Unloaded-=Unloaded;
    if(source!=null){source.Disposed-=Disposed;try{source.RemoveHook(Hook);}catch{}}
    try{if(grid!=null && lights!=null)grid.Children.Remove(lights);}catch{}
    for(int n=saved.Count-1;n>=0;n--)try{saved[n].Restore();}catch{}
    try{if(title!=null)title.UpdateLayout();}catch{}
    RemoveProp(window,Owner);RemoveProp(window,Inline);RemoveProp(window,Adapter);if(process!=IntPtr.Zero){CloseHandle(process);process=IntPtr.Zero;}leases.Remove(window);
   }
   public void Unloaded(object sender,RoutedEventArgs e){source.Dispatcher.BeginInvoke(new Action(()=>{if(!restoring&&!title.IsLoaded)Restore();}));}
  }
  public static int Attach(string value){
   Lease lease=null;
   try {
    var parts=value.Split('|');HwndSource source=Source(value);FrameworkElement title,buttons;Grid grid;
    if(source==null || !source.Dispatcher.CheckAccess() || !Layout(source,out title,out buttons,out grid))return 50;
    lease=new Lease{source=source,title=title,buttons=buttons,grid=grid,window=source.Handle,owner=new IntPtr(long.Parse(parts[1])),adapter=long.Parse(parts[2])};
    uint pid;GetWindowThreadProcessId(lease.owner,out pid);lease.process=OpenProcess(0x100000,false,pid);if(lease.process==IntPtr.Zero)return 5;
    lease.timer=new DispatcherTimer(DispatcherPriority.Background,source.Dispatcher);lease.timer.Interval=TimeSpan.FromMilliseconds(750);var current=lease;
    lease.timer.Tick+=(s,e)=>{if(!IsWindow(current.owner)||!current.title.IsLoaded||WaitForSingleObject(current.process,0)!=258)current.Restore();};
    lease.saved.Add(new Saved(title,FrameworkElement.MarginProperty));lease.saved.Add(new Saved(buttons,UIElement.VisibilityProperty));
    lease.lights=new Canvas{Width=84,HorizontalAlignment=HorizontalAlignment.Left,VerticalAlignment=VerticalAlignment.Stretch};
    Grid.SetRow(lease.lights,Grid.GetRow(title));Grid.SetColumn(lease.lights,Grid.GetColumn(title));Panel.SetZIndex(lease.lights,1000);
    string[] colors={"#FF5F57","#FFBD2E","#28C840"},names={"关闭窗口","最小化窗口","最大化或还原窗口"};
    for(int i=0;i<3;i++){
     int action=i;var button=new Button{Width=20,Background=Brushes.Transparent,BorderThickness=new Thickness(0),Focusable=true,ToolTip=names[i]};
     button.SetBinding(FrameworkElement.HeightProperty,new Binding("ActualHeight"){Source=title});
     var ellipse=new FrameworkElementFactory(typeof(Ellipse));ellipse.SetValue(FrameworkElement.WidthProperty,12.0);ellipse.SetValue(FrameworkElement.HeightProperty,12.0);ellipse.SetValue(Shape.FillProperty,(Brush)new BrushConverter().ConvertFromString(colors[i]));ellipse.SetValue(FrameworkElement.VerticalAlignmentProperty,VerticalAlignment.Center);ellipse.SetValue(FrameworkElement.HorizontalAlignmentProperty,HorizontalAlignment.Center);
     button.Template=new ControlTemplate(typeof(Button)){VisualTree=ellipse};AutomationProperties.SetName(button,names[i]);System.Windows.Shell.WindowChrome.SetIsHitTestVisibleInChrome(button,true);Canvas.SetLeft(button,8+i*20);lease.lights.Children.Add(button);
     button.Click+=(s,e)=>{current.Command(action);};
    }
    source.Disposed+=lease.Disposed;source.AddHook(lease.Hook);buttons.Visibility=Visibility.Collapsed;var margin=title.Margin;title.Margin=new Thickness(margin.Left+84,margin.Top,margin.Right,margin.Bottom);grid.Children.Add(lease.lights);grid.UpdateLayout();
    lease.saved[0].Watch(v=>{var p=(Thickness)v;return new Thickness(p.Left+84,p.Top,p.Right,p.Bottom);});lease.saved[1].Watch(v=>Visibility.Collapsed);title.Unloaded+=lease.Unloaded;
    if(!SetProp(lease.window,Owner,lease.owner)||!SetProp(lease.window,Inline,new IntPtr(3))||!SetProp(lease.window,Adapter,new IntPtr(lease.adapter))){lease.Restore();return 5;}
    leases[lease.window]=lease;lease.timer.Start();return 0;
   }catch{if(lease!=null)try{lease.Restore();}catch{}return 50;}
  }
 }
}
