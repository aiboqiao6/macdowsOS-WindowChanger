#include "adapter-api.h"
#include "pe-identity.h"
#include "embedded-frame.h"
#include <commctrl.h>
#include <windowsx.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>
#include <fstream>
#include <string>
#include "third_party/minhook/include/MinHook.h"

// ApplicationFrame framework backend. Reuses the original caption surfaces,
// layout, hit rectangles, input dispatch, and accessibility providers. Native
// reserved-metrics notifications coordinate with the embedded application.
namespace {
constexpr UINT_PTR SubclassId = 0x4d434146;
BYTE *module{};
using LayoutFn = void (*)(void *);
using PaintFn = HRESULT (*)(void *, IDCompositionSurface *, int, int, const RECT &, const RECT &);
LayoutFn originalLayout{};
PaintFn originalPaint{};
bool hooked{}, hooksEnabled{};
std::mutex mutex;
struct State {
    HWND target{}, owner{}, title{}, core{};
    std::vector<HWND> inputs;
    void *object{};
    DWORD thread{};
    HANDLE process{};
    UINT_PTR timer{};
    bool changing{};
    unsigned paints{}, mask{}, commands{};
    int pressed = -1;
    HWND capture{};
    HRESULT paintError = S_OK;
};
std::map<void *, State *> states;
void Debug(const char *stage, void *object = nullptr, HRESULT error = S_OK) {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Debug), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L'\\') + 1);
    if (GetFileAttributesW((directory + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::ofstream log(directory + L"applicationframe-" + std::to_wstring(GetCurrentProcessId()) + L".txt",
                      std::ios::app);
    log << stage << " object=" << object << " hr=" << std::hex << error << "\n";
}
template <class T> T Fn(size_t rva) {
    return reinterpret_cast<T>(module + rva);
}
bool Readable(const void *pointer, size_t bytes) {
    MEMORY_BASIC_INFORMATION info{};
    return pointer && VirtualQuery(pointer, &info, sizeof(info)) && info.State == MEM_COMMIT &&
           !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           uintptr_t(pointer) + bytes <= uintptr_t(info.BaseAddress) + info.RegionSize;
}
RECT &Rect(void *object, size_t offset) {
    return *reinterpret_cast<RECT *>(static_cast<BYTE *>(object) + offset);
}
bool Runtime() {
    auto handle = GetModuleHandleW(L"ApplicationFrame.dll");
    constexpr GUID id{0x364a364e, 0x553d, 0xe50f, {0x96, 0xea, 0x7a, 0x71, 0xce, 0xa2, 0xe6, 0x76}};
    if (!styler::MatchesPdb(handle, id))
        return false;
    module = reinterpret_cast<BYTE *>(handle);
    constexpr BYTE layout[]{0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20};
    constexpr BYTE paint[]{0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57,
                           0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    constexpr BYTE place[]{0x40, 0x53, 0x55, 0x48, 0x81, 0xec, 0xb8, 0x01, 0x00, 0x00};
    constexpr BYTE frame[]{0x48, 0x8b, 0x81, 0xc0, 0x01, 0x00, 0x00, 0xc3};
    constexpr BYTE invoke[]{0x48, 0x83, 0xec, 0x28, 0x83, 0xfa, 0x08, 0x7f, 0x4e};
    return (hooked || (memcmp(module + 0x24b40, layout, sizeof(layout)) == 0 &&
                       memcmp(module + 0x22210, paint, sizeof(paint)) == 0)) &&
           memcmp(module + 0x247d0, place, sizeof(place)) == 0 && memcmp(module + 0x42350, frame, sizeof(frame)) == 0 &&
           memcmp(module + 0x5ca90, invoke, sizeof(invoke)) == 0;
}
struct Match {
    HWND title{};
    void *object{};
    unsigned count{};
};
Match Find(HWND target) {
    Match result;
    if (!Runtime()) {
        Debug("runtime declined");
        return result;
    }
    wchar_t name[128]{};
    GetClassNameW(target, name, 128);
    if (wcscmp(name, L"ApplicationFrameWindow") != 0)
        return result;
    EnumChildWindows(
        target,
        [](HWND w, LPARAM p) -> BOOL {
            auto &match = *reinterpret_cast<Match *>(p);
            wchar_t name[128]{};
            GetClassNameW(w, name, 128);
            if (wcscmp(name, L"ApplicationFrameTitleBarWindow") != 0 || !IsWindowVisible(w) ||
                GetWindowThreadProcessId(w, nullptr) != GetCurrentThreadId() || GetClassLongPtrW(w, GCL_CBWNDEXTRA) < 8)
                return TRUE;
            auto object = reinterpret_cast<void *>(GetWindowLongPtrW(w, 0));
            if (!Readable(object, 0x630) ||
                *reinterpret_cast<void ***>(object) != reinterpret_cast<void **>(module + 0x76a40))
                return TRUE;
            if (Fn<HWND (*)(void *)>(0x42350)(static_cast<BYTE *>(object) + 0x38) != GetAncestor(w, GA_ROOT))
                return TRUE;
            auto row = Rect(object, 0x354);
            if (row.right - row.left < 300 || row.bottom - row.top < 20 || row.bottom - row.top > 100)
                return TRUE;
            // The original caption must have close, minimize and a maximize/restore
            // slot; fullscreen/overlay-only and incomplete trees decline this backend.
            if (IsRectEmpty(&Rect(object, 0x454)) || IsRectEmpty(&Rect(object, 0x3f4)) ||
                (IsRectEmpty(&Rect(object, 0x474)) && IsRectEmpty(&Rect(object, 0x414)) &&
                 IsRectEmpty(&Rect(object, 0x434))))
                return TRUE;
            match.title = w;
            match.object = object;
            ++match.count;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    if (result.count != 1) {
        Debug("title declined", result.object, HRESULT(result.count));
        return {};
    }
    Debug("title found", result.object);
    return result;
}
State *Active(void *object) {
    std::scoped_lock lock(mutex);
    auto i = states.find(object);
    return i != states.end() && i->second->thread == GetCurrentThreadId() ? i->second : nullptr;
}
int Scale(State *s, int value) {
    return MulDiv(value, GetDpiForWindow(s->target), 96);
}
void LayoutHook(void *object) {
    originalLayout(object);
    auto s = Active(object);
    if (!s)
        return;
    const auto row = Rect(object, 0x354);
    const int unit = Scale(s, 20), left = Scale(s, 8), reserve = Scale(s, 84);
    auto move = [&](size_t outer, int index) {
        auto &r = Rect(object, outer);
        if (IsRectEmpty(&r))
            return;
        auto &glyph = Rect(object, outer - 16);
        int gw = glyph.right - glyph.left, gh = glyph.bottom - glyph.top;
        r.left = left + index * unit;
        r.right = r.left + unit;
        glyph.left = (r.left + r.right - gw) / 2;
        glyph.right = glyph.left + gw;
        glyph.top = (r.top + r.bottom - gh) / 2;
        glyph.bottom = glyph.top + gh;
    };
    move(0x454, 0);
    move(0x3f4, 1);
    move(0x474, 2);
    move(0x414, 2);
    move(0x434, 2);
    // The framework communicates left/right reserved widths to CoreApplication.
    // Keep its height and use its own scale, hit-test and notification paths.
    Rect(object, 0x4d4) = {row.left, row.top, row.left + reserve, row.bottom};
    Rect(object, 0x4c4) = {row.right, row.top, row.right, row.bottom};
    if (s->core) {
        SetPropW(s->core, styler::EmbeddedCaptionTopProperty, reinterpret_cast<HANDLE>(INT_PTR(row.top + 1)));
        SetPropW(s->core, styler::EmbeddedCaptionHeightProperty,
                 reinterpret_cast<HANDLE>(INT_PTR(row.bottom - row.top + 1)));
    }
}
bool Enabled(State *s, int button) {
    auto style = GetWindowLongPtrW(s->target, GWL_STYLE);
    if (!IsWindowEnabled(s->target))
        return false;
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(s->target, FALSE);
    auto flags = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
    return flags == UINT(-1) || !(flags & (MF_DISABLED | MF_GRAYED));
}
HRESULT PaintHook(void *object, IDCompositionSurface *surface, int control, int bitmap, const RECT &bounds,
                  const RECT &glyph) {
    auto s = Active(object);
    int button = control == 11 ? 0 : control == 7 ? 1 : (control == 8 || control == 9 || control == 10) ? 2 : -1;
    if (!s || button < 0)
        return originalPaint(object, surface, control, bitmap, bounds, glyph);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width < 1 || height < 1 || width > 512 || height > 256)
        return originalPaint(object, surface, control, bitmap, bounds, glyph);
    Microsoft::WRL::ComPtr<IDXGISurface1> dxgi;
    POINT offset{};
    RECT update{0, 0, width, height};
    HRESULT hr =
        surface->BeginDraw(&update, __uuidof(IDXGISurface1), reinterpret_cast<void **>(dxgi.GetAddressOf()), &offset);
    if (FAILED(hr)) {
        s->paintError = hr;
        return originalPaint(object, surface, control, bitmap, bounds, glyph);
    }
    HDC dc{};
    hr = dxgi->GetDC(FALSE, &dc);
    if (SUCCEEDED(hr)) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        std::vector<DWORD> pixels(size_t(width) * height);
        constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
        auto color = Enabled(s, button) ? colors[button] : RGB(128, 128, 128);
        double radius = Scale(s, 12) / 2.0, cx = width / 2.0, cy = height / 2.0;
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                int covered = 0;
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        double dx = x + (sx + .5) / 4 - cx, dy = y + (sy + .5) / 4 - cy;
                        if (dx * dx + dy * dy <= radius * radius)
                            ++covered;
                    }
                DWORD alpha = (covered * 255 + 8) / 16;
                pixels[size_t(y) * width + x] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
                                                ((GetGValue(color) * alpha / 255) << 8) |
                                                (GetBValue(color) * alpha / 255);
            }
        if (!SetDIBitsToDevice(dc, offset.x, offset.y, width, height, 0, 0, 0, height, pixels.data(), &info,
                               DIB_RGB_COLORS))
            hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
        HRESULT release = dxgi->ReleaseDC(nullptr);
        if (SUCCEEDED(hr))
            hr = release;
    }
    HRESULT end = surface->EndDraw();
    if (SUCCEEDED(hr))
        hr = end;
    if (SUCCEEDED(hr)) {
        ++s->paints;
        s->mask |= 1u << button;
    } else
        s->paintError = hr;
    return hr;
}
bool Hooks() {
    std::scoped_lock lock(mutex);
    if (hooked) {
        if (hooksEnabled)
            return true;
        MH_QueueEnableHook(module + 0x24b40);
        MH_QueueEnableHook(module + 0x22210);
        hooksEnabled = MH_ApplyQueued() == MH_OK;
        return hooksEnabled;
    }
    auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    if (MH_CreateHook(module + 0x24b40, reinterpret_cast<void *>(&LayoutHook),
                      reinterpret_cast<void **>(&originalLayout)) != MH_OK)
        return false;
    if (MH_CreateHook(module + 0x22210, reinterpret_cast<void *>(&PaintHook),
                      reinterpret_cast<void **>(&originalPaint)) != MH_OK) {
        MH_RemoveHook(module + 0x24b40);
        return false;
    }
    MH_QueueEnableHook(module + 0x24b40);
    MH_QueueEnableHook(module + 0x22210);
    if (MH_ApplyQueued() != MH_OK) {
        MH_DisableHook(module + 0x24b40);
        MH_DisableHook(module + 0x22210);
        MH_RemoveHook(module + 0x24b40);
        MH_RemoveHook(module + 0x22210);
        return false;
    }
    hooked = true;
    hooksEnabled = true;
    return true;
}
void Reflow(State *s) {
    Fn<HRESULT (*)(void *, const RECT *, bool)>(0x247d0)(s->object, nullptr, true);
    for (int control : {7, 8, 9, 10, 11})
        Fn<HRESULT (*)(void *, int, bool)>(0x19a20)(s->object, control, true);
    Fn<HRESULT (*)(void *, void *)>(0x1a850)(s->object, nullptr);
}
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
UINT InputsChanged() {
    static UINT message = RegisterWindowMessageW(L"Macdows.WindowStyler.ApplicationFrame.InputsChanged.v11");
    return message;
}
bool BindInputs(State *s) {
    struct Search {
        State *state;
        bool ok = true;
    } search{s};
    EnumChildWindows(
        s->target,
        [](HWND w, LPARAM p) -> BOOL {
            auto &result = *reinterpret_cast<Search *>(p);
            auto s = result.state;
            if (!styler::WindowClass(w, L"ApplicationFrameTitleBarWindow") ||
                GetWindowThreadProcessId(w, nullptr) != s->thread || GetClassLongPtrW(w, GCL_CBWNDEXTRA) < 8 ||
                reinterpret_cast<void *>(GetWindowLongPtrW(w, 0)) != s->object)
                return TRUE;
            if (std::find(s->inputs.begin(), s->inputs.end(), w) == s->inputs.end()) {
                if (SetWindowSubclass(w, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)))
                    s->inputs.push_back(w);
                else
                    result.ok = false;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.ok && !s->inputs.empty();
}
void Release(State *s, bool destroyed = false) {
    s->changing = true;
    KillTimer(s->target, s->timer);
    RemoveWindowSubclass(s->target, Subclass, SubclassId);
    for (auto input : s->inputs)
        RemoveWindowSubclass(input, Subclass, SubclassId);
    if (s->capture && GetCapture() == s->capture)
        ReleaseCapture();
    {
        std::scoped_lock lock(mutex);
        states.erase(s->object);
    }
    if (!destroyed)
        Reflow(s);
    if (s->core) {
        RemovePropW(s->core, styler::EmbeddedCaptionTopProperty);
        RemovePropW(s->core, styler::EmbeddedCaptionHeightProperty);
    }
    // Restore executable bytes on the last detach. Keep trampoline allocations
    // alive because an unrelated framework thread may still be returning through
    // a pass-through hook. A later content-addressed revision can then validate
    // and hook the original framework, without chaining stale revisions.
    {
        std::scoped_lock lock(mutex);
        if (states.empty() && hooksEnabled) {
            MH_QueueDisableHook(module + 0x24b40);
            MH_QueueDisableHook(module + 0x22210);
            if (MH_ApplyQueued() == MH_OK)
                hooksEnabled = false;
        }
    }
    for (auto property : {styler::OwnerProperty, styler::InlineProperty, styler::AdapterProperty})
        RemovePropW(s->target, property);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
RECT Lights(State *s) {
    auto row = Rect(s->object, 0x354);
    return {0, row.top, Scale(s, 84), row.bottom};
}
int Hit(State *s, POINT p) {
    ScreenToClient(s->target, &p);
    auto row = Lights(s);
    if (p.y < row.top || p.y >= row.bottom)
        return -1;
    for (int i = 0; i < 3; i++)
        if (p.x >= Scale(s, 8 + i * 20) && p.x < Scale(s, 28 + i * 20))
            return i;
    return -1;
}
LRESULT CALLBACK Subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto s = reinterpret_cast<State *>(ref);
    // The framework changes its EXISTING input recipient between CoreWindow,
    // frame and title HWND while maximizing. Normalize only these three controls;
    // the original title procedure keeps all other hit tests and gestures.
    if (m == WM_NCHITTEST && Hit(s, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) >= 0)
        return HTCLIENT;
    if (m == WM_LBUTTONDOWN || m == WM_NCLBUTTONDOWN || m == WM_LBUTTONDBLCLK) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (m != WM_NCLBUTTONDOWN)
            ClientToScreen(w, &p);
        int hit = Hit(s, p);
        if (hit >= 0 && Enabled(s, hit)) {
            s->pressed = hit;
            s->capture = w;
            SetCapture(w);
            return 0;
        }
    }
    if ((m == WM_LBUTTONUP || m == WM_NCLBUTTONUP) && s->pressed >= 0) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (m == WM_LBUTTONUP)
            ClientToScreen(w, &p);
        int action = s->pressed;
        s->pressed = -1;
        s->capture = nullptr;
        if (GetCapture() == w)
            ReleaseCapture();
        if (Hit(s, p) == action)
            PostMessageW(s->target, styler::EmbeddedActionMessage(), action, 0);
        return 0;
    }
    if (m == WM_CAPTURECHANGED || m == WM_CANCELMODE) {
        s->pressed = -1;
        s->capture = nullptr;
    }
    if (w == s->target) {
        if (m == WM_PARENTNOTIFY && LOWORD(wp) == WM_CREATE)
            PostMessageW(w, InputsChanged(), 0, 0);
        if (m == InputsChanged()) {
            if (!BindInputs(s))
                Release(s);
            return 0;
        }
        if (m == styler::EmbeddedActionMessage() && wp < 3) {
            if (Enabled(s, int(wp))) {
                auto object = s->object;
                ++s->commands;
                constexpr UINT64 ids[]{11, 6, 7};
                Fn<void (*)(void *, UINT64)>(0x5ca90)(object, ids[wp]);
                if (auto live = Active(object))
                    --live->commands;
            }
            return 0;
        }
        if (m == styler::QueryMessage())
            return !s->changing && s->mask == 7 && SUCCEEDED(s->paintError) ? styler::Acknowledged : 0;
        if (m == styler::DetachMessage()) {
            if (reinterpret_cast<HWND>(wp) != s->owner)
                return 0;
            if (s->changing || s->commands)
                return styler::TransitionBusy;
            Release(s);
            return styler::Acknowledged;
        }
        if (m == styler::DiagnosticMessage()) {
            auto r = Lights(s);
            switch (static_cast<styler::Metric>(wp)) {
            case styler::Metric::InlineLeft:
                return r.left;
            case styler::Metric::InlineTop:
                return r.top;
            case styler::Metric::InlineWidth:
                return r.right - r.left;
            case styler::Metric::InlineHeight:
                return r.bottom - r.top;
            case styler::Metric::InlineTabsLeft:
                return r.right;
            case styler::Metric::InlineKind:
                return 8;
            case styler::Metric::CaptionPaints:
                return s->paints;
            case styler::Metric::InlineOriginalButtonsVisible:
                return s->mask == 7 ? 0 : 3;
            default:
                return 0;
            }
        }
        if (m == WM_TIMER && wp == s->timer) {
            if (!IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT ||
                !styler::EmbeddedCore(s->core, s->target) || GetPropW(s->core, styler::OwnerProperty) != s->owner ||
                reinterpret_cast<UINT_PTR>(GetPropW(s->core, styler::EmbeddedInsetProperty)) != 84)
                Release(s);
            return 0;
        }
    }
    if (m == WM_NCDESTROY) {
        if (w == s->target)
            Release(s, true);
        else {
            std::erase(s->inputs, w);
            RemoveWindowSubclass(w, Subclass, SubclassId);
            if (s->inputs.empty())
                Release(s, true);
        }
        return DefSubclassProc(w, m, wp, lp);
    }
    return DefSubclassProc(w, m, wp, lp);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    auto core = styler::FrameContent(c->window);
    return core && GetPropW(core, styler::OwnerProperty) == c->owner &&
                   reinterpret_cast<UINT_PTR>(GetPropW(core, styler::EmbeddedInsetProperty)) == 84 &&
                   Find(c->window).object
               ? ERROR_SUCCESS
               : ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    auto match = Find(c->window);
    if (!match.object)
        return ERROR_NOT_SUPPORTED;
    auto s = new State;
    s->target = c->window;
    s->owner = c->owner;
    s->title = match.title;
    s->object = match.object;
    s->thread = GetCurrentThreadId();
    s->changing = true;
    s->core = styler::FrameContent(c->window);
    DWORD pid{};
    GetWindowThreadProcessId(s->owner, &pid);
    s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->process) {
        delete s;
        return ERROR_ACCESS_DENIED;
    }
    if (!Hooks()) {
        Debug("hooks declined");
        CloseHandle(s->process);
        delete s;
        return ERROR_NOT_SUPPORTED;
    }
    {
        std::scoped_lock lock(mutex);
        if (states.contains(s->object)) {
            CloseHandle(s->process);
            delete s;
            return ERROR_ALREADY_EXISTS;
        }
        states[s->object] = s;
    }
    if (!SetWindowSubclass(s->target, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)) || !BindInputs(s)) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    Reflow(s);
    s->changing = false;
    Debug("reflow", s->object, FAILED(s->paintError) ? s->paintError : HRESULT(s->mask));
    if (s->mask != 7 || FAILED(s->paintError)) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    s->timer = SetTimer(s->target, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
    if (!s->timer || !SetPropW(s->target, styler::OwnerProperty, s->owner) ||
        !SetPropW(s->target, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(8))) ||
        !SetPropW(s->target, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId))) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}
