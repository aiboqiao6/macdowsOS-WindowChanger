#pragma once
#include "txgui-order.h"
#include <thread>

namespace txordertest {
struct Watch {
    ComPtr<IUnknown> list;
    ComPtr<txobserve::Filter> filter;
    txobserve::FilterChange remove{};
    bool attached{};
    bool Attach(IUnknown *button, BYTE *image) {
        constexpr GUID id{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
        if (FAILED(button->QueryInterface(id, &list)) || !list)
            return false;
        auto table = *reinterpret_cast<void ***>(list.Get());
        if (table != reinterpret_cast<void **>(image + 0x1f50fc) || table[7] != image + 0x937d0 ||
            table[8] != image + 0x937f0)
            return false;
        filter.Attach(new txobserve::Filter(button, image));
        remove = reinterpret_cast<txobserve::FilterChange>(table[8]);
        attached = SUCCEEDED(reinterpret_cast<txobserve::FilterChange>(table[7])(list.Get(), filter.Get()));
        return attached && filter->References() == 2;
    }
    bool Quiet() const {
        auto state = filter->Read();
        return !state.stage7 && !state.stage8;
    }
    bool Detach() {
        if (!attached)
            return true;
        if (FAILED(remove(list.Get(), filter.Get())))
            return false;
        attached = false;
        return filter->References() == 1;
    }
    ~Watch() {
        Detach();
    }
};
inline void Run(HMODULE gf, IUnknown *stack) {
    auto image = reinterpret_cast<BYTE *>(gf);
    constexpr GUID frameId{0xdda0ed51, 0xb482, 0x4a5b, {0xa2, 0xb4, 0x31, 0xa3, 0x17, 0x68, 0xcd, 0x1d}};
    using GetFactory = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **);
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(gf, "DllGetClassObject"));
    ComPtr<IClassFactory> factory;
    if (!Check(get && SUCCEEDED(get(frameId, IID_PPV_ARGS(&factory))) && factory,
               "obtain the original Frame factory for an owned dual-chain fixture"))
        return;
    ComPtr<IUnknown> parent, other;
    std::array<ComPtr<IUnknown>, 4> children;
    if (!Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&parent))) && parent &&
                   SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&other))) && other,
               "create only owned original ordinary Frame parents"))
        return;
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    auto parentTable = *reinterpret_cast<void ***>(parent.Get());
    auto insert = reinterpret_cast<Insert>(parentTable[52]);
    auto remove = reinterpret_cast<Remove>(parentTable[53]);
    struct Cleanup {
        IUnknown *parent;
        Remove remove;
        std::array<ComPtr<IUnknown>, 4> &children;
        ~Cleanup() {
            for (auto &child : children)
                if (child)
                    remove(parent, child.Get());
        }
    } cleanup{parent.Get(), remove, children};
    std::array<Watch, 4> watches;
    constexpr GUID eventsId{0x6ccbbc7b, 0x5c0a, 0x49aa, {0xb1, 0x99, 0x67, 0x33, 0xfd, 0xcd, 0x7d, 0xd4}};
    constexpr GUID sinkId{0x78b5f698, 0x78d3, 0x46f0, {0x88, 0x34, 0xe7, 0x01, 0x76, 0xd0, 0x34, 0x40}};
    ComPtr<IUnknown> group, sink;
    if (!Check(FixtureFactory(SysButtonId, SysButtonId, reinterpret_cast<void **>(group.GetAddressOf())) && group &&
                   SUCCEEDED(group->QueryInterface(sinkId, &sink)),
               "create an original fixture-owned click notifier without calling an action"))
        return;
    using Clear = HRESULT(STDMETHODCALLTYPE *)(IUnknown *);
    struct Subscriptions {
        std::array<ComPtr<IUnknown>, 4> events;
        Clear clear{};
        ~Subscriptions() {
            if (clear)
                for (auto &event : events)
                    if (event)
                        clear(event.Get());
        }
    } subscriptions;
    std::array<FrameState, 4> states;
    std::array<txevents::Result, 4> bindings;
    for (unsigned i = 0; i < children.size(); ++i) {
        if (!Check(FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(children[i].GetAddressOf())) &&
                       children[i] && SUCCEEDED(insert(parent.Get(), children[i].Get(), nullptr)) &&
                       ReadFrame(children[i].Get(), states[i]) && watches[i].Attach(children[i].Get(), image),
                   "populate an owned original Button and register its original notification observer"))
            return;
        auto button = children[i].Get();
        if (!Check(SUCCEEDED(button->QueryInterface(eventsId, &subscriptions.events[i])) && subscriptions.events[i],
                   "obtain the real original owned event source before order changes"))
            return;
        auto table = *reinterpret_cast<void ***>(subscriptions.events[i].Get());
        using Add = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned, IUnknown *);
        if (!Check(table[7] == image + 0x93910 && table[14] == image + 0x93a10 &&
                       SUCCEEDED(reinterpret_cast<Add>(table[7])(subscriptions.events[i].Get(), 0x1003, sink.Get())),
                   "populate a guarded original click binding on each observed child"))
            return;
        subscriptions.clear = reinterpret_cast<Clear>(table[14]);
        bindings[i] = txevents::Inspect(children[i].Get(), image);
    }
    txorder::Api api(image);
    txorder::Snapshot original;
    if (!Check(api.Capture(parent.Get(), original) && original.elements.items.size() == 4 &&
                   original.frames.items.size() == 4 && original.elements.Same(original.frames),
               "original getters validate both complete doubly-linked orders and all parent identities"))
        return;
    auto same = [&](const txorder::Snapshot &expected) {
        txorder::Snapshot current;
        return api.Capture(parent.Get(), current) && current.elements.Same(expected.elements) &&
               current.frames.Same(expected.frames);
    };
    auto windows = WindowCount();
    std::array<unsigned, 4> permutation{0, 1, 2, 3};
    unsigned permutations{};
    bool all = true;
    do {
        auto desired = original;
        for (unsigned i = 0; i < 4; ++i) {
            desired.elements.items[i] = children[permutation[i]];
            desired.frames.items[i] = children[permutation[i]];
        }
        all &= api.Restore(desired) && same(desired);
        ++permutations;
    } while (all && std::next_permutation(permutation.begin(), permutation.end()));
    Check(all && permutations == 24 && api.Restore(original) && same(original),
          "all 24 head middle tail and adjacent permutations restore through original setters without reparenting");
    auto divergent = original;
    std::rotate(divergent.elements.items.begin(), divergent.elements.items.begin() + 1, divergent.elements.items.end());
    std::reverse(divergent.frames.items.begin(), divergent.frames.items.end());
    Check(api.Restore(divergent) && same(divergent) && api.Restore(original) && same(original),
          "Element and Frame orders can differ and each is restored independently without conflating their chains");
    auto duplicate = original;
    duplicate.elements.items[1] = duplicate.elements.items[0];
    Check(!api.Restore(duplicate) && same(original),
          "reject duplicate requested identities before any original link write");
    auto missing = original;
    missing.frames.items.pop_back();
    Check(!api.Restore(missing) && same(original), "reject missing requested children without changing either chain");
    auto foreign = original;
    foreign.elements.items[0] = other;
    Check(!api.Restore(foreign) && same(original), "reject a foreign-parent object before any mutation");
    bool foreignThreadAccepted = true;
    std::thread worker([&] { foreignThreadAccepted = api.Restore(original); });
    worker.join();
    Check(!foreignThreadAccepted && same(original), "reject a non-owner thread before calling original object methods");
    txorder::Snapshot excluded;
    Check(!api.Capture(stack, excluded), "reject StackPanel parents whose independent typed item order is not covered");
    bool repeated = true;
    for (unsigned i = 0; i < 128 && repeated; ++i)
        repeated = api.Restore(divergent) && same(divergent) && api.Restore(original) && same(original);
    Check(repeated, "128 dual-chain permutations and exact restores leave no cycles stale links or count changes");
    bool exceptionReached{};
    try {
        txorder::Api::ScopedRestore rollback(api, parent.Get());
        if (rollback.Ready() && rollback.Arm() && api.Restore(divergent)) {
            exceptionReached = true;
            throw 42;
        }
    } catch (int) {
    }
    Check(exceptionReached && same(original),
          "exception unwinding restores both chains without allocating or throwing");
    for (unsigned i = 0; i < children.size(); ++i) {
        FrameState after{};
        auto events = txevents::Inspect(children[i].Get(), image);
        Check(watches[i].Quiet() && ReadFrame(children[i].Get(), after) && Equal(states[i], after) && events.complete &&
                  events.numeric == bindings[i].numeric && events.named == bindings[i].named &&
                  events.listeners == bindings[i].listeners,
              "original child receives no parent lifecycle or paint notifications and preserves geometry and bindings");
        std::vector<IUnknown *> filters;
        Check(watches[i].Detach() && txobserve::List(children[i].Get(), image, filters) && filters.empty(),
              "native observer removal restores each original child filter list");
    }
    txorder::Snapshot empty;
    Check(api.Capture(other.Get(), empty) && empty.elements.items.empty() && api.Restore(empty),
          "empty parent round-trip preserves null original heads and tail");
    ComPtr<IUnknown> singleton;
    Check(FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(singleton.GetAddressOf())) && singleton,
          "create one original child for the singleton endpoint case");
    if (singleton) {
        auto otherTable = *reinterpret_cast<void ***>(other.Get());
        auto add = reinterpret_cast<Insert>(otherTable[52]);
        auto erase = reinterpret_cast<Remove>(otherTable[53]);
        txorder::Snapshot single;
        Check(SUCCEEDED(add(other.Get(), singleton.Get(), nullptr)) && api.Capture(other.Get(), single) &&
                  single.elements.items.size() == 1 && single.frames.items.size() == 1 && api.Restore(single),
              "singleton round-trip preserves both null-neighbor pairs and original heads and tail");
        Check(SUCCEEDED(erase(other.Get(), singleton.Get())) && api.Capture(other.Get(), single) &&
                  single.elements.items.empty() && single.frames.items.empty(),
              "native removal after singleton ordering restores the original empty parent");
    }
    Check(WindowCount() == windows, "dual-chain operations create no HWND or input path");
}
} // namespace txordertest
