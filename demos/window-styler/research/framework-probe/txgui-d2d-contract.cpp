#include "../../pe-identity.h"
#include "txgui-d2d-filter.h"
#include "txgui-events.h"
#include <objbase.h>
#include <wrl/client.h>
#include <dxgi.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <array>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
unsigned checks{}, failures{};
bool Check(bool value, const char *message) {
    ++checks;
    failures += !value;
    std::cout << (value ? "PASS " : "FAIL ") << message << std::endl;
    return value;
}
bool Readable(const void *address, size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    auto begin = reinterpret_cast<uintptr_t>(address);
    return address && begin + size >= begin && VirtualQuery(address, &region, sizeof(region)) &&
           region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           begin + size <= reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
}
struct OwnedWindow {
    HWND value{};
    bool Destroy() {
        if (!value)
            return true;
        auto original = value;
        if (!DestroyWindow(original))
            return false;
        value = nullptr;
        return !IsWindow(original);
    }
    ~OwnedWindow() {
        Destroy();
    }
};
struct Api {
    BYTE *image{};
    void *service{};
    using Create = IUnknown *(__thiscall *)(void *, HWND, BOOL);
    using Kind = int(__thiscall *)(void *);
    Create create{};
    unsigned CachedTargets() const {
        auto factory = *reinterpret_cast<BYTE **>(static_cast<BYTE *>(service) + 0x10);
        return Readable(factory, 0x94) ? *reinterpret_cast<unsigned *>(factory + 0x34) : UINT_MAX;
    }
    bool Initialize(HMODULE gf) {
        image = reinterpret_cast<BYTE *>(gf);
        auto get = GetProcAddress(gf, "?GetRenderSerice@@YAPAVIRenderService@@XZ");
        if (reinterpret_cast<BYTE *>(get) != image + 0x3c5a0)
            return false;
        service = reinterpret_cast<void *(__cdecl *)()>(get)();
        if (!Readable(service, 0x14) ||
            *reinterpret_cast<void ***>(service) != reinterpret_cast<void **>(image + 0x1ee268))
            return false;
        auto table = *reinterpret_cast<void ***>(service);
        if (table[3] != image + 0x3c470 || table[8] != image + 0x3c580)
            return false;
        auto kind = reinterpret_cast<Kind>(table[8])(service);
        std::cout << "originalRenderKind=" << kind << std::endl;
        if (!Check(kind == 4, "original renderer initialization selects the actually observed D2D family"))
            return false;
        create = reinterpret_cast<Create>(table[3]);
        return true;
    }
};
struct CanvasApi {
    void *(__cdecl *create)(int, int, void *){};
    void(__cdecl *destroy)(void *){};
    HDC(__cdecl *dc)(void *) {};
    bool Initialize() {
        auto graphic = GetModuleHandleW(L"xGraphic32.dll");
        constexpr GUID identity{0x0fd7d531, 0xc786, 0x4d0b, {0xb0, 0xfb, 0x1e, 0x97, 0x99, 0x6f, 0xdf, 0xbc}};
        if (!styler::MatchesPdb(graphic, identity, 3))
            return false;
        auto c = GetProcAddress(graphic, "CreateSystemCanvas"), d = GetProcAddress(graphic, "DeleteCanvas"),
             h = GetProcAddress(graphic, "GetCanvasHandle");
        auto image = reinterpret_cast<BYTE *>(graphic);
        if (reinterpret_cast<BYTE *>(c) != image + 0x72d40 || reinterpret_cast<BYTE *>(d) != image + 0x72e10 ||
            reinterpret_cast<BYTE *>(h) != image + 0x72e30)
            return false;
        create = reinterpret_cast<decltype(create)>(c);
        destroy = reinterpret_cast<decltype(destroy)>(d);
        dc = reinterpret_cast<decltype(dc)>(h);
        return true;
    }
};
struct OwnedCanvas {
    CanvasApi &api;
    void *value{};
    ~OwnedCanvas() {
        if (value)
            api.destroy(value);
    }
};
using Begin = void(__thiscall *)(void *);
using End = bool(__thiscall *)(void *, RECT *, HDC, BYTE);
using SolidFill = void(__thiscall *)(void *, DWORD, const RECT *, const SIZE *);
using Origin = void(__thiscall *)(void *, int, int, POINT *);
using GetPoint = void(__thiscall *)(void *, POINT *);
using GetClip = void(__thiscall *)(void *, RECT *);
using PushClip = void(__thiscall *)(void *, const RECT *);
using Pop = void(__thiscall *)(void *);
using PushAlpha = void(__thiscall *)(void *, const RECT *, BYTE);
using Alpha = BYTE(__thiscall *)(void *, BYTE);
constexpr RECT Full{0, 0, 96, 48};
constexpr DWORD Background = 0xff20252a;
constexpr std::array<DWORD, 3> Colors{0xffff5f57, 0xffffbd2e, 0xff28c840};
COLORREF Rgb(DWORD argb) {
    return RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
}
std::vector<COLORREF> Pixels(HDC dc) {
    std::vector<COLORREF> pixels;
    if (!dc)
        return pixels;
    GdiFlush();
    pixels.reserve(96 * 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 96; ++x)
            pixels.push_back(GetPixel(dc, x, y));
    return pixels;
}
void PixelContract(Api &api, IUnknown *windowTarget) {
    auto windowTable = *reinterpret_cast<void ***>(windowTarget);
    constexpr GUID bitmapTargetId{0xabcc9004, 0x06d3, 0x4836, {0xbb, 0x33, 0x12, 0x15, 0xa9, 0xb6, 0xa7, 0xf3}};
    if (!Check(windowTable[0x14 / 4] == api.image + 0x1a200 &&
                   !memcmp(api.image + 0x1eb030, &bitmapTargetId, sizeof(bitmapTargetId)),
               "original render-target query method and bitmap-target GUID match the framework self-test"))
        return;
    using QueryTarget = HRESULT(__thiscall *)(void *, GUID, IUnknown **);
    ComPtr<IUnknown> bitmapTarget;
    auto status = reinterpret_cast<QueryTarget>(windowTable[0x14 / 4])(windowTarget, bitmapTargetId, &bitmapTarget);
    if (!Check(SUCCEEDED(status) && bitmapTarget,
               "request a real original compatible bitmap target for native pixel readback"))
        return;
    auto target = bitmapTarget.Get();
    auto table = *reinterpret_cast<void ***>(target);
    if (!Check(
            table == reinterpret_cast<void **>(api.image + 0x1eae94) && table[0x1c / 4] == api.image + 0x16ad0 &&
                table[0x20 / 4] == api.image + 0x194f0 && table[0x58 / 4] == api.image + 0x19870,
            "original bitmap target has its own begin/end methods and shares the actual D2D solid-fill implementation"))
        return;
    CanvasApi canvasApi;
    if (!Check(canvasApi.Initialize(), "guard the original output-canvas library identity and exports"))
        return;
    OwnedCanvas canvas{canvasApi, canvasApi.create(96, 48, nullptr)};
    auto dc = canvas.value ? canvasApi.dc(canvas.value) : nullptr;
    if (!Check(dc != nullptr, "create an owned output memory DC without reading the screen or a user HWND"))
        return;
    auto begin = reinterpret_cast<Begin>(table[0x1c / 4]);
    auto end = reinterpret_cast<End>(table[0x20 / 4]);
    auto fill = reinterpret_cast<SolidFill>(table[0x58 / 4]);
    if (!Check(table[0x24 / 4] == api.image + 0x1c1c0 && table[0x28 / 4] == api.image + 0x1a700 &&
                   table[0x2c / 4] == api.image + 0x1c1f0 && table[0x30 / 4] == api.image + 0x1a720 &&
                   table[0x34 / 4] == api.image + 0x1ad40 && table[0x38 / 4] == api.image + 0x1aca0 &&
                   table[0x3c / 4] == api.image + 0x1a0a0 && table[0x40 / 4] == api.image + 0x1aed0 &&
                   table[0x44 / 4] == api.image + 0x1acc0 && table[0x74 / 4] == api.image + 0x1a6c0,
               "native D2D origin clip and scoped-alpha methods match their independently inspected signatures"))
        return;
    auto viewport = reinterpret_cast<Origin>(table[0x24 / 4]);
    auto windowOrg = reinterpret_cast<Origin>(table[0x2c / 4]);
    auto getViewport = reinterpret_cast<GetPoint>(table[0x28 / 4]);
    auto getWindowOrg = reinterpret_cast<GetPoint>(table[0x30 / 4]);
    auto getClip = reinterpret_cast<GetClip>(table[0x3c / 4]);
    auto pushClip = reinterpret_cast<PushClip>(table[0x34 / 4]);
    auto popClip = reinterpret_cast<Pop>(table[0x38 / 4]);
    auto pushAlpha = reinterpret_cast<PushAlpha>(table[0x40 / 4]);
    auto popAlpha = reinterpret_cast<Pop>(table[0x44 / 4]);
    auto alpha = reinterpret_cast<Alpha>(table[0x74 / 4]);
    struct State {
        POINT viewport{}, window{};
        RECT clip{};
        BYTE alpha{};
        bool Same(const State &other) const {
            return viewport.x == other.viewport.x && viewport.y == other.viewport.y && window.x == other.window.x &&
                   window.y == other.window.y && EqualRect(&clip, &other.clip) && alpha == other.alpha;
        }
    };
    auto state = [&] {
        State result;
        getViewport(target, &result.viewport);
        getWindowOrg(target, &result.window);
        getClip(target, &result.clip);
        result.alpha = alpha(target, 255);
        return result;
    };
    // Native bitmap EndPaint obtains this owned IDXGISurface1's DC for
    // readback. Pair it with the public ReleaseDC operation before reusing the
    // bitmap for another BeginDraw; no native object fields are modified.
    auto surfaceObject = *reinterpret_cast<IUnknown **>(reinterpret_cast<BYTE *>(target) + 0x94);
    ComPtr<IDXGISurface1> surface;
    if (!Check(surfaceObject && SUCCEEDED(surfaceObject->QueryInterface(IID_PPV_ARGS(&surface))) && surface,
               "owned original bitmap target exposes the DXGI surface used by its native GetDC readback"))
        return;
    auto readback = [&] {
        RECT dirty = Full;
        bool completed = end(target, &dirty, dc, 255);
        GdiFlush();
        auto released = surface->ReleaseDC(nullptr);
        std::cout << "readback=" << completed << "; releaseDC=" << std::hex << static_cast<unsigned long>(released)
                  << std::dec << std::endl;
        return completed && SUCCEEDED(released);
    };
    begin(target);
    fill(target, Background, &Full, nullptr);
    auto originalState = state();
    Check(originalState.viewport.x == 0 && originalState.viewport.y == 0 && originalState.window.x == 0 &&
              originalState.window.y == 0 && EqualRect(&originalState.clip, &Full) && originalState.alpha == 255,
          "owned original target starts with full clipping zero origins and opaque drawing");
    Check(readback(), "original bitmap readback copies D2D pixels and releases only the owned interop DC");
    auto background = Pixels(dc);
    Check(background.size() == 96 * 48 && std::all_of(background.begin(), background.end(),
                                                      [](COLORREF color) { return color == Rgb(Background); }),
          "every output pixel matches the original D2D solid background fill");
    const std::array<RECT, 3> circles{RECT{12, 12, 24, 24}, RECT{34, 12, 46, 24}, RECT{56, 12, 68, 24}};
    const SIZE radius{6, 6};
    begin(target);
    fill(target, Background, &Full, nullptr);
    for (size_t i = 0; i < circles.size(); ++i)
        fill(target, Colors[i], &circles[i], &radius);
    Check(state().Same(originalState), "native three-light fill preserves original clip origins and global alpha");
    Check(readback(), "finish three filled lights through the original D2D drawing path");
    auto lights = Pixels(dc);
    if (!Check(lights.size() == 96 * 48, "read every pixel produced by the owned original D2D target"))
        return;
    for (size_t i = 0; i < circles.size(); ++i) {
        auto rect = circles[i];
        auto center = static_cast<size_t>((rect.top + rect.bottom) / 2) * 96 + (rect.left + rect.right) / 2;
        Check(lights[center] == Rgb(Colors[i]), "original D2D light center has its exact requested color");
        Check(lights[static_cast<size_t>(rect.top) * 96 + rect.left] == Rgb(Background),
              "original D2D rounded-light corner remains background");
        unsigned changed{}, blended{};
        for (int y = rect.top; y < rect.bottom; ++y)
            for (int x = rect.left; x < rect.right; ++x) {
                auto pixel = lights[static_cast<size_t>(y) * 96 + x];
                changed += pixel != Rgb(Background);
                blended += pixel != Rgb(Background) && pixel != Rgb(Colors[i]);
            }
        std::cout << "D2D light=" << i << "; changed=" << changed << "; antialias=" << blended << std::endl;
        Check(changed >= 80 && changed <= 140 && blended != 0, "original D2D light is a filled antialiased circle");
    }
    bool unchanged = true;
    for (LONG y = 0; y < 48; ++y)
        for (LONG x = 0; x < 96; ++x) {
            bool inside{};
            for (auto rect : circles)
                inside |= x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
            if (!inside)
                unchanged &= lights[static_cast<size_t>(y) * 96 + x] == Rgb(Background);
        }
    Check(unchanged, "original D2D light drawing changes no pixels outside its three bounds");

    begin(target);
    fill(target, Background, &Full, nullptr);
    const RECT partial{18, 12, 24, 24};
    pushClip(target, &partial);
    auto clippedState = state();
    Check(EqualRect(&clippedState.clip, &partial),
          "original D2D clip getter reports the pushed partial invalidation bounds");
    fill(target, Colors[0], &circles[0], &radius);
    Check(state().Same(clippedState), "drawing a clipped light does not alter its caller's active graphics state");
    popClip(target);
    Check(state().Same(originalState), "native clip pop restores the exact original graphics state");
    Check(readback(), "complete partial original D2D repaint and balance its owned readback DC");
    auto clipped = Pixels(dc);
    bool clipOnly = clipped.size() == 96 * 48;
    unsigned changed{};
    for (int y = 0; y < 48 && clipOnly; ++y)
        for (int x = 0; x < 96; ++x)
            if (clipped[static_cast<size_t>(y) * 96 + x] != Rgb(Background)) {
                ++changed;
                clipOnly &= x >= partial.left && x < partial.right && y >= partial.top && y < partial.bottom;
            }
    Check(clipOnly && changed > 0, "all changed D2D pixels stay inside the original partial clip");

    begin(target);
    fill(target, Background, &Full, nullptr);
    POINT oldViewport{}, oldWindow{};
    viewport(target, 30, 8, &oldViewport);
    windowOrg(target, 3, 2, &oldWindow);
    auto translatedState = state();
    Check(translatedState.viewport.x == 30 && translatedState.viewport.y == 8 && translatedState.window.x == 3 &&
              translatedState.window.y == 2,
          "native D2D origin getters report the requested local drawing translation");
    const RECT local{4, 4, 16, 16};
    fill(target, Colors[2], &local, &radius);
    Check(state().Same(translatedState), "native light fill preserves an existing translated graphics scope");
    windowOrg(target, oldWindow.x, oldWindow.y, nullptr);
    viewport(target, oldViewport.x, oldViewport.y, nullptr);
    Check(state().Same(originalState), "restore both native D2D coordinate origins without writing private fields");
    Check(readback(), "read back translated original D2D output after restoring caller state");
    auto translated = Pixels(dc);
    bool translatedBounds = translated.size() == 96 * 48;
    for (int y = 0; y < 48 && translatedBounds; ++y)
        for (int x = 0; x < 96; ++x)
            if (translated[static_cast<size_t>(y) * 96 + x] != Rgb(Background))
                translatedBounds &= x >= 31 && x < 43 && y >= 10 && y < 22;
    Check(translatedBounds && translated[16 * 96 + 37] == Rgb(Colors[2]) && translated[10 * 96 + 10] == Rgb(Background),
          "D2D circle pixels follow viewport minus window origin and leave the old location untouched");

    begin(target);
    fill(target, Background, &Full, nullptr);
    pushAlpha(target, &Full, 128);
    Check(alpha(target, 255) == 128, "original scoped-alpha calculation reports half-opacity drawing");
    fill(target, Colors[0], &circles[0], &radius);
    popAlpha(target);
    Check(state().Same(originalState), "native scoped-alpha pop also restores the original clip and opacity");
    fill(target, Colors[2], &circles[1], &radius);
    fill(target, 0x00ffbd2e, &circles[2], &radius);
    Check(readback(), "complete original D2D alpha drawing and balanced pixel readback");
    auto blended = Pixels(dc);
    auto mix = [](unsigned foreground, unsigned background) { return (foreground * 128 + background * 127) / 255; };
    auto within = [](unsigned actual, unsigned expected) { return actual + 2 >= expected && actual <= expected + 2; };
    auto center = blended.size() == 96 * 48 ? blended[18 * 96 + 18] : CLR_INVALID;
    Check(center != CLR_INVALID && within(GetRValue(center), mix(255, 32)) && within(GetGValue(center), mix(95, 37)) &&
              within(GetBValue(center), mix(87, 42)),
          "original scoped D2D alpha blends light pixels over the existing background");
    Check(blended.size() == 96 * 48 && blended[18 * 96 + 40] == Rgb(Colors[2]) &&
              blended[18 * 96 + 62] == Rgb(Background),
          "restored opacity paints the next light opaquely while fully transparent paint leaves background intact");
    begin(target);
    fill(target, Background, &Full, nullptr);
    Check(readback() && Pixels(dc) == background,
          "original D2D repaint restores the exact pre-light background pixels");

    // The exact callback used by the bounded real-window experiment must first
    // survive the original Button/filter dispatcher in this owned process.
    constexpr GUID buttonId{0x70551436, 0x19c4, 0x4f25, {0xbf, 0xc8, 0x32, 0x2d, 0x86, 0xae, 0x1a, 0x9c}};
    constexpr GUID filterId{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
    constexpr BYTE prolog[]{0x55, 0x8b, 0xec, 0x6a, 0xff};
    if (!Check(!memcmp(api.image + 0x94ea0, prolog, sizeof(prolog)) &&
                   !memcmp(api.image + 0x63590, prolog, sizeof(prolog)),
               "guard original Button creation and six-argument native filter dispatch before callback tests"))
        return;
    ComPtr<IUnknown> created, button, filters;
    using CreateButton = HRESULT(__cdecl *)(IUnknown **);
    if (!Check(SUCCEEDED(reinterpret_cast<CreateButton>(api.image + 0x94ea0)(&created)) && created &&
                   SUCCEEDED(created->QueryInterface(buttonId, &button)) && button &&
                   *reinterpret_cast<void ***>(button.Get()) == reinterpret_cast<void **>(api.image + 0x1f4c00) &&
                   SUCCEEDED(button->QueryInterface(filterId, &filters)),
               "create one real owned Button with its original native filter collection"))
        return;
    auto filterTable = *reinterpret_cast<void ***>(filters.Get());
    if (!Check(filterTable == reinterpret_cast<void **>(api.image + 0x1f50fc) &&
                   filterTable[7] == api.image + 0x937d0 && filterTable[8] == api.image + 0x937f0,
               "guard original collection add/remove ABI for the shared drawing callback"))
        return;
    auto add = reinterpret_cast<txobserve::FilterChange>(filterTable[7]);
    auto remove = reinterpret_cast<txobserve::FilterChange>(filterTable[8]);
    ComPtr<txd2d::Filter> callback;
    callback.Attach(new txd2d::Filter(button.Get(), api.image, circles[0], Colors[0], true));
    struct CallbackLease {
        IUnknown *list, *filter;
        txobserve::FilterChange remove;
        ~CallbackLease() {
            remove(list, filter);
        }
    } callbackLease{filters.Get(), callback.Get(), remove};
    auto bindings = txevents::Inspect(button.Get(), api.image);
    std::vector<IUnknown *> original, during, restored;
    Check(callback->Ready() && bindings.complete && txobserve::List(button.Get(), api.image, original),
          "shared D2D callback validates the exact original graphics ABI and snapshots original bindings and filters");
    using Send = HRESULT(__cdecl *)(IUnknown *, txobserve::Arg *, BOOL *, LRESULT *, BOOL);
    auto send = reinterpret_cast<Send>(api.image + 0x63590);
    BOOL lastHandled{};
    auto dispatch = [&](unsigned code = txd2d::PaintEvent) {
        txobserve::Arg arg{code, sizeof(txobserve::Arg), FALSE, target};
        BOOL handled{};
        LRESULT result{};
        auto status = send(button.Get(), &arg, &handled, &result, TRUE);
        lastHandled = handled;
        return SUCCEEDED(status);
    };
    Check(SUCCEEDED(add(filters.Get(), callback.Get())) && callback->References() == 2 &&
              txobserve::List(button.Get(), api.image, during) && during.size() == original.size() + 1 &&
              during.back() == callback.Get(),
          "register shared drawing callback through the original owned Button collection");
    callback->Enable(true);
    begin(target);
    fill(target, Background, &Full, nullptr);
    Check(dispatch() && lastHandled && state().Same(originalState),
          "original Button dispatcher consumes custom D2D paint without changing graphics state");
    Check(readback(), "read back original-dispatch shared callback drawing through the owned bitmap only");
    auto dispatched = Pixels(dc);
    Check(dispatched.size() == 96 * 48 && dispatched[18 * 96 + 18] == Rgb(Colors[0]) &&
              dispatched[12 * 96 + 12] == Rgb(Background),
          "shared real-window callback produces the expected filled D2D light pixels");
    auto drawing = callback->ReadDrawing();
    Check(drawing.paints == 1 && drawing.rejected == 0 && drawing.stateChanges == 0,
          "shared callback accepts native source thread and graphics while preserving clip origins and alpha");
    begin(target);
    fill(target, Background, &Full, nullptr);
    Check(dispatch(0x7fff) && !lastHandled && callback->ReadDrawing().paints == drawing.paints && readback() &&
              Pixels(dc) == background,
          "unrelated original notifications pass through without drawing or consuming");
    begin(target);
    fill(target, Background, &Full, nullptr);
    Check(dispatch(0x39a) && !lastHandled && callback->ReadDrawing().paints == drawing.paints && readback() &&
              Pixels(dc) == background,
          "original background notification is preserved instead of replacing the wrong paint phase");
    begin(target);
    fill(target, Background, &Full, nullptr);
    pushClip(target, &partial);
    auto dispatchState = state();
    Check(dispatch() && state().Same(dispatchState), "shared callback preserves the caller's partial clip scope");
    popClip(target);
    Check(readback() && Pixels(dc) == clipped,
          "shared callback partial repaint matches independently verified original D2D clipping pixels");
    begin(target);
    fill(target, Background, &Full, nullptr);
    const RECT outsideCircle{24, 12, 30, 24};
    pushClip(target, &outsideCircle);
    auto beforeSkip = callback->ReadDrawing().paints;
    Check(dispatch() && lastHandled && callback->ReadDrawing().paints == beforeSkip,
          "content repaint outside the circle still suppresses the original glyph without drawing");
    popClip(target);
    Check(readback() && Pixels(dc) == background,
          "outside-circle content repaint leaves all existing pixels unchanged");
    begin(target);
    fill(target, Background, &Full, nullptr);
    pushAlpha(target, &Full, 128);
    Check(dispatch() && callback->ReadDrawing().before.alpha == 128,
          "shared callback inherits the original renderer's active half-alpha scope");
    popAlpha(target);
    Check(readback() && Pixels(dc)[18 * 96 + 18] == center,
          "shared callback alpha pixels match the independent native half-opacity fill");
    begin(target);
    fill(target, Background, &Full, nullptr);
    callback->RequireOrigin({99, 99});
    Check(dispatch() && !lastHandled && readback() && Pixels(dc) == background && callback->ReadDrawing().rejected == 1,
          "unexpected original graphics coordinate origin fails open with no custom pixels");
    callback->RequireOrigin({0, 0});
    auto paints = callback->ReadDrawing().paints;
    Check(SUCCEEDED(remove(filters.Get(), callback.Get())) && callback->References() == 1 &&
              txobserve::List(button.Get(), api.image, restored) && restored == original,
          "detach shared callback and restore exact original filter identities and order");
    begin(target);
    fill(target, Background, &Full, nullptr);
    Check(dispatch() && callback->ReadDrawing().paints == paints && readback() && Pixels(dc) == background,
          "detached shared callback receives no dispatch and a native repaint removes all light pixels");
    bool repeated = true;
    for (unsigned i = 0; i < 32; ++i) {
        begin(target);
        fill(target, Background, &Full, nullptr);
        repeated &= SUCCEEDED(add(filters.Get(), callback.Get())) && dispatch() && readback() &&
                    SUCCEEDED(remove(filters.Get(), callback.Get())) && callback->References() == 1;
    }
    auto finalBindings = txevents::Inspect(button.Get(), api.image);
    Check(repeated && callback->ReadDrawing().stateChanges == 0 && txobserve::List(button.Get(), api.image, restored) &&
              restored == original && finalBindings.complete && finalBindings.numeric == bindings.numeric &&
              finalBindings.named == bindings.named && finalBindings.listeners == bindings.listeners,
          "32 shared D2D registration dispatch removal cycles preserve original filters and click containers");
}

void LifetimeContract(Api &api, HWND window) {
    CanvasApi canvasApi;
    if (!canvasApi.Initialize()) {
        Check(false, "owned D2D lifetime output library is available");
        return;
    }
    OwnedCanvas canvas{canvasApi, canvasApi.create(96, 48, nullptr)};
    auto dc = canvas.value ? canvasApi.dc(canvas.value) : nullptr;
    if (!Check(dc != nullptr, "create one reusable owned DC for D2D lifetime cycles"))
        return;
    auto cached = api.CachedTargets();
    auto cycle = [&] {
        ComPtr<IUnknown> root;
        root.Attach(api.create(api.service, window, FALSE));
        if (!root || *reinterpret_cast<void ***>(root.Get()) != reinterpret_cast<void **>(api.image + 0x1eaf9c))
            return false;
        using Query = HRESULT(__thiscall *)(void *, GUID, IUnknown **);
        constexpr GUID id{0xabcc9004, 0x06d3, 0x4836, {0xbb, 0x33, 0x12, 0x15, 0xa9, 0xb6, 0xa7, 0xf3}};
        auto rootTable = *reinterpret_cast<void ***>(root.Get());
        ComPtr<IUnknown> bitmap;
        if (FAILED(reinterpret_cast<Query>(rootTable[0x14 / 4])(root.Get(), id, &bitmap)) || !bitmap ||
            *reinterpret_cast<void ***>(bitmap.Get()) != reinterpret_cast<void **>(api.image + 0x1eae94))
            return false;
        auto table = *reinterpret_cast<void ***>(bitmap.Get());
        reinterpret_cast<Begin>(table[0x1c / 4])(bitmap.Get());
        reinterpret_cast<SolidFill>(table[0x58 / 4])(bitmap.Get(), Background, &Full, nullptr);
        const RECT rect{12, 12, 24, 24};
        const SIZE radius{6, 6};
        reinterpret_cast<SolidFill>(table[0x58 / 4])(bitmap.Get(), Colors[0], &rect, &radius);
        RECT dirty = Full;
        if (!reinterpret_cast<End>(table[0x20 / 4])(bitmap.Get(), &dirty, dc, 255))
            return false;
        GdiFlush();
        auto surfaceObject = *reinterpret_cast<IUnknown **>(reinterpret_cast<BYTE *>(bitmap.Get()) + 0x94);
        ComPtr<IDXGISurface1> surface;
        if (!surfaceObject || FAILED(surfaceObject->QueryInterface(IID_PPV_ARGS(&surface))) ||
            FAILED(surface->ReleaseDC(nullptr)))
            return false;
        return GetPixel(dc, 18, 18) == Rgb(Colors[0]);
    };
    bool warmed = true;
    for (unsigned i = 0; i < 4; ++i)
        warmed &= cycle();
    if (!Check(warmed && api.CachedTargets() == cached,
               "warm up complete original target bitmap draw readback release cycles"))
        return;
    auto process = GetCurrentProcess();
    auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
    bool passed = true;
    for (unsigned i = 0; i < 32; ++i)
        passed &= cycle() && api.CachedTargets() == cached;
    GdiFlush();
    Check(passed, "32 original D2D target create draw readback release cycles restore the factory cache every time");
    auto afterGdi = GetGuiResources(process, GR_GDIOBJECTS), afterUser = GetGuiResources(process, GR_USEROBJECTS);
    std::cout << "D2D lifetime GDI=" << gdi << "->" << afterGdi << "; USER=" << user << "->" << afterUser << std::endl;
    Check(gdi == afterGdi && user == afterUser,
          "repeated original D2D object destruction leaks no owned GDI or USER objects");
}
void Contract(HMODULE gf) {
    Api api;
    if (!Check(api.Initialize(gf), "original render service getter and native service ABI match the guarded profile"))
        return;
    auto before = api.CachedTargets();
    if (!Check(before != UINT_MAX, "read the original factory target-cache count after native initialization"))
        return;
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"OwnedTxGuiD2DContract";
    if (!Check(RegisterClassW(&cls) != 0, "register a class used only by this owned hidden-window fixture"))
        return;
    OwnedWindow window{CreateWindowExW(0, cls.lpszClassName, L"Owned original-render-target contract", WS_POPUP, 0, 0,
                                       96, 48, nullptr, nullptr, cls.hInstance, nullptr)};
    if (!Check(window.value && !IsWindowVisible(window.value), "create only a hidden owned 96x48 target HWND"))
        return;
    ComPtr<IUnknown> target;
    target.Attach(api.create(api.service, window.value, FALSE));
    if (!Check(target != nullptr, "create the render target through the original service and factory"))
        return;
    auto table = *reinterpret_cast<void ***>(target.Get());
    if (!Check(table == reinterpret_cast<void **>(api.image + 0x1eaf9c) && table[0] == api.image + 0x1afe0 &&
                   table[1] == api.image + 0x16a60 && table[2] == api.image + 0x1b100,
               "factory returns the same original D2DRenderTarget vtable observed on the real caption buttons"))
        return;
    auto kind = reinterpret_cast<Api::Kind>(table[3])(target.Get());
    Check(kind == 0x10, "original render target reports its D2D target kind");
    Check(api.CachedTargets() == before + 1, "original factory cache contains exactly the owned target");
    auto retained = target->AddRef();
    auto remaining = target->Release();
    Check(retained == 2 && remaining == 1, "original creation transfers one owned COM reference");
    PixelContract(api, target.Get());
    target.Reset();
    Check(api.CachedTargets() == before,
          "final native Release removes the owned target from the original factory cache");
    Check(IsWindow(window.value) && !IsWindowVisible(window.value),
          "native target release does not destroy or show the owned HWND");
    LifetimeContract(api, window.value);
    Check(window.Destroy(), "explicitly destroy the fixture-owned hidden HWND after all native targets are released");
    Check(UnregisterClassW(cls.lpszClassName, cls.hInstance) != FALSE,
          "unregister the owned window class after native resource cleanup");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    static_assert(sizeof(void *) == 4, "This guarded original renderer ABI is x86 only");
    if (argc != 2)
        return ERROR_INVALID_PARAMETER;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return static_cast<int>(initialized);
    auto path = std::filesystem::path(argv[1]).make_preferred();
    auto gf =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
    if (!Check(gf && styler::MatchesPdb(gf, identity, 24),
               "original GF image matches the independently verified CodeView profile"))
        return ERROR_REVISION_MISMATCH;
    auto initialize = reinterpret_cast<void(__cdecl *)()>(GetProcAddress(gf, "?InitDPI@DPI@GF@Util@@YAXXZ"));
    if (!initialize)
        return ERROR_PROC_NOT_FOUND;
    initialize();
    Contract(gf);
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    CoUninitialize();
    // Native renderer factory/device/TLS lifetime ends with this owned process.
    return failures ? ERROR_INVALID_DATA : ERROR_SUCCESS;
}
