#include "visual.h"
#include <dwmapi.h>

namespace styler {
namespace {
bool Identity(HWND window, const VisualState &state) {
    DWORD pid{};
    return IsWindow(window) && styler::WindowThreadProcessId(window, &pid) == state.thread && pid == state.pid;
}
} // namespace
bool VisualSession::Owns(HWND window) const {
    return GetPropW(window, OwnerProperty) == owner_;
}
size_t VisualSession::Count() const {
    size_t count{};
    for (const auto &[window, state] : windows_)
        if (Identity(window, state) && Owns(window))
            ++count;
    return count;
}
Result VisualSession::Apply(HWND window) {
    auto candidate = Inspect(window);
    if (!candidate.supported)
        return {false, candidate.reason};
    if (Owns(window))
        return {true, L"已接管，原有布局保留。"};
    if (GetPropW(window, OwnerProperty))
        return {false, L"另一个控制器正在管理此窗口，请先还原。"};
    if (windows_.contains(window))
        windows_.erase(window); // destroyed/reused HWND
    VisualState state;
    state.thread = styler::WindowThreadProcessId(window, &state.pid);
    BOOL dark{};
    DwmGetWindowAttribute(window, 20, &dark, sizeof(dark));
    std::array<DWORD, 4> desired{2, dark ? RGB(78, 78, 82) : RGB(212, 210, 207),
                                 dark ? RGB(40, 40, 42) : RGB(244, 243, 241),
                                 dark ? RGB(232, 232, 234) : RGB(58, 57, 55)};
    // Capture every readable original before making any change.
    std::array<bool, 4> readable{};
    for (size_t i = 0; i < state.values.size(); ++i) {
        auto &value = state.values[i];
        value.attribute = VisualAttributes[i];
        value.applied = desired[i];
        readable[i] = SUCCEEDED(DwmGetWindowAttribute(window, value.attribute, &value.original, sizeof(DWORD)));
    }
    if (!SetPropW(window, OwnerProperty, owner_))
        return {false, L"无法建立窗口会话（权限或窗口已失效）。"};
    if (!SetPropW(window, AdapterProperty, reinterpret_cast<HANDLE>(Adapter::Dwm))) {
        RemovePropW(window, OwnerProperty);
        return {false, L"无法记录原位适配状态。"};
    }
    auto &record = windows_[window] = state;
    size_t applied{}, changed{};
    UINT acceptedMask{}, changedMask{};
    for (size_t i = 0; i < record.values.size(); ++i) {
        auto &value = record.values[i];
        if (!readable[i])
            continue;
        if (value.original == value.applied) {
            ++applied;
            acceptedMask |= 1u << i;
            continue;
        }
        if (SUCCEEDED(DwmSetWindowAttribute(window, value.attribute, &value.applied, sizeof(DWORD)))) {
            value.changed = true;
            ++applied;
            ++changed;
            acceptedMask |= 1u << i;
            changedMask |= 1u << i;
        }
    }
    if (!applied) {
        Restore(window);
        return {false, L"系统未接受任何可还原的 DWM 外观属性。"};
    }
    if (!SetPropW(window, VisualMaskProperty, reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(acceptedMask))) ||
        (changedMask &&
         !SetPropW(window, VisualChangedProperty, reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(changedMask))))) {
        Restore(window);
        return {false, L"无法记录外观属性能力。"};
    }
    return {true, L"已原位接管：" + std::to_wstring(applied) + L"/4 项可用属性，实际修改 " + std::to_wstring(changed) +
                      L" 项；其余保留。"};
}
Result VisualSession::Restore(HWND window) {
    auto item = windows_.find(window);
    if (item == windows_.end())
        return {true, L"无需还原。"};
    if (!Identity(window, item->second) || !Owns(window)) {
        windows_.erase(item);
        return {true, L"原窗口已结束或会话已转移。"};
    }
    bool ok = true;
    for (auto &value : item->second.values) {
        if (!value.changed)
            continue;
        if (SUCCEEDED(DwmSetWindowAttribute(window, value.attribute, &value.original, sizeof(DWORD))))
            value.changed = false;
        else
            ok = false;
    }
    if (!ok)
        return {false, L"部分 DWM 属性还原失败，将由守护进程重试。"};
    RemovePropW(window, VisualMaskProperty);
    RemovePropW(window, VisualChangedProperty);
    RemovePropW(window, AdapterProperty);
    RemovePropW(window, OwnerProperty);
    windows_.erase(item);
    return {true, L"已还原原始外观。"};
}
Result VisualSession::RestoreAll() {
    std::vector<HWND> copy;
    for (const auto &[window, state] : windows_) {
        (void)state;
        copy.push_back(window);
    }
    bool ok = true;
    for (HWND window : copy)
        if (!Restore(window).ok)
            ok = false;
    return {ok, ok ? L"全部外观已还原。" : L"部分属性等待重试还原。"};
}
} // namespace styler
