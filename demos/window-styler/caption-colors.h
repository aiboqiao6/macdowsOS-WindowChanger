#pragma once
#include <windows.h>
#include <dwmapi.h>

namespace styler {
inline constexpr wchar_t CaptionColorOwner[] = L"Macdows.WindowStyler.NativeCaptionColors.v1";
inline constexpr wchar_t CaptionDarkModeProperty[] = L"Macdows.WindowStyler.NativeDarkMode.v1";
inline constexpr const wchar_t *CaptionColorProperties[]{L"Macdows.WindowStyler.NativeCaptionColor.v1",
                                                         L"Macdows.WindowStyler.NativeTextColor.v1"};
inline int CaptionColorIndex(DWORD attribute) {
    return attribute == DWMWA_CAPTION_COLOR ? 0 : attribute == DWMWA_TEXT_COLOR ? 1 : -1;
}
inline UINT CaptionColorMessage() {
    static UINT message = RegisterWindowMessageW(L"Macdows.WindowStyler.CaptionColorsChanged.v1");
    return message;
}
inline bool TrackCaptionColor(HWND window, DWORD attribute, COLORREF value) {
    auto index = CaptionColorIndex(attribute);
    if (index < 0)
        return false;
    // Encode documented RGB colors and both DWM sentinels in a non-null value
    // that fits x86. Default must differ from an uncaptured property.
    ULONG_PTR encoded = value == DWMWA_COLOR_DEFAULT ? 0x1000001u
                        : value == DWMWA_COLOR_NONE  ? 0x1000002u
                                                     : ULONG_PTR(value) + 1;
    return SetPropW(window, CaptionColorProperties[index], reinterpret_cast<HANDLE>(encoded)) != FALSE;
}
inline bool TrackedCaptionColor(HWND window, DWORD attribute, COLORREF &value) {
    const auto index = CaptionColorIndex(attribute);
    if (index < 0 || !GetPropW(window, CaptionColorOwner))
        return false;
    auto encoded = reinterpret_cast<ULONG_PTR>(GetPropW(window, CaptionColorProperties[index]));
    if (!encoded)
        return false;
    value = encoded == 0x1000001u   ? DWMWA_COLOR_DEFAULT
            : encoded == 0x1000002u ? DWMWA_COLOR_NONE
                                    : static_cast<COLORREF>(encoded - 1);
    return true;
}
inline void ClearCaptionColors(HWND window) {
    RemovePropW(window, CaptionColorOwner);
    RemovePropW(window, CaptionDarkModeProperty);
    for (auto property : CaptionColorProperties)
        RemovePropW(window, property);
}
inline bool TrackedCaptionDarkMode(HWND window, BOOL &dark) {
    auto value = GetPropW(window, CaptionDarkModeProperty);
    if (!GetPropW(window, CaptionColorOwner) || !value)
        return false;
    dark = reinterpret_cast<ULONG_PTR>(value) == 2;
    return true;
}
struct CaptionColors {
    COLORREF background{}, foreground{}, separator{};
};
inline bool ExplicitDwmColor(COLORREF value) {
    return value != DWMWA_COLOR_DEFAULT && value != DWMWA_COLOR_NONE;
}
inline bool DarkCaption(COLORREF value) {
    return 299 * GetRValue(value) + 587 * GetGValue(value) + 114 * GetBValue(value) < 128000;
}
// The app's explicit caption/text colors take precedence over its dark-mode hint.
// Resolve foreground from the actual background when only a caption color exists.
inline CaptionColors ResolveCaptionColors(bool dark, bool active, COLORREF caption, COLORREF text) {
    CaptionColors result;
    result.background = ExplicitDwmColor(caption) ? caption
                        : dark                    ? RGB(32, 32, 32)
                        : active                  ? RGB(244, 243, 241)
                                                  : RGB(247, 247, 246);
    const bool backgroundDark = DarkCaption(result.background);
    result.foreground = ExplicitDwmColor(text) ? text
                        : backgroundDark       ? RGB(235, 235, 235)
                        : active               ? RGB(58, 57, 55)
                                               : RGB(142, 141, 139);
    result.separator = backgroundDark ? RGB(55, 55, 55) : RGB(218, 217, 215);
    return result;
}
} // namespace styler
