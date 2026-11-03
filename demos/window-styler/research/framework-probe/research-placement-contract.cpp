#include "research-placement.h"
#include <iostream>

namespace {
unsigned checks{}, failures{};
void Check(bool ok, const char *name) {
    ++checks;
    if (!ok)
        ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
}
LRESULT CALLBACK Window(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    // A self-owned stand-in for hosts that reopen themselves on activation.
    if (m == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE && IsIconic(w) && GetPropW(w, L"RestoreOnActivate"))
        ShowWindow(w, SW_RESTORE);
    return DefWindowProcW(w, m, wp, lp);
}
} // namespace
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto foreground = GetForegroundWindow();
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = Window;
    cls.lpszClassName = L"Macdows.ResearchPlacement.Contract";
    RegisterClassW(&cls);
    auto target = CreateWindowW(cls.lpszClassName, L"Owned placement fixture", WS_OVERLAPPEDWINDOW, 80, 80, 640, 480,
                                nullptr, nullptr, cls.hInstance, nullptr);
    auto other = CreateWindowW(cls.lpszClassName, L"Owned activation-sensitive fixture", WS_OVERLAPPEDWINDOW, 100, 100,
                               500, 360, nullptr, nullptr, cls.hInstance, nullptr);
    if (!target || !other)
        return 1;
    SetPropW(target, L"RestoreOnActivate", reinterpret_cast<HANDLE>(1));
    SetPropW(other, L"RestoreOnActivate", reinterpret_cast<HANDLE>(1));
    ShowWindow(target, SW_SHOWMINNOACTIVE);
    {
        ResearchPlacement placement;
        Check(placement.Capture(target) && placement.iconic, "save an owned iconic window");
        placement.foreground = target;
        ShowWindow(target, SW_SHOWNOACTIVATE);
        Check(!IsIconic(target), "temporarily show the owned target");
        Check(placement.Finish() && IsIconic(target), "restore iconic state without activating the target");
        RECT outer{}, client{};
        Check(GetWindowRect(target, &outer) && GetClientRect(target, &client) && EqualRect(&outer, &placement.outer) &&
                  EqualRect(&client, &placement.client),
              "iconic completion waits for exact original window and client bounds, not just IsIconic");
        Check(placement.Finish(), "placement completion is idempotent");
    }
    ShowWindow(target, SW_SHOWNOACTIVATE);
    ShowWindow(other, SW_SHOWMINNOACTIVE);
    {
        ResearchPlacement placement;
        Check(placement.Capture(target) && !placement.iconic, "save an owned normal window");
        placement.foreground = other;
        ShowWindow(target, SW_SHOWMINNOACTIVE);
        Check(placement.Finish() && !IsIconic(target), "restore normal target placement");
        RECT outer{}, client{};
        Check(GetWindowRect(target, &outer) && GetClientRect(target, &client) && EqualRect(&outer, &placement.outer) &&
                  EqualRect(&client, &placement.client),
              "normal completion restores actual outer and client geometry before returning");
        Check(IsIconic(other), "do not reactivate a different iconic foreground reference");
    }
    RECT original{};
    GetWindowRect(target, &original);
    {
        ResearchPlacement placement;
        Check(placement.Capture(target), "save geometry for destructor fallback");
        SetWindowPos(target, nullptr, 180, 180, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT restored{};
    GetWindowRect(target, &restored);
    Check(EqualRect(&original, &restored), "destructor restores the original normal geometry");
    DestroyWindow(other);
    DestroyWindow(target);
    Check(!IsWindow(other) && !IsWindow(target), "only the owned test windows are destroyed");
    if (IsWindow(foreground) && !IsIconic(foreground))
        SetForegroundWindow(foreground);
    std::cout << "checks=" << checks << "; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
