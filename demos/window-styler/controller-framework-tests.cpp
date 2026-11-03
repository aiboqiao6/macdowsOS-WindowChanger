#include "controller-settings.h"
#include "host-client.h"
#include "research/framework-probe/research-placement.h"
#include <filesystem>
#include <iostream>
#include <functional>
#include <objbase.h>

namespace {
void Pump(DWORD ms) {
    auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(15);
    } while (GetTickCount64() < end);
}
bool Until(const std::function<bool()> &fn) {
    auto end = GetTickCount64() + 10000;
    do {
        if (fn())
            return true;
        Pump(50);
    } while (GetTickCount64() < end);
    return fn();
}
LRESULT Send(HWND w, UINT m, WPARAM wp = 0, LPARAM lp = 0) {
    DWORD_PTR r{};
    return SendMessageTimeoutW(w, m, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &r) ? static_cast<LRESULT>(r) : -1;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return ERROR_INVALID_PARAMETER;
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t *end{};
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(wcstoull(argv[2], &end, 0)));
    wchar_t type[128]{};
    GetClassNameW(target, type, 128);
    bool tx = !wcscmp(type, L"TXGuiFoundation"), mfc = !wcsncmp(type, L"Afx", 3), office{};
    EnumChildWindows(
        target,
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(window, name, 128);
            if (!wcscmp(name, L"NetUIHWND"))
                *reinterpret_cast<bool *>(data) = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&office));
    // This verifier's supported families match actual framework window classes;
    // it never selects by application title, process name or executable path.
    if (!end || *end || !IsWindow(target) || (!tx && !mfc && !office) || GetPropW(target, styler::OwnerProperty))
        return ERROR_NOT_SUPPORTED;
    ResearchPlacement placement;
    if (!placement.Capture(target))
        return GetLastError();
    ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    Pump(350);
    RECT outer{}, client{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    auto style = GetWindowLongPtrW(target, GWL_STYLE), exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    styler::settings::Scope scope{L"framework-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(GetTickCount64())};
    auto exe = (std::filesystem::absolute(argv[1]) / L"WindowStyler.exe").wstring();
    auto command =
        L"\"" + exe + L"\" --background --test-mode " + scope.testId + L" --scope-pid " + std::to_wstring(pid);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *name) {
        ++checks;
        failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
        return ok;
    };
    bool started = check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                        &startup, &process) != FALSE,
                         "start actual packaged controller scoped to the selected framework PID");
    HWND controller{};
    if (started) {
        check(Until([&] {
                  struct Find {
                      DWORD pid;
                      HWND window{};
                  } find{process.dwProcessId};
                  EnumWindows(
                      [](HWND w, LPARAM p) -> BOOL {
                          auto &f = *reinterpret_cast<Find *>(p);
                          DWORD pid{};
                          GetWindowThreadProcessId(w, &pid);
                          if (pid == f.pid && GetPropW(w, L"Macdows.WindowChanger.Primary.v3")) {
                              f.window = w;
                              return FALSE;
                          }
                          return TRUE;
                      },
                      reinterpret_cast<LPARAM>(&find));
                  controller = find.window;
                  return controller != nullptr;
              }),
              "background controller is running with a real tray instance");
        bool owned = check(Until([&] {
                               auto owner = reinterpret_cast<HWND>(GetPropW(target, styler::OwnerProperty));
                               return owner && GetPropW(owner, styler::HostParentProperty) == controller;
                           }),
                           "automatic management claims the real framework through its own architecture helper");
        if (owned) {
            check(!IsWindowVisible(controller), "real framework automatic capture needs no visible controller overlay");
            if (tx || office) {
                auto metrics = RegisterWindowMessageW(tx ? L"Macdows.TXGuiLeftLease.Metrics.v1"
                                                         : L"Macdows.OfficeLease.Metrics.v1");
                check(Send(target, metrics, 1) == 1 && Send(target, metrics, 3, 0) == 7,
                      "automatically selected original backend paints all three real light colors");
            } else {
                auto metrics = RegisterWindowMessageW(L"Macdows.MfcPaintProbe.Metrics.v1");
                auto screen = GetDC(nullptr);
                bool colors = screen != nullptr;
                constexpr COLORREF expected[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
                RedrawWindow(target, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                Pump(100);
                for (unsigned i = 0; i < 3; ++i) {
                    POINT p{static_cast<LONG>(Send(target, metrics, 10 + i * 2)),
                            static_cast<LONG>(Send(target, metrics, 11 + i * 2))};
                    colors = colors && Send(target, metrics, i) > 0 &&
                             GetAncestor(WindowFromPoint(p), GA_ROOT) == target &&
                             GetPixel(screen, p.x, p.y) == expected[i];
                }
                if (screen)
                    ReleaseDC(nullptr, screen);
                check(colors, "automatic MFC original renderer paints all three real left-light colors");
            }
            Send(controller, WM_COMMAND, 105);
            check(Until([&] { return !GetPropW(target, styler::OwnerProperty); }),
                  "restore-all from actual controller releases the real framework");
            Pump(1100);
            check(!GetPropW(target, styler::OwnerProperty),
                  "paused automatic mode does not immediately reclaim the real framework");
        }
        if (controller)
            Send(controller, RegisterWindowMessageW(L"Macdows.WindowChanger.Test.v3"), 9);
        if (!Until([&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; })) {
            TerminateProcess(process.hProcess, 0x54455354);
            WaitForSingleObject(process.hProcess, 3000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    RECT nowOuter{}, nowClient{};
    GetWindowRect(target, &nowOuter);
    GetClientRect(target, &nowClient);
    check(IsWindow(target) && EqualRect(&outer, &nowOuter) && EqualRect(&client, &nowClient) &&
              GetWindowLongPtrW(target, GWL_STYLE) == style && GetWindowLongPtrW(target, GWL_EXSTYLE) == exStyle &&
              !GetPropW(target, styler::OwnerProperty),
          "original framework HWND geometry style and ownership are restored");
    check(placement.Finish(), "restore the user's current minimized or normal placement after verification");
    check(SUCCEEDED(styler::settings::RemoveTestSettings(scope)), "remove only the verifier's isolated preferences");
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    if (SUCCEEDED(com))
        CoUninitialize();
    return failures ? 1 : 0;
}
