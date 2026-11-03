#pragma once
#include "txgui-paint-observer.h"

// Experimental original-filter callback. No HWND, input, layout or renderer
// lifetime is owned here. Begin/EndDraw belong exclusively to the caller.
namespace txd2d {
// The original renderer sends background (0x39a), Button content (0x385)
// and border (0x3a8) separately. Replacing background alone leaves the native
// content glyph intact. Preserve background/border and replace only content.
constexpr unsigned PaintEvent = 0x385;
struct State {
    POINT viewport{}, window{};
    RECT clip{};
    BYTE alpha{};
    bool Same(const State &other) const {
        return viewport.x == other.viewport.x && viewport.y == other.viewport.y && window.x == other.window.x &&
               window.y == other.window.y && EqualRect(&clip, &other.clip) && alpha == other.alpha;
    }
};
struct Drawing {
    unsigned paints{}, rejected{}, stateChanges{}, skipped{};
    BOOL scaled{};
    State before{}, after{};
};
class Filter : public txobserve::Filter {
    LONG references_{1};
    IUnknown *source_{};
    BYTE *image_{};
    DWORD thread_{};
    RECT circle_{};
    DWORD color_{};
    bool profile_{}, bitmap_{}, enabled_{}, busy_{};
    bool requireOrigin_{};
    POINT origin_{};
    HWND originalWindow_{};
    Drawing drawing_{};

    bool Table(void *graphics) const {
        if (!profile_ || !txobserve::ReadableMemory(graphics, sizeof(void *)))
            return false;
        auto table = *reinterpret_cast<void ***>(graphics);
        return table == reinterpret_cast<void **>(image_ + 0x1eaf9c) ||
               (bitmap_ && table == reinterpret_cast<void **>(image_ + 0x1eae94));
    }
    State ReadState(void *graphics) const {
        auto table = *reinterpret_cast<void ***>(graphics);
        using GetPoint = void(__thiscall *)(void *, POINT *);
        using GetClip = void(__thiscall *)(void *, RECT *);
        using Alpha = BYTE(__thiscall *)(void *, BYTE);
        State result;
        reinterpret_cast<GetPoint>(table[0x28 / 4])(graphics, &result.viewport);
        reinterpret_cast<GetPoint>(table[0x30 / 4])(graphics, &result.window);
        reinterpret_cast<GetClip>(table[0x3c / 4])(graphics, &result.clip);
        result.alpha = reinterpret_cast<Alpha>(table[0x74 / 4])(graphics, 255);
        return result;
    }

  public:
    Filter(IUnknown *source, BYTE *image, RECT circle, DWORD color, bool ownedBitmap = false)
        : txobserve::Filter(source, image), source_(source), image_(image), thread_(GetCurrentThreadId()),
          circle_(circle), color_(color), bitmap_(ownedBitmap) {
        constexpr GUID id{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
        if (!styler::MatchesPdb(reinterpret_cast<HMODULE>(image), id, 24))
            return;
        profile_ = circle.right > circle.left && circle.bottom > circle.top &&
                   circle.right - circle.left == circle.bottom - circle.top;
        for (auto rva : {0x1eaf9cu, 0x1eae94u}) {
            auto table = reinterpret_cast<void **>(image + rva);
            profile_ &= table[0x58 / 4] == image + 0x19870 && table[0x28 / 4] == image + 0x1a700 &&
                        table[0x30 / 4] == image + 0x1a720 && table[0x3c / 4] == image + 0x1a0a0 &&
                        table[0x74 / 4] == image + 0x1a6c0;
        }
    }
    bool Ready() const {
        return profile_;
    }
    // The passive base has no virtual destructor. Keep derived COM lifetime
    // here so final Release deletes the complete drawing-filter type.
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&references_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto remaining = InterlockedDecrement(&references_);
        if (!remaining)
            delete this;
        return remaining;
    }
    LONG References() const {
        return InterlockedCompareExchange(const_cast<LONG *>(&references_), 0, 0);
    }
    void Enable(bool enabled) {
        enabled_ = enabled;
    }
    void RequireOrigin(POINT origin) {
        requireOrigin_ = true;
        origin_ = origin;
    }
    void RequireOriginalOrigin(HWND window) {
        originalWindow_ = window;
        requireOrigin_ = true;
    }
    Drawing ReadDrawing() const {
        return drawing_;
    }
    HRESULT STDMETHODCALLTYPE Stage7(IUnknown *source, txobserve::Arg *arg, BOOL *handled, LRESULT *result, BOOL *route,
                                     BOOL *observers) override {
        auto error = GetLastError();
        txobserve::Filter::Stage7(source, arg, handled, result, route, observers);
        struct RestoreError {
            DWORD value;
            ~RestoreError() {
                SetLastError(value);
            }
        } restore{error};
        if (!txobserve::ReadableMemory(arg, 12) || arg->code != PaintEvent)
            return S_OK;
        // Fail open: unknown payloads and foreign sources never consume the
        // original notification or call any graphics method.
        if (source != source_ || GetCurrentThreadId() != thread_ || !handled || !route || !observers || busy_ ||
            arg->size < sizeof(txobserve::Arg) || arg->size > 4096 ||
            !txobserve::ReadableMemory(arg, sizeof(txobserve::Arg)) || !Table(arg->graphics)) {
            ++drawing_.rejected;
            return S_OK;
        }
        auto before = ReadState(arg->graphics);
        drawing_.before = before;
        drawing_.scaled = arg->scaled;
        if (originalWindow_) {
            // Read the original source's current native geometry on the paint
            // callback itself. This is not a layout-follow timer or a position
            // write, and remains correct during nested native resize painting.
            auto table = *reinterpret_cast<void ***>(source_);
            using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
            RECT rect{};
            POINT client{};
            if (table != reinterpret_cast<void **>(image_ + 0x1f4c00) || table[0x254 / 4] != image_ + 0x3b50 ||
                !IsWindow(originalWindow_) || !ClientToScreen(originalWindow_, &client) ||
                FAILED(reinterpret_cast<Get>(table[0x254 / 4])(source_, &rect))) {
                ++drawing_.rejected;
                return S_OK;
            }
            origin_ = {rect.left - client.x, rect.top - client.y};
        }
        if (requireOrigin_ &&
            (before.viewport.x - before.window.x != origin_.x || before.viewport.y - before.window.y != origin_.y)) {
            ++drawing_.rejected;
            return S_OK;
        }
        RECT intersection{};
        if (!enabled_) {
            ++drawing_.skipped;
            return S_OK;
        }
        if (!IntersectRect(&intersection, &circle_, &before.clip)) {
            // A content repaint outside the circle must still suppress the
            // old glyph; passing it through would resurrect part of a glyph.
            ++drawing_.skipped;
            *handled = TRUE;
            *route = FALSE;
            *observers = FALSE;
            return S_OK;
        }
        busy_ = true;
        auto table = *reinterpret_cast<void ***>(arg->graphics);
        using Fill = void(__thiscall *)(void *, DWORD, const RECT *, const SIZE *);
        const SIZE radii{(circle_.right - circle_.left) / 2, (circle_.bottom - circle_.top) / 2};
        reinterpret_cast<Fill>(table[0x58 / 4])(arg->graphics, color_, &circle_, &radii);
        drawing_.after = ReadState(arg->graphics);
        drawing_.stateChanges += !before.Same(drawing_.after);
        ++drawing_.paints;
        busy_ = false;
        *handled = TRUE;
        *route = FALSE;
        *observers = FALSE;
        return S_OK;
    }
};
} // namespace txd2d
