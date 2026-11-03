#include "inline.h"
#include "host-client.h"
#include <shellapi.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <io.h>
#include <fcntl.h>

namespace {
std::unique_ptr<styler::GuardedSession> session;
HWND controller{};
inline constexpr wchar_t InjectorProperty[] = L"Macdows.WindowStyler.Injector.v4";
UINT Control() {
    return RegisterWindowMessageW(L"Macdows.WindowStyler.Injector.Control.v4");
}
LRESULT CALLBACK Procedure(HWND w, UINT m, WPARAM p, LPARAM l) {
    if (m == Control()) {
        if (p == 1)
            return session->RestoreAll().ok;
        if (p == 2)
            return session->Restore(reinterpret_cast<HWND>(l)).ok;
    }
    if (m == WM_TIMER || m == WM_CLOSE) {
        DestroyWindow(w);
        return 0;
    }
    if (m == WM_DESTROY) {
        session.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, p, l);
}
BOOL WINAPI Cancel(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        PostMessageW(controller, WM_CLOSE, 0, 0);
        return TRUE;
    }
    return FALSE;
}
bool Notify(UINT command, HWND target) {
    struct Data {
        UINT command;
        HWND target;
        bool ok{};
    } data{command, target};
    EnumWindows(
        [](HWND w, LPARAM param) -> BOOL {
            auto &d = *reinterpret_cast<Data *>(param);
            if (GetPropW(w, InjectorProperty)) {
                DWORD_PTR result{};
                if (SendMessageTimeoutW(w, Control(), d.command, reinterpret_cast<LPARAM>(d.target), SMTO_ABORTIFHUNG,
                                        5000, &result) &&
                    result)
                    d.ok = true;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));
    return data.ok;
}

} // namespace
int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    if (argc < 2 || wcscmp(argv[1], L"--list") == 0) {
        for (auto &c : styler::EnumerateInline())
            std::wcout << L"0x" << std::hex << reinterpret_cast<UINT_PTR>(c.window) << std::dec << L" | " << c.className
                       << L" | " << c.title << L" | " << (c.supported ? c.adapter : c.reason) << L"\n";
        std::wcout << L"Usage: WindowInjector --apply 0xHWND [--duration seconds]\n       WindowInjector --all "
                      L"[--duration seconds]\n       WindowInjector --detach 0xHWND | --restore-all\nClass matches are "
                      L"candidates; plugin Probe validates the actual layout before mutation.\n";
        return 0;
    }
    if (wcscmp(argv[1], L"--restore-all") == 0)
        return Notify(1, nullptr) ? 0 : 2;
    bool all = wcscmp(argv[1], L"--all") == 0;
    bool detach = wcscmp(argv[1], L"--detach") == 0;
    if (!all && !detach && wcscmp(argv[1], L"--apply") != 0)
        return 2;
    if (!all && argc < 3)
        return 2;
    HWND target = all ? nullptr : reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 0)));
    if (detach)
        return Notify(2, target) ? 0 : 2;
    if (!all && !IsWindow(target))
        return 2;
    WNDCLASSW cls{};
    cls.lpfnWndProc = Procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    controller = CreateWindowExW(0, cls.lpszClassName, L"Macdows direct injector", 0, 0, 0, 0, 0, nullptr, nullptr,
                                 cls.hInstance, nullptr);
    if (!controller)
        return 3;
    session = std::make_unique<styler::GuardedSession>(controller);
    SetPropW(controller, InjectorProperty, reinterpret_cast<HANDLE>(UINT_PTR(1)));
    SetConsoleCtrlHandler(Cancel, TRUE);
    size_t applied{};
    auto apply = [&](HWND w) {
        auto result = session->Apply(w);
        std::wcout << L"0x" << std::hex << reinterpret_cast<UINT_PTR>(w) << std::dec << L" " << result.message << L"\n";
        if (result.ok)
            ++applied;
    };
    if (all) {
        for (auto &c : styler::EnumerateInline())
            if (c.supported)
                apply(c.window);
            else
                std::wcout << L"Skipped: " << c.title << L" | " << c.reason << L"\n";
    } else
        apply(target);
    if (!applied) {
        DestroyWindow(controller);
        return 4;
    }
    for (int i = all ? 2 : 3; i + 1 < argc; ++i)
        if (wcscmp(argv[i], L"--duration") == 0) {
            auto seconds = wcstoul(argv[i + 1], nullptr, 10);
            if (seconds > 0 && seconds < 86400)
                SetTimer(controller, 1, seconds * 1000, nullptr);
        }
    std::wcout << L"Attached " << applied << L". Keep this process running; Ctrl+C restores the original layout.\n";
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
