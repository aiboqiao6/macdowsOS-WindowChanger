#include "adapter-api.h"
#include <commctrl.h>
#include <windowsx.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cwctype>
#include <unordered_set>
#include <functional>
#include "xaml-roots.h"
#include "native-style-view.h"
#include "nonclient-frame.h"

namespace {
using namespace winrt;
namespace x = winrt::Windows::UI::Xaml;
namespace h = x::Hosting;
namespace c = x::Controls;
constexpr UINT_PTR SubclassId = 0x4d43584d;
struct Layout {
    x::UIElement root{nullptr};
    c::Grid title{nullptr};
    x::FrameworkElement content{nullptr}, original{nullptr};
    HWND bridge{};
    bool nativeCaption{};
    x::Media::Brush captionBrush{nullptr};
    std::wstring captionResource;
};
x::Media::Brush Background(x::DependencyObject const &node) {
    if (auto p = node.try_as<c::Panel>())
        return p.Background();
    if (auto control = node.try_as<c::Control>())
        return control.Background();
    if (auto border = node.try_as<c::Border>())
        return border.Background();
    return nullptr;
}
bool CaptionBackground(Layout &layout) {
    // A transparent Island may share a native caption backdrop. Resolve the
    // framework theme resource; GDI GetPixel cannot sample composition surfaces.
    for (x::DependencyObject node = layout.title; node; node = x::Media::VisualTreeHelper::GetParent(node))
        if (auto brush = Background(node).try_as<x::Media::SolidColorBrush>();
            brush && brush.Color().A == 255 && brush.Opacity() == 1) {
            layout.captionBrush = brush;
            return true;
        }
    if (auto app = x::Application::Current())
        for (auto key : {L"SolidBackgroundFillColorBaseBrush", L"ApplicationPageBackgroundThemeBrush"})
            if (auto brush = app.Resources().TryLookup(winrt::box_value(key)).try_as<x::Media::SolidColorBrush>();
                brush && brush.Color().A == 255 && brush.Opacity() == 1) {
                layout.captionResource = key;
                return true;
            }
    return false;
}
int Command(x::FrameworkElement const &e) {
    if (!e.try_as<c::Primitives::ButtonBase>())
        return -1;
    std::wstring label = e.Name().c_str();
    label += L" ";
    label += x::Automation::AutomationProperties::GetName(e).c_str();
    std::transform(label.begin(), label.end(), label.begin(), [](wchar_t ch) { return std::towlower(ch); });
    if (label.find(L"close") != label.npos || label.find(L"关闭") != label.npos)
        return 0;
    if (label.find(L"minimi") != label.npos || label.find(L"最小化") != label.npos)
        return 1;
    if (label.find(L"maximi") != label.npos || label.find(L"restore") != label.npos ||
        label.find(L"最大化") != label.npos || label.find(L"还原") != label.npos)
        return 2;
    return -1;
}
void Controls(x::DependencyObject const &node, x::UIElement const &root, std::vector<x::FrameworkElement> &controls,
              int depth = 0) {
    if (!node || depth > 30)
        return;
    if (auto e = node.try_as<x::FrameworkElement>(); e && e.Visibility() == x::Visibility::Visible && Command(e) >= 0) {
        auto p = e.TransformToVisual(root).TransformPoint({0, 0});
        if (p.Y >= -4 && p.Y < 64 && e.ActualWidth() <= 100 && e.ActualHeight() <= 64)
            controls.push_back(e);
    }
    int count = x::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++)
        Controls(x::Media::VisualTreeHelper::GetChild(node, i), root, controls, depth + 1);
}
bool Descendant(x::DependencyObject node, x::DependencyObject const &ancestor) {
    for (; node; node = x::Media::VisualTreeHelper::GetParent(node))
        if (node == ancestor)
            return true;
    return false;
}
void Trace(x::DependencyObject const &node, x::UIElement const &root, int depth = 0) {
    if (!node || depth > 12)
        return;
    std::ostringstream line;
    line << std::string(depth, ' ') << winrt::to_string(winrt::get_class_name(node));
    if (auto e = node.try_as<x::FrameworkElement>()) {
        auto p = e.TransformToVisual(root).TransformPoint({0, 0});
        line << " name=" << winrt::to_string(e.Name())
             << " automation=" << winrt::to_string(x::Automation::AutomationProperties::GetName(e)) << " xy=" << p.X
             << "," << p.Y << " size=" << e.ActualWidth() << "," << e.ActualHeight();
        if (p.Y > 64)
            return;
    }
    x::Media::Brush brush{nullptr};
    if (auto panel = node.try_as<c::Panel>())
        brush = panel.Background();
    else if (auto control = node.try_as<c::Control>())
        brush = control.Background();
    else if (auto border = node.try_as<c::Border>())
        brush = border.Background();
    if (brush) {
        line << " background=" << winrt::to_string(winrt::get_class_name(brush));
        if (auto solid = brush.try_as<x::Media::SolidColorBrush>()) {
            auto color = solid.Color();
            line << " argb=" << int(color.A) << "," << int(color.R) << "," << int(color.G) << "," << int(color.B);
        }
    }
    xaml_roots::Log(line.str());
    int n = x::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < n; i++)
        Trace(x::Media::VisualTreeHelper::GetChild(node, i), root, depth + 1);
}
Layout FindContent(xaml_roots::Content const &source) {
    Layout l;
    l.root = source.element;
    l.bridge = source.bridge;
    if (!l.root)
        return l;
    std::vector<x::FrameworkElement> controls;
    Controls(l.root, l.root, controls);
    for (auto close : controls)
        if (Command(close) == 0) {
            for (auto group = x::Media::VisualTreeHelper::GetParent(close); group;
                 group = x::Media::VisualTreeHelper::GetParent(group)) {
                auto original = group.try_as<x::FrameworkElement>();
                if (!original || original.ActualWidth() > 240 || original.ActualHeight() > 64)
                    break;
                bool min = false, max = false;
                for (auto control : controls)
                    if (Descendant(control, group)) {
                        min |= Command(control) == 1;
                        max |= Command(control) == 2;
                    }
                if (!min || !max)
                    continue;
                for (auto row = x::Media::VisualTreeHelper::GetParent(group); row;
                     row = x::Media::VisualTreeHelper::GetParent(row)) {
                    auto element = row.try_as<x::FrameworkElement>();
                    if (!element || element.ActualHeight() > 64)
                        break;
                    auto grid = row.try_as<c::Grid>();
                    if (!grid || grid.ColumnDefinitions().Size() < 2 || grid.ActualWidth() < 300 ||
                        grid.ActualHeight() < 24)
                        continue;
                    x::FrameworkElement content{nullptr};
                    for (auto child : grid.Children())
                        if (auto e = child.try_as<x::FrameworkElement>();
                            e && !Descendant(original, e) && e.Visibility() == x::Visibility::Visible &&
                            e.ActualWidth() > 100 && c::Grid::GetColumn(e) == 0) {
                            content = e;
                            break;
                        }
                    if (content) {
                        l.title = grid;
                        l.content = content;
                        l.original = original;
                        return l;
                    }
                }
            }
        }
    return {};
}
void NativeTitleGrids(x::DependencyObject const &node, x::UIElement const &root, std::wstring const &title,
                      std::vector<c::Grid> &grids, int depth = 0) {
    if (!node || depth > 8)
        return;
    if (auto grid = node.try_as<c::Grid>();
        grid && grid.ActualWidth() > 300 && grid.ActualHeight() >= 24 && grid.ActualHeight() <= 64) {
        for (auto child : grid.Children())
            if (auto text = child.try_as<c::TextBlock>(); text && text.Visibility() == x::Visibility::Visible &&
                                                          text.ActualWidth() > 0 &&
                                                          std::wstring_view(text.Text()) == title) {
                grids.push_back(grid);
                break;
            }
    }
    int n = x::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < n; i++)
        NativeTitleGrids(x::Media::VisualTreeHelper::GetChild(node, i), root, title, grids, depth + 1);
}
Layout FindNativeCaption(HWND window, std::vector<xaml_roots::Content> const &roots) {
    // Mixed Win32/XAML title rows keep the three system controls in DWM. Match
    // the existing window title and native caption geometry, never a host type,
    // executable, element name, or application-specific automation identifier.
    RECT caption{};
    BOOL nc{};
    auto style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!(style & WS_SYSMENU) || FAILED(DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &nc, sizeof(nc))) ||
        !nc || FAILED(DwmGetWindowAttribute(window, DWMWA_CAPTION_BUTTON_BOUNDS, &caption, sizeof(caption))) ||
        IsRectEmpty(&caption))
        return {};
    wchar_t text[1024]{};
    GetWindowTextW(window, text, 1024);
    if (!*text)
        return {};
    Layout result;
    unsigned matches{};
    double scale = GetDpiForWindow(window) / 96.0;
    for (auto const &root : roots) {
        if (!IsWindowVisible(root.bridge))
            continue;
        RECT r{};
        GetClientRect(root.bridge, &r);
        POINT p{};
        MapWindowPoints(root.bridge, window, &p, 1);
        {
            std::ostringstream line;
            line << "hybrid candidate bridge=" << root.bridge << " position=" << p.x << "," << p.y
                 << " size=" << r.right << "," << r.bottom;
            xaml_roots::Log(line.str());
        }
        if (p.x < 0 || p.x > scale * 128 || p.y < 0 || p.y > scale * 24 || r.bottom < scale * 24 ||
            r.bottom > scale * 64 || r.right < scale * 300)
            continue;
        std::vector<c::Grid> grids;
        NativeTitleGrids(root.element, root.element, text, grids);
        {
            std::ostringstream line;
            line << "hybrid title matches=" << grids.size();
            xaml_roots::Log(line.str());
        }
        if (grids.size() != 1)
            continue;
        auto grid = grids.front();
        auto point = grid.TransformToVisual(root.element).TransformPoint({0, 0});
        {
            std::ostringstream line;
            line << "hybrid grid position=" << point.X << "," << point.Y << " width=" << grid.ActualWidth()
                 << " columns=" << grid.ColumnDefinitions().Size();
            xaml_roots::Log(line.str());
        }
        if (std::abs(point.X) > 1 || std::abs(point.Y) > 1 || std::abs(grid.ActualWidth() * scale - r.right) > 2)
            continue;
        for (auto child : grid.Children())
            if (auto e = child.try_as<c::TextBlock>(); e && e.Visibility() == x::Visibility::Visible &&
                                                       std::wstring_view(e.Text()) == text && e.ActualWidth() > 0) {
                Layout candidate;
                candidate.root = root.element;
                candidate.title = grid;
                candidate.content = e;
                candidate.bridge = root.bridge;
                candidate.nativeCaption = true;
                if (CaptionBackground(candidate)) {
                    result = std::move(candidate);
                    ++matches;
                }
                break;
            }
    }
    return matches == 1 ? result : Layout{};
}
Layout Find(HWND window) {
    auto roots = xaml_roots::Contents(window);
    if (GetFileAttributesW((xaml_roots::Directory() + L"layout-diagnostics.enabled").c_str()) !=
        INVALID_FILE_ATTRIBUTES) {
        thread_local std::unordered_set<HWND> traced;
        if (!roots.empty() && traced.insert(window).second)
            for (auto const &r : roots) {
                std::ostringstream line;
                line << "framework root target=" << window << " bridge=" << r.bridge;
                xaml_roots::Log(line.str());
                Trace(r.element, r.element);
            }
    }
    for (auto const &root : roots)
        if (auto layout = FindContent(root); layout.title)
            return layout;
    return FindNativeCaption(window, roots);
}
struct Saved {
    x::DependencyObject object{nullptr};
    x::DependencyProperty property{nullptr};
    winrt::Windows::Foundation::IInspectable value{nullptr};
    x::Data::Binding binding{nullptr};
    Saved(x::FrameworkElement const &o, x::DependencyProperty const &p)
        : object(o), property(p), value(o.ReadLocalValue(p)) {
        if (auto e = o.GetBindingExpression(p))
            binding = e.ParentBinding();
    }
    int64_t token{};
    bool watching{}, enforcing{};
    void Watch(std::function<winrt::Windows::Foundation::IInspectable(winrt::Windows::Foundation::IInspectable const &)>
                   apply) {
        token = object.RegisterPropertyChangedCallback(property, [this, apply](auto const &, auto const &) {
            if (enforcing)
                return;
            // Keep the host's latest local value/binding for detach, not the stale
            // attach-time snapshot. Suppression itself never becomes that value.
            value = object.ReadLocalValue(property);
            binding = nullptr;
            if (auto e = object.as<x::FrameworkElement>().GetBindingExpression(property))
                binding = e.ParentBinding();
            enforcing = true;
            try {
                object.SetValue(property, apply(object.GetValue(property)));
            } catch (...) {
                enforcing = false;
                throw;
            }
            enforcing = false;
        });
        watching = true;
    }
    void Stop() {
        if (watching) {
            object.UnregisterPropertyChangedCallback(property, token);
            watching = false;
        }
    }
    void Restore() {
        if (binding)
            x::Data::BindingOperations::SetBinding(object, property, binding);
        else if (value == x::DependencyProperty::UnsetValue())
            object.ClearValue(property);
        else
            object.SetValue(property, value);
    }
};
struct State {
    HWND target{}, owner{}, drag{}, bridge{};
    HANDLE process{};
    UINT_PTR timer{};
    unsigned commands{};
    Layout layout;
    c::Grid lights{nullptr};
    c::ColumnDefinition column{nullptr}, implicitColumn{nullptr};
    std::vector<Saved> saved;
    std::vector<HWND> captionInputs;
    bool styleReads{}, systemMenu{}, movingBridge{}, unloadWatched{};
    winrt::event_token unloadToken{};
    int bridgeShift{}, pressed = -1;
};
UINT TreeChanged() {
    return RegisterWindowMessageW(L"Macdows.WindowStyler.Xaml.TreeChanged.v1");
}
HWND CaptionInput(HWND target, Layout const &layout) {
    auto p = layout.original.TransformToVisual(layout.root).TransformPoint({0, 0});
    HWND bridge = layout.bridge;
    POINT screen{LONG(std::lround((p.X + layout.original.ActualWidth() / 2) * GetDpiForWindow(target) / 96)),
                 LONG(std::lround((p.Y + layout.original.ActualHeight() / 2) * GetDpiForWindow(target) / 96))};
    ClientToScreen(bridge, &screen);
    struct Search {
        POINT point;
        HWND child{};
    } search{screen};
    EnumChildWindows(
        target,
        [](HWND child, LPARAM value) -> BOOL {
            auto &s = *reinterpret_cast<Search *>(value);
            RECT rect{};
            GetWindowRect(child, &rect);
            if (!IsWindowVisible(child) || GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId() ||
                !PtInRect(&rect, s.point))
                return TRUE;
            auto hit = SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(s.point.x, s.point.y));
            if (hit == HTCLOSE || hit == HTMINBUTTON || hit == HTMAXBUTTON) {
                s.child = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.child;
}
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
bool Enabled(HWND w, int i) {
    auto style = GetWindowLongPtrW(w, GWL_STYLE);
    if (!IsWindowEnabled(w))
        return false;
    if (i == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (i == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(w, FALSE);
    return !menu || !(GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED));
}
void Action(HWND w, int i) {
    if (Enabled(w, i)) {
        std::ostringstream line;
        line << "light action " << i << " window=" << w;
        xaml_roots::Log(line.str());
        PostMessageW(w, WM_SYSCOMMAND,
                     i == 0        ? SC_CLOSE
                     : i == 1      ? SC_MINIMIZE
                     : IsZoomed(w) ? SC_RESTORE
                                   : SC_MAXIMIZE,
                     0);
    }
}
RECT LightRect(State *s) {
    auto p = s->lights.TransformToVisual(s->layout.root).TransformPoint({0, 0});
    double scale = GetDpiForWindow(s->target) / 96.0;
    POINT origin{};
    MapWindowPoints(s->bridge, s->target, &origin, 1);
    return {origin.x + LONG(std::lround(p.X * scale)), origin.y + LONG(std::lround(p.Y * scale)),
            origin.x + LONG(std::lround((p.X + s->lights.ActualWidth()) * scale)),
            origin.y + LONG(std::lround((p.Y + s->lights.ActualHeight()) * scale))};
}
int LightHit(State *s, POINT screen) noexcept {
    try {
        if (!s->lights)
            return -1;
        ScreenToClient(s->target, &screen);
        auto r = LightRect(s);
        if (screen.y < r.top || screen.y >= r.bottom)
            return -1;
        int offset = screen.x - r.left;
        for (int i = 0; i < 3; i++)
            if (offset >= MulDiv(8 + i * 20, GetDpiForWindow(s->target), 96) &&
                offset < MulDiv(28 + i * 20, GetDpiForWindow(s->target), 96))
                return i;
    } catch (...) {
    }
    return -1;
}
void CaptionInputs(State *s) {
    // A native drag surface can sit above a mixed XAML title row. Discover it
    // by actual caption hit-test semantics, never by host class or control name.
    // InputSite and all other Island descendants keep their original dispatch.
    EnumChildWindows(
        s->target,
        [](HWND child, LPARAM value) -> BOOL {
            auto state = reinterpret_cast<State *>(value);
            if (child == state->bridge || IsChild(state->bridge, child) || !IsWindowVisible(child) ||
                GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId())
                return TRUE;
            auto r = LightRect(state);
            for (int i = 0; i < 3; i++) {
                POINT p{r.left + MulDiv(18 + i * 20, GetDpiForWindow(state->target), 96), (r.top + r.bottom) / 2};
                ClientToScreen(state->target, &p);
                RECT bounds{};
                if (!GetWindowRect(child, &bounds) || !PtInRect(&bounds, p) ||
                    SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)) != HTCAPTION)
                    return TRUE;
            }
            state->captionInputs.push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(s));
}
void Release(State *s) {
    try {
        if (s->unloadWatched)
            s->layout.title.Unloaded(s->unloadToken);
        for (auto &saved : s->saved)
            saved.Stop();
    } catch (...) {
    }
    KillTimer(s->target, s->timer);
    RemoveWindowSubclass(s->target, Subclass, SubclassId);
    if (s->drag)
        RemoveWindowSubclass(s->drag, Subclass, SubclassId);
    for (auto input : s->captionInputs)
        RemoveWindowSubclass(input, Subclass, SubclassId);
    if (s->pressed >= 0 && (GetCapture() == s->target || std::find(s->captionInputs.begin(), s->captionInputs.end(),
                                                                   GetCapture()) != s->captionInputs.end()))
        ReleaseCapture();
    if (s->layout.nativeCaption)
        RemoveWindowSubclass(s->bridge, Subclass, SubclassId);
    try {
        uint32_t index{};
        if (s->lights && s->layout.title && s->layout.title.Children().IndexOf(s->lights, index))
            s->layout.title.Children().RemoveAt(index);
        if (s->column && s->layout.title && s->layout.title.ColumnDefinitions().IndexOf(s->column, index))
            s->layout.title.ColumnDefinitions().RemoveAt(index);
        if (s->implicitColumn && s->layout.title &&
            s->layout.title.ColumnDefinitions().IndexOf(s->implicitColumn, index))
            s->layout.title.ColumnDefinitions().RemoveAt(index);
        for (auto it = s->saved.rbegin(); it != s->saved.rend(); ++it)
            it->Restore();
        if (s->layout.root)
            s->layout.root.UpdateLayout();
    } catch (...) {
    }
    if (s->layout.nativeCaption) {
        RECT r{};
        if (GetWindowRect(s->bridge, &r)) {
            POINT p{r.left, r.top};
            MapWindowPoints(nullptr, s->target, &p, 1);
            SetWindowPos(s->bridge, nullptr, p.x + s->bridgeShift, p.y, std::max(1L, r.right - r.left - s->bridgeShift),
                         r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (s->systemMenu && IsWindow(s->target))
            styler::style_view::RestoreMenu(s->target, true);
        RemovePropW(s->target, styler::style_view::StyleProperty);
        if (s->styleReads)
            styler::style_view::ReleaseReads();
        for (auto property : styler::style_view::BoundsProperties)
            RemovePropW(s->target, property);
        RedrawWindow(s->target, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    RemovePropW(s->target, styler::InlineProperty);
    RemovePropW(s->target, styler::OwnerProperty);
    RemovePropW(s->target, styler::AdapterProperty);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
LRESULT CALLBACK Subclass(HWND window, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto *s = reinterpret_cast<State *>(ref);
    if (s->layout.nativeCaption && (window == s->target || std::find(s->captionInputs.begin(), s->captionInputs.end(),
                                                                     window) != s->captionInputs.end())) {
        if (m == WM_NCHITTEST && LightHit(s, {GET_X_LPARAM(l), GET_Y_LPARAM(l)}) >= 0)
            return HTCLIENT;
        if (m == WM_LBUTTONDOWN || m == WM_NCLBUTTONDOWN) {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (m == WM_LBUTTONDOWN)
                ClientToScreen(window, &p);
            int hit = LightHit(s, p);
            if (hit >= 0) {
                s->pressed = hit;
                SetCapture(window);
                return 0;
            }
        }
        if ((m == WM_LBUTTONUP || m == WM_NCLBUTTONUP) && s->pressed >= 0) {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (m == WM_LBUTTONUP)
                ClientToScreen(window, &p);
            int action = s->pressed;
            s->pressed = -1;
            if (GetCapture() == window)
                ReleaseCapture();
            if (LightHit(s, p) == action)
                Action(s->target, action);
            return 0;
        }
        if (m == WM_CAPTURECHANGED || m == WM_CANCELMODE)
            s->pressed = -1;
        if (window != s->target) {
            if (m == WM_NCDESTROY) {
                RemoveWindowSubclass(window, Subclass, SubclassId);
                std::erase(s->captionInputs, window);
            }
            return DefSubclassProc(window, m, w, l);
        }
    }
    if (window == s->bridge && s->layout.nativeCaption) {
        if (m == WM_WINDOWPOSCHANGING && !s->movingBridge) {
            auto p = reinterpret_cast<WINDOWPOS *>(l);
            if (!(p->flags & SWP_NOMOVE)) {
                s->bridgeShift = p->x;
                p->x = 0;
            }
            if (!(p->flags & SWP_NOSIZE))
                p->cx += s->bridgeShift;
        }
        if (m == WM_NCDESTROY) {
            Release(s);
            return DefSubclassProc(window, m, w, l);
        }
        return DefSubclassProc(window, m, w, l);
    }
    if (s->layout.nativeCaption && GetPropW(window, styler::style_view::StyleProperty) &&
        (m == WM_STYLECHANGING || m == WM_STYLECHANGED) && w == WPARAM(GWL_STYLE))
        return styler::style_view::MenuStyleMessage(window, m, w, l);
    if (window == s->drag) {
        if (m == WM_NCHITTEST) {
            auto hit = DefSubclassProc(window, m, w, l);
            return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE ? HTCAPTION : hit;
        }
        return DefSubclassProc(window, m, w, l);
    }
    if (m == TreeChanged()) {
        try {
            if (!s->layout.title.IsLoaded() || x::Media::VisualTreeHelper::GetParent(s->lights) != s->layout.title)
                Release(s);
        } catch (...) {
            Release(s);
        }
        return 0;
    }
    if (m == styler::QueryMessage())
        try {
            return s->layout.title.IsLoaded() && x::Media::VisualTreeHelper::GetParent(s->lights) == s->layout.title
                       ? styler::Acknowledged
                       : 0;
        } catch (...) {
            return 0;
        }
    if (m == WM_NCHITTEST) {
        auto hit = DefSubclassProc(window, m, w, l);
        return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE ? HTCAPTION : hit;
    }
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(w) != s->owner)
            return 0;
        if (s->commands)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (m == styler::DiagnosticMessage())
        try {
            auto r = LightRect(s);
            auto p = s->layout.content.TransformToVisual(s->layout.root).TransformPoint({0, 0});
            switch (static_cast<styler::Metric>(w)) {
            case styler::Metric::InlineLeft:
                return r.left;
            case styler::Metric::InlineTop:
                return r.top;
            case styler::Metric::InlineWidth:
                return r.right - r.left;
            case styler::Metric::InlineHeight:
                return r.bottom - r.top;
            case styler::Metric::InlineTabsLeft: {
                POINT origin{};
                MapWindowPoints(s->bridge, s->target, &origin, 1);
                return origin.x + LONG(std::lround(p.X * GetDpiForWindow(window) / 96));
            }
            case styler::Metric::InlineOriginalButtonsVisible:
                return s->layout.nativeCaption ? ((GetWindowLongPtrW(window, GWL_STYLE) & WS_SYSMENU) ? 3 : 0)
                                               : (s->layout.original.Visibility() == x::Visibility::Visible ? 3 : 0);
            case styler::Metric::InlineKind:
                return 4;
            default:
                return 0;
            }
        } catch (...) {
            return 0;
        }
    if (m == WM_TIMER && w == s->timer) {
        if (!IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT)
            Release(s);
        return 0;
    }
    if (m == WM_NCDESTROY) {
        Release(s);
        return DefSubclassProc(window, m, w, l);
    }
    if (m == WM_SYSCOMMAND) {
        ++s->commands;
        auto result = DefSubclassProc(window, m, w, l);
        DWORD_PTR live{};
        if (GetWindowSubclass(window, Subclass, SubclassId, &live))
            --reinterpret_cast<State *>(live)->commands;
        return result;
    }
    return DefSubclassProc(window, m, w, l);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    try {
        if (xaml_roots::Contents(context->window).empty()) {
            if (xaml_abi::Runtime())
                return ERROR_NOT_SUPPORTED;
            xaml_roots::Start();
            return GetTickCount64() - xaml_roots::startedAt.load() < 750 ? ERROR_RETRY : ERROR_NOT_SUPPORTED;
        }
        return Find(context->window).title ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    } catch (...) {
        return ERROR_NOT_SUPPORTED;
    }
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    auto *s = new State;
    s->target = context->window;
    s->owner = context->owner;
    try {
        s->layout = Find(s->target);
        if (!s->layout.title) {
            delete s;
            return ERROR_NOT_SUPPORTED;
        }
        DWORD pid{};
        GetWindowThreadProcessId(s->owner, &pid);
        s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!s->process) {
            delete s;
            return ERROR_ACCESS_DENIED;
        }
        s->bridge = s->layout.bridge;
        s->drag = s->layout.nativeCaption ? nullptr : CaptionInput(s->target, s->layout);
        if (!SetWindowSubclass(s->target, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)) ||
            (s->drag && !SetWindowSubclass(s->drag, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s))))
            throw hresult_error(E_FAIL);
        if (s->layout.nativeCaption) {
            POINT originalOrigin{};
            MapWindowPoints(s->bridge, s->target, &originalOrigin, 1);
            s->bridgeShift = originalOrigin.x;
            if (!SetWindowSubclass(s->bridge, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)))
                throw hresult_error(E_FAIL);
            RECT bounds{}, outer{};
            GetWindowRect(s->target, &outer);
            check_hresult(DwmGetWindowAttribute(s->target, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds)));
            int metrics[]{bounds.right - bounds.left, bounds.top, outer.right - outer.left - bounds.right,
                          bounds.bottom - bounds.top};
            for (int i = 0; i < 4; i++)
                if (!SetPropW(s->target, styler::style_view::BoundsProperties[i],
                              reinterpret_cast<HANDLE>(INT_PTR(metrics[i] + 1))))
                    throw hresult_error(E_OUTOFMEMORY);
            if (!styler::style_view::AcquireReads())
                throw hresult_error(E_FAIL);
            s->styleReads = true;
            if (!SetPropW(s->target, styler::style_view::StyleProperty, reinterpret_cast<HANDLE>(1)))
                throw hresult_error(E_OUTOFMEMORY);
            s->systemMenu = true;
            styler::style_view::HideMenu(s->target);
            RECT r{};
            GetWindowRect(s->bridge, &r);
            POINT p{r.left, r.top};
            MapWindowPoints(nullptr, s->target, &p, 1);
            if (p.x > 0)
                s->bridgeShift = p.x;
            s->movingBridge = true;
            SetWindowPos(s->bridge, nullptr, 0, p.y, r.right - r.left + p.x, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            s->movingBridge = false;
        } else {
            // Collapsing the tallest original control must not collapse the
            // title row and shift the application's body on first attach.
            s->saved.emplace_back(s->layout.title, x::FrameworkElement::MinHeightProperty());
            s->layout.title.MinHeight(std::max(s->layout.title.MinHeight(), s->layout.title.ActualHeight()));
            s->saved.emplace_back(s->layout.original, x::UIElement::VisibilityProperty());
            s->layout.original.Visibility(x::Visibility::Collapsed);
        }
        if (s->layout.nativeCaption) {
            // Keep the original parent, Loaded/Unloaded lifetime, and all host
            // column indexes intact. Padding reserves space INSIDE the Grid's
            // hit-test bounds; Margin would put transformed lights outside it.
            s->saved.emplace_back(s->layout.title, c::Grid::PaddingProperty());
            auto inset = s->layout.title.Padding();
            inset.Left += 84;
            s->layout.title.Padding(inset);
        } else
            for (auto child : s->layout.title.Children())
                if (auto e = child.try_as<x::FrameworkElement>()) {
                    s->saved.emplace_back(e, c::Grid::ColumnProperty());
                    c::Grid::SetColumn(e, c::Grid::GetColumn(e) + 1);
                }
        if (!s->layout.nativeCaption) {
            if (!s->layout.title.ColumnDefinitions().Size()) {
                s->implicitColumn = c::ColumnDefinition();
                s->layout.title.ColumnDefinitions().Append(s->implicitColumn);
            }
            s->column = c::ColumnDefinition();
            s->column.Width({84, x::GridUnitType::Pixel});
            s->layout.title.ColumnDefinitions().InsertAt(0, s->column);
        }
        s->lights = s->layout.captionResource.empty()
                        ? c::Grid()
                        : x::Markup::XamlReader::Load(
                              L"<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                              L"Background=\"{ThemeResource " +
                              s->layout.captionResource + L"}\"/>")
                              .as<c::Grid>();
        s->lights.Width(84);
        s->lights.HorizontalAlignment(x::HorizontalAlignment::Left);
        c::Grid::SetColumn(s->lights, 0);
        if (s->layout.nativeCaption) {
            if (s->layout.captionBrush)
                s->lights.Background(s->layout.captionBrush);
            x::Media::TranslateTransform shift;
            shift.X(-s->layout.title.Padding().Left);
            s->lights.RenderTransform(shift);
            c::Grid::SetColumnSpan(s->lights, std::max(1u, s->layout.title.ColumnDefinitions().Size()));
        }
        const winrt::Windows::UI::Color colors[]{{255, 255, 95, 87}, {255, 255, 189, 46}, {255, 40, 200, 64}};
        const wchar_t *names[]{L"Close window", L"Minimize window", L"Maximize or restore window"};
        for (int i = 0; i < 3; i++) {
            c::Button b;
            b.Width(20);
            b.Height(24);
            b.Padding({0, 0, 0, 0});
            b.BorderThickness({0, 0, 0, 0});
            b.HorizontalAlignment(x::HorizontalAlignment::Left);
            b.VerticalAlignment(x::VerticalAlignment::Center);
            b.Margin({double(8 + i * 20), 0, 0, 0});
            b.Background(x::Media::SolidColorBrush({0, 0, 0, 0}));
            b.IsEnabled(Enabled(s->target, i));
            x::Shapes::Ellipse dot;
            dot.Width(12);
            dot.Height(12);
            dot.Fill(x::Media::SolidColorBrush(colors[i]));
            b.Content(dot);
            x::Automation::AutomationProperties::SetName(b, names[i]);
            b.Click([target = s->target, i](auto const &, auto const &) { Action(target, i); });
            s->lights.Children().Append(b);
        }
        s->layout.title.Children().Append(s->lights);
        s->layout.root.UpdateLayout();
        if (s->layout.nativeCaption) {
            CaptionInputs(s);
            for (auto input : s->captionInputs)
                if (!SetWindowSubclass(input, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)))
                    throw hresult_error(E_FAIL);
        }
        s->timer = SetTimer(s->target, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
        if (!s->timer || !SetPropW(s->target, styler::OwnerProperty, s->owner))
            throw hresult_error(E_OUTOFMEMORY);
        // Register only after saved has reached its final size: callbacks hold
        // addresses of these entries, and are removed before any restoration.
        for (auto &saved : s->saved) {
            if (saved.property == x::UIElement::VisibilityProperty())
                saved.Watch([](auto const &) { return box_value(x::Visibility::Collapsed); });
            if (saved.property == c::Grid::PaddingProperty())
                saved.Watch([s](auto const &v) {
                    auto p = unbox_value<x::Thickness>(v);
                    p.Left += 84;
                    s->lights.RenderTransform().as<x::Media::TranslateTransform>().X(-p.Left);
                    return box_value(p);
                });
        }
        s->unloadToken = s->layout.title.Unloaded(
            [window = s->target](auto const &, auto const &) { PostMessageW(window, TreeChanged(), 0, 0); });
        s->unloadWatched = true;
        SetPropW(s->target, styler::AdapterProperty, reinterpret_cast<HANDLE>(context->adapterId));
        SetPropW(s->target, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(4)));
        return ERROR_SUCCESS;
    } catch (...) {
        std::ostringstream line;
        line << "attach failure hr=" << std::hex << winrt::to_hresult();
        xaml_roots::Log(line.str());
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
}
