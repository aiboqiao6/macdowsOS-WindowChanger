#include "session.h"
#include <iostream>
namespace {
HWND found{};
DWORD targetPid{};
bool Find() {
    EnumWindows(
        [](HWND w, LPARAM) -> BOOL {
            DWORD pid{};
            GetWindowThreadProcessId(w, &pid);
            if (pid == targetPid && GetPropW(w, styler::SampleProperty)) {
                found = w;
                return FALSE;
            }
            return TRUE;
        },
        0);
    return found != nullptr;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    HWND owner =
        CreateWindowExW(0, cls.lpszClassName, L"ABI test", 0, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
    std::wstring line = L"\"" + std::wstring(argv[1]) + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(argv[1], line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &child))
        return 3;
    CloseHandle(child.hThread);
    targetPid = child.dwProcessId;
    for (int i = 0; i < 200 && !Find(); ++i)
        Sleep(20);
    if (found) {
        for (int i = 0; i < 100 && !GetPropW(found, styler::SamplePaintProperty); ++i)
            Sleep(10);
        Sleep(150);
    }
    bool ok = found != nullptr;
    if (ok) {
        RECT before{}, after{};
        GetClientRect(found, &before);
        styler::Session session(owner);
        auto result = session.Apply(found, static_cast<styler::Adapter>(257));
        if (!result.ok)
            std::wcout << result.message << std::endl;
        ok = result.ok && GetPropW(found, styler::AdapterProperty) == reinterpret_cast<HANDLE>(UINT_PTR(257));
        GetClientRect(found, &after);
        ok = ok && EqualRect(&before, &after) && session.Restore(found).ok && !GetPropW(found, styler::OwnerProperty);
    }
    if (ok) {
        RECT before{}, after{};
        GetClientRect(found, &before);
        styler::Session session(owner);
        auto result = session.Apply(found, styler::Adapter::Automatic);
        if (!result.ok)
            std::wcout << result.message << std::endl;
        ok = result.ok && GetPropW(found, styler::AdapterProperty) == reinterpret_cast<HANDLE>(UINT_PTR(257));
        GetClientRect(found, &after);
        ok = ok && EqualRect(&before, &after) && session.Restore(found).ok && !GetPropW(found, styler::OwnerProperty);
    }
    if (ok) {
        auto file = styler::ExecutableDirectory() + L"\\adapters.ini";
        WritePrivateProfileStringW(L"Adapters", L"Ids", L"513", file.c_str());
        RECT before{}, after{};
        GetClientRect(found, &before);
        styler::Session session(owner);
        auto result = session.Apply(found, styler::Adapter::Automatic);
        GetClientRect(found, &after);
        ok = !result.ok && EqualRect(&before, &after) && !GetPropW(found, styler::OwnerProperty) &&
             !GetPropW(found, styler::AdapterProperty);
        WritePrivateProfileStringW(L"Adapters", L"Ids", L"513,257", file.c_str());
    }
    if (found)
        PostMessageW(found, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(child.hProcess, 4000) != WAIT_OBJECT_0)
        TerminateProcess(child.hProcess, 99);
    CloseHandle(child.hProcess);
    DestroyWindow(owner);
    std::cout << (ok ? "PASS " : "FAIL ")
              << "explicit and automatic C ABI dispatch; rejection chain; no native fallback; original layout restored"
              << std::endl;
    return ok ? 0 : 1;
}
