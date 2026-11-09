# Avalonia original title backend

This x64 backend discovers the real Application and Window in its existing
AssemblyLoadContext. It requires Avalonia 11.3.12 and FluentAvalonia 2.5, the
original AppWindow caption template, a flexible title-content panel and verified
geometry. No executable-name allowlist is used. Other templates fail closed.

It reserves 84 DIP in the original title row, inserts three Avalonia buttons in
that same tree, and overrides the old group visibility, enabled, hit-test and
focus properties with reversible animation-priority leases. Native nonclient
caption input is also suppressed because FluentAvalonia can handle it without
consulting the hidden controls. New buttons use the same public Window actions
as FluentAvalonia; Close continues through the original Closing event.

Dispose releases property overrides to expose the host current values and
bindings. Owner death, invalid layout, content-tree replacement and fullscreen
restore the original tree. Host framework assemblies are never copied or loaded
from the backend package. Diagnostic files are written only when an explicit
local `layout-diagnostics.enabled` marker exists.

Build separately with CMake, or use the main build default
`STYLER_AVALONIA_INLINE=ON`. Managed compilation requires a .NET 8 or newer SDK.
The owned fixture additionally requires a .NET 10 SDK/runtime for FluentAvalonia.

Run `research/test-avalonia-inline.ps1` with absolute RuntimeDirectory,
FixtureExecutable and OutputDirectory paths. When FixtureExecutable is a DLL,
pass its host in DotNetExecutable. The test changes only its owned windows.
It covers light/dark rendering, real mouse actions, canceled close, old-caption
input rejection, live capabilities, theme changes, host property updates,
crash recovery, tree replacement and explicit detach.

Real Watt Toolkit attachment, hidden/disabled original controls, full-window
screenshots and detach passed on 2026-11-09 with its existing dark theme. Its
position and dimensions were preserved. This does not verify every Watt page,
other Avalonia versions or every installed application.
