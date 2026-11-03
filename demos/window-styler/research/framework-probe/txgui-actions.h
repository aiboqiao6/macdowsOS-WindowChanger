#pragma once
// Explicit, bounded original-input contract. No layout properties or event
// subscriptions are changed, no owner is published, and Close is never clicked.
namespace {
struct TxActionScope {
    HWND window{};
    WINDOWPLACEMENT placement{sizeof(placement)};
    bool held{}, saved{}, touched{};
    void Cancel() {
        if (held && IsWindow(window)) {
            SendMessageW(window, WM_CANCELMODE, 0, 0);
            auto capture = GetCapture();
            if (capture == window || (capture && IsChild(window, capture)))
                ReleaseCapture();
        }
        held = false;
    }
    void Restore() {
        Cancel();
        if (saved && touched && IsWindow(window)) {
            SetWindowPlacement(window, &placement);
        }
        saved = false;
    }
    ~TxActionScope() {
        Restore();
    }
};
bool TxRect(IUnknown *frame, RECT &rect) {
    using Getter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
    auto get = Slot<Getter>(frame, 0x254 / 4);
    return get && SUCCEEDED(get(frame, &rect)) && rect.right > rect.left && rect.bottom > rect.top;
}
bool TxEnabled(IUnknown *frame) {
    using Getter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BOOL *);
    auto get = Slot<Getter>(frame, 0x168 / 4);
    BOOL value{};
    return get && SUCCEEDED(get(frame, &value)) && value;
}
bool TxHint(IUnknown *frame, txcaption::Intent expected) {
    using Getter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR *);
    auto get = Slot<Getter>(frame, 0x1bc / 4);
    if (!get || get != reinterpret_cast<Getter>(gfImage + 0x3690) ||
        *reinterpret_cast<BYTE ***>(frame) != reinterpret_cast<BYTE **>(gfImage + 0x1f4c00))
        return false;
    BSTR tip{};
    auto hr = get(frame, &tip);
    bool matches = hr == S_OK && txcaption::Hint(tip, SysStringLen(tip)) == expected;
    SysFreeString(tip);
    return matches;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || context->adapterId != 273 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId()) {
        captionGroup = {};
        return ERROR_NOT_SUPPORTED;
    }
    auto selected = std::move(captionGroup);
    captionGroup = {};
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
    wchar_t file[32768]{};
    GetModuleFileNameW(self, file, 32768);
    auto report = std::filesystem::path(file).parent_path() /
                  (L"txgui-actions-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    unsigned checks{}, failures{};
    std::vector<std::string> failed;
    auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            ++failures;
            failed.emplace_back(label);
        }
        return ok;
    };
    auto window = context->window;
    TxActionScope scope;
    scope.window = window;
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool ready = check(!IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) &&
                           !GetPropW(window, styler::OwnerProperty) && !GetCapture() &&
                           (style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ==
                               (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
                       "enabled-normal-window-with-original-capabilities");
    scope.saved = GetWindowPlacement(window, &scope.placement) != FALSE;
    ready &= check(scope.saved, "save-original-placement");
    std::array<RECT, 3> originalRects{};
    std::array<txevents::Result, 3> originalEvents;
    uintptr_t notifier{};
    for (unsigned i = 0; i < 3; ++i) {
        auto button = selected.buttons[i].Get();
        ready &= check(TxEnabled(button) && TxHint(button, static_cast<txcaption::Intent>(i)) &&
                           TxRect(button, originalRects[i]),
                       "original-enabled-button-intent-and-rectangle");
        originalEvents[i] = txevents::Inspect(button, gfImage);
        auto &events = originalEvents[i];
        bool oneClick = events.profile && events.complete && events.numeric.size() == 1 &&
                        events.numeric[0].id == 0x1003 && events.named.empty() && events.listeners.empty();
        ready &= check(oneClick, "one-original-click-subscription-without-extra-listeners");
        if (oneClick) {
            if (!i)
                notifier = events.numeric[0].sink;
            ready &= check(events.numeric[0].sink == notifier, "common-original-caption-notifier");
        }
    }
    POINT origin{};
    ClientToScreen(window, &origin);
    bool row = true;
    for (auto rect : originalRects)
        row &= rect.top == originalRects[0].top && rect.bottom == originalRects[0].bottom && rect.left >= origin.x &&
               rect.right <= origin.x + client.right && rect.top >= origin.y &&
               rect.bottom <= origin.y + MulDiv(64, GetDpiForWindow(window), 96);
    ready &= check(row && selected.parent, "one-original-parent-and-valid-top-row");
    bool maximize{}, restore{}, minimize{};
    std::vector<MSG> deferred;
    auto click = [&](unsigned index) {
        auto button = selected.buttons[index].Get();
        RECT rect{};
        auto events = txevents::Inspect(button, gfImage);
        if (!IsWindow(window) || !TxRect(button, rect) || !TxEnabled(button) ||
            !TxHint(button, static_cast<txcaption::Intent>(index)) || !events.complete ||
            events.numeric != originalEvents[index].numeric || !events.named.empty() || !events.listeners.empty())
            return check(false, "input-preflight-still-matches-original-source");
        POINT point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
        using Hit = HRESULT(__cdecl *)(IUnknown *, const POINT *, IUnknown **);
        ComPtr<IUnknown> found, identity;
        auto hr = reinterpret_cast<Hit>(gfImage + 0x66e20)(selected.root.Get(), &point, &found);
        if (found)
            found.As(&identity);
        if (!check(SUCCEEDED(hr) && identity.Get() == button, "original-hit-query-matches-intended-source"))
            return false;
        ScreenToClient(window, &point);
        auto coordinates = MAKELPARAM(point.x, point.y);
        scope.touched = true;
        SendMessageW(window, WM_MOUSEMOVE, 0, coordinates);
        scope.held = true;
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, coordinates);
        SendMessageW(window, WM_LBUTTONUP, 0, coordinates);
        bool released = !GetCapture();
        if (released)
            scope.held = false;
        else
            scope.Cancel();
        auto end = GetTickCount64() + 60;
        do {
            MSG message{};
            while (PeekMessageW(&message, window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE)) {
                auto command = message.wParam & 0xfff0;
                if ((index == 1 && command == SC_MINIMIZE) ||
                    (index == 2 && (command == SC_MAXIMIZE || command == SC_RESTORE)))
                    DispatchMessageW(&message);
                else
                    deferred.push_back(message);
            }
            Sleep(5);
        } while (GetTickCount64() < end);
        return check(released, "original-input-releases-capture");
    };
    if (ready) {
        maximize = click(2) && IsZoomed(window);
        check(maximize, "original-maximize-input");
        if (maximize) {
            restore = click(2) && !IsZoomed(window) && !IsIconic(window);
            check(restore, "original-restore-input");
        }
        if (restore) {
            minimize = click(1) && IsIconic(window);
            check(minimize, "original-minimize-input");
        }
    }
    scope.Restore();
    RECT afterOuter{}, afterClient{};
    GetWindowRect(window, &afterOuter);
    GetClientRect(window, &afterClient);
    bool restored =
        check(IsWindow(window) && !IsIconic(window) && !IsZoomed(window) && EqualRect(&outer, &afterOuter) &&
                  EqualRect(&client, &afterClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
                  GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle,
              "original-window-state-restored");
    for (unsigned i = 0; i < 3; ++i) {
        auto events = txevents::Inspect(selected.buttons[i].Get(), gfImage);
        RECT rect{};
        restored &=
            check(events.complete && events.numeric == originalEvents[i].numeric &&
                      events.named == originalEvents[i].named && events.listeners == originalEvents[i].listeners &&
                      TxRect(selected.buttons[i].Get(), rect) && EqualRect(&rect, &originalRects[i]) &&
                      TxHint(selected.buttons[i].Get(), static_cast<txcaption::Intent>(i)),
                  "original-source-binding-tooltip-and-geometry-restored");
    }
    std::ofstream output(report);
    output << "{\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"maximizeInput\":" << (maximize ? "true" : "false")
           << ",\"restoreInput\":" << (restore ? "true" : "false")
           << ",\"minimizeInput\":" << (minimize ? "true" : "false") << ",\"closeInput\":false,\"layoutModified\":false"
           << ",\"restored\":" << (restored ? "true" : "false")
           << ",\"persistentSession\":false,\"intentSource\":\"original-toolTip\",\"failed\":[";
    for (size_t i = 0; i < failed.size(); ++i) {
        if (i)
            output << ',';
        output << '"' << failed[i] << '"';
    }
    output << "]}\n";
    for (auto message : deferred)
        PostMessageW(message.hwnd, message.message, message.wParam, message.lParam);
    return ERROR_NOT_SUPPORTED;
}
