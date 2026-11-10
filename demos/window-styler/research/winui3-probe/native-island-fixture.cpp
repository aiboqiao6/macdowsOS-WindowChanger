// Owned native HWND + full-client XAML island. This is a layout experiment,
// never an adapter and never a reason to enable native GDI fallback for WinUI.
#include <windows.h>
#include <appmodel.h>
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <cmath>
#include <fstream>
#include <vector>

namespace x = winrt::Microsoft::UI::Xaml;
namespace c = x::Controls;
namespace w = winrt::Microsoft::UI::Windowing;
namespace input = winrt::Microsoft::UI::Input;
using winrt::Windows::Graphics::RectInt32;
constexpr UINT Command = WM_APP + 73, Metric = WM_APP + 74;
void Failure(const char *stage, HRESULT hr) {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring file(path);
    file.resize(file.find_last_of(L'\\') + 1);
    std::ofstream log(file + L"native-island-error-" + std::to_wstring(GetCurrentProcessId()) + L".txt", std::ios::app);
    log << stage << " HRESULT=" << std::hex << hr << '\n';
}

#include "native-island-frame.h"
#include <memory>

struct App : x::ApplicationT<App> {
    HWND hwnd{};
    x::Hosting::DesktopWindowXamlSource source{nullptr};
    c::Grid body{nullptr};
    std::unique_ptr<styler::research::winui::NativeIslandFrame> native;
    UINT dpi{};
    unsigned generation{};
    void Resize() {
        if (!source || IsIconic(hwnd))
            return;
        if (native && native->active)
            native->Resize();
        else {
            RECT r{};
            GetClientRect(hwnd, &r);
            source.SiteBridge().MoveAndResize({0, 0, r.right, r.bottom});
        }
    }
    void Theme(bool light) {
        body.RequestedTheme(light ? x::ElementTheme::Light : x::ElementTheme::Dark);
        body.Background(x::Media::SolidColorBrush(light ? winrt::Windows::UI::Color{255, 243, 243, 243}
                                                        : winrt::Windows::UI::Color{255, 32, 32, 32}));
    }
    void Apply() {
        if (!native)
            native = std::make_unique<styler::research::winui::NativeIslandFrame>(
                hwnd, source, wcsstr(GetCommandLineW(), L"--preserve-root") != nullptr,
                wcsstr(GetCommandLineW(), L"--title-island") != nullptr);
        native->Apply();
    }
    void Restore() {
        if (native) {
            native->Restore();
            native.reset();
        }
    }
    LRESULT ReadMetric(WPARAM index) {
        RECT r{};
        POINT origin{};
        GetClientRect(hwnd, &r);
        ClientToScreen(reinterpret_cast<HWND>(source.SiteBridge().WindowId().Value), &origin);
        const double scale = double(dpi) / 96;
        auto p = body.TransformToVisual(nullptr).TransformPoint({0, 0});
        switch (index) {
        case 1:
            return native && native->active;
        case 2:
            return native ? native->delta : 0;
        case 3:
            return origin.y + std::lround(p.Y * scale);
        case 4:
            return origin.x + std::lround(p.X * scale);
        case 5:
            return std::lround(body.ActualWidth() * scale);
        case 6:
            return std::lround(body.ActualHeight() * scale);
        case 7:
            return static_cast<int>(body.ActualTheme());
        case 8:
            return native && native->active ? std::lround(native->lights.ActualHeight() * scale) : 0;
        case 9:
            return native && native->active ? native->OwnsSource() : source.Content() == body;
        case 10:
            return w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)})
                .Presenter()
                .as<w::OverlappedPresenter>()
                .HasTitleBar();
        case 11:
            return generation;
        case 12:
            return native && native->active ? static_cast<int>(native->frame.ActualTheme()) : 0;
        case 13:
        case 14: {
            RECT island{};
            GetWindowRect(reinterpret_cast<HWND>(source.SiteBridge().WindowId().Value), &island);
            return index == 13 ? island.right - island.left : island.bottom - island.top;
        }
        default:
            return 0;
        }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        auto self = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd = h;
        }
        if (!self)
            return DefWindowProcW(h, m, wp, lp);
        try {
            if (m == Command) {
                if (wp == 1)
                    self->Apply();
                else if (wp == 2)
                    self->Restore();
                else if (wp == 3)
                    self->Theme(true);
                else if (wp == 4)
                    self->Theme(false);
                else if (wp == 5) {
                    auto light = self->body.ActualTheme() == x::ElementTheme::Light;
                    self->body = c::Grid();
                    self->Theme(light);
                    self->source.Content(self->body);
                    ++self->generation;
                }
                return 0;
            }
            if (m == Metric)
                return self->ReadMetric(wp);
            if (m == WM_SIZE)
                self->Resize();
            if (m == WM_GETTITLEBARINFOEX && (self->native && self->native->active) && lp) {
                auto result = DefWindowProcW(h, m, wp, lp);
                auto info = reinterpret_cast<TITLEBARINFOEX *>(lp);
                if (info->cbSize == sizeof(*info))
                    for (auto i : {2, 3, 5}) {
                        info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                        SetRectEmpty(&info->rgrect[i]);
                    }
                return result;
            }
            if ((self->native && self->native->active) && (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) &&
                (m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP || m == WM_NCLBUTTONDBLCLK))
                return 0;
            if (m == WM_CLOSE) {
                self->Restore();
                self->source.Close();
                self->source = nullptr;
                DestroyWindow(h);
                return 0;
            }
            if (m == WM_DESTROY) {
                self->Exit();
                return 0;
            }
        } catch (...) {
            return static_cast<LRESULT>(winrt::to_hresult());
        }
        return DefWindowProcW(h, m, wp, lp);
    }
    void OnLaunched(x::LaunchActivatedEventArgs const &) {
        try {
            Launch();
        } catch (...) {
            Failure("launch", winrt::to_hresult());
            Exit();
        }
    }
    void Launch() {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = Proc;
        cls.lpszClassName = L"Macdows.Research.NativeIslandFixture";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        auto h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, cls.lpszClassName, L"Owned native XAML island fixture",
                                 WS_OVERLAPPEDWINDOW, 160, 160, 1200, 720, nullptr, nullptr, cls.hInstance, this);
        winrt::check_bool(h != nullptr);
        dpi = GetDpiForWindow(h);
        source = x::Hosting::DesktopWindowXamlSource();
        source.Initialize({reinterpret_cast<uint64_t>(h)});
        body = c::Grid();
        c::RowDefinition toolbarRow;
        toolbarRow.Height({52, x::GridUnitType::Pixel});
        body.RowDefinitions().Append(toolbarRow);
        body.RowDefinitions().Append(c::RowDefinition());
        c::Border toolbar;
        toolbar.Background(x::Media::SolidColorBrush({255, 45, 105, 165}));
        c::TextBlock label;
        label.Text(L"Original toolbar: geometry and pixels must stay in place");
        label.Margin({16, 12, 16, 12});
        label.Foreground(x::Media::SolidColorBrush({255, 255, 255, 255}));
        toolbar.Child(label);
        body.Children().Append(toolbar);
        c::TextBlock content;
        content.Text(L"Original XAML island content");
        content.FontSize(26);
        content.HorizontalAlignment(x::HorizontalAlignment::Center);
        content.VerticalAlignment(x::VerticalAlignment::Center);
        c::Grid::SetRow(content, 1);
        body.Children().Append(content);
        Theme(wcsstr(GetCommandLineW(), L"--light") != nullptr);
        source.Content(body);
        SetPropW(h, L"Macdows.Research.NativeIsland.Owned", reinterpret_cast<HANDLE>(1));
        Resize();
        ShowWindow(h, SW_SHOW);
    }
};
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        PACKAGE_VERSION version{};
        version.Major = 2;
        PWSTR id{};
        PACKAGEDEPENDENCY_CONTEXT context{};
        auto kernel = GetModuleHandleW(L"kernelbase.dll");
        auto create = reinterpret_cast<decltype(&TryCreatePackageDependency)>(
            GetProcAddress(kernel, "TryCreatePackageDependency"));
        auto add = reinterpret_cast<decltype(&AddPackageDependency)>(GetProcAddress(kernel, "AddPackageDependency"));
        if (!create || !add)
            winrt::throw_hresult(E_NOTIMPL);
        winrt::check_hresult(create(nullptr, L"Microsoft.WindowsAppRuntime.2_8wekyb3d8bbwe", version,
                                    PackageDependencyProcessorArchitectures_X64, PackageDependencyLifetimeKind_Process,
                                    nullptr, CreatePackageDependencyOptions_None, &id));
        winrt::check_hresult(add(id, 0, AddPackageDependencyOptions_None, &context, nullptr));
        HeapFree(GetProcessHeap(), 0, id);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        x::Application::Start([](auto const &) { winrt::make<App>(); });
        return 0;
    } catch (...) {
        Failure("main", winrt::to_hresult());
        return static_cast<int>(winrt::to_hresult());
    }
}
