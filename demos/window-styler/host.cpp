#include "host-client.h"
#include "visual.h"
#include "inline.h"
#include <memory>

namespace {
HWND parentWindow{};
DWORD parentPid{};
HANDLE parentProcess{};
std::unique_ptr<styler::VisualSession> session;
std::unique_ptr<styler::InlineSession> direct;
styler::Result RestoreAll() {
    return direct ? direct->RestoreAll() : session->RestoreAll();
}
bool stopping{};
unsigned retries{};
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM param, LPARAM data) {
    if (message == styler::HostMessage()) {
        switch (static_cast<styler::HostCommand>(param)) {
        case styler::HostCommand::Apply: {
            if (stopping)
                return 0;
            auto result =
                direct ? direct->Apply(reinterpret_cast<HWND>(data)) : session->Apply(reinterpret_cast<HWND>(data));
            SetWindowTextW(window, result.message.c_str());
            return result.ok;
        }
        case styler::HostCommand::Restore:
            return (direct ? direct->Restore(reinterpret_cast<HWND>(data))
                           : session->Restore(reinterpret_cast<HWND>(data)))
                .ok;
        case styler::HostCommand::RestoreAll:
            return RestoreAll().ok;
        case styler::HostCommand::Count:
            return static_cast<LRESULT>(direct ? direct->Count() : session->Count());
        case styler::HostCommand::Query:
            return styler::Acknowledged;
        case styler::HostCommand::Stop:
            stopping = true;
            if (RestoreAll().ok)
                PostMessageW(window, WM_CLOSE, 0, 0);
            return 1;
        }
    }
    if (message == WM_TIMER) {
        DWORD pid{};
        GetWindowThreadProcessId(parentWindow, &pid);
        if (!IsWindow(parentWindow) || pid != parentPid || WaitForSingleObject(parentProcess, 0) != WAIT_TIMEOUT)
            stopping = true;
        if (stopping) {
            if (RestoreAll().ok || ++retries >= 20)
                DestroyWindow(window);
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        direct.reset();
        session.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, param, data);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if ((argc != 3 && argc != 4) || wcscmp(argv[1], L"--owner") != 0)
        return 2;
    parentWindow = reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 10)));
    if (!IsWindow(parentWindow))
        return 2;
    GetWindowThreadProcessId(parentWindow, &parentPid);
    parentProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parentProcess)
        return 3;
    WNDCLASSW cls{};
    cls.lpfnWndProc = Procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = styler::HostClass;
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Window appearance host", 0, 0, 0, 0, 0, nullptr, nullptr,
                                  cls.hInstance, nullptr);
    if (!window) {
        CloseHandle(parentProcess);
        return 4;
    }
    if (argc == 4 && wcscmp(argv[3], L"--visual-only") == 0)
        session = std::make_unique<styler::VisualSession>(window);
    else
        direct = std::make_unique<styler::InlineSession>(window);
    SetTimer(window, 1, 250, nullptr);                          // lease watchdog only in direct mode
    SetPropW(window, styler::HostParentProperty, parentWindow); // publish only after initialization
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(parentProcess);
    return 0;
}
