#include "host-client.h"
#include <string>

namespace {
HWND FixtureWindow(DWORD process) {
    struct Search {
        DWORD process;
        HWND window{};
    } search{process};
    EnumWindows(
        [](HWND h, LPARAM value) -> BOOL {
            auto &s = *reinterpret_cast<Search *>(value);
            DWORD pid{};
            GetWindowThreadProcessId(h, &pid);
            if (pid == s.process && GetPropW(h, styler::SampleProperty)) {
                s.window = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"Macdows.WinUI3.DemoOwner";
    RegisterClassW(&cls);
    HWND owner = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!owner)
        return 1;
    auto directory = styler::ExecutableDirectory();
    auto path = directory + L"\\WinUI3InlineTarget.exe";
    std::wstring command = L"\"" + path + L"\" --binding";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup,
                        &child)) {
        MessageBoxW(nullptr, L"Cannot start the bundled WinUI 3 test application.", L"WinUI 3 framework demo",
                    MB_OK | MB_ICONERROR);
        DestroyWindow(owner);
        return 2;
    }
    CloseHandle(child.hThread);
    HWND fixture{};
    auto deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
        fixture = FixtureWindow(child.dwProcessId);
        if (fixture)
            break;
        Sleep(20);
    }
    int result{};
    {
        styler::GuardedSession session(owner);
        auto applied =
            fixture
                ? session.Apply(fixture)
                : styler::Result{
                      false,
                      L"The test application did not create a supported window. Windows App SDK 2 x64 is required."};
        if (!applied.ok || reinterpret_cast<UINT_PTR>(GetPropW(fixture, styler::AdapterProperty)) != 266) {
            session.RestoreAll();
            MessageBoxW(nullptr, applied.message.c_str(), L"WinUI 3 framework demo", MB_OK | MB_ICONERROR);
            if (fixture)
                PostMessageW(fixture, WM_CLOSE, 0, 0);
            result = 3;
        } else {
            // Wait for our own child and dispatch owner messages. This performs no
            // positioning, paint polling, or lookup of other application windows.
            while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) {
                auto wait = MsgWaitForMultipleObjects(1, &child.hProcess, FALSE, INFINITE, QS_ALLINPUT);
                if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED)
                    break;
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }
    }
    CloseHandle(child.hProcess);
    DestroyWindow(owner);
    return result;
}
