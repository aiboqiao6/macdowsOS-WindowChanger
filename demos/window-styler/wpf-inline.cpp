#include "adapter-api.h"
#include <metahost.h>
#include <string>
#pragma comment(lib, "mscoree.lib")

namespace {
DWORD Execute(const styler::AdapterContextV1 *c, const wchar_t *method) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Execute), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring file(path);
    file.resize(file.find_last_of(L'\\') + 1);
    if (GetModuleHandleW(L"coreclr.dll")) {
        auto fxr = GetModuleHandleW(L"hostfxr.dll");
        if (!fxr)
            return ERROR_NOT_SUPPORTED;
        using Init = int(__cdecl *)(const wchar_t *, const void *, void **);
        using Delegate = int(__cdecl *)(void *, int, void **);
        using Close = int(__cdecl *)(void *);
        using Load =
            int(__cdecl *)(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *, void *, void **);
        using Invoke = int(__cdecl *)(const wchar_t *, int);
        auto init = reinterpret_cast<Init>(GetProcAddress(fxr, "hostfxr_initialize_for_runtime_config"));
        auto get = reinterpret_cast<Delegate>(GetProcAddress(fxr, "hostfxr_get_runtime_delegate"));
        auto close = reinterpret_cast<Close>(GetProcAddress(fxr, "hostfxr_close"));
        if (!init || !get || !close)
            return ERROR_PROC_NOT_FOUND;
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring config(exe);
        auto dot = config.find_last_of(L'.');
        config.resize(dot);
        config += L".runtimeconfig.json";
        void *context{};
        int status = init(config.c_str(), nullptr, &context);
        if (status < 0 || !context)
            return ERROR_NOT_SUPPORTED;
        void *loader{};
        status = get(context, 5, &loader);
        close(context);
        if (status < 0 || !loader)
            return ERROR_NOT_SUPPORTED;
        auto assembly = file + STYLER_WPF_CORE;
        std::wstring type = L"Macdows.WpfInline.Entry, " STYLER_WPF_CORE_NAME;
        std::wstring coreMethod = L"Core";
        coreMethod += method;
        void *entry{};
        status = reinterpret_cast<Load>(loader)(assembly.c_str(), type.c_str(), coreMethod.c_str(),
                                                reinterpret_cast<const wchar_t *>(-1), nullptr, &entry);
        if (status < 0 || !entry)
            return static_cast<DWORD>(status);
        std::wstring argument = std::to_wstring(reinterpret_cast<UINT_PTR>(c->window)) + L"|" +
                                std::to_wstring(reinterpret_cast<UINT_PTR>(c->owner)) + L"|" +
                                std::to_wstring(c->adapterId);
        return reinterpret_cast<Invoke>(entry)(argument.c_str(), static_cast<int>(argument.size()));
    }
    if (!GetModuleHandleW(L"clr.dll"))
        return ERROR_NOT_SUPPORTED;
    file += STYLER_WPF_MANAGED;
    ICLRMetaHost *meta{};
    ICLRRuntimeInfo *runtime{};
    ICLRRuntimeHost *host{};
    HRESULT hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&meta));
    if (SUCCEEDED(hr))
        hr = meta->GetRuntime(L"v4.0.30319", IID_PPV_ARGS(&runtime));
    BOOL loaded = FALSE;
    if (SUCCEEDED(hr))
        hr = runtime->IsLoaded(GetCurrentProcess(), &loaded);
    if (SUCCEEDED(hr) && !loaded)
        hr = E_NOTIMPL;
    if (SUCCEEDED(hr))
        hr = runtime->GetInterface(CLSID_CLRRuntimeHost, IID_PPV_ARGS(&host));
    std::wstring argument = std::to_wstring(reinterpret_cast<UINT_PTR>(c->window)) + L"|" +
                            std::to_wstring(reinterpret_cast<UINT_PTR>(c->owner)) + L"|" +
                            std::to_wstring(c->adapterId);
    DWORD result = ERROR_NOT_SUPPORTED;
    if (SUCCEEDED(hr))
        hr = host->ExecuteInDefaultAppDomain(file.c_str(), L"Macdows.WpfInline.Entry", method, argument.c_str(),
                                             &result);
    if (host)
        host->Release();
    if (runtime)
        runtime->Release();
    if (meta)
        meta->Release();
    return FAILED(hr) ? static_cast<DWORD>(hr) : result;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    return Execute(c, L"Probe");
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    return Execute(c, L"Attach");
}
