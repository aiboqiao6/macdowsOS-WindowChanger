#pragma once
#include "txgui-d2d-filter.h"
#include "txgui-caption-pixels.h"
#include <dwmapi.h>

namespace {
struct DrawFrameState {
    RECT rect{}, screen{}, padding{}, margin{};
    BOOL hidden{}, visible{}, enabled{};
    bool Read(IUnknown *frame) {
        using Rect = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
        using Bool = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BOOL *);
        auto r = [&](size_t offset, RECT &value) {
            auto get = Slot<Rect>(frame, offset / 4);
            return get && SUCCEEDED(get(frame, &value));
        };
        auto b = [&](size_t offset, BOOL &value) {
            auto get = Slot<Bool>(frame, offset / 4);
            return get && SUCCEEDED(get(frame, &value));
        };
        return r(0x1e0, rect) && r(0x254, screen) && r(0x210, padding) && r(0x208, margin) && b(0x158, hidden) &&
               b(0x160, visible) && b(0x168, enabled);
    }
    bool Same(const DrawFrameState &other) const {
        return EqualRect(&rect, &other.rect) && EqualRect(&screen, &other.screen) &&
               EqualRect(&padding, &other.padding) && EqualRect(&margin, &other.margin) && hidden == other.hidden &&
               visible == other.visible && enabled == other.enabled;
    }
};
struct DrawLease {
    ComPtr<IUnknown> list;
    ComPtr<txd2d::Filter> filter;
    txobserve::FilterChange remove{};
    bool attached{};
    bool Detach() {
        if (!attached)
            return true;
        if (!remove || FAILED(remove(list.Get(), filter.Get())))
            return false;
        attached = false;
        return true;
    }
    ~DrawLease() {
        Detach();
    }
};
void DrawRect(std::ostream &stream, RECT rect) {
    stream << '[' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << ']';
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    if (!styler::ValidContext(context) || context->adapterId != 275 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId()) {
        captionGroup = {};
        return ERROR_NOT_SUPPORTED;
    }
    auto selected = std::move(captionGroup);
    captionGroup = {};
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self))
        return ERROR_INVALID_HANDLE;
    wchar_t module[32768]{};
    GetModuleFileNameW(self, module, 32768);
    auto stem =
        std::filesystem::path(module).parent_path() / (L"txgui-d2d-drawing-" + std::to_wstring(GetCurrentProcessId()));
    std::ofstream report(stem.wstring() + L".json", std::ios::trunc);
    if (!report)
        return ERROR_WRITE_FAULT;
    unsigned checks{}, failures{};
    std::vector<std::string> failed;
    auto check = [&](bool value, const char *name) {
        ++checks;
        if (!value) {
            ++failures;
            failed.emplace_back(name);
        }
        return value;
    };
    auto window = context->window;
    RECT outer{}, client{};
    POINT clientOrigin{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    ClientToScreen(window, &clientOrigin);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool ready = check(!IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) && !GetCapture() &&
                           !GetPropW(window, styler::OwnerProperty),
                       "normal-enabled-original-window-with-no-capture-or-owner");
    // Construct before leases: exception unwinding removes all registrations
    // before requesting the final original repaint.
    struct RepaintCleanup {
        HWND window;
        ~RepaintCleanup() {
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    } repaintCleanup{window};
    std::array<DrawLease, 3> leases;
    std::array<DrawFrameState, 3> before;
    std::array<txevents::Result, 3> events;
    std::array<std::vector<IUnknown *>, 3> original;
    std::array<RECT, 3> circles;
    constexpr std::array<DWORD, 3> colors{0xffff5f57, 0xffffbd2e, 0xff28c840};
    constexpr GUID filtersId{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
    RECT capture{};
    uintptr_t notifier{};
    for (unsigned i = 0; i < 3; ++i) {
        auto button = selected.buttons[i].Get();
        auto &lease = leases[i];
        ready &= check(before[i].Read(button) && before[i].visible && !before[i].hidden && before[i].enabled,
                       "original-caption-visible-enabled-state");
        auto screen = before[i].screen;
        int width = screen.right - screen.left, height = screen.bottom - screen.top;
        int diameter = MulDiv(12, GetDpiForWindow(window), 96);
        ready &= check(width >= diameter && width < 256 && height >= diameter && height < 256,
                       "original-caption-physical-dimensions-bounded");
        circles[i] = {(width - diameter) / 2, (height - diameter) / 2, (width - diameter) / 2 + diameter,
                      (height - diameter) / 2 + diameter};
        if (!i)
            capture = screen;
        else
            UnionRect(&capture, &capture, &screen);
        events[i] = txevents::Inspect(button, gfImage);
        auto &bindings = events[i];
        bool click = bindings.profile && bindings.complete && bindings.numeric.size() == 1 &&
                     bindings.numeric[0].id == 0x1003 && bindings.named.empty() && bindings.listeners.empty();
        if (click && !i)
            notifier = bindings.numeric[0].sink;
        ready &= check(click && bindings.numeric[0].sink == notifier, "original-common-caption-click-notifier");
        ready &= check(txobserve::List(button, gfImage, original[i]) &&
                           SUCCEEDED(button->QueryInterface(filtersId, &lease.list)) && lease.list,
                       "snapshot-original-filter-identities-and-order");
        auto table = lease.list ? *reinterpret_cast<void ***>(lease.list.Get()) : nullptr;
        bool abi = table == reinterpret_cast<void **>(gfImage + 0x1f50fc) && table[7] == gfImage + 0x937d0 &&
                   table[8] == gfImage + 0x937f0;
        ready &= check(abi, "original-filter-collection-abi");
        if (abi) {
            lease.remove = reinterpret_cast<txobserve::FilterChange>(table[8]);
            lease.filter.Attach(new txd2d::Filter(button, gfImage, circles[i], colors[i]));
            lease.filter->RequireOrigin({screen.left - clientOrigin.x, screen.top - clientOrigin.y});
            ready &= check(lease.filter->Ready(), "shared-original-D2D-fill-ABI");
        }
    }
    CaptionPixels baseline, painted, restored;
    bool pixelsWritten{};
    auto repaint = [&] {
        return RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE;
    };
    if (ready) {
        CaptionPixels warmed;
        ready &= check(repaint() && baseline.Read(window, capture) && repaint() && warmed.Read(window, capture) &&
                           baseline.pixels == warmed.pixels && baseline.Save(stem.wstring() + L"-before.bmp"),
                       "unoccluded-original-caption-pixels-stable-before-registration");
    }
    if (ready) {
        for (unsigned i = 0; i < 3; ++i) {
            auto &lease = leases[i];
            auto table = *reinterpret_cast<void ***>(lease.list.Get());
            lease.attached =
                SUCCEEDED(reinterpret_cast<txobserve::FilterChange>(table[7])(lease.list.Get(), lease.filter.Get()));
            auto expected = original[i];
            expected.push_back(lease.filter.Get());
            std::vector<IUnknown *> during;
            ready &= check(lease.attached && lease.filter->References() == 2 &&
                               txobserve::List(selected.buttons[i].Get(), gfImage, during) && during == expected,
                           "append-only-owned-D2D-callback");
            lease.filter->Enable(true);
        }
        if (ready) {
            check(repaint(), "normal-original-renderer-repaint-with-no-synthetic-notification");
            check(painted.Read(window, capture) && painted.Save(stem.wstring() + L"-painted.bmp"),
                  "read-actual-unoccluded-caption-pixels-after-original-paint");
        }
    }
    std::array<txd2d::Drawing, 3> drawing;
    std::array<unsigned, 3> originalGlyph{}, remainingGlyph{}, circlePixels{}, antialias{};
    for (unsigned i = 0; i < 3; ++i) {
        auto &lease = leases[i];
        if (lease.filter)
            drawing[i] = lease.filter->ReadDrawing();
        pixelsWritten |= drawing[i].paints != 0;
        check(drawing[i].paints > 0 && drawing[i].rejected == 0 && drawing[i].stateChanges == 0,
              "real-D2D-callback-draws-and-preserves-original-graphics-state");
        auto screen = before[i].screen;
        int x = screen.left - capture.left + (circles[i].left + circles[i].right) / 2;
        int y = screen.top - capture.top + (circles[i].top + circles[i].bottom) / 2;
        auto index = static_cast<size_t>(y) * painted.width + x;
        check(x >= 0 && y >= 0 && index < painted.pixels.size() && painted.pixels[index] == (colors[i] & 0xffffff),
              "actual-original-caption-light-center-has-requested-D2D-color");
        bool backgroundProfile = !baseline.pixels.empty() && baseline.pixels.size() == painted.pixels.size();
        int width = screen.right - screen.left, height = screen.bottom - screen.top;
        // Check the central content interior, not the window border or the
        // Close control's original asymmetric right-side padding.
        for (int row = height / 4; row < height - 8 && backgroundProfile; ++row) {
            auto offset =
                static_cast<size_t>(screen.top - capture.top + row) * baseline.width + screen.left - capture.left;
            // The Close glyph is asymmetrically aligned and can intersect a
            // quarter-width sample. Use the row's left background strip,
            // outside every original caption glyph, and independently verify
            // a second background sample inside this control.
            auto background = baseline.pixels[(offset / baseline.width) * baseline.width + 4];
            backgroundProfile &= background == baseline.pixels[offset + width * 3 / 4];
            for (int column = 8; column < width * 3 / 4; ++column) {
                bool inside = column >= circles[i].left && column < circles[i].right && row >= circles[i].top &&
                              row < circles[i].bottom;
                auto old = baseline.pixels[offset + column], current = painted.pixels[offset + column];
                if (!inside) {
                    originalGlyph[i] += old != background;
                    remainingGlyph[i] += current != background;
                } else {
                    circlePixels[i] += current != background;
                    antialias[i] += current != background && current != (colors[i] & 0xffffff);
                }
            }
        }
        check(backgroundProfile && originalGlyph[i] > 0 && remainingGlyph[i] == 0,
              "actual-original-content-glyph-is-present-before-and-absent-during-custom-paint");
        auto area = (circles[i].right - circles[i].left) * (circles[i].bottom - circles[i].top);
        check(backgroundProfile && circlePixels[i] >= static_cast<unsigned>(area / 2) &&
                  circlePixels[i] < static_cast<unsigned>(area) && antialias[i] > 0,
              "actual-D2D-light-is-filled-rounded-and-antialiased-not-just-one-center-pixel");
        check(lease.Detach() && lease.filter && lease.filter->References() == 1, "remove-only-owned-drawing-callback");
        std::vector<IUnknown *> filters;
        auto bindings = txevents::Inspect(selected.buttons[i].Get(), gfImage);
        DrawFrameState after;
        check(txobserve::List(selected.buttons[i].Get(), gfImage, filters) && filters == original[i] &&
                  after.Read(selected.buttons[i].Get()) && before[i].Same(after) && bindings.complete &&
                  bindings.numeric == events[i].numeric && bindings.named == events[i].named &&
                  bindings.listeners == events[i].listeners,
              "restore-original-filter-order-with-geometry-and-click-bindings-unchanged");
    }
    check(repaint() && restored.Read(window, capture) && !baseline.pixels.empty() &&
              restored.pixels == baseline.pixels && restored.Save(stem.wstring() + L"-restored.bmp"),
          "native-repaint-after-detach-exactly-restores-all-caption-pixels");
    for (unsigned i = 0; i < 3; ++i)
        check(leases[i].filter && leases[i].filter->ReadDrawing().paints == drawing[i].paints,
              "detached-D2D-callback-receives-no-further-paint");
    RECT finalOuter{}, finalClient{};
    check(GetWindowRect(window, &finalOuter) && GetClientRect(window, &finalClient) && EqualRect(&outer, &finalOuter) &&
              EqualRect(&client, &finalClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
              GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle && !GetCapture() &&
              !GetPropW(window, styler::OwnerProperty),
          "original-window-state-styles-capture-and-owner-preserved");
    report
        << "{\"pid\":" << GetCurrentProcessId()
        << ",\"layoutModified\":false,\"inputSent\":false,\"syntheticNotifications\":false,\"persistentSession\":false,"
           "\"pixelsWritten\":"
        << (pixelsWritten ? "true" : "false") << ",\"captureRect\":";
    DrawRect(report, capture);
    report << ",\"lights\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            report << ',';
        auto &d = drawing[i];
        report << "{\"intent\":\"" << txcaption::Name(static_cast<txcaption::Intent>(i)) << "\",\"paints\":" << d.paints
               << ",\"rejected\":" << d.rejected << ",\"stateChanges\":" << d.stateChanges << ",\"scaled\":" << d.scaled
               << ",\"originalGlyphPixels\":" << originalGlyph[i] << ",\"remainingGlyphPixels\":" << remainingGlyph[i]
               << ",\"circlePixels\":" << circlePixels[i] << ",\"antialiasPixels\":" << antialias[i]
               << ",\"viewport\":[" << d.before.viewport.x << ',' << d.before.viewport.y << "],\"windowOrigin\":["
               << d.before.window.x << ',' << d.before.window.y << "],\"clip\":";
        DrawRect(report, d.before.clip);
        report << ",\"circle\":";
        DrawRect(report, circles[i]);
        report << '}';
    }
    report << "],\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (failures == 0 ? "true" : "false") << ",\"failedChecks\":[";
    for (unsigned i = 0; i < failed.size(); ++i) {
        if (i)
            report << ',';
        report << '"' << failed[i] << '"';
    }
    report << "]}\n";
    return ERROR_NOT_SUPPORTED;
} catch (...) {
    captionGroup = {};
    return ERROR_UNHANDLED_EXCEPTION;
}
