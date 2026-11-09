using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Shapes;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.VisualTree;
using Path = System.IO.Path;

namespace Macdows.AvaloniaInline;

public static class Entry
{
    static readonly Dictionary<Window, Lease> Leases = new();
    const string CaptionType = "FluentAvalonia.UI.Windowing.MinMaxCloseControl";
    const string WindowType = "FluentAvalonia.UI.Windowing.AppWindow";

    static bool Shown(Visual visual) => visual.GetVisualAncestors().Prepend(visual).All(v => v.IsVisible);
    static Rect RectIn(Control control, Visual root)
    {
        var point = control.TranslatePoint(default, root) ?? throw new InvalidOperationException("Detached title tree");
        return new Rect(point, control.Bounds.Size);
    }
    static bool Painted(Control control) => control is Button || control is Image { Source: not null } ||
        control is TextBlock { Text.Length: > 0 };
    static int Depth(Visual control) => control.GetVisualAncestors().Count();
    sealed record Layout(Window Window, Panel Parent, Control Group, Panel DefaultTitle,
        Control Lane, Button[] Original, bool Custom);

    static Layout? Find(Window window)
    {
        Layout? Reject(string reason)
        {
            var directory = Path.GetDirectoryName(typeof(Entry).Assembly.Location)!;
            if (File.Exists(Path.Combine(directory, "layout-diagnostics.enabled")))
                try { File.WriteAllText(Path.Combine(directory, "avalonia-probe.txt"), reason); } catch { }
            return null;
        }
        // Only public Avalonia 11.3.12 and the verified FluentAvalonia title
        // contract are eligible. No application executable or title is matched.
        if (!window.CheckAccess() || typeof(Window).Assembly.GetName().Version != new Version(11, 3, 12, 0) ||
            window.WindowState == WindowState.FullScreen || window.FlowDirection != FlowDirection.LeftToRight)
            return Reject("Access, runtime version, fullscreen or flow direction");
        var appWindow = window.GetType();
        while (appWindow != null && appWindow.FullName != WindowType) appWindow = appWindow.BaseType;
        if (appWindow?.Assembly.GetName().Version != new Version(2, 5, 0, 0)) return Reject("Unknown FluentAvalonia version");
        var nodes = window.GetVisualDescendants().OfType<Control>().Take(2001).ToArray();
        if (nodes.Length > 2000) return Reject("Tree exceeds 2000 controls");
        var groups = nodes.Where(c => c.GetType().FullName == CaptionType &&
            c.GetType().Assembly == appWindow.Assembly && c.Name == "SystemCaptionButtons" &&
            ReferenceEquals(c.TemplatedParent, window)).ToArray();
        if (groups.Length != 1 || groups[0].GetVisualParent() is not Panel parent) return Reject($"Caption groups={groups.Length}");
        var group = groups[0];
        var title = parent.Children.OfType<Panel>().SingleOrDefault(p => p.Name == "DefaultTitleBar");
        var content = parent.Children.OfType<ContentPresenter>().SingleOrDefault(p => p.Name == "PART_ContentPresenter");
        if (title == null || content == null || !Shown(group)) return Reject("Title/content missing or group not shown");
        var box = RectIn(group, window);
        var borderOffset = window.WindowState == WindowState.Maximized ? 1 / window.RenderScaling : 0;
        if (box.Top < -borderOffset - .01 || box.Top > 8 || box.Height < 24 || box.Height > 64 || box.Width < 60 || box.Width > 240 ||
            box.Left < window.Bounds.Width * .65 || Math.Abs(title.Bounds.Height - box.Height) > 1) return Reject($"Unexpected title geometry: group={box}, title={title.Bounds}");
        var original = new[] { "CloseButton", "MinimizeButton", "MaxRestoreButton" }
            .Select(name => group.GetVisualDescendants().OfType<Button>().SingleOrDefault(b => b.Name == name)).ToArray();
        if (original.Any(b => b == null)) return Reject("Missing original buttons");
        var lanes = content.GetVisualDescendants().OfType<Control>().Where(c => Shown(c) &&
            c.Bounds.Height >= 24 && Math.Abs(c.Bounds.Height - box.Height) < 1 &&
            c.Bounds.Width >= window.Bounds.Width * .7 && Math.Abs(RectIn(c, window).Top - box.Top) < 1)
            .OrderBy(Depth).ToArray();
        var custom = lanes.FirstOrDefault();
        Control lane = title;
        if (custom != null)
        {
            // Shift the original content panel; the existing outer title control
            // and its theme background continue covering the entire title row.
            lane = custom is Panel ? custom : custom.GetVisualDescendants().OfType<Panel>()
                .Where(p => Math.Abs(p.Bounds.Height - box.Height) < 1 && p.Bounds.Width >= window.Bounds.Width * .6)
                .OrderBy(Depth).FirstOrDefault()!;
            if (lane == null || lane.Margin.Left < 0 || !double.IsNaN(lane.Width)) return Reject("No flexible original title panel");
        }
        else if (!title.Children.Any(c => Shown(c) && Painted(c))) return Reject("No visible original title contents");
        return new Layout(window, parent, group, title, lane, original!, custom != null);
    }

    public static int Call(object target, string argument)
    {
        Lease? created = null;
        try
        {
            if (target is not Window window || !window.CheckAccess()) return 50;
            var parts = argument.Split('|');
            Leases.TryGetValue(window, out var current);
            switch (parts[3])
            {
                case "probe": return Find(window) == null ? 50 : 0;
                case "attach":
                    if (current != null) return 183;
                    var layout = Find(window);
                    if (layout == null) return 50;
                    created = new Lease(layout);
                    Leases[window] = created;
                    created.Attach();
                    return 0;
                case "detach": current?.Restore(); return 0;
                case "validate": return current?.Validate() == true ? 0 : 50;
                case "metric": return current?.Metric(int.Parse(parts[4]), int.Parse(parts[5])) ?? 0;
            }
        }
        catch (Exception error)
        {
            created?.Restore();
            var directory = Path.GetDirectoryName(typeof(Entry).Assembly.Location)!;
            if (File.Exists(Path.Combine(directory, "layout-diagnostics.enabled")))
                try { File.WriteAllText(Path.Combine(directory, "avalonia-error.txt"), error.ToString()); } catch { }
        }
        return 50;
    }

    sealed class Lease
    {
        readonly Layout layout;
        readonly List<IDisposable> values = new();
        readonly Dictionary<Control, Rect> before = new();
        readonly Canvas lights;
        readonly Button[] buttons = new Button[3];
        bool restoring;
        bool attached;
        bool layoutValid = true;
        public Lease(Layout value)
        {
            layout = value;
            lights = new Canvas { Width = 84, Height = value.Group.Bounds.Height,
                HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top,
                Margin = new Thickness(0, value.Group.Margin.Top, 0, 0), ZIndex = value.Group.ZIndex + 1 };
        }
        void Set<T>(AvaloniaObject control, StyledProperty<T> property, T value)
        {
            if (control.IsAnimating(property)) throw new InvalidOperationException("Original property is animated");
            values.Add(control.SetValue(property, value, BindingPriority.Animation) ??
                throw new InvalidOperationException("Property has no reversible override"));
        }
        public void Attach()
        {
            var window = layout.Window;
            foreach (var control in layout.Lane.GetVisualDescendants().OfType<Control>().Prepend(layout.Lane)
                .Where(c => Shown(c) && Painted(c))) before.Add(control, RectIn(control, window));
            if (layout.Custom)
            {
                var margin = layout.Lane.Margin;
                Set(layout.Lane, Layoutable.MarginProperty,
                    new Thickness(margin.Left + 84, margin.Top, margin.Right, margin.Bottom));
            }
            else foreach (var child in layout.DefaultTitle.Children)
            {
                var margin = child.Margin;
                Set(child, Layoutable.MarginProperty,
                    new Thickness(margin.Left + 84, margin.Top, margin.Right, margin.Bottom));
            }
            // Animation-priority leases leave the application's bindings and
            // lower-priority updates alive; Dispose exposes their latest values.
            Set(layout.Group, InputElement.IsEnabledProperty, false);
            Set(layout.Group, InputElement.IsHitTestVisibleProperty, false);
            Set(layout.Group, InputElement.FocusableProperty, false);
            Set(layout.Group, Visual.IsVisibleProperty, false);
            foreach (var original in layout.Original)
            {
                Set(original, InputElement.IsHitTestVisibleProperty, false);
                Set(original, InputElement.FocusableProperty, false);
            }
            string[] colors = { "#FF5F57", "#FFBD2E", "#28C840" };
            string[] names = { "关闭窗口", "最小化窗口", "最大化或还原窗口" };
            for (int index = 0; index < 3; index++)
            {
                var action = index;
                var button = new Button { Width = 20, Height = lights.Height, Background = Brushes.Transparent,
                    Padding = new Thickness(0), BorderThickness = new Thickness(0), Focusable = true };
                button.Template = new FuncControlTemplate<Button>((control, scope) =>
                    new Border { Background = Brushes.Transparent, Child =
                    new Ellipse { Width = 12, Height = 12, Fill = control.IsEnabled ? Brush.Parse(colors[action]) :
                        Brush.Parse(window.ActualThemeVariant == Avalonia.Styling.ThemeVariant.Dark ? "#808080" : "#D0D0CF"),
                        HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                        IsHitTestVisible = false } });
                AutomationProperties.SetName(button, names[index]);
                ToolTip.SetTip(button, names[index]);
                Canvas.SetLeft(button, 8 + index * 20);
                button.Click += (_, e) => { e.Handled = true; if (button.IsEnabled) Command(action); };
                buttons[index] = button;
                lights.Children.Add(button);
            }
            UpdateCapabilities();
            layout.Parent.Children.Add(lights);
            window.UpdateLayout();
            if (!Geometry(true)) throw new InvalidOperationException("Title contents overlap, clip, or occupy the lights");
            attached = true;
            window.LayoutUpdated += LayoutUpdated;
            window.Closed += Closed;
        }
        void Command(int action)
        {
            if (restoring || !Validate() || !buttons[action].IsEnabled) return;
            // These are the public operations used by FluentAvalonia's original
            // MinMaxCloseControl. Close still goes through the app's Closing event.
            if (action == 0) layout.Window.Close();
            else if (action == 1) layout.Window.WindowState = WindowState.Minimized;
            else layout.Window.WindowState = layout.Window.WindowState == WindowState.Maximized ?
                WindowState.Normal : WindowState.Maximized;
        }
        void UpdateCapabilities()
        {
            for (var i = 0; i < 3; i++)
            {
                var original = layout.Original[i];
                var enabled = original.IsEnabled && original.IsVisible && layout.Window.IsEnabled &&
                    (i != 1 || layout.Window.CanMinimize) &&
                    (i != 2 || layout.Window.CanMaximize && layout.Window.CanResize);
                if (buttons[i] != null && buttons[i].IsEnabled != enabled)
                {
                    buttons[i].IsEnabled = enabled;
                }
                if (buttons[i] != null)
                    foreach (var ellipse in buttons[i].GetVisualDescendants().OfType<Ellipse>())
                    {
                        var color = enabled ? Color.Parse(i == 0 ? "#FF5F57" : i == 1 ? "#FFBD2E" : "#28C840") :
                            Color.Parse(layout.Window.ActualThemeVariant == Avalonia.Styling.ThemeVariant.Dark ? "#808080" : "#D0D0CF");
                        if (ellipse.Fill is not ISolidColorBrush brush || brush.Color != color)
                            ellipse.Fill = new SolidColorBrush(color);
                    }
            }
        }
        bool Geometry(bool initial)
        {
            var window = layout.Window;
            bool Reject(string reason)
            {
                var directory = Path.GetDirectoryName(typeof(Entry).Assembly.Location)!;
                if (File.Exists(Path.Combine(directory, "layout-diagnostics.enabled")))
                    try { File.WriteAllText(Path.Combine(directory, "avalonia-geometry.txt"),
                        $"state={window.WindowState} size={window.Bounds} lights={RectIn(lights, window)}\n{reason}"); } catch { }
                return false;
            }
            if (window.WindowState == WindowState.Minimized) return true;
            if (window.WindowState == WindowState.FullScreen || !lights.IsAttachedToVisualTree() ||
                !layout.Group.IsAttachedToVisualTree() || !layout.Lane.IsAttachedToVisualTree() ||
                !ReferenceEquals(layout.Group.GetVisualParent(), layout.Parent)) return Reject("Original tree detached or fullscreen");
            var area = RectIn(lights, window);
            // FluentAvalonia removes the outer one-physical-pixel border when
            // maximized. Follow that same original title row, including its
            // fractional-DIP origin, instead of treating it as a detached tree.
            var borderOffset = window.WindowState == WindowState.Maximized ? 1 / window.RenderScaling : 0;
            if (Math.Abs(area.X) > 8 || area.Y < -borderOffset - .01 || area.Y > 8 ||
                Math.Abs(area.Y - RectIn(layout.DefaultTitle, window).Y) > .01 || area.Height < 24 || area.Height > 64 ||
                Math.Abs(area.Width - 84) > 1) return Reject("Invalid lights bounds");
            var now = new Dictionary<Control, Rect>();
            foreach (var control in layout.Lane.GetVisualDescendants().OfType<Control>().Prepend(layout.Lane)
                .Where(c => Shown(c) && Painted(c)))
            {
                var rect = RectIn(control, window);
                if (rect.Width < .5 || rect.Height < .5) continue;
                if (rect.Left < area.Right - .5 || rect.Right > window.Bounds.Width + .5 ||
                    rect.Top < area.Top - .5 || rect.Bottom > area.Bottom + .5) return Reject($"Outside title: {control.GetType().Name}/{control.Name} {rect}");
                if (initial && before.TryGetValue(control, out var old) &&
                    (rect.Width < (control is TextBlock ? Math.Min(old.Width, control.DesiredSize.Width) : old.Width) - 1 ||
                     rect.Height < old.Height - 1)) return Reject($"Clipped: {control.GetType().Name}/{control.Name} {rect} before={old}");
                now.Add(control, rect);
            }
            foreach (var a in now) foreach (var b in now)
            {
                if (ReferenceEquals(a.Key, b.Key) || !before.TryGetValue(a.Key, out var oldA) ||
                    !before.TryGetValue(b.Key, out var oldB)) continue;
                if (!oldA.Intersects(oldB) && a.Value.Intersects(b.Value)) return Reject($"New overlap: {a.Key.GetType().Name} {a.Value} / {b.Key.GetType().Name} {b.Value}");
            }
            return now.Count > 0 && !layout.Group.IsVisible && !layout.Group.IsEnabled && !layout.Group.IsHitTestVisible;
        }
        void LayoutUpdated(object? sender, EventArgs e)
        {
            if (restoring || !attached) return;
            UpdateCapabilities();
            layoutValid = Geometry(false);
        }
        void Closed(object? sender, EventArgs e) => Restore();
        public bool Validate()
        {
            if (restoring || !attached) return false;
            UpdateCapabilities();
            return layoutValid && Geometry(false);
        }
        public int Metric(int metric, int button)
        {
            if (restoring) return 0;
            var scale = layout.Window.RenderScaling;
            var area = RectIn(lights, layout.Window);
            int Pixels(double value) => (int)Math.Round(value * scale);
            return metric switch {
                7 => Pixels(area.Left), 8 => Pixels(area.Top), 9 => Pixels(area.Width), 10 => Pixels(area.Height),
                11 => Pixels(layout.Custom ? RectIn(layout.Lane, layout.Window).Left : 84),
                12 => layout.Group.IsVisible ? 1 : 0, 13 => 21,
                15 => layout.Group.IsEnabled || layout.Group.IsHitTestVisible ? 1 : 0,
                16 => layout.Window.PointToScreen(new Point(area.X + 18 + Math.Clamp(button, 0, 2) * 20, area.Y)).X,
                17 => layout.Window.PointToScreen(new Point(area.X, area.Y + area.Height / 2)).Y,
                _ => 0
            };
        }
        public void Restore()
        {
            if (restoring) return;
            restoring = true;
            layout.Window.LayoutUpdated -= LayoutUpdated;
            layout.Window.Closed -= Closed;
            layout.Parent.Children.Remove(lights);
            for (int i = values.Count - 1; i >= 0; i--) values[i].Dispose();
            values.Clear();
            Leases.Remove(layout.Window);
            layout.Window.UpdateLayout();
        }
    }
}
