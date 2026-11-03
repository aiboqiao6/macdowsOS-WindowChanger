#include "../../native-style-view.h"
#include <iostream>

namespace {
using namespace styler::style_view;
STYLESTRUCT changing{}, changed{};
unsigned checks{}, failures{};
void Check(bool value, const char *label) {
    ++checks;
    if (!value)
        ++failures;
    std::cout << (value ? "PASS " : "FAIL ") << label << '\n';
}
LRESULT CALLBACK Window(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (wp == WPARAM(GWL_STYLE)) {
        if (m == WM_STYLECHANGING)
            changing = *reinterpret_cast<STYLESTRUCT *>(lp);
        if (m == WM_STYLECHANGED)
            changed = *reinterpret_cast<STYLESTRUCT *>(lp);
    }
    return DefWindowProcW(w, m, wp, lp);
}
LRESULT CALLBACK Subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (GetPropW(w, StyleProperty) && wp == WPARAM(GWL_STYLE) && (m == WM_STYLECHANGING || m == WM_STYLECHANGED))
        return MenuStyleMessage(w, m, wp, lp);
    return DefSubclassProc(w, m, wp, lp);
}
} // namespace
int wmain() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = Window;
    cls.lpszClassName = L"Macdows.StyleView.OwnedContract";
    RegisterClassW(&cls);
    auto w = CreateWindowW(cls.lpszClassName, L"Owned style contract", WS_OVERLAPPEDWINDOW, 0, 0, 400, 240, nullptr,
                           nullptr, cls.hInstance, nullptr);
    if (!w)
        return GetLastError();
    SetWindowSubclass(w, Subclass, 1, 0);
    auto original = GetWindowLongPtrW(w, GWL_STYLE);
    constexpr LONG_PTR mask = WS_SYSMENU | CaptionBoxMask;
    SetPropW(w, StyleProperty, reinterpret_cast<HANDLE>(1));
    HideMenu(w);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == CaptionBoxMask,
          "legacy mask changes only the real system-menu bit");
    Check((LongPtrW(w, GWL_STYLE) & mask) == mask, "legacy framework style view preserves all original capabilities");
    Check((changing.styleOld & mask) == mask && (changing.styleNew & mask) == mask,
          "legacy changing notification preserves the host view");
    Check((changed.styleOld & mask) == mask && (changed.styleNew & mask) == mask,
          "legacy changed notification preserves the host view");
    RestoreMenu(w, true);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == (original & mask), "legacy restore returns the physical style");
    SetPropW(w, StyleProperty, reinterpret_cast<HANDLE>(1));
    SetPropW(w, CaptionBoxesProperty, reinterpret_cast<HANDLE>(CaptionBoxMask + 1));
    HideMenu(w);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == 0, "extended mask removes all three physical caption buttons");
    Check((LongPtrW(w, GWL_STYLE) & mask) == mask, "extended style view preserves original button capabilities");
    WINDOWINFO info{sizeof(info)};
    Check(Info(w, &info) && (info.dwStyle & mask) == mask, "GetWindowInfo wrapper agrees with the extended style view");
    Check((changed.styleOld & mask) == mask && (changed.styleNew & mask) == mask,
          "extended internal transition remains invisible to the host");
    SetWindowLongPtrW(w, GWL_STYLE, (original & ~WS_MAXIMIZEBOX));
    auto wanted = LONG_PTR(WS_SYSMENU | WS_MINIMIZEBOX);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == 0, "host capability changes keep native buttons suppressed");
    Check((LongPtrW(w, GWL_STYLE) & mask) == wanted, "host removal of maximize is reflected in the logical view");
    Check((changing.styleOld & mask) == mask && (changing.styleNew & mask) == wanted,
          "changing callback receives original old and requested new capabilities");
    Check((changed.styleOld & mask) == mask && (changed.styleNew & mask) == wanted,
          "changed callback retains previous box bits after a host change");
    RestoreMenu(w, true);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == wanted,
          "restore preserves the host's latest caption capabilities");
    Check(!GetPropW(w, StyleProperty) && !GetPropW(w, CaptionBoxesProperty) &&
              !GetPropW(w, PreviousCaptionBoxesProperty) && !GetPropW(w, RestoreCaptionBoxesProperty),
          "restore removes every extended style property");
    RestoreMenu(w, true);
    Check((GetWindowLongPtrW(w, GWL_STYLE) & mask) == wanted, "repeated restore is idempotent");
    RemoveWindowSubclass(w, Subclass, 1);
    DestroyWindow(w);
    std::cout << "checks=" << checks << "; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
