// Bounded research adapter. The product registry never references this DLL.
#ifdef WINUI3_NATIVE_ISLAND_DLL
#define MacdowsAdapterProbeV1 ProductWinUI3FixtureProbe
#define MacdowsAdapterAttachV1 ProductWinUI3FixtureAttach
#include "../../winui3-inline.cpp"
#undef MacdowsAdapterProbeV1
#undef MacdowsAdapterAttachV1
#include "native-island-frame.h"
#include <filesystem>
#include <fstream>

namespace {
using NativeFrame = styler::research::winui::NativeIslandFrame;
constexpr UINT_PTR ResearchSubclass = 0x4e495352;
struct NativeLease {
    HWND target{}, owner{};
    HANDLE process{};
    UINT_PTR timer{};
    bool attaching{true}, releaseRequested{}, releasing{}, destroyed{};
    HRESULT restoreError{};
    std::unique_ptr<NativeFrame> frame;
};
void NativeRecoveryLog(NativeLease *s, HRESULT error) noexcept {
    try {
        HMODULE module{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&NativeRecoveryLog), &module);
        wchar_t path[32768]{};
        GetModuleFileNameW(module, path, 32768);
        auto file = L"native-island-recovery-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(reinterpret_cast<UINT_PTR>(s->target)) + L".txt";
        std::ofstream log(std::filesystem::path(path).parent_path() / file, std::ios::app);
        log << "stage=" << s->frame->restoreStage << " HRESULT=" << std::hex << error << '\n';
    } catch (...) {
    }
}
LRESULT CALLBACK NativeProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
NativeLease *NativeLive(HWND h) {
    DWORD_PTR value{};
    return GetWindowSubclass(h, NativeProcedure, ResearchSubclass, &value) ? reinterpret_cast<NativeLease *>(value)
                                                                           : nullptr;
}
x::Hosting::DesktopWindowXamlSource NativeSource(HWND h) {
    if (IsIconic(h) || IsZoomed(h) || GetDpiForWindow(h) != 144 || !IsWindowEnabled(h) || GetMenu(h) ||
        (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
        (GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) != WS_CAPTION ||
        GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(h)) != DPI_AWARENESS_PER_MONITOR_AWARE)
        return nullptr;
    RECT client{};
    POINT origin{};
    GetClientRect(h, &client);
    ClientToScreen(h, &origin);
    x::Hosting::DesktopWindowXamlSource result{nullptr};
    unsigned matches{};
    for (auto const &source : Roots())
        if (auto root = source.Content();
            root && root.XamlRoot().ContentIslandEnvironment().AppWindowId().Value == reinterpret_cast<uint64_t>(h)) {
            ++matches;
            auto element = root.try_as<x::FrameworkElement>();
            auto window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(h)});
            auto presenter = window.Presenter().try_as<w::OverlappedPresenter>();
            auto island = reinterpret_cast<HWND>(source.SiteBridge().WindowId().Value);
            RECT actual{}, expected{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
            if (!element || !element.IsLoaded() || element.FlowDirection() != x::FlowDirection::LeftToRight ||
                element.GetBindingExpression(c::Grid::RowProperty()) || !presenter || !presenter.HasBorder() ||
                !presenter.HasTitleBar() || window.TitleBar().ExtendsContentIntoTitleBar() || GetParent(island) != h ||
                !GetWindowRect(island, &actual) || !EqualRect(&actual, &expected) ||
                std::abs(element.ActualWidth() * root.XamlRoot().RasterizationScale() - client.right) > 1 ||
                std::abs(element.ActualHeight() * root.XamlRoot().RasterizationScale() - client.bottom) > 1)
                return nullptr;
            result = source;
        }
    return matches == 1 ? result : nullptr;
}
bool NativeRelease(NativeLease *s, bool destroying = false) {
    // Public XAML operations can pump messages. Never delete the frame while
    // Apply is on the stack, including a nested WM_NCDESTROY.
    if (s->attaching) {
        s->releaseRequested = true;
        s->destroyed |= destroying;
        return false;
    }
    if (s->releasing) {
        s->destroyed |= destroying;
        return false;
    }
    s->releasing = true;
    if (!destroying && s->frame)
        try {
            s->frame->Restore();
        } catch (...) {
            const auto error = winrt::to_hresult();
            if (s->restoreError != error) {
                s->restoreError = error;
                NativeRecoveryLog(s, error);
            }
            // Keep the timer, frame and bootstrap lease alive for retry. A
            // failed rollback must not be acknowledged or silently forgotten.
            if (!s->destroyed) {
                s->frame->invalid = true;
                s->releasing = false;
                return false;
            }
        }
    RemoveWindowSubclass(s->target, NativeProcedure, ResearchSubclass);
    if (s->timer)
        KillTimer(s->target, s->timer);
    RemovePropW(s->target, styler::OwnerProperty);
    RemovePropW(s->target, styler::InlineProperty);
    RemovePropW(s->target, styler::AdapterProperty);
    if (s->process)
        CloseHandle(s->process);
    delete s;
    return true;
}
LRESULT CALLBACK NativeProcedure(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto s = reinterpret_cast<NativeLease *>(data);
    if (m == styler::QueryMessage())
        return !s->attaching && !s->releasing && s->frame->active && !s->frame->changing && !s->frame->invalid &&
                       s->frame->OwnsSource()
                   ? styler::Acknowledged
                   : 0;
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(wp) != s->owner)
            return 0;
        return NativeRelease(s) ? styler::Acknowledged : 0;
    }
    if (m == WM_NCDESTROY) {
        NativeRelease(s, true);
        return DefSubclassProc(h, m, wp, lp);
    }
    if (m == WM_TIMER && wp == s->timer) {
        if (s->attaching || s->releasing)
            return 0;
        try {
            if (!IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT || s->frame->invalid ||
                !s->frame->OwnsSource())
                NativeRelease(s);
            else
                s->frame->UpdateCapabilities();
        } catch (...) {
            NativeRelease(s);
        }
        return 0;
    }
    if (m == WM_GETTITLEBARINFOEX && lp) {
        auto result = DefSubclassProc(h, m, wp, lp);
        auto info = reinterpret_cast<TITLEBARINFOEX *>(lp);
        if (info->cbSize == sizeof(*info))
            for (auto i : {2, 3, 5}) {
                info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                SetRectEmpty(&info->rgrect[i]);
            }
        return result;
    }
    auto result = DefSubclassProc(h, m, wp, lp);
    if (NativeLive(h) == s && !s->attaching && !s->releasing && !s->frame->changing &&
        (m == WM_SIZE || m == WM_THEMECHANGED || m == WM_DPICHANGED))
        try {
            if (GetDpiForWindow(h) != 144 || !s->frame->OwnsSource())
                NativeRelease(s);
            else {
                s->frame->Resize();
                s->frame->UpdateTheme();
            }
        } catch (...) {
            if (NativeLive(h) == s)
                NativeRelease(s);
        }
    return result;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId())
        return ERROR_INVALID_PARAMETER;
    try {
        return NativeSource(context->window) ? 0 : ERROR_NOT_SUPPORTED;
    } catch (...) {
        return ERROR_NOT_SUPPORTED;
    }
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (auto error = MacdowsAdapterProbeV1(context))
        return error;
    auto s = std::make_unique<NativeLease>();
    s->target = context->window;
    s->owner = context->owner;
    DWORD pid{};
    GetWindowThreadProcessId(s->owner, &pid);
    s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->process)
        return ERROR_ACCESS_DENIED;
    auto state = s.release();
    try {
        state->frame = std::make_unique<NativeFrame>(state->target, NativeSource(state->target), false, true);
        if (!SetWindowSubclass(state->target, NativeProcedure, ResearchSubclass, reinterpret_cast<DWORD_PTR>(state)))
            winrt::throw_last_error();
        state->timer = SetTimer(state->target, reinterpret_cast<UINT_PTR>(state), 500, nullptr);
        if (!state->timer)
            winrt::throw_last_error();
        state->frame->Apply();
        if (state->releaseRequested || state->destroyed || !IsWindow(state->target))
            winrt::throw_hresult(E_ABORT);
        if (!SetPropW(state->target, styler::OwnerProperty, state->owner) ||
            !SetPropW(state->target, styler::AdapterProperty, reinterpret_cast<HANDLE>(context->adapterId)) ||
            !SetPropW(state->target, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(4))))
            winrt::throw_hresult(E_OUTOFMEMORY);
        state->attaching = false;
        return 0;
    } catch (...) {
        state->attaching = false;
        NativeRelease(state, state->destroyed);
        return ERROR_NOT_SUPPORTED;
    }
}
#else
#include "../../session.h"
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return ERROR_INVALID_PARAMETER;
    wchar_t *end{};
    auto value = wcstoull(argv[1], &end, 0);
    if (!value || !end || *end)
        return ERROR_INVALID_PARAMETER;
    auto h = reinterpret_cast<HWND>(value);
    auto duration = wcstoul(argv[2], &end, 10);
    if (!end || *end || duration < 3 || duration > 30 || !IsWindow(h))
        return ERROR_INVALID_PARAMETER;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Bounded native-island research", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return GetLastError();
    int exit = 0;
    {
        styler::Session session(owner);
        auto result = session.Apply(h, static_cast<styler::Adapter>(288));
        std::cout << "attached=" << result.ok << '\n';
        if (!result.ok)
            exit = ERROR_NOT_SUPPORTED;
        else {
            const auto deadline = GetTickCount64() + duration * 1000;
            while (GetTickCount64() < deadline && IsWindow(h)) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
                MSG m{};
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&m);
                    DispatchMessageW(&m);
                }
            }
            auto restored = session.RestoreAll();
            std::cout << "restored=" << restored.ok << '\n';
            if (!restored.ok)
                exit = ERROR_INVALID_STATE;
        }
    }
    DestroyWindow(owner);
    return exit;
}
#endif
