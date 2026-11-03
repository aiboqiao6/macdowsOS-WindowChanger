#include "../../session.h"
#include "research-placement.h"
#include <filesystem>
#include <fstream>
#include <iostream>

void PumpOfficeHost(DWORD milliseconds) {
    auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return ERROR_INVALID_PARAMETER;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t *end{};
    auto number = wcstoull(argv[1], &end, 0);
    if (!end || *end)
        return ERROR_INVALID_PARAMETER;
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(number));
    if (!IsWindow(target))
        return ERROR_INVALID_WINDOW_HANDLE;
    ResearchPlacement restore;
    if (!restore.Capture(target))
        return GetLastError();
    ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    PumpOfficeHost(300);
    if (GetForegroundWindow() != target)
        return ERROR_NOT_READY;
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    auto report =
        std::filesystem::path(styler::ExecutableDirectory()) / (L"office-layout-" + std::to_wstring(pid) + L".json");
    auto before = std::filesystem::exists(report) ? std::filesystem::last_write_time(report)
                                                  : std::filesystem::file_time_type::min();
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Bounded Office original-node layout contract", 0, 0, 0, 0, 0,
                               nullptr, nullptr, cls.hInstance, nullptr);
    if (!owner)
        return GetLastError();
    {
        styler::Session session(owner);
        auto result = session.Apply(target, static_cast<styler::Adapter>(271));
        std::cout << "persistentAttach=" << result.ok << '\n';
        if (result.ok || session.Owns(target)) {
            session.RestoreAll();
            DestroyWindow(owner);
            return ERROR_INVALID_STATE;
        }
    }
    DestroyWindow(owner);
    if (!restore.Finish())
        return ERROR_INVALID_STATE;
    if (!std::filesystem::exists(report) || std::filesystem::last_write_time(report) <= before)
        return ERROR_FILE_NOT_FOUND;
    std::ifstream file(report);
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::cout << json << '\n' << report.string() << '\n';
    return json.find("\"failures\":0,") != std::string::npos && json.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
}
