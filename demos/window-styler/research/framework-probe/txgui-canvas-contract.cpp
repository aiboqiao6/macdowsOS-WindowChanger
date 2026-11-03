#include "../../pe-identity.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

// Standalone, owned-process ABI contract. No target HWND, process attachment,
// patched vtable, screen DC, or application callback is used here.
namespace {
using Canvas = void *;
unsigned checks{}, failures{};
bool Check(bool value, const char *message) {
    ++checks;
    failures += !value;
    std::cout << (value ? "PASS " : "FAIL ") << message << std::endl;
    return value;
}
int WindowCount() {
    int count{};
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            DWORD process{};
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId())
                ++*reinterpret_cast<int *>(context);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
    return count;
}
struct Api {
    Canvas(__cdecl *create)(int, int, Canvas) {};
    void(__cdecl *destroy)(Canvas){};
    HDC(__cdecl *dc)(Canvas) {};
    BOOL(__cdecl *size)(Canvas, SIZE *) {};
    BOOL(__cdecl *fill)(Canvas, DWORD, const RECT *) {};
    BOOL(__cdecl *stroke)(Canvas, const RECT *, const SIZE *, DWORD, int) {};
    BOOL(__cdecl *circle)(Canvas, const RECT *, const SIZE *, DWORD) {};
    BOOL(__cdecl *getClip)(Canvas, RECT *) {};
    BOOL(__cdecl *setClip)(Canvas, const RECT *) {};
    BOOL(__cdecl *intersectClip)(Canvas, const RECT *) {};
    BOOL(__cdecl *viewport)(Canvas, int, int, POINT *) {};
    BOOL(__cdecl *windowOrg)(Canvas, int, int, POINT *) {};
    BOOL(__cdecl *getViewport)(Canvas, POINT *) {};
    BOOL(__cdecl *getWindowOrg)(Canvas, POINT *) {};
    BOOL(__cdecl *getFilter)(Canvas, DWORD *) {};
    Canvas(__cdecl *gfCreate)(int, int, Canvas) {};
    BOOL(__cdecl *gfCircle)(Canvas, const RECT *, const SIZE *, DWORD) {};
    BOOL(__cdecl *gfViewport)(Canvas, int, int, POINT *) {};

    template <class T> bool Bind(T &function, HMODULE module, const char *name, size_t rva) {
        auto address = GetProcAddress(module, name);
        if (!address || reinterpret_cast<BYTE *>(address) != reinterpret_cast<BYTE *>(module) + rva)
            return false;
        function = reinterpret_cast<T>(address);
        return true;
    }
    bool Load(HMODULE gf, HMODULE graphic) {
        return Bind(create, graphic, "CreateCanvas", 0x72d20) && Bind(destroy, graphic, "DeleteCanvas", 0x72e10) &&
               Bind(dc, graphic, "GetCanvasHandle", 0x72e30) && Bind(size, graphic, "GetCanvasSize", 0x72e50) &&
               Bind(fill, graphic, "FillSolidRect", 0x6dac0) && Bind(stroke, graphic, "GeoRoundRectEx", 0x6e3e0) &&
               Bind(circle, graphic, "FillRoundRectEx", 0x6e5c0) &&
               Bind(getClip, graphic, "GetCanvasClipRect", 0x73310) &&
               Bind(setClip, graphic, "SetCanvasClipRect", 0x73140) &&
               Bind(intersectClip, graphic, "IntersectCanvasClipRect", 0x73250) &&
               Bind(viewport, graphic, "SetViewportOrg", 0x72eb0) &&
               Bind(windowOrg, graphic, "SetWindowOrg", 0x72f40) &&
               Bind(getViewport, graphic, "GetViewportOrg", 0x72f10) &&
               Bind(getWindowOrg, graphic, "GetWindowOrg", 0x72fa0) &&
               Bind(getFilter, graphic, "GetCanvasFilter", 0x73090) &&
               Bind(gfCreate, gf, "?CreateCanvas@DPI@GF@Util@@YAPAUHGCANVAS__@@HHPAU4@@Z", 0x62890) &&
               Bind(gfCircle, gf, "?GeoRoundRectEx@DPI@GF@Util@@YAHPAUHGCANVAS__@@PBUtagRECT@@PBUtagSIZE@@TtagARGB@@@Z",
                    0x640e0) &&
               Bind(gfViewport, gf, "?SetViewportOrg@DPI@GF@Util@@YAHPAUHGCANVAS__@@HHPAUtagPOINT@@@Z", 0x66bd0);
    }
};
struct OwnedCanvas {
    Api &api;
    Canvas value{};
    OwnedCanvas(Api &functions, Canvas canvas) : api(functions), value(canvas) {}
    ~OwnedCanvas() {
        if (value)
            api.destroy(value);
    }
    OwnedCanvas(const OwnedCanvas &) = delete;
    OwnedCanvas &operator=(const OwnedCanvas &) = delete;
};
constexpr DWORD Background = 0xff20252a;
constexpr std::array<DWORD, 3> Colors{0xffff5f57, 0xffffbd2e, 0xff28c840};
constexpr RECT Bounds{0, 0, 96, 48};
COLORREF Rgb(DWORD argb) {
    return RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
}
std::vector<COLORREF> Pixels(Api &api, Canvas canvas, int width = 96, int height = 48) {
    auto dc = api.dc(canvas);
    std::vector<COLORREF> result;
    if (!dc)
        return result;
    result.reserve(static_cast<size_t>(width) * height);
    // All callers restore the original native origin and clip before sampling.
    GdiFlush();
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            result.push_back(GetPixel(dc, x, y));
    return result;
}
bool Uniform(const std::vector<COLORREF> &pixels, COLORREF color) {
    return pixels.size() == 96 * 48 &&
           std::all_of(pixels.begin(), pixels.end(), [color](COLORREF actual) { return actual == color; });
}
bool Draw(Api &api, Canvas canvas, const RECT &rect, DWORD color) {
    SIZE radius{rect.right - rect.left, rect.bottom - rect.top};
    return api.circle(canvas, &rect, &radius, color) != FALSE;
}
void CanvasContract(Api &api) {
    OwnedCanvas canvas(api, api.create(96, 48, nullptr));
    if (!Check(canvas.value != nullptr, "create an owned original memory canvas"))
        return;
    SIZE size{};
    Check(api.size(canvas.value, &size) && size.cx == 96 && size.cy == 48, "original canvas size is physical pixels");
    Check(api.dc(canvas.value) != nullptr, "obtain only the owned canvas DC for pixel readback");
    RECT originalClip{};
    Check(api.getClip(canvas.value, &originalClip) && EqualRect(&originalClip, &Bounds),
          "new canvas starts with full clipping bounds");
    Check(api.fill(canvas.value, Background, &Bounds) != FALSE, "fill background through the original renderer");
    Check(Uniform(Pixels(api, canvas.value), Rgb(Background)), "read back every opaque background pixel");
    std::array<RECT, 3> circles{RECT{12, 12, 24, 24}, RECT{34, 12, 46, 24}, RECT{56, 12, 68, 24}};
    for (size_t i = 0; i < circles.size(); ++i)
        Check(Draw(api, canvas.value, circles[i], Colors[i]), "fill a light through original FillRoundRectEx");
    auto pixels = Pixels(api, canvas.value);
    if (!Check(pixels.size() == 96 * 48, "original canvas pixels are readable"))
        return;
    for (size_t i = 0; i < circles.size(); ++i) {
        auto rect = circles[i];
        auto center = static_cast<size_t>((rect.top + rect.bottom) / 2) * 96 + (rect.left + rect.right) / 2;
        Check(pixels[center] == Rgb(Colors[i]), "light center has the exact requested red yellow or green color");
        Check(pixels[static_cast<size_t>(rect.top) * 96 + rect.left] == Rgb(Background),
              "rounded light leaves its bounding-box corner unpainted");
        unsigned changed{}, blended{};
        for (LONG y = rect.top; y < rect.bottom; ++y)
            for (LONG x = rect.left; x < rect.right; ++x) {
                auto pixel = pixels[static_cast<size_t>(y) * 96 + x];
                changed += pixel != Rgb(Background);
                blended += pixel != Rgb(Background) && pixel != Rgb(Colors[i]);
            }
        std::cout << "light=" << i << "; changed=" << changed << "; antialias=" << blended << std::endl;
        Check(changed >= 80 && changed <= 140 && blended != 0, "light is a filled antialiased circle, not a square");
    }
    bool outside = true;
    for (LONG y = 0; y < 48; ++y)
        for (LONG x = 0; x < 96; ++x) {
            bool inside{};
            for (auto rect : circles)
                inside |= x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
            if (!inside)
                outside &= pixels[static_cast<size_t>(y) * 96 + x] == Rgb(Background);
        }
    Check(outside, "all pixels outside the three light bounds remain unchanged");
    Check(api.fill(canvas.value, Background, &Bounds) && Uniform(Pixels(api, canvas.value), Rgb(Background)),
          "repainting the original background removes every test light");

    RECT clip{18, 12, 24, 24}, tighter{18, 14, 23, 23}, actual{};
    Check(api.setClip(canvas.value, &clip) && api.intersectClip(canvas.value, &tighter),
          "set and intersect a native partial invalidation clip");
    Check(api.getClip(canvas.value, &actual) && EqualRect(&actual, &tighter),
          "original renderer reports the exact intersected clip");
    Check(Draw(api, canvas.value, circles[0], Colors[0]), "draw a circle under the original clip");
    Check(api.setClip(canvas.value, &originalClip) && api.getClip(canvas.value, &actual) &&
              EqualRect(&actual, &originalClip),
          "restore the exact native canvas clip before readback");
    pixels = Pixels(api, canvas.value);
    bool clipped = pixels.size() == 96 * 48;
    unsigned changed{};
    for (LONG y = 0; y < 48 && clipped; ++y)
        for (LONG x = 0; x < 96; ++x) {
            if (pixels[static_cast<size_t>(y) * 96 + x] != Rgb(Background)) {
                ++changed;
                clipped &= x >= tighter.left && x < tighter.right && y >= tighter.top && y < tighter.bottom;
            }
        }
    Check(clipped && changed > 0, "partial redraw changes pixels only inside the native clip");

    api.fill(canvas.value, Background, &Bounds);
    POINT viewport{}, window{};
    Check(api.viewport(canvas.value, 30, 8, &viewport) && api.windowOrg(canvas.value, 3, 2, &window),
          "set original viewport and window origins");
    RECT local{4, 4, 16, 16};
    Check(Draw(api, canvas.value, local, Colors[2]), "draw a local light in a translated original canvas");
    Check(api.windowOrg(canvas.value, window.x, window.y, nullptr) &&
              api.viewport(canvas.value, viewport.x, viewport.y, nullptr),
          "restore both native coordinate origins");
    pixels = Pixels(api, canvas.value);
    Check(pixels.size() == 96 * 48 && pixels[16 * 96 + 37] == Rgb(Colors[2]) && pixels[10 * 96 + 10] == Rgb(Background),
          "original painting honors viewport minus window origin without painting old coordinates");
    Check(api.fill(canvas.value, Background, &Bounds) && Uniform(Pixels(api, canvas.value), Rgb(Background)),
          "translated painting leaves no pixels after exact state rollback and repaint");

    DWORD filterBefore{}, filterAfter{};
    POINT viewportBefore{}, windowBefore{}, viewportAfter{}, windowAfter{};
    RECT clipBefore{}, clipAfter{};
    Check(api.getFilter(canvas.value, &filterBefore) && api.getClip(canvas.value, &clipBefore) &&
              api.getViewport(canvas.value, &viewportBefore) && api.getWindowOrg(canvas.value, &windowBefore),
          "save native graphics state before alpha painting");
    Check(Draw(api, canvas.value, circles[0], 0x80ff5f57), "paint a translucent light through the original renderer");
    pixels = Pixels(api, canvas.value);
    auto center = pixels.size() == 96 * 48 ? pixels[18 * 96 + 18] : CLR_INVALID;
    auto withinColorRange = [](unsigned actual, unsigned expected) {
        return actual + 2 >= expected && actual <= expected + 2;
    };
    auto mixed = [](unsigned foreground, unsigned background) { return (foreground * 128 + background * 127) / 255; };
    Check(center != CLR_INVALID && withinColorRange(GetRValue(center), mixed(255, 32)) &&
              withinColorRange(GetGValue(center), mixed(95, 37)) && withinColorRange(GetBValue(center), mixed(87, 42)),
          "translucent original drawing blends over existing pixels instead of replacing the background");
    Check(api.getFilter(canvas.value, &filterAfter) && api.getClip(canvas.value, &clipAfter) &&
              api.getViewport(canvas.value, &viewportAfter) && api.getWindowOrg(canvas.value, &windowAfter) &&
              filterAfter == filterBefore && EqualRect(&clipBefore, &clipAfter) &&
              viewportBefore.x == viewportAfter.x && viewportBefore.y == viewportAfter.y &&
              windowBefore.x == windowAfter.x && windowBefore.y == windowAfter.y,
          "original light drawing preserves the canvas filter clip and both origins");
    auto beforeTransparent = Pixels(api, canvas.value);
    Check(Draw(api, canvas.value, circles[1], 0x00ffbd2e) && Pixels(api, canvas.value) == beforeTransparent,
          "a fully transparent light does not alter existing canvas pixels");
    Check(api.fill(canvas.value, Background, &Bounds) && Uniform(Pixels(api, canvas.value), Rgb(Background)),
          "opaque background repaint removes alpha and transparent test state");
}
void GfWrapperContract(Api &api) {
    OwnedCanvas canvas(api, api.gfCreate(96, 48, nullptr));
    if (!Check(canvas.value != nullptr, "create a canvas through the original GF DPI wrapper"))
        return;
    SIZE size{};
    if (!Check(api.size(canvas.value, &size) && size.cx >= 96 && size.cx <= 384 && size.cy * 2 == size.cx,
               "GF wrapper reports a bounded consistent DPI scale"))
        return;
    std::cout << "gfCanvas=" << size.cx << 'x' << size.cy << std::endl;
    RECT full{0, 0, size.cx, size.cy}, light{12, 12, 24, 24};
    SIZE radius{12, 12};
    api.dc(canvas.value);
    Check(api.fill(canvas.value, Background, &full) && api.gfCircle(canvas.value, &light, &radius, Colors[0]),
          "GF DPI wrapper paints using its own original drawing interface");
    POINT original{};
    Check(api.gfViewport(canvas.value, 8, 4, &original) &&
              api.gfViewport(canvas.value, original.x, original.y, nullptr),
          "GF DPI wrapper can round-trip its original viewport");
    auto pixels = Pixels(api, canvas.value, size.cx, size.cy);
    auto x = 18 * size.cx / 96, y = 18 * size.cy / 48;
    unsigned changed{};
    for (auto color : pixels)
        changed += color != Rgb(Background);
    Check(pixels.size() == static_cast<size_t>(size.cx) * size.cy && changed > 0 &&
              pixels[static_cast<size_t>(y) * size.cx + x] == Rgb(Background) && pixels.front() == Rgb(Background),
          "GF GeoRoundRectEx is an outline with an untouched center, not a filled-light API");
}
void LifetimeContract(Api &api) {
    // Warm up lazy renderer/DC allocation before comparing resource counts.
    for (unsigned i = 0; i < 4; ++i) {
        OwnedCanvas canvas(api, api.create(96, 48, nullptr));
        if (canvas.value) {
            api.dc(canvas.value);
            Draw(api, canvas.value, RECT{4, 4, 16, 16}, Colors[0]);
        }
    }
    auto process = GetCurrentProcess();
    auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
    auto windows = WindowCount();
    bool success = true;
    for (unsigned i = 0; i < 128; ++i) {
        OwnedCanvas canvas(api, api.create(96, 48, nullptr));
        if (!canvas.value) {
            success = false;
            break;
        }
        success &= api.dc(canvas.value) != nullptr && api.fill(canvas.value, Background, &Bounds) &&
                   Draw(api, canvas.value, RECT{4, 4, 16, 16}, Colors[i % 3]);
    }
    GdiFlush();
    Check(success, "128 owned canvas create draw destroy cycles succeed");
    auto afterGdi = GetGuiResources(process, GR_GDIOBJECTS), afterUser = GetGuiResources(process, GR_USEROBJECTS);
    std::cout << "gdi=" << gdi << "->" << afterGdi << "; user=" << user << "->" << afterUser << std::endl;
    Check(afterGdi == gdi && afterUser == user, "repeated native canvas destruction leaks no GDI or USER objects");
    Check(WindowCount() == windows, "canvas painting and cleanup create no top-level HWNDs");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    static_assert(sizeof(void *) == 4, "This original graphics ABI is x86 only");
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
    auto graphic = GetModuleHandleW(L"xGraphic32.dll");
    constexpr GUID gfIdentity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
    constexpr GUID graphicIdentity{0x0fd7d531, 0xc786, 0x4d0b, {0xb0, 0xfb, 0x1e, 0x97, 0x99, 0x6f, 0xdf, 0xbc}};
    if (!Check(gf && graphic && styler::MatchesPdb(gf, gfIdentity, 24) &&
                   styler::MatchesPdb(graphic, graphicIdentity, 3),
               "both original framework and graphics engine match their guarded CodeView identities"))
        return ERROR_REVISION_MISMATCH;
    Check(!styler::MatchesPdb(gf, gfIdentity, 23) && !styler::MatchesPdb(graphic, graphicIdentity, 4),
          "unsupported framework and graphics revisions are rejected before ABI calls");
    Api api;
    if (!Check(api.Load(gf, graphic), "all original graphics exports match independently inspected entry RVAs"))
        return ERROR_PROC_NOT_FOUND;
    auto initialize = reinterpret_cast<void(__cdecl *)()>(GetProcAddress(gf, "?InitDPI@DPI@GF@Util@@YAXXZ"));
    if (!initialize)
        return ERROR_PROC_NOT_FOUND;
    initialize();
    auto windows = WindowCount();
    CanvasContract(api);
    GfWrapperContract(api);
    LifetimeContract(api);
    Check(WindowCount() == windows, "entire original-renderer contract leaves the owned HWND count unchanged");
    std::cout << "checks=" << checks << "; failures=" << failures << std::endl;
    CoUninitialize();
    // GF can retain TLS callbacks; process teardown owns module unloading.
    return failures ? ERROR_INVALID_DATA : ERROR_SUCCESS;
}
