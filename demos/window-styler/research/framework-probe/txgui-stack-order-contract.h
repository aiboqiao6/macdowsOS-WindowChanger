#pragma once
#include "txgui-stack-order.h"

namespace txstacktest {
inline void Run(HMODULE gf, IUnknown *source) {
    auto image = reinterpret_cast<BYTE *>(gf);
    txstack::Api api(image);
    txorder::Api ordinary(image);
    txorder::Snapshot rejected;
    Check(!ordinary.Capture(source, rejected), "ordinary order API continues to reject StackPanel parents");
    ComPtr<IUnknown> target, notifier, sink;
    std::array<ComPtr<IUnknown>, 4> children;
    constexpr GUID sinkId{0x78b5f698, 0x78d3, 0x46f0, {0x88, 0x34, 0xe7, 0x01, 0x76, 0xd0, 0x34, 0x40}};
    constexpr GUID eventsId{0x6ccbbc7b, 0x5c0a, 0x49aa, {0xb1, 0x99, 0x67, 0x33, 0xfd, 0xcd, 0x7d, 0xd4}};
    if (!Check(FixtureFactory(StackPanelId, StackPanelId, reinterpret_cast<void **>(target.GetAddressOf())) &&
                   FixtureFactory(SysButtonId, SysButtonId, reinterpret_cast<void **>(notifier.GetAddressOf())) &&
                   SUCCEEDED(notifier->QueryInterface(sinkId, &sink)),
               "create owned original target and native click notifier"))
        return;
    using Clear = HRESULT(STDMETHODCALLTYPE *)(IUnknown *);
    using Add = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned, IUnknown *);
    std::array<ComPtr<IUnknown>, 4> events;
    struct Cleanup {
        txstack::Api &api;
        IUnknown *source, *target;
        decltype(children) &children;
        decltype(events) &events;
        ~Cleanup() {
            for (size_t i = 0; i < children.size(); ++i) {
                if (events[i])
                    reinterpret_cast<Clear>((*reinterpret_cast<void ***>(events[i].Get()))[14])(events[i].Get());
                if (children[i]) {
                    api.Remove(target, children[i].Get());
                    api.Remove(source, children[i].Get());
                }
            }
        }
    } cleanup{api, source, target.Get(), children, events};
    std::array<txevents::Result, 4> bindings;
    for (size_t i = 0; i < children.size(); ++i) {
        if (!Check(FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(children[i].GetAddressOf())) &&
                       api.InsertAt(source, children[i].Get(), i) &&
                       SUCCEEDED(children[i]->QueryInterface(eventsId, &events[i])) &&
                       SUCCEEDED(reinterpret_cast<Add>((*reinterpret_cast<void ***>(events[i].Get()))[7])(
                           events[i].Get(), 0x1003, sink.Get())),
                   "populate original Button identity and real click subscription"))
            return;
        bindings[i] = txevents::Inspect(children[i].Get(), image);
    }
    txstack::Snapshot original;
    if (!Check(api.Capture(source, original) && original.typed.items.size() == 4,
               "capture all original typed Element and Frame orders with pinned references"))
        return;
    auto divergent = original;
    std::rotate(divergent.typed.items.begin(), divergent.typed.items.begin() + 1, divergent.typed.items.end());
    std::reverse(divergent.links.elements.items.begin(), divergent.links.elements.items.end());
    std::rotate(divergent.links.frames.items.begin(), divergent.links.frames.items.begin() + 2,
                divergent.links.frames.items.end());
    txstack::Snapshot actual;
    Check(api.Restore(divergent) && api.Capture(source, actual) && divergent.Same(actual),
          "restore three deliberately different original orders without removing unrelated children");
    std::array<unsigned, 4> permutation{0, 1, 2, 3};
    unsigned permutations{};
    bool all = true;
    do {
        auto desired = divergent;
        for (size_t i = 0; i < permutation.size(); ++i)
            desired.typed.items[i] = children[permutation[i]];
        all = all && api.Restore(desired) && api.Capture(source, actual) && desired.Same(actual);
        ++permutations;
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    Check(all && permutations == 24, "24 typed permutations preserve independently divergent raw link orders");
    Check(api.Restore(divergent), "restore the divergent source before cross-container migration");
    auto windows = WindowCount();
    auto migrate = [&] {
        for (size_t i = 0; i < 3; ++i)
            if (!api.Remove(source, children[i].Get()) || !api.InsertAt(target.Get(), children[i].Get(), i))
                return false;
        return true;
    };
    auto restore = [&] {
        // Only moved children leave/reenter. The unrelated fourth Button never
        // leaves its original parent, regardless of the three raw orders.
        for (size_t i = 0; i < divergent.typed.items.size(); ++i) {
            auto child = divergent.typed.items[i].Get();
            if (child == children[3].Get())
                continue;
            if (!api.Remove(target.Get(), child) || !api.InsertAt(source, child, i))
                return false;
        }
        return api.Restore(divergent);
    };
    Check(migrate() && api.Capture(source, actual) && actual.typed.items.size() == 1 &&
              actual.typed.items[0] == children[3],
          "move only the three existing caption candidates while preserving the unrelated source Button");
    txstack::Snapshot destination;
    Check(api.Capture(target.Get(), destination) && destination.typed.items.size() == 3,
          "all three original identities enter the real original destination layout vector");
    Check(restore() && api.Capture(source, actual) && divergent.Same(actual) &&
              api.Capture(target.Get(), destination) && destination.typed.items.empty(),
          "indexed reinsert plus original link setters exactly restore all three source orders");
    bool repeated = true;
    for (unsigned i = 0; i < 128 && repeated; ++i)
        repeated = migrate() && restore() && api.Capture(source, actual) && divergent.Same(actual);
    Check(repeated, "128 real cross-container cycles restore typed and both raw chains without duplicates");
    bool sameEvents = true;
    for (size_t i = 0; i < children.size(); ++i) {
        auto after = txevents::Inspect(children[i].Get(), image);
        sameEvents &= after.complete && bindings[i].complete && !bindings[i].numeric.empty() &&
                      after.numeric == bindings[i].numeric && after.named == bindings[i].named &&
                      after.listeners == bindings[i].listeners;
    }
    Check(sameEvents, "all original populated click bindings survive repeated three-order migrations");
    auto invalid = divergent;
    invalid.typed.items[1] = invalid.typed.items[0];
    Check(!api.Restore(invalid) && api.Capture(source, actual) && divergent.Same(actual),
          "duplicate typed identity is rejected before writes");
    invalid = divergent;
    invalid.links.frames.items.pop_back();
    Check(!api.Restore(invalid) && api.Capture(source, actual) && divergent.Same(actual),
          "incomplete raw order is rejected before writes");
    Check(!api.InsertAt(source, children[0].Get(), 0) && !api.Remove(target.Get(), children[0].Get()),
          "already-parented insertion and foreign removal are rejected");
    bool foreignAccepted{};
    std::thread other([&] {
        txstack::Snapshot value;
        foreignAccepted = api.Capture(source, value) || api.Restore(divergent);
    });
    other.join();
    Check(!foreignAccepted, "foreign thread cannot inspect or mutate the original layout transaction");
    Check(api.Restore(original) && api.Capture(source, actual) && original.Same(actual),
          "fixture restores its exact initial three orders");
    Check(WindowCount() == windows, "original StackPanel migration creates no top-level HWNDs");
}
} // namespace txstacktest
