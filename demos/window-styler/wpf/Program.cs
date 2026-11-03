using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;

internal static class Program
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool SetPropW(IntPtr window, string name, IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetPropW(IntPtr window, string name);

    [STAThread]
    private static void Main()
    {
        var application = new Application { ShutdownMode = ShutdownMode.OnLastWindowClose };
        for (int i = 0; i < 2; ++i)
        {
            var panel = new StackPanel { Margin = new Thickness(28) };
            panel.Children.Add(new TextBlock { Text = "Actual WPF content", FontSize = 24, Margin = new Thickness(0, 0, 0, 24) });
            panel.Children.Add(new TextBox { Text = "This WPF text box remains in the original client area." });
            var window = new Window
            {
                Title = i == 0 ? "Actual WPF - Native" : "Actual WPF - Frameless",
                WindowStyle = i == 0 ? WindowStyle.SingleBorderWindow : WindowStyle.None,
                ResizeMode = ResizeMode.CanResize,
                Width = 600,
                Height = 360,
                Left = 160 + i * 80,
                Top = 120 + i * 60,
                Background = new SolidColorBrush(Color.FromRgb(247, 244, 238)),
                Content = panel
            };
            window.ContentRendered += (_, _) =>
            {
                IntPtr handle = new WindowInteropHelper(window).Handle;
                long count = GetPropW(handle, "Macdows.WindowStyler.Sample.PaintCount").ToInt64();
                SetPropW(handle, "Macdows.WindowStyler.Sample.PaintCount", (IntPtr)(count + 1));
            };
            window.SourceInitialized += (_, _) =>
            {
                IntPtr handle = new WindowInteropHelper(window).Handle;
                SetPropW(handle, "Macdows.WindowStyler.Sample", (IntPtr)1);
                HwndSource.FromHwnd(handle).AddHook((IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled) =>
                {
                    if (message == 0x000F)
                    {
                        long count = GetPropW(hwnd, "Macdows.WindowStyler.Sample.PaintCount").ToInt64();
                        SetPropW(hwnd, "Macdows.WindowStyler.Sample.PaintCount", (IntPtr)(count + 1));
                    }
                    return IntPtr.Zero;
                });
            };
            window.Show();
        }
        application.Run();
    }
}
