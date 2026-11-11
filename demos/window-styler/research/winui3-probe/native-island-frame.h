#pragma once
// Shared public-framework native-title transaction; registration is external.
#include <windows.h>
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <optional>

namespace styler::research::winui {
namespace x = winrt::Microsoft::UI::Xaml;
namespace c = x::Controls;
namespace w = winrt::Microsoft::UI::Windowing;
namespace input = winrt::Microsoft::UI::Input;
using winrt::Windows::Graphics::RectInt32;
struct TitlePalette {
    using Color = winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>;
    std::array<Color, 12> colors;
    w::IconShowOptions icon;
    std::optional<w::TitleBarHeightOption> height;
    std::optional<w::TitleBarTheme> theme;
    explicit TitlePalette(w::AppWindowTitleBar const &bar)
        : colors{bar.BackgroundColor(),
                 bar.ButtonBackgroundColor(),
                 bar.ButtonForegroundColor(),
                 bar.ButtonHoverBackgroundColor(),
                 bar.ButtonHoverForegroundColor(),
                 bar.ButtonInactiveBackgroundColor(),
                 bar.ButtonInactiveForegroundColor(),
                 bar.ButtonPressedBackgroundColor(),
                 bar.ButtonPressedForegroundColor(),
                 bar.ForegroundColor(),
                 bar.InactiveBackgroundColor(),
                 bar.InactiveForegroundColor()},
          icon(bar.IconShowOptions()) {
        if (auto value = bar.try_as<w::IAppWindowTitleBar2>())
            height = value.PreferredHeightOption();
        if (auto value = bar.try_as<w::IAppWindowTitleBar3>())
            theme = value.PreferredTheme();
    }
    void PreserveHostChanges(TitlePalette const &current, TitlePalette const &applied) {
        for (size_t i = 0; i != colors.size(); ++i) {
            auto same =
                (!current.colors[i] && !applied.colors[i]) ||
                (current.colors[i] && applied.colors[i] && current.colors[i].Value() == applied.colors[i].Value());
            if (!same)
                colors[i] = current.colors[i];
        }
        if (current.icon != applied.icon)
            icon = current.icon;
        if (current.height != applied.height)
            height = current.height;
        if (current.theme != applied.theme)
            theme = current.theme;
    }
    void Restore(w::AppWindowTitleBar const &bar) const {
        bar.ResetToDefault();
        // Unchanged defaults need no setter; height/icon setters can require
        // an extended title bar even when their default is already restored.
        const bool optionsChanged = bar.IconShowOptions() != icon ||
                                    (height && bar.PreferredHeightOption() != *height) ||
                                    (theme && bar.PreferredTheme() != *theme);
        if (optionsChanged)
            bar.ExtendsContentIntoTitleBar(true);
        if (bar.IconShowOptions() != icon)
            bar.IconShowOptions(icon);
        if (height && bar.PreferredHeightOption() != *height)
            bar.PreferredHeightOption(*height);
        if (theme && bar.PreferredTheme() != *theme)
            bar.PreferredTheme(*theme);
        if (optionsChanged)
            bar.ExtendsContentIntoTitleBar(false);
        if (colors[0])
            bar.BackgroundColor(colors[0]);
        if (colors[1])
            bar.ButtonBackgroundColor(colors[1]);
        if (colors[2])
            bar.ButtonForegroundColor(colors[2]);
        if (colors[3])
            bar.ButtonHoverBackgroundColor(colors[3]);
        if (colors[4])
            bar.ButtonHoverForegroundColor(colors[4]);
        if (colors[5])
            bar.ButtonInactiveBackgroundColor(colors[5]);
        if (colors[6])
            bar.ButtonInactiveForegroundColor(colors[6]);
        if (colors[7])
            bar.ButtonPressedBackgroundColor(colors[7]);
        if (colors[8])
            bar.ButtonPressedForegroundColor(colors[8]);
        if (colors[9])
            bar.ForegroundColor(colors[9]);
        if (colors[10])
            bar.InactiveBackgroundColor(colors[10]);
        if (colors[11])
            bar.InactiveForegroundColor(colors[11]);
    }
};
struct NativeIslandFrame {
    HWND hwnd{};
    x::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge sourceBridge{nullptr};
    x::Hosting::DesktopWindowXamlSource titleSource{nullptr};
    x::FrameworkElement body{nullptr};
    c::Grid frame{nullptr}, lights{nullptr};
    c::Button buttons[3]{nullptr, nullptr, nullptr};
    x::FrameworkElement::ActualThemeChanged_revoker themeChanged;
    x::DependencyProperty backgroundProperty{nullptr};
    int64_t backgroundToken{};
    bool watchingBackground{};
    c::Grid container{nullptr};
    winrt::Windows::Foundation::IInspectable originalMargin{nullptr};
    x::Thickness margin{};
    winrt::Windows::Foundation::IInspectable originalRow{nullptr};
    c::RowDefinition captionRow{nullptr};
    w::AppWindow window{nullptr};
    std::optional<TitlePalette> palette, appliedPalette;
    const char *restoreStage{"inactive"};
    w::OverlappedPresenter presenter{nullptr};
    input::InputNonClientPointerSource pointer{nullptr};
    std::vector<RectInt32> caption, passthrough;
    bool active{}, changing{}, preserveRoot{}, separateTitle{}, invalid{}, restoring{}, themePending{};
    UINT closeMessage{};
    WPARAM closeWP{};
    LPARAM closeLP{};
    unsigned operationDepth{};
    LONG delta{}, normalDelta{};
    UINT dpi{};
    struct Operation {
        NativeIslandFrame &frame;
        explicit Operation(NativeIslandFrame &value) : frame(value) {
            ++frame.operationDepth;
        }
        ~Operation() {
            // A public XAML call can reenter WM_CLOSE. Let that call unwind
            // before the host closes its island; never pump the close again
            // while a nested Apply/Restore still has framework calls in flight.
            if (--frame.operationDepth == 0 && frame.closeMessage && IsWindow(frame.hwnd) &&
                PostMessageW(frame.hwnd, frame.closeMessage, frame.closeWP, frame.closeLP))
                frame.closeMessage = 0;
        }
    };
    static bool IsClose(UINT message, WPARAM wp) {
        return message == WM_CLOSE || (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE);
    }
    static UINT ThemeMessage() {
        static const UINT value = RegisterWindowMessageW(L"Macdows.WindowStyler.WinUI3.Theme.v1");
        return value;
    }

    NativeIslandFrame(HWND target, x::Hosting::DesktopWindowXamlSource const &island, bool keepRoot = false,
                      bool ownTitle = false)
        : hwnd(target), source(island), preserveRoot(keepRoot), separateTitle(ownTitle) {
        sourceBridge = source.SiteBridge();
        if (sourceBridge.IsClosed())
            winrt::throw_hresult(RO_E_CLOSED);
        body = source.Content().as<x::FrameworkElement>();
        window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)});
        presenter = window.Presenter().as<w::OverlappedPresenter>();
        pointer = input::InputNonClientPointerSource::GetForWindowId(window.Id());
        dpi = GetDpiForWindow(hwnd);
    }
    bool OwnsSource() {
        Operation operation(*this);
        try {
            // A closed DesktopWindowXamlSource can AV in Content(), rather
            // than returning RO_E_CLOSED. Retain its public bridge and consult
            // the bridge's close state before touching the source again.
            return !sourceBridge.IsClosed() && source.Content() == (preserveRoot || separateTitle ? body : frame) &&
                   (!separateTitle || (titleSource && titleSource.Content() == frame));
        } catch (...) {
            return false;
        }
    }
    bool InputReady() {
        Operation operation(*this);
        try {
            // Content assignment and UpdateLayout can precede Loaded for a
            // newly created source. A lease alone does not mean that its
            // caption controls have entered the input/visual tree.
            if (!frame || !lights || !frame.IsLoaded() || !lights.IsLoaded() || frame.ActualWidth() <= 0 ||
                frame.ActualHeight() <= 0)
                return false;
            for (auto const &button : buttons)
                if (!button || !button.IsLoaded() || button.ActualWidth() <= 0 || button.ActualHeight() <= 0)
                    return false;
            return true;
        } catch (...) {
            return false;
        }
    }
    void Inset() {
        captionRow.Height({delta * 96.0 / dpi, x::GridUnitType::Pixel});
        if (preserveRoot && !separateTitle) {
            auto value = margin;
            value.Top += delta * 96.0 / dpi;
            body.Margin(value);
            frame.Height(delta * 96.0 / dpi);
            x::Media::TranslateTransform shift;
            shift.Y(-delta * 96.0 / dpi);
            frame.RenderTransform(shift);
        }
    }

    ~NativeIslandFrame() {
        try {
            Unwatch();
        } catch (...) {
        }
        if (titleSource)
            try {
                titleSource.Close();
            } catch (...) {
            }
        RemoveWindowSubclass(hwnd, CaptionFilter, 0x4e49534c);
    }
    static LRESULT CALLBACK CaptionFilter(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        // Public framework calls can pump host input. Replay the original
        // close only after the outermost call has returned.
        auto self = reinterpret_cast<NativeIslandFrame *>(data);
        if (self && m == ThemeMessage()) {
            self->themePending = false;
            self->RefreshTheme();
            return 0;
        }
        if (self && self->operationDepth && IsClose(m, wp)) {
            self->closeMessage = m;
            self->closeWP = wp;
            self->closeLP = lp;
            return 0;
        }
        // Do not expose the transient caption-sized viewport to the host.
        // If the original source closed, only the host can lay out its new
        // island on rollback. Otherwise the retained bridge handles the final
        // viewport below; forwarding also would lay out original content twice.
        if (self && self->changing && m == WM_SIZE) {
            if (!self->separateTitle || !self->restoring)
                return 0;
            try {
                if (!self->sourceBridge.IsClosed())
                    return 0;
            } catch (...) {
                return 0;
            }
        }
        if ((wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) &&
            (m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP || m == WM_NCLBUTTONDBLCLK))
            return 0;
        return DefSubclassProc(h, m, wp, lp);
    }

    void Resize() {
        Operation operation(*this);
        if (!source || IsIconic(hwnd))
            return;
        if (sourceBridge.IsClosed()) {
            invalid = true;
            winrt::throw_hresult(RO_E_CLOSED);
        }
        RECT r{};
        GetClientRect(hwnd, &r);
        if (active && separateTitle && !restoring && (r.right < MulDiv(84, dpi, 96) || r.bottom <= normalDelta)) {
            invalid = true;
            winrt::throw_hresult(E_BOUNDS);
        }
        if (active && !changing) {
            // Maximized overlapped windows retain the resize border outside
            // the visible client. It is not additional title-row space.
            auto border = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            delta = normalDelta - (IsZoomed(hwnd) ? border : 0);
            Inset();
        }
        // Restore can reenter through the host's final WM_SIZE after the
        // title island has closed. Only the native content viewport remains.
        if (active && separateTitle && !restoring) {
            sourceBridge.MoveAndResize({0, delta, r.right, r.bottom - delta});
            titleSource.SiteBridge().MoveAndResize({0, 0, r.right, delta});
        } else
            sourceBridge.MoveAndResize({0, 0, r.right, r.bottom});
        if (active && !changing)
            Regions();
    }
    void Regions() {
        if (!pointer || !active)
            return;
        RECT r{};
        GetClientRect(hwnd, &r);
        pointer.SetRegionRects(input::NonClientRegionKind::Caption, std::vector<RectInt32>{{0, 0, r.right, delta}});
        pointer.SetRegionRects(input::NonClientRegionKind::Passthrough,
                               std::vector<RectInt32>{{0, 0, MulDiv(84, dpi, 96), delta}});
    }
    void UpdateTheme() {
        Operation operation(*this);
        if (!frame)
            return;
        frame.RequestedTheme(body.ActualTheme());
        x::Media::Brush brush{nullptr};
        if (auto panel = body.try_as<c::Panel>())
            brush = panel.Background();
        else if (auto control = body.try_as<c::Control>())
            brush = control.Background();
        else if (auto border = body.try_as<c::Border>())
            brush = border.Background();
        frame.Background(brush ? brush
                               : x::Media::SolidColorBrush(body.ActualTheme() == x::ElementTheme::Light
                                                               ? winrt::Windows::UI::Color{255, 243, 243, 243}
                                                               : winrt::Windows::UI::Color{255, 32, 32, 32}));
    }
    void RefreshTheme() {
        Operation operation(*this);
        if (active && !changing)
            try {
                UpdateTheme();
            } catch (...) {
                invalid = true;
            }
    }
    void QueueTheme() {
        // ActualThemeChanged runs during the host's resource propagation.
        // Updating another XamlRoot from that callback reenters the theme walk.
        // Read the final host palette after the outer framework call returns.
        if (active && !changing && !themePending) {
            themePending = true;
            if (!ThemeMessage() || !PostMessageW(hwnd, ThemeMessage(), 0, 0)) {
                themePending = false;
                invalid = true;
            }
        }
    }
    void Unwatch() {
        themePending = false;
        themeChanged.revoke();
        if (watchingBackground) {
            body.UnregisterPropertyChangedCallback(backgroundProperty, backgroundToken);
            watchingBackground = false;
        }
    }
    bool CanExecute(int i) const {
        auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (i == 1)
            return (style & WS_MINIMIZEBOX) != 0;
        if (i == 2)
            return (style & WS_MAXIMIZEBOX) != 0;
        auto menu = GetSystemMenu(hwnd, FALSE);
        auto flags = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
        return flags != UINT(-1) && !(flags & (MF_DISABLED | MF_GRAYED));
    }
    void UpdateCapabilities() {
        Operation operation(*this);
        for (int i = 0; i != 3; ++i)
            if (buttons[i])
                buttons[i].IsEnabled(CanExecute(i));
    }
    void Restore() {
        if (!active)
            return;
        Operation operation(*this);
        // Capture changes once, before our rollback setters run. In particular,
        // a host can clear a previously non-null color while we are attached.
        if (!restoring) {
            if (appliedPalette)
                palette->PreserveHostChanges(TitlePalette(window.TitleBar()), *appliedPalette);
            restoring = true;
        }
        changing = true;
        restoreStage = "unwatch";
        Unwatch();
        restoreStage = "close-title";
        if (separateTitle) {
            if (titleSource) {
                titleSource.Content(nullptr);
                titleSource.Close();
                titleSource = nullptr;
            }
        } else if (!OwnsSource()) {
            // Comparison modes may not overwrite a replacement supplied by
            // the host. The separate-title mode never writes Content at all.
            winrt::throw_hresult(E_UNEXPECTED);
        } else if (preserveRoot) {
            uint32_t index{};
            if (container.Children().IndexOf(frame, index))
                container.Children().RemoveAt(index);
            if (originalMargin == x::DependencyProperty::UnsetValue())
                body.ClearValue(x::FrameworkElement::MarginProperty());
            else
                body.SetValue(x::FrameworkElement::MarginProperty(), originalMargin);
        } else {
            source.Content(nullptr);
            frame.Children().Clear();
            if (originalRow == x::DependencyProperty::UnsetValue())
                body.ClearValue(c::Grid::RowProperty());
            else
                body.SetValue(c::Grid::RowProperty(), originalRow);
            source.Content(body);
        }
        restoreStage = "native-presenter";
        presenter.SetBorderAndTitleBar(true, true);
        restoreStage = "native-title-palette";
        palette->Restore(window.TitleBar());
        restoreStage = "input-regions";
        if (caption.empty())
            pointer.ClearRegionRects(input::NonClientRegionKind::Caption);
        else
            pointer.SetRegionRects(input::NonClientRegionKind::Caption, caption);
        if (passthrough.empty())
            pointer.ClearRegionRects(input::NonClientRegionKind::Passthrough);
        else
            pointer.SetRegionRects(input::NonClientRegionKind::Passthrough, passthrough);
        RECT nativeClient{};
        restoreStage = "native-island-position";
        GetClientRect(hwnd, &nativeClient);
        try {
            if (!sourceBridge.IsClosed() && separateTitle && IsIconic(hwnd)) {
                // A minimized HWND's client rect is not its content viewport.
                // Reposition the retained island without shrinking its size.
                RECT island{};
                winrt::check_bool(GetWindowRect(reinterpret_cast<HWND>(sourceBridge.WindowId().Value), &island));
                nativeClient.right = island.right - island.left;
                nativeClient.bottom = island.bottom - island.top;
            }
            if (!sourceBridge.IsClosed())
                sourceBridge.MoveAndResize({0, 0, nativeClient.right, nativeClient.bottom});
        } catch (winrt::hresult_error const &error) {
            // A host may close its island before destroying the HWND. There
            // is then no original bridge left to reposition.
            if (error.code() != RO_E_CLOSED)
                throw;
        }
        if (!separateTitle && OwnsSource())
            body.UpdateLayout();
        for (auto &button : buttons)
            button = nullptr;
        lights = nullptr;
        frame = nullptr;
        captionRow = nullptr;
        active = false;
        changing = false;
        restoreStage = "inactive";
        RemoveWindowSubclass(hwnd, CaptionFilter, 0x4e49534c);
    }
    void Apply() {
        Operation operation(*this);
        if (active || source.Content() != body || !presenter.HasBorder() || !presenter.HasTitleBar() ||
            window.TitleBar().ExtendsContentIntoTitleBar() || body.GetBindingExpression(c::Grid::RowProperty()))
            winrt::throw_hresult(E_UNEXPECTED);
        const bool contentHadFocus = separateTitle && source.HasFocus();
        auto focusedContent = contentHadFocus ? x::Input::FocusManager::GetFocusedElement(body.XamlRoot()) : nullptr;
        originalRow = body.ReadLocalValue(c::Grid::RowProperty());
        palette.emplace(window.TitleBar());
        if (preserveRoot && !separateTitle) {
            if (body.GetBindingExpression(x::FrameworkElement::MarginProperty()) || body.Clip())
                winrt::throw_hresult(E_NOTIMPL);
            container = body.try_as<c::Grid>();
            if (!container && x::Media::VisualTreeHelper::GetChildrenCount(body) == 1)
                container = x::Media::VisualTreeHelper::GetChild(body, 0).try_as<c::Grid>();
            if (!container || container.Clip())
                winrt::throw_hresult(E_NOTIMPL);
            auto p = container.TransformToVisual(body).TransformPoint({0, 0});
            if (std::abs(p.X) > 0.01 || std::abs(p.Y) > 0.01 ||
                std::abs(container.ActualWidth() - body.ActualWidth()) > 0.01 ||
                std::abs(container.ActualHeight() - body.ActualHeight()) > 0.01)
                winrt::throw_hresult(E_NOTIMPL);
            originalMargin = body.ReadLocalValue(x::FrameworkElement::MarginProperty());
            margin = body.Margin();
        }
        RECT before{}, after{};
        POINT originBefore{}, originAfter{};
        GetClientRect(hwnd, &before);
        if (separateTitle && (before.right < MulDiv(84, dpi, 96) || before.bottom <= 0))
            winrt::throw_hresult(E_BOUNDS);
        ClientToScreen(hwnd, &originBefore);
        auto a = pointer.GetRegionRects(input::NonClientRegionKind::Caption);
        auto b = pointer.GetRegionRects(input::NonClientRegionKind::Passthrough);
        caption.assign(a.begin(), a.end());
        passthrough.assign(b.begin(), b.end());
        changing = true;
        restoring = false;
        appliedPalette.reset();
        // Let the real composition client grow upward through the public
        // presenter API, then reserve precisely that gained space in XAML.
        active = true;
        try {
            if (!SetWindowSubclass(hwnd, CaptionFilter, 0x4e49534c, reinterpret_cast<DWORD_PTR>(this)))
                winrt::throw_last_error();
            frame = c::Grid();
            UpdateTheme();
            captionRow = c::RowDefinition();
            captionRow.Height({0, x::GridUnitType::Pixel});
            frame.RowDefinitions().Append(captionRow);
            frame.RowDefinitions().Append(c::RowDefinition());
            if (separateTitle) {
                titleSource = x::Hosting::DesktopWindowXamlSource();
                titleSource.Initialize(window.Id());
                titleSource.Content(frame);
            } else if (preserveRoot) {
                frame.VerticalAlignment(x::VerticalAlignment::Top);
                c::Grid::SetRowSpan(frame, std::max(1u, container.RowDefinitions().Size()));
                c::Grid::SetColumnSpan(frame, std::max(1u, container.ColumnDefinitions().Size()));
                c::Canvas::SetZIndex(frame, 1000);
                container.Children().Append(frame);
            } else {
                source.Content(nullptr);
                c::Grid::SetRow(body, 1);
                frame.Children().Append(body);
                source.Content(frame);
            }
            window.TitleBar().ExtendsContentIntoTitleBar(true);
            presenter.SetBorderAndTitleBar(true, false);
            GetClientRect(hwnd, &after);
            ClientToScreen(hwnd, &originAfter);
            delta = originBefore.y - originAfter.y;
            normalDelta = delta + (IsZoomed(hwnd) ? GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                                                        GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
                                                  : 0);
            if (delta <= 0 || delta > MulDiv(64, dpi, 96) || originBefore.x != originAfter.x ||
                before.right != after.right || before.bottom + delta != after.bottom)
                winrt::throw_hresult(E_UNEXPECTED);
            Inset();
            lights = c::Grid();
            lights.Width(84);
            lights.HorizontalAlignment(x::HorizontalAlignment::Left);
            lights.Background(x::Media::SolidColorBrush({0, 0, 0, 0}));
            auto buttonTemplate =
                x::Markup::XamlReader::Load(
                    L"<ControlTemplate xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                    L"TargetType=\"Button\"><ContentPresenter Content=\"{TemplateBinding Content}\" "
                    L"HorizontalContentAlignment=\"Center\" VerticalContentAlignment=\"Center\" "
                    L"Background=\"Transparent\"/></ControlTemplate>")
                    .as<c::ControlTemplate>();
            const winrt::Windows::UI::Color colors[]{{255, 255, 95, 87}, {255, 255, 189, 46}, {255, 40, 200, 64}};
            const wchar_t *names[]{L"Close window", L"Minimize window", L"Maximize or restore window"};
            for (int i = 0; i < 3; ++i) {
                c::Button button;
                button.Template(buttonTemplate);
                button.MinWidth(0);
                button.MinHeight(0);
                // Native caption buttons do not take the document's tab focus.
                button.IsTabStop(false);
                button.AllowFocusOnInteraction(false);
                button.Width(20);
                button.Height(24);
                button.HorizontalAlignment(x::HorizontalAlignment::Left);
                button.VerticalAlignment(x::VerticalAlignment::Center);
                button.Margin({double(8 + i * 20), 0, 0, 0});
                x::Shapes::Ellipse dot;
                dot.Width(12);
                dot.Height(12);
                dot.Fill(x::Media::SolidColorBrush(colors[i]));
                button.Content(dot);
                x::Automation::AutomationProperties::SetName(button, names[i]);
                button.Click([this, i](auto const &, auto const &) {
                    if (!active || changing || !CanExecute(i))
                        return;
                    PostMessageW(hwnd, WM_SYSCOMMAND,
                                 i == 0           ? SC_CLOSE
                                 : i == 1         ? SC_MINIMIZE
                                 : IsZoomed(hwnd) ? SC_RESTORE
                                                  : SC_MAXIMIZE,
                                 0);
                });
                buttons[i] = button;
                lights.Children().Append(button);
            }
            frame.Children().Append(lights);
            UpdateCapabilities();
            Resize();
            frame.UpdateLayout();
            Regions();
            if (!SetWindowSubclass(hwnd, CaptionFilter, 0x4e49534c, reinterpret_cast<DWORD_PTR>(this)))
                winrt::throw_last_error();
            themeChanged =
                body.ActualThemeChanged(winrt::auto_revoke, [this](auto const &, auto const &) { QueueTheme(); });
            if (body.try_as<c::Panel>())
                backgroundProperty = c::Panel::BackgroundProperty();
            else if (body.try_as<c::Control>())
                backgroundProperty = c::Control::BackgroundProperty();
            else if (body.try_as<c::Border>())
                backgroundProperty = c::Border::BackgroundProperty();
            if (backgroundProperty) {
                backgroundToken = body.RegisterPropertyChangedCallback(
                    backgroundProperty, [this](auto const &, auto const &) { QueueTheme(); });
                watchingBackground = true;
            }
            appliedPalette.emplace(window.TitleBar());
            // Creating a second source can move native focus away from a
            // recently rebuilt island while its logical TextBox focus remains.
            // Restore only that existing focus, in the still-foreground host;
            // never select a new control or undo a host's logical focus change.
            if (contentHadFocus && focusedContent && GetForegroundWindow() == hwnd && OwnsSource() &&
                x::Input::FocusManager::GetFocusedElement(body.XamlRoot()) == focusedContent && !source.HasFocus()) {
                source.NavigateFocus(
                    x::Hosting::XamlSourceFocusNavigationRequest(x::Hosting::XamlSourceFocusNavigationReason::Restore));
                if (!source.HasFocus())
                    winrt::throw_hresult(E_FAIL);
            }
            changing = false;
        } catch (...) {
            Restore();
            throw;
        }
    }
};
} // namespace styler::research::winui
