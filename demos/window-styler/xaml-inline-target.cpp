#include "protocol.h"
#include <windowsx.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <memory>

namespace {
namespace x = winrt::Windows::UI::Xaml;
namespace c = x::Controls;
struct App {
    x::Hosting::DesktopWindowXamlSource source{nullptr};
    c::Grid title{nullptr};
    c::StackPanel buttons{nullptr};
    HWND bridge{}, captionInput{};
    bool hybrid{}, closing{};
};
void SizeBridge(HWND window, App *app) {
    RECT r{};
    GetClientRect(window, &r);
    int scale = GetDpiForWindow(window), left = app->hybrid ? MulDiv(32, scale, 96) : 0,
        right = app->hybrid ? MulDiv(138, scale, 96) : 0;
    SetWindowPos(app->bridge, nullptr, left, 0, std::max(1L, r.right - left - right),
                 app->hybrid ? MulDiv(40, scale, 96) : r.bottom, SWP_NOZORDER | SWP_SHOWWINDOW);
    if (app->captionInput)
        SetWindowPos(app->captionInput, HWND_TOP, 0, 0, MulDiv(84, scale, 96), MulDiv(40, scale, 96),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
LRESULT CALLBACK CaptionProcedure(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCHITTEST)
        return HTCAPTION;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(w, &ps);
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCALCSIZE)
        return 0;
    if (message == WM_NCHITTEST) {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window, &p);
        if (p.y < 4)
            return HTTOP;
        return HTCLIENT;
    }
    if (message == WM_SIZE && app && app->bridge && w != SIZE_MINIMIZED)
        SizeBridge(window, app);
    if (message == WM_APP + 66 && app) {
        if (app->hybrid) {
            auto style = GetWindowLongPtrW(window, GWL_STYLE);
            SetWindowLongPtrW(window, GWL_STYLE, w == 2 ? style & ~WS_SYSMENU : style | WS_SYSMENU);
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            app->title.Padding({4, 0, 0, 0});
            SizeBridge(window, app);
        } else
            app->buttons.Visibility(w == 2 ? x::Visibility::Collapsed : x::Visibility::Visible);
        return 1;
    }
    if (message == WM_CLOSE && app)
        app->closing = true;
    if (message == WM_PAINT)
        SetPropW(window, styler::SamplePaintProperty, reinterpret_cast<HANDLE>(1));
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcW(window, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int) {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = x::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = Procedure;
        cls.lpszClassName = L"Macdows.XamlOrdinarySample";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        bool hybrid = wcsstr(args, L"--hybrid") != nullptr;
        const wchar_t *caption =
            hybrid ? L"XAML / native caption hybrid row" : L"XAML Islands / ordinary semantic title row";
        HWND window = CreateWindowExW(0, cls.lpszClassName, caption, WS_OVERLAPPEDWINDOW, 180, 180, 1230, 750, nullptr,
                                      nullptr, instance, nullptr);
        if (!window)
            return 2;
        App app;
        app.hybrid = hybrid;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
        app.source = x::Hosting::DesktopWindowXamlSource();
        auto native = app.source.as<IDesktopWindowXamlSourceNative>();
        winrt::check_hresult(native->AttachToWindow(window));
        winrt::check_hresult(native->get_WindowHandle(&app.bridge));
        c::Grid root;
        root.Background(x::Media::SolidColorBrush({255, 36, 38, 43}));
        c::RowDefinition top;
        top.Height({40, x::GridUnitType::Pixel});
        root.RowDefinitions().Append(top);
        root.RowDefinitions().Append(c::RowDefinition());
        c::Grid title;
        title.Background(x::Media::SolidColorBrush({255, 48, 50, 56}));
        title.ColumnDefinitions().Append(c::ColumnDefinition());
        c::ColumnDefinition commands;
        commands.Width({120, x::GridUnitType::Pixel});
        title.ColumnDefinitions().Append(commands);
        c::TextBox content;
        content.Text(L"Ordinary XAML content: edit me");
        content.BorderThickness({0, 0, 0, 0});
        content.Margin({8, 0, 8, 0});
        title.Children().Append(content);
        c::StackPanel group;
        group.Orientation(c::Orientation::Horizontal);
        c::Grid::SetColumn(group, 1);
        const wchar_t *labels[]{L"Minimize", L"Maximize", L"Close"};
        const wchar_t *glyphs[]{L"−", L"□", L"×"};
        for (int i = 0; i < 3; ++i) {
            c::Button button;
            button.Width(40);
            button.Height(40);
            button.Content(winrt::box_value(glyphs[i]));
            x::Automation::AutomationProperties::SetName(button, labels[i]);
            button.Click([window, i](auto const &, auto const &) {
                PostMessageW(window, WM_SYSCOMMAND,
                             i == 0   ? SC_MINIMIZE
                             : i == 1 ? (IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE)
                                      : SC_CLOSE,
                             0);
            });
            group.Children().Append(button);
        }
        title.Children().Append(group);
        root.Children().Append(title);
        app.title = title;
        app.buttons = group;
        c::TextBlock body;
        body.Text(L"Windows XAML Islands\n\nOriginal title controls, no application control names.\nThe injector loads "
                  L"after this layout has rendered.");
        body.FontSize(24);
        body.Foreground(x::Media::SolidColorBrush({255, 230, 230, 235}));
        body.HorizontalAlignment(x::HorizontalAlignment::Center);
        body.VerticalAlignment(x::VerticalAlignment::Center);
        c::Grid::SetRow(body, 1);
        root.Children().Append(body);
        if (hybrid) {
            c::Grid hybridRow;
            app.title = hybridRow;
            hybridRow.Background(x::Media::SolidColorBrush({255, 48, 50, 56}));
            c::ColumnDefinition label;
            label.Width({260, x::GridUnitType::Pixel});
            hybridRow.ColumnDefinitions().Append(label);
            hybridRow.ColumnDefinitions().Append(c::ColumnDefinition());
            c::TextBlock titleText;
            titleText.Text(caption);
            titleText.VerticalAlignment(x::VerticalAlignment::Center);
            hybridRow.Children().Append(titleText);
            c::TextBox search;
            search.PlaceholderText(L"Ordinary search field");
            c::Grid::SetColumn(search, 1);
            hybridRow.Children().Append(search);
            c::UserControl holder;
            holder.Content(hybridRow);
            app.source.Content(holder);
            // A host may release callbacks and named controls on Unloaded. An adapter
            // must preserve the lifetime of this existing tree during its lease.
            hybridRow.Unloaded([&app](auto const &, auto const &) {
                if (!app.closing)
                    PostQuitMessage(5);
            });
        } else
            app.source.Content(root);
        if (hybrid) {
            WNDCLASSW input{};
            input.hInstance = instance;
            input.lpfnWndProc = CaptionProcedure;
            input.lpszClassName = L"Macdows.OrdinaryCaptionInput";
            RegisterClassW(&input);
            app.captionInput = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, input.lpszClassName, L"", WS_CHILD, 0, 0, 1,
                                               1, window, nullptr, instance, nullptr);
        }
        SizeBridge(window, &app);
        SetPropW(window, styler::SampleProperty, reinterpret_cast<HANDLE>(1));
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        MSG message{};
        auto native2 = app.source.as<IDesktopWindowXamlSourceNative2>();
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            BOOL handled{};
            native2->PreTranslateMessage(&message, &handled);
            if (!handled) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        app.closing = true;
        app.source.Close();
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        manager.Close();
        return static_cast<int>(message.wParam);
    } catch (winrt::hresult_error const &e) {
        OutputDebugStringW(e.message().c_str());
        return static_cast<int>(e.code());
    }
}
