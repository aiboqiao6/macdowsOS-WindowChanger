#pragma once
#include "../../pe-identity.h"
#include <oleauto.h>
#include <array>
#include <vector>

// Passive observer shared by an owned fixture and the bounded research probe.
// No drawing, event routing flags, input, layouts, or original subscriptions
// are changed by a callback. Unknown graphics profiles are only identified.
namespace txobserve {
inline bool ReadableMemory(const void *address, size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    auto begin = reinterpret_cast<uintptr_t>(address);
    return address && begin + size >= begin && VirtualQuery(address, &region, sizeof(region)) &&
           region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           begin + size <= reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
}
struct Arg {
    unsigned code{}, size{};
    BOOL scaled{};
    void *graphics{};
};
struct Record {
    uintptr_t tableRva{};
    bool inGf{}, originalGdi{}, originalD2d{}, canvasRead{};
    SIZE size{};
    RECT clip{};
    unsigned occurrences{};
};
struct Snapshot {
    unsigned stage7{}, stage8{}, paints{}, foreignSource{}, foreignThread{}, invalidPayload{}, count{};
    std::array<Record, 8> records{};
};
using FilterChange = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
inline bool List(IUnknown *button, BYTE *gf, std::vector<IUnknown *> &items) {
    if (!ReadableMemory(button, 0x50) ||
        *reinterpret_cast<void ***>(button) != reinterpret_cast<void **>(gf + 0x1f4c00))
        return false;
    auto base = reinterpret_cast<BYTE *>(button);
    if (*reinterpret_cast<void **>(base + 0x40) != gf + 0x1e9d04)
        return false;
    auto begin = *reinterpret_cast<IUnknown ***>(base + 0x44);
    auto end = *reinterpret_cast<IUnknown ***>(base + 0x48);
    auto capacity = *reinterpret_cast<IUnknown ***>(base + 0x4c);
    auto first = reinterpret_cast<uintptr_t>(begin), last = reinterpret_cast<uintptr_t>(end);
    if (last < first || reinterpret_cast<uintptr_t>(capacity) < last || (last - first) % sizeof(void *) ||
        (last - first) / sizeof(void *) > 128 || (last != first && !ReadableMemory(begin, last - first)))
        return false;
    items.clear();
    if (last != first)
        items.assign(begin, end);
    return true;
}
class Filter : public IDispatch {
    LONG references_{1};
    IUnknown *source_{};
    BYTE *gf_{};
    DWORD thread_{};
    SRWLOCK lock_ = SRWLOCK_INIT;
    Snapshot data_{};
    BOOL(__cdecl *canvasSize_)(void *, SIZE *) {};
    BOOL(__cdecl *canvasClip_)(void *, RECT *) {};

    HRESULT Observe(unsigned stage, IUnknown *source, const Arg *arg) {
        auto savedError = GetLastError();
        AcquireSRWLockExclusive(&lock_);
        if (stage == 7)
            ++data_.stage7;
        else
            ++data_.stage8;
        if (source != source_) {
            ++data_.foreignSource;
        } else if (GetCurrentThreadId() != thread_) {
            ++data_.foreignThread;
        } else if (!ReadableMemory(arg, 12)) {
            ++data_.invalidPayload;
        } else if (arg->code == 0x39a) {
            ++data_.paints;
            if (arg->size < sizeof(Arg) || arg->size > 4096 || !ReadableMemory(arg, sizeof(Arg)) ||
                !ReadableMemory(arg->graphics, sizeof(void *))) {
                ++data_.invalidPayload;
            } else {
                auto table = *reinterpret_cast<void ***>(arg->graphics);
                MEMORY_BASIC_INFORMATION region{};
                Record record;
                record.inGf = table && VirtualQuery(table, &region, sizeof(region)) && region.AllocationBase == gf_;
                record.tableRva =
                    record.inGf ? reinterpret_cast<uintptr_t>(table) - reinterpret_cast<uintptr_t>(gf_) : 0;
                record.originalGdi = record.inGf && table == reinterpret_cast<void **>(gf_ + 0x1ecd6c);
                // Static EndPaint diagnostics and the original factory identify
                // this profile. No D2D method is called by the observer.
                record.originalD2d = record.inGf && table == reinterpret_cast<void **>(gf_ + 0x1eaf9c);
                if (record.originalGdi && canvasSize_ && canvasClip_ && ReadableMemory(arg->graphics, 0x40)) {
                    auto canvas = *reinterpret_cast<void **>(reinterpret_cast<BYTE *>(arg->graphics) + 0xc);
                    if (ReadableMemory(canvas, 0x78))
                        record.canvasRead = canvasSize_(canvas, &record.size) && canvasClip_(canvas, &record.clip);
                }
                unsigned slot{};
                for (; slot < data_.count; ++slot) {
                    auto &current = data_.records[slot];
                    if (current.inGf == record.inGf && current.tableRva == record.tableRva &&
                        current.canvasRead == record.canvasRead && current.size.cx == record.size.cx &&
                        current.size.cy == record.size.cy && EqualRect(&current.clip, &record.clip))
                        break;
                }
                if (slot < data_.records.size()) {
                    if (slot == data_.count) {
                        data_.records[slot] = record;
                        ++data_.count;
                    }
                    ++data_.records[slot].occurrences;
                }
            }
        }
        ReleaseSRWLockExclusive(&lock_);
        SetLastError(savedError);
        return S_OK;
    }

  public:
    Filter(IUnknown *source, BYTE *gf) : source_(source), gf_(gf), thread_(GetCurrentThreadId()) {
        auto graphic = GetModuleHandleW(L"xGraphic32.dll");
        constexpr GUID identity{0x0fd7d531, 0xc786, 0x4d0b, {0xb0, 0xfb, 0x1e, 0x97, 0x99, 0x6f, 0xdf, 0xbc}};
        if (styler::MatchesPdb(graphic, identity, 3)) {
            auto size = GetProcAddress(graphic, "GetCanvasSize"), clip = GetProcAddress(graphic, "GetCanvasClipRect");
            if (reinterpret_cast<BYTE *>(size) == reinterpret_cast<BYTE *>(graphic) + 0x72e50 &&
                reinterpret_cast<BYTE *>(clip) == reinterpret_cast<BYTE *>(graphic) + 0x73310) {
                canvasSize_ = reinterpret_cast<decltype(canvasSize_)>(size);
                canvasClip_ = reinterpret_cast<decltype(canvasClip_)>(clip);
            }
        }
    }
    LONG References() const {
        return InterlockedCompareExchange(const_cast<LONG *>(&references_), 0, 0);
    }
    Snapshot Read() {
        AcquireSRWLockShared(&lock_);
        auto copy = data_;
        ReleaseSRWLockShared(&lock_);
        return copy;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch)
            return E_NOINTERFACE;
        *object = static_cast<IDispatch *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&references_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto remaining = InterlockedDecrement(&references_);
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override {
        if (!count)
            return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **info) override {
        if (info)
            *info = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
                                     UINT *) override {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE Stage7(IUnknown *source, Arg *arg, BOOL *, LRESULT *, BOOL *, BOOL *) {
        return Observe(7, source, arg);
    }
    virtual HRESULT STDMETHODCALLTYPE Stage8(IUnknown *source, Arg *arg, BOOL *, LRESULT *, BOOL *, BOOL *) {
        return Observe(8, source, arg);
    }
    virtual HRESULT STDMETHODCALLTYPE IsNative(BOOL *native) {
        if (!native)
            return E_POINTER;
        *native = TRUE;
        return S_OK;
    }
};
} // namespace txobserve
