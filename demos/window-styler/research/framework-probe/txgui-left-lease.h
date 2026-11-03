#pragma once
#include <commctrl.h>
#include <memory>

namespace {
constexpr UINT_PTR LeftLeaseSubclass = 0x54584c53;
UINT LeftLeaseMetrics() {
    return RegisterWindowMessageW(L"Macdows.TXGuiLeftLease.Metrics.v1");
}
struct LeftLease {
    HWND window{}, owner{};
    DWORD ownerPid{};
    HANDLE process{};
    CaptionGroup selected;
    txleft::Objects objects;
    std::unique_ptr<txleft::Scope> layout;
    std::array<LeftDrawLease, 3> drawing;
    std::array<txevents::Result, 3> events;
    std::array<std::vector<IUnknown *>, 3> filters;
    std::filesystem::path report;
    LONG_PTR capabilities{};
    unsigned calls{}, working{}, cleanupFailures{}, canceled{}, inputMessages{};
    int pressed{-1};
    bool active{}, stopping{}, finished{}, pending{}, timer{}, subclass{}, published{}, destroyed{};
    const char *reason{"attach-failed"};
    ~LeftLease() {
        if (!finished && subclass) {
            working = 0;
            Stop("exception-unwind");
        }
        if (process)
            CloseHandle(process);
    }
    bool OwnerAlive() const {
        DWORD pid{};
        return IsWindow(owner) && GetWindowThreadProcessId(owner, &pid) && pid == ownerPid && process &&
               WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
    bool ButtonRect(size_t i, RECT &rect) const {
        using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
        auto object = objects.buttons[i].Get();
        auto table = *reinterpret_cast<void ***>(object);
        return table == reinterpret_cast<void **>(gfImage + 0x1f4c00) && table[0x254 / 4] == gfImage + 0x3b50 &&
               SUCCEEDED(reinterpret_cast<Get>(table[0x254 / 4])(object, &rect)) && rect.right > rect.left &&
               rect.bottom > rect.top;
    }
    bool Health() const {
        constexpr LONG_PTR mask = WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        if (!active || !layout || destroyed || !IsWindow(window) || GetDpiForWindow(window) != 144 ||
            (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
            (GetWindowLongPtrW(window, GWL_STYLE) & mask) != capabilities || !layout->Healthy())
            return false;
        for (size_t i = 0; i < 3; ++i) {
            auto after = txevents::Inspect(objects.buttons[i].Get(), gfImage);
            auto expectedFilters = filters[i];
            expectedFilters.push_back(drawing[i].filter.Get());
            std::vector<IUnknown *> currentFilters;
            if (!after.complete || after.numeric != events[i].numeric || after.named != events[i].named ||
                after.listeners != events[i].listeners || !drawing[i].attached ||
                !txobserve::List(objects.buttons[i].Get(), gfImage, currentFilters) ||
                currentFilters != expectedFilters)
                return false;
        }
        return true;
    }
    int Hit(POINT point) const {
        for (size_t i = 0; i < 3; ++i) {
            RECT rect{};
            if (ButtonRect(i, rect) && PtInRect(&rect, point) &&
                LeftHit(selected.root.Get(), point, objects.buttons[i].Get()))
                return static_cast<int>(i);
        }
        return -1;
    }
    void Cancel() {
        if (pressed < 0)
            return;
        pressed = -1;
        ++canceled;
        if (!destroyed && IsWindow(window)) {
            SendMessageW(window, WM_CANCELMODE, 0, 0);
            auto capture = GetCapture();
            if (capture == window || (capture && IsChild(window, capture)))
                ReleaseCapture();
        }
    }
    void WriteReport() const {
        std::ofstream out(report);
        out << "{\"active\":" << (active ? "true" : "false") << ",\"reason\":\"" << reason
            << "\",\"cleanupFailures\":" << cleanupFailures << ",\"canceledInput\":" << canceled
            << ",\"originalInputMessages\":" << inputMessages
            << ",\"closeInputGenerated\":false,\"layoutFollowTimer\":false}\n";
    }
    void Stop(const char *why) {
        if (finished || stopping)
            return;
        reason = why;
        if (working || calls > 1) {
            pending = true;
            return;
        }
        stopping = true;
        active = false;
        pending = false;
        ++working;
        if (timer && !destroyed)
            KillTimer(window, LeftLeaseSubclass);
        timer = false;
        Cancel();
        for (size_t i = 0; i < 3; ++i) {
            cleanupFailures += !drawing[i].Detach();
            std::vector<IUnknown *> after;
            cleanupFailures += !txobserve::List(objects.buttons[i].Get(), gfImage, after) || after != filters[i];
        }
        if (layout) {
            cleanupFailures += !layout->Finish();
            layout.reset();
        }
        if (subclass && !destroyed)
            RemoveWindowSubclass(window, Dispatch, LeftLeaseSubclass);
        subclass = false;
        if (!destroyed) {
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            if (published && GetPropW(window, styler::OwnerProperty) == owner) {
                RemovePropW(window, styler::AdapterProperty);
                RemovePropW(window, styler::InlineProperty);
                RemovePropW(window, styler::OwnerProperty);
            }
        }
        --working;
        finished = true;
        WriteReport();
    }
    struct Callback {
        LeftLease *self;
        explicit Callback(LeftLease *value) : self(value) {
            ++self->calls;
        }
        ~Callback() {
            if (!--self->calls) {
                if (self->finished)
                    delete self;
                else if (self->pending && !self->working) {
                    self->Stop(self->reason);
                    if (self->finished)
                        delete self;
                }
            }
        }
    };
    static LRESULT CALLBACK Dispatch(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<LeftLease *>(data);
        Callback callback(self);
        if (message == styler::QueryMessage())
            return self->active && !self->pending ? styler::Acknowledged : 0;
        if (message == styler::DetachMessage()) {
            if (reinterpret_cast<HWND>(wp) != self->owner)
                return 0;
            if (self->calls > 1 || self->working || self->stopping)
                return styler::TransitionBusy;
            self->Stop("explicit-detach");
            return styler::Acknowledged;
        }
        if (message == styler::DiagnosticMessage()) {
            if (wp == static_cast<WPARAM>(styler::Metric::AdapterMode))
                return 279;
            if (wp == static_cast<WPARAM>(styler::Metric::InlineKind))
                return 20;
            if (wp == static_cast<WPARAM>(styler::Metric::CaptionPaints)) {
                unsigned total{};
                for (auto &draw : self->drawing)
                    if (draw.filter)
                        total += draw.filter->ReadDrawing().paints;
                return total;
            }
            return 0;
        }
        if (message == LeftLeaseMetrics()) {
            if (wp == 1)
                return self->Health();
            if (wp == 2)
                return self->inputMessages;
            if (wp == 4)
                return self->pressed >= 0;
            if (wp == 5)
                return GetCapture() == window;
            if (wp == 6)
                return self->canceled;
            if (wp == 40)
                return SendMessageW(window, styler::DetachMessage(), reinterpret_cast<WPARAM>(self->owner), 0);
            if (wp == 41)
                return SendMessageW(window, LeftLeaseMetrics(), 42, 0);
            if (wp == 42) {
                self->Stop("nested-invalidation");
                return styler::TransitionBusy;
            }
            if (wp >= 10 && wp < 16) {
                RECT rect{};
                if (!self->ButtonRect((wp - 10) / 2, rect))
                    return -1;
                return (wp - 10) % 2 ? (rect.top + rect.bottom) / 2 : (rect.left + rect.right) / 2;
            }
            if (wp == 3 && lp >= 0 && lp < 16 && self->Health() && !IsIconic(window)) {
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                RECT first{}, last{};
                if (!self->ButtonRect(0, first) || !self->ButtonRect(2, last))
                    return 0;
                auto inset = MulDiv(8, GetDpiForWindow(window), 96);
                RECT area{first.left + inset, first.top + inset, last.right, last.bottom - inset};
                CaptionPixels pixels;
                if (!pixels.Read(window, area))
                    return 0;
                constexpr DWORD colors[]{0xff5f57, 0xffbd2e, 0x28c840};
                unsigned mask{};
                for (size_t i = 0; i < 3; ++i) {
                    RECT rect{};
                    if (!self->ButtonRect(i, rect))
                        continue;
                    auto x = (rect.left + rect.right) / 2 - area.left, y = (rect.top + rect.bottom) / 2 - area.top;
                    auto offset = static_cast<size_t>(y) * pixels.width + x;
                    auto stats = self->drawing[i].filter->ReadDrawing();
                    if (x >= 0 && y >= 0 && offset < pixels.pixels.size() && pixels.pixels[offset] == colors[i] &&
                        stats.paints && !stats.rejected && !stats.stateChanges)
                        mask |= 1u << i;
                }
                pixels.Save(self->report.parent_path() /
                            (L"txgui-left-lease-" + std::to_wstring(GetCurrentProcessId()) + L"-paint-" +
                             std::to_wstring(lp) + L".bmp"));
                return mask;
            }
            return 0;
        }
        if (self->stopping || !self->active)
            return DefSubclassProc(window, message, wp, lp);
        if (message == WM_TIMER && wp == LeftLeaseSubclass) {
            if (self->working || self->calls > 1)
                return 0;
            if (!self->OwnerAlive())
                self->Stop("owner-lost");
            else if (!self->Health())
                self->Stop("original-tree-or-capability-changed");
            return 0;
        }
        if (message == WM_DESTROY || message == WM_NCDESTROY) {
            // Restore while the original tree is still available, before
            // forwarding destruction into the framework.
            self->Stop("window-destroyed");
            return DefSubclassProc(window, message, wp, lp);
        }
        if (message == WM_DPICHANGED || message == WM_THEMECHANGED || message == WM_SETTINGCHANGE) {
            self->Stop("unsupported-environment-change");
            return DefSubclassProc(window, message, wp, lp);
        }
        if (message == WM_NCHITTEST) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (self->Hit(point) >= 0)
                return HTCLIENT;
        }
        if (message == WM_LBUTTONDOWN) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window, &point);
            self->pressed = self->Hit(point);
            if (self->pressed >= 0) {
                if (!self->Health() || !self->OwnerAlive()) {
                    self->Stop("input-original-tree-changed");
                    return 0;
                }
                ++self->inputMessages;
            }
        }
        if (message == WM_LBUTTONUP && self->pressed >= 0) {
            if (!self->Health() || !self->OwnerAlive()) {
                self->Stop("input-original-tree-changed");
                return 0;
            }
            self->pressed = -1;
            ++self->inputMessages;
        }
        if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED) {
            if (self->pressed >= 0) {
                self->pressed = -1;
                ++self->canceled;
            }
        }
        return DefSubclassProc(window, message, wp, lp);
    }
};
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    constexpr LONG_PTR mask = WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (!styler::ValidContext(context) || context->adapterId != 279 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId() ||
        GetDpiForWindow(context->window) != 144 || IsZoomed(context->window) || IsIconic(context->window) ||
        GetCapture() || GetPropW(context->window, styler::OwnerProperty) ||
        (GetWindowLongPtrW(context->window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
        (GetWindowLongPtrW(context->window, GWL_STYLE) & mask) != mask) {
        captionGroup = {};
        return ERROR_NOT_SUPPORTED;
    }
    auto lease = std::make_unique<LeftLease>();
    lease->selected = std::move(captionGroup);
    captionGroup = {};
    lease->window = context->window;
    lease->owner = context->owner;
    GetWindowThreadProcessId(lease->owner, &lease->ownerPid);
    lease->process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, lease->ownerPid);
    lease->capabilities = GetWindowLongPtrW(lease->window, GWL_STYLE) & mask;
    OrderTree before;
    if (!lease->OwnerAlive() || !ReadOrderTree(lease->selected.root.Get(), before) ||
        !LeftSelection(before, lease->selected, lease->objects))
        return ERROR_NOT_SUPPORTED;
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self))
        return ERROR_INVALID_HANDLE;
    wchar_t module[32768]{};
    GetModuleFileNameW(self, module, 32768);
    lease->report = std::filesystem::path(module).parent_path() /
                    (L"txgui-left-lease-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    for (size_t i = 0; i < 3; ++i) {
        auto found = before.find(reinterpret_cast<uintptr_t>(lease->objects.buttons[i].Get()));
        if (found == before.end() || !found->second.enabled || !found->second.visible || found->second.hidden)
            return ERROR_NOT_SUPPORTED;
        lease->events[i] = txevents::Inspect(lease->objects.buttons[i].Get(), gfImage);
        if (!lease->events[i].complete || lease->events[i].numeric.size() != 1 ||
            lease->events[i].numeric[0].id != 0x1003 ||
            !txobserve::List(lease->objects.buttons[i].Get(), gfImage, lease->filters[i]))
            return ERROR_NOT_SUPPORTED;
    }
    lease->layout = std::make_unique<txleft::Scope>(gfImage, lease->objects);
    if (!lease->layout->Ready())
        return ERROR_NOT_SUPPORTED;
    lease->working = 1;
    lease->subclass = SetWindowSubclass(lease->window, LeftLease::Dispatch, LeftLeaseSubclass,
                                        reinterpret_cast<DWORD_PTR>(lease.get())) != FALSE;
    bool ok = lease->subclass && lease->layout->Apply();
    constexpr DWORD colors[]{0xffff5f57, 0xffffbd2e, 0xff28c840};
    for (size_t i = 0; i < 3 && ok; ++i) {
        RECT rect{};
        POINT origin{};
        ClientToScreen(lease->window, &origin);
        ok = lease->ButtonRect(i, rect);
        int width = rect.right - rect.left, height = rect.bottom - rect.top, diameter = MulDiv(12, 144, 96);
        RECT circle{(width - diameter) / 2, (height - diameter) / 2, (width + diameter) / 2, (height + diameter) / 2};
        ok = ok && lease->drawing[i].Attach(lease->objects.buttons[i].Get(), circle, colors[i],
                                            {rect.left - origin.x, rect.top - origin.y});
        if (ok)
            lease->drawing[i].filter->RequireOriginalOrigin(lease->window);
    }
    if (ok) {
        lease->timer = SetTimer(lease->window, LeftLeaseSubclass, 250, nullptr) != 0;
        ok = lease->timer && SetPropW(lease->window, styler::InlineProperty, reinterpret_cast<HANDLE>(20)) &&
             SetPropW(lease->window, styler::AdapterProperty, reinterpret_cast<HANDLE>(279)) &&
             SetPropW(lease->window, styler::OwnerProperty, lease->owner);
        lease->published = GetPropW(lease->window, styler::OwnerProperty) == lease->owner;
    }
    lease->working = 0;
    lease->active = ok;
    if (!ok || !lease->Health() || lease->pending) {
        lease->Stop("attach-failed");
        return ERROR_NOT_SUPPORTED;
    }
    lease->reason = "attached";
    lease->WriteReport();
    auto window = lease->window;
    lease.release();
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return ERROR_SUCCESS;
} catch (...) {
    captionGroup = {};
    return ERROR_UNHANDLED_EXCEPTION;
}
