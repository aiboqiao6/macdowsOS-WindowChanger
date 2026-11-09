using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Macdows.AvaloniaBridge;

// This assembly deliberately has no Avalonia references. Component hosts may
// load their real Application into an isolated context rather than Default.
public static class Entry
{
    [UnmanagedCallersOnly]
    public static int CoreCall(IntPtr value, int length)
    {
        try
        {
            var argument = Marshal.PtrToStringUni(value, length)!;
            var parts = argument.Split('|');
            var hwnd = new IntPtr(long.Parse(parts[0]));
            var trace = new List<string>();
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                var application = assembly.GetType("Avalonia.Application")?.GetProperty("Current")?.GetValue(null);
                if (assembly.GetType("Avalonia.Application") != null) trace.Add($"{AssemblyLoadContext.GetLoadContext(assembly)?.Name}: {application?.GetType().FullName ?? "no application"}");
                var lifetime = application?.GetType().GetProperty("ApplicationLifetime")?.GetValue(application);
                if (lifetime?.GetType().GetProperty("Windows")?.GetValue(lifetime) is not IEnumerable windows)
                    continue;
                foreach (var window in windows)
                {
                    var platform = window.GetType().GetMethod("TryGetPlatformHandle")?.Invoke(window, null);
                    trace.Add($"window {window.GetType().FullName} handle={platform?.GetType().GetProperty("Handle")?.GetValue(platform)} target={hwnd}");
                    if (platform?.GetType().GetProperty("Handle")?.GetValue(platform) is not IntPtr handle || handle != hwnd)
                        continue;
                    var context = AssemblyLoadContext.GetLoadContext(assembly)!;
                    var file = Path.GetFullPath(parts[6]);
                    var backend = context.Assemblies.FirstOrDefault(a => a.Location == file) ?? context.LoadFromAssemblyPath(file);
                    var call = backend.GetType("Macdows.AvaloniaInline.Entry")!.GetMethod("Call")!;
                    return (int)call.Invoke(null, new object[] { window, argument })!;
                }
            }
            var directory = Path.GetDirectoryName(typeof(Entry).Assembly.Location)!;
            if (File.Exists(Path.Combine(directory, "layout-diagnostics.enabled")))
                File.WriteAllLines(Path.Combine(directory, "avalonia-bridge-search.txt"), trace);
        }
        catch (Exception error)
        {
            var directory = Path.GetDirectoryName(typeof(Entry).Assembly.Location)!;
            if (File.Exists(Path.Combine(directory, "layout-diagnostics.enabled")))
                try { File.WriteAllText(Path.Combine(directory, "avalonia-bridge-error.txt"), error.ToString()); } catch { }
        }
        return 50;
    }
}
