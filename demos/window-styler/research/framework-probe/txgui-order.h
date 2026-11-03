#pragma once
#include "../../pe-identity.h"
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <set>
#include <array>

// Guarded, GUI-thread-only ordering of existing Frame children. The original
// framework exposes separate Element and Frame links; changing only one is
// insufficient. Every object is pinned before invoking original link setters.
// No private field is written, parent setter called, or child removed/inserted.
namespace txstack {
class Api;
}
namespace txorder {
using Microsoft::WRL::ComPtr;
using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown **);
using Put = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
struct Chain {
    std::vector<ComPtr<IUnknown>> items;
    bool Same(const Chain &other) const {
        if (items.size() != other.items.size())
            return false;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].Get() != other.items[i].Get())
                return false;
        return true;
    }
};
struct Snapshot {
    ComPtr<IUnknown> parent;
    Chain elements, frames;
};
class Api {
  public:
    class ScopedRestore;

  private:
    friend class txstack::Api;
    BYTE *image_{};
    DWORD thread_{};
    bool ready_{};
    bool InImage(const void *address) const {
        MEMORY_BASIC_INFORMATION region{};
        return address && VirtualQuery(address, &region, sizeof(region)) && region.AllocationBase == image_;
    }
    static bool Readable(const void *address, size_t size) {
        MEMORY_BASIC_INFORMATION region{};
        auto begin = reinterpret_cast<uintptr_t>(address);
        return address && begin + size >= begin && VirtualQuery(address, &region, sizeof(region)) &&
               region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
               begin + size <= reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    }
    bool Guard(IUnknown *object) const {
        if (!ready_ || GetCurrentThreadId() != thread_ || !Readable(object, 12))
            return false;
        auto primary = *reinterpret_cast<void ***>(object);
        auto native = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(object) + 8);
        if (!Readable(primary, 0x108) || !Readable(native, 0x204) || !InImage(primary) || !InImage(native) ||
            !InImage(primary[1]) || !InImage(primary[2]))
            return false;
        // These wrappers and leaf setters are independently disassembled.
        // All setters only exchange a COM reference; no virtual host callback,
        // repaint, event dispatch, or parent lifecycle is invoked by a setter.
        for (auto [slot, rva] : {std::pair{0x60u, 0x6170u},
                                 {0x74u, 0x6350u},
                                 {0x78u, 0x6370u},
                                 {0x7cu, 0x2b50u},
                                 {0x84u, 0x2b90u},
                                 {0x88u, 0x2bb0u},
                                 {0x90u, 0x2bf0u},
                                 {0x94u, 0x2c10u},
                                 {0x98u, 0x2c30u},
                                 {0xe0u, 0x4140u},
                                 {0xe4u, 0x4160u},
                                 {0xe8u, 0x4180u},
                                 {0xf0u, 0x41c0u},
                                 {0xf4u, 0x41e0u},
                                 {0xf8u, 0x4200u},
                                 {0x100u, 0x4240u}})
            if (primary[slot / 4] != image_ + rva)
                return false;
        for (auto [slot, rva] : {std::pair{0x2cu, 0x75040u},
                                 {0x58u, 0x75380u},
                                 {0x5cu, 0x750a0u},
                                 {0x60u, 0x753c0u},
                                 {0x64u, 0x74f80u},
                                 {0x6cu, 0x75340u},
                                 {0x70u, 0x74df0u},
                                 {0x78u, 0x752c0u},
                                 {0x7cu, 0x74eb0u},
                                 {0x80u, 0x75300u},
                                 {0x1f8u, 0x7cb20u},
                                 {0x1fcu, 0x7c4e0u},
                                 {0x200u, 0x7c450u}})
            if (native[slot / 4] != image_ + rva)
                return false;
        return true;
    }
    static bool Read(IUnknown *object, unsigned offset, ComPtr<IUnknown> &result) {
        result.Reset();
        return SUCCEEDED(reinterpret_cast<Get>((*reinterpret_cast<void ***>(object))[offset / 4])(object, &result));
    }
    static bool Write(IUnknown *object, unsigned offset, IUnknown *value) {
        return SUCCEEDED(reinterpret_cast<Put>((*reinterpret_cast<void ***>(object))[offset / 4])(object, value));
    }
    bool ReadChain(IUnknown *parent, bool element, Chain &result) const {
        result.items.clear();
        ComPtr<IUnknown> current, previous;
        if (!Read(parent, element ? 0x88 : 0xf4, current))
            return false;
        std::set<IUnknown *> seen;
        while (current) {
            if (result.items.size() >= 128 || !seen.insert(current.Get()).second || !Guard(current.Get()))
                return false;
            ComPtr<IUnknown> actualParent, actualPrevious, next;
            if (!Read(current.Get(), element ? 0x60 : 0x100, actualParent) || actualParent.Get() != parent ||
                !Read(current.Get(), element ? 0x74 : 0xe0, actualPrevious) || actualPrevious != previous ||
                !Read(current.Get(), element ? 0x7c : 0xe8, next))
                return false;
            result.items.push_back(current);
            previous = current;
            current = next;
        }
        if (element) {
            ComPtr<IUnknown> tail;
            if (!Read(parent, 0x94, tail) || tail != previous)
                return false;
        }
        return true;
    }
    static bool SameSet(const Chain &a, const Chain &b) {
        std::set<IUnknown *> first, second;
        for (auto &item : a.items)
            first.insert(item.Get());
        for (auto &item : b.items)
            second.insert(item.Get());
        return a.items.size() == first.size() && b.items.size() == second.size() && first == second;
    }
    static IUnknown *At(const Chain &chain, ptrdiff_t index) {
        return index >= 0 && static_cast<size_t>(index) < chain.items.size() ? chain.items[index].Get() : nullptr;
    }
    static bool Commit(IUnknown *parent, const Chain &current, const Chain &desired, bool element, bool force = false) {
        // Both vectors retain all old and new references until both chains
        // are committed and validated. No message pump runs inside this scope.
        for (size_t i = 0; i < desired.items.size(); ++i) {
            auto object = desired.items[i].Get();
            auto old = std::find_if(current.items.begin(), current.items.end(),
                                    [&](const auto &item) { return item.Get() == object; });
            auto index = old - current.items.begin();
            auto desiredIndex = static_cast<ptrdiff_t>(i);
            if ((force || At(current, index - 1) != At(desired, desiredIndex - 1)) &&
                !Write(object, element ? 0x78 : 0xe4, At(desired, desiredIndex - 1)))
                return false;
            if ((force || At(current, index + 1) != At(desired, desiredIndex + 1)) &&
                !Write(object, element ? 0x84 : 0xf0, At(desired, desiredIndex + 1)))
                return false;
        }
        if ((force || At(current, 0) != At(desired, 0)) && !Write(parent, element ? 0x90 : 0xf8, At(desired, 0)))
            return false;
        auto oldTail = At(current, static_cast<ptrdiff_t>(current.items.size()) - 1);
        auto newTail = At(desired, static_cast<ptrdiff_t>(desired.items.size()) - 1);
        return !element || (!force && oldTail == newTail) || Write(parent, 0x98, newTail);
    }
    bool ForceOriginal(const Snapshot &saved) const noexcept {
        // Only ScopedRestore's own pre-mutation capture reaches this path.
        // All interfaces are pinned and their leaf ABI already verified.
        return Commit(saved.parent.Get(), saved.elements, saved.elements, true, true) &&
               Commit(saved.parent.Get(), saved.frames, saved.frames, false, true);
    }

    bool CaptureLinks(IUnknown *parent, Snapshot &result, bool stack) const {
        if (!Guard(parent) ||
            (!stack && *reinterpret_cast<void ***>(parent) == reinterpret_cast<void **>(image_ + 0x220f58)))
            return false;
        Snapshot value;
        value.parent = parent;
        if (!ReadChain(parent, true, value.elements) || !ReadChain(parent, false, value.frames) ||
            !SameSet(value.elements, value.frames))
            return false;
        result = std::move(value);
        return true;
    }
    bool RestoreLinks(const Snapshot &desired, bool stack) const {
        Snapshot current;
        if (!desired.parent || !CaptureLinks(desired.parent.Get(), current, stack) ||
            !SameSet(current.elements, desired.elements) || !SameSet(current.frames, desired.frames))
            return false;
        for (auto &chain : {&desired.elements, &desired.frames})
            for (auto &item : chain->items)
                if (!Guard(item.Get()))
                    return false;
        bool committed = Commit(desired.parent.Get(), current.elements, desired.elements, true) &&
                         Commit(desired.parent.Get(), current.frames, desired.frames, false);
        if (committed) {
            try {
                Snapshot after;
                if (CaptureLinks(desired.parent.Get(), after, stack) && desired.elements.Same(after.elements) &&
                    desired.frames.Same(after.frames))
                    return true;
            } catch (...) {
            }
        }
        Commit(current.parent.Get(), desired.elements, current.elements, true, true);
        Commit(current.parent.Get(), desired.frames, current.frames, false, true);
        return false;
    }

  public:
    explicit Api(BYTE *image) : image_(image), thread_(GetCurrentThreadId()) {
        constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
        ready_ = styler::MatchesPdb(reinterpret_cast<HMODULE>(image), identity, 24);
        if (!ready_)
            return;
        // These seven complete 51-byte leaf bodies contain no absolute
        // relocations. Verify their original instructions, not only RVAs, so
        // an intercepted setter cannot silently introduce host callbacks.
        constexpr std::array<BYTE, 51> leaf{
            0x55, 0x8b, 0xec, 0x8d, 0x41, 0x50, 0x8b, 0x4d, 0x08, 0x39, 0x08, 0x74, 0x20, 0x56, 0x8b, 0xf1, 0x87,
            0x30, 0x85, 0xc9, 0x74, 0x06, 0x8b, 0x01, 0x51, 0xff, 0x50, 0x04, 0x85, 0xf6, 0x74, 0x06, 0x8b, 0x06,
            0x56, 0xff, 0x50, 0x08, 0x33, 0xc0, 0x5e, 0x5d, 0xc2, 0x04, 0x00, 0x33, 0xc0, 0x5d, 0xc2, 0x04, 0x00};
        for (auto [rva, member] : {std::pair{0x753c0u, 0x0cu},
                                   {0x75340u, 0x10u},
                                   {0x752c0u, 0x1cu},
                                   {0x75300u, 0x20u},
                                   {0x7cb20u, 0x50u},
                                   {0x7c4e0u, 0x54u},
                                   {0x7c450u, 0x58u}}) {
            auto expected = leaf;
            expected[5] = static_cast<BYTE>(member);
            ready_ &= !memcmp(image + rva, expected.data(), expected.size());
        }
    }
    bool Capture(IUnknown *parent, Snapshot &result) const {
        // A StackPanel additionally owns a typed layout vector; ordering its
        // children requires a separate three-container transaction.
        return CaptureLinks(parent, result, false);
    }
    bool Restore(const Snapshot &desired) const {
        return RestoreLinks(desired, false);
    }
};
class Api::ScopedRestore {
    Api &api_;
    Snapshot saved_;
    bool ready_{}, armed_{};

  public:
    ScopedRestore(Api &api, IUnknown *parent) : api_(api), ready_(api.Capture(parent, saved_)) {}
    ScopedRestore(const ScopedRestore &) = delete;
    ScopedRestore &operator=(const ScopedRestore &) = delete;
    bool Ready() const {
        return ready_;
    }
    const Snapshot &Original() const {
        return saved_;
    }
    bool Arm() {
        return armed_ = ready_;
    }
    bool Finish() {
        if (!armed_)
            return ready_;
        if (api_.Restore(saved_)) {
            armed_ = false;
            return true;
        }
        return false;
    }
    ~ScopedRestore() noexcept {
        if (armed_)
            api_.ForceOriginal(saved_);
    }
};
} // namespace txorder
