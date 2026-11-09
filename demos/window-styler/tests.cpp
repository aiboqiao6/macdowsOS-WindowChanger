#include "session.h"
#include "host-client.h"
#include "visual.h"
#include "inline.h"
#include "framework.h"
#include "embedded-frame.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <iostream>
#include <tlhelp32.h>

namespace {
int failures{};
void Check(bool condition, const char *description) {
    std::cout << (condition ? "PASS " : "FAIL ") << description << std::endl;
    if (!condition)
        ++failures;
}
bool Until(const std::function<bool()> &condition, DWORD timeout = 4000) {
    ULONGLONG deadline = GetTickCount64() + timeout;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (condition())
            return true;
        Sleep(15);
    } while (GetTickCount64() < deadline);
    return condition();
}
bool RoundedRegion(HWND window, bool rounded) {
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    int kind = GetWindowRgn(window, region);
    RECT r{};
    GetRgnBox(region, &r);
    bool ok = kind != ERROR && !PtInRegion(region, r.left + 1, r.top + 1) == rounded &&
              !PtInRegion(region, r.right - 2, r.top + 1) == rounded &&
              !PtInRegion(region, r.left + 1, r.bottom - 2) == rounded &&
              !PtInRegion(region, r.right - 2, r.bottom - 2) == rounded &&
              PtInRegion(region, (r.left + r.right) / 2, r.top + 1) &&
              PtInRegion(region, (r.left + r.right) / 2, r.bottom - 2);
    DeleteObject(region);
    return ok;
}
bool HasWindowRegion(HWND window) {
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    bool result = GetWindowRgn(window, region) != ERROR;
    DeleteObject(region);
    return result;
}
bool Settled(HWND window, const std::function<bool()> &condition) {
    RECT previous{};
    ULONGLONG stableSince{};
    return Until([&] {
        RECT current{};
        GetWindowRect(window, &current);
        bool ready = condition() && !GetPropW(window, L"Macdows.Sample.CommandDepth");
        if (!ready || !EqualRect(&current, &previous))
            stableSince = GetTickCount64();
        previous = current;
        return ready && GetTickCount64() - stableSince >= 200;
    });
}
struct Child {
    PROCESS_INFORMATION process{};
    Child(const std::wstring &path, const std::wstring &arguments) {
        std::wstring command = L"\"" + path + L"\" " + arguments;
        STARTUPINFOW startup{sizeof(startup)};
        if (CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process))
            CloseHandle(process.hThread);
    }
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
                TerminateProcess(process.hProcess, 99);
            CloseHandle(process.hProcess);
        }
    }
};
HWND Controller() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    return CreateWindowExW(0, cls.lpszClassName, L"Styler test controller", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
                           nullptr, nullptr, cls.hInstance, nullptr);
}
std::vector<HWND> Windows(DWORD pid) {
    struct Search {
        DWORD pid;
        std::vector<HWND> windows;
    } search{pid, {}};
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            auto *search = reinterpret_cast<Search *>(param);
            DWORD windowPid{};
            GetWindowThreadProcessId(window, &windowPid);
            if (windowPid == search->pid && GetPropW(window, styler::SampleProperty) && IsWindowVisible(window))
                search->windows.push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.windows;
}
bool Send(HWND window, UINT message, WPARAM wParam = 0, LPARAM lParam = 0, DWORD_PTR *result = nullptr) {
    DWORD_PTR ignored{};
    return SendMessageTimeoutW(window, message, wParam, lParam, SMTO_ABORTIFHUNG, 2000, result ? result : &ignored) !=
           0;
}
POINT ButtonPoint(HWND window, int button) {
    UINT dpi = GetDpiForWindow(window);
    RECT rect{};
    GetWindowRect(window, &rect);
    POINT client{};
    ClientToScreen(window, &client);
    int border = client.x - rect.left;
    bool extended = GetPropW(window, styler::AdapterProperty) == reinterpret_cast<HANDLE>(styler::Adapter::Extended);
    int topBorder = (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME)
                        ? GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
                        : GetSystemMetricsForDpi(SM_CYFIXEDFRAME, dpi);
    return {rect.left + border + MulDiv(18 + 20 * button, dpi, 96),
            extended ? (rect.top + border + client.y) / 2
                     : rect.top + topBorder + GetSystemMetricsForDpi(SM_CYCAPTION, dpi) / 2};
}
void ClickButton(HWND window, int button) {
    POINT point = ButtonPoint(window, button);
    const WPARAM hits[] = {HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
    Send(window, WM_NCLBUTTONDOWN, hits[button], MAKELPARAM(point.x, point.y));
    ScreenToClient(window, &point);
    Send(window, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}
std::array<DWORD, 4> Colors(HWND window) {
    std::array<DWORD, 4> values{};
    for (size_t i = 0; i < values.size(); ++i)
        DwmGetWindowAttribute(window, styler::VisualAttributes[i], &values[i], sizeof(DWORD));
    return values;
}
int HookModules(HWND window) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return -1;
    int count{};
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot, &entry))
        do {
            if (std::wstring(entry.szModule).starts_with(L"MacdowsStyleHook"))
                ++count;
        } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}
int VisualTests(const std::wstring &path, const std::wstring &args, size_t expected, bool faultTest) {
    HWND controller = Controller();
    Child target(path, args);
    std::vector<HWND> windows;
    Check(Until([&] {
              windows = Windows(target.process.dwProcessId);
              return windows.size() == expected;
          }),
          "discover actual visual test windows");
    {
        styler::GuardedSession session(controller, true);
        Check(session.HostPid() != 0, "independent appearance guardian starts");
        for (size_t index = 0; index < windows.size(); ++index) {
            HWND window = windows[index];
            if (!faultTest)
                Check(Until([&] { return GetPropW(window, styler::SamplePaintProperty) != nullptr; }),
                      "framework completes its initial render before measuring stable geometry");
            if (faultTest && index == 1) {
                Send(window, WM_SYSCOMMAND, SC_MINIMIZE);
                Until([&] { return IsIconic(window) != FALSE; });
            }
            if (faultTest && index == 2) {
                Send(window, WM_SYSCOMMAND, SC_MAXIMIZE);
                Until([&] { return IsZoomed(window) != FALSE; });
            }
            std::wcout << L"Minimal: " << styler::WindowTitle(window) << std::endl;
            RECT outer{}, client{}, currentOuter{}, currentClient{};
            POINT origin{}, currentOrigin{};
            GetWindowRect(window, &outer);
            GetClientRect(window, &client);
            ClientToScreen(window, &origin);
            auto style = GetWindowLongPtrW(window, GWL_STYLE), ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
            auto menu = GetMenu(window);
            auto before = Colors(window);
            bool iconic = IsIconic(window), zoomed = IsZoomed(window);
            int modules = HookModules(window);
            Check(session.Apply(window).ok, "public DWM adapter accepts the window");
            Check(session.Apply(window).ok && session.Count() == 1, "minimal takeover is idempotent");
            Check(GetPropW(window, styler::AdapterProperty) == reinterpret_cast<HANDLE>(styler::Adapter::Dwm),
                  "default uses the non-injecting in-place adapter");
            auto applied = Colors(window);
            BOOL dark{};
            DwmGetWindowAttribute(window, 20, &dark, sizeof(dark));
            std::array<DWORD, 4> desired{2, dark ? RGB(78, 78, 82) : RGB(212, 210, 207),
                                         dark ? RGB(40, 40, 42) : RGB(244, 243, 241),
                                         dark ? RGB(232, 232, 234) : RGB(58, 57, 55)};
            bool accepted = true;
            size_t readable{};
            for (size_t i = 0; i < desired.size(); ++i) {
                DWORD value{};
                if (SUCCEEDED(DwmGetWindowAttribute(window, styler::VisualAttributes[i], &value, sizeof(value)))) {
                    ++readable;
                    accepted = accepted && value == desired[i];
                }
            }
            Check(accepted && readable > 0, "readable DWM properties match; unsupported colors are preserved");
            std::cout << "Readable DWM properties: " << readable
                      << "/4; actual stored-value changes: " << (applied != before) << std::endl;
            Until([] { return false; }, 150);
            GetWindowRect(window, &currentOuter);
            GetClientRect(window, &currentClient);
            ClientToScreen(window, &currentOrigin);
            Check(EqualRect(&outer, &currentOuter) && EqualRect(&client, &currentClient) &&
                      origin.x == currentOrigin.x && origin.y == currentOrigin.y,
                  "outer rectangle and complete client geometry remain exact");
            Check(style == GetWindowLongPtrW(window, GWL_STYLE) && ex == GetWindowLongPtrW(window, GWL_EXSTYLE) &&
                      menu == GetMenu(window),
                  "original style bits and menu remain exact");
            Check(iconic == (IsIconic(window) != FALSE) && zoomed == (IsZoomed(window) != FALSE),
                  "minimized and maximized states are preserved without showing or restoring");
            if (modules >= 0)
                Check(HookModules(window) == modules, "no hook DLL is injected by minimal mode");
            Check(session.Restore(window).ok && Colors(window) == before,
                  "all original DWM values are restored exactly");
            Check(!GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::AdapterProperty),
                  "minimal restore removes ownership metadata");
        }
    }
    if (faultTest && !windows.empty()) {
        HWND window = windows.front();
        auto before = Colors(window);
        Child worker(styler::ExecutableDirectory() + L"\\StylerTests.exe",
                     L"--visual-lease-worker " + std::to_wstring(reinterpret_cast<UINT_PTR>(window)));
        Check(Until([&] { return GetPropW(window, styler::OwnerProperty) != nullptr; }),
              "a separate controller acquires an appearance lease");
        Check(worker.process.hProcess && TerminateProcess(worker.process.hProcess, 99),
              "fault-inject controller termination only");
        Check(Until([&] { return !GetPropW(window, styler::OwnerProperty) && Colors(window) == before; }),
              "guardian restores exact original attributes after controller crash");
        Check(IsWindow(window) != FALSE, "target application survives controller crash");
    }
    for (HWND window : windows)
        Send(window, WM_CLOSE);
    Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0, "visual test targets exit cleanly");
    DestroyWindow(controller);
    return failures ? 1 : 0;
}
} // namespace

LRESULT Metric(HWND w, styler::Metric metric) {
    DWORD_PTR r{};
    Send(w, styler::DiagnosticMessage(), static_cast<WPARAM>(metric), 0, &r);
    return static_cast<LRESULT>(r);
}
void ClientClick(HWND w, int x, int y) {
    POINT p{x, y};
    ClientToScreen(w, &p);
    HWND recipient = w;
    for (int i = 0; i < 12; ++i) {
        POINT q = p;
        ScreenToClient(recipient, &q);
        HWND child = ChildWindowFromPointEx(recipient, q, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (!child || child == recipient)
            break;
        recipient = child;
    }
    ScreenToClient(recipient, &p);
    Send(recipient, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
    Send(recipient, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
}
void InlineClick(HWND w, int button) {
    auto dpi = GetDpiForWindow(w);
    int px = static_cast<int>(Metric(w, styler::Metric::InlineLeft)) + MulDiv(18 + button * 20, dpi, 96),
        py = static_cast<int>(Metric(w, styler::Metric::InlineTop) + Metric(w, styler::Metric::InlineHeight) / 2);
    if (Metric(w, styler::Metric::InlineKind) == 4) {
        POINT saved{}, point{px, py};
        GetCursorPos(&saved);
        ClientToScreen(w, &point);
        bool topmost = (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(w);
        SetCursorPos(point.x, point.y);
        Sleep(80);
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(input));
        Sleep(50);
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(input));
        SetCursorPos(saved.x, saved.y);
        if (!topmost && IsWindow(w))
            SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    ClientClick(w, px, py);
}
size_t TopLevelCount(DWORD pid) {
    struct Data {
        DWORD pid;
        size_t count;
    } data{pid, 0};
    EnumWindows(
        [](HWND w, LPARAM p) -> BOOL {
            auto &d = *reinterpret_cast<Data *>(p);
            DWORD pid{};
            GetWindowThreadProcessId(w, &pid);
            if (pid == d.pid && IsWindowVisible(w))
                ++d.count;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));
    return data.count;
}
size_t ChildCount(HWND w) {
    size_t n{};
    EnumChildWindows(
        w,
        [](HWND, LPARAM p) -> BOOL {
            ++*reinterpret_cast<size_t *>(p);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&n));
    return n;
}
int InlineTests(const std::wstring &path, size_t expected, int kind = 0, const std::wstring &arguments = L"") {
    HWND controller = Controller();
    Child target(path, arguments);
    std::vector<HWND> windows;
    Check(Until([&] {
              windows = Windows(target.process.dwProcessId);
              return windows.size() == expected;
          }),
          "discover ordinary framework application windows");
    auto topLevels = TopLevelCount(target.process.dwProcessId);
    for (HWND w : windows) {
        Check(Until([&] { return GetPropW(w, styler::SamplePaintProperty) != nullptr; }),
              "application renders before injection");
        std::wcout << L"Inline: " << styler::WindowTitle(w) << std::endl;
        RECT outer{}, client{}, after{}, currentClient{};
        GetWindowRect(w, &outer);
        GetClientRect(w, &client);
        auto childCount = ChildCount(w);
        auto style = GetWindowLongPtrW(w, GWL_STYLE), ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        POINT origin{};
        ClientToScreen(w, &origin);
        styler::GuardedSession session(controller);
        auto result = session.Apply(w);
        Check(result.ok, "direct framework layout injection succeeds");
        if (!result.ok) {
            std::wcout << result.message << std::endl;
            continue;
        }
        bool quick = Metric(w, styler::Metric::InlineKind) == 2;
        Check(HookModules(w) > 0, "hook DLL is mapped in the target process");
        Check(session.Apply(w).ok && session.Count() == 1, "direct attach is idempotent");
        Check(GetPropW(w, styler::InlineProperty) != nullptr, "target acknowledges an original-layout adapter");
        Check(TopLevelCount(target.process.dwProcessId) == topLevels, "injection creates no visible top-level surface");
        auto control = reinterpret_cast<HWND>(GetPropW(w, styler::InlineControlProperty));
        Check(!control && ChildCount(w) == childCount,
              "controls render inside the original framework tree without any new native surface HWND");
        Check(kind ? Metric(w, styler::Metric::InlineKind) == kind
                   : Metric(w, styler::Metric::InlineKind) == 1 || Metric(w, styler::Metric::InlineKind) == 2,
              "in-process adapter identifies the actual rendering framework");
        GetWindowRect(w, &after);
        GetClientRect(w, &currentClient);
        POINT currentOrigin{};
        ClientToScreen(w, &currentOrigin);
        Check(EqualRect(&outer, &after) && EqualRect(&client, &currentClient) && origin.x == currentOrigin.x &&
                  origin.y == currentOrigin.y,
              "first attach preserves complete outer and client geometry without dragging");
        auto currentStyle = GetWindowLongPtrW(w, GWL_STYLE);
        Check((style == currentStyle || (kind == 4 && (style ^ currentStyle) == WS_SYSMENU && (style & WS_SYSMENU))) &&
                  ex == GetWindowLongPtrW(w, GWL_EXSTYLE),
              "only a native system-button group may change its visibility style");
        Check(Metric(w, styler::Metric::InlineOriginalButtonsVisible) == 0,
              "original right-side window buttons are hidden");
        if (!kind || kind == 3 || kind == 4)
            Check(Metric(w, styler::Metric::InlineOriginalButtonsEnabled) == 0,
                  "hidden original commands are disabled for pointer and keyboard input");
        auto dpi = GetDpiForWindow(w);
        auto h = Metric(w, styler::Metric::InlineHeight);
        Check(h == MulDiv(40, dpi, 96) && Metric(w, styler::Metric::InlineTabsLeft) >= MulDiv(84, dpi, 96),
              "lights share the original 40-DIP tab row and tabs move right");
        if (!kind) {
            int tabsLeft = static_cast<int>(Metric(w, styler::Metric::InlineTabsLeft));
            ClientClick(w, tabsLeft + MulDiv(180, dpi, 96), static_cast<int>(h / 2));
            Check(Until([&] { return Metric(w, styler::Metric::InlineTabIndex) == 1; }),
                  "original second tab receives input after the layout shift");
        }
        bool captionStable = true;
        for (int i = 0; i < 12; ++i) {
            Send(w, WM_APP + 66, 1);
            captionStable = captionStable && Metric(w, styler::Metric::InlineOriginalButtonsVisible) == 0 &&
                            Metric(w, styler::Metric::InlineTabsLeft) >= MulDiv(84, dpi, 96);
            if (!kind || kind == 3 || kind == 4)
                captionStable = captionStable && Metric(w, styler::Metric::InlineOriginalButtonsEnabled) == 0;
        }
        Check(captionStable, "host view refresh cannot re-show original buttons or overwrite reserved title space");
        Check(Until([&] { return Metric(w, styler::Metric::InlineLeft) == 0; }),
              "host refresh settles the title layout before synchronous resize checks");
        ULONGLONG maximum{};
        bool synchronous = true;
        for (int i = 0; i < 24; ++i) {
            auto begin = GetTickCount64();
            SetWindowPos(w, nullptr, outer.left + i * 2, outer.top + i, outer.right - outer.left + i * 3,
                         outer.bottom - outer.top + i, SWP_NOZORDER | SWP_NOACTIVATE);
            auto left = Metric(w, styler::Metric::InlineLeft), height = Metric(w, styler::Metric::InlineHeight);
            if (left != 0 || height != h)
                std::cout << "Inline resize i=" << i << " left=" << left << " height=" << height << " expected=" << h
                          << std::endl;
            synchronous = synchronous && left == 0 && height == h;
            maximum = std::max(maximum, GetTickCount64() - begin);
        }
        Check(synchronous, "repeated move/resize keeps controls in the original layout synchronously");
        std::cout << "Maximum synchronous move/resize + diagnostic latency (ms): " << maximum << std::endl;
        SetWindowPos(w, nullptr, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InlineClick(w, 1);
        Check(Settled(w, [&] { return IsIconic(w) != FALSE; }), "injected yellow control minimizes the application");
        if (!IsWindow(w)) {
            DWORD code{};
            GetExitCodeProcess(target.process.hProcess, &code);
            std::cout << "Target exited after pointer input, code=" << std::hex << code << std::dec << std::endl;
        }
        Send(w, WM_SYSCOMMAND, SC_RESTORE);
        Check(Settled(w, [&] { return !IsIconic(w); }), "application restores after minimization");
        InlineClick(w, 2);
        Check(Settled(w, [&] { return IsZoomed(w) != FALSE; }), "injected green control maximizes the application");
        InlineClick(w, 2);
        Check(Settled(w, [&] { return !IsZoomed(w) && !IsIconic(w); }), "injected green control restores normal size");
        Check(session.Restore(w).ok, "original framework layout detaches");
        GetWindowRect(w, &after);
        GetClientRect(w, &currentClient);
        if (!EqualRect(&outer, &after) || !EqualRect(&client, &currentClient))
            std::cout << "Geometry before=" << outer.left << "," << outer.top << "," << outer.right << ","
                      << outer.bottom << " client=" << client.right << "," << client.bottom << " after=" << after.left
                      << "," << after.top << "," << after.right << "," << after.bottom
                      << " client=" << currentClient.right << "," << currentClient.bottom << std::endl;
        Check(EqualRect(&outer, &after) && EqualRect(&client, &currentClient) &&
                  style == GetWindowLongPtrW(w, GWL_STYLE) && ex == GetWindowLongPtrW(w, GWL_EXSTYLE) &&
                  !GetPropW(w, styler::InlineProperty) && !GetPropW(w, styler::OwnerProperty),
              "detach restores exact geometry/styles and removes injected state");
        Check(ChildCount(w) == childCount, "detach removes all native child HWNDs created by injection");
        if (!kind || kind == 3) {
            DWORD_PTR originalInput{};
            Check(Send(w, WM_APP + 67, 0, 0, &originalInput) && originalInput == 3,
                  "detach restores the original framework command group's input properties");
        }
        if (kind == 4) {
            Check(Until([&] { return session.Apply(w).ok; }), "attach before host disables its original command group");
            Send(w, WM_APP + 66, 2);
            Check(session.Restore(w).ok, "detach after the host changes its own caption policy");
            if (arguments.find(L"--hybrid") != arguments.npos)
                Check(!(GetWindowLongPtrW(w, GWL_STYLE) & WS_SYSMENU),
                      "detach preserves the host's latest disabled system menu");
            Send(w, WM_APP + 66, 1);
        }
        if (quick) {
            SetWindowPos(w, nullptr, 0, 0, MulDiv(750, dpi, 96), outer.bottom - outer.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            DWORD_PTR padding{}, visible{};
            bool bound = Until(
                [&] {
                    Send(w, WM_APP + 64, 0, 0, &padding);
                    Send(w, WM_APP + 65, 0, 0, &visible);
                    return padding == 6 && visible == 3;
                },
                1000);
            if (!bound)
                std::cout << "Quick restored padding=" << padding << ", buttons=" << visible << std::endl;
            Check(bound, "Quick padding and original-button bindings still react to resize after detach");
            SetWindowPos(w, nullptr, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Child worker(styler::ExecutableDirectory() + L"\\StylerTests.exe",
                     L"--inline-lease-worker " + std::to_wstring(reinterpret_cast<UINT_PTR>(w)));
        Check(Until([&] { return GetPropW(w, styler::InlineProperty) != nullptr; }),
              "independent controller attaches to the original layout");
        Check(worker.process.hProcess && TerminateProcess(worker.process.hProcess, 99),
              "terminate only the test controller");
        Check(Until([&] { return !GetPropW(w, styler::OwnerProperty) && !GetPropW(w, styler::InlineProperty); }),
              "guardian restores the layout after controller failure");
        {
            styler::GuardedSession doomed(controller);
            Check(doomed.Apply(w).ok, "attach before host-failure injection");
            HANDLE host = OpenProcess(PROCESS_TERMINATE, FALSE, doomed.HostPid());
            Check(host && TerminateProcess(host, 99), "terminate only the test guardian process");
            if (host)
                CloseHandle(host);
            Check(Until([&] { return !GetPropW(w, styler::OwnerProperty) && !GetPropW(w, styler::InlineProperty); }),
                  "in-process watchdog restores original layout after guardian failure");
        }
        Check(session.Apply(w).ok, "attach again for close-control validation");
        InlineClick(w, 0);
        Check(Until([&] { return !IsWindow(w); }), "injected red control closes only the test application window");
        --topLevels;
    }
    for (HWND w : windows)
        if (IsWindow(w))
            Send(w, WM_CLOSE);
    Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0,
          "framework applications exit cleanly after close");
    DestroyWindow(controller);
    return failures ? 1 : 0;
}
int XamlOsCaptionTests(const std::wstring &path) {
    HWND controller = Controller();
    auto clientPixels = [](HWND w) {
        RECT r{};
        GetClientRect(w, &r);
        POINT origin{};
        ClientToScreen(w, &origin);
        auto screen = GetDC(nullptr);
        auto dc = CreateCompatibleDC(screen);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = r.right;
        info.bmiHeader.biHeight = -r.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *data{};
        auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        auto old = bitmap && dc ? SelectObject(dc, bitmap) : nullptr;
        std::vector<DWORD> pixels;
        if (old && BitBlt(dc, 0, 0, r.right, r.bottom, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            for (int y = 3; y < r.bottom - 3; y += 3)
                for (int x = 3; x < r.right - 3; x += 3)
                    pixels.push_back(static_cast<DWORD *>(data)[y * r.right + x] & 0xffffff);
        }
        if (old) SelectObject(dc, old);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return pixels;
    };
    for (const auto *arguments : {L"--native-caption", L"--native-caption --no-system-menu",
                                  L"--native-caption --no-redirection"}) {
        Child target(path, arguments);
        std::vector<HWND> windows;
        Check(Until([&] {
                  windows = Windows(target.process.dwProcessId);
                  return windows.size() == 1 && GetPropW(windows.front(), styler::SamplePaintProperty);
              }),
              "real XAML composition client with a native-looking OS caption renders");
        if (windows.size() != 1)
            continue;
        HWND w = windows.front();
        SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(w);
        Check(Until([&] { return GetForegroundWindow() == w; }), "owned XAML fixture is unobscured for pixel checks");
        Until([] { return false; }, 250);
        Check(styler::DetectFramework(w).id == L"winui", "real XAML child identifies the framework");
        auto candidate = styler::InspectInline(w);
        Check(candidate.adapterMode != styler::Adapter::Native,
              "a real nonclient band cannot bypass original-tree validation for XAML composition");
        RECT outer{}, client{};
        POINT origin{};
        GetWindowRect(w, &outer);
        GetClientRect(w, &client);
        ClientToScreen(w, &origin);
        const auto style = GetWindowLongPtrW(w, GWL_STYLE);
        const auto childCount = ChildCount(w);
        const auto originalPixels = clientPixels(w);
        styler::GuardedSession session(controller);
        Check(!session.Apply(w).ok, "XAML host without a compatible original title tree rejects attachment");
        Check(!GetPropW(w, styler::OwnerProperty) && !GetPropW(w, styler::InlineProperty) &&
                  !GetPropW(w, styler::AdapterProperty),
              "rejected composition layout leaves no active caption lease");
        RECT currentOuter{}, currentClient{};
        POINT currentOrigin{};
        GetWindowRect(w, &currentOuter);
        GetClientRect(w, &currentClient);
        ClientToScreen(w, &currentOrigin);
        Check(EqualRect(&outer, &currentOuter) && EqualRect(&client, &currentClient) &&
                  origin.x == currentOrigin.x && origin.y == currentOrigin.y && ChildCount(w) == childCount,
              "rejection preserves original outer/client geometry, origin and XAML HWND count");
        Check(GetWindowLongPtrW(w, GWL_STYLE) == style, "rejection preserves original button capabilities");
        Check(!originalPixels.empty() && originalPixels == clientPixels(w),
              "rejected layout preserves sampled pixels of the real composed XAML client");
        Send(w, WM_CLOSE);
        Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0,
              "owned composition rejection fixture exits cleanly");
    }
    DestroyWindow(controller);
    return failures ? 1 : 0;
}
int DirectPolicyTests() {
    HWND controller = Controller();
    Child target(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--variants");
    std::vector<HWND> windows;
    Check(Until([&] {
              windows = Windows(target.process.dwProcessId);
              return windows.size() == 5;
          }),
          "discover native, Qt-like, Chromium-like and unknown layouts");
    styler::GuardedSession session(controller);
    if (!windows.empty()) {
        HWND child = CreateWindowExW(0, L"STATIC", L"policy child", WS_CHILD | WS_VISIBLE, 0, 0, 40, 20,
                                     windows.front(), nullptr, GetModuleHandleW(nullptr), nullptr);
        styler::Session raw(controller);
        Check(child && !styler::EmbeddedCore(child, windows.front()) &&
                  !styler::Inspect(child, true, false, windows.front()).supported,
              "embedded exception rejects an unrelated Win32 child and frame");
        Check(!raw.ApplyEmbedded(child, windows.front()).ok && !GetPropW(child, styler::EmbeddedFrameProperty) &&
                  !GetPropW(child, styler::OwnerProperty),
              "rejected embedded attachment leaves no request or ownership lease");
        DestroyWindow(child);
    }
    WNDCLASSW mock{};
    mock.hInstance = GetModuleHandleW(nullptr);
    mock.lpfnWndProc = DefWindowProcW;
    mock.lpszClassName = L"ApplicationFrameWindow";
    RegisterClassW(&mock);
    HWND localFrame =
        CreateWindowExW(0, mock.lpszClassName, L"embedded policy fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320,
                        200, nullptr, nullptr, mock.hInstance, nullptr);
    mock.lpszClassName = L"Windows.UI.Core.CoreWindow";
    RegisterClassW(&mock);
    HWND localCore = CreateWindowExW(0, mock.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 320, 200, localFrame,
                                     nullptr, mock.hInstance, nullptr);
    Check(localFrame && localCore && !styler::EmbeddedCore(localCore, localFrame) && !styler::FrameContent(localFrame),
          "matching class names in one process cannot impersonate an embedded cross-process pair");
    DestroyWindow(localFrame);
    for (HWND w : windows) {
        RECT before{}, outer{}, after{}, restored{};
        GetClientRect(w, &before);
        GetWindowRect(w, &outer);
        auto candidate = styler::InspectInline(w);
        auto count = TopLevelCount(target.process.dwProcessId);
        auto result = session.Apply(w);
        bool native = candidate.className == styler::TargetClass;
        Check(result.ok == native, "strict mode accepts native captions and rejects unsupported custom layouts");
        Check(TopLevelCount(target.process.dwProcessId) == count, "strict mode never adds a top-level drawing surface");
        GetClientRect(w, &after);
        Check(EqualRect(&before, &after), "accepted or rejected attachment keeps the complete client size");
        if (native) {
            Check(GetPropW(w, styler::AdapterProperty) == reinterpret_cast<HANDLE>(styler::Adapter::Native),
                  "default native path uses injected existing nonclient painting");
            Check(session.Restore(w).ok, "native direct session restores");
        } else
            Check(!GetPropW(w, styler::OwnerProperty) && !GetPropW(w, styler::InlineProperty) &&
                      !GetPropW(w, styler::AdapterProperty),
                  "rejected layout leaves no attachment metadata");
        GetWindowRect(w, &restored);
        Check(EqualRect(&outer, &restored), "rollback preserves original outer geometry");
    }
    for (HWND w : windows)
        Send(w, WM_CLOSE);
    Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0, "policy test applications exit cleanly");
    Child custom(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--custom-frameworks");
    Check(Until([&] {
              windows = Windows(custom.process.dwProcessId);
              return windows.size() == 4;
          }),
          "discover unsupported framework policy fixtures");
    std::vector<std::wstring> detected;
    for (auto w : windows) {
        auto kind = styler::DetectFramework(w).id;
        detected.push_back(kind);
        Check(!styler::InspectInline(w).supported,
              "unsupported framework cannot use a native-looking caption as fallback");
        ShowWindow(w, SW_MINIMIZE);
        Check(styler::DetectFramework(w).id == kind && !styler::InspectInline(w).supported,
              "minimized geometry preserves framework identity and rejection");
        Check(!GetPropW(w, styler::OwnerProperty), "classification does not mutate the target");
        Send(w, WM_CLOSE);
    }
    std::sort(detected.begin(), detected.end());
    Check(detected == std::vector<std::wstring>{L"mfc-bcg", L"office-netui", L"txgui", L"winui3"},
          "framework evidence distinguishes BCG, Office NetUI, TXGui and WinUI 3");
    Check(WaitForSingleObject(custom.process.hProcess, 4000) == WAIT_OBJECT_0,
          "framework policy fixtures exit cleanly");
    DestroyWindow(controller);
    return failures ? 1 : 0;
}
int BootstrapTests() {
    HWND controller = Controller();
    Child target(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--pair");
    std::vector<HWND> windows;
    Check(Until([&] {
              windows = Windows(target.process.dwProcessId);
              return windows.size() == 2;
          }),
          "bootstrap discovers separate native application windows");
    if (windows.size() != 2) {
        DestroyWindow(controller);
        return 1;
    }
    {
        styler::Session session(controller, true);
        for (auto window : windows) {
            RECT before{}, after{};
            GetClientRect(window, &before);
            auto style = GetWindowLongPtrW(window, GWL_STYLE);
            for (int cycle = 0; cycle < 2; cycle++) {
                Check(session.Apply(window, styler::Adapter::Native).ok,
                      "target-local bootstrap acknowledges GUI-thread attachment");
                DWORD_PTR response{};
                Check(Send(window, styler::QueryMessage(), 0, 0, &response) && response == styler::Acknowledged,
                      "bootstrap reaches the original GUI-thread subclass");
                Check(session.Restore(window).ok && !GetPropW(window, styler::OwnerProperty) &&
                          !GetPropW(window, styler::RequestProperty),
                      "bootstrap repeat detach removes ownership and requests");
            }
            GetClientRect(window, &after);
            Check(EqualRect(&before, &after) && style == GetWindowLongPtrW(window, GWL_STYLE),
                  "bootstrap preserves native styles and client dimensions");
        }
    }
    for (auto window : windows)
        Send(window, WM_CLOSE);
    Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0, "bootstrap target exits cleanly");
    DestroyWindow(controller);
    return failures ? 1 : 0;
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && wcscmp(argv[1], L"--thread-bootstrap") == 0)
        return BootstrapTests();
    if (argc == 2 && wcscmp(argv[1], L"--direct-policy") == 0)
        return DirectPolicyTests();
    if (argc == 4 && wcscmp(argv[1], L"--inline-framework") == 0)
        return InlineTests(argv[2], static_cast<size_t>(_wtoi(argv[3])));
    if (argc == 3 && wcscmp(argv[1], L"--wpf-inline") == 0)
        return InlineTests(argv[2], 1, 3);
    if (argc == 3 && wcscmp(argv[1], L"--xaml-inline") == 0)
        return InlineTests(argv[2], 1, 4);
    if (argc == 3 && wcscmp(argv[1], L"--xaml-hybrid") == 0)
        return InlineTests(argv[2], 1, 4, L"--hybrid");
    if (argc == 3 && wcscmp(argv[1], L"--xaml-os-caption") == 0)
        return XamlOsCaptionTests(argv[2]);
    if (argc == 3 && wcscmp(argv[1], L"--inline-lease-worker") == 0) {
        HWND controller = Controller();
        styler::GuardedSession session(controller);
        auto result = session.Apply(reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 10))));
        if (!result.ok) {
            // Preserve diagnostics when stderr is redirected without a console
            // locale capable of encoding the controller's Chinese messages.
            int size = WideCharToMultiByte(CP_UTF8, 0, result.message.data(), static_cast<int>(result.message.size()),
                                           nullptr, 0, nullptr, nullptr);
            std::string diagnostic(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, result.message.data(), static_cast<int>(result.message.size()),
                                diagnostic.data(), size, nullptr, nullptr);
            std::cerr << diagnostic << std::endl;
            return 2;
        }
        Sleep(120000);
        return 0;
    }
    if (argc == 3 && wcscmp(argv[1], L"--visual-lease-worker") == 0) {
        HWND controller = Controller();
        styler::GuardedSession session(controller, true);
        if (!session.Apply(reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 10)))).ok)
            return 2;
        Sleep(30000);
        return 0;
    }
    if (argc == 2 && wcscmp(argv[1], L"--visual") == 0)
        return VisualTests(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--variants", 5, true);
    if (argc == 4 && wcscmp(argv[1], L"--visual-framework") == 0)
        return VisualTests(argv[2], L"", static_cast<size_t>(_wtoi(argv[3])), false);
    if (argc == 3 && wcscmp(argv[1], L"--visual-architecture") == 0)
        return VisualTests(argv[2], L"", 1, false);
    if (argc == 2 && wcscmp(argv[1], L"--audit") == 0) {
        Child target(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--variants");
        std::vector<HWND> windows;
        Check(Until([&] {
                  windows = Windows(target.process.dwProcessId);
                  return windows.size() == 5;
              }),
              "audit discovers five independent test HWNDs");
        for (size_t i = 0; i < windows.size(); ++i) {
            HWND window = windows[i];
            RECT before{}, after{};
            GetWindowRect(window, &before);
            bool minimized = i == 0;
            if (minimized) {
                Send(window, WM_SYSCOMMAND, SC_MINIMIZE);
                Check(Until([&] { return IsIconic(window) != FALSE; }), "audit starts with a minimized target");
            }
            auto directory = styler::ExecutableDirectory() + L"\\audit-tests\\" +
                             std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i);
            Child audit(styler::ExecutableDirectory() + L"\\WindowAudit.exe",
                        L"--window " + std::to_wstring(reinterpret_cast<UINT_PTR>(window)) + L" \"" + directory +
                            L"\"");
            Check(audit.process.hProcess && WaitForSingleObject(audit.process.hProcess, 15000) == WAIT_OBJECT_0,
                  "independent per-window audit finishes within its deadline");
            DWORD code{99};
            GetExitCodeProcess(audit.process.hProcess, &code);
            Check(code == 0, "all in-place DWM, geometry preservation and restoration audit checks pass");
            Check(std::filesystem::exists(directory + L"\\report.json") &&
                      std::filesystem::exists(directory + L"\\report.md"),
                  "audit writes machine-readable and readable per-window reports");
            Check(!GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::AdapterProperty),
                  "audit leaves no attached window state");
            GetWindowRect(window, &after);
            Check(minimized ? IsIconic(window) != FALSE : EqualRect(&before, &after),
                  "audit restores original placement");
            Send(window, WM_CLOSE);
        }
        return failures ? 1 : 0;
    }
    if ((argc == 3 || argc == 4) && wcscmp(argv[1], L"--framework-target") == 0) {
        size_t expectedWindows = argc == 4 ? static_cast<size_t>(_wtoi(argv[3])) : 3;
        HWND controller = Controller();
        Child target(argv[2], L"");
        std::vector<HWND> windows;
        Check(target.process.hProcess && Until([&] {
                  windows = Windows(target.process.dwProcessId);
                  return windows.size() == expectedWindows;
              }),
              "start actual framework windows");
        styler::Session session(controller);
        for (HWND window : windows) {
            std::wcout << L"Framework: " << styler::WindowTitle(window) << std::endl;
            Check(Until([&] { return GetPropW(window, styler::SamplePaintProperty) != nullptr; }),
                  "framework performs its initial render");
            auto paints = reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty));
            RECT original{}, after{};
            GetClientRect(window, &original);
            auto result = session.Apply(window);
            if (!result.ok)
                std::wcerr << result.message << std::endl;
            Check(result.ok, "attach to an actual framework HWND");
            Check(Until([&] {
                      return reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty)) > paints;
                  }),
                  "framework renders or flushes its backing store after first apply without manual movement");
            GetClientRect(window, &after);
            Check(EqualRect(&original, &after), "framework client size remains complete after frame transition");
            POINT close = ButtonPoint(window, 0);
            DWORD_PTR hit{};
            Send(window, WM_NCHITTEST, 0, MAKELPARAM(close.x, close.y), &hit);
            Check(hit == HTCLOSE, "custom caption is present and hit-testable on first apply");
            Check(session.Restore(window).ok, "restore actual framework window");
            GetClientRect(window, &after);
            Check(EqualRect(&original, &after), "restore framework client size");
        }
        for (HWND window : windows)
            Send(window, WM_CLOSE);
        Check(WaitForSingleObject(target.process.hProcess, 4000) == WAIT_OBJECT_0, "framework target exits cleanly");
        DestroyWindow(controller);
        return failures ? 1 : 0;
    }
    if (argc == 3 && wcscmp(argv[1], L"--check-architecture") == 0) {
        HWND controller = Controller();
        Child target(argv[2], L"");
        std::vector<HWND> windows;
        Check(target.process.hProcess && Until([&] {
                  windows = Windows(target.process.dwProcessId);
                  return !windows.empty();
              }),
              "start the opposite-architecture target");
        if (!windows.empty()) {
            styler::Session session(controller);
            auto result = session.Apply(windows[0]);
            Check(!result.ok && result.message.find(L"架构不同") != std::wstring::npos,
                  "reject a cross-architecture target before installing a hook");
            Check(!GetPropW(windows[0], styler::OwnerProperty) && !GetPropW(windows[0], styler::RequestProperty),
                  "architecture rejection leaves no target state");
            Send(windows[0], WM_CLOSE);
            Check(WaitForSingleObject(target.process.hProcess, 3000) == WAIT_OBJECT_0,
                  "opposite-architecture target exits cleanly");
        }
        DestroyWindow(controller);
        return failures ? 1 : 0;
    }
    if (argc == 3 && wcscmp(argv[1], L"--lease-worker") == 0) {
        HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 10)));
        HWND controller = Controller();
        styler::Session session(controller);
        if (!session.Apply(window).ok)
            return 2;
        Sleep(30000);
        return 0;
    }
    HWND controller = Controller();
    Check(controller != nullptr, "create a controller window with the protocol class");
    Child target(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--pair");
    Check(target.process.hProcess != nullptr, "start a separate target process");
    std::vector<HWND> windows;
    Check(Until([&] {
              windows = Windows(target.process.dwProcessId);
              return windows.size() == 2;
          }),
          "discover two independent HWNDs on the target GUI thread");
    if (windows.size() != 2) {
        DestroyWindow(controller);
        return 1;
    }
    HWND plain = GetMenu(windows[0]) ? windows[1] : windows[0];
    HWND menuWindow = plain == windows[0] ? windows[1] : windows[0];
    LONG_PTR originalStyle = GetWindowLongPtrW(plain, GWL_STYLE);
    LONG_PTR originalExStyle = GetWindowLongPtrW(plain, GWL_EXSTYLE);
    HMENU originalMenu = GetMenu(menuWindow);
    DWORD_PTR originalScrollbarHits[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        SCROLLBARINFO info{sizeof(info)};
        if (GetScrollBarInfo(menuWindow, i ? OBJID_HSCROLL : OBJID_VSCROLL, &info))
            Send(menuWindow, WM_NCHITTEST, 0,
                 MAKELPARAM((info.rcScrollBar.left + info.rcScrollBar.right) / 2,
                            (info.rcScrollBar.top + info.rcScrollBar.bottom) / 2),
                 &originalScrollbarHits[i]);
    }
    RECT originalClient{};
    GetClientRect(plain, &originalClient);
    BOOL originalRendering{};
    DwmGetWindowAttribute(plain, DWMWA_NCRENDERING_ENABLED, &originalRendering, sizeof(BOOL));
    DWORD originalCorner{};
    bool cornerSupported = SUCCEEDED(DwmGetWindowAttribute(plain, 33, &originalCorner, sizeof(DWORD)));
    {
        styler::Session session(controller);
        Check(!session.Apply(controller).ok, "reject attaching to the controller process");
        Check(!session.Apply(nullptr).ok, "reject an invalid HWND");
        auto beforePaint = reinterpret_cast<UINT_PTR>(GetPropW(plain, styler::SamplePaintProperty));
        auto result = session.Apply(plain);
        if (!result.ok)
            std::wcerr << result.message << std::endl;
        Check(result.ok && session.Owns(plain), "inject the DLL and attach in a different process");
        Check(reinterpret_cast<UINT_PTR>(GetPropW(plain, styler::SamplePaintProperty)) > beforePaint,
              "first attach repaints the client immediately without moving the window");
        Check(session.Apply(plain).ok && session.Count() == 1, "repeated attach is idempotent");
        DWORD_PTR acknowledgement{};
        Check(Send(plain, styler::QueryMessage(), 0, 0, &acknowledgement) && acknowledgement == styler::Acknowledged,
              "query is acknowledged by the injected subclass");
        Check(GetWindowLongPtrW(plain, GWL_STYLE) == originalStyle &&
                  GetWindowLongPtrW(plain, GWL_EXSTYLE) == originalExStyle,
              "original style and extended style bits are unchanged");
        Check(RoundedRegion(plain, true), "first frame clips all four original window corners");
        SetWindowRgn(plain, nullptr, FALSE);
        Send(plain, WM_THEMECHANGED);
        Check(Until([&] { return RoundedRegion(plain, true); }),
              "same-size theme notification rebuilds a cleared host region");
        RECT rectangular{};
        GetWindowRect(plain, &rectangular);
        SetWindowRgn(plain,
                     CreateRectRgn(0, 0, rectangular.right - rectangular.left, rectangular.bottom - rectangular.top),
                     FALSE);
        Send(plain, WM_SETTINGCHANGE);
        Check(Until([&] { return RoundedRegion(plain, true); }),
              "same-size setting notification replaces a rectangular host region");
        RECT currentClient{};
        GetClientRect(plain, &currentClient);
        Check(EqualRect(&currentClient, &originalClient), "original client geometry is preserved");
        HWND edit = GetDlgItem(plain, 200);
        Check(Send(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"client survives injection")),
              "edit control accepts input after attach");
        wchar_t text[128]{};
        Send(edit, WM_GETTEXT, 128, reinterpret_cast<LPARAM>(text));
        Check(wcscmp(text, L"client survives injection") == 0, "original edit control retains its content");
        Send(plain, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Dynamic title test"));
        Check(styler::WindowTitle(plain) == L"Dynamic title test", "dynamic window titles are preserved");
        const LRESULT expected[] = {HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
        for (int i = 0; i < 3; ++i) {
            POINT point = ButtonPoint(plain, i);
            DWORD_PTR hit{};
            Send(plain, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y), &hit);
            Check(hit == static_cast<DWORD_PTR>(expected[i]), i == 0   ? "red button hit test"
                                                              : i == 1 ? "yellow button hit test"
                                                                       : "green button hit test");
        }
        POINT clientPoint{40, 160};
        ClientToScreen(plain, &clientPoint);
        DWORD_PTR hit{};
        Send(plain, WM_NCHITTEST, 0, MAKELPARAM(clientPoint.x, clientPoint.y), &hit);
        Check(hit == HTCLIENT, "client input area is not covered by the custom frame");
        RECT frame{};
        GetWindowRect(plain, &frame);
        POINT dragPoint{frame.right - 40, ButtonPoint(plain, 0).y};
        Send(plain, WM_NCHITTEST, 0, MAKELPARAM(dragPoint.x, dragPoint.y), &hit);
        Check(hit == HTCAPTION, "covered native buttons become draggable title bar space");
        POINT edge{frame.left + 1, (frame.top + frame.bottom) / 2};
        Send(plain, WM_NCHITTEST, 0, MAKELPARAM(edge.x, edge.y), &hit);
        Check(hit == HTLEFT, "native resize border is retained");
        ClickButton(plain, 1);
        Check(Until([&] { return IsIconic(plain) != FALSE; }), "yellow button minimizes the real target window");
        Send(plain, WM_SYSCOMMAND, SC_RESTORE);
        Check(Until([&] { return !IsIconic(plain); }), "native restore still works");
        SetPropW(plain, L"Macdows.Sample.FullscreenMaxCommand", reinterpret_cast<HANDLE>(1));
        ClickButton(plain, 2);
        Check(Settled(plain, [&] { return IsZoomed(plain) != FALSE; }),
              "green button maximizes the real target window");
        Check((GetWindowLongPtrW(plain, GWL_STYLE) & WS_CAPTION) == WS_CAPTION,
              "native green uses standard maximize even when host commands request borderless fullscreen");
        Check(Until([&] { return RoundedRegion(plain, false); }),
              "maximized frame has rectangular corners without clipped content");
        ClickButton(plain, 2);
        Check(Settled(plain, [&] { return !IsZoomed(plain); }), "green button restores from maximized state");
        RemovePropW(plain, L"Macdows.Sample.FullscreenMaxCommand");
        Check(Until([&] { return RoundedRegion(plain, true); }), "restore reapplies all four rounded corners");
        Check(session.Apply(menuWindow).ok && session.Count() == 2, "attach a second window on the same target thread");
        Check(GetMenu(menuWindow) == originalMenu, "native menu handle is preserved");
        for (auto id : {OBJID_VSCROLL, OBJID_HSCROLL}) {
            SCROLLBARINFO info{sizeof(info)};
            bool visible = Until([&] {
                info = {sizeof(info)};
                return GetScrollBarInfo(menuWindow, id, &info) && !(info.rgstate[0] & STATE_SYSTEM_INVISIBLE) &&
                       !IsRectEmpty(&info.rcScrollBar);
            });
            Check(visible, "native non-client scrollbar remains visible after injection");
            if (visible) {
                POINT p{(info.rcScrollBar.left + info.rcScrollBar.right) / 2,
                        (info.rcScrollBar.top + info.rcScrollBar.bottom) / 2};
                Send(menuWindow, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y), &hit);
                if (hit != originalScrollbarHits[id == OBJID_VSCROLL ? 0 : 1])
                    std::cout << "Scrollbar hit actual=" << hit
                              << " original=" << originalScrollbarHits[id == OBJID_VSCROLL ? 0 : 1] << " center=" << p.x
                              << "," << p.y << std::endl;
                Check(hit == originalScrollbarHits[id == OBJID_VSCROLL ? 0 : 1],
                      "native scrollbar keeps its original input region");
            }
        }
        POINT menuPoint{};
        ClientToScreen(menuWindow, &menuPoint);
        menuPoint.x += 18;
        menuPoint.y -= GetSystemMetricsForDpi(SM_CYMENU, GetDpiForWindow(menuWindow)) / 2;
        Send(menuWindow, WM_NCHITTEST, 0, MAKELPARAM(menuPoint.x, menuPoint.y), &hit);
        Check(hit == HTMENU, "native menu area retains its hit test");
        Send(menuWindow, WM_COMMAND, 301);
        Check(styler::WindowTitle(menuWindow) == L"标题已更新 · 原生菜单正常工作",
              "native menu command still reaches the target");
        HWND otherController = Controller();
        {
            styler::Session other(otherController);
            Check(!other.Apply(plain).ok, "reject a second controller taking over an attached window");
            DWORD_PTR detachResult{};
            Send(plain, styler::DetachMessage(), reinterpret_cast<WPARAM>(otherController), 0, &detachResult);
            Check(detachResult == 0 && session.Owns(plain), "ignore detach from the wrong controller");
        }
        DestroyWindow(otherController);
        Check(session.Restore(plain).ok && !GetPropW(plain, styler::OwnerProperty),
              "explicit detach clears target ownership");
        Check(!HasWindowRegion(plain), "detach removes the injected region and returns shape ownership to DWM");
        Check(GetWindowLongPtrW(plain, GWL_STYLE) == originalStyle &&
                  GetWindowLongPtrW(plain, GWL_EXSTYLE) == originalExStyle,
              "style bits are unchanged after detach");
        BOOL restoredRendering{};
        DwmGetWindowAttribute(plain, DWMWA_NCRENDERING_ENABLED, &restoredRendering, sizeof(BOOL));
        Check(originalRendering == restoredRendering, "restore the original DWM rendering enabled state");
        DWORD restoredCorner{};
        if (cornerSupported) {
            DwmGetWindowAttribute(plain, 33, &restoredCorner, sizeof(DWORD));
            Check(restoredCorner == originalCorner, "restore the original DWM corner preference");
        }
        Check(session.Apply(plain).ok, "reattach after restore");
        ClickButton(menuWindow, 0);
        Check(Until([&] { return !IsWindow(menuWindow); }),
              "red button closes the real target and cleans up its subclass");
        Check(session.Restore(menuWindow).ok, "restore tolerates a destroyed target HWND");
        Check(session.RestoreAll().ok && session.Count() == 0, "restore all remaining windows");
        Send(plain, WM_SYSCOMMAND, SC_MAXIMIZE);
        Check(Settled(plain, [&] { return IsZoomed(plain) != FALSE; }), "prepare an initially maximized native target");
        Check(session.Apply(plain).ok && RoundedRegion(plain, false),
              "attach to an already maximized target without rounding screen corners");
        Send(plain, WM_SYSCOMMAND, SC_RESTORE);
        Check(Settled(plain, [&] { return !IsZoomed(plain) && RoundedRegion(plain, true); }),
              "initially maximized target restores its normal rounded frame");
        Check(session.Restore(plain).ok && !HasWindowRegion(plain),
              "initially maximized lease restores the original absence of a region");
        PostMessageW(plain, WM_APP + 42, 3200, 0);
        Sleep(80);
        ULONGLONG start = GetTickCount64();
        Check(!session.Apply(plain).ok && GetTickCount64() - start < 2900,
              "hung target attach returns within its bounded timeout");
        Check(session.Restore(plain).ok, "cancel a pending timed-out attach");
        Check(Until([&] { return Send(plain, WM_NULL); }), "fault-injected GUI thread recovers");
        Check(!GetPropW(plain, styler::OwnerProperty) && !GetPropW(plain, styler::RequestProperty),
              "a canceled late attach leaves no ownership or request behind");
    }
    {
        Child worker(styler::ExecutableDirectory() + L"\\StylerTests.exe",
                     L"--lease-worker " + std::to_wstring(reinterpret_cast<UINT_PTR>(plain)));
        Check(Until([&] { return GetPropW(plain, styler::OwnerProperty) != nullptr; }),
              "separate controller worker attaches");
        if (worker.process.hProcess) {
            TerminateProcess(worker.process.hProcess, 77);
            WaitForSingleObject(worker.process.hProcess, 1000);
        }
        Check(Until([&] { return GetPropW(plain, styler::OwnerProperty) == nullptr; }),
              "target automatically restores after abrupt controller termination");
        Check(IsWindow(plain) && Send(plain, WM_NULL), "target stays alive after controller termination");
    }
    Send(plain, WM_CLOSE);
    Check(WaitForSingleObject(target.process.hProcess, 3000) == WAIT_OBJECT_0,
          "target process exits cleanly after repeated attach/detach");
    {
        Child variants(styler::ExecutableDirectory() + L"\\StyleTarget.exe", L"--variants");
        std::vector<HWND> samples;
        Check(Until([&] {
                  samples = Windows(variants.process.dwProcessId);
                  return samples.size() == 5;
              }),
              "create native, menu, frameless, known-custom and unknown-custom window models");
        styler::Session session(controller);
        for (HWND window : samples) {
            std::wcout << L"Variant: " << styler::WindowTitle(window) << std::endl;
            RECT before{}, after{};
            GetClientRect(window, &before);
            LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
            auto paints = reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty));
            Check(styler::Inspect(window).supported, "window model has an eligible adapter");
            Check(session.Apply(window).ok, "attach the selected adapter");
            Check(reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty)) > paints,
                  "first attach paints client content without a drag or resize");
            GetClientRect(window, &after);
            Check(EqualRect(&before, &after), "frame transition preserves the complete client dimensions");
            Check(GetWindowLongPtrW(window, GWL_STYLE) == style, "adapter preserves the application's style flags");
            RECT outer{};
            GetWindowRect(window, &outer);
            POINT origin{};
            ClientToScreen(window, &origin);
            Check(origin.y > outer.top, "caption has a real non-client band on first apply");
            // The original transparent resize footprint remains in GetWindowRect.
            // Only the visible region must meet the client; shrinking the HWND
            // to remove that invisible footprint would move/resize the app.
            RECT visible = outer;
            HRGN region = CreateRectRgn(0, 0, 0, 0);
            if (GetWindowRgn(window, region) != ERROR) {
                GetRgnBox(region, &visible);
                OffsetRect(&visible, outer.left, outer.top);
            }
            DeleteObject(region);
            int leftAllowance = MulDiv(2, GetDpiForWindow(window), 96), rightAllowance = leftAllowance,
                bottomAllowance = leftAllowance;
            // The style reserves this NC space even when accessibility marks
            // an occluded sample's scrollbar invisible.
            if (style & WS_VSCROLL) {
                auto width = GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window));
                if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LEFTSCROLLBAR)
                    leftAllowance += width;
                else
                    rightAllowance += width;
            }
            if (style & WS_HSCROLL)
                bottomAllowance += GetSystemMetricsForDpi(SM_CYHSCROLL, GetDpiForWindow(window));
            if (origin.x < visible.left || origin.x - visible.left > leftAllowance ||
                visible.right < origin.x + after.right || visible.right - origin.x - after.right > rightAllowance ||
                visible.bottom < origin.y + after.bottom || visible.bottom - origin.y - after.bottom > bottomAllowance)
                std::cout << "frame geometry: visible=" << visible.left << ',' << visible.top << ',' << visible.right
                          << ',' << visible.bottom << " client-origin=" << origin.x << ',' << origin.y
                          << " client-size=" << after.right << ',' << after.bottom << " dpi=" << GetDpiForWindow(window)
                          << " allowances=" << leftAllowance << ',' << rightAllowance << ',' << bottomAllowance
                          << std::endl;
            Check(origin.x >= visible.left && origin.x - visible.left <= leftAllowance &&
                      visible.right >= origin.x + after.right &&
                      visible.right - origin.x - after.right <= rightAllowance,
                  "visible side frame reaches the client without a wide blank margin or clipping");
            Check(visible.bottom >= origin.y + after.bottom &&
                      visible.bottom - origin.y - after.bottom <= bottomAllowance,
                  "visible bottom frame reaches the client without a wide blank margin or clipping");
            for (int i = 0; i < 3; ++i) {
                POINT point = ButtonPoint(window, i);
                DWORD_PTR hit{};
                Send(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y), &hit);
                const LRESULT expected[] = {HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
                Check(hit == static_cast<DWORD_PTR>(expected[i]), "traffic-light hit test works for this model");
            }
            ClickButton(window, 2);
            Check(Settled(window, [&] { return IsZoomed(window) != FALSE; }), "custom/native adapter maximizes");
            ClickButton(window, 2);
            Check(Settled(window, [&] { return !IsZoomed(window); }), "custom/native adapter restores");
            if (styler::WindowTitle(window).find(L"Unknown") != std::wstring::npos)
                Check(GetPropW(window, styler::AdapterProperty) == reinterpret_cast<HANDLE>(styler::Adapter::Extended),
                      "actual NCCALCSIZE geometry detects an unknown custom frame");
            GetClientRect(window, &before);
            Check(session.Restore(window).ok, "restore the window model");
            GetClientRect(window, &after);
            Check(EqualRect(&before, &after), "restore keeps client dimensions and removes the extra band");
            Check(!GetPropW(window, styler::AdapterProperty), "restore removes adapter state");
            Send(window, WM_CLOSE);
        }
        Check(WaitForSingleObject(variants.process.hProcess, 3000) == WAIT_OBJECT_0, "all window models exit cleanly");
    }
    DestroyWindow(controller);
    std::cout << "Failures: " << failures << std::endl;
    return failures ? 1 : 0;
}
