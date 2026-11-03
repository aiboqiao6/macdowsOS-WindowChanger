#include "session.h"
#include "host-client.h"
#include "inline.h"
#include "controller-settings.h"
#include "auto-manage.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <windowsx.h>
#include <memory>
#include <array>
#include <filesystem>
#include <cmath>
#include <objbase.h>

namespace {
constexpr int IdList = 100, IdRefresh = 102, IdApply = 103, IdRestore = 104, IdRestoreAll = 105, IdAuto = 108,
              IdStartup = 109;
constexpr int IdShow = 120, IdExit = 123;
constexpr UINT TrayMessage = WM_APP + 21;
constexpr wchar_t PrimaryProperty[] = L"Macdows.WindowChanger.Primary.v3";
struct App {
    HWND window{}, list{}, automatic{}, startup{};
    std::array<HWND, 4> buttons{};
    HFONT body{}, heading{}, detailFont{};
    HICON icon{};
    UINT dpi{96};
    std::unique_ptr<styler::GuardedSession> session;
    styler::settings::Scope scope;
    styler::AutoManageQueue queue;
    std::vector<styler::Candidate> candidates;
    styler::settings::StartupStatus startupStatus;
    std::wstring executable, instanceProperty, status{L"准备就绪"};
    DWORD scopePid{}, lastSettingsError{};
    unsigned trayAdds{}, autoAttempts{}, autoAccepted{};
    bool manageAll{true}, tray{}, background{}, paused{}, exiting{}, scanning{}, explorerMessages{};
    int selected{-1};
    ULONGLONG lastRefresh{};
    int Scale(int n) const {
        return MulDiv(n, static_cast<int>(dpi), 96);
    }
} app;
UINT ShowMessage() {
    return RegisterWindowMessageW(L"Macdows.WindowChanger.Show.v3");
}
UINT TaskbarMessage() {
    return RegisterWindowMessageW(L"TaskbarCreated");
}
UINT TestMessage() {
    return RegisterWindowMessageW(L"Macdows.WindowChanger.Test.v3");
}

void Refresh();
void Layout();
void ManageWindows();
void UpdateState() {
    if (!app.window || !app.session)
        return;
    SetPropW(app.window, L"Macdows.WindowStyler.ManagedCount.v2", reinterpret_cast<HANDLE>(app.session->Count()));
    SetPropW(app.window, L"Macdows.WindowStyler.AutoManage.v2",
             reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(app.manageAll)));
    Button_SetCheck(app.automatic, app.manageAll ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(app.startup, app.startupStatus.enabled ? BST_CHECKED : BST_UNCHECKED);
    InvalidateRect(app.window, nullptr, FALSE);
}
HICON MakeIcon(int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits{};
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::vector<BYTE> maskBits(static_cast<size_t>((size + 15) / 16 * 2) * size, 0);
    auto mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!bitmap || !mask) {
        if (bitmap)
            DeleteObject(bitmap);
        if (mask)
            DeleteObject(mask);
        return nullptr;
    }
    auto pixels = static_cast<DWORD *>(bits);
    constexpr DWORD colors[]{0xff5f57, 0xffbd2e, 0x28c840};
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            DWORD pixel{};
            for (int i = 0; i < 3; ++i) {
                auto cx = size * (0.2 + i * 0.3), cy = size * 0.5, r = size * 0.13;
                auto distance = std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
                auto alpha = static_cast<DWORD>(255 * std::clamp(r + 0.5 - distance, 0.0, 1.0));
                if (alpha) {
                    auto c = colors[i];
                    pixel = (alpha << 24) | ((((c >> 16) & 255) * alpha / 255) << 16) |
                            ((((c >> 8) & 255) * alpha / 255) << 8) | ((c & 255) * alpha / 255);
                }
            }
            pixels[y * size + x] = pixel;
        }
    ICONINFO icon{TRUE, 0, 0, mask, bitmap};
    auto result = CreateIconIndirect(&icon);
    DeleteObject(mask);
    DeleteObject(bitmap);
    return result;
}
bool AddTray() {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = app.window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = app.icon;
    wcscpy_s(data.szTip, L"macdowsOS WindowChanger");
    app.tray = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (!app.tray)
        app.tray = Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
    if (app.tray) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
        ++app.trayAdds;
    }
    return app.tray;
}
void RemoveTray() {
    if (app.tray) {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = app.window;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        app.tray = false;
    }
}
void ShowMain() {
    ShowWindow(app.window, IsIconic(app.window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(app.window);
    Refresh();
}
void HideMain() {
    if (!app.tray && !AddTray()) {
        app.status = L"托盘不可用，窗口保持显示";
        ShowMain();
        return;
    }
    ShowWindow(app.window, SW_HIDE);
}
void SetAutomatic(bool enabled, bool save = true) {
    app.manageAll = enabled;
    if (enabled)
        app.queue.Reset();
    if (save) {
        app.lastSettingsError = styler::settings::SaveAutoManage(app.scope, enabled);
        app.status = app.lastSettingsError ? L"设置保存失败"
                     : enabled             ? L"自动接管已开启"
                                           : L"自动接管已暂停，现有外观保留";
    }
    UpdateState();
}
void ToggleStartup() {
    auto current = styler::settings::QueryStartup(app.scope, app.executable);
    auto result = FAILED(current.result) ? current.result
                                         : styler::settings::SetStartup(app.scope, app.executable, !current.enabled);
    app.lastSettingsError = static_cast<DWORD>(result);
    app.startupStatus = styler::settings::QueryStartup(app.scope, app.executable);
    app.status = FAILED(result)              ? L"开机自启设置失败，请检查管理员权限或计划任务冲突"
                 : app.startupStatus.enabled ? L"已启用登录后后台启动"
                                             : L"已关闭开机自启";
    UpdateState();
}
void Text(HDC dc, const std::wstring &value, RECT rect, HFONT font, COLORREF color,
          UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}
void Fonts() {
    for (auto font : {app.body, app.heading, app.detailFont})
        if (font)
            DeleteObject(font);
    auto make = [](int size, int weight) {
        return CreateFontW(-app.Scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    };
    app.body = make(13, FW_NORMAL);
    app.heading = make(23, FW_SEMIBOLD);
    app.detailFont = make(11, FW_NORMAL);
    for (auto control : app.buttons)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.body), TRUE);
    for (auto control : {app.list, app.automatic, app.startup})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.body), TRUE);
}
void Selection() {
    app.selected = ListView_GetNextItem(app.list, -1, LVNI_SELECTED);
    auto candidate = app.selected >= 0 && app.selected < static_cast<int>(app.candidates.size())
                         ? &app.candidates[app.selected]
                         : nullptr;
    EnableWindow(app.buttons[1], candidate && candidate->supported && !app.session->Owns(candidate->window));
    EnableWindow(app.buttons[2], candidate && app.session->Owns(candidate->window));
    InvalidateRect(app.window, nullptr, FALSE);
}
bool InScope(const styler::Candidate &candidate) {
    return candidate.pid != GetCurrentProcessId() && (!app.scopePid || candidate.pid == app.scopePid) &&
           IsWindowVisible(candidate.window) && !candidate.title.empty() &&
           (candidate.supported || (GetWindowLongPtrW(candidate.window, GWL_STYLE) & WS_CAPTION)) &&
           !(GetWindowLongPtrW(candidate.window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
}
void Refresh() {
    HWND previous = app.selected >= 0 && app.selected < static_cast<int>(app.candidates.size())
                        ? app.candidates[app.selected].window
                        : nullptr;
    auto candidates = styler::EnumerateInline();
    std::erase_if(candidates, [](const auto &candidate) { return !InScope(candidate); });
    app.candidates = std::move(candidates);
    SendMessageW(app.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(app.list);
    int selected = -1;
    for (int i = 0; i < static_cast<int>(app.candidates.size()); ++i) {
        auto &candidate = app.candidates[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = candidate.title.data();
        ListView_InsertItem(app.list, &item);
        auto kind = candidate.framework;
        ListView_SetItemText(app.list, i, 1, kind.data());
        std::wstring state = app.session->Owns(candidate.window) ? L"已接管"
                             : candidate.supported               ? L"可接管"
                                                                 : L"不支持";
        ListView_SetItemText(app.list, i, 2, state.data());
        if (candidate.window == previous)
            selected = i;
    }
    if (selected < 0 && !app.candidates.empty())
        selected = 0;
    if (selected >= 0)
        ListView_SetItemState(app.list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(app.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(app.list, nullptr, TRUE);
    app.lastRefresh = GetTickCount64();
    Selection();
    UpdateState();
}
void Layout() {
    RECT client{};
    GetClientRect(app.window, &client);
    int width = MulDiv(client.right, 96, app.dpi), height = MulDiv(client.bottom, 96, app.dpi);
    auto move = [](HWND w, int x, int y, int dx, int dy) {
        MoveWindow(w, app.Scale(x), app.Scale(y), app.Scale(dx), app.Scale(dy), TRUE);
    };
    move(app.buttons[0], width - 128, 24, 104, 32);
    move(app.automatic, 24, 88, 170, 26);
    move(app.startup, 216, 88, 170, 26);
    move(app.list, 24, 132, width - 48, std::max(60, height - 268));
    move(app.buttons[1], 24, height - 92, 128, 34);
    move(app.buttons[2], 164, height - 92, 128, 34);
    move(app.buttons[3], width - 152, height - 92, 128, 34);
    RECT listClient{};
    GetClientRect(app.list, &listClient);
    int remaining = listClient.right - app.Scale(1);
    ListView_SetColumnWidth(app.list, 0, std::max(app.Scale(190), remaining - app.Scale(300)));
    ListView_SetColumnWidth(app.list, 1, app.Scale(190));
    ListView_SetColumnWidth(app.list, 2, app.Scale(110));
    InvalidateRect(app.window, nullptr, TRUE);
}
void ManageWindows() {
    if (app.scanning || !app.manageAll || (!app.scope.testId.empty() && !app.scopePid))
        return;
    app.scanning = true;
    auto now = GetTickCount64();
    auto candidates = styler::EnumerateInline();
    std::vector<std::pair<uintptr_t, uint32_t>> alive;
    unsigned operations{}, added{};
    for (auto &candidate : candidates) {
        if (app.scopePid && candidate.pid != app.scopePid)
            continue;
        alive.emplace_back(reinterpret_cast<uintptr_t>(candidate.window), candidate.pid);
        if (!candidate.supported || app.session->Owns(candidate.window))
            continue;
        auto key = reinterpret_cast<uintptr_t>(candidate.window);
        if (!app.queue.Ready(key, candidate.pid, now, IsWindowVisible(candidate.window) && !IsIconic(candidate.window)))
            continue;
        if (operations >= 2)
            continue;
        ++operations;
        ++app.autoAttempts;
        auto result = app.session->Apply(candidate.window);
        if (result.ok) {
            ++added;
            ++app.autoAccepted;
            app.queue.Allow(key);
        } else {
            app.queue.Failed(key, GetTickCount64());
            app.status = L"部分窗口暂未接管，将延后重试";
        }
    }
    app.queue.Prune(alive);
    app.scanning = false;
    if (added)
        app.status = L"已自动接管 " + std::to_wstring(added) + L" 个窗口";
    if (operations || (IsWindowVisible(app.window) && now - app.lastRefresh > 3000))
        Refresh();
    else
        UpdateState();
}
void Paint() {
    PAINTSTRUCT paint{};
    auto dc = BeginPaint(app.window, &paint);
    RECT client{};
    GetClientRect(app.window, &client);
    auto brush = CreateSolidBrush(RGB(250, 250, 251));
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    int width = MulDiv(client.right, 96, app.dpi), height = MulDiv(client.bottom, 96, app.dpi);
    auto r = [](int x, int y, int dx, int dy) -> RECT {
        return {app.Scale(x), app.Scale(y), app.Scale(x + dx), app.Scale(y + dy)};
    };
    Text(dc, L"macdowsOS WindowChanger", r(24, 16, width - 180, 34), app.heading, RGB(35, 38, 43));
    Text(dc, std::to_wstring(app.session->Count()) + L" 个已接管 · 关闭窗口后驻留托盘", r(24, 54, width - 48, 22),
         app.detailFont, RGB(100, 105, 113));
    if (app.selected >= 0 && app.selected < static_cast<int>(app.candidates.size())) {
        auto &candidate = app.candidates[app.selected];
        Text(dc, candidate.supported ? L"" : candidate.reason, r(24, height - 126, width - 48, 24), app.detailFont,
             RGB(158, 108, 43));
    }
    Text(dc, app.status, r(24, height - 44, width - 48, 22), app.detailFont,
         app.lastSettingsError ? RGB(178, 62, 55) : RGB(76, 104, 86));
    EndPaint(app.window, &paint);
}
void TrayMenu() {
    auto menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, IdShow, L"打开主界面");
    AppendMenuW(menu, MF_STRING | (app.manageAll ? MF_CHECKED : 0), IdAuto, L"自动接管");
    AppendMenuW(menu, MF_STRING | (app.startupStatus.enabled ? MF_CHECKED : 0), IdStartup, L"开机自启");
    AppendMenuW(menu, MF_STRING, IdRestoreAll, L"暂停并全部还原");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, L"还原并退出");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(app.window);
    auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, app.window, nullptr);
    DestroyMenu(menu);
    if (command)
        SendMessageW(app.window, WM_COMMAND, command, 0);
    if (IsWindow(app.window))
        PostMessageW(app.window, WM_NULL, 0, 0);
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == ShowMessage()) {
        ShowMain();
        return 0;
    }
    if (message == TaskbarMessage()) {
        app.tray = false;
        if (!AddTray())
            ShowMain();
        return 0;
    }
    if (message == TestMessage() && !app.scope.testId.empty()) {
        if (wp == 1) {
            ManageWindows();
            return 1;
        }
        if (wp == 2) {
            ShowMain();
            return 1;
        }
        if (wp == 3) {
            HideMain();
            return 1;
        }
        if (wp == 4) {
            RemoveTray();
            SendMessageW(window, TaskbarMessage(), 0, 0);
            return app.tray;
        }
        if (wp == 5)
            return app.tray;
        if (wp == 6)
            return app.manageAll;
        if (wp == 7)
            return app.startupStatus.enabled;
        if (wp == 8)
            return app.session->Count();
        if (wp == 9) {
            SendMessageW(window, WM_COMMAND, IdExit, 0);
            return 1;
        }
        if (wp == 10)
            return app.trayAdds;
        if (wp == 11)
            return app.autoAttempts;
        if (wp == 12)
            return app.lastSettingsError;
        if (wp == 13)
            return app.autoAccepted;
        if (wp == 14)
            return app.scopePid;
        if (wp == 15)
            return app.explorerMessages;
    }
    switch (message) {
    case WM_CREATE: {
        app.window = window;
        app.dpi = GetDpiForWindow(window);
        // Explorer normally runs at medium integrity while this controller is
        // elevated. Admit only the harmless tray callbacks and taskbar event,
        // not WM_COMMAND, test protocol, or arbitrary privileged operations.
        app.explorerMessages = ChangeWindowMessageFilterEx(window, TaskbarMessage(), MSGFLT_ALLOW, nullptr) &&
                               ChangeWindowMessageFilterEx(window, TrayMessage, MSGFLT_ALLOW, nullptr);
        app.session = std::make_unique<styler::GuardedSession>(window);
        app.list =
            CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0,
                            0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdList)), nullptr, nullptr);
        ListView_SetExtendedListViewStyle(app.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        SetWindowTheme(app.list, L"Explorer", nullptr);
        const wchar_t *headers[]{L"窗口", L"类型", L"状态"};
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH};
            column.pszText = const_cast<LPWSTR>(headers[i]);
            column.cx = 120;
            ListView_InsertColumn(app.list, i, &column);
        }
        const wchar_t *labels[]{L"刷新", L"接管选中", L"还原选中", L"全部还原"};
        constexpr int ids[]{IdRefresh, IdApply, IdRestore, IdRestoreAll};
        for (int i = 0; i < 4; ++i)
            app.buttons[i] =
                CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
                                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])), nullptr, nullptr);
        app.automatic =
            CreateWindowExW(0, L"BUTTON", L"自动接管", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
                            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdAuto)), nullptr, nullptr);
        app.startup =
            CreateWindowExW(0, L"BUTTON", L"开机自启", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
                            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdStartup)), nullptr, nullptr);
        app.icon = MakeIcon(GetSystemMetricsForDpi(SM_CXSMICON, app.dpi));
        if (!app.icon)
            app.icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app.icon));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app.icon));
        app.startupStatus = styler::settings::QueryStartup(app.scope, app.executable);
        if (FAILED(app.startupStatus.result))
            app.status = L"开机启动状态读取失败";
        else if (app.startupStatus.enabled && !app.startupStatus.currentPath)
            app.status = L"开机启动指向其他位置，可重新启用以更新路径";
        Fonts();
        Refresh();
        Layout();
        AddTray();
        SetPropW(window, PrimaryProperty, reinterpret_cast<HANDLE>(1));
        SetPropW(window, app.instanceProperty.c_str(), reinterpret_cast<HANDLE>(1));
        SetTimer(window, 4, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        auto command = LOWORD(wp);
        if (command == IdShow) {
            ShowMain();
            return 0;
        }
        if (command == IdExit) {
            app.exiting = true;
            DestroyWindow(window);
            return 0;
        }
        if (command == IdRefresh) {
            Refresh();
            return 0;
        }
        if (command == IdAuto) {
            SetAutomatic(!app.manageAll);
            return 0;
        }
        if (command == IdStartup) {
            ToggleStartup();
            return 0;
        }
        if (command == IdRestoreAll) {
            SetAutomatic(false);
            auto result = app.session->RestoreAll();
            app.status = result.message;
            Refresh();
            return 0;
        }
        if (app.selected >= 0 && app.selected < static_cast<int>(app.candidates.size())) {
            auto &candidate = app.candidates[app.selected];
            auto key = reinterpret_cast<uintptr_t>(candidate.window);
            if (command == IdApply) {
                app.queue.Allow(key);
                app.status = app.session->Apply(candidate.window).message;
                Refresh();
            }
            if (command == IdRestore) {
                app.queue.Exclude(key, candidate.pid);
                app.status = app.session->Restore(candidate.window).message;
                Refresh();
            }
        }
        return 0;
    }
    case TrayMessage:
        if (LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT || LOWORD(lp) == WM_LBUTTONDBLCLK)
            ShowMain();
        else if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == WM_RBUTTONUP)
            TrayMenu();
        return 0;
    case WM_CLOSE:
        if (!app.exiting && app.tray) {
            HideMain();
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            app.exiting = true;
            DestroyWindow(window);
        }
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR *>(lp)->idFrom == IdList && reinterpret_cast<NMHDR *>(lp)->code == LVN_ITEMCHANGED)
            Selection();
        return 0;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && app.tray) {
            HideMain();
            return 0;
        }
        if (app.list)
            Layout();
        return 0;
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wp);
        auto rect = reinterpret_cast<RECT *>(lp);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Fonts();
        Layout();
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(lp)->ptMinTrackSize = {app.Scale(740), app.Scale(440)};
        return 0;
    case WM_TIMER:
        if (wp == 4)
            ManageWindows();
        return 0;
    case WM_DESTROY:
        KillTimer(window, 4);
        RemoveTray();
        RemovePropW(window, PrimaryProperty);
        RemovePropW(window, app.instanceProperty.c_str());
        app.session.reset();
        if (app.icon) {
            DestroyIcon(app.icon);
            app.icon = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return ERROR_INVALID_PARAMETER;
    bool valid = true;
    for (int i = 1; i < count; ++i) {
        if (!wcscmp(arguments[i], L"--background"))
            app.background = true;
        else if (!wcscmp(arguments[i], L"--paused"))
            app.paused = true;
        else if (!wcscmp(arguments[i], L"--test-mode") && i + 1 < count) {
            app.scope.testId = arguments[++i];
            valid = styler::settings::ValidTestId(app.scope.testId);
        } else if (!wcscmp(arguments[i], L"--scope-pid") && i + 1 < count) {
            wchar_t *end{};
            app.scopePid = wcstoul(arguments[++i], &end, 10);
            valid = app.scopePid && end && !*end;
        } else
            valid = false;
        if (!valid)
            break;
    }
    LocalFree(arguments);
    if (!valid)
        return ERROR_INVALID_PARAMETER;
    auto mutexName = L"Local\\" + app.scope.TaskName() + L"-Controller";
    app.instanceProperty = L"Macdows.WindowChanger.Instance." + app.scope.TaskName();
    auto mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex)
        return static_cast<int>(GetLastError());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        struct Search {
            HWND found{};
            const wchar_t *property{};
        } search{nullptr, app.instanceProperty.c_str()};
        EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                if (GetPropW(window, reinterpret_cast<Search *>(data)->property)) {
                    reinterpret_cast<Search *>(data)->found = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (search.found) {
            DWORD pid{};
            GetWindowThreadProcessId(search.found, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(search.found, ShowMessage(), 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    app.executable = path;
    app.manageAll = !app.paused && styler::settings::LoadAutoManage(app.scope);
    InitCommonControls();
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = Procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassExW(&cls);
    app.dpi = GetDpiForSystem();
    auto window = CreateWindowExW(0, cls.lpszClassName, L"macdowsOS WindowChanger",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, app.Scale(900),
                                  app.Scale(560), nullptr, nullptr, instance, nullptr);
    int result = 1;
    if (window) {
        if (!app.background || !app.tray)
            ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        result = static_cast<int>(message.wParam);
    }
    for (auto font : {app.body, app.heading, app.detailFont})
        if (font)
            DeleteObject(font);
    if (SUCCEEDED(com))
        CoUninitialize();
    CloseHandle(mutex);
    return result;
}
