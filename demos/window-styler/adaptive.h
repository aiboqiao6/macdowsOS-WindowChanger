#pragma once
#include "visual.h"
#include <memory>

namespace styler {
inline constexpr wchar_t HeaderProperty[] = L"Macdows.WindowStyler.Header.v3";
inline constexpr wchar_t MaskProperty[] = L"Macdows.WindowStyler.ButtonMask.v3";
inline constexpr wchar_t PolicyProperty[] = L"Macdows.WindowStyler.Policy.v3";
enum class FramePolicy : UINT_PTR {
    Pending = 1,
    ExistingCaption,
    ExistingClientStrip,
    ExtendedFallback
};
class AdaptiveSession {
  public:
    explicit AdaptiveSession(HWND owner);
    ~AdaptiveSession();
    Result Apply(HWND window);
    Result Restore(HWND window);
    Result RestoreAll();
    bool Owns(HWND window) const;
    size_t Count() const;
    void Tick();

  private:
    struct Frame;
    HWND owner_{};
    VisualSession visual_;
    Session injected_;
    std::map<HWND, std::unique_ptr<Frame>> frames_;
    Result Activate(HWND window, Frame &frame);
    void Update(HWND window, Frame &frame);
};
} // namespace styler
