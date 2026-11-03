#pragma once
#include "protocol.h"
namespace styler {
inline constexpr UINT_PTR UwpInsetAdapter = 262, ApplicationFrameAdapter = 263;
inline constexpr wchar_t EmbeddedFrameProperty[] = L"Macdows.WindowStyler.EmbeddedFrame.v11";
inline constexpr wchar_t EmbeddedInsetProperty[] = L"Macdows.WindowStyler.EmbeddedInset.v11";
inline constexpr wchar_t EmbeddedCaptionTopProperty[] = L"Macdows.WindowStyler.EmbeddedCaptionTop.v11";
inline constexpr wchar_t EmbeddedCaptionHeightProperty[] = L"Macdows.WindowStyler.EmbeddedCaptionHeight.v11";
inline UINT EmbeddedActionMessage() {
    static UINT message = RegisterWindowMessageW(L"Macdows.WindowStyler.EmbeddedAction.v11");
    return message;
}
inline bool WindowClass(HWND w, const wchar_t *expected) {
    wchar_t name[128]{};
    return GetClassNameW(w, name, 128) && wcscmp(name, expected) == 0;
}
inline bool EmbeddedCore(HWND core, HWND frame) {
    DWORD app{}, shell{};
    GetWindowThreadProcessId(core, &app);
    GetWindowThreadProcessId(frame, &shell);
    return IsWindow(core) && IsWindow(frame) && WindowClass(frame, L"ApplicationFrameWindow") &&
           WindowClass(core, L"Windows.UI.Core.CoreWindow") && app && shell && app != shell &&
           GetAncestor(core, GA_ROOT) == frame && IsChild(frame, core) && IsWindowVisible(frame) &&
           IsWindowVisible(core);
}
inline HWND FrameContent(HWND frame) {
    struct Search {
        HWND frame{}, core{};
        unsigned count{};
    } s{frame};
    if (!WindowClass(frame, L"ApplicationFrameWindow"))
        return nullptr;
    EnumChildWindows(
        frame,
        [](HWND w, LPARAM p) -> BOOL {
            auto &s = *reinterpret_cast<Search *>(p);
            if (EmbeddedCore(w, s.frame)) {
                s.core = w;
                ++s.count;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    return s.count == 1 ? s.core : nullptr;
}
} // namespace styler
