#include "adapter-api.h"
#include "pe-identity.h"
#include "nonclient-frame.h"
#include "native-style-view.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <array>
#include <set>
#include <string>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
constexpr UINT_PTR SubclassId = 0x4d434e55;
BYTE *module{};
template <class T> T Fn(size_t rva) {
    return reinterpret_cast<T>(module + rva);
}
using namespace styler::style_view;
bool Readable(const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION i{};
    return p && VirtualQuery(p, &i, sizeof(i)) && i.State == MEM_COMMIT &&
           !(i.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && uintptr_t(p) + n <= uintptr_t(i.BaseAddress) + i.RegionSize;
}
bool Runtime() {
    module = reinterpret_cast<BYTE *>(GetModuleHandleW(L"UIRibbon.dll"));
    // Library ABI guard. No host executable, process name, directory or HWND list.
    constexpr GUID id{0xfee19cf7, 0x23b1, 0xa0ad, {0x09, 0xac, 0x63, 0xe3, 0xf2, 0x30, 0xb7, 0x85}};
    return styler::MatchesPdb(reinterpret_cast<HMODULE>(module), id);
}
std::string Type(void *p) {
    if (!Readable(p, 32))
        return {};
    auto v = *reinterpret_cast<BYTE ***>(p);
    if (!Readable(v - 1, 16))
        return {};
    auto col = reinterpret_cast<DWORD *>(v[-1]);
    if (!Readable(col, 24) || col[0] != 1)
        return {};
    const char *name = reinterpret_cast<const char *>(module + col[3] + 16);
    if (!Readable(name, 256))
        return {};
    return std::string(name, strnlen(name, 255));
}
RECT Rect(void *p) {
    RECT r{};
    Fn<void *(*)(void *, RECT *)>(0xcbc4)(p, &r);
    return r;
}
struct Layout {
    HWND netui{};
    void *root{};
    void *title{};
    int titles{};
};
void Walk(void *node, Layout &layout, int depth, std::set<void *> &seen) {
    if (!node || depth > 25 || seen.size() > 5000 || !seen.insert(node).second)
        return;
    auto type = Type(node);
    if (type.empty())
        return;
    if (type == ".?AVAppFrameHelper@NetUI@@" && Fn<int (*)(void *)>(0x476b8)(node) == 2) {
        auto r = Rect(node);
        if (r.left == 0 && r.top == 0 && r.right > 300 && r.bottom >= 20 && r.bottom <= 100) {
            layout.title = node;
            ++layout.titles;
        }
    }
    for (int i = 0; i < 160; i++) {
        auto child = Fn<void *(*)(void *, int)>(0x1615c8)(node, i);
        if (!child)
            break;
        Walk(child, layout, depth + 1, seen);
    }
}
Layout Find(HWND target) {
    Layout result{};
    if (!Runtime())
        return result;
    EnumChildWindows(
        target,
        [](HWND w, LPARAM p) -> BOOL {
            wchar_t cls[80]{};
            GetClassNameW(w, cls, 80);
            if (wcscmp(cls, L"NetUIHWND") != 0 || !IsWindowVisible(w) ||
                GetWindowThreadProcessId(w, nullptr) != GetCurrentThreadId())
                return TRUE;
            Layout candidate;
            candidate.netui = w;
            candidate.root = reinterpret_cast<void *>(GetWindowLongPtrW(w, 0));
            if (Type(candidate.root) != ".?AVHWNDElement@NetUI@@")
                return TRUE;
            std::set<void *> seen;
            Walk(candidate.root, candidate, 0, seen);
            if (candidate.titles == 1 && Fn<void *(*)(void *, int)>(0x1615c8)(candidate.title, 0)) {
                *reinterpret_cast<Layout *>(p) = candidate;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
struct State {
    HWND target{}, owner{};
    Layout layout;
    HANDLE process{};
    UINT_PTR timer{};
    void *spacer{};
    void **originalVtable{};
    std::array<void *, 65> vtable{};
    BOOL originalNc{};
    bool changing{}, systemMenu{}, styleReads{};
    int pressed = -1;
    unsigned commands{}, paints{};
};
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
State *Live(HWND w) {
    DWORD_PTR p{};
    return GetWindowSubclass(w, Subclass, SubclassId, &p) ? reinterpret_cast<State *>(p) : nullptr;
}
LRESULT Original(State *s, HWND w, UINT m, WPARAM wp, LPARAM lp) {
    (void)s;
    return DefSubclassProc(w, m, wp, lp);
}
int Scale(HWND w, int n) {
    return MulDiv(n, GetDpiForWindow(w), 96);
}
RECT Lights(State *s) {
    if (!s->spacer)
        return {};
    auto r = Rect(s->spacer);
    POINT p{r.left, r.top};
    MapWindowPoints(s->layout.netui, s->target, &p, 1);
    return {p.x, p.y, p.x + r.right - r.left, p.y + r.bottom - r.top};
}
bool Enabled(State *s, int button) {
    if (!IsWindowEnabled(s->target))
        return false;
    auto style = GetWindowLongPtrW(s->target, GWL_STYLE);
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(s->target, FALSE);
    auto flags = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
    return flags == UINT(-1) || !(flags & (MF_DISABLED | MF_GRAYED));
}
// Paint in the target's existing HWND paint dispatch. The NetUI spacer takes
// part in the ORIGINAL title-row layout. No surface covers existing controls;
// only the three circles occupy the title row, with no new HWND or title band.
void Draw(State *s) {
    if (!s->spacer || s->changing || IsIconic(s->target))
        return;
    auto r = Lights(s);
    if (r.right - r.left < Scale(s->target, 80))
        return;
    POINT origin{};
    ClientToScreen(s->target, &origin);
    RECT outer{};
    GetWindowRect(s->target, &outer);
    OffsetRect(&r, origin.x - outer.left, origin.y - outer.top);
    auto dc = GetWindowDC(s->target);
    if (!dc)
        return;
    int diameter = Scale(s->target, 12), y = r.top + (r.bottom - r.top - diameter) / 2;
    // DWM retains the original resize margins and rounded corners. Only the
    // original title-row spacer is drawn; never make its transparent margins opaque.
    constexpr COLORREF colors[] = {RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
    for (int button = 0; button < 3; button++) {
        int x = r.left + Scale(s->target, 12 + button * 20);
        RECT dot{x, y, x + diameter, y + diameter};
        HDC bufferDc{};
        auto buffer = BeginBufferedPaint(dc, &dot, BPBF_TOPDOWNDIB, nullptr, &bufferDc);
        if (!buffer)
            continue;
        BitBlt(bufferDc, x, y, diameter, diameter, dc, x, y, SRCCOPY);
        RGBQUAD *bits{};
        int stride{};
        if (SUCCEEDED(GetBufferedPaintBits(buffer, &bits, &stride))) {
            auto color = Enabled(s, button) ? colors[button] : RGB(128, 128, 128);
            const double radius = diameter / 2.0;
            for (int py = 0; py < diameter; py++)
                for (int px = 0; px < diameter; px++) {
                    int samples = 0;
                    for (int sy = 0; sy < 4; sy++)
                        for (int sx = 0; sx < 4; sx++) {
                            double dx = px + (sx + .5) / 4 - radius, dy = py + (sy + .5) / 4 - radius;
                            if (dx * dx + dy * dy <= radius * radius)
                                ++samples;
                        }
                    if (!samples)
                        continue;
                    auto &pixel = bits[py * stride + px];
                    auto blend = [&](BYTE old, BYTE value) {
                        return BYTE((old * (16 - samples) + value * samples + 8) / 16);
                    };
                    pixel.rgbRed = blend(pixel.rgbRed, GetRValue(color));
                    pixel.rgbGreen = blend(pixel.rgbGreen, GetGValue(color));
                    pixel.rgbBlue = blend(pixel.rgbBlue, GetBValue(color));
                    pixel.rgbReserved = blend(pixel.rgbReserved, 255);
                }
        }
        EndBufferedPaint(buffer, TRUE);
    }
    ReleaseDC(s->target, dc);
    ++s->paints;
}
void Erase(State *s) {
    if (!s->spacer || !IsWindow(s->target))
        return;
    auto r = Lights(s);
    POINT origin{};
    RECT outer{};
    ClientToScreen(s->target, &origin);
    GetWindowRect(s->target, &outer);
    OffsetRect(&r, origin.x - outer.left, origin.y - outer.top);
    int diameter = Scale(s->target, 12), y = r.top + (r.bottom - r.top - diameter) / 2;
    auto dc = GetWindowDC(s->target);
    if (!dc)
        return;
    BP_PAINTPARAMS params{sizeof(params)};
    params.dwFlags = BPPF_NOCLIP | BPPF_ERASE;
    for (int i = 0; i < 3; i++) {
        int x = r.left + Scale(s->target, 12 + i * 20);
        RECT dot{x, y, x + diameter, y + diameter};
        HDC temp{};
        auto buffer = BeginBufferedPaint(dc, &dot, BPBF_TOPDOWNDIB, &params, &temp);
        if (buffer) {
            BufferedPaintClear(buffer, nullptr);
            BufferedPaintSetAlpha(buffer, nullptr, 0);
            EndBufferedPaint(buffer, TRUE);
        }
    }
    ReleaseDC(s->target, dc);
}
// The owned spacer can be destroyed when a host rebuilds its title tree. Track
// that event before its memory is freed; never poll a stale element pointer.
void SpacerDestroyed(void *node) {
    // Find the owner through HWND properties rather than a global object map.
    struct Search {
        void *node;
        State *state{};
    } search{node};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND w, LPARAM p) -> BOOL {
            auto &a = *reinterpret_cast<Search *>(p);
            auto s = Live(w);
            if (s && s->spacer == a.node) {
                a.state = s;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (auto s = search.state) {
        *reinterpret_cast<void ***>(node) = s->originalVtable;
        s->spacer = nullptr;
    }
    Fn<void (*)(void *)>(0x28390)(node);
}
void Refresh(State *s) {
    RedrawWindow(s->target, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
void Release(State *s, bool destroyed = false, bool treeDestroyed = false) {
    s->changing = true;
    if (!destroyed && !treeDestroyed)
        Erase(s);
    KillTimer(s->target, s->timer);
    RemoveWindowSubclass(s->target, Subclass, SubclassId);
    RemoveWindowSubclass(s->layout.netui, Subclass, SubclassId);
    if (s->pressed >= 0 && (GetCapture() == s->layout.netui || GetCapture() == s->target))
        ReleaseCapture();
    if (s->spacer) {
        *reinterpret_cast<void ***>(s->spacer) = s->originalVtable;
        if (!destroyed && !treeDestroyed) {
            Fn<void (*)()>(0xbfa64)();
            Fn<HRESULT (*)(void *, void *)>(0x161940)(s->layout.title, s->spacer);
            Fn<void (*)(void *, bool)>(0xc6608)(s->spacer, true);
            Fn<void (*)()>(0x16570)();
        }
    }
    if (!destroyed && s->systemMenu)
        RestoreMenu(s->target, true);
    RemovePropW(s->target, StyleProperty);
    if (s->styleReads)
        ReleaseReads();
    for (auto property : BoundsProperties)
        RemovePropW(s->target, property);
    RemovePropW(s->target, styler::OwnerProperty);
    RemovePropW(s->target, styler::InlineProperty);
    RemovePropW(s->target, styler::AdapterProperty);
    if (!destroyed)
        Refresh(s);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
int Hit(State *s, POINT screen) {
    ScreenToClient(s->target, &screen);
    auto r = Lights(s);
    if (screen.y < r.top || screen.y >= r.bottom)
        return -1;
    int x = screen.x - r.left;
    for (int i = 0; i < 3; i++)
        if (x >= Scale(s->target, 8 + i * 20) && x < Scale(s->target, 28 + i * 20))
            return i;
    return -1;
}
LRESULT CALLBACK Subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto s = reinterpret_cast<State *>(ref);
    HWND target = s->target;
    if (w == target && GetPropW(w, StyleProperty) && (m == WM_STYLECHANGING || m == WM_STYLECHANGED) &&
        wp == WPARAM(GWL_STYLE)) {
        return MenuStyleMessage(w, m, wp, lp);
    }
    if (w == target && styler::NonclientFrame::Filter(w, m, wp, lp))
        return 0;
    if (w == target) {
        if (m == styler::NonclientFrame::StateMessage()) {
            if (Enabled(s, 2)) {
                ++s->commands;
                styler::NonclientFrame::ToggleMaximize(w);
                if (auto live = Live(w))
                    --live->commands;
            }
            return 0;
        }
        if (m == styler::QueryMessage())
            return s->spacer && !s->changing && s->paints ? styler::Acknowledged : 0;
        if (m == styler::DetachMessage()) {
            if (reinterpret_cast<HWND>(wp) != s->owner)
                return 0;
            if (s->commands || s->changing)
                return styler::TransitionBusy;
            Release(s);
            return styler::Acknowledged;
        }
        if (m == styler::DiagnosticMessage()) {
            auto r = Lights(s);
            switch (static_cast<styler::Metric>(wp)) {
            case styler::Metric::InlineLeft:
                return r.left;
            case styler::Metric::InlineTop:
                return r.top;
            case styler::Metric::InlineWidth:
                return r.right - r.left;
            case styler::Metric::InlineHeight:
                return r.bottom - r.top;
            case styler::Metric::InlineTabsLeft:
                return r.right;
            case styler::Metric::InlineKind:
                return 6;
            case styler::Metric::InlineOriginalButtonsVisible:
                return (GetWindowLongPtrW(w, GWL_STYLE) & WS_SYSMENU) != 0;
            case styler::Metric::CaptionPaints:
                return s->paints;
            default:
                return 0;
            }
        }
        if (m == WM_TIMER && wp == s->timer) {
            if (!s->spacer || !IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT)
                Release(s);
            return 0;
        }
        if (m == WM_SYSCOMMAND) {
            ++s->commands;
            auto result = Original(s, w, m, wp, lp);
            if (auto live = Live(w))
                --live->commands;
            return result;
        }
    }
    if (m == WM_NCDESTROY) {
        Release(s, w == target, true);
        return DefSubclassProc(w, m, wp, lp);
    }
    if (m == WM_NCHITTEST) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (Hit(s, p) >= 0)
            return HTCLIENT;
        auto result = Original(s, w, m, wp, lp);
        return result == HTCLOSE || result == HTMINBUTTON || result == HTMAXBUTTON ? HTCAPTION : result;
    }
    if (m == WM_LBUTTONDOWN || m == WM_NCLBUTTONDOWN) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (m == WM_LBUTTONDOWN)
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
        if (action == Hit(s, p) && Enabled(s, action)) {
            if (action == 2)
                PostMessageW(target, styler::NonclientFrame::StateMessage(), 0, 0);
            else
                PostMessageW(target, WM_SYSCOMMAND, action == 0 ? SC_CLOSE : SC_MINIMIZE, 0);
        }
        return 0;
    }
    if (m == WM_CAPTURECHANGED || m == WM_CANCELMODE)
        s->pressed = -1;
    if (m == WM_PAINT || m == WM_NCPAINT || m == WM_NCACTIVATE || m == WM_SIZE || m == WM_WINDOWPOSCHANGED ||
        m == 0xae || m == 0xaf) {
        auto result = Original(s, w, m, wp, lp);
        if (auto live = Live(target); live == s)
            Draw(live);
        return result;
    }
    return Original(s, w, m, wp, lp);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    BOOL nc{};
    if (FAILED(DwmGetWindowAttribute(c->window, DWMWA_NCRENDERING_ENABLED, &nc, sizeof(nc))) || !nc)
        return ERROR_NOT_SUPPORTED;
    return Find(c->window).title ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    if (Live(c->window))
        return ERROR_ALREADY_EXISTS;
    auto layout = Find(c->window);
    if (!layout.title)
        return ERROR_NOT_SUPPORTED;
    auto s = new State;
    s->target = c->window;
    s->owner = c->owner;
    s->layout = layout;
    s->changing = true;
    DWORD pid{};
    GetWindowThreadProcessId(s->owner, &pid);
    s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->process) {
        delete s;
        return ERROR_ACCESS_DENIED;
    }
    if (!SetWindowSubclass(s->target, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)) ||
        !SetWindowSubclass(layout.netui, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s))) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    if (FAILED(DwmGetWindowAttribute(s->target, DWMWA_NCRENDERING_ENABLED, &s->originalNc, sizeof(s->originalNc)))) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    if (FAILED(Fn<HRESULT (*)(unsigned, void **)>(0xbdad0)(0, &s->spacer)) || !s->spacer) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    s->originalVtable = *reinterpret_cast<void ***>(s->spacer);
    if (s->originalVtable != reinterpret_cast<void **>(module + 0x240d98)) {
        Fn<void (*)(void *, bool)>(0xc6608)(s->spacer, true);
        s->spacer = nullptr;
        Release(s);
        return ERROR_REVISION_MISMATCH;
    }
    std::copy_n(s->originalVtable - 1, s->vtable.size(), s->vtable.begin());
    s->vtable[1 + 0x38 / 8] = reinterpret_cast<void *>(&SpacerDestroyed);
    *reinterpret_cast<void ***>(s->spacer) = s->vtable.data() + 1;
    auto row = Rect(layout.title);
    int height = MulDiv(row.bottom - row.top, 96, GetDpiForWindow(s->target));
    Fn<void (*)()>(0xbfa64)();
    HRESULT hr = Fn<HRESULT (*)(void *, int, int)>(0x1a790)(s->spacer, 84, 0);
    if (SUCCEEDED(hr))
        hr = Fn<HRESULT (*)(void *, int, int)>(0x30798)(s->spacer, height, 0);
    auto first = Fn<void *(*)(void *, int)>(0x1615c8)(layout.title, 0);
    if (SUCCEEDED(hr))
        hr = Fn<HRESULT (*)(void *, void *, void *)>(0x1618f8)(layout.title, s->spacer, first);
    Fn<void (*)()>(0x16570)();
    s->systemMenu = (GetWindowLongPtrW(s->target, GWL_STYLE) & WS_SYSMENU) != 0;
    if (s->systemMenu) {
        RECT bounds{}, outer{};
        GetWindowRect(s->target, &outer);
        // Resolve the framework's own delayed DWM import before installing its read
        // shim. The original caption geometry remains available to its layout code.
        if (FAILED(Fn<decltype(&DwmGetWindowAttribute)>(0xc87fa)(s->target, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds,
                                                                 sizeof(bounds))) ||
            bounds.right <= bounds.left) {
            Release(s);
            return ERROR_NOT_SUPPORTED;
        }
        int metrics[]{bounds.right - bounds.left, bounds.top, outer.right - outer.left - bounds.right,
                      bounds.bottom - bounds.top};
        for (int i = 0; i < 4; i++)
            if (!SetPropW(s->target, BoundsProperties[i], reinterpret_cast<HANDLE>(INT_PTR(metrics[i] + 1)))) {
                Release(s);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
        if (!AcquireReads()) {
            Release(s);
            return ERROR_NOT_SUPPORTED;
        }
        s->styleReads = true;
        if (!SetPropW(s->target, StyleProperty, reinterpret_cast<HANDLE>(1))) {
            Release(s);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    HideMenu(s->target);
    s->changing = false;
    if (FAILED(hr) || Lights(s).right - Lights(s).left < Scale(s->target, 80)) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    s->timer = SetTimer(s->target, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
    if (!s->timer || !SetPropW(s->target, styler::OwnerProperty, s->owner)) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SetPropW(s->target, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(6)));
    SetPropW(s->target, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId));
    Refresh(s);
    if (auto live = Live(c->window))
        Draw(live);
    return Live(c->window) ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
