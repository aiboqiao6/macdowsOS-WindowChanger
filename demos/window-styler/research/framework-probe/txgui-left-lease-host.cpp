#include "../../session.h"
#include "research-placement.h"
#include <windowsx.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void Pump(DWORD milliseconds) {
    auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < until);
}
LRESULT Send(HWND window, UINT message, WPARAM wp = 0, LPARAM lp = 0) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(window, message, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result)
               ? static_cast<LRESULT>(result)
               : -1;
}
UINT Metrics() {
    return RegisterWindowMessageW(L"Macdows.TXGuiLeftLease.Metrics.v1");
}
HWND Owner() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    return CreateWindowW(cls.lpszClassName, L"TXGui original-tree lease verifier", 0, 0, 0, 0, 0, nullptr, nullptr,
                         cls.hInstance, nullptr);
}
bool Detached(HWND window) {
    auto until = GetTickCount64() + 4000;
    do {
        if (!GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::InlineProperty) &&
            Send(window, styler::QueryMessage()) != styler::Acknowledged)
            return true;
        Pump(50);
    } while (GetTickCount64() < until);
    return false;
}
bool NoCapture(HWND window) {
    GUITHREADINFO state{sizeof(state)};
    return GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &state) && !state.hwndCapture;
}
bool Point(HWND window, unsigned index, LPARAM &coordinates) {
    if (index != 1 && index != 2)
        return false; // Close is never generated.
    if (Send(window, Metrics(), 1) != 1)
        return false;
    POINT point{static_cast<LONG>(Send(window, Metrics(), 10 + index * 2)),
                static_cast<LONG>(Send(window, Metrics(), 11 + index * 2))};
    if (Send(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) != HTCLIENT || !ScreenToClient(window, &point))
        return false;
    coordinates = MAKELPARAM(point.x, point.y);
    return true;
}
bool Press(HWND window) {
    LPARAM coordinates{};
    return Point(window, 2, coordinates) && Send(window, WM_MOUSEMOVE, 0, coordinates) != -1 &&
           Send(window, WM_LBUTTONDOWN, MK_LBUTTON, coordinates) != -1;
}
bool Click(HWND window, unsigned index) {
    LPARAM coordinates{};
    if (!Point(window, index, coordinates))
        return false;
    if (Send(window, WM_MOUSEMOVE, 0, coordinates) == -1 ||
        Send(window, WM_LBUTTONDOWN, MK_LBUTTON, coordinates) == -1 || Send(window, WM_LBUTTONUP, 0, coordinates) == -1)
        return false;
    Pump(350);
    return NoCapture(window);
}
bool Report(HWND window, const char *reason) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    std::ifstream file(std::filesystem::path(styler::ExecutableDirectory()) /
                       (L"txgui-left-lease-" + std::to_wstring(pid) + L".json"));
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::cout << json;
    return json.find(reason) != std::string::npos && json.find("\"cleanupFailures\":0,") != std::string::npos;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2 || argc > 4)
        return ERROR_INVALID_PARAMETER;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t *end{};
    auto window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(wcstoull(argv[1], &end, 0)));
    if (!end || *end || !IsWindow(window))
        return ERROR_INVALID_WINDOW_HANDLE;
    bool smoke = argc == 3 && !wcscmp(argv[2], L"--smoke");
    auto owner = Owner();
    if (!owner)
        return GetLastError();
    if (argc == 4 && !wcscmp(argv[2], L"--crash-owner")) {
        styler::Session session(owner);
        if (!session.Apply(window, static_cast<styler::Adapter>(279)).ok || !Press(window) ||
            Send(window, Metrics(), 5) != 1)
            return ERROR_NOT_SUPPORTED;
        auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
        if (!event)
            return GetLastError();
        SetEvent(event);
        CloseHandle(event);
        for (;;)
            Pump(100);
    }
    ResearchPlacement placement;
    if (!placement.Capture(window))
        return GetLastError();
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    Pump(350);
    if (GetForegroundWindow() != window)
        return ERROR_NOT_READY;
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok)
            ++failures;
        std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
        return ok;
    };
    auto restored = [&] {
        RECT currentOuter{}, currentClient{};
        return Detached(window) && GetWindowRect(window, &currentOuter) && GetClientRect(window, &currentClient) &&
               EqualRect(&outer, &currentOuter) && EqualRect(&client, &currentClient) && NoCapture(window) &&
               GetWindowLongPtrW(window, GWL_STYLE) == style && GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle;
    };
    {
        styler::Session session(owner);
        for (unsigned cycle = 0; cycle < 2 && !failures; ++cycle) {
            if (!check(session.Apply(window, static_cast<styler::Adapter>(279)).ok,
                       "persistent original-tree left-light attach succeeds"))
                break;
            Pump(1000);
            check(session.Owns(window) && Send(window, styler::QueryMessage()) == styler::Acknowledged &&
                      Send(window, Metrics(), 1) == 1,
                  "original-tree lease survives after Attach returns");
            check(Send(window, Metrics(), 3, cycle) == 7,
                  "all actual left-light colors and original D2D state remain valid");
            check(session.Apply(window, static_cast<styler::Adapter>(279)).ok && session.Owns(window),
                  "repeated apply is idempotent");
            check(Send(window, styler::DetachMessage(), reinterpret_cast<WPARAM>(window)) == 0 && session.Owns(window),
                  "wrong owner cannot detach");
            check(Send(window, Metrics(), 40) == styler::TransitionBusy && session.Owns(window),
                  "nested detach is rejected without losing the lease");
            if (!smoke && !cycle && !failures) {
                check(Click(window, 2) && IsZoomed(window),
                      "relocated original Button invokes original maximize route");
                check(session.Owns(window) && Send(window, Metrics(), 1) == 1 && Send(window, Metrics(), 3, 2) == 7,
                      "native original layout and all three lights survive maximize");
                check(Click(window, 2) && !IsZoomed(window) && !IsIconic(window),
                      "relocated original Button invokes original restore route");
                check(Send(window, Metrics(), 3, 3) == 7, "original D2D lights survive restore");
                check(Click(window, 1) && IsIconic(window),
                      "relocated original Button invokes original minimize route");
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
                Pump(350);
                check(session.Owns(window) && Send(window, Metrics(), 3, 4) == 7,
                      "persistent original-tree lights survive minimize restore");
                SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left - 60, outer.bottom - outer.top - 30,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(350);
                check(Send(window, Metrics(), 1) == 1 && Send(window, Metrics(), 3, 5) == 7,
                      "native layout follows resize without a layout-follow timer");
                SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(350);
            }
            check(session.Restore(window).ok && restored(),
                  "explicit undo restores original geometry styles and capture");
            check(Report(window, "explicit-detach"),
                  "explicit undo restores native layout orders and original filters without errors");
            check(session.Restore(window).ok, "repeated undo is harmless");
        }
    }
    if (!smoke && !failures) {
        styler::Session session(owner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok,
                  "attach for undo at a newer viewport size")) {
            SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left - 60, outer.bottom - outer.top - 30,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            Pump(350);
            RECT resized{};
            GetWindowRect(window, &resized);
            check(Send(window, Metrics(), 1) == 1 && session.Restore(window).ok && Detached(window),
                  "detach succeeds directly at the newer native viewport size");
            RECT detachedRect{};
            GetWindowRect(window, &detachedRect);
            check(EqualRect(&resized, &detachedRect), "undo does not force a newer viewport back to historical bounds");
            check(Report(window, "explicit-detach"),
                  "newer viewport undo restores original layout constraints without errors");
            SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            Pump(350);
            check(restored(), "restore only the verifier-owned viewport size change");
        }
    }
    if (!smoke && !failures) {
        styler::Session session(owner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok, "attach for undo while maximized")) {
            check(Click(window, 2) && IsZoomed(window), "maximize through the relocated original Button before undo");
            check(session.Restore(window).ok && Detached(window) && IsZoomed(window),
                  "detach preserves the newer maximized window state");
            check(Report(window, "explicit-detach"),
                  "maximized undo restores native constraints without cleanup errors");
            ShowWindow(window, SW_RESTORE);
            Pump(350);
            check(restored(), "original layout returns after normal restore without a persistent lease");
        }
    }
    if (!smoke && !failures) {
        styler::Session session(owner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok,
                  "attach for held original input cancellation")) {
            check(Press(window) && Send(window, Metrics(), 4) == 1 && Send(window, Metrics(), 5) == 1,
                  "original framework captures a pressed relocated non-close Button");
            Send(window, WM_CANCELMODE);
            check(Send(window, Metrics(), 4) == 0 && NoCapture(window) && !IsZoomed(window),
                  "cancel original input without generating a button-up action");
            check(Press(window) && session.Restore(window).ok && restored(),
                  "detach while held cancels original capture before restoring layout");
            check(Report(window, "explicit-detach"), "held detach records zero native cleanup failures");
        }
    }
    if (!smoke && !failures) {
        styler::Session session(owner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok,
                  "attach for deferred nested invalidation")) {
            check(Send(window, Metrics(), 41) == styler::TransitionBusy && Detached(window) && restored(),
                  "nested invalidation restores only after the outer callback unwinds");
            check(Report(window, "nested-invalidation"),
                  "deferred nested teardown restores native orders without errors");
        }
    }
    if (!smoke && !failures) {
        auto lostOwner = Owner();
        styler::Session session(lostOwner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok, "attach for owner HWND loss")) {
            check(Press(window), "hold original non-close input before owner loss");
            DestroyWindow(lostOwner);
            check(Detached(window) && restored() && !IsZoomed(window),
                  "owner HWND loss cancels held input and restores original tree");
            check(Report(window, "owner-lost"), "owner HWND loss reports zero native cleanup failures");
        } else
            DestroyWindow(lostOwner);
    }
    if (!smoke && !failures) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto eventName = L"Local\\Macdows.TxGuiLeft.Crash." + std::to_wstring(GetCurrentProcessId()) + L"." +
                         std::to_wstring(GetTickCount64());
        auto event = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        std::wstring command =
            L"\"" + std::wstring(executable) + L"\" " + argv[1] + L" --crash-owner \"" + eventName + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        bool created = event && CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                               nullptr, nullptr, &startup, &child);
        bool attached{};
        if (created) {
            auto until = GetTickCount64() + 6000;
            do {
                attached = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
                if (attached || WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0)
                    break;
                Pump(30);
            } while (GetTickCount64() < until);
            check(attached && GetPropW(window, styler::OwnerProperty),
                  "own verifier child attaches and holds native input before crash");
            // This handle identifies only the verifier process created above.
            // No user application process is terminated or restarted.
            TerminateProcess(child.hProcess, 0x54455354);
            WaitForSingleObject(child.hProcess, 3000);
            check(Detached(window) && restored() && !IsZoomed(window),
                  "controller process crash restores original tree and cancels held input");
            check(Report(window, "owner-lost"), "controller crash reports zero native cleanup failures");
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
        } else
            check(false, "create only the owned crash-verifier child");
        if (event)
            CloseHandle(event);
    }
    if (!smoke && !failures) {
        styler::Session session(owner);
        if (check(session.Apply(window, static_cast<styler::Adapter>(279)).ok,
                  "attach for changed original window capabilities")) {
            SetWindowLongPtrW(window, GWL_STYLE, style & ~WS_MAXIMIZEBOX);
            check(Detached(window) && (GetWindowLongPtrW(window, GWL_STYLE) & WS_MAXIMIZEBOX) == 0,
                  "capability change fails closed and preserves the newer host window style");
            check(Report(window, "original-tree-or-capability-changed"),
                  "capability invalidation restores native layout without errors");
            SetWindowLongPtrW(window, GWL_STYLE, style);
            Pump(150);
            check(restored(), "restore only the verifier-owned capability mutation");
        }
    }
    check(placement.Finish(), "current user placement and foreground are restored after the verifier");
    DestroyWindow(owner);
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    return failures ? ERROR_INVALID_DATA : ERROR_SUCCESS;
}
