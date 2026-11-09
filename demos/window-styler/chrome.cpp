#include "chrome.h"
#include "nonclient-frame.h"
#include "caption-colors.h"
#include "native-style-view.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <iterator>
#include <map>
#include <new>
#include <vector>

namespace styler::chrome {
namespace {
constexpr UINT_PTR SubclassId = 0x4D414357;
constexpr DWORD CornerAttribute = 33;
// UxTheme can issue these non-client redraws after WM_NCPAINT. Suppressing
// them prevents native caption buttons from painting over our replacement.
// These two private messages are isolated here; see the compatibility notes.
constexpr UINT NcUxDrawCaption = 0x00AE;
constexpr UINT NcUxDrawFrame = 0x00AF;
struct State {
    Adapter adapter = Adapter::Native;
    HWND controller{};
    DWORD controllerPid{};
    HANDLE controllerProcess{};
    UINT_PTR timer{};
    int hover = -1;
    int pressed = -1;
    DWORD corner{};
    bool hasCorner{};
    BOOL originalRendering = TRUE;
    bool refreshing{};
    bool paintQueued{};
    bool colorReads{};
    bool capturedPalette{};
    CaptionColors originalPalette{};
    unsigned systemCommands{};
    int nativeTopBorder{};
    UINT captionPaints{}, clientPaints{}, refreshes{};
    NonclientFrame frame;
};

struct Layout {
    RECT window{};
    RECT client{};
    RECT caption{};
    RECT buttons[3]{};
    UINT dpi = 96;
    int border{};
};
// CAPTION_COLOR and TEXT_COLOR are setter-only DWM attributes. Capture the
// original composed frame before changing rendering instead of treating a
// failed getter as an application request for the system default. PrintWindow
// works for an occluded window without sampling another application's pixels.
bool CapturePalette(HWND window, CaptionColors &palette) {
    RECT outer{};
    if (!GetWindowRect(window, &outer) || IsIconic(window))
        return false;
    auto dpi = GetDpiForWindow(window);
    POINT origin{};
    ClientToScreen(window, &origin);
    const int top = std::max(0L, origin.x - outer.left);
    const int height = top + GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
    const int width = outer.right - outer.left;
    const int left = top + MulDiv(36, dpi, 96);
    const int right = width - top - MulDiv(150, dpi, 96);
    if (width <= 0 || width > 16384 || height <= top + 4 || height > 256 || right <= left)
        return false;
    POINT physicalFirst{outer.left, outer.top}, physicalLast{outer.right, outer.top + height};
    if (!LogicalToPhysicalPointForPerMonitorDPI(window, &physicalFirst) ||
        !LogicalToPhysicalPointForPerMonitorDPI(window, &physicalLast))
        return false;
    struct PhysicalScope {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~PhysicalScope() {
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } physicalScope;
    RECT band{physicalFirst.x, physicalFirst.y, physicalLast.x, physicalLast.y};
    bool unobscured = IsWindowVisible(window) != FALSE;
    bool found = false;
    for (auto above = GetTopWindow(nullptr); above; above = GetWindow(above, GW_HWNDNEXT)) {
        if (above == window) {
            found = true;
            break;
        }
        RECT bounds{}, intersection{};
        DWORD cloaked{};
        if (IsWindowVisible(above) && !IsIconic(above) &&
            SUCCEEDED(DwmGetWindowAttribute(above, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && !cloaked &&
            GetWindowRect(above, &bounds) && IntersectRect(&intersection, &band, &bounds))
            unobscured = false;
    }
    unobscured = unobscured && found;
    const bool legacy =
        GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window)) == DPI_AWARENESS_UNAWARE;
    // PrintWindow's legacy path omits DWM's explicit colors on Windows 11.
    // Only use the actual composed frame for those windows, without moving or
    // activating them. A covered legacy title is retried when it becomes visible.
    if (legacy && !unobscured)
        return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    auto screen = GetDC(nullptr);
    auto dc = CreateCompatibleDC(screen);
    void *pixels{};
    auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto old = bitmap && dc ? SelectObject(dc, bitmap) : nullptr;
    bool captured = false;
    if (old) {
        std::fill_n(static_cast<DWORD *>(pixels), size_t(width) * height, 0x00ff00ff);
        const bool copied =
            unobscured ? StretchBlt(dc, 0, 0, width, height, screen, band.left, band.top, band.right - band.left,
                                    band.bottom - band.top, SRCCOPY | CAPTUREBLT) != FALSE
                       : PrintWindow(window, dc, PW_RENDERFULLCONTENT) != FALSE;
        if (copied) {
            GdiFlush();
            std::map<COLORREF, unsigned> colors;
            for (int y = top + 2; y < height - 2; ++y)
                for (int x = left; x < right; ++x) {
                    auto color = GetPixel(dc, x, y);
                    if (color != CLR_INVALID && color != RGB(255, 0, 255))
                        ++colors[color];
                }
            auto background = std::max_element(colors.begin(), colors.end(),
                                               [](const auto &a, const auto &b) { return a.second < b.second; });
            if (background != colors.end() && background->second > unsigned((right - left) * (height - top - 4) / 2)) {
                palette = ResolveCaptionColors(false, true, background->first, DWMWA_COLOR_DEFAULT);
                unsigned count{};
                for (const auto &[color, frequency] : colors) {
                    int difference = abs(int(GetRValue(color)) - int(GetRValue(palette.background))) +
                                     abs(int(GetGValue(color)) - int(GetGValue(palette.background))) +
                                     abs(int(GetBValue(color)) - int(GetBValue(palette.background)));
                    if (difference >= 120 && frequency > count) {
                        palette.foreground = color;
                        count = frequency;
                    }
                }
                captured = count >= 3;
            }
        }
    }
    if (old)
        SelectObject(dc, old);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    if (screen)
        ReleaseDC(nullptr, screen);
    return captured;
}
int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

bool CaptionVisible(HWND window, const State *state) {
    return (state->adapter == Adapter::Extended || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) &&
           !IsIconic(window);
}

Layout Measure(HWND window, const State *state) {
    Layout l;
    l.dpi = GetDpiForWindow(window);
    if (!l.dpi)
        l.dpi = 96;
    GetWindowRect(window, &l.window);
    OffsetRect(&l.window, -l.window.left, -l.window.top);
    GetClientRect(window, &l.client);
    POINT origin{};
    ClientToScreen(window, &origin);
    RECT screen{};
    GetWindowRect(window, &screen);
    OffsetRect(&l.client, origin.x - screen.left, origin.y - screen.top);
    l.border = std::max(0L, l.client.left);
    // Let the target's original NCCALCSIZE preserve its menu and client layout.
    // A native menu can wrap, so do not derive caption height from client.top.
    int captionTop = state->adapter == Adapter::Extended ? l.border : state->nativeTopBorder;
    int bottom =
        state->adapter == Adapter::Extended
            ? l.client.top
            : std::min(l.client.top, static_cast<LONG>(captionTop + GetSystemMetricsForDpi(SM_CYCAPTION, l.dpi)));
    l.caption = {l.border, captionTop, l.window.right - l.border, bottom};
    int diameter = Scale(12, l.dpi);
    int y = l.caption.top + (l.caption.bottom - l.caption.top - diameter) / 2;
    for (int i = 0; i < 3; ++i) {
        int x = l.caption.left + Scale(12 + i * 20, l.dpi);
        l.buttons[i] = {x, y, x + diameter, y + diameter};
    }
    return l;
}

bool ButtonEnabled(HWND window, const State *state, int button) {
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!IsWindowEnabled(window))
        return false;
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    HMENU menu = GetSystemMenu(window, FALSE);
    return (state->adapter == Adapter::Extended || (style & WS_SYSMENU)) &&
           (!menu || !(GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED)));
}

int ButtonAt(HWND window, const State *state, POINT screenPoint) {
    if (!CaptionVisible(window, state))
        return -1;
    RECT screen{};
    GetWindowRect(window, &screen);
    POINT point{screenPoint.x - screen.left, screenPoint.y - screen.top};
    auto l = Measure(window, state);
    for (int i = 0; i < 3; ++i) {
        RECT hit = l.buttons[i];
        InflateRect(&hit, Scale(3, l.dpi), Scale(3, l.dpi));
        if (PtInRect(&hit, point) && ButtonEnabled(window, state, i))
            return i;
    }
    return -1;
}

State *Live(HWND window);
UINT PaintMessage() {
    static UINT message = RegisterWindowMessageW(L"Macdows.WindowStyler.NcPaint.v8");
    return message;
}
void QueuePaint(HWND window, State *state) {
    if (!state->paintQueued)
        state->paintQueued = PostMessageW(window, PaintMessage(), 0, 0) != FALSE;
}

struct NativeControls {
    RECT areas[4]{};
    unsigned count{};
};
NativeControls Controls(HWND window, const Layout &l, const State *state) {
    NativeControls controls;
    if (state->adapter != Adapter::Native)
        return controls;
    if (GetMenu(window) && l.client.top > l.caption.bottom)
        controls.areas[controls.count++] = {l.client.left, l.caption.bottom, l.client.right, l.client.top};
    RECT outer{};
    GetWindowRect(window, &outer);
    RECT bars[2]{};
    bool visible[2]{};
    constexpr LONG ids[]{OBJID_VSCROLL, OBJID_HSCROLL};
    for (unsigned i = 0; i < 2; ++i) {
        SCROLLBARINFO info{sizeof(info)};
        if (GetScrollBarInfo(window, ids[i], &info) && !(info.rgstate[0] & STATE_SYSTEM_INVISIBLE)) {
            auto r = info.rcScrollBar;
            OffsetRect(&r, -outer.left, -outer.top);
            if (!IsRectEmpty(&r)) {
                bars[i] = r;
                visible[i] = true;
                controls.areas[controls.count++] = r;
            }
        }
    }
    // Preserve the native size box at the intersection of both scrollbars.
    if (visible[0] && visible[1])
        controls.areas[controls.count++] = {bars[0].left, bars[1].top, bars[0].right, bars[1].bottom};
    return controls;
}

void PaintScrollbars(HWND window, State *state, HDC output, BOOL dark) {
    if (!dark || state->adapter != Adapter::Native)
        return;
    // Disabling DWM NC can make DefWindowProc fall back to a light scrollbar,
    // even though the host still requests dark mode. Theme the existing native
    // scrollbar on its original HWND, retaining its geometry and input handler.
    RECT outer{};
    GetWindowRect(window, &outer);
    POINT mouse{};
    GetCursorPos(&mouse);
    for (int vertical = 0; vertical < 2; ++vertical) {
        SCROLLBARINFO info{sizeof(info)};
        if (!GetScrollBarInfo(window, vertical ? OBJID_VSCROLL : OBJID_HSCROLL, &info) ||
            (info.rgstate[0] & STATE_SYSTEM_INVISIBLE))
            continue;
        auto area = info.rcScrollBar;
        OffsetRect(&area, -outer.left, -outer.top);
        HDC dc{};
        BP_PAINTPARAMS parameters{sizeof(parameters)};
        parameters.dwFlags = BPPF_NOCLIP;
        auto buffer = BeginBufferedPaint(output, &area, BPBF_TOPDOWNDIB, &parameters, &dc);
        if (!buffer)
            continue;
        auto track = CreateSolidBrush(RGB(24, 24, 24));
        FillRect(dc, &area, track);
        DeleteObject(track);
        RECT first = area, last = area, thumb = area;
        if (vertical) {
            first.bottom = first.top + info.dxyLineButton;
            last.top = last.bottom - info.dxyLineButton;
            thumb.top += info.xyThumbTop;
            thumb.bottom = area.top + info.xyThumbBottom;
        } else {
            first.right = first.left + info.dxyLineButton;
            last.left = last.right - info.dxyLineButton;
            thumb.left += info.xyThumbTop;
            thumb.right = area.left + info.xyThumbBottom;
        }
        POINT local{mouse.x - outer.left, mouse.y - outer.top};
        auto status = [&](const RECT &r, int element) {
            if ((info.rgstate[0] | info.rgstate[element]) & STATE_SYSTEM_UNAVAILABLE)
                return 0;
            if (!PtInRect(&r, local))
                return 1;
            return GetCapture() == window && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 3 : 2;
        };
        auto arrow = [&](RECT r, bool second) {
            int state = status(r, second ? 5 : 1);
            if (state > 1) {
                auto brush = CreateSolidBrush(RGB(48, 48, 48));
                FillRect(dc, &r, brush);
                DeleteObject(brush);
            }
            int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2,
                d = std::max(2, Scale(3, GetDpiForWindow(window))), direction = second ? 1 : -1;
            POINT points[3]{};
            if (vertical) {
                points[0] = {x - d, y - direction * d / 2};
                points[1] = {x + d, y - direction * d / 2};
                points[2] = {x, y + direction * d / 2};
            } else {
                points[0] = {x - direction * d / 2, y - d};
                points[1] = {x - direction * d / 2, y + d};
                points[2] = {x + direction * d / 2, y};
            }
            auto brush = CreateSolidBrush(state ? RGB(170, 170, 170) : RGB(75, 75, 75));
            auto oldBrush = SelectObject(dc, brush);
            auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Polygon(dc, points, 3);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        };
        arrow(first, false);
        arrow(last, true);
        if (!IsRectEmpty(&thumb)) {
            int value = status(thumb, 3);
            auto brush = CreateSolidBrush(value > 1 ? RGB(150, 150, 150) : RGB(100, 100, 100));
            auto oldBrush = SelectObject(dc, brush);
            auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            int inset = std::max(2, Scale(3, GetDpiForWindow(window)));
            if (vertical) {
                thumb.left += inset;
                thumb.right -= inset;
            } else {
                thumb.top += inset;
                thumb.bottom -= inset;
            }
            RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, inset * 2, inset * 2);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
        BufferedPaintSetAlpha(buffer, nullptr, 255);
        EndBufferedPaint(buffer, TRUE);
    }
}

void Paint(HWND window, State *state) {
    if (!CaptionVisible(window, state))
        return;
    auto l = Measure(window, state);
    auto controls = Controls(window, l, state);
    if (l.caption.bottom <= l.caption.top)
        return;
    HDC output = GetWindowDC(window);
    if (!output)
        return;
    // Extended glass captions need explicit alpha. Plain GDI writes zero alpha
    // and a shell frame can then add its tint to every painted channel.
    HDC dc = output;
    BP_PAINTPARAMS params{sizeof(params)};
    params.dwFlags = BPPF_NOCLIP;
    HPAINTBUFFER buffer = BeginBufferedPaint(output, &l.caption, BPBF_TOPDOWNDIB, &params, &dc);
    if (!buffer)
        dc = output;
    int saved = SaveDC(dc);
    // Clip away the entire original client area and the native menu strip.
    ExcludeClipRect(dc, l.client.left, l.client.top, l.client.right, l.client.bottom);
    if (l.client.top > l.caption.bottom)
        ExcludeClipRect(dc, l.client.left, l.caption.bottom, l.client.right, l.client.top);
    bool active = GetForegroundWindow() == GetAncestor(window, GA_ROOT);
    BOOL dark{};
    DwmGetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    TrackedCaptionDarkMode(window, dark);
    COLORREF caption = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    DwmGetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmGetWindowAttribute(window, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    TrackedCaptionColor(window, DWMWA_CAPTION_COLOR, caption);
    TrackedCaptionColor(window, DWMWA_TEXT_COLOR, textColor);
    auto palette = ResolveCaptionColors(dark != FALSE, active, caption, textColor);
    if (state->capturedPalette && !GetPropW(window, CaptionDarkModeProperty) &&
        !GetPropW(window, CaptionColorProperties[0])) {
        palette = state->originalPalette;
        if (TrackedCaptionColor(window, DWMWA_TEXT_COLOR, textColor))
            palette.foreground = ResolveCaptionColors(dark != FALSE, active, palette.background, textColor).foreground;
    }
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        palette.background = GetSysColor(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
        palette.foreground = GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT);
        palette.separator = GetSysColor(COLOR_WINDOWFRAME);
    }
    HBRUSH background = CreateSolidBrush(palette.background);
    FillRect(dc, &l.window, background);
    HPEN line = CreatePen(PS_SOLID, 1, palette.separator);
    HGDIOBJ oldPen = SelectObject(dc, line);
    MoveToEx(dc, l.caption.left, l.caption.bottom - 1, nullptr);
    LineTo(dc, l.caption.right, l.caption.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(line);

    constexpr COLORREF colors[] = {RGB(255, 95, 87), RGB(254, 188, 46), RGB(40, 200, 64)};
    constexpr COLORREF borders[] = {RGB(222, 73, 65), RGB(218, 159, 35), RGB(30, 166, 48)};
    for (int i = 0; i < 3; ++i) {
        bool enabled = ButtonEnabled(window, state, i);
        bool colored = enabled && (active || state->hover >= 0);
        HBRUSH brush = CreateSolidBrush(colored ? colors[i] : RGB(208, 208, 207));
        HPEN pen = CreatePen(PS_SOLID, 1, colored ? borders[i] : RGB(190, 190, 189));
        auto oldBrush = SelectObject(dc, brush);
        oldPen = SelectObject(dc, pen);
        auto r = l.buttons[i];
        Ellipse(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        if (state->hover >= 0 && enabled) {
            HPEN glyph = CreatePen(PS_SOLID, std::max(1, Scale(1, l.dpi)), RGB(76, 63, 49));
            oldPen = SelectObject(dc, glyph);
            int inset = Scale(4, l.dpi);
            int centerY = (r.top + r.bottom) / 2;
            MoveToEx(dc, r.left + inset, i == 0 ? r.top + inset : centerY, nullptr);
            LineTo(dc, r.right - inset, i == 0 ? r.bottom - inset : centerY);
            if (i == 0) {
                MoveToEx(dc, r.right - inset, r.top + inset, nullptr);
                LineTo(dc, r.left + inset, r.bottom - inset);
            } else if (i == 2) {
                MoveToEx(dc, (r.left + r.right) / 2, r.top + inset, nullptr);
                LineTo(dc, (r.left + r.right) / 2, r.bottom - inset);
            }
            SelectObject(dc, oldPen);
            DeleteObject(glyph);
        }
    }
    wchar_t title[512]{};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    // In-process WM_GETTEXT can reenter application code and destroy/detach
    // the window. Never retain its State across that callback.
    if (Live(window) != state) {
        RestoreDC(dc, saved);
        if (buffer)
            EndBufferedPaint(buffer, FALSE);
        DeleteObject(background);
        ReleaseDC(window, output);
        return;
    }
    HFONT font = CreateFontW(-Scale(12, l.dpi), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.foreground);
    RECT text = l.caption;
    text.left += Scale(90, l.dpi);
    text.right -= Scale(90, l.dpi);
    if (text.right > text.left)
        DrawTextW(dc, title, -1, &text, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    RestoreDC(dc, saved);
    if (buffer) {
        BufferedPaintSetAlpha(buffer, &l.caption, 255);
        EndBufferedPaint(buffer, TRUE);
    }
    // A caption-sized buffer cannot update the top/side/bottom frame. Paint
    // each NC strip separately, never buffering or overwriting a GPU client.
    auto strip = [&](RECT area) {
        if (IsRectEmpty(&area))
            return;
        auto region = CreateRectRgnIndirect(&area);
        if (!region)
            return;
        for (unsigned i = 0; i < controls.count; ++i) {
            auto control = CreateRectRgnIndirect(&controls.areas[i]);
            if (control) {
                CombineRgn(region, region, control, RGN_DIFF);
                DeleteObject(control);
            }
        }
        DWORD size = GetRegionData(region, 0, nullptr);
        std::vector<BYTE> bytes(size);
        auto data = reinterpret_cast<RGNDATA *>(bytes.data());
        if (size && GetRegionData(region, size, data)) {
            auto rectangles = reinterpret_cast<RECT *>(data->Buffer);
            for (DWORD i = 0; i < data->rdh.nCount; ++i) {
                auto &r = rectangles[i];
                HDC temp{};
                auto paint = BeginBufferedPaint(output, &r, BPBF_TOPDOWNDIB, &params, &temp);
                if (paint) {
                    FillRect(temp, &r, background);
                    BufferedPaintSetAlpha(paint, nullptr, 255);
                    EndBufferedPaint(paint, TRUE);
                } else
                    FillRect(output, &r, background);
            }
        }
        DeleteObject(region);
    };
    strip({0, 0, l.window.right, l.caption.top});
    strip({0, l.caption.top, l.client.left, l.window.bottom});
    strip({l.client.right, l.caption.top, l.window.right, l.window.bottom});
    strip({l.client.left, l.client.bottom, l.client.right, l.window.bottom});
    PaintScrollbars(window, state, output, dark);
    DeleteObject(background);
    ReleaseDC(window, output);
    if (auto *live = Live(window))
        ++live->captionPaints;
}

LRESULT CALLBACK WindowSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR reference);

State *Live(HWND window) {
    DWORD_PTR reference{};
    return GetWindowSubclass(window, WindowSubclass, SubclassId, &reference) ? reinterpret_cast<State *>(reference)
                                                                             : nullptr;
}

// Keep the application's content size across the change in frame metrics.
// This also gives GPU-backed clients a real, consistent size notification.
void PreserveClientSize(HWND window, const RECT &desired) {
    if (!IsWindow(window) || IsIconic(window) || IsZoomed(window))
        return;
    RECT frame{}, current{};
    GetWindowRect(window, &frame);
    GetClientRect(window, &current);
    int dx = desired.right - desired.left - current.right;
    int dy = desired.bottom - desired.top - current.bottom;
    if (dx || dy)
        SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left + dx, frame.bottom - frame.top + dy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool HasCompositedChild(HWND window) {
    bool found{};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM value) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(child, name, static_cast<int>(std::size(name)));
            if (wcscmp(name, L"Chrome_RenderWidgetHostHWND") == 0 || wcscmp(name, L"Intermediate D3D Window") == 0) {
                *reinterpret_cast<bool *>(value) = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}
void SynchronizeComposition(HWND window) {
    if (IsIconic(window) || IsZoomed(window) || !HasCompositedChild(window))
        return;
    RECT outer{};
    if (!GetWindowRect(window, &outer))
        return;
    // Embedded GPU surfaces can cache the old DWM/client transform and ignore
    // a same-size WM_SIZE. A real one-pixel size transaction refreshes their
    // original backing store; restore the exact geometry before acknowledging.
    constexpr UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left + 1, outer.bottom - outer.top, flags);
    if (IsWindow(window))
        SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, flags);
}

void RefreshFrame(HWND window) {
    State *state = Live(window);
    if (state && state->refreshing)
        return;
    if (state)
        state->refreshing = true;
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (IsWindow(window) && !IsIconic(window)) {
        RECT client{};
        GetClientRect(window, &client);
        // FRAMECHANGED alone can leave the client's swap chain / child layout
        // at its old size until the user moves the window. Notify the actual
        // size, then synchronously repaint the complete frame and all children.
        SendMessageW(window, WM_SIZE, IsZoomed(window) ? SIZE_MAXIMIZED : SIZE_RESTORED,
                     MAKELPARAM(client.right, client.bottom));
        RedrawWindow(window, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASENOW);
    }
    if (auto *live = Live(window)) {
        ++live->refreshes;
        live->refreshing = false;
        Paint(window, live);
    }
}

void Release(HWND window, State *state, bool destroyed) {
    COLORREF caption = DWMWA_COLOR_DEFAULT, text = DWMWA_COLOR_DEFAULT;
    const bool captionChanged = TrackedCaptionColor(window, DWMWA_CAPTION_COLOR, caption);
    const bool textChanged = TrackedCaptionColor(window, DWMWA_TEXT_COLOR, text);
    ClearCaptionColors(window);
    RECT client{};
    if (!destroyed)
        GetClientRect(window, &client);
    KillTimer(window, state->timer);
    RemoveWindowSubclass(window, WindowSubclass, SubclassId);
    if (state->pressed >= 0 && GetCapture() == window)
        ReleaseCapture();
    RemovePropW(window, OwnerProperty);
    RemovePropW(window, ResultProperty);
    RemovePropW(window, AdapterProperty);
    if (!destroyed && IsWindow(window)) {
        // Windows exposes the current enabled state, not a readable policy.
        DWMNCRENDERINGPOLICY policy = state->originalRendering ? DWMNCRP_USEWINDOWSTYLE : DWMNCRP_DISABLED;
        DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        if (state->hasCorner)
            DwmSetWindowAttribute(window, CornerAttribute, &state->corner, sizeof(state->corner));
    }
    if (state->controllerProcess)
        CloseHandle(state->controllerProcess);
    if (!destroyed && IsWindow(window)) {
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        PreserveClientSize(window, client);
        RefreshFrame(window);
        // Theme/DWM reactivation can install an intermediate region. Restore
        // the original shape after those synchronous frame calculations.
        state->frame.Restore(window);
        SynchronizeComposition(window);
    }
    if (!destroyed && IsWindow(window)) {
        if (captionChanged)
            DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        if (textChanged)
            DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
    }
    if (state->colorReads)
        style_view::ReleaseReads();
    delete state;
}

LRESULT CALLBACK WindowSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                DWORD_PTR reference) {
    auto *state = reinterpret_cast<State *>(reference);
    if (NonclientFrame::Filter(window, message, wParam, lParam))
        return 0;
    if (message == QueryMessage())
        return Acknowledged;
    if (message == DiagnosticMessage()) {
        switch (static_cast<Metric>(wParam)) {
        case Metric::CaptionPaints:
            return state->captionPaints;
        case Metric::ClientPaints:
            return state->clientPaints;
        case Metric::Refreshes:
            return state->refreshes;
        case Metric::CaptionTop:
        case Metric::CaptionBottom: {
            RECT outer{};
            GetWindowRect(window, &outer);
            POINT origin{outer.left, outer.top};
            POINT edge = origin;
            auto caption = Measure(window, state).caption;
            edge.y += static_cast<Metric>(wParam) == Metric::CaptionTop ? caption.top : caption.bottom;
            LogicalToPhysicalPointForPerMonitorDPI(window, &origin);
            LogicalToPhysicalPointForPerMonitorDPI(window, &edge);
            return edge.y - origin.y;
        }
        case Metric::ButtonCenterX:
        case Metric::ButtonCenterY: {
            if (lParam < 0 || lParam >= 3)
                return 0;
            RECT outer{};
            GetWindowRect(window, &outer);
            auto rect = Measure(window, state).buttons[lParam];
            POINT point{outer.left + (rect.left + rect.right) / 2, outer.top + (rect.top + rect.bottom) / 2};
            LogicalToPhysicalPointForPerMonitorDPI(window, &point);
            return static_cast<Metric>(wParam) == Metric::ButtonCenterX ? point.x : point.y;
        }
        case Metric::AdapterMode:
            return static_cast<LRESULT>(state->adapter);
        }
        return 0;
    }
    if (message == RefreshMessage()) {
        RefreshFrame(window);
        return 0;
    }
    if (message == NonclientFrame::StateMessage()) {
        // Green means maximize/restore. A host can reinterpret SC_MAXIMIZE as
        // borderless fullscreen, removing the very row needed to restore it.
        // Use the standard HWND state transition for every native backend;
        // original WM_SIZE/layout handling still belongs to the application.
        if (ButtonEnabled(window, state, 2)) {
            ++state->systemCommands;
            NonclientFrame::ToggleMaximize(window);
            if (auto *current = Live(window))
                --current->systemCommands;
        }
        return 0;
    }
    if (message == PaintMessage()) {
        state->paintQueued = false;
        Paint(window, state);
        return 0;
    }
    if (message == CaptionColorMessage()) {
        if (wParam != DWMWA_TEXT_COLOR)
            state->capturedPalette = false;
        QueuePaint(window, state);
        return 0;
    }
    if (message == DetachMessage()) {
        if (reinterpret_cast<HWND>(wParam) != state->controller)
            return 0;
        // DefWindowProc pumps sent messages during minimize/maximize animation.
        // Reentrant frame removal would leave its pending placement calculation
        // operating on different metrics. Let the controller retry after it exits.
        if (state->systemCommands)
            return TransitionBusy;
        Release(window, state, false);
        return Acknowledged;
    }
    switch (message) {
    case WM_SYSCOMMAND: {
        ++state->systemCommands;
        auto result = DefSubclassProc(window, message, wParam, lParam);
        if (auto *live = Live(window))
            --live->systemCommands;
        return result;
    }
    case WM_PAINT: {
        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (auto *live = Live(window)) {
            ++live->clientPaints;
            // A host can repaint its non-client scrollbars as part of its
            // client render. Apply the frame only after that render completes.
            Paint(window, live);
        }
        return result;
    }
    case WM_NCCALCSIZE: {
        RECT *client =
            wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam)->rgrc[0] : reinterpret_cast<RECT *>(lParam);
        RECT outer = *client;
        DefSubclassProc(window, message, wParam, lParam);
        state = Live(window);
        if (!state)
            return 0;
        state->nativeTopBorder = std::max(0L, client->left - outer.left);
        // Extend a real non-client band above the original content. Do not
        // cover client-rendered tabs, menus, or framework-specific chrome.
        UINT dpi = GetDpiForWindow(window);
        // Keep the original resize metrics. NonclientFrame clips away
        // DWM's original invisible margins instead of widening the client.
        if (state->adapter == Adapter::Extended)
            client->top = std::max(client->top, outer.top + (client->left - outer.left) + Scale(36, dpi));
        if (IsZoomed(window)) {
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
                client->left = std::max(client->left, monitor.rcWork.left);
                client->right = std::min(client->right, monitor.rcWork.right);
                client->bottom = std::min(client->bottom, monitor.rcWork.bottom);
                if (state->adapter == Adapter::Extended)
                    client->top = std::max(client->top, monitor.rcWork.top + Scale(36, dpi));
            }
        }
        client->right = std::max(client->left, client->right);
        client->bottom = std::max(client->top, client->bottom);
        // Force a complete repaint instead of reusing stale valid rectangles.
        return 0;
    }
    case WM_NCDESTROY:
        Release(window, state, true);
        return DefSubclassProc(window, message, wParam, lParam);
    case WM_TIMER:
        if (wParam == state->timer) {
            DWORD ownerPid{};
            GetWindowThreadProcessId(state->controller, &ownerPid);
            if (!IsWindow(state->controller) || ownerPid != state->controllerPid ||
                WaitForSingleObject(state->controllerProcess, 0) != WAIT_TIMEOUT) {
                Release(window, state, false);
            }
            return 0;
        }
        break;
    case WM_NCPAINT: {
        LRESULT result{};
        // Native menus and scrollbars paint only their own NC areas. Painting the full
        // classic frame first can leave white/black slivers outside its caption
        // buffer, especially after a region or activation transition.
        auto layout = Measure(window, state);
        auto controls = Controls(window, layout, state);
        if (controls.count) {
            auto native = CreateRectRgn(0, 0, 0, 0);
            if (native) {
                for (unsigned i = 0; i < controls.count; ++i) {
                    auto area = CreateRectRgnIndirect(&controls.areas[i]);
                    if (area) {
                        CombineRgn(native, native, area, RGN_OR);
                        DeleteObject(area);
                    }
                }
                result = DefSubclassProc(window, message, reinterpret_cast<WPARAM>(native), lParam);
                DeleteObject(native);
            }
        }
        DWORD_PTR live{};
        if (GetWindowSubclass(window, WindowSubclass, SubclassId, &live)) {
            auto *current = reinterpret_cast<State *>(live);
            current->frame.Update(window);
            if (Live(window) == current) {
                Paint(window, current);
                QueuePaint(window, current);
            }
        }
        return result;
    }
    case WM_NCACTIVATE: {
        LRESULT result = DefSubclassProc(window, message, wParam, -1);
        DWORD_PTR live{};
        if (GetWindowSubclass(window, WindowSubclass, SubclassId, &live))
            Paint(window, reinterpret_cast<State *>(live));
        return result;
    }
    case NcUxDrawCaption:
    case NcUxDrawFrame:
        if (CaptionVisible(window, state)) {
            Paint(window, state);
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        if (!CaptionVisible(window, state))
            break;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int button = ButtonAt(window, state, point);
        constexpr LRESULT hits[] = {HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
        if (button >= 0)
            return hits[button];
        RECT screen{};
        GetWindowRect(window, &screen);
        point.x -= screen.left;
        point.y -= screen.top;
        auto l = Measure(window, state);
        if (state->adapter == Adapter::Extended) {
            if (!PtInRect(&l.window, point))
                return HTNOWHERE;
            if (!IsZoomed(window) && (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME)) {
                int grip = Scale(6, l.dpi);
                bool left = point.x < grip, right = point.x >= l.window.right - grip;
                bool top = point.y < grip, bottom = point.y >= l.window.bottom - grip;
                if (top && left)
                    return HTTOPLEFT;
                if (top && right)
                    return HTTOPRIGHT;
                if (bottom && left)
                    return HTBOTTOMLEFT;
                if (bottom && right)
                    return HTBOTTOMRIGHT;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (top)
                    return HTTOP;
                if (bottom)
                    return HTBOTTOM;
            }
            if (PtInRect(&l.caption, point))
                return HTCAPTION;
            if (!PtInRect(&l.client, point))
                return HTBORDER;
            return DefSubclassProc(window, message, wParam, lParam);
        }
        LRESULT original = DefSubclassProc(window, message, wParam, lParam);
        if (original >= HTLEFT && original <= HTBOTTOMRIGHT)
            return original;
        if (PtInRect(&l.caption, point))
            return HTCAPTION;
        if (original == HTCLOSE || original == HTMINBUTTON || original == HTMAXBUTTON || original == HTSYSMENU)
            return HTCAPTION;
        return original;
    }
    case WM_GETTITLEBARINFOEX: {
        auto result = DefSubclassProc(window, message, wParam, lParam);
        if (Live(window) == state && lParam) {
            auto *info = reinterpret_cast<TITLEBARINFOEX *>(lParam);
            if (info->cbSize == sizeof(TITLEBARINFOEX))
                for (int i = 2; i <= 5; ++i) {
                    info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                    SetRectEmpty(&info->rgrect[i]);
                }
        }
        return result;
    }
    case WM_NCMOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hover = ButtonAt(window, state, point);
        if (state->hover != hover) {
            state->hover = hover;
            Paint(window, state);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_NONCLIENT, window, 0};
        TrackMouseEvent(&tracking);
        // The native caption buttons are covered; don't let DefWindowProc
        // animate the original buttons over the replacement title bar.
        if (wParam == HTCLOSE || wParam == HTMINBUTTON || wParam == HTMAXBUTTON || wParam == HTCAPTION)
            return 0;
        break;
    }
    case WM_NCMOUSELEAVE:
        state->hover = -1;
        Paint(window, state);
        return 0;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int button = ButtonAt(window, state, point);
        if (button >= 0 && message != WM_NCLBUTTONUP) {
            state->pressed = button;
            SetCapture(window);
            if (auto *live = Live(window))
                Paint(window, live);
            return 0;
        }
        // A queued native-button message may outlive the frame transition.
        // Never forward an old right-side caption hit to the host's handler.
        if (wParam == HTCLOSE || wParam == HTMINBUTTON || wParam == HTMAXBUTTON || wParam == HTSYSMENU)
            return 0;
        break;
    }
    case WM_MOUSEMOVE:
        if (state->pressed >= 0) {
            POINT point{};
            GetCursorPos(&point);
            state->hover = ButtonAt(window, state, point);
            Paint(window, state);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (state->pressed >= 0) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window, &point);
            int pressed = state->pressed;
            state->pressed = -1;
            ReleaseCapture();
            // ReleaseCapture can synchronously invoke application code.
            state = Live(window);
            if (state && ButtonAt(window, state, point) == pressed && ButtonEnabled(window, state, pressed)) {
                WPARAM action = pressed == 0       ? SC_CLOSE
                                : pressed == 1     ? SC_MINIMIZE
                                : IsZoomed(window) ? SC_RESTORE
                                                   : SC_MAXIMIZE;
                if (pressed == 2)
                    PostMessageW(window, NonclientFrame::StateMessage(), 0, 0);
                else
                    PostMessageW(window, WM_SYSCOMMAND, action, 0);
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        state->pressed = -1;
        Paint(window, state);
        break;
    case WM_SETTEXT:
    case WM_VSCROLL:
    case WM_HSCROLL:
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED:
    case WM_STYLECHANGED:
    case WM_DPICHANGED:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_ENABLE: {
        // Application code can destroy its own window during these callbacks.
        // Reacquire the subclass data before touching State again.
        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        DWORD_PTR live{};
        if (GetWindowSubclass(window, WindowSubclass, SubclassId, &live)) {
            auto *current = reinterpret_cast<State *>(live);
            current->frame.Update(window);
            if (Live(window) == current) {
                Paint(window, current);
                QueuePaint(window, current);
            }
        }
        return result;
    }
    }
    auto result = DefSubclassProc(window, message, wParam, lParam);
    // Hosts can update scroll geometry/paint in their own posted messages.
    // Finish their callback before refreshing the original NC scrollbar.
    bool scrollbarInput = message == WM_NCMOUSEMOVE || message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP ||
                          message == WM_MOUSEMOVE || message == WM_LBUTTONUP || message == WM_MOUSEWHEEL ||
                          message == WM_MOUSEHWHEEL || message == WM_TIMER;
    if ((message >= WM_USER || scrollbarInput) && (GetWindowLongPtrW(window, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL)))
        if (auto *current = Live(window))
            Paint(window, current);
    return result;
}
} // namespace

DWORD Attach(HWND window, HWND controller, Adapter adapter) {
    DWORD_PTR existing{};
    if (GetWindowSubclass(window, WindowSubclass, SubclassId, &existing))
        return reinterpret_cast<State *>(existing)->controller == controller ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS;
    auto *state = new (std::nothrow) State;
    if (!state)
        return ERROR_NOT_ENOUGH_MEMORY;
    state->controller = controller;
    RECT originalClient{}, originalWindow{};
    GetClientRect(window, &originalClient);
    GetWindowRect(window, &originalWindow);
    POINT clientOrigin{};
    ClientToScreen(window, &clientOrigin);
    // Unknown applications can keep WS_CAPTION but return a frameless client
    // from NCCALCSIZE. Probe actual geometry instead of relying on class names.
    if (adapter == Adapter::Native && !GetMenu(window) &&
        clientOrigin.y - originalWindow.top < GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(window))) {
        delete state;
        return ERROR_NOT_SUPPORTED;
    }
    state->adapter = adapter;
    GetWindowThreadProcessId(controller, &state->controllerPid);
    state->controllerProcess = OpenProcess(SYNCHRONIZE, FALSE, state->controllerPid);
    if (!state->controllerProcess) {
        DWORD error = GetLastError();
        delete state;
        return error;
    }
    DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &state->originalRendering, sizeof(BOOL));
    if (!state->frame.Capture(window)) {
        CloseHandle(state->controllerProcess);
        delete state;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    state->hasCorner = SUCCEEDED(DwmGetWindowAttribute(window, CornerAttribute, &state->corner, sizeof(DWORD)));
    if (adapter == Adapter::Native)
        state->capturedPalette = CapturePalette(window, state->originalPalette);
    if (adapter == Adapter::Native && !state->capturedPalette &&
        GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window)) == DPI_AWARENESS_UNAWARE) {
        CloseHandle(state->controllerProcess);
        delete state;
        return ERROR_RETRY;
    }
    // PrintWindow can reenter the host. Do not attach to a destroyed/replaced
    // target or overwrite a lease installed by that callback.
    if (!IsWindow(window) || GetPropW(window, OwnerProperty)) {
        CloseHandle(state->controllerProcess);
        delete state;
        return ERROR_NOT_SUPPORTED;
    }
    if (!SetWindowSubclass(window, WindowSubclass, SubclassId, reinterpret_cast<DWORD_PTR>(state))) {
        CloseHandle(state->controllerProcess);
        delete state;
        return ERROR_NOT_SUPPORTED;
    }
    state->timer = SetTimer(window, reinterpret_cast<UINT_PTR>(state), 750, nullptr);
    if (!state->timer || !SetPropW(window, OwnerProperty, controller)) {
        DWORD error = GetLastError();
        Release(window, state, false);
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    SetPropW(window, AdapterProperty, reinterpret_cast<HANDLE>(adapter));
    COLORREF caption = DWMWA_COLOR_DEFAULT, text = DWMWA_COLOR_DEFAULT;
    const bool captionRead = SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption)));
    const bool textRead = SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text)));
    if ((captionRead && !TrackCaptionColor(window, DWMWA_CAPTION_COLOR, caption)) ||
        (textRead && !TrackCaptionColor(window, DWMWA_TEXT_COLOR, text)) ||
        !SetPropW(window, CaptionColorOwner, reinterpret_cast<HANDLE>(1))) {
        Release(window, state, false);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!style_view::AcquireReads()) {
        Release(window, state, false);
        return ERROR_NOT_SUPPORTED;
    }
    state->colorReads = true;
    DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
    HRESULT applied = DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    if (FAILED(applied)) {
        Release(window, state, false);
        return ERROR_NOT_SUPPORTED;
    }
    if (state->hasCorner) {
        DWORD rounded = 2;
        DwmSetWindowAttribute(window, CornerAttribute, &rounded, sizeof(rounded));
    }
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    PreserveClientSize(window, originalClient);
    if (!state->frame.Update(window)) {
        if (Live(window) == state)
            Release(window, state, false);
        return ERROR_NOT_SUPPORTED;
    }
    if (Live(window) != state)
        return ERROR_NOT_SUPPORTED;
    RefreshFrame(window);
    SynchronizeComposition(window);
    if (Live(window) != state)
        return ERROR_NOT_SUPPORTED;
    // A second pass after the triggering sent message finishes lets deferred
    // framework layout / GPU work observe the final non-client geometry.
    PostMessageW(window, RefreshMessage(), 0, 0);
    return ERROR_SUCCESS;
}

bool Detach(HWND window, HWND controller) {
    DWORD_PTR reference{};
    if (!GetWindowSubclass(window, WindowSubclass, SubclassId, &reference))
        return true;
    auto *state = reinterpret_cast<State *>(reference);
    if (state->controller != controller)
        return false;
    Release(window, state, false);
    return true;
}
} // namespace styler::chrome
