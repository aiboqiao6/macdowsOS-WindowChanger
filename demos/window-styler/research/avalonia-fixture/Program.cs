using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using FluentAvalonia.Styling;
using FluentAvalonia.UI.Windowing;
using Path = System.IO.Path;

namespace OwnedAvaloniaFixture;

internal static class Program
{
    public static string Output = "";
    [STAThread] public static void Main(string[] args)
    {
        Output = Path.GetFullPath(args.Single(a => a.StartsWith("--fixture-output=")).Split('=', 2)[1]);
        Directory.CreateDirectory(Output);
        AppBuilder.Configure<App>().UsePlatformDetect().StartWithClassicDesktopLifetime(args);
    }
}
public sealed class App : Application
{
    public override void Initialize() => Styles.Add(new FluentAvaloniaTheme());
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime lifetime)
        {
            RequestedThemeVariant = lifetime.Args?.Contains("--fixture-light") == true ? ThemeVariant.Light : ThemeVariant.Dark;
            lifetime.MainWindow = new FixtureWindow();
        }
        base.OnFrameworkInitializationCompleted();
    }
}
public sealed class FixtureWindow : AppWindow
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool SetProp(IntPtr h, string name, IntPtr value);
    Grid lane = null!;
    readonly TextBox entry = new() { Text = "Keep this original text", Width = 300 };
    readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(150) };
    int lastCommand;
    int rebuilt;
    int actionCount;
    int closeRequests;
    bool allowClose;
    public FixtureWindow()
    {
        Title = "Owned Avalonia title fixture";
        Width = 1000; Height = 650; MinWidth = 720; MinHeight = 400;
        Topmost = true;
        TitleBar.ExtendsContentIntoTitleBar = true;
        TitleBar.TitleBarHitTestType = TitleBarHitTestType.Complex;
        TitleBar.Height = 40;
        Build();
        Closing += (_, e) => { closeRequests++; e.Cancel = !allowClose; };
        Opened += (_, _) => {
            SetProp(TryGetPlatformHandle()!.Handle, "Macdows.WindowStyler.Sample.PaintCount", new IntPtr(1));
            Save("ready.json", new { hwnd = TryGetPlatformHandle()!.Handle.ToInt64(), pid = Environment.ProcessId,
                avalonia = typeof(Window).Assembly.FullName, fluent = typeof(AppWindow).Assembly.FullName });
            timer.Start();
        };
        timer.Tick += (_, _) => { try { Tick(); } catch (Exception error) { Save("tick-error.json", new { error = error.ToString() }); } };
        Closed += (_, _) => timer.Stop();
    }
    void Build()
    {
        Save("build-phase.json", new { rebuilt, phase = "start" });
        if (entry.Parent is StackPanel old) old.Children.Remove(entry);
        Save("build-phase.json", new { rebuilt, phase = "entry detached" });
        var root = new Grid { RowDefinitions = new RowDefinitions("40,*") };
        lane = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"),
            Margin = new Thickness(0, 0, 140, 0), Background = Brushes.Transparent };
        var title = new TextBlock { Text = "Original title content", VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(14, 0, 0, 0) };
        lane.Children.Add(title);
        var toolbar = new Button { Content = "Original toolbar action", VerticalAlignment = VerticalAlignment.Center };
        toolbar.Click += (_, _) => actionCount++;
        Grid.SetColumn(toolbar, 2); lane.Children.Add(toolbar);
        var titleHost = new Border { Child = lane, Height = 40 };
        root.Children.Add(titleHost);
        var body = new StackPanel { Margin = new Thickness(24), Spacing = 18 };
        body.Children.Add(new TextBlock { Text = "The title row, toolbar and editor must stay visible." });
        body.Children.Add(entry);
        body.Children.Add(new TextBlock { Text = "This is an owned test window. No user application is closed or rethemed." });
        Grid.SetRow(body, 1); root.Children.Add(body);
        Content = root;
        Save("build-phase.json", new { rebuilt, phase = "content assigned" });
    }
    void Save(string name, object value) => File.WriteAllText(Path.Combine(Program.Output, name), JsonSerializer.Serialize(value));
    void Tick()
    {
        var commandFile = Path.Combine(Program.Output, "command.json");
        if (File.Exists(commandFile)) try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(commandFile));
            var command = document.RootElement;
            var id = command.GetProperty("id").GetInt32();
            if (id != lastCommand)
            {
                lastCommand = id;
                var action = command.GetProperty("action").GetString();
                Save("command-start.json", new { lastCommand, action });
                if (action == "theme") Application.Current!.RequestedThemeVariant = command.GetProperty("value").GetString() == "light" ? ThemeVariant.Light : ThemeVariant.Dark;
                if (action == "capabilities") { CanMinimize = command.GetProperty("minimize").GetBoolean(); CanMaximize = command.GetProperty("maximize").GetBoolean(); }
                if (action == "refresh")
                {
                    var group = this.GetVisualDescendants().OfType<MinMaxCloseControl>().Single();
                    group.IsVisible = true; group.IsEnabled = true; group.IsHitTestVisible = true;
                    lane.Margin = new Thickness(12, 0, 140, 0);
                }
                if (action == "rebuild") { rebuilt++; Build(); }
                if (action == "restore") WindowState = WindowState.Normal;
                if (action == "close") { allowClose = true; Close(); }
                if (action == "click")
                {
                    var label = command.GetProperty("label").GetString();
                    var button = this.GetVisualDescendants().OfType<Button>().Single(b =>
                        Avalonia.Automation.AutomationProperties.GetName(b) == label);
                    if (button.IsEnabled) button.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));
                }
            }
        }
        catch (Exception error) { Save("command-error.json", new { lastCommand, error = error.ToString() }); }
        var original = this.GetVisualDescendants().OfType<MinMaxCloseControl>().SingleOrDefault();
        var lights = this.GetVisualDescendants().OfType<Canvas>().FirstOrDefault(c => c.Children.Count == 3 && c.Children.All(b => b is Button));
        Save("status.json", new { lastCommand, rebuilt, actionCount, closeRequests, entry.Text, theme = ActualThemeVariant.ToString(),
            state = WindowState.ToString(), laneLeft = lane.Margin.Left,
            originalVisible = original?.IsVisible, originalEnabled = original?.IsEnabled,
            originalHitTest = original?.IsHitTestVisible,
            originalFocusable = original?.Focusable,
            originalButtonHitTest = original?.GetVisualDescendants().OfType<Button>().Select(b => b.IsHitTestVisible),
            originalCenters = original?.GetVisualDescendants().OfType<Button>().Where(b => b.IsAttachedToVisualTree()).Select(b => new { b.Name,
                point = b.PointToScreen(new Point(b.Bounds.Width / 2, b.Bounds.Height / 2)) }),
            lightColors = lights?.GetVisualDescendants().OfType<Ellipse>().Select(e =>
                e.Fill is ISolidColorBrush brush ? "#" + brush.Color.ToUInt32().ToString("x8") : null),
            enabled = lights?.Children.OfType<Button>().Select(b => b.IsEnabled),
            bounds = lane.Bounds });
    }
}
