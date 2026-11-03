#include "protocol.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <vector>

namespace {
int windows = 0;
constexpr wchar_t KindProperty[] = L"Macdows.WindowStyler.Sample.Kind";
HFONT CreateBodyFont(UINT dpi) {
    return CreateFontW(-MulDiv(15, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Microsoft YaHei UI");
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SYSCOMMAND: {
        auto depth = reinterpret_cast<UINT_PTR>(GetPropW(window, L"Macdows.Sample.CommandDepth"));
        SetPropW(window, L"Macdows.Sample.CommandDepth", reinterpret_cast<HANDLE>(depth + 1));
        SetPropW(window, L"Macdows.Sample.LastCommand", reinterpret_cast<HANDLE>(wParam));
        if ((wParam & 0xfff0) == SC_MAXIMIZE && GetPropW(window, L"Macdows.Sample.FullscreenMaxCommand")) {
            SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) & ~WS_CAPTION);
            ShowWindow(window, SW_MAXIMIZE);
            SetPropW(window, L"Macdows.Sample.CommandDepth", reinterpret_cast<HANDLE>(depth));
            return 0;
        }
        auto result = DefWindowProcW(window, message, wParam, lParam);
        if (IsWindow(window))
            SetPropW(window, L"Macdows.Sample.CommandDepth", reinterpret_cast<HANDLE>(depth));
        return result;
    }
    case WM_NCCREATE: {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetPropW(window, KindProperty,
                 reinterpret_cast<HANDLE>(reinterpret_cast<UINT_PTR>(create->lpCreateParams) + 1));
        break;
    }
    case WM_NCCALCSIZE:
        if (reinterpret_cast<UINT_PTR>(GetPropW(window, KindProperty)) >= 3)
            return 0;
        break;
    case WM_NCHITTEST:
        if (reinterpret_cast<UINT_PTR>(GetPropW(window, KindProperty)) >= 3)
            return HTCLIENT;
        break;
    case WM_CREATE: {
        ++windows;
        SetPropW(window, styler::SampleProperty, reinterpret_cast<HANDLE>(1));
        if (GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) {
            SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS, 0, 100, 10, 30};
            SetScrollInfo(window, SB_VERT, &scroll, TRUE);
            SetScrollInfo(window, SB_HORZ, &scroll, TRUE);
        }
        HFONT body = CreateBodyFont(GetDpiForWindow(window));
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(body));
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                    L"这是独立的 Win32 "
                                    L"进程。\r\n\r\n你可以在这里输入，测试标题栏变化后原有控件是否仍然正常。\r\n\r\n试"
                                    L"试拖动窗口、调整大小、双击标题栏，以及左上角的红黄绿按钮。",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 28,
                                    146, 560, 200, window, reinterpret_cast<HMENU>(200), nullptr, nullptr);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        return 0;
    }
    case WM_SIZE: {
        UINT dpi = GetDpiForWindow(window);
        int margin = MulDiv(28, dpi, 96);
        int top = MulDiv(146, dpi, 96);
        RECT client{};
        GetClientRect(window, &client);
        MoveWindow(GetDlgItem(window, 200), margin, top, std::max(1L, client.right - 2 * margin),
                   std::max(1L, client.bottom - top - margin), TRUE);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    case WM_DPICHANGED: {
        auto previousFont = reinterpret_cast<HFONT>(GetWindowLongPtrW(window, GWLP_USERDATA));
        HFONT body = CreateBodyFont(HIWORD(wParam));
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(body));
        SendMessageW(GetDlgItem(window, 200), WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        DeleteObject(previousFont);
        const auto *rect = reinterpret_cast<const RECT *>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 301)
            SetWindowTextW(window, L"标题已更新 · 原生菜单正常工作");
        if (LOWORD(wParam) == 302)
            PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_APP + 42:
        // Fault injection used by the integration test for a hung GUI thread.
        Sleep(static_cast<DWORD>(std::min<WPARAM>(wParam, 10000)));
        return 0;
    case WM_APP + 43:
        DeleteMenu(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH brush = CreateSolidBrush(RGB(253, 252, 250));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        UINT dpi = GetDpiForWindow(window);
        auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(GetWindowLongPtrW(window, GWLP_USERDATA)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(56, 64, 75));
        RECT text{scale(28), scale(28), client.right - scale(28), scale(58)};
        DrawTextW(dc, L"A familiar window. A different feeling.", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text.top = scale(70);
        text.bottom = scale(128);
        SetTextColor(dc, RGB(113, 119, 128));
        DrawTextW(dc, L"客户区由这个程序自己绘制。\n标题栏由 Window Lab 在进程内替换，菜单和输入框保持原生行为。", -1,
                  &text, DT_LEFT | DT_WORDBREAK);
        SelectObject(dc, old);
        EndPaint(window, &paint);
        auto count = reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty));
        SetPropW(window, styler::SamplePaintProperty, reinterpret_cast<HANDLE>(count + 1));
        return 0;
    }
    case WM_DESTROY:
        DeleteObject(reinterpret_cast<HFONT>(GetWindowLongPtrW(window, GWLP_USERDATA)));
        if (--windows == 0)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = Procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    const wchar_t *classes[] = {styler::TargetClass, styler::TargetClass, L"Qt6QWindowIcon.Sample",
                                L"Chrome_WidgetWin_1.Sample", L"Macdows.CustomClient.Sample"};
    for (const auto *name : classes) {
        cls.lpszClassName = name;
        RegisterClassExW(&cls);
    }
    bool frameworkPolicy = wcsstr(command, L"--custom-frameworks") != nullptr;
    if (frameworkPolicy) {
        // These are deliberately ordinary native frames with framework evidence.
        // They catch unsafe native fallback when minimized client geometry is zero.
        const wchar_t *evidence[]{L"Microsoft.UI.Content.DesktopChildSiteBridge", L"BCGPRibbonBar:Sample", L"NetUIHWND",
                                  L"TXGuiFoundation"};
        for (int i = 0; i < 4; i++) {
            WNDCLASSEXW marker{sizeof(marker)};
            marker.hInstance = instance;
            marker.lpfnWndProc = i == 3 ? Procedure : DefWindowProcW;
            marker.lpszClassName = evidence[i];
            RegisterClassExW(&marker);
            auto w = CreateWindowExW(0, i == 3 ? evidence[i] : styler::TargetClass, L"Framework policy fixture",
                                     WS_OVERLAPPEDWINDOW, 60 + i * 30, 60 + i * 30, 640, 420, nullptr, nullptr,
                                     instance, nullptr);
            if (i < 3)
                CreateWindowExW(0, evidence[i], L"", WS_CHILD | WS_VISIBLE, 0, 0, 300, 50, w, nullptr, instance,
                                nullptr);
            ShowWindow(w, SW_SHOWNOACTIVATE);
        }
    }
    bool pair = wcsstr(command, L"--pair") != nullptr;
    bool variants = wcsstr(command, L"--variants") != nullptr;
    const wchar_t *titles[] = {L"Win32 Sample · Native frame", L"Win32 Sample · Native menu",
                               L"Sample · Qt-like frameless", L"Sample · Chromium-like custom frame",
                               L"Sample · Unknown custom frame"};
    for (int i = 0; i < (frameworkPolicy ? 0 : variants ? 5 : pair ? 2 : 1); ++i) {
        HMENU menu{};
        if (i == 1 || wcsstr(command, L"--menu")) {
            menu = CreateMenu();
            HMENU file = CreatePopupMenu();
            AppendMenuW(file, MF_STRING, 301, L"更新窗口标题");
            AppendMenuW(file, MF_STRING, 302, L"关闭");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");
        }
        UINT dpi = GetDpiForSystem();
        DWORD style =
            i == 2 ? WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX : WS_OVERLAPPEDWINDOW;
        if (i == 1)
            style |= WS_VSCROLL | WS_HSCROLL;
        HWND window = CreateWindowExW(0, classes[i], titles[i], style | WS_CLIPCHILDREN, 120 + i * 50, 90 + i * 35,
                                      MulDiv(680, dpi, 96), MulDiv(480, dpi, 96), nullptr, menu, instance,
                                      reinterpret_cast<void *>(static_cast<UINT_PTR>(i)));
        if (!window)
            return 1;
        ShowWindow(window, show);
        UpdateWindow(window);
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
