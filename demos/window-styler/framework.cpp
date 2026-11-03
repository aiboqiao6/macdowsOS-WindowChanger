#include "framework.h"
#include "module-capabilities.h"
#include <tlhelp32.h>
#include <set>
#include <cwctype>
namespace styler {
FrameworkInfo DetectFramework(HWND window) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    std::wstring cls(name);
    struct Children {
        bool netui{}, island{}, winui3{}, bcg{};
    } children;
    EnumChildWindows(
        window,
        [](HWND w, LPARAM p) -> BOOL {
            auto &c = *reinterpret_cast<Children *>(p);
            wchar_t name[256]{};
            GetClassNameW(w, name, 256);
            std::wstring_view value(name);
            c.netui |= value == L"NetUIHWND";
            c.island |= value == L"Windows.UI.Composition.DesktopWindowContentBridge";
            c.winui3 |=
                value == L"Microsoft.UI.Content.DesktopChildSiteBridge" || value == L"ReunionWindowingCaptionControls";
            c.bcg |= value.starts_with(L"BCGPRibbonBar:");
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&children));
    // Framework evidence is present even while a window is minimized. Do not
    // infer a native caption from its temporary zero-sized client rectangle.
    if (children.winui3)
        return {L"winui3", L"WinUI 3 / Windows App SDK", L"Microsoft.UI.Content / 原 XAML 与 Windowing 控件树"};
    if (children.bcg)
        return {L"mfc-bcg", L"MFC / BCG Ribbon", L"BCGPRibbonBar / 原自绘标题布局"};
    if (cls.starts_with(L"Afx"))
        return {L"mfc", L"MFC", L"CWnd / 原生或自绘非客户区"};
    if (cls == L"TXGuiFoundation")
        return {L"txgui", L"TXGuiFoundation", L"自绘控件树 / 非系统标题布局"};
    if (cls.starts_with(L"Chrome_")) {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        bool electron = false, chromium = false;
        HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W e{sizeof(e)};
            if (Module32FirstW(snapshot, &e))
                do {
                    if (process && RemoteExport(process, e, "napi_get_cb_info") &&
                        RemoteExport(process, e, "?TryGetCurrent@Isolate@v8@@SAPEAV12@XZ"))
                        electron = true;
                    chromium |= _wcsicmp(e.szModule, L"msedge.dll") == 0 || _wcsicmp(e.szModule, L"chrome.dll") == 0;
                } while (Module32NextW(snapshot, &e));
            CloseHandle(snapshot);
        }
        if (process)
            CloseHandle(process);
        if (electron)
            return {L"chromium", L"Electron", L"Node-API / V8 导出能力 + renderer 原控件树"};
        if (chromium)
            return {L"chromium-views", L"Chromium Views", L"原生浏览器控件树"};
        return {L"chromium-unknown", L"Chromium / 运行时未确认", L"未确认 Electron 或原生 Views 宿主"};
    }
    if (children.netui) {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        bool systemRibbon = false;
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W e{sizeof(e)};
            if (Module32FirstW(snapshot, &e))
                do {
                    systemRibbon |= _wcsicmp(e.szModule, L"UIRibbon.dll") == 0;
                } while (Module32NextW(snapshot, &e));
            CloseHandle(snapshot);
        }
        return systemRibbon
                   ? FrameworkInfo{L"netui", L"NetUI / Windows Ribbon", L"UIRibbon.dll / 原标题行与控件树"}
                   : FrameworkInfo{L"office-netui", L"Office NetUI", L"Office NetUI 控件树 / 与系统 UIRibbon ABI 分离"};
    }
    if (cls == L"CabinetWClass")
        return {L"win32", L"Win32 Shell", L"Shell 标准非客户区 / DirectUI 客户区"};
    if (children.island)
        return {L"winui", L"WinUI / XAML Islands", L"DesktopWindowXamlSource / 原控件树"};
    if (cls == L"ConsoleWindowClass")
        return {L"console", L"Console host", L"conhost GUI thread / 原生非客户区"};
    if (cls.starts_with(L"HwndWrapper") || cls.starts_with(L"Qt")) {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        std::set<std::wstring> modules;
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W e{sizeof(e)};
            if (Module32FirstW(snapshot, &e))
                do {
                    std::wstring module = e.szModule;
                    for (auto &ch : module)
                        ch = static_cast<wchar_t>(towlower(ch));
                    modules.insert(module);
                } while (Module32NextW(snapshot, &e));
            CloseHandle(snapshot);
        }
        if (cls.starts_with(L"HwndWrapper")) {
            if (modules.contains(L"coreclr.dll"))
                return {L"wpf", L"WPF", L"CoreCLR (.NET)"};
            if (modules.contains(L"clr.dll"))
                return {L"wpf", L"WPF", L"CLR v4 (.NET Framework)"};
            return {L"unknown", L"HwndWrapper / 运行时未确认", L"缺少已加载的 WPF 运行时"};
        }
        if (modules.contains(L"qt6core.dll"))
            return {L"qt", L"Qt 6", L"Qt6Core.dll"};
        if (modules.contains(L"qt5core.dll"))
            return {L"qt", L"Qt 5", L"Qt5Core.dll"};
        return {L"unknown", L"Qt 窗口类 / 运行时未确认", L"缺少已加载的 Qt 运行时"};
    }
    if (cls.starts_with(L"Windows.UI.") || cls == L"ApplicationFrameWindow")
        return {L"winui", L"UWP / Windows XAML", L"Windows.UI.Xaml"};
    return {L"win32", L"Win32", L"原生窗口 / 非客户区"};
}
} // namespace styler
