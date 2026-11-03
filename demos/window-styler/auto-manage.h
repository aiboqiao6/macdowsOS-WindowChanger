#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace styler {
// Discovery scheduling only: no layout/pixel following. All times are injected
// monotonic milliseconds so initialization, retries and manual exclusions test
// without sleeps or touching unrelated application windows.
class AutoManageQueue {
    struct State {
        uint32_t pid{};
        uint64_t first{}, next{};
        unsigned failures{};
        bool excluded{};
    };
    std::map<uintptr_t, State> states_;

  public:
    bool Ready(uintptr_t window, uint32_t pid, uint64_t now, bool visible) {
        auto [it, added] = states_.try_emplace(window, State{pid, now, now + 500});
        auto &state = it->second;
        if (state.pid != pid)
            state = {pid, now, now + 500};
        if (!visible) {
            state.first = now;
            state.next = std::max(state.next, now + 500);
            return false;
        }
        return !added && !state.excluded && now >= state.next;
    }
    void Failed(uintptr_t window, uint64_t now) {
        auto &state = states_.at(window);
        state.next = now + std::min<uint64_t>(60000, 5000ull << std::min(state.failures, 4u));
        ++state.failures;
    }
    void Exclude(uintptr_t window, uint32_t pid) {
        states_[window] = {pid, 0, 0, 0, true};
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
