using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Automation;
public static class WpfInlineTarget {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool SetProp(IntPtr h,string s,IntPtr v);
 [STAThread]public static void Main(){
  var app=new Application();var window=new Window{Title="WPF framework inline compatibility target",Width=920,Height=600,WindowStyle=WindowStyle.None,Background=new SolidColorBrush(Color.FromRgb(32,32,32))};
  var root=new Grid();root.RowDefinitions.Add(new RowDefinition{Height=new GridLength(40)});root.RowDefinitions.Add(new RowDefinition());
  var title=new Grid{Name="ArbitraryHeader",Background=new SolidColorBrush(Color.FromRgb(45,45,45))};title.ColumnDefinitions.Add(new ColumnDefinition());title.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
  title.Children.Add(new TextBlock{Text="Original WPF title content",Foreground=Brushes.White,VerticalAlignment=VerticalAlignment.Center,Margin=new Thickness(12,0,0,0)});
  var buttons=new StackPanel{Name="ArbitraryCommands",Orientation=Orientation.Horizontal};Grid.SetColumn(buttons,1);int index=0;foreach(string label in new[]{"_","□","×"}){var button=new Button{Content=label,Width=44};AutomationProperties.SetName(button,new[]{"Minimize","Maximize","Close"}[index++]);buttons.Children.Add(button);}title.Children.Add(buttons);root.Children.Add(title);
  var body=new TextBlock{Text="Original client content\nNo style adapter is linked into this application.",Foreground=Brushes.White,Margin=new Thickness(24),FontSize=22};Grid.SetRow(body,1);root.Children.Add(body);window.Content=root;
  window.Loaded+=(s,e)=>{var h=new WindowInteropHelper(window).Handle;SetProp(h,"Macdows.WindowStyler.Sample",new IntPtr(1));SetProp(h,"Macdows.WindowStyler.Sample.PaintCount",new IntPtr(1));HwndSource.FromHwnd(h).AddHook((IntPtr hwnd,int message,IntPtr w,IntPtr l,ref bool handled)=>{if(message==0x8000+66){buttons.Visibility=w.ToInt64()==2?Visibility.Collapsed:Visibility.Visible;title.Margin=new Thickness(0);handled=true;return new IntPtr(1);}return IntPtr.Zero;});};app.Run(window);
 }
}
