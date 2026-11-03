#pragma once
// Bounded passive observation of actual renderer-generated notifications.
// Only our own three filter registrations are temporarily added and removed.
namespace {
struct TxPaintState {
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
    bool Same(const TxPaintState &other) const {
        return EqualRect(&rect, &other.rect) && EqualRect(&screen, &other.screen) &&
               EqualRect(&padding, &other.padding) && EqualRect(&margin, &other.margin) && hidden == other.hidden &&
               visible == other.visible && enabled == other.enabled;
    }
};
void PaintRect(std::ostream &report, const RECT &rect) {
    report << '[' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << ']';
}
void PaintAncestors(std::ostream &report, IUnknown *parent) {
    std::set<IUnknown *> visited;
    report << '[';
    bool first = true;
    ComPtr<IUnknown> current = parent;
    auto count = reinterpret_cast<FrameCount>(
        GetProcAddress(reinterpret_cast<HMODULE>(gfImage), "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    auto at = reinterpret_cast<FrameAt>(GetProcAddress(reinterpret_cast<HMODULE>(gfImage),
                                                       "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    while (current && visited.size() < 24 && visited.insert(current.Get()).second) {
        if (!first)
            report << ',';
        first = false;
        TxPaintState state;
        auto type = reinterpret_cast<const wchar_t *(__cdecl *)(IUnknown *)>(gfImage + 0x1b0600)(current.Get());
        unsigned children{};
        bool valid = state.Read(current.Get());
        report << "{\"identity\":" << reinterpret_cast<uintptr_t>(current.Get()) << ",\"type\":" << StaticText(type)
               << ",\"stateRead\":" << (valid ? "true" : "false") << ",\"rect\":";
        PaintRect(report, state.rect);
        report << ",\"screen\":";
        PaintRect(report, state.screen);
        report << ",\"padding\":";
        PaintRect(report, state.padding);
        report << ",\"visible\":" << (state.visible && !state.hidden ? "true" : "false") << ",\"children\":[";
        if (count && at && SUCCEEDED(count(current.Get(), &children)) && children <= 64) {
            for (unsigned i = 0; i < children; ++i) {
                ComPtr<IUnknown> child;
                if (i)
                    report << ',';
                if (FAILED(at(current.Get(), static_cast<int>(i), &child)) || !child) {
                    report << "null";
                    continue;
                }
                TxPaintState childState;
                auto childType =
                    reinterpret_cast<const wchar_t *(__cdecl *)(IUnknown *)>(gfImage + 0x1b0600)(child.Get());
                report << "{\"identity\":" << reinterpret_cast<uintptr_t>(child.Get())
                       << ",\"type\":" << StaticText(childType)
                       << ",\"stateRead\":" << (childState.Read(child.Get()) ? "true" : "false") << ",\"rect\":";
                PaintRect(report, childState.rect);
                report << ",\"screen\":";
                PaintRect(report, childState.screen);
                report << ",\"visible\":" << (childState.visible && !childState.hidden ? "true" : "false") << '}';
            }
        }
        report << ']';
        if (type && !wcscmp(type, L"IGFStackPanel") &&
            *reinterpret_cast<void ***>(current.Get()) == reinterpret_cast<void **>(gfImage + 0x220f58)) {
            using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int *);
            auto get = Slot<Get>(current.Get(), 0x30c / 4);
            int direction{};
            if (get == reinterpret_cast<Get>(gfImage + 0x93b00) && SUCCEEDED(get(current.Get(), &direction)))
                report << ",\"originalOrientation\":" << direction;
        }
        report << '}';
        auto it = paintParents.find(reinterpret_cast<uintptr_t>(current.Get()));
        current = it == paintParents.end() ? nullptr : it->second;
    }
    report << ']';
}
struct TxPaintLease {
    ComPtr<IUnknown> list;
    ComPtr<txobserve::Filter> observer;
    txobserve::FilterChange remove{};
    bool attached{};
    bool Detach() {
        if (!attached)
            return true;
        if (remove && SUCCEEDED(remove(list.Get(), observer.Get()))) {
            attached = false;
            return true;
        }
        return false;
    }
    ~TxPaintLease() {
        Detach();
    }
};
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    if (!styler::ValidContext(context) || context->adapterId != 274 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId()) {
        captionGroup = {};
        paintParents.clear();
        return ERROR_NOT_SUPPORTED;
    }
    auto selected = std::move(captionGroup);
    captionGroup = {};
    HMODULE self{};
    // A native filter snapshot can retain a COM callback until dispatch ends.
    // Never unload callback code, including when a research controller exits.
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
    wchar_t modulePath[32768]{};
    GetModuleFileNameW(self, modulePath, 32768);
    auto path = std::filesystem::path(modulePath).parent_path() /
                (L"txgui-paint-observation-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    std::ofstream report(path, std::ios::trunc);
    if (!self || !report) {
        paintParents.clear();
        return ERROR_WRITE_FAULT;
    }
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
    report << "{\"pid\":" << GetCurrentProcessId() << ",\"guiThread\":" << GetCurrentThreadId()
           << ",\"layoutModified\":false,\"pixelsWritten\":false,\"inputSent\":false,\"syntheticNotifications\":false,"
              "\"persistentSession\":false,\"ancestorChain\":";
    PaintAncestors(report, selected.parent.Get());
    paintParents.clear();
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool ready = check(!IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) && !GetCapture() &&
                           !GetPropW(window, styler::OwnerProperty),
                       "normal-enabled-window-without-input-capture-or-owner");
    std::array<TxPaintState, 3> before;
    std::array<txevents::Result, 3> events;
    std::array<std::vector<IUnknown *>, 3> originalFilters;
    std::array<TxPaintLease, 3> leases;
    uintptr_t notifier{};
    constexpr GUID filtersId{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
    for (unsigned i = 0; i < 3; ++i) {
        auto button = selected.buttons[i].Get();
        auto &lease = leases[i];
        ready &=
            check(before[i].Read(button) && before[i].visible && !before[i].hidden && before[i].enabled &&
                      before[i].screen.right > before[i].screen.left && before[i].screen.bottom > before[i].screen.top,
                  "original-visible-enabled-caption-source");
        events[i] = txevents::Inspect(button, gfImage);
        auto &bindings = events[i];
        bool click = bindings.profile && bindings.complete && bindings.numeric.size() == 1 &&
                     bindings.numeric[0].id == 0x1003 && bindings.named.empty() && bindings.listeners.empty();
        if (click && !i)
            notifier = bindings.numeric[0].sink;
        ready &=
            check(click && bindings.numeric[0].sink == notifier, "original-single-click-and-common-caption-notifier");
        ready &= check(txobserve::List(button, gfImage, originalFilters[i]) &&
                           SUCCEEDED(button->QueryInterface(filtersId, &lease.list)) && lease.list,
                       "save-original-filter-identities-and-order");
        auto table = lease.list ? *reinterpret_cast<void ***>(lease.list.Get()) : nullptr;
        bool abi = table == reinterpret_cast<void **>(gfImage + 0x1f50fc) && table[7] == gfImage + 0x937d0 &&
                   table[8] == gfImage + 0x937f0;
        ready &= check(abi, "original-filter-registration-abi");
        if (abi) {
            lease.remove = reinterpret_cast<txobserve::FilterChange>(table[8]);
            lease.observer.Attach(new txobserve::Filter(button, gfImage));
        }
    }
    if (ready) {
        for (unsigned i = 0; i < 3; ++i) {
            auto &lease = leases[i];
            auto table = *reinterpret_cast<void ***>(lease.list.Get());
            lease.attached =
                SUCCEEDED(reinterpret_cast<txobserve::FilterChange>(table[7])(lease.list.Get(), lease.observer.Get()));
            ready &= check(lease.attached && lease.observer->References() == 2, "register-one-owned-passive-observer");
            std::vector<IUnknown *> current;
            auto expected = originalFilters[i];
            expected.push_back(lease.observer.Get());
            ready &= check(txobserve::List(selected.buttons[i].Get(), gfImage, current) && current == expected,
                           "registration-preserves-all-original-filter-entries");
        }
        for (unsigned pass = 0; pass < 3 && ready; ++pass) {
            check(RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
                  "request-normal-original-window-repaint");
            bool observed = true;
            for (auto &lease : leases)
                observed &= lease.observer->Read().paints != 0;
            if (observed)
                break;
        }
    }
    std::array<txobserve::Snapshot, 3> observed;
    for (unsigned i = 0; i < 3; ++i) {
        auto &lease = leases[i];
        if (lease.observer)
            observed[i] = lease.observer->Read();
        auto &data = observed[i];
        check(data.paints > 0 && data.stage7 > 0 && data.stage8 > 0,
              "real-renderer-generated-caption-paint-notifications");
        check(data.foreignThread == 0 && data.foreignSource == 0 && data.invalidPayload == 0,
              "original-notifications-have-valid-source-thread-and-payload");
        check(lease.Detach() && lease.observer && lease.observer->References() == 1,
              "remove-only-owned-observer-and-release-retained-reference");
        std::vector<IUnknown *> restored;
        check(txobserve::List(selected.buttons[i].Get(), gfImage, restored) && restored == originalFilters[i],
              "restore-original-filter-identities-and-order");
        TxPaintState after;
        auto bindings = txevents::Inspect(selected.buttons[i].Get(), gfImage);
        check(after.Read(selected.buttons[i].Get()) && before[i].Same(after) && bindings.complete &&
                  bindings.numeric == events[i].numeric && bindings.named == events[i].named &&
                  bindings.listeners == events[i].listeners,
              "original-caption-geometry-state-and-click-subscriptions-unchanged");
    }
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    for (unsigned i = 0; i < 3; ++i)
        check(leases[i].observer && leases[i].observer->Read().paints == observed[i].paints,
              "detached-observer-receives-no-later-renderer-paint");
    RECT finalOuter{}, finalClient{};
    check(GetWindowRect(window, &finalOuter) && GetClientRect(window, &finalClient) && EqualRect(&outer, &finalOuter) &&
              EqualRect(&client, &finalClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
              GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle && !GetCapture() &&
              !GetPropW(window, styler::OwnerProperty),
          "original-window-geometry-styles-capture-and-owner-unchanged");
    report << ",\"observations\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            report << ',';
        auto &data = observed[i];
        report << "{\"intent\":\"" << txcaption::Name(static_cast<txcaption::Intent>(i))
               << "\",\"stage7\":" << data.stage7 << ",\"stage8\":" << data.stage8 << ",\"paints\":" << data.paints
               << ",\"foreignSource\":" << data.foreignSource << ",\"foreignThread\":" << data.foreignThread
               << ",\"invalidPayload\":" << data.invalidPayload << ",\"records\":[";
        for (unsigned j = 0; j < data.count; ++j) {
            if (j)
                report << ',';
            auto &record = data.records[j];
            report << "{\"graphicsInGf\":" << (record.inGf ? "true" : "false") << ",\"tableRva\":" << record.tableRva
                   << ",\"originalGdi\":" << (record.originalGdi ? "true" : "false")
                   << ",\"originalD2d\":" << (record.originalD2d ? "true" : "false")
                   << ",\"canvasRead\":" << (record.canvasRead ? "true" : "false") << ",\"size\":[" << record.size.cx
                   << ',' << record.size.cy << "],\"clip\":";
            PaintRect(report, record.clip);
            report << ",\"occurrences\":" << record.occurrences << '}';
        }
        report << "]}";
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
    paintParents.clear();
    return ERROR_UNHANDLED_EXCEPTION;
}
