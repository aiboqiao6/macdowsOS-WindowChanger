#pragma once
// Research transaction for an existing TabView's public header slot. This does
// not move HWNDs, suppress native caption buttons or register a product backend.
#include "native-island-frame.h"
#include <memory>

namespace styler::research::winui {
struct TabStripSlot {
    HWND window{};
    c::TabView tabs{nullptr};
    c::Grid lights{nullptr};
    std::array<c::Button, 3> buttons{nullptr, nullptr, nullptr};
    winrt::Windows::Foundation::IInspectable original{nullptr};
    struct Commands {
        HWND window{};
        DWORD pid{}, thread{};
        bool active{};
        winrt::weak_ref<c::TabView> tabs;
        winrt::weak_ref<c::Grid> lights;
        bool OwnsSlot() const {
            if (!active)
                return false;
            auto view = tabs.get();
            auto header = lights.get();
            auto root = view ? view.XamlRoot() : nullptr;
            return view && header && root &&
                   root.ContentIslandEnvironment().AppWindowId().Value == reinterpret_cast<uint64_t>(window) &&
                   view.IsLoaded() && header.IsLoaded() && view.ActualWidth() >= 220 &&
                   view.FlowDirection() == x::FlowDirection::LeftToRight && !view.TabStripHeaderTemplate() &&
                   view.TabStripHeader() == header && !view.GetBindingExpression(c::TabView::TabStripHeaderProperty());
        }
        bool Execute(unsigned index) const {
            DWORD current{};
            if (index >= 3 || !active || GetCurrentThreadId() != thread || !IsWindow(window) ||
                GetWindowThreadProcessId(window, &current) != thread || current != pid || !OwnsSlot())
                return false;
            auto style = GetWindowLongPtrW(window, GWL_STYLE);
            auto menu = GetSystemMenu(window, FALSE);
            auto close = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
            if ((index == 1 && !(style & WS_MINIMIZEBOX)) || (index == 2 && !(style & WS_MAXIMIZEBOX)) ||
                (index == 0 && (close == UINT(-1) || (close & (MF_DISABLED | MF_GRAYED)))))
                return false;
            return PostMessageW(window, WM_SYSCOMMAND,
                                index == 0         ? SC_CLOSE
                                : index == 1       ? SC_MINIMIZE
                                : IsZoomed(window) ? SC_RESTORE
                                                   : SC_MAXIMIZE,
                                0) != FALSE;
        }
    };
    std::shared_ptr<Commands> commands;
    bool active{}, restoring{};

    TabStripSlot(HWND target, c::TabView const &view) : window(target), tabs(view) {}
    TabStripSlot(TabStripSlot const &) = delete;
    TabStripSlot &operator=(TabStripSlot const &) = delete;
    bool CanExecute(unsigned index) const {
        auto style = GetWindowLongPtrW(window, GWL_STYLE);
        if (index == 1)
            return (style & WS_MINIMIZEBOX) != 0;
        if (index == 2)
            return (style & WS_MAXIMIZEBOX) != 0;
        auto menu = GetSystemMenu(window, FALSE);
        auto state = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
        return state != UINT(-1) && !(state & (MF_DISABLED | MF_GRAYED));
    }
    bool OwnsSlot() const {
        return active && tabs.TabStripHeader() == lights &&
               !tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty());
    }
    bool Ready() const {
        if (!OwnsSlot() || !commands->OwnsSlot() || lights.ActualWidth() < 84 || lights.ActualHeight() < 24)
            return false;
        for (auto const &button : buttons)
            if (!button.IsLoaded() || button.ActualWidth() <= 0 || button.ActualHeight() <= 0)
                return false;
        return true;
    }
    void UpdateCapabilities() {
        if (OwnsSlot())
            for (unsigned i = 0; i != buttons.size(); ++i)
                buttons[i].IsEnabled(CanExecute(i));
    }
    // A host integration must call this after layout changes. The research
    // fixture does so explicitly; there is no product polling lease yet.
    bool Refresh() {
        if (!OwnsSlot() || !commands->OwnsSlot()) {
            Restore();
            return false;
        }
        UpdateCapabilities();
        return Ready();
    }
    void Restore() {
        if (!active || restoring)
            return;
        restoring = true;
        struct Guard {
            bool &flag;
            ~Guard() {
                flag = false;
            }
        } guard{restoring};
        commands->active = false;
        // A host replacement belongs to the host. Never write our old null
        // value over a new header or a new binding installed while attached.
        if (tabs.TabStripHeader() == lights && !tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty())) {
            if (original == x::DependencyProperty::UnsetValue())
                tabs.ClearValue(c::TabView::TabStripHeaderProperty());
            else
                tabs.SetValue(c::TabView::TabStripHeaderProperty(), original);
        }
        active = false;
        buttons = {nullptr, nullptr, nullptr};
        lights = nullptr;
    }
    void Apply() {
        if (active || restoring || !IsWindow(window) ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() || !tabs || !tabs.IsLoaded() ||
            tabs.FlowDirection() != x::FlowDirection::LeftToRight || tabs.ActualWidth() < 220 ||
            tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty()) || tabs.TabStripHeader() ||
            tabs.TabStripHeaderTemplate())
            winrt::throw_hresult(E_NOTIMPL);
        auto root = tabs.XamlRoot();
        if (!root || root.ContentIslandEnvironment().AppWindowId().Value != reinterpret_cast<uint64_t>(window))
            winrt::throw_hresult(E_NOTIMPL);
        original = tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty());
        commands = std::make_shared<Commands>();
        commands->window = window;
        commands->thread = GetWindowThreadProcessId(window, &commands->pid);
        lights = c::Grid();
        commands->tabs = winrt::make_weak(tabs);
        commands->lights = winrt::make_weak(lights);
        lights.Width(84);
        lights.MinWidth(84);
        lights.Height(24);
        lights.VerticalAlignment(x::VerticalAlignment::Center);
        // Inherit the existing island's theme and background. Do not copy a
        // theme or opaque brush from a different XamlRoot.
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
        for (unsigned i = 0; i != buttons.size(); ++i) {
            auto button = c::Button();
            button.Template(buttonTemplate);
            button.MinWidth(0);
            button.MinHeight(0);
            button.Width(20);
            button.Height(24);
            button.HorizontalAlignment(x::HorizontalAlignment::Left);
            button.Margin({double(8 + i * 20), 0, 0, 0});
            button.IsTabStop(false);
            button.AllowFocusOnInteraction(false);
            button.IsEnabled(CanExecute(i));
            x::Automation::AutomationProperties::SetName(button, names[i]);
            x::Shapes::Ellipse dot;
            dot.Width(12);
            dot.Height(12);
            dot.Fill(x::Media::SolidColorBrush(colors[i]));
            button.Content(dot);
            // Host-retained old controls must not issue commands after undo or
            // retain a callback into a destroyed transaction.
            button.Click([state = commands, i](auto const &, auto const &) { state->Execute(i); });
            buttons[i] = button;
            lights.Children().Append(button);
        }
        // Retain rollback state before a setter that can reenter host code.
        active = true;
        try {
            tabs.TabStripHeader(lights);
            if (!OwnsSlot())
                winrt::throw_hresult(E_ABORT);
            commands->active = true;
        } catch (...) {
            Restore();
            throw;
        }
    }
    ~TabStripSlot() {
        try {
            Restore();
        } catch (...) {
        }
    }
};
} // namespace styler::research::winui
