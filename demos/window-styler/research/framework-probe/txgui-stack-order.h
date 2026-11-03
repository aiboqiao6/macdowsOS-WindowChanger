#pragma once
#include "txgui-order.h"

// A StackPanel owns three independent orders. Never restore just its linked
// lists: the typed vector drives native layout and must be validated first.
namespace txstack {
using Microsoft::WRL::ComPtr;
struct Snapshot {
    txorder::Snapshot links;
    txorder::Chain typed;
    bool Same(const Snapshot &other) const {
        return links.parent == other.links.parent && links.elements.Same(other.links.elements) &&
               links.frames.Same(other.links.frames) && typed.Same(other.typed);
    }
};
class Api {
    BYTE *image_;
    txorder::Api links_;
    bool Guard(IUnknown *parent) const {
        if (!links_.Guard(parent))
            return false;
        auto table = *reinterpret_cast<void ***>(parent);
        return table == reinterpret_cast<void **>(image_ + 0x220f58) && table[0x32c / 4] == image_ + 0x17cc30 &&
               table[0x338 / 4] == image_ + 0x17caf0 && table[0x33c / 4] == image_ + 0x17cb50 &&
               table[52] == image_ + 0x4090 && table[53] == image_ + 0x40d0;
    }
    static bool At(IUnknown *parent, size_t index, ComPtr<IUnknown> &item) {
        using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int, IUnknown **);
        item.Reset();
        return SUCCEEDED(reinterpret_cast<Get>((*reinterpret_cast<void ***>(parent))[0x32c / 4])(
            parent, static_cast<int>(index), &item));
    }
    bool Valid(const Snapshot &value) const {
        return Guard(value.links.parent.Get()) && txorder::Api::SameSet(value.links.elements, value.links.frames) &&
               txorder::Api::SameSet(value.links.elements, value.typed);
    }
    static bool Move(IUnknown *parent, IUnknown *child, size_t index) {
        using Put = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int);
        return SUCCEEDED(reinterpret_cast<Put>((*reinterpret_cast<void ***>(parent))[0x33c / 4])(
            parent, child, static_cast<int>(index)));
    }
    bool Apply(const Snapshot &saved) const {
        auto parent = saved.links.parent.Get();
        for (size_t i = 0; i < saved.typed.items.size(); ++i) {
            ComPtr<IUnknown> current;
            if (!At(parent, i, current) ||
                (current != saved.typed.items[i] && !Move(parent, saved.typed.items[i].Get(), i)))
                return false;
        }
        return links_.RestoreLinks(saved.links, true);
    }

  public:
    explicit Api(BYTE *image) : image_(image), links_(image) {}
    bool Capture(IUnknown *parent, Snapshot &result) const {
        Snapshot value;
        if (!Guard(parent) || !links_.CaptureLinks(parent, value.links, true))
            return false;
        for (size_t i = 0; i < value.links.frames.items.size(); ++i) {
            ComPtr<IUnknown> item;
            if (!At(parent, i, item) || !item)
                return false;
            value.typed.items.push_back(item);
        }
        ComPtr<IUnknown> extra;
        if (At(parent, value.typed.items.size(), extra) || extra || !Valid(value))
            return false;
        result = std::move(value);
        return true;
    }
    bool Restore(const Snapshot &saved) const {
        Snapshot current;
        if (!Valid(saved) || !Capture(saved.links.parent.Get(), current) ||
            !txorder::Api::SameSet(current.typed, saved.typed))
            return false;
        try {
            Snapshot after;
            if (Apply(saved) && Capture(saved.links.parent.Get(), after) && saved.Same(after))
                return true;
        } catch (...) {
        }
        Apply(current);
        return false;
    }
    bool InsertAt(IUnknown *parent, IUnknown *child, size_t index) const {
        Snapshot current;
        ComPtr<IUnknown> owner;
        if (!Capture(parent, current) || !links_.Guard(child) || index > current.typed.items.size() ||
            !txorder::Api::Read(child, 0x100, owner) || owner)
            return false;
        using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int);
        return SUCCEEDED(reinterpret_cast<Insert>((*reinterpret_cast<void ***>(parent))[0x338 / 4])(
            parent, child, static_cast<int>(index)));
    }
    bool Remove(IUnknown *parent, IUnknown *child) const {
        Snapshot current;
        if (!Capture(parent, current) || std::none_of(current.typed.items.begin(), current.typed.items.end(),
                                                      [&](const auto &item) { return item.Get() == child; }))
            return false;
        using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
        return SUCCEEDED(reinterpret_cast<Remove>((*reinterpret_cast<void ***>(parent))[53])(parent, child));
    }
};
} // namespace txstack
