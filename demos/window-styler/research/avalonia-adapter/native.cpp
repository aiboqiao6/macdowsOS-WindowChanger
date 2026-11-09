#include "adapter-api.h"
#include <commctrl.h>
#include <string>

namespace {
using Invoke = int(__cdecl *)(const wchar_t *, int);
constexpr UINT_PTR SubclassId = 0x4d414356;
struct Lease {
    HWND window{}, owner{};
    HANDLE process{};
    UINT_PTR adapter{};
    bool restoring{};
};
std::wstring Directory() {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&Directory), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring file(path);
    return file.substr(0, file.find_last_of(L'\\') + 1);
}
Invoke Entry() {
    static Invoke entry{};
    if (entry) return entry;
    if (!GetModuleHandleW(L"coreclr.dll")) return nullptr;
    auto fxr = GetModuleHandleW(L"hostfxr.dll");
    if (!fxr) return nullptr;
    using Init = int(__cdecl *)(const wchar_t *, const void *, void **);
    using Get = int(__cdecl *)(void *, int, void **);
    using Close = int(__cdecl *)(void *);
    using Load = int(__cdecl *)(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *, void *, void **);
    auto init = reinterpret_cast<Init>(GetProcAddress(fxr, "hostfxr_initialize_for_runtime_config"));
    auto get = reinterpret_cast<Get>(GetProcAddress(fxr, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<Close>(GetProcAddress(fxr, "hostfxr_close"));
    if (!init || !get || !close) return nullptr;
    auto path = Directory();
    auto diagnostic = [&](const wchar_t *stage, int code) {
        if (GetFileAttributesW((path + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES) return;
        auto value = std::wstring(stage) + L" " + std::to_wstring(code);
        auto file = CreateFileW((path + L"avalonia-native.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) { DWORD count{}; WriteFile(file, value.data(), static_cast<DWORD>(value.size() * sizeof(wchar_t)), &count, nullptr); CloseHandle(file); }
    };
    auto config = path + STYLER_AVALONIA_BRIDGE L".runtimeconfig.json";
    void *context{};
    auto initialized = init(config.c_str(), nullptr, &context);
    if (initialized < 0 || !context) { diagnostic(L"initialize", initialized); return nullptr; }
    void *loader{};
    auto result = get(context, 5, &loader);
    close(context);
    if (result < 0 || !loader) { diagnostic(L"delegate", result); return nullptr; }
    auto assembly = path + STYLER_AVALONIA_BRIDGE L".dll";
    auto type = L"Macdows.AvaloniaBridge.Entry, " STYLER_AVALONIA_BRIDGE;
    void *callback{};
    auto loaded = reinterpret_cast<Load>(loader)(assembly.c_str(), type, L"CoreCall", reinterpret_cast<const wchar_t *>(-1), nullptr, &callback);
    if (loaded < 0) { diagnostic(L"load", loaded); return nullptr; }
    entry = reinterpret_cast<Invoke>(callback);
    return entry;
}
int Call(const Lease &lease, const wchar_t *operation, WPARAM metric = 0, LPARAM button = 0) {
    auto invoke = Entry();
    if (!invoke) return ERROR_NOT_SUPPORTED;
    auto value = std::to_wstring(reinterpret_cast<UINT_PTR>(lease.window)) + L"|" +
                 std::to_wstring(reinterpret_cast<UINT_PTR>(lease.owner)) + L"|" + std::to_wstring(lease.adapter) +
                 L"|" + operation + L"|" + std::to_wstring(metric) + L"|" + std::to_wstring(button) + L"|" +
                 Directory() + STYLER_AVALONIA_TREE;
    return invoke(value.c_str(), static_cast<int>(value.size()));
}
LRESULT CALLBACK Hook(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void Restore(Lease *lease) {
    if (lease->restoring) return;
    lease->restoring = true;
    Call(*lease, L"detach");
    KillTimer(lease->window, reinterpret_cast<UINT_PTR>(lease));
    RemovePropW(lease->window, styler::OwnerProperty);
    RemovePropW(lease->window, styler::InlineProperty);
    RemovePropW(lease->window, styler::AdapterProperty);
    RemoveWindowSubclass(lease->window, Hook, SubclassId);
    CloseHandle(lease->process);
    delete lease;
}
bool OriginalButton(WPARAM hit) { return hit == HTCLOSE || hit == HTMINBUTTON || hit == HTMAXBUTTON; }
LRESULT CALLBACK Hook(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *lease = reinterpret_cast<Lease *>(data);
    if (message == styler::QueryMessage()) return lease->restoring ? styler::TransitionBusy : styler::Acknowledged;
    if (message == styler::DetachMessage()) {
        if (lease->restoring) return styler::TransitionBusy;
        if (w && reinterpret_cast<HWND>(w) != lease->owner) return 0;
        Restore(lease);
        return styler::Acknowledged;
    }
    if (message == styler::DiagnosticMessage())
        return w == static_cast<WPARAM>(styler::Metric::AdapterMode) ? lease->adapter : Call(*lease, L"metric", w, l);
    if (message == WM_TIMER && w == reinterpret_cast<UINT_PTR>(lease)) {
        if (!IsWindow(lease->owner) || WaitForSingleObject(lease->process, 0) != WAIT_TIMEOUT ||
            Call(*lease, L"validate") != ERROR_SUCCESS) Restore(lease);
        return 0;
    }
    if (message == WM_NCHITTEST) {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        ScreenToClient(window, &point);
        auto left = Call(*lease, L"metric", 7), top = Call(*lease, L"metric", 8);
        if (point.x >= left && point.x < left + Call(*lease, L"metric", 9) &&
            point.y >= top && point.y < top + Call(*lease, L"metric", 10)) return HTCLIENT;
        auto hit = DefSubclassProc(window, message, w, l);
        return OriginalButton(hit) ? HTCLIENT : hit;
    }
    if ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK ||
         message == WM_NCRBUTTONDOWN || message == WM_NCRBUTTONUP) && OriginalButton(w)) return 0;
    if (message == WM_NCDESTROY) {
        Restore(lease);
        return DefSubclassProc(window, message, w, l);
    }
    return DefSubclassProc(window, message, w, l);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c)) return ERROR_INVALID_PARAMETER;
    return Call(Lease{c->window, c->owner, nullptr, c->adapterId}, L"probe");
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c)) return ERROR_INVALID_PARAMETER;
    DWORD_PTR previous{};
    if (GetWindowSubclass(c->window, Hook, SubclassId, &previous)) return ERROR_ALREADY_EXISTS;
    auto *lease = new Lease{c->window, c->owner, nullptr, c->adapterId};
    DWORD pid{};
    GetWindowThreadProcessId(c->owner, &pid);
    lease->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!lease->process) { delete lease; return ERROR_ACCESS_DENIED; }
    if (!SetWindowSubclass(c->window, Hook, SubclassId, reinterpret_cast<DWORD_PTR>(lease))) {
        CloseHandle(lease->process); delete lease; return ERROR_ACCESS_DENIED;
    }
    auto result = Call(*lease, L"attach");
    if (result != ERROR_SUCCESS) { Restore(lease); return result; }
    if (!SetPropW(c->window, styler::OwnerProperty, c->owner) ||
        !SetPropW(c->window, styler::InlineProperty, reinterpret_cast<HANDLE>(21)) ||
        !SetPropW(c->window, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId)) ||
        !SetTimer(c->window, reinterpret_cast<UINT_PTR>(lease), 500, nullptr)) {
        Restore(lease); return ERROR_ACCESS_DENIED;
    }
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                      reinterpret_cast<LPCWSTR>(&Hook), &self);
    return ERROR_SUCCESS;
}
