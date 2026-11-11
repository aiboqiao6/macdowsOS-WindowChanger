// Exercise title closure on an owned background window. The bootstrap mode
// injects only into this test process. No global input, screenshots or changes
// to an existing application are used.
#include <windows.h>
BOOL WINAPI ShowOwnedWithoutActivation(HWND window, int command) {
    if (command == SW_SHOW)
        SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return ShowWindow(window, command == SW_SHOW ? SW_SHOWNOACTIVATE : command);
}
#define ShowWindow ShowOwnedWithoutActivation
#define wWinMain OwnedFixtureMain
#include "native-island-fixture.cpp"
#undef wWinMain
#undef ShowWindow
#include "../../adapter-api.h"
#include "../../pe-identity.h"
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

namespace {
UINT_PTR timer{};
ULONGLONG started{};
unsigned stage{}, checks{};
bool checking{};
std::array<LONG, 10> geometry{};
LONG_PTR style{};
HWND originalIsland{};
bool production{}, bootstrap{};
HWND owner{};
HANDLE host{};
HANDLE hostRead{};
DWORD hostPid{};
std::string hostOutput;
styler::AdapterContextV1 context;
styler::AdapterAttachV1 attach{};
x::Hosting::DesktopWindowXamlSource closedTitle{nullptr};
winrt::Microsoft::UI::Content::DesktopChildSiteBridge closedBridge{nullptr};

void Check(bool value, const char *name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    if (!value)
        throw std::runtime_error(name);
    ++checks;
}
std::array<LONG, 10> Geometry(HWND window) {
    RECT outer{}, client{};
    POINT origin{};
    winrt::check_bool(GetWindowRect(window, &outer));
    winrt::check_bool(GetClientRect(window, &client));
    winrt::check_bool(ClientToScreen(window, &origin));
    return {outer.left, outer.top,    outer.right,   outer.bottom, client.left,
            client.top, client.right, client.bottom, origin.x,     origin.y};
}
std::wstring Directory() {
    wchar_t executable[32768]{};
    winrt::check_bool(GetModuleFileNameW(nullptr, executable, 32768) != 0);
    std::wstring directory(executable);
    directory.resize(directory.find_last_of(L'\\') + 1);
    return directory;
}
void StartHost(HWND window) {
    auto executable = Directory() + L"WinUI3NativeIslandHost.exe";
    auto command = L"\"" + executable + L"\" " + std::to_wstring(reinterpret_cast<UINT_PTR>(window)) + L" 10";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE write{};
    winrt::check_bool(CreatePipe(&hostRead, &write, &security, 0));
    winrt::check_bool(SetHandleInformation(hostRead, HANDLE_FLAG_INHERIT, 0));
    auto input =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    winrt::check_bool(input != INVALID_HANDLE_VALUE);
    SIZE_T size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<BYTE> attributes(size);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = write;
    startup.StartupInfo.hStdInput = input;
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    winrt::check_bool(InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size));
    HANDLE inherited[]{write, input};
    winrt::check_bool(UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                inherited, sizeof(inherited), nullptr, nullptr));
    PROCESS_INFORMATION process{};
    auto created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                  &startup.StartupInfo, &process);
    auto error = GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(write);
    CloseHandle(input);
    if (!created)
        winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    CloseHandle(process.hThread);
    host = process.hProcess;
    hostPid = process.dwProcessId;
    hostOutput.clear();
    Check(true, "bounded external host starts against only this owned HWND");
}
void ReadHost() {
    DWORD available{};
    while (hostRead && PeekNamedPipe(hostRead, nullptr, 0, nullptr, &available, nullptr) && available) {
        char buffer[1024]{};
        DWORD bytes{};
        winrt::check_bool(ReadFile(hostRead, buffer, std::min<DWORD>(available, sizeof(buffer)), &bytes, nullptr));
        if (!bytes)
            break;
        hostOutput.append(buffer, bytes);
        std::cout << "host-output: " << std::string(buffer, bytes);
    }
}
bool HostReady() {
    if (!bootstrap)
        return true;
    ReadHost();
    auto line = [](const char *text) {
        return hostOutput.find(std::string(text) + "\n") != std::string::npos ||
               hostOutput.find(std::string(text) + "\r\n") != std::string::npos;
    };
    if (line("attached=1") && line("adapter=288"))
        return true;
    if (WaitForSingleObject(host, 0) == WAIT_OBJECT_0) {
        DWORD code{};
        GetExitCodeProcess(host, &code);
        std::cout << "host-exit-before-ready=" << code << '\n';
        throw std::runtime_error("external host exited before completing Apply");
    }
    return false;
}
bool HostExited() {
    if (!host)
        return true;
    auto wait = WaitForSingleObject(host, 0);
    if (wait == WAIT_TIMEOUT)
        return false;
    DWORD code{};
    winrt::check_bool(wait == WAIT_OBJECT_0 && GetExitCodeProcess(host, &code));
    ReadHost();
    std::cout << "host-exit=" << code << '\n';
    Check(code == 0, "external automatic host exits successfully after rollback");
    CloseHandle(host);
    CloseHandle(hostRead);
    host = nullptr;
    hostRead = nullptr;
    owner = nullptr;
    return true;
}
void Restored(App *app) {
    Check(!app->native, "transaction is released");
    if (production)
        Check(!GetPropW(app->hwnd, styler::OwnerProperty) && !GetPropW(app->hwnd, styler::AdapterProperty) &&
                  !GetPropW(app->hwnd, styler::InlineProperty),
              "production lease properties are removed");
    Check(app->source.Content() == app->body, "original content remains installed");
    Check(!app->source.SiteBridge().IsClosed() &&
              app->source.SiteBridge().WindowId().Value == reinterpret_cast<uint64_t>(originalIsland),
          "original island remains open and retains its HWND");
    Check(Geometry(app->hwnd) == geometry, "outer and client geometry are restored");
    RECT island{};
    winrt::check_bool(GetWindowRect(originalIsland, &island));
    Check(island.left == geometry[8] && island.top == geometry[9] && island.right - island.left == geometry[6] &&
              island.bottom - island.top == geometry[7],
          "original content viewport is restored");
    Check(GetWindowLongPtrW(app->hwnd, GWL_STYLE) == style, "original style is restored");
    auto window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(app->hwnd)});
    auto presenter = window.Presenter().as<w::OverlappedPresenter>();
    Check(presenter.HasTitleBar() && presenter.HasBorder() && !window.TitleBar().ExtendsContentIntoTitleBar(),
          "native caption and border are restored");
    Check(app->document && app->document.Text() == L"Original document" && app->documentEvents->loads == 1 &&
              app->documentEvents->unloads == 0,
          "original TextBox remains loaded with its document");
}
void AttachModule(HWND window) {
    if (!attach) {
        auto directory = Directory();
        wchar_t file[512]{};
        winrt::check_bool(GetPrivateProfileStringW(L"Adapter288", L"File", L"", file, 512,
                                                   (directory + L"adapters.ini").c_str()) != 0);
        auto module = LoadLibraryExW((directory + file).c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        winrt::check_bool(module != nullptr);
        auto probe = reinterpret_cast<styler::AdapterProbeV1>(GetProcAddress(module, "MacdowsAdapterProbeV1"));
        attach = reinterpret_cast<styler::AdapterAttachV1>(GetProcAddress(module, "MacdowsAdapterAttachV1"));
        Check(probe && attach, "registered production module exposes both adapter entry points");
        owner = CreateWindowW(L"STATIC", L"Owned background adapter contract", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
        winrt::check_bool(owner != nullptr);
        context.window = window;
        context.owner = owner;
        context.adapterId = 288;
        Check(probe(&context) == ERROR_SUCCESS && !GetPropW(window, styler::OwnerProperty) &&
                  Geometry(window) == geometry && GetWindowLongPtrW(window, GWL_STYLE) == style,
              "production probe accepts the original island without modifying it");
    }
    Check(attach(&context) == ERROR_SUCCESS, "production module attaches through its public adapter ABI");
}
x::Hosting::DesktopWindowXamlSource FindTitle(HWND window) {
    // Only obtain the source needed to simulate the host's public Close().
    // Its ABI identity and entry bytes must match the production root reader.
    auto module = GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
    constexpr GUID id{0x4acdbe85, 0xd668, 0x192e, {0xb8, 0xb6, 0xa9, 0x33, 0x4a, 0xc9, 0x1b, 0x79}};
    Check(styler::MatchesPdb(module, id), "owned runtime matches the verified source discovery ABI");
    constexpr BYTE bytes[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55,
                           0x57, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x40};
    auto address = reinterpret_cast<BYTE *>(module) + 0x731804;
    Check(memcmp(address, bytes, sizeof(bytes)) == 0, "source discovery entry bytes match");
    std::vector<Microsoft::WRL::ComPtr<::IInspectable>> values;
    winrt::check_hresult(reinterpret_cast<HRESULT (*)(decltype(values) &)>(address)(values));
    x::Hosting::DesktopWindowXamlSource title{nullptr};
    unsigned count{};
    for (auto const &value : values) {
        winrt::Windows::Foundation::IInspectable object{nullptr};
        winrt::copy_from_abi(object, value.Get());
        if (auto source = object.try_as<x::Hosting::DesktopWindowXamlSource>()) {
            auto bridge = source.SiteBridge();
            if (!bridge.IsClosed() && bridge.WindowId().Value != reinterpret_cast<uint64_t>(originalIsland) &&
                GetParent(reinterpret_cast<HWND>(bridge.WindowId().Value)) == window) {
                title = source;
                ++count;
            }
        }
    }
    Check(count == 1, "exactly one independent production title source exists");
    return title;
}
bool Ready(App *app) {
    // Loaded target controls can acknowledge Query while the external
    // Session::Apply is still on its stack. Do not close them until the
    // bounded host has confirmed both successful Apply and Adapter288.
    return HostReady() && (production ? SendMessageW(app->hwnd, styler::QueryMessage(), 0, 0) == styler::Acknowledged
                                      : app->native && app->native->InputReady());
}
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
    if (checking)
        return;
    checking = true;
    try {
        if (GetTickCount64() - started > 15000)
            throw std::runtime_error("owned lifecycle test timed out");
        HWND target{};
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM result) -> BOOL {
                wchar_t name[128]{};
                GetClassNameW(window, name, 128);
                if (wcscmp(name, L"Macdows.Research.NativeIslandFixture"))
                    return TRUE;
                *reinterpret_cast<HWND *>(result) = window;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&target));
        auto app = target ? reinterpret_cast<App *>(GetWindowLongPtrW(target, GWLP_USERDATA)) : nullptr;
        if (app && stage == 0 && app->body.IsLoaded() && app->document && app->document.IsLoaded()) {
            Check(GetForegroundWindow() != target, "owned fixture starts without activation");
            geometry = Geometry(target);
            style = GetWindowLongPtrW(target, GWL_STYLE);
            originalIsland = reinterpret_cast<HWND>(app->source.SiteBridge().WindowId().Value);
            if (bootstrap)
                StartHost(target);
            else if (production)
                AttachModule(target);
            else
                app->Apply();
            stage = 1;
        } else if (app && stage == 1 && Ready(app) && production) {
            if (bootstrap) {
                owner = reinterpret_cast<HWND>(GetPropW(target, styler::OwnerProperty));
                DWORD pid{};
                Check(owner && GetWindowThreadProcessId(owner, &pid) && pid == hostPid,
                      "production lease belongs to the owned external host process");
            }
            Check(GetPropW(target, styler::OwnerProperty) == owner &&
                      reinterpret_cast<UINT_PTR>(GetPropW(target, styler::AdapterProperty)) == 288,
                  "production lease reports the expected owner and adapter");
            closedTitle = FindTitle(target);
            closedBridge = closedTitle.SiteBridge();
            closedTitle.Close();
            Check(closedBridge.IsClosed(), "host closes the production title through its public source API");
            Check(SendMessageW(target, styler::QueryMessage(), 0, 0) == 0,
                  "production query safely rejects the closed title");
            stage = 2;
        } else if (app && stage == 1 && Ready(app)) {
            Check(app->native->OwnsSource(), "loaded title is initially owned");
            auto bridge = app->native->titleBridge;
            Check(bridge && !bridge.IsClosed(), "title bridge is retained and open");
            app->native->titleSource.Close();
            Check(bridge.IsClosed(), "host closes the title through the public source API");
            stage = 2;
        } else if (app && stage == 2 && production && !GetPropW(target, styler::OwnerProperty) && HostExited()) {
            Restored(app);
            closedTitle = nullptr;
            closedBridge = nullptr;
            if (bootstrap)
                StartHost(target);
            else
                AttachModule(target);
            stage = 3;
        } else if (app && stage == 2 && !production) {
            // The old implementation blocks in Content() here after Close().
            Check(!app->native->OwnsSource(), "closed title is rejected without reading its Content");
            Check(!app->native->InputReady(), "closed title cannot acknowledge input readiness");
            HRESULT resizeError{};
            try {
                app->native->Resize();
            } catch (winrt::hresult_error const &error) {
                resizeError = error.code();
            }
            Check(resizeError == RO_E_CLOSED && app->native->invalid, "resize rejects a closed title bridge");
            app->Restore();
            Restored(app);
            app->Apply();
            stage = 3;
        } else if (app && stage == 3 && Ready(app)) {
            if (bootstrap) {
                owner = reinterpret_cast<HWND>(GetPropW(target, styler::OwnerProperty));
                DWORD pid{};
                Check(owner && GetWindowThreadProcessId(owner, &pid) && pid == hostPid,
                      "replacement lease belongs to the second owned external host");
            }
            if (production)
                Check(SendMessageW(target, styler::DetachMessage(), reinterpret_cast<WPARAM>(owner), 0) ==
                          styler::Acknowledged,
                      "production module explicitly detaches after successful reattachment");
            else {
                Check(app->native->OwnsSource(), "a new independent title can attach after recovery");
                app->Restore();
            }
            Restored(app);
            stage = 4;
        } else if (app && stage == 4 && HostExited()) {
            Check(GetForegroundWindow() != target, "lifecycle test requires no foreground activation");
            KillTimer(nullptr, timer);
            if (owner) {
                DestroyWindow(owner);
                owner = nullptr;
            }
            stage = 5;
            PostMessageW(target, WM_CLOSE, 0, 0);
        }
    } catch (std::exception const &error) {
        std::cout << "ERROR " << error.what() << '\n';
        ExitProcess(ERROR_INVALID_STATE);
    } catch (...) {
        std::cout << "ERROR HRESULT=" << std::hex << winrt::to_hresult() << '\n';
        ExitProcess(ERROR_INVALID_STATE);
    }
    checking = false;
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << std::unitbuf;
    bootstrap = wcsstr(GetCommandLineW(), L"--bootstrap") != nullptr;
    production = bootstrap || wcsstr(GetCommandLineW(), L"--production") != nullptr;
    started = GetTickCount64();
    timer = SetTimer(nullptr, 0, 80, Tick);
    if (!timer)
        return GetLastError();
    auto result = OwnedFixtureMain(instance, previous, command, show);
    KillTimer(nullptr, timer);
    std::cout << "closed-title checks=" << checks << " completed=" << (stage == 5) << '\n';
    return result ? result : stage == 5 ? 0 : ERROR_INVALID_STATE;
}
