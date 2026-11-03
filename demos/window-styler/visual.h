#pragma once
#include "session.h"
#include <array>

namespace styler {
inline constexpr std::array<DWORD, 4> VisualAttributes{33, 34, 35, 36};
struct VisualValue {
    DWORD attribute{}, original{}, applied{};
    bool changed{};
};
struct VisualState {
    DWORD pid{}, thread{};
    std::array<VisualValue, 4> values{};
};
// Public DWM attributes only: no injection, subclass, style/size messages.
class VisualSession {
  public:
    explicit VisualSession(HWND owner) : owner_(owner) {}
    ~VisualSession() {
        RestoreAll();
    }
    Result Apply(HWND window);
    Result Restore(HWND window);
    Result RestoreAll();
    bool Owns(HWND window) const;
    size_t Count() const;

  private:
    HWND owner_{};
    std::map<HWND, VisualState> windows_;
};
} // namespace styler
