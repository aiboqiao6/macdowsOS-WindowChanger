#pragma once
#include "../../pe-identity.h"
#include <wrl/client.h>
#include <array>

namespace txlayout {
using Microsoft::WRL::ComPtr;
struct State {
    RECT rect{}, margin{}, padding{};
    std::array<LONG, 12> policy{};
    bool Same(const State &other) const {
        return EqualRect(&rect, &other.rect) && EqualRect(&margin, &other.margin) &&
               EqualRect(&padding, &other.padding) && policy == other.policy;
    }
};
class Api {
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
        if (!ready_ || thread_ != GetCurrentThreadId() || !Readable(object, 12))
            return false;
        auto primary = *reinterpret_cast<void ***>(object);
        auto native = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(object) + 8);
        if (!Readable(primary, 0x2fc) || !Readable(native, 0x22c) || !InImage(primary) || !InImage(native))
            return false;
        for (auto [offset, rva] : {std::pair{0x1e0u, 0x2dc0u},
                                   {0x1e4u, 0x2d50u},
                                   {0x208u, 0x2e70u},
                                   {0x20cu, 0x2e00u},
                                   {0x210u, 0x2f20u},
                                   {0x214u, 0x2eb0u},
                                   {0x2f4u, 0x4430u},
                                   {0x2f8u, 0x4450u}})
            if (primary[offset / 4] != image_ + rva)
                return false;
        for (auto [offset, rva] : {std::pair{0x90u, 0x79860u},
                                   {0x94u, 0x7c490u},
                                   {0x98u, 0x7ef90u},
                                   {0x9cu, 0x80310u},
                                   {0xa0u, 0x7f070u},
                                   {0xa4u, 0x80490u},
                                   {0x228u, 0x799f0u}})
            if (native[offset / 4] != image_ + rva)
                return false;
        return true;
    }
    static bool Put(IUnknown *object, size_t offset, RECT value) {
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT);
        return SUCCEEDED(reinterpret_cast<Function>((*reinterpret_cast<void ***>(object))[offset / 4])(object, value));
    }

  public:
    explicit Api(BYTE *image) : image_(image), thread_(GetCurrentThreadId()) {
        constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
        ready_ = styler::MatchesPdb(reinterpret_cast<HMODULE>(image), identity, 24);
    }
    bool Read(IUnknown *object, State &state) const {
        if (!Guard(object))
            return false;
        using Rect = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
        using Policy = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, LONG *);
        auto table = *reinterpret_cast<void ***>(object);
        return SUCCEEDED(reinterpret_cast<Rect>(table[0x1e0 / 4])(object, &state.rect)) &&
               SUCCEEDED(reinterpret_cast<Rect>(table[0x208 / 4])(object, &state.margin)) &&
               SUCCEEDED(reinterpret_cast<Rect>(table[0x210 / 4])(object, &state.padding)) &&
               SUCCEEDED(reinterpret_cast<Policy>(table[0x2f8 / 4])(object, state.policy.data()));
    }
    bool Margin(IUnknown *object, RECT value) const {
        return Guard(object) && Put(object, 0x20c, value);
    }
    bool Padding(IUnknown *object, RECT value) const {
        return Guard(object) && Put(object, 0x214, value);
    }
    bool Rect(IUnknown *object, RECT value) const {
        return Guard(object) && Put(object, 0x1e4, value);
    }
    bool PhysicalSize(IUnknown *object, LONG width, LONG height) const {
        if (!Guard(object))
            return false;
        auto native = reinterpret_cast<BYTE *>(object) + 8;
        auto table = *reinterpret_cast<void ***>(native);
        if (table[0xb0 / 4] != image_ + 0x805e0)
            return false;
        using Function = HRESULT(__thiscall *)(void *, int, int);
        return SUCCEEDED(reinterpret_cast<Function>(table[0xb0 / 4])(native, width, height));
    }
    bool Size(IUnknown *object, int width, int height) const {
        if (!Guard(object))
            return false;
        auto table = *reinterpret_cast<void ***>(object);
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int, int);
        // The public wrapper converts DIP to the original physical policy.
        auto native = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(object) + 8);
        return table[0x1ec / 4] == image_ + 0x2fa0 && native[0xb0 / 4] == image_ + 0x805e0 &&
               SUCCEEDED(reinterpret_cast<Function>(table[0x1ec / 4])(object, width, height));
    }
    bool Anchor(IUnknown *object, const wchar_t *text) const {
        if (!Guard(object))
            return false;
        auto table = *reinterpret_cast<void ***>(object);
        auto native = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(object) + 8);
        if (table[0x21c / 4] != image_ + 0x30b0 || native[0xd0 / 4] != image_ + 0x7f800)
            return false;
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR);
        auto value = SysAllocString(text);
        auto status = value ? reinterpret_cast<Function>(table[0x21c / 4])(object, value) : E_OUTOFMEMORY;
        SysFreeString(value);
        return SUCCEEDED(status);
    }
    bool Layout(IUnknown *parent, IUnknown *child = nullptr) const {
        if (!Guard(parent) || (child && !Guard(child)))
            return false;
        auto native = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(parent) + 8);
        auto primary = *reinterpret_cast<void ***>(parent);
        bool ordinary = native[0x224 / 4] == image_ + 0x7a440;
        bool stack = primary == reinterpret_cast<void **>(image_ + 0x220f58) && native[0x224 / 4] == image_ + 0x17f060;
        if (!ordinary && !stack)
            return false;
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
        return SUCCEEDED(reinterpret_cast<Function>((*reinterpret_cast<void ***>(parent))[0x2f4 / 4])(parent, child));
    }
};
class Scope {
    Api &api_;
    ComPtr<IUnknown> parent_, frame_;
    State saved_{};
    bool ready_{}, armed_{};
    bool Restore() const {
        bool ok = api_.Padding(frame_.Get(), saved_.padding);
        ok = api_.Margin(frame_.Get(), saved_.margin) && ok;
        ok = api_.Layout(parent_.Get(), frame_.Get()) && ok;
        ok = api_.Rect(frame_.Get(), saved_.rect) && ok;
        return api_.Layout(frame_.Get()) && ok;
    }

  public:
    Scope(Api &api, IUnknown *parent, IUnknown *frame) : api_(api), parent_(parent), frame_(frame) {
        State parentState;
        using GetParent = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown **);
        ComPtr<IUnknown> actualParent;
        if (!api.Read(parent, parentState) || !api.Read(frame, saved_))
            return;
        auto table = *reinterpret_cast<void ***>(frame);
        ready_ =
            SUCCEEDED(reinterpret_cast<GetParent>(table[0x100 / 4])(frame, &actualParent)) && actualParent == parent_;
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
    bool Ready() const {
        return ready_;
    }
    const State &Original() const {
        return saved_;
    }
    bool ExpandLeft() {
        if (!ready_ || saved_.margin.left <= 0 || saved_.margin.left > 512 || saved_.rect.left != saved_.margin.left ||
            saved_.rect.right <= saved_.rect.left || saved_.rect.bottom <= saved_.rect.top || saved_.padding.left < 0 ||
            saved_.padding.left > 4096 - saved_.margin.left)
            return false;
        armed_ = true;
        auto margin = saved_.margin, padding = saved_.padding;
        padding.left += margin.left;
        margin.left = 0;
        return api_.Padding(frame_.Get(), padding) && api_.Margin(frame_.Get(), margin) &&
               api_.Layout(parent_.Get(), frame_.Get()) && api_.Layout(frame_.Get());
    }
    bool Finish() {
        if (!armed_)
            return ready_;
        State actual;
        if (Restore() && api_.Read(frame_.Get(), actual) && actual.Same(saved_)) {
            armed_ = false;
            return true;
        }
        return false;
    }
    ~Scope() noexcept {
        if (armed_) {
            try {
                Restore();
            } catch (...) {
            }
        }
    }
};
} // namespace txlayout
