#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace styler {
// Capability alone selects candidates; there is no product name or test-record list.
inline bool AutomaticCandidate(bool supported, bool alreadyOwned) {
    return supported && !alreadyOwned;
}
// Discovery scheduling only: no layout/pixel following. All times are injected
// monotonic milliseconds so retries and visibility transitions can be tested
// without sleeps or touching unrelated application windows.
class AutoManageQueue {
    struct State {
        uint32_t pid{};
        uint64_t next{};
        unsigned failures{};
        bool visible{};
    };
    std::map<uintptr_t, State> states_;

  public:
    bool Ready(uintptr_t window, uint32_t pid, uint64_t now, bool visible) {
        auto it = states_.try_emplace(window, State{pid, now}).first;
        auto &state = it->second;
        if (state.pid != pid)
            state = {pid, now};
        if (!visible) {
            state.visible = false;
            return false;
        }
        if (!state.visible) {
            state.visible = true;
            state.next = now;
        }
        return now >= state.next;
    }
    void Failed(uintptr_t window, uint64_t now) {
        auto &state = states_.at(window);
        state.next = now + std::min<uint64_t>(60000, 5000ull << std::min(state.failures, 4u));
        ++state.failures;
    }
    void Allow(uintptr_t window) {
        states_.erase(window);
    }
    void Reset() {
        states_.clear();
    }
    void Prune(const std::vector<std::pair<uintptr_t, uint32_t>> &alive) {
        std::erase_if(states_, [&](const auto &entry) {
            return std::find(alive.begin(), alive.end(), std::pair{entry.first, entry.second.pid}) == alive.end();
        });
    }
    size_t Size() const {
        return states_.size();
    }
};
} // namespace styler
