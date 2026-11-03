#include "controller-settings.h"
#include "protocol.h"
#include <commctrl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <functional>

namespace {
void Pump(DWORD ms) {
    auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(15);
    } while (GetTickCount64() < end);
}
bool Until(const std::function<bool()> &fn, DWORD timeout = 10000) {
    auto end = GetTickCount64() + timeout;
    do {
        if (fn())
            return true;
        Pump(50);
    } while (GetTickCount64() < end);
    return fn();
}
LRESULT Send(HWND w, UINT m, WPARAM wp = 0, LPARAM lp = 0) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(w, m, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 4000, &result)
               ? static_cast<LRESULT>(result)
               : -1;
}
UINT Metrics() {
    return RegisterWindowMessageW(L"Macdows.WindowChanger.Test.v3");
}
bool SaveOwnedWindow(HWND window, const std::filesystem::path &path) {
    RECT rect{};
    if (!GetClientRect(window, &rect))
        return false;
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width < 1 || width > 2048 || height < 1 || height > 1536)
        return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    auto dc = CreateCompatibleDC(nullptr);
    void *bits{};
    auto bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    auto old = bitmap ? SelectObject(dc, bitmap) : nullptr;
    bool ok = old && PrintWindow(window, dc, PW_CLIENTONLY);
    if (ok) {
        GdiFlush();
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
        auto size = static_cast<DWORD>(width * height * 4);
        file.bfSize = file.bfOffBits + size;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(&file), sizeof(file));
        out.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
        out.write(static_cast<const char *>(bits), size);
        ok = out.good();
    }
    if (old)
        SelectObject(dc, old);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    return ok;
}
struct Process {
    PROCESS_INFORMATION info{};
    bool Start(const std::wstring &exe, const std::wstring &args, int show = SW_SHOWNORMAL) {
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = static_cast<WORD>(show);
        auto command = L"\"" + exe + L"\" " + args;
        return CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                              &info) != FALSE;
    }
    bool Done() const {
        return info.hProcess && WaitForSingleObject(info.hProcess, 0) == WAIT_OBJECT_0;
    }
    void Close() {
        if (info.hThread)
            CloseHandle(info.hThread);
        if (info.hProcess)
            CloseHandle(info.hProcess);
        info = {};
    }
    ~Process() {
        Close();
    }
};
std::vector<HWND> OwnedWindows(DWORD pid, const wchar_t *property) {
    struct Find {
        DWORD pid;
        const wchar_t *property;
        std::vector<HWND> result;
    } find{pid, property, {}};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            auto &f = *reinterpret_cast<Find *>(data);
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            if (pid == f.pid && GetPropW(window, f.property))
                f.result.push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&find));
    return find.result;
}
HWND Controller(DWORD pid) {
    auto found = OwnedWindows(pid, L"Macdows.WindowChanger.Primary.v3");
    return found.empty() ? nullptr : found.front();
}
bool Attached(HWND window) {
    return GetPropW(window, styler::OwnerProperty) != nullptr;
}
void Exit(Process &process) {
    if (!process.info.hProcess || process.Done())
        return;
    if (auto window = Controller(process.info.dwProcessId))
        Send(window, Metrics(), 9);
    if (!Until([&] { return process.Done(); }, 4000)) {
        // This is only a process created by this test, never a user app.
        TerminateProcess(process.info.hProcess, 0x54455354);
        WaitForSingleObject(process.info.hProcess, 3000);
    }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return ERROR_INVALID_PARAMETER;
    auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto exe = std::filesystem::absolute(argv[1]).wstring(), fixtureExe = std::filesystem::absolute(argv[2]).wstring();
    auto output = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(output);
    auto initialForeground = GetForegroundWindow();
    styler::settings::Scope scope{L"ui-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(GetTickCount64())};
    auto realAuto = styler::settings::LoadAutoManage({});
    auto realStartup = styler::settings::QueryStartup({}, exe);
    Process fixture, ui, second;
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *name) {
        ++checks;
        failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
        return ok;
    };
    std::vector<HWND> targets;
    auto args = L"--test-mode " + scope.testId;
    bool setup = check(fixture.Start(fixtureExe, L"--pair"), "start only an owned pair of native fixture windows");
    if (setup)
        setup = check(Until([&] {
                          targets = OwnedWindows(fixture.info.dwProcessId, styler::SampleProperty);
                          return targets.size() == 2;
                      }),
                      "owned native fixtures expose both HWNDs");
    if (setup) {
        args += L" --scope-pid " + std::to_wstring(fixture.info.dwProcessId);
        ShowWindow(targets[1], SW_MINIMIZE);
        Pump(200);
        setup = check(ui.Start(exe, args + L" --background"),
                      "start isolated background controller for only the owned fixture PID");
    }
    HWND window{};
    if (setup)
        setup = check(Until([&] {
                          window = Controller(ui.info.dwProcessId);
                          return window != nullptr;
                      }),
                      "controller publishes its primary HWND");
    if (setup) {
        check(!IsWindowVisible(window) && Send(window, Metrics(), 5) == 1,
              "background start has a real registered tray icon and no visible main window");
        check(Send(window, Metrics(), 15) == 1,
              "elevated controller admits only Explorer tray and taskbar-recreated callbacks across UIPI");
        check(Send(window, Metrics(), 6) == 1, "automatic management is enabled on first run");
        check(Until([&] { return Attached(targets[0]); }),
              "automatic management attaches a stable supported original window");
        check(!Attached(targets[1]), "initially minimized window is deferred without activating it");
        ShowWindow(targets[1], SW_RESTORE);
        check(Until([&] { return Attached(targets[1]); }),
              "restored original window is automatically discovered and attached");
        check(Send(window, Metrics(), 8) == 2 &&
                  static_cast<DWORD>(Send(window, Metrics(), 14)) == fixture.info.dwProcessId,
              "automatic scope and both owned leases are confirmed");
        DWORD ownerPid{}, controllerType{}, fixtureType{};
        GetWindowThreadProcessId(reinterpret_cast<HWND>(GetPropW(targets[0], styler::OwnerProperty)), &ownerPid);
        auto ownerProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ownerPid);
        wchar_t helperPath[32768]{};
        DWORD helperLength = 32768;
        bool helper = ownerProcess && QueryFullProcessImageNameW(ownerProcess, 0, helperPath, &helperLength) &&
                      GetBinaryTypeW(exe.c_str(), &controllerType) && GetBinaryTypeW(fixtureExe.c_str(), &fixtureType);
        auto expectedHelper = std::filesystem::weakly_canonical(std::filesystem::path(exe).parent_path());
        if (controllerType != fixtureType)
            expectedHelper /= fixtureType == SCS_32BIT_BINARY ? L"x86" : L"x64";
        expectedHelper /= L"WindowStyleHost.exe";
        check(helper && !_wcsicmp(helperPath, expectedHelper.c_str()),
              "automatic capture uses the matching helper from the tested runtime directory");
        if (ownerProcess)
            CloseHandle(ownerProcess);
        check(!GetDlgItem(window, 101) && !GetDlgItem(window, 106) && !GetDlgItem(window, 107),
              "sample launch and screenshot audit controls are absent from the production UI");
        check(GetDlgItem(window, 102) && GetDlgItem(window, 103) && GetDlgItem(window, 104) &&
                  GetDlgItem(window, 105) && GetDlgItem(window, 108) && GetDlgItem(window, 109),
              "essential controls and two setting switches are present");
        Send(window, Metrics(), 2);
        check(IsWindowVisible(window), "tray action opens the main window");
        check(SaveOwnedWindow(window, output / L"controller.bmp"),
              "render only the scoped controller and owned fixture list for visual verification");
        RECT originalRect{};
        GetWindowRect(window, &originalRect);
        auto dpi = GetDpiForWindow(window);
        SetWindowPos(window, nullptr, 0, 0, MulDiv(740, dpi, 96), MulDiv(440, dpi, 96),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT client{};
        GetClientRect(window, &client);
        bool fits = true;
        for (int id : {100, 102, 103, 104, 105, 108, 109}) {
            RECT rect{};
            fits = GetWindowRect(GetDlgItem(window, id), &rect) && fits;
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&rect), 2);
            fits =
                fits && rect.left >= 0 && rect.top >= 0 && rect.right <= client.right && rect.bottom <= client.bottom;
        }
        check(fits, "all essential controls fit the minimum supported controller window size");
        SetWindowPos(window, nullptr, 0, 0, originalRect.right - originalRect.left,
                     originalRect.bottom - originalRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        Send(window, WM_CLOSE);
        check(!IsWindowVisible(window) && !ui.Done() && Attached(targets[0]),
              "closing main window keeps the controller and leases alive in the tray");
        auto trayAdds = Send(window, Metrics(), 10);
        Send(window, Metrics(), 4);
        check(Send(window, Metrics(), 5) == 1 && Send(window, Metrics(), 10) == trayAdds + 1,
              "tray registration is restored after taskbar reconstruction");
        check(second.Start(exe, args), "start a second invocation of the same isolated instance");
        check(Until([&] { return second.Done() && IsWindowVisible(window) && !ui.Done(); }),
              "single-instance second launch exits and reveals the existing window");
        Send(window, WM_COMMAND, 104);
        check(Until([&] { return !Attached(targets[0]) || !Attached(targets[1]); }),
              "manual selected restore detaches one owned window");
        auto detached = !Attached(targets[0]) ? targets[0] : targets[1];
        Pump(1700);
        check(!Attached(detached), "automatic discovery respects manual per-window exclusion");
        Send(window, WM_COMMAND, 105);
        check(Until([&] { return !Attached(targets[0]) && !Attached(targets[1]); }) && Send(window, Metrics(), 6) == 0,
              "restore all pauses automatic mode and restores both leases");
        check(!styler::settings::LoadAutoManage(scope), "paused automatic setting is persisted");
        Send(window, WM_COMMAND, 108);
        check(Until([&] { return Attached(targets[0]) && Attached(targets[1]); }),
              "resuming automatic mode clears exclusions and reapplies supported windows");
        Send(window, WM_COMMAND, 109);
        auto startup = styler::settings::QueryStartup(scope, exe);
        check(SUCCEEDED(startup.result) && startup.exists && startup.enabled && startup.owned && startup.currentPath &&
                  Send(window, Metrics(), 12) == 0,
              "startup toggle registers a current-user highest-privilege background task");
        if (startup.exists) {
            using Microsoft::WRL::ComPtr;
            ComPtr<ITaskService> service;
            ComPtr<ITaskFolder> folder;
            ComPtr<IRegisteredTask> task;
            ComPtr<ITaskDefinition> definition;
            ComPtr<IPrincipal> principal;
            ComPtr<ITriggerCollection> triggers;
            ComPtr<ITrigger> trigger;
            VARIANT empty{};
            BSTR root = SysAllocString(L"\\"), name = SysAllocString(scope.TaskName().c_str());
            auto result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
            if (SUCCEEDED(result))
                result = service->Connect(empty, empty, empty, empty);
            if (SUCCEEDED(result))
                result = service->GetFolder(root, &folder);
            if (SUCCEEDED(result))
                result = folder->GetTask(name, &task);
            if (SUCCEEDED(result))
                result = task->get_Definition(&definition);
            if (SUCCEEDED(result))
                result = definition->get_Principal(&principal);
            TASK_RUNLEVEL_TYPE level{};
            TASK_LOGON_TYPE logon{};
            TASK_TRIGGER_TYPE2 type{};
            LONG count{};
            if (SUCCEEDED(result))
                result = principal->get_RunLevel(&level);
            if (SUCCEEDED(result))
                result = principal->get_LogonType(&logon);
            if (SUCCEEDED(result))
                result = definition->get_Triggers(&triggers);
            if (SUCCEEDED(result))
                result = triggers->get_Count(&count);
            if (SUCCEEDED(result))
                result = triggers->get_Item(1, &trigger);
            if (SUCCEEDED(result))
                result = trigger->get_Type(&type);
            check(SUCCEEDED(result) && level == TASK_RUNLEVEL_HIGHEST && logon == TASK_LOGON_INTERACTIVE_TOKEN &&
                      count == 1 && type == TASK_TRIGGER_LOGON,
                  "registered task uses logon trigger interactive token and highest privilege without a password");
            // Stop this isolated instance; the actual scheduler must launch it.
            Exit(ui);
            ui.Close();
            ComPtr<IRunningTask> running;
            result = task ? task->Run(empty, &running) : E_FAIL;
            auto instanceProperty = L"Macdows.WindowChanger.Instance." + scope.TaskName();
            HWND scheduled{};
            check(SUCCEEDED(result) && Until([&] {
                      struct Find {
                          const wchar_t *property;
                          HWND found{};
                      } find{instanceProperty.c_str()};
                      EnumWindows(
                          [](HWND w, LPARAM p) -> BOOL {
                              auto &f = *reinterpret_cast<Find *>(p);
                              if (GetPropW(w, f.property)) {
                                  f.found = w;
                                  return FALSE;
                              }
                              return TRUE;
                          },
                          reinterpret_cast<LPARAM>(&find));
                      scheduled = find.found;
                      return scheduled != nullptr;
                  }),
                  "Task Scheduler actually launches the isolated background controller");
            if (scheduled) {
                check(!IsWindowVisible(scheduled) && Send(scheduled, Metrics(), 5) == 1 &&
                          Send(scheduled, Metrics(), 14) == 0,
                      "scheduled launch is tray-only and cannot manage unrelated PIDs in test mode");
                Send(scheduled, Metrics(), 9);
                check(Until([&] { return !IsWindow(scheduled); }),
                      "scheduled test controller exits and removes its tray icon");
            }
            SysFreeString(root);
            SysFreeString(name);
            if (running)
                running->Stop();
            check(SUCCEEDED(styler::settings::SetStartup(scope, exe, false)) &&
                      !styler::settings::QueryStartup(scope, exe).exists,
                  "disable startup removes only the isolated registered task");
            // A conflicting task in this fixture's unique namespace must not
            // be overwritten/deleted just because its name matches ours.
            ComPtr<IRegistrationInfo> foreignInfo;
            auto conflict = definition->get_RegistrationInfo(&foreignInfo);
            BSTR foreign = SysAllocString(L"Owned test conflict, not a WindowChanger task");
            if (SUCCEEDED(conflict))
                conflict = foreignInfo->put_Description(foreign);
            BSTR taskName = SysAllocString(scope.TaskName().c_str());
            ComPtr<IRegisteredTask> foreignTask;
            if (SUCCEEDED(conflict))
                conflict = folder->RegisterTaskDefinition(taskName, definition.Get(), TASK_CREATE_OR_UPDATE, empty,
                                                          empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &foreignTask);
            auto refused = SUCCEEDED(conflict) && styler::settings::SetStartup(scope, exe, true) == E_ACCESSDENIED &&
                           styler::settings::SetStartup(scope, exe, false) == E_ACCESSDENIED;
            check(refused, "unrecognized conflicting task is not overwritten or deleted");
            if (SUCCEEDED(conflict))
                check(SUCCEEDED(folder->DeleteTask(taskName, 0)),
                      "remove only the foreign-marker task created by this test");
            SysFreeString(foreign);
            SysFreeString(taskName);
        }
        if (!ui.info.hProcess) {
            check(ui.Start(exe, args + L" --background"),
                  "restart the controller after startup execution verification");
            check(Until([&] {
                      window = Controller(ui.info.dwProcessId);
                      return window != nullptr;
                  }),
                  "restarted isolated controller publishes its HWND");
        }
        Send(window, WM_COMMAND, 105);
        check(Until([&] { return !Attached(targets[0]) && !Attached(targets[1]); }),
              "restart verification begins with no fixture leases");
        Exit(ui);
        ui.Close();
        check(ui.Start(exe, args + L" --background") && Until([&] {
                  window = Controller(ui.info.dwProcessId);
                  return window != nullptr;
              }),
              "controller restarts from saved per-user settings");
        Pump(1200);
        check(Send(window, Metrics(), 6) == 0 && !Attached(targets[0]) && !Attached(targets[1]),
              "saved paused mode survives restart and performs no automatic attach");
        Send(window, WM_COMMAND, 108);
        check(Until([&] { return Attached(targets[0]) && Attached(targets[1]); }),
              "automatic mode can be reenabled after restart");
        TerminateProcess(ui.info.hProcess, 0x54455354);
        WaitForSingleObject(ui.info.hProcess, 3000);
        check(Until([&] { return !Attached(targets[0]) && !Attached(targets[1]); }),
              "controller crash restores only its own fixture leases");
        check(IsWindow(targets[0]) && IsWindow(targets[1]), "fixture windows remain alive after controller crash");
    }
    Exit(second);
    Exit(ui);
    if (fixture.info.hProcess && !fixture.Done()) {
        for (auto w : OwnedWindows(fixture.info.dwProcessId, styler::SampleProperty))
            PostMessageW(w, WM_CLOSE, 0, 0);
        if (!Until([&] { return fixture.Done(); }, 4000)) {
            TerminateProcess(fixture.info.hProcess, 0x54455354);
            WaitForSingleObject(fixture.info.hProcess, 3000);
        }
    }
    check(SUCCEEDED(styler::settings::RemoveTestSettings(scope)),
          "remove all fixture-created startup and preference settings");
    auto afterStartup = styler::settings::QueryStartup({}, exe);
    bool realPreferencesPreserved =
        realAuto == styler::settings::LoadAutoManage({}) && realStartup.exists == afterStartup.exists &&
        realStartup.enabled == afterStartup.enabled && realStartup.executable == afterStartup.executable;
    check(realPreferencesPreserved, "real user startup and automatic preferences remain unchanged");
    if (IsWindow(initialForeground) && !IsIconic(initialForeground))
        SetForegroundWindow(initialForeground);
    std::ofstream report(output / L"result.json");
    report << "{\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"realPreferencesPreserved\":" << (realPreferencesPreserved ? "true" : "false")
           << ",\"testScopeOnly\":true}\n";
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return failures ? 1 : 0;
}
