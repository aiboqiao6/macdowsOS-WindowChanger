#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>

namespace styler {
// ConsoleWindowClass virtualizes both the process and thread returned by
// GetWindowThreadProcessId. Enumerate the actual host's GUI threads instead.
inline DWORD WindowThreadProcessId(HWND window, DWORD *processId) {
    DWORD reported{};
    DWORD thread = GetWindowThreadProcessId(window, &reported);
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (wcscmp(name, L"ConsoleWindowClass") == 0) {
        std::vector<DWORD> hosts;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W e{sizeof(e)};
            if (Process32FirstW(snapshot, &e))
                do {
                    if (_wcsicmp(e.szExeFile, L"conhost.exe") == 0 || _wcsicmp(e.szExeFile, L"OpenConsole.exe") == 0)
                        hosts.push_back(e.th32ProcessID);
                } while (Process32NextW(snapshot, &e));
            CloseHandle(snapshot);
        }
        struct Search {
            HWND target;
            bool found{};
        } search{window};
        reported = 0;
        thread = 0;
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 e{sizeof(e)};
            if (Thread32First(snapshot, &e))
                do {
                    bool host = false;
                    for (DWORD pid : hosts)
                        if (pid == e.th32OwnerProcessID) {
                            host = true;
                            break;
                        }
                    if (!host)
                        continue;
                    EnumThreadWindows(
                        e.th32ThreadID,
                        [](HWND h, LPARAM p) -> BOOL {
                            auto &s = *reinterpret_cast<Search *>(p);
                            if (h == s.target) {
                                s.found = true;
                                return FALSE;
                            }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&search));
                    if (search.found) {
                        reported = e.th32OwnerProcessID;
                        thread = e.th32ThreadID;
                        break;
                    }
                } while (Thread32Next(snapshot, &e));
            CloseHandle(snapshot);
        }
    }
    if (processId)
        *processId = reported;
    return thread;
}
enum class Adapter : UINT_PTR {
    Native = 1,
    Extended = 2,
    Dwm = 3,
    QtInline = 4,
    Automatic = 255
};
inline bool FrameworkAdapter(Adapter a) {
    return a == Adapter::Automatic || static_cast<UINT_PTR>(a) >= 256;
}
inline constexpr wchar_t InlineProperty[] = L"Macdows.WindowStyler.Inline.v4";
inline constexpr wchar_t InlineControlProperty[] = L"Macdows.WindowStyler.Inline.Control.v4";
inline constexpr wchar_t ControllerClass[] = L"Macdows.WindowStyler.Controller.v1";
inline constexpr wchar_t TargetClass[] = L"Macdows.WindowStyler.Sample.v1";
inline constexpr wchar_t OwnerProperty[] = L"Macdows.WindowStyler.Owner.v1";
inline constexpr wchar_t ResultProperty[] = L"Macdows.WindowStyler.Result.v1";
inline constexpr wchar_t RequestProperty[] = L"Macdows.WindowStyler.Request.v1";
inline constexpr wchar_t AdapterProperty[] = L"Macdows.WindowStyler.Adapter.v1";
inline constexpr wchar_t SampleProperty[] = L"Macdows.WindowStyler.Sample";
inline constexpr wchar_t SamplePaintProperty[] = L"Macdows.WindowStyler.Sample.PaintCount";
inline constexpr wchar_t VisualMaskProperty[] = L"Macdows.WindowStyler.Visual.Accepted.v2";
inline constexpr wchar_t VisualChangedProperty[] = L"Macdows.WindowStyler.Visual.Changed.v2";
inline constexpr WPARAM Version = 0x10001;
struct ThreadBootstrapV1 {
    DWORD size = sizeof(ThreadBootstrapV1);
    DWORD version = static_cast<DWORD>(Version);
    HWND window{}, owner{};
};
inline constexpr LRESULT Acknowledged = 0x4D414357;
inline constexpr LRESULT TransitionBusy = Acknowledged + 1;
inline UINT ApplyMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Apply.v1");
    return id;
}
inline UINT DetachMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Detach.v1");
    return id;
}
inline UINT QueryMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Query.v1");
    return id;
}
inline UINT RefreshMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Refresh.v1");
    return id;
}
// Scalar-only diagnostics keep pointers out of cross-process messages.
enum class Metric : WPARAM {
    CaptionPaints = 1,
    ClientPaints,
    Refreshes,
    CaptionTop,
    CaptionBottom,
    AdapterMode,
    InlineLeft,
    InlineTop,
    InlineWidth,
    InlineHeight,
    InlineTabsLeft,
    InlineOriginalButtonsVisible,
    InlineKind,
    InlineTabIndex
};
inline UINT DiagnosticMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Diagnostics.v1");
    return id;
}
} // namespace styler
