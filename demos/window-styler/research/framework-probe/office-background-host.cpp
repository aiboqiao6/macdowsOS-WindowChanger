// Explicit HWND + PID only; never enumerates/injects into other applications.
#include "../../session.h"
#include <iostream>

namespace {
void Pump(DWORD milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    } while (GetTickCount64() < deadline);
}
bool OriginalButtonsDisabled(HWND target) {
    TITLEBARINFOEX info{};
    info.cbSize = sizeof(info);
    DWORD_PTR result{};
    if (!SendMessageTimeoutW(target, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result))
        return false;
    for (auto i : {2, 3, 5})
        if ((info.rgstate[i] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)) !=
                (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE) ||
            !IsRectEmpty(&info.rgrect[i]))
            return false;
    return true;
}
bool Healthy(HWND target, styler::Session &session, bool requireBackground = true) {
    const auto metrics = RegisterWindowMessageW(L"Macdows.OfficeLease.Metrics.v1");
    DWORD_PTR healthy{}, disabled{};
    return (!requireBackground || GetForegroundWindow() != target) && session.Owns(target) &&
           OriginalButtonsDisabled(target) &&
           SendMessageTimeoutW(target, metrics, 1, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &healthy) && healthy == 1 &&
           SendMessageTimeoutW(target, metrics, 7, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &disabled) && disabled == 1;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    std::cout << std::unitbuf;
    const bool waitForOwnedInput = argc == 4 && !wcscmp(argv[3], L"--wait-owned-input");
    const bool waitForOwnerLoss = waitForOwnedInput || (argc == 4 && !wcscmp(argv[3], L"--wait-owner-loss"));
    const bool resizeRoundtrip = argc == 4 && !wcscmp(argv[3], L"--resize-roundtrip");
    if (argc != 3 && !waitForOwnerLoss && !resizeRoundtrip)
        return ERROR_INVALID_PARAMETER;
    wchar_t *end{};
    const auto value = wcstoull(argv[1], &end, 0);
    if (!value || !end || *end)
        return ERROR_INVALID_PARAMETER;
    auto target = reinterpret_cast<HWND>(static_cast<UINT_PTR>(value));
    const auto expectedPid = wcstoul(argv[2], &end, 10);
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    if (!expectedPid || !end || *end || pid != expectedPid || !IsWindow(target) ||
        (!waitForOwnedInput && GetForegroundWindow() == target))
        return ERROR_INVALID_WINDOW_HANDLE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT outer{}, client{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    const auto style = GetWindowLongPtrW(target, GWL_STYLE);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Owned Office background research", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return GetLastError();
    int code{};
    {
        styler::Session session(owner);
        const auto result = session.Apply(target, static_cast<styler::Adapter>(272));
        std::cout << "attached=" << result.ok << '\n';
        if (!result.ok)
            code = ERROR_NOT_SUPPORTED;
        else {
            Pump(1000);
            const auto owned = session.Owns(target);
            std::cout << "query=" << owned << '\n';
            const auto hidden = OriginalButtonsDisabled(target);
            std::cout << "original-buttons-hidden-disabled=" << hidden << '\n';
            if (!owned || !hidden || !Healthy(target, session, !waitForOwnedInput))
                code = ERROR_INVALID_STATE;
            if (!code && resizeRoundtrip) {
                const auto width = outer.right - outer.left, height = outer.bottom - outer.top;
                const auto metrics = RegisterWindowMessageW(L"Macdows.OfficeLease.Metrics.v1");
                DWORD_PTR minimum{}, editor{};
                if (!SendMessageTimeoutW(target, metrics, 8, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &editor) ||
                    (editor &&
                     (!SendMessageTimeoutW(target, metrics, 9, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &minimum) ||
                      minimum < 200 || minimum > width)))
                    code = ERROR_INVALID_STATE;
                std::cout << "initial-width=" << width << "; minimum-editor-width=" << minimum << '\n';
                const auto narrowWidth = editor ? static_cast<int>(minimum) : width - 60;
                unsigned completed{};
                for (const auto requestedWidth : {narrowWidth, width + 120, width}) {
                    if (code)
                        break;
                    const auto requestedHeight = height + (requestedWidth - width) / 2;
                    const bool moved = SetWindowPos(target, nullptr, 0, 0, requestedWidth, requestedHeight,
                                                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
                    Pump(400);
                    RECT actual{};
                    GetWindowRect(target, &actual);
                    std::cout << "requested-width=" << requestedWidth << "; actual-width=" << actual.right - actual.left
                              << "; actual-height=" << actual.bottom - actual.top
                              << "; healthy=" << Healthy(target, session) << '\n';
                    if (!moved || !GetWindowRect(target, &actual) || actual.left != outer.left ||
                        actual.top != outer.top || actual.right - actual.left != requestedWidth ||
                        actual.bottom - actual.top != requestedHeight || !Healthy(target, session)) {
                        code = ERROR_INVALID_STATE;
                        break;
                    }
                    ++completed;
                }
                if (!code && editor) {
                    bool guarded = SetWindowPos(target, nullptr, 0, 0, static_cast<int>(minimum) - 24, height,
                                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
                    Pump(400);
                    RECT before{}, after{};
                    guarded = guarded && GetForegroundWindow() != target && !session.Owns(target) &&
                              !GetPropW(target, styler::OwnerProperty) && !GetPropW(target, styler::InlineProperty) &&
                              GetWindowLongPtrW(target, GWL_STYLE) == style && GetWindowRect(target, &before);
                    const auto rejected = session.Apply(target, static_cast<styler::Adapter>(272));
                    guarded = guarded && !rejected.ok && GetWindowRect(target, &after) && EqualRect(&before, &after) &&
                              !GetPropW(target, styler::OwnerProperty) && !GetPropW(target, styler::InlineProperty) &&
                              GetWindowLongPtrW(target, GWL_STYLE) == style && GetForegroundWindow() != target;
                    std::cout << "narrow-layout-detached-and-reattach-rejected=" << guarded << '\n';
                    if (!guarded)
                        code = ERROR_INVALID_STATE;
                }
                // Restore the owned fixture's original size even if a check fails.
                if (!SetWindowPos(target, nullptr, outer.left, outer.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
                    code = ERROR_INVALID_STATE;
                Pump(400);
                std::cout << "background-resize-stages=" << completed << '\n';
                if (completed != 3 || (!editor && !Healthy(target, session)))
                    code = ERROR_INVALID_STATE;
            }
            if (!code && waitForOwnerLoss) {
                std::cout << "ready-for-owner-loss=1\n";
                // The parent kills only this owned verifier. A bounded normal
                // cleanup remains if the parent disappears before doing so.
                const auto deadline = GetTickCount64() + 15000;
                while (GetTickCount64() < deadline && IsWindow(target) && session.Owns(target)) {
                    Pump(50);
                }
            }
            const auto restored = session.RestoreAll();
            std::cout << "restored=" << restored.ok << '\n';
            if (!restored.ok)
                code = ERROR_INVALID_STATE;
        }
        RECT nowOuter{}, nowClient{};
        GetWindowRect(target, &nowOuter);
        GetClientRect(target, &nowClient);
        const auto exact = EqualRect(&outer, &nowOuter) && EqualRect(&client, &nowClient) &&
                           style == GetWindowLongPtrW(target, GWL_STYLE);
        std::cout << "original-geometry-and-style-restored=" << exact << '\n';
        if (!exact || (!waitForOwnedInput && GetForegroundWindow() == target))
            code = ERROR_INVALID_STATE;
    }
    DestroyWindow(owner);
    return code;
}
