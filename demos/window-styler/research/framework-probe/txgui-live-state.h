#pragma once
#include "txgui-order.h"
#include "txgui-paint-observer.h"

namespace {
struct OrderNode {
    ComPtr<IUnknown> object;
    uintptr_t parent{};
    RECT rect{}, screen{}, padding{}, margin{};
    BOOL hidden{}, visible{}, enabled{};
    std::vector<uintptr_t> children;
    txevents::Result events;
    std::vector<IUnknown *> filters;
    bool button{};
    bool Read(IUnknown *frame) {
        using Rect = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
        using Bool = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BOOL *);
        auto r = [&](size_t offset, RECT &value) {
            auto get = Slot<Rect>(frame, offset / 4);
            return get && SUCCEEDED(get(frame, &value));
        };
        auto b = [&](size_t offset, BOOL &value) {
            auto get = Slot<Bool>(frame, offset / 4);
            return get && SUCCEEDED(get(frame, &value));
        };
        if (FAILED(frame->QueryInterface(IID_PPV_ARGS(&object))) || !object || !r(0x1e0, rect) || !r(0x254, screen) ||
            !r(0x210, padding) || !r(0x208, margin) || !b(0x158, hidden) || !b(0x160, visible) || !b(0x168, enabled))
            return false;
        button = *reinterpret_cast<void ***>(frame) == reinterpret_cast<void **>(gfImage + 0x1f4c00);
        if (button) {
            events = txevents::Inspect(frame, gfImage);
            if (!events.profile || !events.complete || !txobserve::List(frame, gfImage, filters))
                return false;
        }
        return true;
    }
    bool SameState(const OrderNode &other) const {
        return object == other.object && parent == other.parent && EqualRect(&rect, &other.rect) &&
               EqualRect(&screen, &other.screen) && EqualRect(&padding, &other.padding) &&
               EqualRect(&margin, &other.margin) && hidden == other.hidden && visible == other.visible &&
               enabled == other.enabled && button == other.button &&
               (!button || (events.numeric == other.events.numeric && events.named == other.events.named &&
                            events.listeners == other.events.listeners && filters == other.filters));
    }
};
using OrderTree = std::map<uintptr_t, OrderNode>;
bool ReadOrderTree(IUnknown *root, OrderTree &result) {
    auto count = reinterpret_cast<FrameCount>(
        GetProcAddress(reinterpret_cast<HMODULE>(gfImage), "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    auto at = reinterpret_cast<FrameAt>(GetProcAddress(reinterpret_cast<HMODULE>(gfImage),
                                                       "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    txtree::Budget budget;
    auto visit = [&](auto &&self, IUnknown *frame, uintptr_t parent, unsigned depth) -> void {
        OrderNode node;
        if (!node.Read(frame)) {
            budget.ChildFailure();
            return;
        }
        auto identity = reinterpret_cast<uintptr_t>(node.object.Get());
        if (!budget.Enter(identity, depth))
            return;
        node.parent = parent;
        unsigned total{};
        auto status = count ? count(frame, &total) : E_NOINTERFACE;
        if (!budget.Children(SUCCEEDED(status), total))
            return;
        for (unsigned i = 0; i < total; ++i) {
            if (!budget.Next())
                break;
            ComPtr<IUnknown> child, canonical;
            if (!at || FAILED(at(frame, static_cast<int>(i), &child)) || !child ||
                FAILED(child->QueryInterface(IID_PPV_ARGS(&canonical))) || !canonical) {
                budget.ChildFailure();
                continue;
            }
            node.children.push_back(reinterpret_cast<uintptr_t>(canonical.Get()));
            self(self, child.Get(), identity, depth + 1);
        }
        result.emplace(identity, std::move(node));
    };
    result.clear();
    visit(visit, root, 0, 0);
    return budget.Complete() && budget.nodes == result.size();
}
bool SameOrderTree(const OrderTree &before, const OrderTree &after, uintptr_t changedParent = 0) {
    if (before.size() != after.size())
        return false;
    for (auto &[id, node] : before) {
        auto found = after.find(id);
        if (found == after.end() || !node.SameState(found->second))
            return false;
        auto expected = node.children, actual = found->second.children;
        if (id == changedParent) {
            std::sort(expected.begin(), expected.end());
            std::sort(actual.begin(), actual.end());
        }
        if (expected != actual)
            return false;
    }
    return true;
}
void OrderIds(std::ostream &stream, const txorder::Chain &chain) {
    stream << '[';
    for (size_t i = 0; i < chain.items.size(); ++i) {
        if (i)
            stream << ',';
        stream << reinterpret_cast<uintptr_t>(chain.items[i].Get());
    }
    stream << ']';
}
} // namespace
