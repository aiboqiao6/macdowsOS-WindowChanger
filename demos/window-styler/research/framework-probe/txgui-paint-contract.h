#pragma once

// Included only by the standalone owned-process fixture. This is not an
// injected adapter and accepts no external process or HWND.
namespace txpaint {
struct PaintArg {
    unsigned code{0x39a}, size{sizeof(PaintArg)};
    BOOL scaled{};
    IUnknown *graphics{};
    RECT reserved{};
};
using Send = HRESULT(__cdecl *)(IUnknown *, PaintArg *, BOOL *, LRESULT *, BOOL);
using FilterChange = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
using FillCircle = BOOL(__cdecl *)(void *, const RECT *, const SIZE *, DWORD);

struct Filter : IDispatch {
    ULONG references{1};
    IUnknown *frame{}, *graphics{}, *list{};
    void *canvas{};
    FillCircle fill{};
    FilterChange remove{};
    DWORD thread{GetCurrentThreadId()};
    unsigned stage7{}, stage8{}, paints{}, errors{};
    bool selfRemove{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch)
            return E_NOINTERFACE;
        *object = static_cast<IDispatch *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto remaining = --references;
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override {
        if (!count)
            return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **info) override {
        if (info)
            *info = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
                                     UINT *) override {
        return E_NOTIMPL;
    }
    // Native filter-list dispatch calls these exact slots with six arguments.
    // They are not the five-argument IGF frame message-handler interface.
    virtual HRESULT STDMETHODCALLTYPE Stage7(IUnknown *source, PaintArg *arg, BOOL *handled, LRESULT *, BOOL *route,
                                             BOOL *observers) {
        ++stage7;
        if (!arg || arg->code != 0x39a)
            return S_OK;
        if (source != frame || arg->graphics != graphics || !handled || !route || !observers ||
            GetCurrentThreadId() != thread) {
            ++errors;
            return E_INVALIDARG;
        }
        const RECT circle{12, 12, 24, 24};
        const SIZE radius{12, 12};
        if (!fill(canvas, &circle, &radius, 0xffff5f57)) {
            ++errors;
            return E_FAIL;
        }
        ++paints;
        *handled = TRUE;
        *route = FALSE;
        *observers = FALSE;
        if (selfRemove && FAILED(remove(list, this)))
            ++errors;
        return S_OK;
    }
    virtual HRESULT STDMETHODCALLTYPE Stage8(IUnknown *, PaintArg *, BOOL *, LRESULT *, BOOL *, BOOL *) {
        ++stage8;
        return S_OK;
    }
    virtual HRESULT STDMETHODCALLTYPE IsNative(BOOL *native) {
        if (!native)
            return E_POINTER;
        *native = TRUE;
        return S_OK;
    }
};

inline void Run(HMODULE gf, IUnknown *frame, bool passiveOnly = false) {
    auto image = reinterpret_cast<BYTE *>(gf);
    auto graphic = GetModuleHandleW(L"xGraphic32.dll");
    constexpr GUID graphicsIdentity{0x0fd7d531, 0xc786, 0x4d0b, {0xb0, 0xfb, 0x1e, 0x97, 0x99, 0x6f, 0xdf, 0xbc}};
    constexpr GUID filtersId{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
    constexpr BYTE prolog[]{0x55, 0x8b, 0xec, 0x6a, 0xff};
    if (!Check(graphic && styler::MatchesPdb(graphic, graphicsIdentity, 3) &&
                   !memcmp(image + 0x272e0, prolog, sizeof(prolog)) &&
                   !memcmp(image + 0x63590, prolog, sizeof(prolog)) &&
                   *reinterpret_cast<void ***>(frame) == reinterpret_cast<void **>(image + 0x1f4c00),
               "paint fixture matches the original Button dispatcher graphics constructor and graphics-engine ABI"))
        return;
    auto create = reinterpret_cast<void *(__cdecl *)(int, int, void *)>(GetProcAddress(graphic, "CreateCanvas"));
    auto destroy = reinterpret_cast<void(__cdecl *)(void *)>(GetProcAddress(graphic, "DeleteCanvas"));
    auto dc = reinterpret_cast<HDC(__cdecl *)(void *)>(GetProcAddress(graphic, "GetCanvasHandle"));
    auto fill =
        reinterpret_cast<BOOL(__cdecl *)(void *, DWORD, const RECT *)>(GetProcAddress(graphic, "FillSolidRect"));
    auto circle = reinterpret_cast<FillCircle>(GetProcAddress(graphic, "FillRoundRectEx"));
    auto setClip =
        reinterpret_cast<BOOL(__cdecl *)(void *, const RECT *)>(GetProcAddress(graphic, "SetCanvasClipRect"));
    if (!Check(create && destroy && dc && fill && circle && setClip,
               "all original owned-canvas painting exports exist"))
        return;
    auto canvas = create(96, 48, nullptr);
    if (!Check(canvas != nullptr, "create the owned original canvas for framework paint notifications"))
        return;
    struct CanvasCleanup {
        void *canvas;
        decltype(destroy) release;
        ~CanvasCleanup() {
            release(canvas);
        }
    } canvasCleanup{canvas, destroy};
    // The original rendering path at 0x186e36 constructs this same 0x40-byte
    // graphics object on its stack, detaches the borrowed canvas, then destroys
    // it. Mirror that native lifetime only inside this owned test process.
    alignas(16) BYTE storage[0x40]{};
    auto construct = reinterpret_cast<void *(__thiscall *)(void *, HWND, void *)>(image + 0x272e0);
    auto graphics = static_cast<IUnknown *>(construct(storage, nullptr, canvas));
    struct GraphicsCleanup {
        BYTE *image;
        void *graphics;
        ~GraphicsCleanup() {
            reinterpret_cast<void *(__thiscall *)(void *)>(image + 0x27630)(graphics);
            reinterpret_cast<void(__thiscall *)(void *)>(image + 0x273f0)(graphics);
        }
    } graphicsCleanup{image, graphics};
    if (!Check(graphics == reinterpret_cast<IUnknown *>(storage) &&
                   *reinterpret_cast<void ***>(graphics) == reinterpret_cast<void **>(image + 0x1ecd6c),
               "original stack graphics construction returns its real graphics vtable"))
        return;
    ComPtr<IUnknown> filters;
    if (!Check(SUCCEEDED(frame->QueryInterface(filtersId, &filters)) && filters,
               "owned real Button exposes its original filter-list interface"))
        return;
    auto filterTable = *reinterpret_cast<void ***>(filters.Get());
    if (!Check(filterTable == reinterpret_cast<void **>(image + 0x1f50fc) && filterTable[7] == image + 0x937d0 &&
                   filterTable[8] == image + 0x937f0,
               "original filter add and remove wrappers match the independently inspected ABI"))
        return;
    auto add = reinterpret_cast<FilterChange>(filterTable[7]);
    auto remove = reinterpret_cast<FilterChange>(filterTable[8]);
    if (passiveOnly) {
        ComPtr<txobserve::Filter> passive;
        passive.Attach(new txobserve::Filter(frame, image));
        struct PassiveCleanup {
            IUnknown *list, *observer;
            FilterChange remove;
            ~PassiveCleanup() {
                remove(list, observer);
            }
        } cleanup{filters.Get(), passive.Get(), remove};
        std::vector<IUnknown *> original, during, restored;
        Check(txobserve::List(frame, image, original) && original.empty(),
              "owned passive observer starts with an empty native filter list");
        auto send = reinterpret_cast<Send>(image + 0x63590);
        const RECT full{0, 0, 96, 48};
        PaintArg arg;
        arg.graphics = graphics;
        BOOL baseline{};
        LRESULT baselineResult{};
        bool filled = fill(canvas, 0xff20252a, &full) != FALSE;
        SetLastError(0x1234);
        auto baselineStatus = send(frame, &arg, &baseline, &baselineResult, TRUE);
        auto baselineError = GetLastError();
        Check(filled && SUCCEEDED(baselineStatus), "capture the unobserved original paint dispatch result");
        Check(SUCCEEDED(add(filters.Get(), passive.Get())) && passive->References() == 2 &&
                  txobserve::List(frame, image, during) && during.size() == original.size() + 1 &&
                  during.back() == passive.Get(),
              "original registration appends one passive observer without changing existing entries");
        BOOL handled{};
        LRESULT result{};
        SetLastError(0x1234);
        auto observedStatus = send(frame, &arg, &handled, &result, TRUE);
        auto observedError = GetLastError();
        Check(SUCCEEDED(observedStatus) && handled == baseline && result == baselineResult &&
                  observedError == baselineError,
              "passive observation preserves original handled result and thread last-error values");
        auto record = passive->Read();
        Check(record.stage7 == 1 && record.stage8 == 1 && record.paints == 2 && record.count == 1 &&
                  record.foreignSource == 0 && record.foreignThread == 0 && record.invalidPayload == 0,
              "passive observer records both original paint stages without invalid reads");
        Check(record.records[0].originalGdi && !record.records[0].originalD2d && record.records[0].canvasRead &&
                  record.records[0].tableRva == 0x1ecd6c && record.records[0].size.cx == 96 &&
                  record.records[0].size.cy == 48 && EqualRect(&record.records[0].clip, &full),
              "passive observation correctly identifies the original graphics size and clipping profile");
        bool unchanged = true;
        auto handle = dc(canvas);
        GdiFlush();
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 96; ++x)
                unchanged &= GetPixel(handle, x, y) == RGB(32, 37, 42);
        Check(unchanged, "passive observation writes no canvas pixels");
        Check(SUCCEEDED(remove(filters.Get(), passive.Get())) && passive->References() == 1 &&
                  txobserve::List(frame, image, restored) && restored == original,
              "native removal restores the original filter list and observer reference count");
        auto calls = passive->Read().paints;
        Check(SUCCEEDED(send(frame, &arg, &handled, &result, TRUE)) && passive->Read().paints == calls,
              "removed passive observer receives no subsequent original paint dispatch");
        return;
    }
    ComPtr<Filter> observer;
    observer.Attach(new Filter);
    observer->frame = frame;
    observer->graphics = graphics;
    observer->canvas = canvas;
    observer->fill = circle;
    observer->list = filters.Get();
    observer->remove = remove;
    struct ObserverCleanup {
        IUnknown *list, *observer;
        FilterChange remove;
        ~ObserverCleanup() {
            remove(list, observer);
        }
    } observerCleanup{filters.Get(), observer.Get(), remove};
    auto send = reinterpret_cast<Send>(image + 0x63590);
    using SetAttribute = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    auto set = reinterpret_cast<SetAttribute>(
        GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    FrameState initial{};
    if (!Check(set && ReadFrame(frame, initial) && SUCCEEDED(set(frame, L"rect", L"0,0,28,32")),
               "configure a positive owned Button rectangle before exercising its paint path"))
        return;
    struct RectCleanup {
        IUnknown *frame;
        SetAttribute set;
        RECT rect;
        ~RectCleanup() {
            set(frame, L"rect", RectText(rect).c_str());
        }
    } rectCleanup{frame, set, initial.rect};
    constexpr GUID eventsId{0x6ccbbc7b, 0x5c0a, 0x49aa, {0xb1, 0x99, 0x67, 0x33, 0xfd, 0xcd, 0x7d, 0xd4}};
    constexpr GUID sinkId{0x78b5f698, 0x78d3, 0x46f0, {0x88, 0x34, 0xe7, 0x01, 0x76, 0xd0, 0x34, 0x40}};
    ComPtr<IUnknown> ownedGroup, events, sink;
    if (!Check(FixtureFactory(SysButtonId, SysButtonId, reinterpret_cast<void **>(ownedGroup.GetAddressOf())) &&
                   ownedGroup && SUCCEEDED(ownedGroup->QueryInterface(sinkId, &sink)) &&
                   SUCCEEDED(frame->QueryInterface(eventsId, &events)),
               "create an original owned click notifier and the real Button event interface"))
        return;
    auto eventTable = *reinterpret_cast<void ***>(events.Get());
    if (eventTable[7] != image + 0x93910 || eventTable[14] != image + 0x93a10) {
        Check(false, "original owned click subscription ABI matches before any mutation or cleanup");
        return;
    }
    using ClearEvents = HRESULT(STDMETHODCALLTYPE *)(IUnknown *);
    struct EventsCleanup {
        IUnknown *events;
        ClearEvents clear;
        ~EventsCleanup() {
            clear(events);
        }
    } eventsCleanup{events.Get(), reinterpret_cast<ClearEvents>(eventTable[14])};
    using AddNumeric = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned, IUnknown *);
    if (!Check(SUCCEEDED(reinterpret_cast<AddNumeric>(eventTable[7])(events.Get(), 0x1003, sink.Get())),
               "populate a real original click subscription without invoking its callback"))
        return;
    auto eventsBefore = txevents::Inspect(frame, image);
    FrameState before{}, after{};
    Check(ReadFrame(frame, before) && eventsBefore.complete && eventsBefore.numeric.size() == 1,
          "save positive owned Button geometry and a populated original click subscription");
    auto windows = WindowCount();
    const RECT full{0, 0, 96, 48};
    auto repaint = [&] { return fill(canvas, 0xff20252a, &full) != FALSE; };
    BOOL lastHandled{};
    auto dispatch = [&](unsigned code = 0x39a) {
        PaintArg arg;
        arg.code = code;
        arg.graphics = graphics;
        BOOL handled{};
        LRESULT result{};
        auto status = send(frame, &arg, &handled, &result, TRUE);
        lastHandled = handled;
        return SUCCEEDED(status);
    };
    auto pixels = [&] {
        std::vector<COLORREF> result;
        auto handle = dc(canvas);
        if (!handle)
            return result;
        GdiFlush();
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 96; ++x)
                result.push_back(GetPixel(handle, x, y));
        return result;
    };
    auto uniform = [&](const std::vector<COLORREF> &values) {
        return values.size() == 96 * 48 &&
               std::all_of(values.begin(), values.end(), [](COLORREF color) { return color == RGB(32, 37, 42); });
    };
    Check(SUCCEEDED(add(filters.Get(), observer.Get())) && observer->references == 2,
          "original filter list retains the owned paint observer exactly once");
    Check(repaint() && dispatch(), "dispatch an owned paint notification through the original GF dispatcher");
    auto firstPixels = pixels();
    std::cout << "stage7=" << observer->stage7 << "; stage8=" << observer->stage8 << "; paints=" << observer->paints
              << "; errors=" << observer->errors << std::endl;
    Check(observer->stage7 == 1 && observer->stage8 == 1 && observer->paints == 1 && observer->errors == 0,
          "original dispatcher invokes the registered filter stages on the expected frame graphics and thread");
    Check(firstPixels.size() == 96 * 48 && firstPixels[18 * 96 + 18] == RGB(255, 95, 87) &&
              firstPixels[12 * 96 + 12] == RGB(32, 37, 42),
          "framework-dispatched paint reaches the native canvas as a filled antialiased light");
    auto paints = observer->paints;
    Check(dispatch(0x7fff) && !lastHandled && observer->paints == paints && pixels() == firstPixels,
          "unrelated original notifications do not draw or consume paint state");
    Check(SUCCEEDED(remove(filters.Get(), observer.Get())) && observer->references == 1,
          "original filter removal releases its retained observer reference");
    Check(repaint() && dispatch() && observer->paints == paints && uniform(pixels()),
          "detached observer receives no paint and original repaint contains no injected pixels");
    const RECT clip{18, 12, 24, 24};
    Check(SUCCEEDED(add(filters.Get(), observer.Get())) && setClip(canvas, &clip) && dispatch() &&
              setClip(canvas, &full),
          "reattach and dispatch under the original partial paint clip, then restore clipping");
    auto clippedPixels = pixels();
    bool clipped = clippedPixels.size() == 96 * 48;
    unsigned changed{};
    for (int y = 0; y < 48 && clipped; ++y)
        for (int x = 0; x < 96; ++x)
            if (clippedPixels[static_cast<size_t>(y) * 96 + x] != RGB(32, 37, 42)) {
                ++changed;
                clipped &= x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom;
            }
    Check(clipped && changed > 0, "framework-dispatched custom paint honors the original partial invalidation clip");
    observer->selfRemove = true;
    Check(repaint() && dispatch() && observer->references == 1 && observer->errors == 0,
          "original filter snapshot pins the observer while it removes itself during painting");
    paints = observer->paints;
    Check(repaint() && dispatch() && paints == observer->paints && uniform(pixels()),
          "self-removed observer cannot leave a stale paint callback");
    observer->selfRemove = false;
    bool repeated = true;
    for (unsigned i = 0; i < 32; ++i)
        repeated &= SUCCEEDED(add(filters.Get(), observer.Get())) && dispatch() &&
                    SUCCEEDED(remove(filters.Get(), observer.Get())) && observer->references == 1;
    Check(repeated && observer->errors == 0, "32 original filter registration paint removal cycles retain no observer");
    auto eventsAfter = txevents::Inspect(frame, image);
    Check(ReadFrame(frame, after) && Equal(before, after) && eventsAfter.complete &&
              eventsBefore.numeric == eventsAfter.numeric && eventsBefore.named == eventsAfter.named &&
              eventsBefore.listeners == eventsAfter.listeners,
          "paint registration and dispatch preserve the owned Button geometry and all original click bindings");
    Check(*reinterpret_cast<ULONG *>(storage + 0x3c) == 0,
          "original dispatch retains no borrowed stack graphics references");
    Check(WindowCount() == windows, "original paint notification contract creates no top-level HWNDs");
    Check(SUCCEEDED(eventsCleanup.clear(events.Get())) && txevents::Inspect(frame, image).numeric.empty(),
          "remove only the fixture-owned click subscription before notifier destruction");
}
} // namespace txpaint
