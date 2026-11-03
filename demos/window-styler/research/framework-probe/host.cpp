#include "../../session.h"
#include "research-placement.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    if (argc != 2 && !(argc == 3 && !wcscmp(argv[2], L"--active"))) {
        std::wcerr << L"Usage: FrameworkResearchHost <TXGui HWND>\n";
        return ERROR_INVALID_PARAMETER;
    }
    wchar_t *end{};
    auto value = wcstoull(argv[1], &end, 0);
    if (!end || *end)
        return ERROR_INVALID_PARAMETER;
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(value));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ResearchPlacement placement;
    wchar_t name[128]{};
    GetClassNameW(target, name, 128);
#ifdef OFFICE_READONLY
#ifdef OFFICE_TRANSACTIONS
    constexpr UINT_PTR adapter = 270;
    constexpr auto reportPrefix = L"office-transactions-";
#else
    constexpr UINT_PTR adapter = 269;
    constexpr auto reportPrefix = L"office-readonly-";
#endif
    bool accepted{};
    EnumChildWindows(
        target,
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(window, name, 128);
            if (!wcscmp(name, L"NetUIHWND"))
                *reinterpret_cast<bool *>(data) = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&accepted));
    if (!accepted)
#else
#ifdef TXGUI_LEFT_EXPERIMENT
    constexpr UINT_PTR adapter = 278;
    constexpr auto reportPrefix = L"txgui-left-";
#elif defined(TXGUI_TITLE_EXPERIMENT)
    constexpr UINT_PTR adapter = 277;
    constexpr auto reportPrefix = L"txgui-title-";
#elif defined(TXGUI_ORDER_EXPERIMENT)
    constexpr UINT_PTR adapter = 276;
    constexpr auto reportPrefix = L"txgui-order-";
#elif defined(TXGUI_D2D_DRAWING)
    constexpr UINT_PTR adapter = 275;
    constexpr auto reportPrefix = L"txgui-d2d-drawing-";
#elif defined(TXGUI_PAINT_OBSERVER)
    constexpr UINT_PTR adapter = 274;
    constexpr auto reportPrefix = L"txgui-paint-observation-";
#elif defined(TXGUI_ACTIONS)
    constexpr UINT_PTR adapter = 273;
    constexpr auto reportPrefix = L"txgui-actions-";
#else
    constexpr UINT_PTR adapter = 267;
    constexpr auto reportPrefix = L"txgui-readonly-";
#endif
    if (wcscmp(name, L"TXGuiFoundation"))
#endif
        return ERROR_NOT_SUPPORTED;
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    bool activate = argc == 3;
#ifdef TXGUI_ACTIONS
    activate = true;
#endif
#if defined(TXGUI_PAINT_OBSERVER) || defined(TXGUI_D2D_DRAWING) || defined(TXGUI_ORDER_EXPERIMENT) ||                  \
    defined(TXGUI_TITLE_EXPERIMENT) || defined(TXGUI_LEFT_EXPERIMENT)
    activate = true;
#endif
    if (activate) {
        if (!placement.Capture(target))
            return GetLastError();
        ShowWindow(target, SW_RESTORE);
#ifdef TXGUI_ACTIONS
        SetForegroundWindow(target);
#endif
        Sleep(250);
    }
    auto report =
        std::filesystem::path(styler::ExecutableDirectory()) / (reportPrefix + std::to_wstring(pid) + L".json");
    auto previousWrite = std::filesystem::exists(report) ? std::filesystem::last_write_time(report)
                                                         : std::filesystem::file_time_type::min();
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Read-only framework research", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return static_cast<int>(GetLastError());
    {
        styler::Session session(owner);
        auto result = session.Apply(target, static_cast<styler::Adapter>(adapter));
        // A research probe must decline Attach; discovery is recorded separately.
        auto length = WideCharToMultiByte(CP_UTF8, 0, result.message.data(), static_cast<int>(result.message.size()),
                                          nullptr, 0, nullptr, nullptr);
        std::string message(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, result.message.data(), static_cast<int>(result.message.size()), message.data(),
                            length, nullptr, nullptr);
        std::cout << "Attach accepted: " << result.ok << "; " << message << '\n';
        if (result.ok || session.Owns(target)) {
            session.RestoreAll();
            DestroyWindow(owner);
            return ERROR_INVALID_STATE;
        }
    }
    DestroyWindow(owner);
    std::cout << report.string() << '\n';
    if (!std::filesystem::exists(report) || std::filesystem::last_write_time(report) <= previousWrite)
        return ERROR_FILE_NOT_FOUND;
    if (!placement.Finish())
        return ERROR_INVALID_STATE;
#if defined(OFFICE_TRANSACTIONS) || defined(TXGUI_ACTIONS) || defined(TXGUI_PAINT_OBSERVER) ||                         \
    defined(TXGUI_D2D_DRAWING) || defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT) || defined(TXGUI_LEFT_EXPERIMENT)
    std::ifstream file(report);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
#ifdef TXGUI_LEFT_EXPERIMENT
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"layoutModified\":true,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#elif defined(TXGUI_TITLE_EXPERIMENT)
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"titleExpanded\":true,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#elif defined(TXGUI_ORDER_EXPERIMENT)
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"orderModified\":true,") != std::string::npos &&
                   text.find("\"duringStatePreserved\":true,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#elif defined(TXGUI_D2D_DRAWING)
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"syntheticNotifications\":false,") != std::string::npos &&
                   text.find("\"pixelsWritten\":true,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#elif defined(TXGUI_PAINT_OBSERVER)
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"syntheticNotifications\":false,") != std::string::npos &&
                   text.find("\"pixelsWritten\":false,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#elif defined(TXGUI_ACTIONS)
    std::cout << text << '\n';
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"maximizeInput\":true,") != std::string::npos &&
                   text.find("\"restoreInput\":true,") != std::string::npos &&
                   text.find("\"minimizeInput\":true,") != std::string::npos &&
                   text.find("\"restored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#else
    return text.find("\"failures\":0,") != std::string::npos &&
                   text.find("\"propertiesWritten\":12,") != std::string::npos &&
                   text.find("\"localValuesRestored\":true,") != std::string::npos
               ? 0
               : ERROR_INVALID_DATA;
#endif
#else
    return 0;
#endif
}
