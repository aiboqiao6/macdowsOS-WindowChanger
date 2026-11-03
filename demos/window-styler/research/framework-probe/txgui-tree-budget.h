#pragma once
#include <cstdint>
#include <set>

// Bounded discovery must distinguish a complete tree from a successful prefix.
// Shared with a standalone contract; independent of GF and of any target HWND.
namespace txtree {
struct Budget {
    unsigned maxNodes{4096}, maxDepth{48}, maxChildren{1024};
    unsigned nodes{}, deepest{}, errors{}, limited{}, duplicates{};
    std::set<uintptr_t> identities;
    bool Enter(uintptr_t identity, unsigned depth) {
        if (!identity) {
            ++errors;
            return false;
        }
        if (depth > maxDepth || nodes >= maxNodes) {
            ++limited;
            return false;
        }
        if (!identities.insert(identity).second) {
            ++duplicates;
            return false;
        }
        ++nodes;
        if (depth > deepest)
            deepest = depth;
        return true;
    }
    bool Children(bool read, unsigned count) {
        if (!read) {
            ++errors;
            return false;
        }
        if (count > maxChildren) {
            ++limited;
            return false;
        }
        return true;
    }
    bool Next() {
        if (nodes < maxNodes)
            return true;
        ++limited;
        return false;
    }
    void ChildFailure() {
        ++errors;
    }
    bool Complete() const {
        return nodes != 0 && !errors && !limited && !duplicates;
    }
};
} // namespace txtree
