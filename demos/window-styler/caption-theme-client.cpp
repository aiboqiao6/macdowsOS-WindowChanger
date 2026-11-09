#include <windows.h>
#include <dwmapi.h>

// A separate host module uses ordinary imported DWM calls, without linking
// the renderer or its color-tracking internals.
extern "C" __declspec(dllexport) HRESULT WINAPI RequestCaptionTheme(HWND window, BOOL dark, COLORREF caption,
                                                                    COLORREF text) {
    auto result = DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (SUCCEEDED(result))
        result = DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    if (SUCCEEDED(result))
        result = DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
    return result;
}
extern "C" __declspec(dllexport) HRESULT WINAPI ReadCaptionColor(HWND window, DWORD attribute, COLORREF *value) {
    return DwmGetWindowAttribute(window, attribute, value, sizeof(*value));
}
