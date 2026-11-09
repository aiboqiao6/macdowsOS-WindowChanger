#include "caption-colors.h"
#include "chrome.h"
#include <windowsx.h>
#include <filesystem>
#include <fstream>
#include <iostream>
extern "C" __declspec(dllimport) HRESULT WINAPI RequestCaptionTheme(HWND, BOOL, COLORREF, COLORREF);
extern "C" __declspec(dllimport) HRESULT WINAPI ReadCaptionColor(HWND, DWORD, COLORREF *);

namespace {
unsigned checks{}, failures{}, nativeClicks{};
void Check(bool ok, const char *description) {
    ++checks;
    failures += !ok;
    std::cout << (ok ? "PASS " : "FAIL ") << description << std::endl;
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK) {
        ++nativeClicks;
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
void Pump() {
    const auto end = GetTickCount64() + 100;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}
bool SaveTitle(HWND window, const std::filesystem::path &path, COLORREF &sample, COLORREF foreground,
               unsigned &textPixels, bool print = false) {
    struct PhysicalScope {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~PhysicalScope() {
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } physicalScope;
    RECT rect{};
    GetWindowRect(window, &rect);
    int top = static_cast<int>(
        SendMessageW(window, styler::DiagnosticMessage(), static_cast<WPARAM>(styler::Metric::CaptionTop), 0));
    int bottom = static_cast<int>(
        SendMessageW(window, styler::DiagnosticMessage(), static_cast<WPARAM>(styler::Metric::CaptionBottom), 0));
    if (bottom <= top) {
        auto dpi = GetDpiForWindow(window);
        top = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        bottom = top + GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
    }
    int width = rect.right - rect.left, height = bottom;
    if (width < 100 || width > 4096 || height <= top || height > 256)
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
    bool ok = old && (print ? PrintWindow(window, dc, PW_RENDERFULLCONTENT)
                            : BitBlt(dc, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT));
    if (ok) {
        GdiFlush();
        sample = GetPixel(dc, width - MulDiv(24, GetDpiForWindow(window), 96), (top + bottom) / 2);
        const int inset = MulDiv(95, GetDpiForWindow(window), 96);
        for (int y = top + 2; y < bottom - 2; ++y)
            for (int x = inset; x < width - inset; ++x) {
                auto pixel = GetPixel(dc, x, y);
                if (abs(int(GetRValue(pixel)) - int(GetRValue(foreground))) < 12 &&
                    abs(int(GetGValue(pixel)) - int(GetGValue(foreground))) < 12 &&
                    abs(int(GetBValue(pixel)) - int(GetBValue(foreground))) < 12)
                    ++textPixels;
            }
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
        auto bytes = static_cast<DWORD>(width * height * 4);
        file.bfSize = file.bfOffBits + bytes;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(&file), sizeof(file));
        output.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
        output.write(static_cast<const char *>(pixels), bytes);
        ok = output.good();
    }
    if (old)
        SelectObject(dc, old);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    const bool unaware = argc >= 3 && wcscmp(argv[2], L"--dpi-unaware") == 0;
    SetProcessDpiAwarenessContext(unaware ? DPI_AWARENESS_CONTEXT_UNAWARE : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto output = argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"caption-policy");
    std::filesystem::create_directories(output);
    auto light = styler::ResolveCaptionColors(true, true, RGB(245, 245, 245), DWMWA_COLOR_DEFAULT);
    auto dark = styler::ResolveCaptionColors(false, true, RGB(16, 16, 16), DWMWA_COLOR_DEFAULT);
    Check(!styler::DarkCaption(light.background) && styler::DarkCaption(light.foreground),
          "explicit light caption overrides a dark-mode hint and selects dark text");
    Check(styler::DarkCaption(dark.background) && !styler::DarkCaption(dark.foreground),
          "explicit dark caption overrides a light-mode hint and selects light text");
    Check(styler::ResolveCaptionColors(false, false, RGB(20, 30, 40), RGB(10, 210, 160)).foreground ==
              RGB(10, 210, 160),
          "explicit application text color is preserved");
    Check(styler::ResolveCaptionColors(true, false, DWMWA_COLOR_NONE, DWMWA_COLOR_DEFAULT).background ==
              RGB(32, 32, 32),
          "DWM color sentinels are not interpreted as RGB values");
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"Macdows.CaptionPolicy.Owned";
    cls.lpfnWndProc = Procedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    auto foreground = GetForegroundWindow();
    auto owner = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    auto window =
        CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, L"Owned caption theme and input verification",
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 740, 420, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window || !owner) {
        Check(false, "create only self-owned windows");
        if (window)
            DestroyWindow(window);
        if (owner)
            DestroyWindow(owner);
        return 1;
    }
    SetForegroundWindow(window);
    Pump();
    RECT original{};
    GetWindowRect(window, &original);
    TITLEBARINFOEX before{sizeof(before)};
    SendMessageW(window, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&before));
    Check(!IsRectEmpty(&before.rgrect[5]), "owned fixture starts with real native caption controls");
    Check(SUCCEEDED(RequestCaptionTheme(window, TRUE, RGB(245, 245, 245), RGB(20, 40, 60))),
          "host requests explicit colors before attachment");
    Pump();
    COLORREF rawCaption = 0x12345678, rawText = 0x12345678;
    auto captionResult = ReadCaptionColor(window, DWMWA_CAPTION_COLOR, &rawCaption);
    auto textResult = ReadCaptionColor(window, DWMWA_TEXT_COLOR, &rawText);
    COLORREF initialPixel{};
    unsigned initialTextPixels{};
    Check(SaveTitle(window, output / L"before.bmp", initialPixel, RGB(20, 40, 60), initialTextPixels) &&
              initialPixel == RGB(245, 245, 245) && initialTextPixels >= 3,
          "preexisting explicit colors are visible in the actual native frame");
    std::cout << "raw caption hr=" << std::hex << captionResult << " value=" << rawCaption << " text hr=" << textResult
              << " value=" << rawText << " actual=" << initialPixel << std::dec << " text pixels=" << initialTextPixels
              << std::endl;
    initialTextPixels = 0;
    const bool offscreen =
        SaveTitle(window, output / L"before-print.bmp", initialPixel, RGB(20, 40, 60), initialTextPixels, true);
    if (!unaware)
        Check(offscreen && initialPixel == RGB(245, 245, 245) && initialTextPixels >= 3,
              "offscreen native frame preserves colors without reading setter-only attributes");
    else
        std::cout << "Legacy PrintWindow is not a composed-DWM color source; verify the actual screen pixels below."
                  << std::endl;
    const bool attached = styler::chrome::Attach(window, owner) == ERROR_SUCCESS;
    Check(attached, "attach the real nonclient renderer on its owning GUI thread");
    if (attached) {
        Pump();
        initialTextPixels = 0;
        Check(SaveTitle(window, output / L"initial-attached.bmp", initialPixel, RGB(20, 40, 60), initialTextPixels) &&
                  initialPixel == RGB(245, 245, 245) && initialTextPixels >= 3,
              "attachment preserves the preexisting application caption and text colors in actual pixels");
        Check(styler::chrome::Detach(window, owner), "detach without subsequent host color requests");
        Pump();
        initialTextPixels = 0;
        Check(SaveTitle(window, output / L"initial-restored.bmp", initialPixel, RGB(20, 40, 60), initialTextPixels) &&
                  initialPixel == RGB(245, 245, 245) && initialTextPixels >= 3,
              "detach without requests never replaces unreadable initial colors with defaults");
        Check(styler::chrome::Attach(window, owner) == ERROR_SUCCESS, "reattach the original theme");
        SendMessageW(window, WM_THEMECHANGED, 0, 0);
        Pump();
        initialTextPixels = 0;
        Check(SaveTitle(window, output / L"initial-refreshed.bmp", initialPixel, RGB(20, 40, 60), initialTextPixels) &&
                  initialPixel == RGB(245, 245, 245) && initialTextPixels >= 3,
              "a host refresh preserves existing colors without new color requests");
        for (BOOL darkMode : {TRUE, FALSE}) {
            Check(SUCCEEDED(RequestCaptionTheme(window, darkMode, DWMWA_COLOR_DEFAULT, DWMWA_COLOR_DEFAULT)),
                  "host requests system colors with an explicit per-window dark-mode hint");
            Pump();
            COLORREF reportedMode{};
            Check(SUCCEEDED(ReadCaptionColor(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &reportedMode)) &&
                      reportedMode == static_cast<COLORREF>(darkMode),
                  "track setter-only dark-mode requests as well as explicit RGB colors");
            auto palette =
                styler::ResolveCaptionColors(darkMode != FALSE, true, DWMWA_COLOR_DEFAULT, DWMWA_COLOR_DEFAULT);
            unsigned textPixels{};
            Check(SaveTitle(window, output / (darkMode ? L"dark-default.bmp" : L"light-default.bmp"), initialPixel,
                            palette.foreground, textPixels) &&
                      initialPixel == palette.background && textPixels >= 3,
                  "actual caption background and text follow the host's dark-mode change");
        }
        TITLEBARINFOEX hidden{sizeof(hidden)};
        SendMessageW(window, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&hidden));
        bool suppressed = true;
        for (int i = 2; i <= 5; ++i)
            suppressed = suppressed && IsRectEmpty(&hidden.rgrect[i]) &&
                         (hidden.rgstate[i] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)) ==
                             (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE);
        Check(suppressed, "native accessibility metadata exposes no active old caption buttons");
        auto clicks = nativeClicks;
        for (auto hit : {HTCLOSE, HTMINBUTTON, HTMAXBUTTON})
            for (auto message : {WM_NCLBUTTONDOWN, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK})
                SendMessageW(window, message, hit, MAKELPARAM(original.right - 30, original.top + 20));
        Check(nativeClicks == clicks && IsWindow(window), "stale old-button messages never reach the host handler");
        for (int i = 0; i < 4; ++i) {
            BOOL hint = (i & 1) != 0;
            COLORREF background = (i & 1) ? RGB(245, 245, 245) : RGB(16, 16, 16);
            COLORREF foregroundColor = i >= 2 ? RGB(10, 180, 130) : DWMWA_COLOR_DEFAULT;
            // Exercise a host's normal theme notification and subsequent
            // explicit public color requests while native drawing is disabled.
            SendMessageW(window, WM_THEMECHANGED, 0, 0);
            Check(SUCCEEDED(RequestCaptionTheme(window, hint, background, foregroundColor)),
                  "change only the self-owned window's theme attributes");
            COLORREF requested = DWMWA_COLOR_DEFAULT;
            Check(SUCCEEDED(ReadCaptionColor(window, DWMWA_CAPTION_COLOR, &requested)) && requested == background,
                  "host reads its latest requested color while native rendering is suppressed");
            SendMessageW(window, WM_NCPAINT, 1, 0);
            Pump();
            COLORREF actual = CLR_INVALID;
            unsigned textPixels{};
            auto expected = styler::ResolveCaptionColors(hint != FALSE, true, background, foregroundColor);
            Check(SaveTitle(window, output / (L"theme-" + std::to_wstring(i) + L".bmp"), actual, expected.foreground,
                            textPixels),
                  "capture the actual composed title frame");
            if (actual != background) {
                COLORREF reported = DWMWA_COLOR_DEFAULT;
                DwmGetWindowAttribute(window, DWMWA_CAPTION_COLOR, &reported, sizeof(reported));
                HIGHCONTRASTW contrast{sizeof(contrast)};
                SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
                std::cout << "theme=" << i << " pixel=" << std::hex << actual << " requested=" << background
                          << " reported=" << reported << " contrast=" << contrast.dwFlags << std::dec << std::endl;
            }
            Check(actual == background, "actual rendered title follows the application caption color");
            Check(textPixels >= 3, "actual title text follows the explicit color or readable contrast");
        }
        Check(styler::chrome::Detach(window, owner), "detach the real renderer");
        Pump();
        COLORREF restoredCaption{}, restoredText{};
        unsigned restoredTextPixels{};
        // Microsoft documents these attributes for Set, not Get. Assert the
        // actual restored DWM pixels and the restored API behavior separately.
        Check(SaveTitle(window, output / L"restored.bmp", restoredCaption, RGB(10, 180, 130), restoredTextPixels) &&
                  restoredCaption == RGB(245, 245, 245) && restoredTextPixels >= 3 &&
                  !GetPropW(window, styler::CaptionColorOwner) &&
                  !GetPropW(window, styler::CaptionColorProperties[0]) &&
                  !GetPropW(window, styler::CaptionColorProperties[1]),
              "detach restores the host's latest colors in actual pixels and removes tracking state");
        Check(ReadCaptionColor(window, DWMWA_CAPTION_COLOR, &restoredCaption) == captionResult &&
                  ReadCaptionColor(window, DWMWA_TEXT_COLOR, &restoredText) == textResult,
              "detach restores the original host API behavior including unsupported getters");
        TITLEBARINFOEX restored{sizeof(restored)};
        SendMessageW(window, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&restored));
        Check(!IsRectEmpty(&restored.rgrect[5]) && !(restored.rgstate[5] & STATE_SYSTEM_UNAVAILABLE),
              "detach restores native caption accessibility and input availability");
        RECT current{};
        GetWindowRect(window, &current);
        Check(EqualRect(&original, &current), "all theme and input transitions preserve window geometry");
    }
    DestroyWindow(window);
    DestroyWindow(owner);
    if (IsWindow(foreground))
        SetForegroundWindow(foreground);
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    return failures ? 1 : 0;
}
