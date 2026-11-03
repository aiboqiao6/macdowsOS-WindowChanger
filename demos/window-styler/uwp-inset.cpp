#include "adapter-api.h"
#include "embedded-frame.h"
#include <commctrl.h>
#include <windowsx.h>
#include <CoreWindow.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cwctype>
#include <vector>
#include <fstream>
namespace {
using namespace winrt;
namespace x = Windows::UI::Xaml;
namespace c = x::Controls;
constexpr UINT_PTR SubclassId = 0x4d435557;
constexpr double Inset = 84;
void Debug(const char *stage, int button, POINT p = {}) {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Debug), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L'\\') + 1);
    if (GetFileAttributesW((directory + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::ofstream log(directory + L"uwp-input-" + std::to_wstring(GetCurrentProcessId()) + L".txt", std::ios::app);
    log << stage << " button=" << button << " xy=" << p.x << "," << p.y << "\n";
}
UINT TreeChanged() {
    return RegisterWindowMessageW(L"Macdows.WindowStyler.Uwp.TreeChanged.v11");
}
struct Layout {
    x::UIElement root{nullptr};
    c::Grid title{nullptr}, navigation{nullptr};
    x::FrameworkElement back{nullptr};
};
void Collect(x::DependencyObject const &node, x::UIElement const &root, std::wstring_view title,
             std::vector<c::Grid> &titles, std::vector<x::FrameworkElement> &backs, int depth = 0) {
    if (!node || depth > 18)
        return;
    if (auto e = node.try_as<x::FrameworkElement>(); e) {
        auto p = e.TransformToVisual(root).TransformPoint({0, 0});
        if (p.Y > 64 || e.Visibility() != x::Visibility::Visible)
            return;
        if (auto grid = e.try_as<c::Grid>(); grid && p.X >= 0 && p.X <= 128 && std::abs(p.Y) < 2 &&
                                             grid.ActualWidth() > 300 && grid.ActualHeight() >= 24 &&
                                             grid.ActualHeight() <= 64) {
            for (auto child : grid.Children())
                if (auto text = child.try_as<c::TextBlock>(); text && text.Visibility() == x::Visibility::Visible &&
                                                              std::wstring_view(text.Text()) == title &&
                                                              text.ActualWidth() > 0) {
                    titles.push_back(grid);
                    break;
                }
        }
        if (e.try_as<c::Primitives::ButtonBase>() && p.X >= 0 && p.X < 64 && p.Y >= 0 && p.Y < 32 &&
            e.ActualWidth() >= 16 && e.ActualWidth() <= 64 && e.ActualHeight() >= 16 && e.ActualHeight() <= 48) {
            std::wstring name = x::Automation::AutomationProperties::GetName(e).c_str();
            std::transform(name.begin(), name.end(), name.begin(), towlower);
            if (name == L"back" || name == L"go back" || name == L"返回" || name == L"后退")
                backs.push_back(e);
        }
    }
    int count = x::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++)
        Collect(x::Media::VisualTreeHelper::GetChild(node, i), root, title, titles, backs, depth + 1);
}
Layout Find(HWND target) {
    auto frame = reinterpret_cast<HWND>(GetPropW(target, styler::EmbeddedFrameProperty));
    if (!styler::EmbeddedCore(target, frame))
        return {};
    auto current = x::Window::Current();
    if (!current)
        return {};
    HWND core{};
    check_hresult(current.CoreWindow().as<ICoreWindowInterop>()->get_WindowHandle(&core));
    if (core != target)
        return {};
    Layout result;
    result.root = current.Content();
    if (!result.root)
        return {};
    wchar_t title[1024]{};
    GetWindowTextW(frame, title, 1024);
    std::vector<c::Grid> titles;
    std::vector<x::FrameworkElement> backs;
    Collect(result.root, result.root, title, titles, backs);
    if (titles.size() != 1 || backs.size() != 1)
        return {};
    result.title = titles[0];
    result.back = backs[0];
    for (auto node = x::Media::VisualTreeHelper::GetParent(result.back); node;
         node = x::Media::VisualTreeHelper::GetParent(node)) {
        auto e = node.try_as<x::FrameworkElement>();
        if (!e || e.ActualHeight() > 64 || e.ActualWidth() > 160)
            break;
        if (auto grid = e.try_as<c::Grid>(); grid && grid.ActualHeight() >= result.back.ActualHeight() &&
                                             grid.ActualWidth() >= result.back.ActualWidth())
            result.navigation = grid;
    }
    if (!result.navigation || result.navigation == result.title)
        return {};
    return result;
}
struct Saved {
    x::FrameworkElement object{nullptr};
    x::DependencyProperty property{nullptr};
    Windows::Foundation::IInspectable value{nullptr};
    x::Data::Binding binding{nullptr};
    int64_t token{};
    bool watching{}, enforcing{};
    std::function<Windows::Foundation::IInspectable(Windows::Foundation::IInspectable const &)> transform;
    Saved(x::FrameworkElement const &o, x::DependencyProperty const &p, decltype(transform) f)
        : object(o), property(p), transform(std::move(f)) {
        Save();
    }
    void Save() {
        value = object.ReadLocalValue(property);
        binding = nullptr;
        if (auto e = object.GetBindingExpression(property))
            binding = e.ParentBinding();
    }
    void Apply() {
        enforcing = true;
        try {
            object.SetValue(property, transform(object.GetValue(property)));
        } catch (...) {
            enforcing = false;
            throw;
        }
        enforcing = false;
    }
    void Watch() {
        token = object.RegisterPropertyChangedCallback(property, [this](auto const &, auto const &) {
            if (!enforcing) {
                try {
                    Save();
                    Apply();
                } catch (...) {
                    Debug("property exception", to_hresult());
                }
            }
        });
        watching = true;
    }
    void Stop() {
        if (watching)
            object.UnregisterPropertyChangedCallback(property, token);
        watching = false;
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
    HWND target{}, frame{}, owner{};
    HANDLE process{};
    UINT_PTR timer{};
    ULONGLONG started{};
    Layout layout;
    std::vector<Saved> saved;
    std::vector<std::pair<x::FrameworkElement, event_token>> unloads;
    Windows::UI::Core::CoreWindow input{nullptr};
    event_token down{}, up{}, cancel{};
    bool busy{}, pointerCaptured{};
    int pressed = -1;
};
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void Release(State *s) {
    s->busy = true;
    KillTimer(s->target, s->timer);
    RemoveWindowSubclass(s->target, Subclass, SubclassId);
    if (s->pressed >= 0 && GetCapture() == s->target)
        ReleaseCapture();
    try {
        if (s->down.value)
            s->input.PointerPressed(s->down);
    } catch (...) {
    }
    try {
        if (s->up.value)
            s->input.PointerReleased(s->up);
    } catch (...) {
    }
    try {
        if (s->cancel.value)
            s->input.PointerCaptureLost(s->cancel);
    } catch (...) {
    }
    try {
        if (s->pointerCaptured)
            s->input.ReleasePointerCapture();
    } catch (...) {
    }
    for (auto const &item : s->unloads)
        try {
            item.first.Unloaded(item.second);
        } catch (...) {
        }
    for (auto &item : s->saved)
        try {
            item.Stop();
        } catch (...) {
        }
    for (auto i = s->saved.rbegin(); i != s->saved.rend(); ++i)
        try {
            i->Restore();
        } catch (...) {
        }
    try {
        if (s->layout.root)
            s->layout.root.UpdateLayout();
    } catch (...) {
    }
    for (auto property :
         {styler::OwnerProperty, styler::InlineProperty, styler::AdapterProperty, styler::EmbeddedInsetProperty,
          styler::EmbeddedFrameProperty, styler::EmbeddedCaptionTopProperty, styler::EmbeddedCaptionHeightProperty})
        RemovePropW(s->target, property);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
bool Ready(State *s) {
    try {
        return !s->busy && s->layout.title.IsLoaded() && s->layout.navigation.IsLoaded() &&
               s->layout.back.TransformToVisual(s->layout.root).TransformPoint({0, 0}).X >= Inset;
    } catch (...) {
        return false;
    }
}
int Hit(State *s, POINT screen) {
    if (GetPropW(s->frame, styler::OwnerProperty) != s->owner)
        return -1;
    int top = int(reinterpret_cast<INT_PTR>(GetPropW(s->target, styler::EmbeddedCaptionTopProperty))) - 1;
    int height = int(reinterpret_cast<INT_PTR>(GetPropW(s->target, styler::EmbeddedCaptionHeightProperty))) - 1;
    ScreenToClient(s->frame, &screen);
    if (top < 0 || height <= 0 || screen.y < top || screen.y >= top + height)
        return -1;
    for (int i = 0; i < 3; i++)
        if (screen.x >= MulDiv(8 + i * 20, GetDpiForWindow(s->frame), 96) &&
            screen.x < MulDiv(28 + i * 20, GetDpiForWindow(s->frame), 96))
            return i;
    return -1;
}
bool Enabled(State *s, int button) {
    if (!IsWindowEnabled(s->frame))
        return false;
    auto style = GetWindowLongPtrW(s->frame, GWL_STYLE);
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(s->frame, FALSE);
    auto flags = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
    return flags == UINT(-1) || !(flags & (MF_DISABLED | MF_GRAYED));
}
void Action(State *s, int button) {
    Debug("action", button);
    if (Enabled(s, button))
        PostMessageW(s->frame, styler::EmbeddedActionMessage(), button, 0);
}
POINT PointerPosition(State *s, Windows::UI::Core::PointerEventArgs const &args) {
    auto p = args.CurrentPoint().Position();
    auto dpi = GetDpiForWindow(s->target);
    POINT screen{LONG(std::lround(p.X * dpi / 96.0)), LONG(std::lround(p.Y * dpi / 96.0))};
    ClientToScreen(s->target, &screen);
    return screen;
}
void WatchInput(State *s) {
    s->input = x::Window::Current().CoreWindow();
    s->down = s->input.PointerPressed([s](auto const &, Windows::UI::Core::PointerEventArgs const &args) {
        try {
            auto p = PointerPosition(s, args);
            int hit = Hit(s, p);
            Debug("pointer down", hit, p);
            if (hit >= 0 && Enabled(s, hit)) {
                s->pressed = hit;
                args.Handled(true);
                s->input.SetPointerCapture();
                s->pointerCaptured = true;
            }
        } catch (...) {
            Debug("pointer down exception", to_hresult());
        }
    });
    s->up = s->input.PointerReleased([s](auto const &, Windows::UI::Core::PointerEventArgs const &args) {
        try {
            if (s->pressed < 0)
                return;
            int action = s->pressed;
            s->pressed = -1;
            auto screen = PointerPosition(s, args);
            args.Handled(true);
            if (s->pointerCaptured) {
                s->pointerCaptured = false;
                s->input.ReleasePointerCapture();
            }
            if (Hit(s, screen) == action)
                Action(s, action);
        } catch (...) {
            Debug("pointer up exception", to_hresult());
        }
    });
    s->cancel = s->input.PointerCaptureLost([s](auto const &, auto const &) {
        s->pressed = -1;
        s->pointerCaptured = false;
    });
}
LRESULT CALLBACK Subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto s = reinterpret_cast<State *>(ref);
    // A cross-process CoreWindow owns mouse dispatch over a custom title area.
    // Route only the new caption controls from its ORIGINAL window procedure;
    // no input sink, overlay HWND or synthetic cursor tracking is created.
    if (m == WM_LBUTTONDOWN || m == WM_NCLBUTTONDOWN || m == WM_LBUTTONDBLCLK) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (m != WM_NCLBUTTONDOWN)
            ClientToScreen(w, &p);
        int hit = Hit(s, p);
        if (hit >= 0 && Enabled(s, hit)) {
            s->pressed = hit;
            SetCapture(w);
            return 0;
        }
    }
    if ((m == WM_LBUTTONUP || m == WM_NCLBUTTONUP) && s->pressed >= 0) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (m == WM_LBUTTONUP)
            ClientToScreen(w, &p);
        int action = s->pressed;
        s->pressed = -1;
        if (GetCapture() == w)
            ReleaseCapture();
        if (Hit(s, p) == action)
            Action(s, action);
        return 0;
    }
    if (m == WM_CAPTURECHANGED || m == WM_CANCELMODE)
        s->pressed = -1;
    if (m == styler::QueryMessage())
        return Ready(s) ? styler::Acknowledged : 0;
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(wp) != s->owner)
            return 0;
        if (s->busy)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (m == TreeChanged()) {
        if (!Ready(s))
            Release(s);
        return 0;
    }
    if (m == WM_TIMER && wp == s->timer) {
        if (!Ready(s) || !styler::EmbeddedCore(w, s->frame) || !IsWindow(s->owner) ||
            WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT ||
            (GetTickCount64() - s->started > 3000 && GetPropW(s->frame, styler::OwnerProperty) != s->owner))
            Release(s);
        return 0;
    }
    if (m == WM_NCDESTROY) {
        Release(s);
        return DefSubclassProc(w, m, wp, lp);
    }
    return DefSubclassProc(w, m, wp, lp);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    try {
        return Find(c->window).title ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    } catch (...) {
        return ERROR_NOT_SUPPORTED;
    }
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    auto s = new State;
    s->target = c->window;
    s->owner = c->owner;
    s->frame = reinterpret_cast<HWND>(GetPropW(c->window, styler::EmbeddedFrameProperty));
    s->started = GetTickCount64();
    try {
        s->layout = Find(c->window);
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
        if (!SetWindowSubclass(s->target, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)))
            throw hresult_error(E_FAIL);
        s->busy = true;
        // Title text already honors CoreApplication's reserved left metrics. Only
        // reserve the original NavigationView header's back-button container here.
        s->saved.emplace_back(s->layout.navigation, c::Grid::PaddingProperty(), [](auto const &value) {
            auto p = unbox_value<x::Thickness>(value);
            p.Left += Inset;
            return box_value(p);
        });
        double minimum = s->layout.navigation.ActualWidth();
        s->saved.emplace_back(
            s->layout.navigation, x::FrameworkElement::MinWidthProperty(),
            [minimum](auto const &value) { return box_value(std::max(unbox_value<double>(value), minimum) + Inset); });
        s->saved.emplace_back(s->layout.navigation, x::FrameworkElement::WidthProperty(), [](auto const &value) {
            auto width = unbox_value<double>(value);
            return box_value(std::isfinite(width) ? width + Inset : width);
        });
        for (auto &item : s->saved)
            item.Apply();
        s->layout.root.UpdateLayout();
        s->busy = false;
        if (!Ready(s))
            throw hresult_error(E_NOTIMPL);
        for (auto &item : s->saved)
            item.Watch();
        for (auto const &element :
             {x::FrameworkElement(s->layout.title), x::FrameworkElement(s->layout.navigation), s->layout.back})
            s->unloads.emplace_back(element, element.Unloaded([w = s->target](auto const &, auto const &) {
                PostMessageW(w, TreeChanged(), 0, 0);
            }));
        WatchInput(s);
        s->timer = SetTimer(s->target, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
        if (!s->timer || !SetPropW(s->target, styler::OwnerProperty, s->owner) ||
            !SetPropW(s->target, styler::EmbeddedInsetProperty, reinterpret_cast<HANDLE>(UINT_PTR(Inset))) ||
            !SetPropW(s->target, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId)) ||
            !SetPropW(s->target, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(7))))
            throw hresult_error(E_OUTOFMEMORY);
        return ERROR_SUCCESS;
    } catch (...) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
}
