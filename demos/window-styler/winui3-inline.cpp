// Windows App SDK prototype: an existing, extended XAML title row is required.
// Private root discovery is limited to the exact verified runtime ABI; layout,
// controls, input regions and caption visibility use the framework's APIs.
#include "adapter-api.h"
#include "pe-identity.h"
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>
#include <memory>
#include <algorithm>
#include <unordered_set>
#include <array>
#include <atomic>

namespace {
namespace x = winrt::Microsoft::UI::Xaml;
namespace c = x::Controls;
namespace w = winrt::Microsoft::UI::Windowing;
namespace input = winrt::Microsoft::UI::Input;
using winrt::Windows::Graphics::RectInt32;
constexpr UINT_PTR SubclassId = 0x4d575833;
UINT ValidateMessage() {
    return RegisterWindowMessageW(L"Macdows.WindowStyler.WinUI3.Validate.v1");
}
struct Layout {
    x::UIElement root{nullptr};
    c::Grid title{nullptr};
    x::FrameworkElement content{nullptr};
    w::AppWindow window{nullptr};
    w::OverlappedPresenter presenter{nullptr};
    input::InputNonClientPointerSource pointer{nullptr};
};
std::vector<x::Hosting::DesktopWindowXamlSource> Roots() {
    auto module = GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
    constexpr GUID id{0x4acdbe85, 0xd668, 0x192e, {0xb8, 0xb6, 0xa9, 0x33, 0x4a, 0xc9, 0x1b, 0x79}};
    if (!styler::MatchesPdb(module, id))
        return {};
    constexpr BYTE bytes[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55,
                           0x57, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x40};
    auto address = reinterpret_cast<BYTE *>(module) + 0x731804;
    if (memcmp(address, bytes, sizeof(bytes)))
        return {};
    std::vector<Microsoft::WRL::ComPtr<::IInspectable>> values;
    winrt::check_hresult(reinterpret_cast<HRESULT (*)(decltype(values) &)>(address)(values));
    std::vector<x::Hosting::DesktopWindowXamlSource> result;
    for (auto const &value : values) {
        winrt::Windows::Foundation::IInspectable object{nullptr};
        winrt::copy_from_abi(object, value.Get());
        if (auto source = object.try_as<x::Hosting::DesktopWindowXamlSource>())
            result.push_back(source);
    }
    return result;
}
void Candidates(x::DependencyObject const &node, x::UIElement const &root, std::vector<c::Grid> &list, int depth = 0) {
    if (!node || depth > 12)
        return;
    if (auto grid = node.try_as<c::Grid>(); grid && grid.IsLoaded() && grid.Visibility() == x::Visibility::Visible) {
        auto p = grid.TransformToVisual(root).TransformPoint({0, 0});
        auto body = root.as<x::FrameworkElement>();
        if (std::abs(p.X) < 2 && std::abs(p.Y) < 2 && grid.ActualHeight() >= 24 && grid.ActualHeight() <= 64 &&
            grid.ActualWidth() > body.ActualWidth() * .70 && grid.Children().Size() > 0) {
            list.push_back(grid);
            return;
        }
    }
    for (int i = 0; i < x::Media::VisualTreeHelper::GetChildrenCount(node); i++)
        Candidates(x::Media::VisualTreeHelper::GetChild(node, i), root, list, depth + 1);
}
Layout Find(HWND hwnd) {
    if (IsIconic(hwnd) ||
        GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd)) != DPI_AWARENESS_PER_MONITOR_AWARE)
        return {};
    Layout result;
    unsigned matches{};
    for (auto source : Roots())
        if (auto root = source.Content()) {
            auto id = root.XamlRoot().ContentIslandEnvironment().AppWindowId();
            // The verified ABI uses the root owner's HWND as its WindowId value. Never
            // select another window's root on a GUI thread hosting multiple windows.
            if (id.Value != reinterpret_cast<uint64_t>(hwnd))
                continue;
            auto window = w::AppWindow::GetFromWindowId(id);
            if (!window || !window.TitleBar().ExtendsContentIntoTitleBar())
                continue;
            auto presenter = window.Presenter().try_as<w::OverlappedPresenter>();
            if (!presenter)
                continue;
            auto pointer = input::InputNonClientPointerSource::GetForWindowId(id);
            if (!pointer)
                continue;
            std::vector<c::Grid> candidates;
            Candidates(root, root, candidates);
            if (candidates.size() != 1)
                continue;
            auto title = candidates[0];
            if (title.FlowDirection() != x::FlowDirection::LeftToRight)
                continue;
            bool originalDragRegion{};
            double scale = root.XamlRoot().RasterizationScale();
            for (auto rect : pointer.GetRegionRects(input::NonClientRegionKind::Caption))
                if (rect.Y <= 2 * scale && rect.Height >= 20 * scale &&
                    rect.Height <= title.ActualHeight() * scale + 2 && rect.Width > title.ActualWidth() * scale * .5)
                    originalDragRegion = true;
            if (!originalDragRegion)
                continue;
            x::FrameworkElement content{nullptr};
            for (auto child : title.Children())
                if (auto e = child.try_as<x::FrameworkElement>();
                    e && e.Visibility() == x::Visibility::Visible && e.ActualWidth() > 0) {
                    content = e;
                    break;
                }
            bool boundColumns{};
            for (auto child : title.Children())
                if (auto element = child.try_as<x::FrameworkElement>();
                    element && element.GetBindingExpression(c::Grid::ColumnProperty()))
                    boundColumns = true;
            if (!content || boundColumns)
                continue;
            result = {root, title, content, window, presenter, pointer};
            ++matches;
        }
    return matches == 1 ? result : Layout{};
}
struct ChildColumn {
    x::FrameworkElement element{nullptr};
    winrt::Windows::Foundation::IInspectable original{nullptr};
    x::Data::Binding binding{nullptr};
    int64_t token{};
    bool watching{}, enforcing{};
};
struct State {
    HWND hwnd{}, owner{};
    HANDLE process{};
    UINT_PTR timer{};
    UINT_PTR adapter{};
    Layout layout;
    c::Grid lights{nullptr};
    bool border{}, caption{}, changing{}, layoutWatch{}, unloadWatch{}, changedWatch{}, pending{};
    unsigned commands{};
    winrt::event_token layoutToken{}, unloadToken{}, changedToken{};
    std::vector<RectInt32> passthrough, lastApplied;
    c::ColumnDefinition spacer{nullptr}, implicitColumn{nullptr};
    std::vector<std::unique_ptr<ChildColumn>> columns;
    void *presenterInterface{};
    void **presenterVtable{};
    std::array<void *, 23> policyVtable{};
    bool policyHook{};
};
// IOverlappedPresenter has IInspectable's six slots followed by the seventeen
// methods in its public WinRT ABI. Shadow this one interface instance rather
// than changing a shared vtable or polling/repainting native caption controls.
using PolicyFn = HRESULT(STDMETHODCALLTYPE *)(void *, bool, bool);
std::atomic<PolicyFn> originalPolicy{};
thread_local std::vector<State *> policyStates;
State *Live(HWND);
HRESULT STDMETHODCALLTYPE Policy(void *object, bool border, bool caption) {
    auto original = originalPolicy.load();
    if (!original)
        return E_UNEXPECTED;
    for (auto s : policyStates)
        if (s->presenterInterface == object) {
            auto hwnd = s->hwnd;
            auto changing = s->changing;
            auto result = original(object, border, changing ? caption : false);
            if (SUCCEEDED(result) && !changing && Live(hwnd) == s) {
                s->border = border;
                s->caption = caption;
            }
            return result;
        }
    return original(object, border, caption);
}
bool HookPolicy(State *s) {
    s->presenterInterface = winrt::get_abi(s->layout.presenter.as<w::IOverlappedPresenter>());
    auto slot = reinterpret_cast<void ***>(s->presenterInterface);
    s->presenterVtable = *slot;
    auto original = reinterpret_cast<PolicyFn>(s->presenterVtable[22]);
    PolicyFn expected{};
    originalPolicy.compare_exchange_strong(expected, original);
    if (originalPolicy.load() != original)
        return false;
    std::copy_n(s->presenterVtable, s->policyVtable.size(), s->policyVtable.begin());
    s->policyVtable[22] = reinterpret_cast<void *>(&Policy);
    policyStates.push_back(s);
    if (InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(slot), s->policyVtable.data(),
                                          s->presenterVtable) != s->presenterVtable) {
        std::erase(policyStates, s);
        return false;
    }
    s->policyHook = true;
    return true;
}
void UnhookPolicy(State *s) {
    if (!s->policyHook)
        return;
    InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(s->presenterInterface), s->presenterVtable,
                                      s->policyVtable.data());
    std::erase(policyStates, s);
    s->policyHook = false;
}
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
State *Live(HWND h) {
    DWORD_PTR value{};
    return GetWindowSubclass(h, Subclass, SubclassId, &value) ? reinterpret_cast<State *>(value) : nullptr;
}
bool Equal(std::vector<RectInt32> const &a, std::vector<RectInt32> const &b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](auto const &p, auto const &q) {
               return p.X == q.X && p.Y == q.Y && p.Width == q.Width && p.Height == q.Height;
           });
}
std::vector<RectInt32> ReadRegions(State *s) {
    auto values = s->layout.pointer.GetRegionRects(input::NonClientRegionKind::Passthrough);
    return {values.begin(), values.end()};
}
void Queue(State *s) {
    if (!s->pending) {
        s->pending = true;
        PostMessageW(s->hwnd, ValidateMessage(), 0, 0);
    }
}
void Regions(State *s) {
    if (s->changing)
        return;
    s->changing = true;
    try {
        auto current = ReadRegions(s);
        if (!Equal(current, s->lastApplied))
            s->passthrough = current;
        auto point = s->lights.TransformToVisual(s->layout.root).TransformPoint({0, 0});
        double scale = s->layout.root.XamlRoot().RasterizationScale();
        auto rects = s->passthrough;
        rects.push_back({int(std::lround(point.X * scale)), int(std::lround(point.Y * scale)),
                         int(std::lround(s->lights.ActualWidth() * scale)),
                         int(std::lround(s->lights.ActualHeight() * scale))});
        if (!Equal(current, rects))
            s->layout.pointer.SetRegionRects(input::NonClientRegionKind::Passthrough, rects);
        s->lastApplied = std::move(rects);
    } catch (...) {
        Queue(s);
    }
    s->changing = false;
}
void ReserveColumns(State *s) {
    for (auto child : s->layout.title.Children())
        if (auto element = child.try_as<x::FrameworkElement>(); element && element != s->lights) {
            auto saved = std::make_unique<ChildColumn>();
            saved->element = element;
            saved->original = element.ReadLocalValue(c::Grid::ColumnProperty());
            auto original = c::Grid::GetColumn(element);
            auto entry = saved.get();
            s->columns.push_back(std::move(saved));
            c::Grid::SetColumn(element, original + 1);
            entry->token = element.RegisterPropertyChangedCallback(
                c::Grid::ColumnProperty(), [entry, s](auto const &, auto const &) {
                    if (entry->enforcing)
                        return;
                    entry->original = entry->element.ReadLocalValue(c::Grid::ColumnProperty());
                    if (auto binding = entry->element.GetBindingExpression(c::Grid::ColumnProperty())) {
                        entry->binding = binding.ParentBinding();
                        Queue(s);
                        return;
                    }
                    auto index = c::Grid::GetColumn(entry->element);
                    entry->enforcing = true;
                    try {
                        c::Grid::SetColumn(entry->element, index + 1);
                    } catch (...) {
                        entry->enforcing = false;
                        throw;
                    }
                    entry->enforcing = false;
                });
            entry->watching = true;
        }
    if (!s->layout.title.ColumnDefinitions().Size()) {
        s->implicitColumn = c::ColumnDefinition();
        s->layout.title.ColumnDefinitions().Append(s->implicitColumn);
    }
    s->spacer = c::ColumnDefinition();
    s->spacer.Width({84, x::GridUnitType::Pixel});
    s->layout.title.ColumnDefinitions().InsertAt(0, s->spacer);
}
RECT LightRect(State *s) {
    auto p = s->lights.TransformToVisual(s->layout.root).TransformPoint({0, 0});
    double scale = s->layout.root.XamlRoot().RasterizationScale();
    return {LONG(std::lround(p.X * scale)), LONG(std::lround(p.Y * scale)),
            LONG(std::lround((p.X + s->lights.ActualWidth()) * scale)),
            LONG(std::lround((p.Y + s->lights.ActualHeight()) * scale))};
}
bool Enabled(HWND h, int i) {
    auto style = GetWindowLongPtrW(h, GWL_STYLE);
    if (!IsWindowEnabled(h))
        return false;
    if (i == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (i == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(h, FALSE);
    return !menu || !(GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED));
}
void Release(State *s, bool destroyed = false) {
    RemoveWindowSubclass(s->hwnd, Subclass, SubclassId);
    if (s->timer)
        KillTimer(s->hwnd, s->timer);
    UnhookPolicy(s);
    try {
        if (s->layoutWatch)
            s->layout.title.LayoutUpdated(s->layoutToken);
    } catch (...) {
    }
    try {
        if (s->unloadWatch)
            s->layout.title.Unloaded(s->unloadToken);
    } catch (...) {
    }
    try {
        if (s->changedWatch)
            s->layout.window.Changed(s->changedToken);
    } catch (...) {
    }
    for (auto const &column : s->columns)
        try {
            if (column->watching)
                column->element.UnregisterPropertyChangedCallback(c::Grid::ColumnProperty(), column->token);
        } catch (...) {
        }
    try {
        uint32_t index{};
        if (s->lights && s->layout.title.Children().IndexOf(s->lights, index))
            s->layout.title.Children().RemoveAt(index);
        if (s->spacer && s->layout.title.ColumnDefinitions().IndexOf(s->spacer, index))
            s->layout.title.ColumnDefinitions().RemoveAt(index);
        if (s->implicitColumn && s->layout.title.ColumnDefinitions().IndexOf(s->implicitColumn, index))
            s->layout.title.ColumnDefinitions().RemoveAt(index);
        for (auto const &column : s->columns)
            if (column->binding)
                x::Data::BindingOperations::SetBinding(column->element, c::Grid::ColumnProperty(), column->binding);
            else if (column->original == x::DependencyProperty::UnsetValue())
                column->element.ClearValue(c::Grid::ColumnProperty());
            else
                column->element.SetValue(c::Grid::ColumnProperty(), column->original);
        if (!destroyed) {
            auto regions = ReadRegions(s);
            if (Equal(regions, s->lastApplied))
                s->layout.pointer.SetRegionRects(input::NonClientRegionKind::Passthrough, s->passthrough);
            s->layout.presenter.SetBorderAndTitleBar(s->border, s->caption);
            s->layout.root.UpdateLayout();
        }
    } catch (...) {
    }
    RemovePropW(s->hwnd, styler::InlineProperty);
    RemovePropW(s->hwnd, styler::OwnerProperty);
    RemovePropW(s->hwnd, styler::AdapterProperty);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
bool ValidTree(State *s) {
    if (!s->layout.title.IsLoaded() || x::Media::VisualTreeHelper::GetParent(s->lights) != s->layout.title ||
        s->layout.root.XamlRoot().ContentIslandEnvironment().AppWindowId() != s->layout.window.Id() ||
        !s->layout.title.ColumnDefinitions().Size() || s->layout.title.ColumnDefinitions().GetAt(0) != s->spacer)
        return false;
    for (auto const &column : s->columns)
        if (column->binding)
            return false;
    for (auto child : s->layout.title.Children())
        if (auto e = child.try_as<x::FrameworkElement>();
            e && e != s->lights && std::none_of(s->columns.begin(), s->columns.end(), [&](auto const &column) {
                return column->element == e;
            }))
            return false;
    return true;
}
LRESULT CALLBACK Subclass(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto s = reinterpret_cast<State *>(data);
    if (m == styler::QueryMessage())
        try {
            return ValidTree(s) && !s->layout.presenter.HasTitleBar() ? styler::Acknowledged : 0;
        } catch (...) {
            return 0;
        }
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(wp) != s->owner)
            return 0;
        if (s->commands)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (m == styler::DiagnosticMessage())
        try {
            auto rect = LightRect(s);
            auto p = s->layout.content.TransformToVisual(s->layout.root).TransformPoint({0, 0});
            switch (static_cast<styler::Metric>(wp)) {
            case styler::Metric::InlineLeft:
                return rect.left;
            case styler::Metric::InlineTop:
                return rect.top;
            case styler::Metric::InlineWidth:
                return rect.right - rect.left;
            case styler::Metric::InlineHeight:
                return rect.bottom - rect.top;
            case styler::Metric::InlineTabsLeft:
                return LONG(std::lround(p.X * s->layout.root.XamlRoot().RasterizationScale()));
            case styler::Metric::InlineOriginalButtonsVisible:
                return s->layout.presenter.HasTitleBar() ? 3 : 0;
            case styler::Metric::InlineKind:
                return 4;
            default:
                return 0;
            }
        } catch (...) {
            return 0;
        }
    if (m == WM_NCDESTROY) {
        Release(s, true);
        return DefSubclassProc(h, m, wp, lp);
    }
    if (m == WM_TIMER && wp == s->timer) {
        if (!IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT)
            Release(s);
        return 0;
    }
    if (m == ValidateMessage()) {
        s->pending = false;
        if (s->commands) {
            Queue(s);
            return 0;
        }
        try {
            if (!ValidTree(s) || s->layout.window.Presenter() != s->layout.presenter) {
                Release(s);
                return 0;
            }
            if (s->layout.presenter.HasTitleBar()) {
                s->caption = true;
                s->border = s->layout.presenter.HasBorder();
                s->changing = true;
                s->layout.presenter.SetBorderAndTitleBar(s->border, false);
                s->changing = false;
            }
            Regions(s);
        } catch (...) {
            Release(s);
        }
        return 0;
    }
    if (m == WM_SYSCOMMAND) {
        ++s->commands;
        auto r = DefSubclassProc(h, m, wp, lp);
        if (auto live = Live(h); live == s) {
            --live->commands;
            Queue(live);
        }
        return r;
    }
    auto r = DefSubclassProc(h, m, wp, lp);
    if ((m == WM_STYLECHANGED || m == WM_SIZE || m == WM_DPICHANGED || m == WM_THEMECHANGED) && Live(h) == s &&
        !s->changing)
        Queue(s);
    return r;
}
DWORD Attach(styler::AdapterContextV1 const *context) {
    if (Live(context->window))
        return ERROR_ALREADY_EXISTS;
    auto state = std::make_unique<State>();
    state->hwnd = context->window;
    state->owner = context->owner;
    state->adapter = context->adapterId;
    state->layout = Find(state->hwnd);
    if (!state->layout.title)
        return ERROR_NOT_SUPPORTED;
    DWORD pid{};
    GetWindowThreadProcessId(state->owner, &pid);
    state->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!state->process)
        return ERROR_ACCESS_DENIED;
    auto s = state.release();
    try {
        s->border = s->layout.presenter.HasBorder();
        s->caption = s->layout.presenter.HasTitleBar();
        s->passthrough = ReadRegions(s);
        s->lastApplied = s->passthrough;
        if (!HookPolicy(s))
            winrt::throw_hresult(E_NOTIMPL);
        if (!SetWindowSubclass(s->hwnd, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)))
            winrt::throw_last_error();
        s->lights = c::Grid();
        s->lights.Width(84);
        s->lights.HorizontalAlignment(x::HorizontalAlignment::Left);
        s->lights.Background(x::Media::SolidColorBrush({0, 0, 0, 0}));
        c::Grid::SetRowSpan(s->lights, std::max(1u, s->layout.title.RowDefinitions().Size()));
        auto buttonTemplate =
            x::Markup::XamlReader::Load(
                L"<ControlTemplate xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                L"TargetType=\"Button\"><ContentPresenter Content=\"{TemplateBinding Content}\" "
                L"HorizontalContentAlignment=\"Center\" VerticalContentAlignment=\"Center\" "
                L"Background=\"Transparent\"/></ControlTemplate>")
                .as<c::ControlTemplate>();
        const winrt::Windows::UI::Color colors[]{{255, 255, 95, 87}, {255, 255, 189, 46}, {255, 40, 200, 64}};
        const wchar_t *names[]{L"Close window", L"Minimize window", L"Maximize or restore window"};
        for (int i = 0; i < 3; i++) {
            c::Button button;
            button.Template(buttonTemplate);
            button.MinWidth(0);
            button.MinHeight(0);
            button.Width(20);
            button.Height(24);
            button.HorizontalAlignment(x::HorizontalAlignment::Left);
            button.VerticalAlignment(x::VerticalAlignment::Center);
            button.Margin({double(8 + i * 20), 0, 0, 0});
            button.IsEnabled(Enabled(s->hwnd, i));
            x::Shapes::Ellipse dot;
            dot.Width(12);
            dot.Height(12);
            dot.Fill(x::Media::SolidColorBrush(colors[i]));
            button.Content(dot);
            x::Automation::AutomationProperties::SetName(button, names[i]);
            button.Click([h = s->hwnd, i](auto const &, auto const &) {
                if (Enabled(h, i))
                    PostMessageW(h, WM_SYSCOMMAND,
                                 i == 0        ? SC_CLOSE
                                 : i == 1      ? SC_MINIMIZE
                                 : IsZoomed(h) ? SC_RESTORE
                                               : SC_MAXIMIZE,
                                 0);
            });
            s->lights.Children().Append(button);
        }
        ReserveColumns(s);
        s->layout.title.Children().Append(s->lights);
        s->changing = true;
        s->layout.presenter.SetBorderAndTitleBar(s->border, false);
        s->changing = false;
        s->layout.root.UpdateLayout();
        Regions(s);
        s->layoutToken = s->layout.title.LayoutUpdated([s](auto const &, auto const &) {
            if (ValidTree(s))
                Regions(s);
            else
                Queue(s);
        });
        s->layoutWatch = true;
        s->unloadToken = s->layout.title.Unloaded([s](auto const &, auto const &) { Queue(s); });
        s->unloadWatch = true;
        s->changedToken = s->layout.window.Changed([s](auto const &, auto const &) {
            if (!s->changing)
                Queue(s);
        });
        s->changedWatch = true;
        s->timer = SetTimer(s->hwnd, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
        if (!s->timer || !SetPropW(s->hwnd, styler::OwnerProperty, s->owner) ||
            !SetPropW(s->hwnd, styler::AdapterProperty, reinterpret_cast<HANDLE>(context->adapterId)) ||
            !SetPropW(s->hwnd, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(4))))
            winrt::throw_hresult(E_OUTOFMEMORY);
        return 0;
    } catch (...) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    if (!GetPropW(context->window, styler::SampleProperty))
        return ERROR_NOT_SUPPORTED;
    try {
        return Find(context->window).title ? 0 : ERROR_NOT_SUPPORTED;
    } catch (...) {
        return ERROR_NOT_SUPPORTED;
    }
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    if (!GetPropW(context->window, styler::SampleProperty))
        return ERROR_NOT_SUPPORTED;
    try {
        return Attach(context);
    } catch (...) {
        return ERROR_NOT_SUPPORTED;
    }
}
