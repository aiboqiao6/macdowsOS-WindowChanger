#pragma once
#include <xamlOM.h>
#include <ocidl.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <fstream>
#include <sstream>
#include "xaml-abi.h"

// Public diagnostics enumerate the existing sources; never inspect a host's
// private userdata, executable identity, object offsets or control class names.
namespace xaml_roots {
namespace x = winrt::Windows::UI::Xaml;
namespace h = x::Hosting;
inline constexpr GUID TapId{0xb45c321a, 0x19f6, 0x49e9, {0x97, 0xa3, 0x66, 0x52, 0x8a, 0xdd, 0x70, 0x21}};
inline std::mutex mutex;
inline std::vector<winrt::weak_ref<h::DesktopWindowXamlSource>> sources;
inline std::atomic_bool starting{};
inline std::atomic<ULONGLONG> startedAt{};
inline std::atomic<HRESULT> initResult{E_PENDING}, adviseResult{E_PENDING}, sourceResult{E_PENDING};
inline std::atomic<unsigned> events{}, sourceEvents{};
inline std::mutex logMutex;
inline std::atomic<unsigned> loggedEvents{};
inline std::wstring Directory() {
    HMODULE m{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Directory), &m);
    wchar_t p[32768]{};
    GetModuleFileNameW(m, p, 32768);
    std::wstring s(p);
    s.resize(s.find_last_of(L'\\') + 1);
    return s;
}
inline void Debug() {
    auto directory = Directory();
    if (GetFileAttributesW((directory + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::ofstream log(directory + L"xaml-root-status-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    log << std::hex << initResult.load() << " " << adviseResult.load() << " " << sourceResult.load()
        << " events=" << std::dec << events.load() << " sources=" << sourceEvents.load();
}
inline void Log(std::string const &message) {
    auto directory = Directory();
    if (GetFileAttributesW((directory + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::scoped_lock lock(logMutex);
    std::ofstream log(directory + L"xaml-tree-" + std::to_wstring(GetCurrentProcessId()) + L".txt", std::ios::app);
    log << message << "\n";
}
inline bool Track(winrt::Windows::Foundation::IInspectable const &object) {
    if (auto source = object.try_as<h::DesktopWindowXamlSource>()) {
        HWND child{};
        winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&child));
        {
            std::scoped_lock lock(mutex);
            sources.push_back(winrt::make_weak(source));
        }
        ++sourceEvents;
        sourceResult = S_OK;
        std::ostringstream line;
        line << "source thread=" << GetCurrentThreadId() << " hwnd=" << child;
        Log(line.str());
        return true;
    }
    return false;
}
struct Tap : winrt::implements<Tap, IObjectWithSite, IVisualTreeServiceCallback> {
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    HRESULT __stdcall SetSite(IUnknown *site) noexcept override {
        try {
            if (site) {
                winrt::check_hresult(site->QueryInterface(diagnostics.put()));
                auto self = get_strong();
                std::thread([self] {
                    try {
                        adviseResult = self->diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(self.get());
                    } catch (...) {
                        adviseResult = winrt::to_hresult();
                    }
                    Debug();
                }).detach();
            }
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }
    HRESULT __stdcall GetSite(REFIID id, void **result) noexcept override {
        return diagnostics ? diagnostics->QueryInterface(id, result) : E_NOINTERFACE;
    }
    HRESULT __stdcall OnVisualTreeChange(ParentChildRelation relation, VisualElement e,
                                         VisualMutationType kind) noexcept override {
        ++events;
        try {
            if (kind == Add) {
                winrt::Windows::Foundation::IInspectable object{nullptr};
                winrt::check_hresult(diagnostics->GetIInspectableFromHandle(
                    e.Handle, reinterpret_cast<::IInspectable **>(winrt::put_abi(object))));
                Track(object);
                if (loggedEvents.fetch_add(1) < 3000) {
                    std::ostringstream line;
                    line << "add thread=" << GetCurrentThreadId() << " handle=" << e.Handle
                         << " parent=" << relation.Parent << " type=" << (e.Type ? winrt::to_string(e.Type) : "")
                         << " name=" << (e.Name ? winrt::to_string(e.Name) : "");
                    if (auto element = object.try_as<x::FrameworkElement>()) {
                        try {
                            auto root = element.XamlRoot();
                            line << " xamlRoot=" << winrt::get_abi(root);
                            if (root)
                                line << " content=" << winrt::to_string(winrt::get_class_name(root.Content()));
                        } catch (...) {
                            line << " rootHr=" << std::hex << winrt::to_hresult();
                        }
                    }
                    Log(line.str());
                }
            }
        } catch (...) {
            sourceResult = winrt::to_hresult();
            std::ostringstream line;
            line << "callbackHr=" << std::hex << sourceResult.load();
            Log(line.str());
        }
        // The diagnostics implementation transfers these BSTRs to the callback.
        if (kind == Add) {
            SysFreeString(e.Name);
            SysFreeString(e.Type);
            SysFreeString(e.SrcInfo.FileName);
            SysFreeString(e.SrcInfo.Hash);
        }
        return S_OK;
    }
};
struct Factory : winrt::implements<Factory, IClassFactory> {
    HRESULT __stdcall CreateInstance(IUnknown *outer, REFIID id, void **result) noexcept override {
        try {
            if (outer)
                return CLASS_E_NOAGGREGATION;
            return winrt::make_self<Tap>()->QueryInterface(id, result);
        } catch (...) {
            return winrt::to_hresult();
        }
    }
    HRESULT __stdcall LockServer(BOOL) noexcept override {
        return S_OK;
    }
};
inline void Start() {
    if (starting.exchange(true))
        return;
    startedAt = GetTickCount64();
    HMODULE pinned{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&Start), &pinned);
    wchar_t path[32768]{};
    GetModuleFileNameW(pinned, path, 32768);
    std::wstring file(path);
    std::thread([file] {
        try {
            auto runtime = GetModuleHandleW(L"Windows.UI.Xaml.dll");
            if (!runtime)
                return;
            auto init = reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(
                GetProcAddress(runtime, "InitializeXamlDiagnosticsEx"));
            if (!init)
                return;
            for (int i = 1; i <= 12; i++) {
                auto endpoint = L"VisualDiagConnection" + std::to_wstring(i);
                auto result = init(endpoint.c_str(), GetCurrentProcessId(), nullptr, file.c_str(), TapId, nullptr);
                initResult = result;
                if (SUCCEEDED(result))
                    break;
            }
        } catch (...) {
            initResult = winrt::to_hresult();
        }
        Debug();
    }).detach();
}
struct Content {
    x::UIElement element{nullptr};
    HWND bridge{};
};
inline std::vector<Content> Contents(HWND target) {
    std::vector<Content> result;
    auto append = [&](h::DesktopWindowXamlSource const &source) {
        HWND child{};
        winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&child));
        if (!child || GetAncestor(child, GA_ROOT) != target ||
            GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId())
            return;
        for (auto const &content : result)
            if (content.bridge == child)
                return;
        if (auto content = source.Content())
            result.push_back({content, child});
    };
    xaml_abi::Objects objects;
    if (xaml_abi::Enumerate(objects))
        for (auto const &object : objects)
            try {
                winrt::Windows::Foundation::IInspectable inspectable{nullptr};
                winrt::copy_from_abi(inspectable, object.Get());
                if (auto source = inspectable.try_as<h::DesktopWindowXamlSource>())
                    append(source);
            } catch (...) {
                sourceResult = winrt::to_hresult();
            }
    std::vector<winrt::weak_ref<h::DesktopWindowXamlSource>> snapshot;
    {
        std::scoped_lock lock(mutex);
        snapshot = sources;
    }
    for (auto &weak : snapshot)
        try {
            if (auto source = weak.get()) {
                append(source);
            }
        } catch (...) {
            sourceResult = winrt::to_hresult();
        }
    Debug();
    return result;
}
} // namespace xaml_roots
STDAPI DllGetClassObject(REFCLSID id, REFIID iid, void **result) {
    try {
        if (id != xaml_roots::TapId)
            return CLASS_E_CLASSNOTAVAILABLE;
        return winrt::make_self<xaml_roots::Factory>()->QueryInterface(iid, result);
    } catch (...) {
        return winrt::to_hresult();
    }
}
