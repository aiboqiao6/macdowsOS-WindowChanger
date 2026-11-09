#include "../../session.h"
#include "research-placement.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windowsx.h>

namespace {
void Pump(DWORD ms) {
    auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}
LRESULT Send(HWND w, UINT m, WPARAM wp = 0, LPARAM lp = 0) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(w, m, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result)
               ? static_cast<LRESULT>(result)
               : -1;
}
UINT Metrics() {
    return RegisterWindowMessageW(L"Macdows.OfficeLease.Metrics.v1");
}
HWND Owner() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    return CreateWindowW(cls.lpszClassName, L"Office original-tree lease verifier", 0, 0, 0, 0, 0, nullptr, nullptr,
                         cls.hInstance, nullptr);
}
bool Detached(HWND w) {
    auto end = GetTickCount64() + 4000;
    do {
        if (!GetPropW(w, styler::OwnerProperty) && !GetPropW(w, styler::InlineProperty) &&
            Send(w, styler::QueryMessage()) != styler::Acknowledged)
            return true;
        Pump(50);
    } while (GetTickCount64() < end);
    return false;
}
bool Click(HWND target, unsigned index) {
    if (Send(target, styler::QueryMessage()) != styler::Acknowledged || Send(target, Metrics(), 1) != 1)
        return false;
    auto x = Send(target, Metrics(), 10 + index * 2), y = Send(target, Metrics(), 11 + index * 2);
    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    if (x == -1 || y == -1 || Send(target, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) != HTCLIENT ||
        !ScreenToClient(target, &point))
        return false;
    auto coordinates = MAKELPARAM(point.x, point.y);
    if (Send(target, WM_MOUSEMOVE, 0, coordinates) == -1 ||
        Send(target, WM_LBUTTONDOWN, MK_LBUTTON, coordinates) == -1 || Send(target, WM_LBUTTONUP, 0, coordinates) == -1)
        return false;
    Pump(400);
    return true;
}
bool Press(HWND target, bool directlyToRoot) {
    if (Send(target, Metrics(), 1) != 1)
        return false;
    auto x = Send(target, Metrics(), 14), y = Send(target, Metrics(), 15);
    auto destination = directlyToRoot ? reinterpret_cast<HWND>(Send(target, Metrics(), 0)) : target;
    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    if (x == -1 || y == -1 || !IsWindow(destination) || !ScreenToClient(destination, &point))
        return false;
    auto coordinates = MAKELPARAM(point.x, point.y);
    return Send(destination, WM_MOUSEMOVE, 0, coordinates) != -1 &&
           Send(destination, WM_LBUTTONDOWN, MK_LBUTTON, coordinates) != -1;
}
bool NoCapture(HWND target) {
    GUITHREADINFO info{sizeof(info)};
    return GetGUIThreadInfo(GetWindowThreadProcessId(target, nullptr), &info) && !info.hwndCapture;
}
bool NativeCaptionDisabled(HWND target) {
    TITLEBARINFOEX info{sizeof(info)};
    if (Send(target, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info)) == -1)
        return false;
    for (auto i : {2, 3, 5})
        if ((info.rgstate[i] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)) !=
                (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE) || !IsRectEmpty(&info.rgrect[i]))
            return false;
    return true;
}
bool ReportContains(HWND target, const char *text) {
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    std::ifstream report(std::filesystem::path(styler::ExecutableDirectory()) /
                         (L"office-lease-" + std::to_wstring(pid) + L".json"));
    std::string json((std::istreambuf_iterator<char>(report)), std::istreambuf_iterator<char>());
    return json.find(text) != std::string::npos && json.find("\"cleanupFailures\":0,") != std::string::npos;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 2 && !(argc == 4 && !wcscmp(argv[2], L"--crash-owner")))
        return ERROR_INVALID_PARAMETER;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t *tail{};
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(wcstoull(argv[1], &tail, 0)));
    if (!tail || *tail || !IsWindow(target))
        return ERROR_INVALID_WINDOW_HANDLE;
    auto owner = Owner();
    if (!owner)
        return GetLastError();
    if (argc == 4) {
        // Parent terminates only this verifier process after successful attach.
        styler::Session session(owner);
        if (!session.Apply(target, static_cast<styler::Adapter>(272)).ok)
            return ERROR_NOT_SUPPORTED;
        if (!Press(target, false) || Send(target, Metrics(), 5) != 1)
            return ERROR_INVALID_STATE;
        auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
        if (!event)
            return GetLastError();
        SetEvent(event);
        CloseHandle(event);
        for (;;)
            Pump(100);
    }
    ResearchPlacement restore;
    if (!restore.Capture(target))
        return GetLastError();
    ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    Pump(350);
    if (GetForegroundWindow() != target)
        return ERROR_NOT_READY;
    auto style = GetWindowLongPtrW(target, GWL_STYLE);
    RECT outer{}, client{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok)
            ++failures;
        std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
        return ok;
    };
    auto restored = [&]() {
        RECT nowOuter{}, nowClient{};
        GetWindowRect(target, &nowOuter);
        GetClientRect(target, &nowClient);
        return Detached(target) && GetWindowLongPtrW(target, GWL_STYLE) == style && EqualRect(&outer, &nowOuter) &&
               EqualRect(&client, &nowClient);
    };
    {
        styler::Session session(owner);
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            auto applied = session.Apply(target, static_cast<styler::Adapter>(272));
            if (!applied.ok) {
                auto length = WideCharToMultiByte(CP_UTF8, 0, applied.message.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string message(length, '\0');
                if (length)
                    WideCharToMultiByte(CP_UTF8, 0, applied.message.c_str(), -1, message.data(), length, nullptr, nullptr);
                std::cerr << message.c_str() << std::endl;
            }
            if (!check(applied.ok, "persistent attach returns success"))
                break;
            Pump(1200);
            check(session.Owns(target) && Send(target, styler::QueryMessage()) == styler::Acknowledged &&
                      Send(target, Metrics(), 1) == 1,
                  "lease survives after Attach returns");
            check(Send(target, Metrics(), 3, 0) == 7, "all three native circles remain painted after sustained attach");
            check(!(GetWindowLongPtrW(target, GWL_STYLE) & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)),
                  "all original DWM caption glyphs suppressed");
            check(Send(target, Metrics(), 7) == 1, "all original framework buttons disabled with typed boolean values");
            check(NativeCaptionDisabled(target), "native accessibility reports old caption buttons hidden and unavailable");
            RECT nowOuter{}, nowClient{};
            GetWindowRect(target, &nowOuter);
            GetClientRect(target, &nowClient);
            check(EqualRect(&outer, &nowOuter) && EqualRect(&client, &nowClient),
                  "attach preserves outer and client geometry");
            check(Send(target, styler::DetachMessage(), reinterpret_cast<WPARAM>(target)) == 0 && session.Owns(target),
                  "wrong owner cannot detach");
            check(Send(target, Metrics(), 40) == styler::TransitionBusy && session.Owns(target),
                  "nested detach returns TransitionBusy");
            if (cycle == 0) {
                check(Click(target, 2) && IsZoomed(target), "frame input relays to original maximize route");
                check(session.Owns(target) && Send(target, Metrics(), 1) == 1, "lease remains valid maximized");
                check(Send(target, Metrics(), 3, 1) == 7, "native circles remain painted maximized");
                check(Click(target, 2) && !IsZoomed(target), "frame input relays to original restore route");
                check(Send(target, Metrics(), 3, 2) == 7, "native circles remain painted after restore");
                check(Click(target, 1) && IsIconic(target), "frame input relays to original minimize route");
                ShowWindow(target, SW_RESTORE);
                SetForegroundWindow(target);
                Pump(300);
                check(Send(target, Metrics(), 2) >= 9, "input uses original root not direct system commands");
                if (check(session.Owns(target), "lease remains valid after minimize restore")) {
                    SetWindowPos(target, nullptr, 0, 0, outer.right - outer.left - 60, outer.bottom - outer.top - 30,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    Pump(300);
                    check(Send(target, Metrics(), 1) == 1, "lease remains valid after resize");
                    check(Send(target, Metrics(), 3, 3) == 7, "native circles remain painted after resize");
                    SetWindowPos(target, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    Pump(300);
                }
            }
            check(session.Restore(target).ok, "explicit detach succeeds");
            check(restored(), "detach restores styles geometry and session properties");
            check(session.Restore(target).ok, "repeated detach is harmless");
            if (failures)
                break;
        }
    }
    if (!failures) {
        styler::Session session(owner);
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok, "attach before narrow-layout guard")) {
            bool editor = Send(target, Metrics(), 8) == 1;
            if (editor) {
                auto minimum = Send(target, Metrics(), 9);
                SetWindowPos(target, nullptr, 0, 0, static_cast<int>(minimum), outer.bottom - outer.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(350);
                check(Send(target, Metrics(), 1) == 1 && Send(target, Metrics(), 7) == 1,
                      "minimum editor title envelope keeps a healthy suppressed original group");
                check(Send(target, Metrics(), 3, 4) == 7, "three native lights paint at minimum supported editor width");
                SetWindowPos(target, nullptr, 0, 0, 800, outer.bottom - outer.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(350);
                check(Detached(target), "narrow editor restores original layout before title content can clip");
                RECT narrowBefore{}, narrowAfter{};
                GetWindowRect(target, &narrowBefore);
                auto narrowStyle = GetWindowLongPtrW(target, GWL_STYLE);
                check(!session.Apply(target, static_cast<styler::Adapter>(272)).ok && Detached(target),
                      "initial attach to a narrow editor rejects without an active lease");
                check(ReportContains(target, "\"savedProperties\":0") &&
                          ReportContains(target, "\"retainedNodes\":0"),
                      "narrow initial rejection reaches no property write or node retention");
                GetWindowRect(target, &narrowAfter);
                check(EqualRect(&narrowBefore, &narrowAfter) && GetWindowLongPtrW(target, GWL_STYLE) == narrowStyle,
                      "narrow initial rejection preserves geometry and caption capabilities");
                SetWindowPos(target, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(350);
                check(session.Restore(target).ok && restored(), "narrow-layout guard restores caption properties and geometry");
            } else {
                check(session.Restore(target).ok && restored(), "start-page lease remains independent of editor width guard");
            }
        }
    }
    if (!failures) {
        styler::Session session(owner);
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok,
                  "attach for original capture cancellation")) {
            check(Press(target, false) && Send(target, Metrics(), 4) == 1 && Send(target, Metrics(), 5) == 1,
                  "original NetUI captures a pressed non-close button");
            Send(target, WM_CANCELMODE);
            check(Send(target, Metrics(), 4) == 0 && Send(target, Metrics(), 5) == 0 && Send(target, Metrics(), 6) == 1,
                  "frame cancellation clears original pressed state and releases capture");
            check(!IsZoomed(target) && !IsIconic(target) && NoCapture(target),
                  "cancellation executes no window command");
            check(Click(target, 2) && IsZoomed(target) && Click(target, 2) && !IsZoomed(target),
                  "original button works normally after cancellation");
            check(session.Restore(target).ok && restored(), "capture cancellation session restores cleanly");
        }
    }
    if (!failures) {
        styler::Session session(owner);
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok,
                  "attach for detach while root button is held")) {
            check(Press(target, true) && Send(target, Metrics(), 4) == 1 && Send(target, Metrics(), 5) == 1,
                  "direct original root input is tracked while captured");
            check(session.Restore(target).ok && Detached(target),
                  "detach cancels held original input before node destruction");
            check(NoCapture(target) && !IsZoomed(target) && !IsIconic(target),
                  "held-input detach neither captures nor executes a command");
            check(restored() && ReportContains(target, "\"canceledInput\":1"),
                  "held-input cleanup reports cancellation and restores state");
        }
    }
    if (!failures) {
        styler::Session session(owner);
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok,
                  "attach for nested invalidation contract")) {
            check(Send(target, Metrics(), 41) == styler::TransitionBusy && Detached(target),
                  "nested invalidation waits until the outermost callback returns");
            check(restored() && ReportContains(target, "\"reason\":\"nested-invalidation-contract\""),
                  "deferred nested cleanup restores tree properties and frame state");
        }
    }
    if (!failures) {
        styler::Session session(owner);
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok, "attach before a theme notification")) {
            // Notification only: do not change Windows theme or user settings.
            Send(target, WM_THEMECHANGED);
            check(Send(target, styler::QueryMessage()) == styler::Acknowledged && Send(target, Metrics(), 1) == 1,
                  "same-tree theme notification preserves the active original layout");
            Send(target, WM_SETTINGCHANGE);
            check(Send(target, styler::QueryMessage()) == styler::Acknowledged && Send(target, Metrics(), 1) == 1,
                  "same-tree settings notification preserves original button suppression");
            check(Send(target, Metrics(), 7) == 1, "theme and settings refresh preserve disabled original buttons");
            check(NativeCaptionDisabled(target), "native caption accessibility stays suppressed after theme refresh");
            session.RestoreAll();
            check(Detached(target) && restored(), "explicit detach after theme refresh restores original state");
        }
    }
    if (!failures) {
        styler::Session session(owner);
        struct Capabilities {
            HWND window;
            LONG_PTR style;
            bool changed{};
            void Restore() {
                if (changed && IsWindow(window)) {
                    SetWindowLongPtrW(window, GWL_STYLE, style);
                    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                    changed = false;
                }
            }
            ~Capabilities() {
                Restore();
            }
        } capabilities{target, style};
        if (check(session.Apply(target, static_cast<styler::Adapter>(272)).ok,
                  "attach before host changes maximize capability")) {
            SetLastError(0);
            auto previous = SetWindowLongPtrW(target, GWL_STYLE, style & ~LONG_PTR(WS_MAXIMIZEBOX));
            capabilities.changed = true;
            check(previous || !GetLastError(), "host requests minimize enabled and maximize disabled");
            check(Detached(target) && !(GetWindowLongPtrW(target, GWL_STYLE) & WS_MAXIMIZEBOX) &&
                      (GetWindowLongPtrW(target, GWL_STYLE) & WS_MINIMIZEBOX),
                  "capability change ends lease while preserving latest host request");
            capabilities.Restore();
            Pump(250);
            check(session.Restore(target).ok && restored(), "test restores original complete caption capabilities");
        }
    }
    if (!failures) {
        auto disposableOwner = Owner();
        {
            styler::Session session(disposableOwner);
            check(session.Apply(target, static_cast<styler::Adapter>(272)).ok, "attach under disposable owner HWND");
            check(Press(target, true) && Send(target, Metrics(), 5) == 1,
                  "hold original button before owner HWND destruction");
            DestroyWindow(disposableOwner);
            check(Detached(target), "owner HWND destruction automatically detaches");
            check(restored() && NoCapture(target) && ReportContains(target, "\"canceledInput\":1"),
                  "owner HWND loss cancels held input and restores original state");
        }
    }
    if (!failures) {
        auto eventName = L"Local\\Macdows.OfficeLease.Crash." + std::to_wstring(GetCurrentProcessId());
        auto event = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        auto exe = styler::ExecutableDirectory() + L"\\OfficeLeaseHost.exe";
        auto command = L"\"" + exe + L"\" " + argv[1] + L" --crash-owner " + eventName;
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        if (check(event && CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                          nullptr, nullptr, &startup, &child),
                  "launch own crash-test controller")) {
            HANDLE wait[]{event, child.hProcess};
            auto ready = WaitForMultipleObjects(2, wait, FALSE, 10000);
            check(ready == WAIT_OBJECT_0 && GetPropW(target, styler::OwnerProperty) &&
                      Send(target, Metrics(), 4) == 1 && Send(target, Metrics(), 5) == 1,
                  "child publishes a persistent lease with an original button held");
            TerminateProcess(child.hProcess, 73);
            WaitForSingleObject(child.hProcess, 2000);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            check(Detached(target), "controller process crash automatically detaches");
            check(restored() && NoCapture(target) && ReportContains(target, "\"canceledInput\":1"),
                  "controller crash cancels held input and restores original state");
        }
        if (event)
            CloseHandle(event);
    }
    DestroyWindow(owner);
    check(restore.Finish(), "verifier restores original placement without reactivating minimized windows");
    std::cout << "{\"checks\":" << checks << ",\"failures\":" << failures << "}" << std::endl;
    return failures ? ERROR_INVALID_DATA : 0;
}
