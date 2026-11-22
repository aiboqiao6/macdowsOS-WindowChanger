// Explicit HWND + PID only; never enumerates/injects into other applications.
#include "../../session.h"
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    std::cout << std::unitbuf;
    if (argc != 3)
        return ERROR_INVALID_PARAMETER;
    wchar_t *end{};
    const auto value = wcstoull(argv[1], &end, 0);
    if (!value || !end || *end)
        return ERROR_INVALID_PARAMETER;
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(value));
    const auto expectedPid = wcstoul(argv[2], &end, 10);
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    if (!expectedPid || !end || *end || pid != expectedPid || !IsWindow(target) || GetForegroundWindow() == target)
        return ERROR_INVALID_WINDOW_HANDLE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT outer{}, client{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    const auto style = GetWindowLongPtrW(target, GWL_STYLE);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Owned Office background research", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return GetLastError();
    int code{};
    {
        styler::Session session(owner);
        const auto result = session.Apply(target, static_cast<styler::Adapter>(272));
        std::cout << "attached=" << result.ok << '\n';
        if (!result.ok)
            code = ERROR_NOT_SUPPORTED;
        else {
            const auto until = GetTickCount64() + 1000;
            while (GetTickCount64() < until) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            const auto owned = session.Owns(target);
            std::cout << "query=" << owned << '\n';
            TITLEBARINFOEX info{};
            info.cbSize = sizeof(info);
            SendMessageW(target, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
            bool hidden = true;
            for (auto i : {2, 3, 5})
                hidden &= (info.rgstate[i] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)) ==
                              (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE) &&
                          IsRectEmpty(&info.rgrect[i]);
            std::cout << "original-buttons-hidden-disabled=" << hidden << '\n';
            if (!owned || !hidden)
                code = ERROR_INVALID_STATE;
            const auto restored = session.RestoreAll();
            std::cout << "restored=" << restored.ok << '\n';
            if (!restored.ok)
                code = ERROR_INVALID_STATE;
        }
        RECT nowOuter{}, nowClient{};
        GetWindowRect(target, &nowOuter);
        GetClientRect(target, &nowClient);
        const auto exact = EqualRect(&outer, &nowOuter) && EqualRect(&client, &nowClient) &&
                           style == GetWindowLongPtrW(target, GWL_STYLE);
        std::cout << "original-geometry-and-style-restored=" << exact << '\n';
        if (!exact || GetForegroundWindow() == target)
            code = ERROR_INVALID_STATE;
    }
    DestroyWindow(owner);
    return code;
}
