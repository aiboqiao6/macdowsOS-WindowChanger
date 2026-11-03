#include "adaptive.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <ole2.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>

namespace styler {
namespace {
constexpr wchar_t OverlayClass[] = L"Macdows.WindowStyler.Surface.v3";
int Scale(int n, UINT dpi) {
    return MulDiv(n, dpi, 96);
}
struct Surface {
    HWND target{}, window{};
    bool mask{}, compact{}, dark{};
    COLORREF background = RGB(32, 32, 32);
    int pressed = -1;
};
RECT Dot(HWND window, int index) {
    RECT r{};
    GetClientRect(window, &r);
    UINT dpi = GetDpiForWindow(window);
    int d = Scale(12, dpi), x = Scale(12 + index * 20, dpi), y = (r.bottom - d) / 2;
    return {x, y, x + d, y + d};
}
int Hit(HWND window, POINT p) {
    for (int i = 0; i < 3; ++i) {
        RECT r = Dot(window, i);
        InflateRect(&r, Scale(4, GetDpiForWindow(window)), Scale(5, GetDpiForWindow(window)));
        if (PtInRect(&r, p))
            return i;
    }
    return -1;
}
bool Enabled(HWND target, int button) {
    auto style = GetWindowLongPtrW(target, GWL_STYLE);
    if (!IsWindowEnabled(target))
        return false;
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    HMENU menu = GetSystemMenu(target, FALSE);
    UINT state = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : static_cast<UINT>(-1);
    // Custom captions (e.g. Settings) can omit SC_CLOSE from the system menu.
    // Missing metadata is different from an explicitly disabled close command.
    return state == static_cast<UINT>(-1) || !(state & (MF_DISABLED | MF_GRAYED));
}
LRESULT CALLBACK SurfaceProc(HWND window, UINT msg, WPARAM param, LPARAM data) {
    auto *s = reinterpret_cast<Surface *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        s = reinterpret_cast<Surface *>(reinterpret_cast<CREATESTRUCTW *>(data)->lpCreateParams);
        s->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s)
        return DefWindowProcW(window, msg, param, data);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC out = BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        HDC dc = CreateCompatibleDC(out);
        HBITMAP bitmap = CreateCompatibleBitmap(out, std::max(1L, r.right), std::max(1L, r.bottom));
        auto oldBitmap = SelectObject(dc, bitmap);
        COLORREF bg = s->mask ? s->background : s->dark ? RGB(40, 40, 42) : RGB(244, 243, 241);
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        if (!s->mask) {
            constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
            constexpr COLORREF borders[]{RGB(222, 73, 65), RGB(218, 159, 35), RGB(30, 166, 48)};
            for (int i = 0; i < 3; ++i) {
                bool enabled = Enabled(s->target, i);
                auto dot = Dot(window, i);
                brush = CreateSolidBrush(enabled ? colors[i] : RGB(128, 128, 128));
                HPEN pen = CreatePen(PS_SOLID, 1, enabled ? borders[i] : RGB(110, 110, 110));
                auto oldBrush = SelectObject(dc, brush);
                auto oldPen = SelectObject(dc, pen);
                Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
            }
            if (!s->compact) {
                wchar_t title[1024]{};
                GetWindowTextW(s->target, title, 1024);
                HFONT font = CreateFontW(-Scale(12, GetDpiForWindow(window)), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                auto oldFont = SelectObject(dc, font);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, s->dark ? RGB(232, 232, 234) : RGB(58, 57, 55));
                RECT text = r;
                text.left += Scale(86, GetDpiForWindow(window));
                text.right -= Scale(86, GetDpiForWindow(window));
                DrawTextW(dc, title, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                SelectObject(dc, oldFont);
                DeleteObject(font);
            }
        }
        BitBlt(out, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (s->mask)
            return 0;
        POINT p{GET_X_LPARAM(data), GET_Y_LPARAM(data)};
        int hit = Hit(window, p);
        if (hit >= 0) {
            if (Enabled(s->target, hit)) {
                s->pressed = hit;
                SetCapture(window);
            }
            return 0;
        }
        POINT screen = p;
        ClientToScreen(window, &screen);
        SetForegroundWindow(s->target);
        PostMessageW(s->target, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen.x, screen.y));
        return 0;
    }
    case WM_LBUTTONUP: {
        int pressed = s->pressed;
        s->pressed = -1;
        if (GetCapture() == window)
            ReleaseCapture();
        POINT p{GET_X_LPARAM(data), GET_Y_LPARAM(data)};
        if (pressed >= 0 && Hit(window, p) == pressed && Enabled(s->target, pressed))
            PostMessageW(s->target, WM_SYSCOMMAND,
                         pressed == 0          ? SC_CLOSE
                         : pressed == 1        ? SC_MINIMIZE
                         : IsZoomed(s->target) ? SC_RESTORE
                                               : SC_MAXIMIZE,
                         0);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (!s->mask && Enabled(s->target, 2))
            PostMessageW(s->target, WM_SYSCOMMAND, IsZoomed(s->target) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return 0;
    case WM_RBUTTONUP: {
        POINT p{GET_X_LPARAM(data), GET_Y_LPARAM(data)};
        ClientToScreen(window, &p);
        PostMessageW(s->target, WM_SYSCOMMAND, SC_MOUSEMENU, MAKELPARAM(p.x, p.y));
        return 0;
    }
    case WM_CAPTURECHANGED:
        s->pressed = -1;
        return 0;
    case WM_NCDESTROY:
        s->window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, msg, param, data);
}
HWND CreateSurface(Surface &s) {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = SurfaceProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = OverlayClass;
    cls.style = CS_DBLCLKS;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&cls);
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, OverlayClass, L"", WS_POPUP, 0, 0, 1,
                             1, s.target, nullptr, cls.hInstance, &s);
    if (w)
        SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA);
    return w;
}
void Position(HWND overlay, HWND target, const RECT &rect) {
    if (!overlay)
        return;
    RECT old{};
    GetWindowRect(overlay, &old);
    HWND above = GetWindow(target, GW_HWNDPREV);
    UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    if (above == overlay)
        flags |= SWP_NOZORDER;
    if (EqualRect(&old, &rect) && IsWindowVisible(overlay) && (flags & SWP_NOZORDER))
        return;
    SetWindowPos(overlay, above == overlay ? nullptr : above, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, flags);
}
bool ValidButtons(const RECT &r, const RECT &outer, UINT dpi) {
    return r.right > r.left && r.bottom > r.top && r.left > outer.left + (outer.right - outer.left) / 2 &&
           r.right <= outer.right + 2 && r.top >= outer.top && r.top < outer.top + Scale(70, dpi) &&
           r.bottom - r.top <= Scale(60, dpi);
}
// UIA is a read-only fallback for custom title controls with no DWM bounds.
RECT AccessibleButtons(HWND target, const RECT &outer, UINT dpi) {
    using Microsoft::WRL::ComPtr;
    RECT result{};
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))))
        return result;
    ComPtr<IUIAutomation2> timed;
    if (SUCCEEDED(automation.As(&timed))) {
        timed->put_ConnectionTimeout(200);
        timed->put_TransactionTimeout(350);
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(target, &root)))
        return result;
    ComPtr<IUIAutomationCondition> names;
    const wchar_t *labels[]{L"最小化",   L"最大化",   L"还原",    L"关闭",  L"关闭窗口",
                            L"Minimize", L"Maximize", L"Restore", L"Close", L"Close window"};
    for (auto label : labels) {
        VARIANT v{};
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(label);
        ComPtr<IUIAutomationCondition> one;
        automation->CreatePropertyCondition(UIA_NamePropertyId, v, &one);
        VariantClear(&v);
        if (!one)
            continue;
        if (!names)
            names = one;
        else {
            ComPtr<IUIAutomationCondition> combined;
            automation->CreateOrCondition(names.Get(), one.Get(), &combined);
            names = combined;
        }
    }
    if (!names)
        return result;
    ComPtr<IUIAutomationElementArray> found;
    if (FAILED(root->FindAll(TreeScope_Descendants, names.Get(), &found)))
        return result;
    int count{};
    found->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> e;
        found->GetElement(i, &e);
        if (!e)
            continue;
        RECT r{};
        CONTROLTYPEID type{};
        e->get_CurrentControlType(&type);
        e->get_CurrentBoundingRectangle(&r);
        if (type == UIA_ButtonControlTypeId && ValidButtons(r, outer, dpi))
            UnionRect(&result, &result, &r);
    }
    if (IsRectEmpty(&result)) {
        VARIANT v{};
        v.vt = VT_I4;
        v.lVal = UIA_ButtonControlTypeId;
        ComPtr<IUIAutomationCondition> buttons;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &buttons);
        found.Reset();
        if (buttons && SUCCEEDED(root->FindAll(TreeScope_Descendants, buttons.Get(), &found))) {
            found->get_Length(&count);
            for (int i = 0; i < count; ++i) {
                ComPtr<IUIAutomationElement> e;
                found->GetElement(i, &e);
                RECT r{};
                if (!e)
                    continue;
                e->get_CurrentBoundingRectangle(&r);
                if (ValidButtons(r, outer, dpi) && r.right - r.left <= Scale(65, dpi) &&
                    r.bottom - r.top <= Scale(48, dpi) && r.left >= outer.right - Scale(200, dpi))
                    UnionRect(&result, &result, &r);
            }
        }
    }
    return result;
}
RECT OriginalButtons(HWND target, const RECT &outer, UINT dpi) {
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[32768]{};
    DWORD length = 32768;
    bool qq = process && QueryFullProcessImageNameW(process, 0, path, &length) &&
              _wcsicmp(std::wstring(path).substr(std::wstring(path).find_last_of(L"\\/") + 1).c_str(), L"QQ.exe") == 0;
    if (process)
        CloseHandle(process);
    // QQ exposes conversation-toolbar buttons to UIA in the caption range.
    // Its three actual window controls are 32 DIP each at the upper right.
    if (qq)
        return {outer.right - Scale(96, dpi), outer.top + Scale(2, dpi), outer.right - Scale(1, dpi),
                outer.top + Scale(30, dpi)};
    RECT r{};
    DwmGetWindowAttribute(target, DWMWA_CAPTION_BUTTON_BOUNDS, &r, sizeof(r));
    OffsetRect(&r, outer.left, outer.top);
    if (ValidButtons(r, outer, dpi))
        return r;
    r = AccessibleButtons(target, outer, dpi);
    if (ValidButtons(r, outer, dpi))
        return r;
    wchar_t name[256]{};
    GetClassNameW(target, name, 256);
    if (wcscmp(name, L"Chrome_WidgetWin_1") == 0 || wcscmp(name, L"ApplicationFrameWindow") == 0 ||
        wcscmp(name, L"TaskManagerWindow") == 0) {
        // Last-resort bounds for the conventional three-button strip. Every
        // deployed class is separately checked in real composed screenshots.
        return {outer.right - Scale(144, dpi), outer.top + Scale(2, dpi), outer.right - Scale(1, dpi),
                outer.top + Scale(32, dpi)};
    }
    if (std::wstring(name).starts_with(L"HwndWrapper[Universal x86 Tuning Utility"))
        return {outer.right - Scale(132, dpi), outer.top + Scale(2, dpi), outer.right - Scale(1, dpi),
                outer.top + Scale(32, dpi)};
    return {};
}
bool SpareLeftStrip(HWND target, const RECT &outer, UINT dpi) {
    for (int x : {14, 40, 66, 82}) {
        DWORD_PTR hit{};
        POINT p{outer.left + Scale(x, dpi), outer.top + Scale(16, dpi)};
        if (!SendMessageTimeoutW(target, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y), SMTO_ABORTIFHUNG | SMTO_BLOCK, 150,
                                 &hit) ||
            hit != HTCAPTION)
            return false;
    }
    RECT strip{outer.left + Scale(4, dpi), outer.top + Scale(2, dpi), outer.left + Scale(88, dpi),
               outer.top + Scale(32, dpi)};
    POINT center{(strip.left + strip.right) / 2, (strip.top + strip.bottom) / 2};
    HWND at = WindowFromPoint(center);
    if (!at || GetAncestor(at, GA_ROOT) != target)
        return false;
    // HTCAPTION also covers painted tab labels/logos. Require a visually empty
    // strip; otherwise retain that content and take the downward-layout path.
    int width = strip.right - strip.left, height = strip.bottom - strip.top;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void *pixels{};
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return false;
    }
    auto previous = SelectObject(dc, bitmap);
    bool captured = BitBlt(dc, 0, 0, width, height, screen, strip.left, strip.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::array<unsigned, 4096> histogram{};
    if (captured)
        for (int i = 0; i < width * height; ++i) {
            DWORD p = static_cast<DWORD *>(pixels)[i];
            ++histogram[((p >> 12) & 0xf00) | ((p >> 8) & 0xf0) | ((p >> 4) & 0xf)];
        }
    unsigned dominant = *std::max_element(histogram.begin(), histogram.end());
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return captured && dominant * 1000u >= static_cast<unsigned>(width * height) * 995u;
}
} // namespace
struct AdaptiveSession::Frame {
    DWORD pid{}, thread{};
    FramePolicy policy = FramePolicy::Pending;
    bool injected{};
    WINDOWPLACEMENT original{sizeof(WINDOWPLACEMENT)};
    RECT appliedOuter{};
    RECT appliedNormal{};
    UINT appliedDpi = 96;
    Surface header, mask;
    RECT originalButtons{};
    int buttonRight{}, buttonTop{}, buttonWidth{}, buttonHeight{};
};
AdaptiveSession::AdaptiveSession(HWND owner) : owner_(owner), visual_(owner), injected_(owner) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}
AdaptiveSession::~AdaptiveSession() {
    RestoreAll();
    CoUninitialize();
}
bool AdaptiveSession::Owns(HWND w) const {
    return GetPropW(w, OwnerProperty) == owner_;
}
size_t AdaptiveSession::Count() const {
    size_t n{};
    for (auto &[w, f] : frames_)
        if (IsWindow(w) && Owns(w))
            ++n;
    return n;
}
Result AdaptiveSession::Apply(HWND w) {
    auto c = Inspect(w);
    if (!c.supported)
        return {false, c.reason};
    if (Owns(w))
        return {true, L"已应用窗口外观。"};
    if (GetPropW(w, OwnerProperty))
        return {false, L"已有其他控制器管理。"};
    auto frame = std::make_unique<Frame>();
    frame->thread = GetWindowThreadProcessId(w, &frame->pid);
    GetWindowPlacement(w, &frame->original);
    auto *f = frame.get();
    frames_[w] = std::move(frame);
    auto result = Activate(w, *f);
    if (!result.ok) {
        Restore(w);
        return result;
    }
    return {true, L"已隐藏原按钮并启用左上角红黄绿；最小化窗口恢复后适配。"};
}
Result AdaptiveSession::Activate(HWND w, Frame &f) {
    if (IsIconic(w)) {
        auto r = visual_.Apply(w);
        if (r.ok)
            SetPropW(w, PolicyProperty, reinterpret_cast<HANDLE>(FramePolicy::Pending));
        return r;
    }
    RECT outer{}, client{};
    POINT origin{};
    GetWindowRect(w, &outer);
    GetClientRect(w, &client);
    ClientToScreen(w, &origin);
    UINT dpi = GetDpiForWindow(w);
    TITLEBARINFOEX info{sizeof(info)};
    DWORD_PTR ignored{};
    SendMessageTimeoutW(w, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info), SMTO_ABORTIFHUNG | SMTO_BLOCK, 200,
                        &ignored);
    bool native = origin.y - outer.top >= GetSystemMetricsForDpi(SM_CYCAPTION, dpi) &&
                  info.rcTitleBar.bottom > info.rcTitleBar.top;
    f.originalButtons = OriginalButtons(w, outer, dpi);
    if (native)
        f.policy = FramePolicy::ExistingCaption;
    else if (!IsRectEmpty(&f.originalButtons) && SpareLeftStrip(w, outer, dpi))
        f.policy = FramePolicy::ExistingClientStrip;
    else
        f.policy = FramePolicy::ExtendedFallback;
    f.header.target = w;
    f.mask.target = w;
    f.mask.mask = true;
    BOOL dark{};
    DwmGetWindowAttribute(w, 20, &dark, sizeof(dark));
    f.header.dark = dark != FALSE;
    DWORD light = 1, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    if (!light)
        f.header.dark = true;
    f.header.compact = f.policy == FramePolicy::ExistingClientStrip;
    if (!native && !IsRectEmpty(&f.originalButtons)) {
        f.buttonRight = client.right - (f.originalButtons.right - origin.x);
        f.buttonTop = f.originalButtons.top - origin.y;
        f.buttonWidth = f.originalButtons.right - f.originalButtons.left;
        f.buttonHeight = f.originalButtons.bottom - f.originalButtons.top;
    }
    Result result;
    if (f.policy == FramePolicy::ExtendedFallback) {
        f.injected = true;
        result = injected_.Apply(w);
    } else
        result = visual_.Apply(w);
    if (!result.ok)
        return result;
    if (!CreateSurface(f.header))
        return {false, L"无法创建窗口合成层。"};
    if (!native && f.buttonWidth > 0 && !CreateSurface(f.mask))
        return {false, L"无法隐藏原有按钮。"};
    SetPropW(w, HeaderProperty, f.header.window);
    if (f.mask.window)
        SetPropW(w, MaskProperty, f.mask.window);
    SetPropW(w, PolicyProperty, reinterpret_cast<HANDLE>(f.policy));
    GetWindowRect(w, &f.appliedOuter);
    WINDOWPLACEMENT placement{sizeof(placement)};
    GetWindowPlacement(w, &placement);
    f.appliedNormal = placement.rcNormalPosition;
    f.appliedDpi = dpi;
    Update(w, f);
    return {true, L"已应用可见窗口外观。"};
}
void AdaptiveSession::Update(HWND w, Frame &f) {
    if (IsIconic(w) || !IsWindowVisible(w)) {
        if (f.header.window)
            ShowWindow(f.header.window, SW_HIDE);
        if (f.mask.window)
            ShowWindow(f.mask.window, SW_HIDE);
        return;
    }
    DWORD cloaked{};
    DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) {
        if (f.header.window)
            ShowWindow(f.header.window, SW_HIDE);
        if (f.mask.window)
            ShowWindow(f.mask.window, SW_HIDE);
        return;
    }
    RECT outer{}, client{};
    POINT origin{};
    GetWindowRect(w, &outer);
    GetClientRect(w, &client);
    ClientToScreen(w, &origin);
    UINT dpi = GetDpiForWindow(w);
    RECT header{};
    if (f.policy == FramePolicy::ExistingCaption) {
        TITLEBARINFOEX info{sizeof(info)};
        DWORD_PTR ignored{};
        SendMessageTimeoutW(w, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info), SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            100, &ignored);
        int top = info.rcTitleBar.top > outer.top ? info.rcTitleBar.top : outer.top + Scale(1, dpi);
        header = {origin.x, top, origin.x + client.right,
                  std::min<LONG>(origin.y, top + GetSystemMetricsForDpi(SM_CYCAPTION, dpi))};
    } else if (f.policy == FramePolicy::ExistingClientStrip)
        header = {outer.left + Scale(4, dpi), outer.top + Scale(2, dpi), outer.left + Scale(88, dpi),
                  outer.top + Scale(32, dpi)};
    else
        header = {origin.x, outer.top + std::max(0L, origin.x - outer.left), origin.x + client.right, origin.y};
    if (header.bottom > header.top)
        Position(f.header.window, w, header);
    if (f.mask.window) {
        RECT mask{origin.x + client.right - f.buttonRight - f.buttonWidth, origin.y + f.buttonTop,
                  origin.x + client.right - f.buttonRight, origin.y + f.buttonTop + f.buttonHeight};
        POINT sample{mask.left - 3, (mask.top + mask.bottom) / 2};
        HWND at = WindowFromPoint(sample);
        if (at && GetAncestor(at, GA_ROOT) == w) {
            HDC screen = GetDC(nullptr);
            auto color = GetPixel(screen, sample.x, sample.y);
            ReleaseDC(nullptr, screen);
            if (color != CLR_INVALID && color != f.mask.background) {
                f.mask.background = color;
                InvalidateRect(f.mask.window, nullptr, FALSE);
                bool dark = (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) < 110000;
                if (dark != f.header.dark) {
                    f.header.dark = dark;
                    InvalidateRect(f.header.window, nullptr, FALSE);
                }
            }
        }
        Position(f.mask.window, w, mask);
    }
}
void AdaptiveSession::Tick() {
    std::vector<HWND> dead;
    for (auto &[w, f] : frames_) {
        DWORD pid{};
        DWORD thread = GetWindowThreadProcessId(w, &pid);
        if (!IsWindow(w) || pid != f->pid || thread != f->thread) {
            dead.push_back(w);
            continue;
        }
        if (f->policy == FramePolicy::Pending && !IsIconic(w)) {
            visual_.Restore(w);
            GetWindowPlacement(w, &f->original);
            auto r = Activate(w, *f);
            if (!r.ok)
                dead.push_back(w);
        } else
            Update(w, *f);
    }
    for (auto w : dead)
        Restore(w);
}
Result AdaptiveSession::Restore(HWND w) {
    auto it = frames_.find(w);
    if (it == frames_.end())
        return {true, L"无需还原。"};
    auto &f = *it->second;
    RECT current{};
    GetWindowRect(w, &current);
    bool unchanged = EqualRect(&current, &f.appliedOuter);
    auto result = f.injected ? injected_.Restore(w) : visual_.Restore(w);
    if (!result.ok)
        return result;
    if (f.header.window)
        DestroyWindow(f.header.window);
    if (f.mask.window)
        DestroyWindow(f.mask.window);
    DWORD pid{};
    DWORD thread = GetWindowThreadProcessId(w, &pid);
    if (IsWindow(w) && pid == f.pid && thread == f.thread) {
        RemovePropW(w, HeaderProperty);
        RemovePropW(w, MaskProperty);
        RemovePropW(w, PolicyProperty);
        if (f.injected && unchanged)
            SetWindowPlacement(w, &f.original);
        else if (f.injected && (IsIconic(w) || IsZoomed(w))) {
            // Removing an extended frame while minimized/maximized cannot run
            // PreserveClientSize. Correct its saved normal rectangle so the next
            // restore has no leftover width/height from the extra frame.
            WINDOWPLACEMENT placement{sizeof(placement)};
            GetWindowPlacement(w, &placement);
            auto &normal = placement.rcNormalPosition;
            auto &applied = f.appliedNormal;
            auto &original = f.original.rcNormalPosition;
            UINT dpi = GetDpiForWindow(w);
            normal.right -=
                MulDiv((applied.right - applied.left) - (original.right - original.left), dpi, f.appliedDpi);
            normal.bottom -=
                MulDiv((applied.bottom - applied.top) - (original.bottom - original.top), dpi, f.appliedDpi);
            SetWindowPlacement(w, &placement);
        }
    }
    frames_.erase(it);
    return {true, L"已恢复原有窗口外观。"};
}
Result AdaptiveSession::RestoreAll() {
    std::vector<HWND> copy;
    for (auto &[w, f] : frames_)
        copy.push_back(w);
    bool ok = true;
    for (auto w : copy)
        ok = Restore(w).ok && ok;
    return {ok, ok ? L"全部窗口已还原。" : L"部分窗口等待还原。"};
}
} // namespace styler
