#pragma once
#include <algorithm>
namespace styler::views {
struct Rect {
    int x{}, y{}, w{}, h{};
    bool operator==(Rect const &) const = default;
};
enum class Role {
    Region,
    Container,
    Close,
    Minimize,
    Maximize
};
// Normalize per-axis setters that replay the current transformed geometry.
// Reapplying a host's current size must not accumulate another left inset.
inline Rect OriginalRequest(Rect requested, Rect original, Rect applied) {
    if (requested.x == applied.x)
        requested.x = original.x;
    if (requested.w == applied.w)
        requested.w = original.w;
    return requested;
}
inline Rect Apply(Rect r, Role role, int parentX = 0) {
    if (role == Role::Container) {
        r.x = 6 - parentX;
        r.w = 60;
    } else if (role == Role::Region) {
        r.x += 72;
        r.w = std::max(0, r.w - 72);
    } else {
        r.x = role == Role::Close ? 0 : role == Role::Minimize ? 20 : 40;
        r.w = 20;
    }
    return r;
}
} // namespace styler::views
