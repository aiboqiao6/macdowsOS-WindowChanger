#include "inline.h"
#include "host-client.h"
#include "adapter-registry.h"
#include "research/framework-probe/research-placement.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void Pump(DWORD milliseconds) {
    auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < until);
}
LRESULT Send(HWND window, UINT message, WPARAM wp = 0, LPARAM lp = 0) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(window, message, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result)
               ? static_cast<LRESULT>(result)
               : -1;
}
HWND Create(const wchar_t *name, HWND parent = nullptr) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = name;
    RegisterClassW(&cls);
    return CreateWindowW(name, L"Owned original-framework registry contract", parent ? WS_CHILD : WS_OVERLAPPEDWINDOW,
                         30, 30, 800, 600, parent, nullptr, cls.hInstance, nullptr);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc != 2)
        return ERROR_INVALID_PARAMETER;
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok)
            ++failures;
        std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
        return ok;
    };
    auto owner = Create(styler::ControllerClass);
    if (!owner)
        return GetLastError();
    if (!wcscmp(argv[1], L"--reject-fixtures")) {
        auto registry = styler::ReadAdapterRegistry(styler::ExecutableDirectory());
        for (const auto &entry : registry)
            if (entry.id == 268 || entry.id == 272 || entry.id == 279)
                check(
                    std::filesystem::is_regular_file(std::filesystem::path(styler::ExecutableDirectory()) / entry.file),
                    "registered original-framework module exists in the shipped runtime");
        struct Fixture {
            const wchar_t *name, *child;
        } fixtures[]{{L"TXGuiFoundation", nullptr},
                     {L"Afx:OwnedUnknownAbi", L"BCGPRibbonBar:OwnedUnknownAbi"},
                     {L"OwnedUnknownOfficeFrame", L"NetUIHWND"}};
        for (auto fixture : fixtures) {
            auto window = Create(fixture.name);
            auto child = fixture.child ? Create(fixture.child, window) : nullptr;
            RECT before{}, after{};
            GetWindowRect(window, &before);
            auto style = GetWindowLongPtrW(window, GWL_STYLE);
            styler::GuardedSession session(owner);
            auto applied = session.Apply(window);
            GetWindowRect(window, &after);
            check(!applied.ok && !session.Owns(window) && !GetPropW(window, styler::OwnerProperty) &&
                      !GetPropW(window, styler::InlineProperty) && EqualRect(&before, &after) &&
                      GetWindowLongPtrW(window, GWL_STYLE) == style,
                  "unknown framework ABI refuses automatic attach without a native fallback or state change");
            if (child)
                DestroyWindow(child);
            DestroyWindow(window);
        }
    } else {
        wchar_t *end{};
        auto window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(wcstoull(argv[1], &end, 0)));
        if (!end || *end || !IsWindow(window))
            return ERROR_INVALID_WINDOW_HANDLE;
        ResearchPlacement placement;
        if (!placement.Capture(window))
            return GetLastError();
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        Pump(350);
        RECT before{}, clientBefore{};
        GetWindowRect(window, &before);
        GetClientRect(window, &clientBefore);
        auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        auto candidate = styler::InspectInline(window);
        check(candidate.supported && candidate.adapterMode == styler::Adapter::Automatic,
              "main controller discovers a registered original-framework candidate");
        styler::GuardedSession session(owner);
        auto applied = session.Apply(window);
        if (!applied.ok)
            std::wcerr << applied.message << std::endl;
        check(applied.ok && session.Owns(window),
              "main automatic controller path attaches the actual original-tree backend");
        if (applied.ok) {
            Pump(1000);
            check(Send(window, styler::QueryMessage()) == styler::Acknowledged,
                  "main runtime backend remains attached after Apply returns");
            wchar_t name[128]{};
            GetClassNameW(window, name, 128);
            if (!wcscmp(name, L"TXGuiFoundation")) {
                auto metrics = RegisterWindowMessageW(L"Macdows.TXGuiLeftLease.Metrics.v1");
                check(Send(window, metrics, 1) == 1 && Send(window, metrics, 3, 9) == 7,
                      "main TXGui runtime paints all three actual native left-light colors");
            } else if (!wcsncmp(name, L"Afx", 3)) {
                auto metrics = RegisterWindowMessageW(L"Macdows.MfcPaintProbe.Metrics.v1");
                auto ribbon = reinterpret_cast<HWND>(Send(window, metrics, 5));
                POINT origin{};
                bool native = IsWindow(ribbon) && ClientToScreen(ribbon, &origin);
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                Pump(150);
                auto screen = GetDC(nullptr);
                constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
                for (unsigned i = 0; i < 3; ++i) {
                    POINT point{static_cast<LONG>(Send(window, metrics, 10 + i * 2)),
                                static_cast<LONG>(Send(window, metrics, 11 + i * 2))};
                    auto expectedX = origin.x + MulDiv(18 + static_cast<int>(i) * 20, GetDpiForWindow(window), 96);
                    check(native && screen && Send(window, metrics, i) > 0 && point.x == expectedX &&
                              GetAncestor(WindowFromPoint(point), GA_ROOT) == window &&
                              GetPixel(screen, point.x, point.y) == colors[i],
                          "main MFC original renderer paints the actual expected left-light center color");
                }
                if (screen)
                    ReleaseDC(nullptr, screen);
            } else if (wcsncmp(name, L"Afx", 3)) {
                auto metrics = RegisterWindowMessageW(L"Macdows.OfficeLease.Metrics.v1");
                check(Send(window, metrics, 1) == 1 && Send(window, metrics, 3, 3) == 7,
                      "main Office runtime paints all three actual original-tree light colors");
            }
            check(session.Apply(window).ok, "main automatic repeated apply remains idempotent");
            check(session.Restore(window).ok, "main automatic explicit undo succeeds");
        }
        Pump(200);
        RECT after{}, clientAfter{};
        GetWindowRect(window, &after);
        GetClientRect(window, &clientAfter);
        check(EqualRect(&before, &after) && EqualRect(&clientBefore, &clientAfter) &&
                  GetWindowLongPtrW(window, GWL_STYLE) == style && GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle &&
                  !GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::InlineProperty),
              "main runtime undo preserves original geometry styles and removes all session properties");
        check(placement.Finish(), "main runtime verifier restores current user placement and foreground");
    }
    DestroyWindow(owner);
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    return failures ? ERROR_INVALID_DATA : ERROR_SUCCESS;
}
