#pragma once
#include <windows.h>

// Test-harness state only. Restoring an iconic window must not activate it;
// some hosts intentionally show themselves again on WM_ACTIVATE.
struct ResearchPlacement {
    HWND target{}, foreground{};
    WINDOWPLACEMENT saved{sizeof(saved)};
    RECT outer{}, client{};
    bool restore{}, iconic{}, zoomed{};
    bool Capture(HWND window) {
        target = window;
        foreground = GetForegroundWindow();
        iconic = IsIconic(target) != FALSE;
        zoomed = IsZoomed(target) != FALSE;
        restore = GetWindowPlacement(target, &saved) && GetWindowRect(target, &outer) && GetClientRect(target, &client);
        return restore;
    }
    bool Finish() {
        if (!restore)
            return true;
        if (!IsWindow(target))
            return false;
        // Do not activate a different window if it is now minimized either.
        if (IsWindow(foreground) && foreground != target && !IsIconic(foreground))
            SetForegroundWindow(foreground);
        auto placement = saved;
        if (iconic)
            placement.showCmd = SW_SHOWMINNOACTIVE;
        bool ok = SetWindowPlacement(target, &placement) != FALSE;
        if (foreground == target && !iconic && !IsIconic(target))
            SetForegroundWindow(target);
        // IsIconic changes before the animated window/client bounds settle.
        // Verify actual geometry as well, so the next fixture never captures
        // the intermediate normal-size-but-iconic state as its baseline.
        unsigned stable{};
        for (unsigned attempt = 0; ok && attempt < 40 && stable < 3; ++attempt) {
            Sleep(50);
            WINDOWPLACEMENT current{sizeof(current)};
            RECT currentOuter{}, currentClient{};
            bool same = IsWindow(target) && (IsIconic(target) != FALSE) == iconic &&
                        (IsZoomed(target) != FALSE) == zoomed && GetWindowPlacement(target, &current) &&
                        EqualRect(&saved.rcNormalPosition, &current.rcNormalPosition) &&
                        GetWindowRect(target, &currentOuter) && GetClientRect(target, &currentClient) &&
                        EqualRect(&outer, &currentOuter) && EqualRect(&client, &currentClient);
            stable = same ? stable + 1 : 0;
        }
        ok = ok && stable == 3;
        if (ok)
            restore = false;
        return ok;
    }
    ~ResearchPlacement() {
        Finish();
    }
};
