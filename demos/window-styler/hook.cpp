#include "chrome.h"
#include "adapter-registry.h"
#include "embedded-frame.h"
#include <cstring>

namespace {
DWORD AttachAdapter(HWND window, HWND owner, UINT_PTR id) {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&AttachAdapter), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring directory(path);
    auto slash = directory.find_last_of(L"\\");
    if (slash == std::wstring::npos)
        return ERROR_PATH_NOT_FOUND;
    directory.resize(slash);
    auto registrations = styler::ReadAdapterRegistry(directory);
    const bool automatic = id == static_cast<UINT_PTR>(styler::Adapter::Automatic);
    DWORD last = ERROR_NOT_SUPPORTED;
    for (const auto &r : registrations) {
        if ((!automatic && r.id != id) || !r.Matches(window))
            continue;
        if (!r.RuntimeAvailable()) {
            if (!automatic)
                return ERROR_MOD_NOT_FOUND;
            continue;
        }
        // Optional runtime guard happens before loading a toolkit plugin.
        if (!r.qtVersion.empty()) {
            using Version = const char *(*)();
            // Use the exact core named by the registration, including its
            // debug suffix. Never load another Qt runtime into the host.
            HMODULE core{};
            size_t start{};
            while (start < r.requiredModules.size()) {
                auto end = r.requiredModules.find(L',', start);
                if (end == std::wstring::npos)
                    end = r.requiredModules.size();
                auto name = r.requiredModules.substr(start, end - start);
                if (_wcsicmp(name.c_str(), L"Qt6Core.dll") == 0 || _wcsicmp(name.c_str(), L"Qt6Cored.dll") == 0 ||
                    _wcsicmp(name.c_str(), L"Qt5Core.dll") == 0 || _wcsicmp(name.c_str(), L"Qt5Cored.dll") == 0) {
                    core = GetModuleHandleW(name.c_str());
                    break;
                }
                start = end + 1;
            }
            if (!core) {
                if (automatic)
                    continue;
                return ERROR_MOD_NOT_FOUND;
            }
            auto version = reinterpret_cast<Version>(GetProcAddress(core, "qVersion"));
            if (!version)
                version = reinterpret_cast<Version>(GetProcAddress(core, "?qVersion@@YAPEBDXZ"));
            std::string expected;
            for (auto ch : r.qtVersion)
                expected.push_back(static_cast<char>(ch));
            if (!version || expected != version()) {
                if (automatic)
                    continue;
                return ERROR_REVISION_MISMATCH;
            }
        }
        auto file = directory + L"\\" + r.file;
        HMODULE plugin =
            LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!plugin) {
            last = GetLastError();
            if (automatic)
                continue;
            return last;
        }
        auto probe = reinterpret_cast<styler::AdapterProbeV1>(GetProcAddress(plugin, "MacdowsAdapterProbeV1"));
        auto attach = reinterpret_cast<styler::AdapterAttachV1>(GetProcAddress(plugin, "MacdowsAdapterAttachV1"));
        if (!probe)
            probe = reinterpret_cast<styler::AdapterProbeV1>(GetProcAddress(plugin, "_MacdowsAdapterProbeV1@4"));
        if (!attach)
            attach = reinterpret_cast<styler::AdapterAttachV1>(GetProcAddress(plugin, "_MacdowsAdapterAttachV1@4"));
        if (!probe || !attach) {
            FreeLibrary(plugin);
            last = ERROR_PROC_NOT_FOUND;
            if (automatic)
                continue;
            return last;
        }
        styler::AdapterContextV1 context;
        context.window = window;
        context.owner = owner;
        context.adapterId = r.id;
        DWORD result = probe(&context);
        if (result != ERROR_SUCCESS) {
            FreeLibrary(plugin);
            last = result;
            if (automatic && result != ERROR_RETRY)
                continue;
            return result;
        }
        // Selection ends at the first successful read-only Probe. An Attach
        // failure must restore its own transaction, never fall into another
        // backend against a partially modified tree.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(attach), &pinned))
            result = GetLastError();
        else
            result = attach(&context);
        FreeLibrary(plugin);
        return result;
    }
    // A candidate toolkit can own caption-sized pixels itself. Failed layout
    // discovery never authorizes painting over those pixels as a native frame.
    return last;
}
} // namespace

// No work in DllMain: the hook runs on the target's GUI thread after loading.
extern "C" __declspec(dllexport) LRESULT CALLBACK MacdowsStyleHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto *event = reinterpret_cast<const CWPSTRUCT *>(lParam);
        if (event->message == styler::ApplyMessage() && event->lParam == styler::Version) {
            HWND controller = reinterpret_cast<HWND>(event->wParam);
            wchar_t className[128]{};
            GetClassNameW(controller, className, 128);
            DWORD targetPid{};
            styler::WindowThreadProcessId(event->hwnd, &targetPid);
            if (targetPid == GetCurrentProcessId() &&
                (wcscmp(className, styler::ControllerClass) == 0 ||
                 wcscmp(className, L"Macdows.WindowStyler.VisualHost.v2") == 0) &&
                GetPropW(event->hwnd, styler::RequestProperty) == controller &&
                (!(GetWindowLongPtrW(event->hwnd, GWL_STYLE) & WS_CHILD) ||
                 (reinterpret_cast<UINT_PTR>(GetPropW(event->hwnd, styler::AdapterProperty)) ==
                      styler::UwpInsetAdapter &&
                  styler::EmbeddedCore(
                      event->hwnd, reinterpret_cast<HWND>(GetPropW(event->hwnd, styler::EmbeddedFrameProperty)))))) {
                // Keep callback code mapped even when Windows removes the hook
                // or the controller crashes. Explicit detach removes all window
                // state, but this small module remains until the target exits.
                HMODULE pinned{};
                DWORD result = ERROR_DLL_INIT_FAILED;
                auto adapter = static_cast<styler::Adapter>(
                    reinterpret_cast<UINT_PTR>(GetPropW(event->hwnd, styler::AdapterProperty)));
                if (adapter != styler::Adapter::Native && adapter != styler::Adapter::Extended &&
                    !styler::FrameworkAdapter(adapter))
                    return CallNextHookEx(nullptr, code, wParam, lParam);
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                       reinterpret_cast<LPCWSTR>(&MacdowsStyleHook), &pinned))
                    result = styler::FrameworkAdapter(adapter)
                                 ? AttachAdapter(event->hwnd, controller, static_cast<UINT_PTR>(adapter))
                                 : styler::chrome::Attach(event->hwnd, controller, adapter);
                // Restore may have canceled a timed-out attach while this
                // callback was still executing. Do not leave a late attachment.
                if (result == ERROR_SUCCESS && GetPropW(event->hwnd, styler::RequestProperty) != controller) {
                    SendMessageW(event->hwnd, styler::DetachMessage(), reinterpret_cast<WPARAM>(controller), 0);
                    result = ERROR_CANCELLED;
                }
                SetPropW(event->hwnd, styler::ResultProperty,
                         reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(result) + 1));
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// A bounded bootstrap for ERROR_MOD_NOT_FOUND from external hook registration.
// This worker never edits the UI: the sent message runs Attach on its original
// GUI thread. The temporary in-process hook is removed before this call exits.
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsThreadBootstrapV1(void *argument) {
    if (!argument)
        return ERROR_INVALID_PARAMETER;
    auto request = *static_cast<const styler::ThreadBootstrapV1 *>(argument);
    if (request.size != sizeof(request) || request.version != styler::Version ||
        GetPropW(request.window, styler::RequestProperty) != request.owner)
        return ERROR_INVALID_PARAMETER;
    DWORD pid{};
    DWORD thread = styler::WindowThreadProcessId(request.window, &pid);
    wchar_t name[128]{};
    GetClassNameW(request.owner, name, 128);
    if (pid != GetCurrentProcessId() || !thread || thread == GetCurrentThreadId() ||
        (wcscmp(name, styler::ControllerClass) && wcscmp(name, L"Macdows.WindowStyler.VisualHost.v2")))
        return ERROR_ACCESS_DENIED;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, MacdowsStyleHook, nullptr, thread);
    if (!hook)
        return GetLastError();
    DWORD_PTR result{};
    bool sent = SendMessageTimeoutW(request.window, styler::ApplyMessage(), reinterpret_cast<WPARAM>(request.owner),
                                    styler::Version, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) != 0;
    DWORD error = sent ? ERROR_SUCCESS : GetLastError();
    UnhookWindowsHookEx(hook);
    return sent ? ERROR_SUCCESS : error ? error : ERROR_TIMEOUT;
}
