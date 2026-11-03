#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

namespace styler {
// Disabling the native caption also disables DWM's transparent resize margins
// and corner clipping. Retain their ORIGINAL visible footprint on the same HWND.
// Framework title-row backends retain DWM and do not apply this region.
class NonclientFrame {
    static constexpr wchar_t UpdateProperty[] = L"Macdows.WindowStyler.ShapeUpdate.v1";
    HRGN original_{};
    bool hasRegion_{}, captured_{};
    UINT dpi_ = 96;
    RECT insets_{};
    DWORD corner_{};
    int width_ = -1, height_ = -1;
    UINT lastDpi_{};
    bool maximized_{};

  public:
    static UINT StateMessage() {
        static UINT message = RegisterWindowMessageW(L"Macdows.WindowStyler.NativeState.v1");
        return message;
    }
    static void ToggleMaximize(HWND window) {
        ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
    }
    static bool Updating(HWND window) {
        return GetPropW(window, UpdateProperty) != nullptr;
    }
    static void SystemMenu(HWND window, bool visible) {
        auto style = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, visible ? style | WS_SYSMENU : style & ~WS_SYSMENU);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    static bool Filter(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto *insets = reinterpret_cast<const RECT *>(GetPropW(window, UpdateProperty));
        if (!insets)
            return false;
        // A region update is a shape-only change. Do not let its synthetic
        // FRAMECHANGED rebuild the host's title tree or recalculate its client.
        if (message == WM_NCCALCSIZE) {
            auto *r =
                wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam)->rgrc[0] : reinterpret_cast<RECT *>(lParam);
            r->left += insets->left;
            r->top += insets->top;
            r->right -= insets->right;
            r->bottom -= insets->bottom;
            return true;
        }
        return message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED || message == WM_STYLECHANGING ||
               message == WM_STYLECHANGED;
    }
    ~NonclientFrame() {
        if (original_)
            DeleteObject(original_);
    }
    bool Capture(HWND window) {
        RECT outer{}, visible{};
        if (!GetWindowRect(window, &outer))
            return false;
        dpi_ = GetDpiForWindow(window);
        if (!dpi_)
            dpi_ = 96;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))) {
            insets_ = {std::max(0L, visible.left - outer.left), std::max(0L, visible.top - outer.top),
                       std::max(0L, outer.right - visible.right), std::max(0L, outer.bottom - visible.bottom)};
        }
        DwmGetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_, sizeof(corner_));
        if (IsZoomed(window)) {
            RECT client{};
            POINT origin{};
            GetClientRect(window, &client);
            ClientToScreen(window, &origin);
            UINT thickness = std::max(1, MulDiv(1, dpi_, 96));
            DwmGetWindowAttribute(window, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &thickness, sizeof(thickness));
            insets_ = {std::max(0L, origin.x - outer.left - LONG(thickness)), 0,
                       std::max(0L, outer.right - origin.x - client.right - LONG(thickness)),
                       std::max(0L, outer.bottom - origin.y - client.bottom - LONG(thickness))};
        }
        original_ = CreateRectRgn(0, 0, 0, 0);
        if (!original_)
            return false;
        hasRegion_ = GetWindowRgn(window, original_) != ERROR;
        captured_ = true;
        return true;
    }
    RECT Visible(HWND window) const {
        RECT r{};
        GetWindowRect(window, &r);
        OffsetRect(&r, -r.left, -r.top);
        if (!IsZoomed(window)) {
            UINT dpi = GetDpiForWindow(window);
            if (!dpi)
                dpi = 96;
            r.left += MulDiv(insets_.left, dpi, dpi_);
            r.top += MulDiv(insets_.top, dpi, dpi_);
            r.right -= MulDiv(insets_.right, dpi, dpi_);
            r.bottom -= MulDiv(insets_.bottom, dpi, dpi_);
            // An explicit extended-frame backend may use a thinner NC band
            // than the original DWM footprint. A shape must never cut into
            // the current client or its child controls.
            RECT client{}, outer{};
            POINT origin{};
            if (GetWindowRect(window, &outer) && GetClientRect(window, &client) && ClientToScreen(window, &origin)) {
                r.left = std::min(r.left, origin.x - outer.left);
                r.top = std::min(r.top, origin.y - outer.top);
                r.right = std::max(r.right, origin.x - outer.left + client.right);
                r.bottom = std::max(r.bottom, origin.y - outer.top + client.bottom);
            }
        }
        return r;
    }
    bool Update(HWND window) {
        if (!captured_ || Updating(window) || IsIconic(window))
            return true;
        RECT outer{};
        GetWindowRect(window, &outer);
        int width = outer.right - outer.left, height = outer.bottom - outer.top;
        UINT dpi = GetDpiForWindow(window);
        if (!dpi)
            dpi = 96;
        bool maximized = IsZoomed(window) != FALSE;
        if (width == width_ && height == height_ && dpi == lastDpi_ && maximized == maximized_)
            return true;
        auto r = Visible(window);
        int radius =
            maximized || corner_ == DWMWCP_DONOTROUND ? 0 : MulDiv(corner_ == DWMWCP_ROUNDSMALL ? 4 : 8, dpi, 96);
        // Round-rectangle regions exclude their final scanline/column; include
        // that extra unit so the region box still covers the full client.
        HRGN region = radius ? CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2)
                             : CreateRectRgn(r.left, r.top, r.right, r.bottom);
        if (!region)
            return false;
        if (hasRegion_ && CombineRgn(region, region, original_, RGN_AND) == ERROR) {
            DeleteObject(region);
            return false;
        }
        // SetWindowRgn reenters the original window procedure. Establish the
        // dimensions before it, and never transfer a saved HRGN. Do not touch
        // this object after the call: the host can destroy its HWND inside it.
        width_ = width;
        height_ = height;
        lastDpi_ = dpi;
        maximized_ = maximized;
        RECT client{};
        POINT origin{};
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
        RECT insets{origin.x - outer.left, origin.y - outer.top, outer.right - origin.x - client.right,
                    outer.bottom - origin.y - client.bottom};
        if (!SetPropW(window, UpdateProperty, &insets)) {
            DeleteObject(region);
            return false;
        }
        bool ok = SetWindowRgn(window, region, FALSE) != 0;
        if (GetPropW(window, UpdateProperty) == &insets)
            RemovePropW(window, UpdateProperty);
        if (!ok)
            DeleteObject(region);
        // Recreate the original backing surface after a shape change, once
        // the placement callback has finished. This is event-driven, not a
        // position/paint polling timer.
        if (ok)
            PostMessageW(window, RegisterWindowMessageW(L"Macdows.WindowStyler.Refresh.v1"), 0, 0);
        return ok;
    }
    void Restore(HWND window) {
        if (!captured_)
            return;
        HRGN region{};
        if (hasRegion_) {
            region = CreateRectRgn(0, 0, 0, 0);
            if (!region || CombineRgn(region, original_, nullptr, RGN_COPY) == ERROR) {
                if (region)
                    DeleteObject(region);
                return;
            }
        }
        captured_ = false;
        if (!SetWindowRgn(window, region, TRUE) && region)
            DeleteObject(region);
    }
};
} // namespace styler
