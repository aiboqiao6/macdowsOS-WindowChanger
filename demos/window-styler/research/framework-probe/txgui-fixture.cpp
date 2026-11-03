#include "../../pe-identity.h"
#include "txgui-events.h"
#include "txgui-caption.h"
#include <oleauto.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

using Microsoft::WRL::ComPtr;
int failures{};
unsigned checks{};
BYTE *fixtureImage{};
constexpr GUID SysButtonId{0xeb0c3b5c, 0x0e7c, 0x44a7, {0x80, 0x37, 0x0f, 0x71, 0x22, 0x63, 0x27, 0x82}};
constexpr GUID ButtonId{0x70551436, 0x19c4, 0x4f25, {0xbf, 0xc8, 0x32, 0x2d, 0x86, 0xae, 0x1a, 0x9c}};
constexpr GUID StackPanelId{0xfcaa4ce1, 0x0cce, 0x4d72, {0xbc, 0x81, 0xe9, 0x5f, 0x3c, 0xcb, 0x86, 0xd9}};

BOOL __cdecl FixtureFactory(REFCLSID clsid, REFIID iid, void **object) {
    if (!object || !fixtureImage)
        return FALSE;
    *object = nullptr;
    size_t creator = clsid == SysButtonId    ? 0xe69a0
                     : clsid == ButtonId     ? 0x94ea0
                     : clsid == StackPanelId ? 0x17ce70
                                             : 0;
    if (!creator)
        return FALSE;
    ComPtr<IUnknown> created;
    auto result = reinterpret_cast<HRESULT(__cdecl *)(IUnknown **)>(fixtureImage + creator)(&created);
    return SUCCEEDED(result) && created && SUCCEEDED(created->QueryInterface(iid, object));
}

bool Check(bool passed, const char *name) {
    ++checks;
    std::cout << (passed ? "PASS " : "FAIL ") << name << std::endl;
    if (!passed)
        ++failures;
    return passed;
}

bool Readable(const void *address, size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    return address && VirtualQuery(address, &region, sizeof(region)) && region.State == MEM_COMMIT &&
           !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           reinterpret_cast<uintptr_t>(address) + size <=
               reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
}

struct FrameState {
    RECT rect{}, margin{}, padding{};
    BOOL hidden{}, enabled{};
};
bool ReadFrame(IUnknown *frame, FrameState &state) {
    auto table = *reinterpret_cast<void ***>(frame);
    if (!Readable(table, 134 * sizeof(void *)))
        return false;
    using RectGetter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
    using BoolGetter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BOOL *);
    return SUCCEEDED(reinterpret_cast<RectGetter>(table[0x1e0 / 4])(frame, &state.rect)) &&
           SUCCEEDED(reinterpret_cast<RectGetter>(table[0x208 / 4])(frame, &state.margin)) &&
           SUCCEEDED(reinterpret_cast<RectGetter>(table[0x210 / 4])(frame, &state.padding)) &&
           SUCCEEDED(reinterpret_cast<BoolGetter>(table[0x158 / 4])(frame, &state.hidden)) &&
           SUCCEEDED(reinterpret_cast<BoolGetter>(table[0x168 / 4])(frame, &state.enabled));
}
std::wstring RectText(const RECT &rect) {
    return std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top) + L"," + std::to_wstring(rect.right) + L"," +
           std::to_wstring(rect.bottom);
}
bool Equal(const FrameState &left, const FrameState &right) {
    return EqualRect(&left.rect, &right.rect) && EqualRect(&left.margin, &right.margin) &&
           EqualRect(&left.padding, &right.padding) && left.hidden == right.hidden && left.enabled == right.enabled;
}
int WindowCount() {
    int count{};
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetCurrentProcessId())
                ++*reinterpret_cast<int *>(param);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
    return count;
}
void EventTransactions(IUnknown *button, IUnknown *group, BYTE *image) {
    constexpr GUID eventsId{0x6ccbbc7b, 0x5c0a, 0x49aa, {0xb1, 0x99, 0x67, 0x33, 0xfd, 0xcd, 0x7d, 0xd4}};
    constexpr GUID sinkId{0x78b5f698, 0x78d3, 0x46f0, {0x88, 0x34, 0xe7, 0x01, 0x76, 0xd0, 0x34, 0x40}};
    auto empty = txevents::Inspect(button, image);
    if (!Check(empty.profile && empty.complete && empty.numeric.empty() && empty.named.empty() &&
                   empty.listeners.empty(),
               "new detached Button has valid empty original event containers"))
        return;
    ComPtr<IUnknown> events, sink;
    if (!Check(SUCCEEDED(button->QueryInterface(eventsId, &events)) && SUCCEEDED(group->QueryInterface(sinkId, &sink)),
               "owned controls expose the original event source and SysButton notifier interfaces"))
        return;
    auto table = *reinterpret_cast<void ***>(events.Get());
    if (!Check(table[7] == image + 0x93910 && table[9] == image + 0x93950 && table[14] == image + 0x93a10 &&
                   table[16] == image + 0x93a30,
               "event subscription methods match the independently verified native wrappers"))
        return;
    using AddNumeric = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned, IUnknown *);
    using AddNamed = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, const char *, IUnknown *);
    using AddListener = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using Clear = HRESULT(STDMETHODCALLTYPE *)(IUnknown *);
    struct Cleanup {
        IUnknown *events;
        Clear clear;
        ~Cleanup() {
            clear(events);
        }
    } cleanup{events.Get(), reinterpret_cast<Clear>(table[14])};
    FrameState before{}, after{};
    ReadFrame(button, before);
    auto windows = WindowCount();
    Check(txcaption::Hint(L"Minimize") == txcaption::Intent::Minimize &&
              txcaption::Hint(L"\x6700\x5c0f\x5316") == txcaption::Intent::Minimize,
          "standard original minimize hints provide intent without business names");
    Check(txcaption::Hint(L"\x6700\x5927\x5316") == txcaption::Intent::MaximizeOrRestore &&
              txcaption::Hint(L"Restore Down") == txcaption::Intent::MaximizeOrRestore,
          "standard original maximize and restore hints share one intent");
    Check(txcaption::Hint(L"\x5173\x95ed") == txcaption::Intent::Close &&
              txcaption::Hint(L"Close document") == txcaption::Intent::Unknown &&
              txcaption::Hint(L"business_close_button") == txcaption::Intent::Unknown &&
              txcaption::Hint(nullptr) == txcaption::Intent::Unknown,
          "hint matching rejects substrings business-control names and missing evidence");
    auto embedded = SysAllocStringLen(L"Close\0document", 14);
    Check(embedded && txcaption::Hint(embedded, SysStringLen(embedded)) == txcaption::Intent::Unknown,
          "hint matching rejects embedded-NUL BSTRs instead of accepting a prefix");
    SysFreeString(embedded);
    for (unsigned id : {0x1003u, 7u, 42u, 9u, 100u})
        Check(SUCCEEDED(reinterpret_cast<AddNumeric>(table[7])(events.Get(), id, sink.Get())),
              "subscribe a controlled numeric event on an owned real Button");
    for (auto name : {"onClick", "fixture_long_named_event_exercises_heap_string"})
        Check(SUCCEEDED(reinterpret_cast<AddNamed>(table[16])(events.Get(), name, sink.Get())),
              "subscribe controlled short and long named events on an owned Button");
    Check(SUCCEEDED(reinterpret_cast<AddListener>(table[9])(events.Get(), sink.Get())),
          "subscribe an owned catch-all listener");
    auto bindings = txevents::Inspect(button, image);
    Check(bindings.complete && bindings.errors == 0, "populated original event trees pass structural validation");
    Check(bindings.numeric.size() == 5 && bindings.named.size() == 2 && bindings.listeners.size() == 1,
          "read-only inspector finds exact numeric named and listener counts");
    std::vector<uint32_t> ids;
    for (const auto &binding : bindings.numeric)
        ids.push_back(binding.id);
    Check(ids == std::vector<uint32_t>({7, 9, 42, 100, 0x1003}),
          "numeric event keys are decoded in native sorted order");
    auto second = txevents::Inspect(button, image);
    Check(second.complete && second.numeric == bindings.numeric && second.named == bindings.named &&
              second.listeners == bindings.listeners,
          "repeated inspection leaves every subscription unchanged");
    unsigned clicks{}, names{};
    for (const auto &binding : bindings.numeric)
        if (binding.id == 0x1003 && binding.sink == reinterpret_cast<uintptr_t>(sink.Get()) &&
            binding.numericCallback == reinterpret_cast<uintptr_t>(image + 0xe6490))
            ++clicks;
    for (const auto &binding : bindings.named)
        if ((binding.name == "onClick" || binding.name == "fixture_long_named_event_exercises_heap_string") &&
            binding.sink == reinterpret_cast<uintptr_t>(sink.Get()))
            ++names;
    Check(clicks == 1, "read-only inspector identifies the real SysButton click notifier without invoking it");
    Check(names == 2, "read-only inspector decodes both native SSO and heap-backed event names");
    Check(ReadFrame(button, after) && Equal(before, after) && WindowCount() == windows,
          "event inspection leaves original frame state and HWND count unchanged");
    Check(SUCCEEDED(cleanup.clear(events.Get())), "remove all fixture-owned subscriptions through the original API");
    auto restored = txevents::Inspect(button, image);
    Check(restored.complete && restored.numeric.empty() && restored.named.empty() && restored.listeners.empty(),
          "event containers return exactly to their original empty state");
    bool added = true;
    for (unsigned i = 0; i < 129; ++i)
        added &= SUCCEEDED(reinterpret_cast<AddNumeric>(table[7])(events.Get(), 2000 + i, sink.Get()));
    Check(added, "create an over-budget event map only on an owned fixture Button");
    auto oversized = txevents::Inspect(button, image);
    Check(oversized.profile && !oversized.complete && oversized.errors != 0,
          "read-only inspector rejects over-budget maps before traversing them");
    cleanup.clear(events.Get());
    auto cleared = txevents::Inspect(button, image);
    Check(cleared.complete && cleared.numeric.empty(), "native cleanup restores the over-budget fixture map");
}
void TooltipTransactions(IUnknown *button, BYTE *image) {
    auto table = *reinterpret_cast<void ***>(button);
    if (!Check(table[0x1bc / 4] == image + 0x3690 && table[0x1b8 / 4] == image + 0x3670,
               "original tooltip getter and setter match the verified frame wrappers"))
        return;
    using Getter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR *);
    using Setter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR);
    auto get = reinterpret_cast<Getter>(table[0x1bc / 4]);
    auto put = reinterpret_cast<Setter>(table[0x1b8 / 4]);
    BSTR original{};
    if (!Check(SUCCEEDED(get(button, &original)), "save original detached Button tooltip"))
        return;
    struct Restore {
        IUnknown *button;
        Setter put;
        BSTR original;
        ~Restore() {
            put(button, original);
            SysFreeString(original);
        }
    } restore{button, put, original};
    FrameState before{}, after{};
    ReadFrame(button, before);
    auto windows = WindowCount();
    for (auto text : {L"Minimize", L"\x6700\x5927\x5316", L"\x5173\x95ed", L""}) {
        auto value = SysAllocString(text);
        Check(value && SUCCEEDED(put(button, value)), "set tooltip through the original Button property setter");
        SysFreeString(value);
        BSTR actual{};
        Check(SUCCEEDED(get(button, &actual)) && std::wstring(actual ? actual : L"", SysStringLen(actual)) == text,
              "original tooltip getter reports exact Unicode text");
        SysFreeString(actual);
    }
    Check(SUCCEEDED(put(button, original)), "restore the original tooltip through its own setter");
    BSTR restored{};
    Check(SUCCEEDED(get(button, &restored)) && std::wstring(restored ? restored : L"", SysStringLen(restored)) ==
                                                   std::wstring(original ? original : L"", SysStringLen(original)),
          "original tooltip is restored exactly");
    SysFreeString(restored);
    Check(ReadFrame(button, after) && Equal(before, after) && WindowCount() == windows,
          "tooltip transactions preserve original geometry state and HWND count");
}
void Transactions(HMODULE gf, IUnknown *frame) {
    using SetAttribute = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    auto set = reinterpret_cast<SetAttribute>(
        GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    if (!set) {
        Check(false, "framework attribute setter export exists");
        return;
    }
    FrameState original{};
    if (!ReadFrame(frame, original)) {
        Check(false, "read original detached frame properties");
        return;
    }
    Check(true, "read original detached frame properties");
    int windows = WindowCount();
    for (const RECT padding : {RECT{84, 0, 0, 0}, RECT{12, 3, 7, 9}, RECT{0, 0, 0, 0}}) {
        auto text = RectText(padding);
        Check(SUCCEEDED(set(frame, L"padding", text.c_str())), "set padding through original framework API");
        FrameState actual{};
        Check(ReadFrame(frame, actual) && EqualRect(&actual.padding, &padding), "framework reports requested padding");
        Check(EqualRect(&actual.rect, &original.rect), "padding preserves detached frame outer rectangle");
    }
    const RECT margin{3, 4, 5, 6}, rect{10, 20, 630, 440};
    Check(SUCCEEDED(set(frame, L"margin", RectText(margin).c_str())), "set margin through original framework API");
    FrameState actual{};
    Check(ReadFrame(frame, actual) && EqualRect(&actual.margin, &margin), "framework reports requested margin");
    Check(SUCCEEDED(set(frame, L"rect", RectText(rect).c_str())), "set rectangle through original framework API");
    Check(ReadFrame(frame, actual) && EqualRect(&actual.rect, &rect), "framework reports requested rectangle");
    Check(SUCCEEDED(set(frame, L"hidden", L"true")), "hide detached frame through framework API");
    Check(ReadFrame(frame, actual) && actual.hidden, "framework reports hidden state");
    Check(SUCCEEDED(set(frame, L"enable", L"false")), "disable detached frame through framework API");
    Check(ReadFrame(frame, actual) && !actual.enabled, "framework reports disabled state");
    auto restore = [&] {
        bool ok = SUCCEEDED(set(frame, L"rect", RectText(original.rect).c_str()));
        ok &= SUCCEEDED(set(frame, L"margin", RectText(original.margin).c_str()));
        ok &= SUCCEEDED(set(frame, L"padding", RectText(original.padding).c_str()));
        ok &= SUCCEEDED(set(frame, L"hidden", original.hidden ? L"true" : L"false"));
        ok &= SUCCEEDED(set(frame, L"enable", original.enabled ? L"true" : L"false"));
        return ok;
    };
    Check(restore(), "restore every modified property through framework setters");
    Check(ReadFrame(frame, actual) && Equal(actual, original), "restore exact original framework state");
    Check(restore() && ReadFrame(frame, actual) && Equal(actual, original), "repeated restore is idempotent");
    Check(WindowCount() == windows, "detached frame transactions create no top-level HWNDs");
}

void ChildTransactions(HMODULE gf, IUnknown *parent, IClassFactory *factory) {
    auto table = *reinterpret_cast<void ***>(parent);
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using Count = HRESULT(__cdecl *)(IUnknown *, unsigned *);
    using At = HRESULT(__cdecl *)(IUnknown *, int, IUnknown **);
    auto insert = reinterpret_cast<Insert>(table[52]);
    auto remove = reinterpret_cast<Remove>(table[53]);
    auto count = reinterpret_cast<Count>(GetProcAddress(gf, "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    auto at = reinterpret_cast<At>(GetProcAddress(gf, "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    unsigned original{};
    Check(SUCCEEDED(count(parent, &original)) && original == 0, "fixture starts with an empty original child tree");
    ComPtr<IUnknown> content[2];
    for (auto &child : content) {
        Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&child))), "create pre-existing fixture content");
        if (!child)
            return;
        Check(SUCCEEDED(insert(parent, child.Get(), nullptr)), "populate original child tree before injection");
    }
    Check(SUCCEEDED(count(parent, &original)) && original == 2,
          "original content is present before inserting new controls");
    ComPtr<IUnknown> children[3];
    for (auto &child : children)
        Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&child))),
              "create an independent real GF child frame");
    if (!children[0] || !children[1] || !children[2])
        return;
    for (int i = 0; i < 3; ++i) {
        Check(SUCCEEDED(insert(parent, children[i].Get(), nullptr)), "insert child through original GF frame method");
        unsigned current{};
        Check(SUCCEEDED(count(parent, &current)) && current == original + static_cast<unsigned>(i + 1),
              "original framework child enumeration sees inserted child");
    }
    for (int i = 0; i < 3; ++i) {
        ComPtr<IUnknown> actual;
        Check(SUCCEEDED(at(parent, static_cast<int>(original) + i, &actual)) && actual.Get() == children[i].Get(),
              "framework preserves original insertion order and object identity");
    }
    for (int i = 2; i >= 0; --i)
        Check(SUCCEEDED(remove(parent, children[i].Get())), "remove inserted child through original GF frame method");
    unsigned after{};
    Check(SUCCEEDED(count(parent, &after)) && after == original, "rollback restores the exact original child count");
    for (int i = 0; i < 2; ++i) {
        ComPtr<IUnknown> actual;
        Check(SUCCEEDED(at(parent, i, &actual)) && actual.Get() == content[i].Get(),
              "rollback preserves the original content objects and their order");
    }
    for (auto &child : content)
        Check(SUCCEEDED(remove(parent, child.Get())), "clean up fixture-owned original content");
}

void SystemButtonTransactions(HMODULE gf, IUnknown *group) {
    auto image = reinterpret_cast<BYTE *>(gf);
    auto table = *reinterpret_cast<void ***>(group);
    if (!Readable(table, 229 * sizeof(void *)) || table[227] != image + 0xe6360 || table[228] != image + 0xe6390) {
        Check(false, "original SysButton mask methods match the verified ABI");
        return;
    }
    using SetMask = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned);
    using GetMask = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned *);
    auto set = reinterpret_cast<SetMask>(table[227]);
    auto get = reinterpret_cast<GetMask>(table[228]);
    unsigned original{};
    if (!Check(SUCCEEDED(get(group, &original)), "read the original system-button visibility mask"))
        return;
    FrameState before{};
    Check(ReadFrame(group, before), "save original SysButton frame state before mask transactions");
    auto windows = WindowCount();
    for (unsigned mask : {0u, 1u, 2u, 4u, 3u, 5u, 6u, 7u}) {
        Check(SUCCEEDED(set(group, mask)), "set visibility using the original SysButton method");
        unsigned actual{};
        Check(SUCCEEDED(get(group, &actual)) && actual == mask, "original getter reports the requested button mask");
        for (unsigned index = 0; index < 3; ++index) {
            auto child = *reinterpret_cast<IUnknown **>(reinterpret_cast<BYTE *>(group) + 0x17c + index * 4);
            FrameState state{};
            Check(child && ReadFrame(child, state) && state.hidden == !(mask & (1u << index)),
                  "mask controls the intended original inner button visibility");
        }
    }
    Check(SUCCEEDED(set(group, original)), "restore original system-button mask");
    unsigned actual{};
    Check(SUCCEEDED(get(group, &actual)) && actual == original, "original system-button mask is restored exactly");
    FrameState after{};
    Check(ReadFrame(group, after) && Equal(before, after),
          "system-button mask transaction preserves outer frame state");
    Check(WindowCount() == windows, "system-button mask transaction creates no top-level HWNDs");
}

void StackPanelTransactions(HMODULE gf, IUnknown *parent) {
    auto image = reinterpret_cast<BYTE *>(gf);
    auto table = *reinterpret_cast<void ***>(parent);
    if (!Check(table == reinterpret_cast<void **>(image + 0x220f58) && table[0x2f4 / 4] == image + 0x4430 &&
                   table[0x30c / 4] == image + 0x93b00 && table[0x310 / 4] == image + 0x17ca90 &&
                   table[0x32c / 4] == image + 0x17cc30 && table[0x338 / 4] == image + 0x17caf0,
               "owned StackPanel matches the original typed orientation layout and indexed-item ABI"))
        return;
    using SetAttribute = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    using GetInt = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int *);
    using PutInt = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int);
    using Layout = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using InsertItem = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int);
    using ItemAt = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int, IUnknown **);
    using Count = HRESULT(__cdecl *)(IUnknown *, unsigned *);
    auto set = reinterpret_cast<SetAttribute>(
        GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    auto count = reinterpret_cast<Count>(GetProcAddress(gf, "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    using FrameAt = HRESULT(__cdecl *)(IUnknown *, int, IUnknown **);
    auto enumerate =
        reinterpret_cast<FrameAt>(GetProcAddress(gf, "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    auto orientation = reinterpret_cast<GetInt>(table[0x30c / 4]);
    auto putOrientation = reinterpret_cast<PutInt>(table[0x310 / 4]);
    auto layout = reinterpret_cast<Layout>(table[0x2f4 / 4]);
    auto insert = reinterpret_cast<Insert>(table[52]);
    auto remove = reinterpret_cast<Remove>(table[53]);
    auto insertItem = reinterpret_cast<InsertItem>(table[0x338 / 4]);
    auto itemAt = reinterpret_cast<ItemAt>(table[0x32c / 4]);
    if (!Check(set && count && enumerate, "original frame property and child enumeration exports exist"))
        return;
    constexpr GUID frameId{0xdda0ed51, 0xb482, 0x4a5b, {0xa2, 0xb4, 0x31, 0xa3, 0x17, 0x68, 0xcd, 0x1d}};
    auto getFactory =
        reinterpret_cast<HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **)>(GetProcAddress(gf, "DllGetClassObject"));
    ComPtr<IClassFactory> factory;
    if (!Check(getFactory && SUCCEEDED(getFactory(frameId, IID_PPV_ARGS(&factory))) && factory,
               "obtain the original Frame class factory for pre-existing owned content"))
        return;
    FrameState original{};
    int originalOrientation{};
    if (!Check(ReadFrame(parent, original) && SUCCEEDED(orientation(parent, &originalOrientation)),
               "save original StackPanel geometry padding and orientation"))
        return;
    auto windows = WindowCount();
    std::array<ComPtr<IUnknown>, 2> content;
    ComPtr<IUnknown> caption;
    std::array<ComPtr<IUnknown>, 3> lights;
    struct Cleanup {
        IUnknown *parent;
        Remove remove;
        SetAttribute set;
        PutInt orientation;
        FrameState original;
        int originalOrientation;
        std::array<ComPtr<IUnknown>, 2> &content;
        ComPtr<IUnknown> &caption;
        std::array<ComPtr<IUnknown>, 3> &lights;
        ~Cleanup() {
            if (caption) {
                auto childTable = *reinterpret_cast<void ***>(caption.Get());
                auto removeLight = reinterpret_cast<Remove>(childTable[53]);
                for (auto &light : lights)
                    if (light)
                        removeLight(caption.Get(), light.Get());
                remove(parent, caption.Get());
            }
            for (auto &child : content)
                if (child)
                    remove(parent, child.Get());
            orientation(parent, originalOrientation);
            set(parent, L"rect", RectText(original.rect).c_str());
            set(parent, L"padding", RectText(original.padding).c_str());
        }
    } cleanup{parent, remove, set, putOrientation, original, originalOrientation, content, caption, lights};
    auto dimensions = [&](IUnknown *frame, int width, int height) {
        return SUCCEEDED(set(frame, L"rect", RectText(RECT{0, 0, width, height}).c_str()));
    };
    Check(SUCCEEDED(putOrientation(parent, 1)), "select horizontal native StackPanel layout through its own setter");
    int actualOrientation{};
    Check(SUCCEEDED(orientation(parent, &actualOrientation)) && actualOrientation == 1,
          "original orientation getter confirms horizontal layout");
    Check(dimensions(parent, 320, 64) && SUCCEEDED(set(parent, L"padding", L"8,4,8,4")),
          "configure a detached existing title container with explicit dimensions and padding");
    for (size_t i = 0; i < content.size(); ++i) {
        if (!Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&content[i]))) && content[i],
                   "create a real pre-existing content Frame"))
            return;
        Check(dimensions(content[i].Get(), i == 0 ? 80 : 100, 32) &&
                  SUCCEEDED(insert(parent, content[i].Get(), nullptr)),
              "populate original content through native frame insertion");
    }
    Check(SUCCEEDED(layout(parent, nullptr)), "run the original container layout on the pre-existing tree");
    std::array<FrameState, 2> baseline;
    for (size_t i = 0; i < content.size(); ++i) {
        Check(ReadFrame(content[i].Get(), baseline[i]), "capture original content geometry after native layout");
        std::wcout << L"baselineContent=" << i << L"; rect=" << RectText(baseline[i].rect) << std::endl;
    }
    const RECT expectedFirst{8, 4, 88, 36};
    Check(EqualRect(&baseline[0].rect, &expectedFirst), "baseline content has a valid positive native layout");
    if (!Check(FixtureFactory(StackPanelId, StackPanelId, reinterpret_cast<void **>(caption.GetAddressOf())) && caption,
               "create a second original StackPanel for the three-light slot"))
        return;
    auto captionTable = *reinterpret_cast<void ***>(caption.Get());
    Check(dimensions(caption.Get(), 84, 32) &&
              SUCCEEDED(reinterpret_cast<PutInt>(captionTable[0x310 / 4])(caption.Get(), 1)),
          "configure a bounded horizontal native light group");
    for (auto &light : lights) {
        if (!Check(FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(light.GetAddressOf())) && light,
                   "create a real framework Button for the owned light group"))
            return;
        Check(dimensions(light.Get(), 28, 32) &&
                  SUCCEEDED(reinterpret_cast<Insert>(captionTable[52])(caption.Get(), light.Get(), nullptr)),
              "place the real light Button through the original container API");
    }
    Check(SUCCEEDED(insertItem(parent, caption.Get(), 0)) && SUCCEEDED(layout(parent, nullptr)),
          "reserve the left slot using original indexed layout insertion, not manual position writes");
    ComPtr<IUnknown> first;
    Check(SUCCEEDED(itemAt(parent, 0, &first)) && first.Get() == caption.Get(),
          "typed StackPanel order places the inserted caption group first");
    first.Reset();
    unsigned children{};
    Check(SUCCEEDED(count(parent, &children)) && children == 3, "original frame tree contains exactly one new group");
    for (int i = 0; i < 2; ++i) {
        ComPtr<IUnknown> child;
        Check(SUCCEEDED(enumerate(parent, i, &child)) && child.Get() == content[i].Get(),
              "insertion preserves the original frame-tree content identities and order");
    }
    auto verify = [&] {
        bool ok = true;
        FrameState group{}, row{};
        ok &= ReadFrame(caption.Get(), group) && group.rect.right - group.rect.left == 84 &&
              group.rect.bottom - group.rect.top == 32;
        ok &= ReadFrame(parent, row) && group.rect.left >= row.padding.left && group.rect.top >= row.padding.top;
        for (size_t i = 0; i < lights.size(); ++i) {
            FrameState light{};
            ok &= ReadFrame(lights[i].Get(), light) && light.rect.left == static_cast<LONG>(i) * 28 &&
                  light.rect.right == static_cast<LONG>(i + 1) * 28 && light.rect.top == 0 && light.rect.bottom == 32;
        }
        for (size_t i = 0; i < content.size(); ++i) {
            FrameState actual{};
            ok &= ReadFrame(content[i].Get(), actual) && actual.rect.left == baseline[i].rect.left + 84 &&
                  actual.rect.right == baseline[i].rect.right + 84 && actual.rect.top == baseline[i].rect.top &&
                  actual.rect.bottom == baseline[i].rect.bottom &&
                  actual.rect.right <= row.rect.right - row.rect.left - row.padding.right &&
                  actual.rect.bottom <= row.rect.bottom - row.rect.top - row.padding.bottom;
            std::wcout << L"shiftedContent=" << i << L"; rect=" << RectText(actual.rect) << std::endl;
        }
        return ok;
    };
    Check(verify(), "native layout reserves exactly 84 logical pixels while preserving original content sizes");
    for (int width : {480, 280, 320})
        Check(dimensions(parent, width, 64) && SUCCEEDED(layout(parent, nullptr)) && verify(),
              "native relayout preserves the left reservation after parent width changes");
    FrameState row{};
    auto fits = [&] {
        return ReadFrame(parent, row) &&
               baseline[1].rect.right + 84 + row.padding.right <= row.rect.right - row.rect.left &&
               baseline[1].rect.bottom + row.padding.bottom <= row.rect.bottom - row.rect.top;
    };
    Check(fits(), "reservation budget accepts a row whose original contents and lights fit together");
    Check(dimensions(parent, 240, 64) && !fits(),
          "readback budget rejects a narrow row before claiming an unclipped left layout");
    Check(SUCCEEDED(remove(parent, caption.Get())) && SUCCEEDED(layout(parent, nullptr)),
          "remove the inserted group and rerun the original layout");
    Check(SUCCEEDED(count(parent, &children)) && children == 2, "rollback restores exact original child count");
    for (int i = 0; i < 2; ++i) {
        ComPtr<IUnknown> child;
        FrameState actual{};
        Check(SUCCEEDED(itemAt(parent, i, &child)) && child.Get() == content[i].Get() &&
                  ReadFrame(content[i].Get(), actual) && Equal(actual, baseline[i]),
              "rollback restores typed layout order and the exact original content state");
    }
    Check(dimensions(parent, 320, 64) && SUCCEEDED(layout(parent, nullptr)),
          "restore the owned row after narrow rejection");
    Check(WindowCount() == windows, "original layout transactions create no top-level HWNDs");
}

#include "txgui-paint-observer.h"
#include "txgui-paint-contract.h"
#include "txgui-reparent-contract.h"
#include "txgui-order-contract.h"
#include "txgui-stack-order-contract.h"
#include "txgui-left-layout-contract.h"
#include "txgui-frame-layout-contract.h"

int wmain(int argc, wchar_t **argv) {
    if (argc < 3)
        return ERROR_INVALID_PARAMETER;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return static_cast<int>(initialized);
    auto libraryPath = std::filesystem::path(argv[1]).make_preferred();
    auto gf = LoadLibraryExW(libraryPath.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!gf) {
        std::cout << "loadError=" << GetLastError() << std::endl;
        return static_cast<int>(GetLastError());
    }
    constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
    if (!styler::MatchesPdb(gf, identity, 24))
        return ERROR_REVISION_MISMATCH;
    auto image = reinterpret_cast<BYTE *>(gf);
    fixtureImage = image;
    auto initializeDpi = reinterpret_cast<void(__cdecl *)()>(GetProcAddress(gf, "?InitDPI@DPI@GF@Util@@YAXXZ"));
    if (!initializeDpi)
        return ERROR_PROC_NOT_FOUND;
    initializeDpi();
    if (wcscmp(argv[2], L"--list") == 0) {
        auto begin = *reinterpret_cast<void ***>(image + 0x254104);
        auto end = *reinterpret_cast<void ***>(image + 0x254108);
        if (!begin || end < begin || end - begin > 256 || !Readable(begin, (end - begin) * sizeof(void *)))
            return ERROR_INVALID_DATA;
        for (auto item = begin; item != end; ++item) {
            if (!Readable(*item, 24))
                continue;
            auto record = reinterpret_cast<void **>(*item);
            if (!Readable(record[0], sizeof(GUID)))
                continue;
            wchar_t text[40]{};
            StringFromGUID2(*reinterpret_cast<GUID *>(record[0]), text, 40);
            std::wcout << text << L" creatorRva=" << std::hex
                       << reinterpret_cast<uintptr_t>(record[3]) - reinterpret_cast<uintptr_t>(gf) << std::endl;
        }
        return 0;
    }
    HRESULT result = E_FAIL;
    ComPtr<IUnknown> element;
    ComPtr<IClassFactory> frameFactory;
    const bool contract = wcscmp(argv[2], L"--contract") == 0;
    const bool sysContract = wcscmp(argv[2], L"--sysbutton-contract") == 0;
    const bool stackInspect = wcscmp(argv[2], L"--stackpanel-inspect") == 0;
    const bool stackContract = wcscmp(argv[2], L"--stackpanel-contract") == 0;
    const bool reparentContract = wcscmp(argv[2], L"--reparent-contract") == 0;
    const bool stackOrderContract = wcscmp(argv[2], L"--stack-order-contract") == 0;
    const bool leftContract = wcscmp(argv[2], L"--left-contract") == 0;
    const bool orderContract = wcscmp(argv[2], L"--order-contract") == 0 || stackOrderContract || leftContract;
    const bool titleContract = wcscmp(argv[2], L"--title-contract") == 0;
    const bool passiveContract = wcscmp(argv[2], L"--paint-observer-contract") == 0;
    const bool paintContract = wcscmp(argv[2], L"--paint-contract") == 0 || passiveContract;
    auto counter = reinterpret_cast<float(__cdecl *)()>(GetProcAddress(gf, "?GetGFFrameCount@@YAMXZ"));
    float framesBefore = counter ? counter() : -1;
    int windowsBefore = WindowCount();
    if (sysContract || stackInspect || stackContract || reparentContract || orderContract || paintContract) {
        constexpr BYTE entry[]{0x55, 0x8b, 0xec, 0x6a, 0xff};
        if (memcmp(image + 0xe69a0, entry, sizeof(entry)) || memcmp(image + 0x94ea0, entry, sizeof(entry)) ||
            memcmp(image + 0x201510, &SysButtonId, sizeof(SysButtonId)) ||
            memcmp(image + 0x1f4b50, &ButtonId, sizeof(ButtonId)) ||
            ((stackInspect || stackContract || reparentContract || orderContract) &&
             (memcmp(image + 0x17ce70, entry, sizeof(entry)) ||
              memcmp(image + 0x204804, &StackPanelId, sizeof(StackPanelId)))))
            return ERROR_REVISION_MISMATCH;
        // The documented export accepts a fixture-local object factory. Route
        // only fixture-owned verified classes to their original GF creators;
        // these are real framework objects, not stand-ins for their layout.
        using Factory = BOOL(__cdecl *)(REFCLSID, REFIID, void **);
        auto setFactory = reinterpret_cast<void(__cdecl *)(Factory)>(
            GetProcAddress(gf, "?SetCustomObjectFactory@GF@Util@@YAXP6AHABU_GUID@@0PAPAX@Z@Z"));
        auto create = reinterpret_cast<HRESULT(__cdecl *)(REFCLSID, REFIID, void **)>(
            GetProcAddress(gf, "?CreateObject@GF@Util@@YAJABU_GUID@@0PAPAX@Z"));
        if (!setFactory || !create)
            return ERROR_PROC_NOT_FOUND;
        setFactory(FixtureFactory);
        auto &classId = (stackInspect || stackContract || reparentContract || orderContract) ? StackPanelId
                        : paintContract                                                      ? ButtonId
                                                                                             : SysButtonId;
        result = create(classId, classId, reinterpret_cast<void **>(element.GetAddressOf()));
    } else if (wcscmp(argv[2], L"--raw-frame") == 0) {
        auto create = reinterpret_cast<HRESULT(__cdecl *)(wchar_t *, IUnknown **)>(
            GetProcAddress(gf, "?RawCreateGFObjectByName@GF@Util@@YAJPA_WPAPAUIGFElement@@@Z"));
        if (!create)
            return ERROR_PROC_NOT_FOUND;
        wchar_t name[] = L"Frame";
        std::cout << "creating detached Frame in the fixture process" << std::endl;
        result = create(name, &element);
    } else {
        GUID clsid{};
        result =
            CLSIDFromString((contract || titleContract) ? L"{DDA0ED51-B482-4A5B-A2B4-31A31768CD1D}" : argv[2], &clsid);
        if (FAILED(result))
            return static_cast<int>(result);
        auto getFactory = reinterpret_cast<HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **)>(
            GetProcAddress(gf, "DllGetClassObject"));
        ComPtr<IClassFactory> factory;
        result = getFactory ? getFactory(clsid, IID_PPV_ARGS(&factory)) : E_NOINTERFACE;
        std::cout << "factoryHresult=" << std::hex << static_cast<unsigned long>(result) << std::endl;
        if (SUCCEEDED(result))
            result = factory->CreateInstance(nullptr, IID_PPV_ARGS(&element));
        if (contract)
            frameFactory = factory;
    }
    std::cout << "createHresult=" << std::hex << static_cast<unsigned long>(result) << "; object=" << element.Get()
              << std::endl;
    if (SUCCEEDED(result) && element) {
        auto type = reinterpret_cast<const wchar_t *(__cdecl *)(IUnknown *)>(image + 0x1b0600)(element.Get());
        std::wcout << L"frameworkType=" << (type ? type : L"<unknown>") << std::endl;
        auto nativeTable = *reinterpret_cast<void ***>(reinterpret_cast<BYTE *>(element.Get()) + 8);
        if (Readable(nativeTable, 0x248))
            std::cout << "native240Rva=" << std::hex
                      << reinterpret_cast<uintptr_t>(nativeTable[0x240 / 4]) - reinterpret_cast<uintptr_t>(gf)
                      << "; native244Rva="
                      << reinterpret_cast<uintptr_t>(nativeTable[0x244 / 4]) - reinterpret_cast<uintptr_t>(gf)
                      << std::endl;
        constexpr GUID frameInterfaces[]{
            {0x0bfbd0c8, 0x507b, 0x46dc, {0xbc, 0x8c, 0xd6, 0x8a, 0xf3, 0xba, 0xf2, 0x98}},
            {0xa5dff81a, 0xb003, 0x4967, {0xa2, 0x86, 0x87, 0xeb, 0x38, 0x04, 0x1c, 0x7c}},
            {0xa1767715, 0xe4d9, 0x464d, {0xb6, 0x2a, 0x57, 0x14, 0x2c, 0x58, 0xb9, 0xee}}};
        for (const auto &iid : frameInterfaces) {
            ComPtr<IUnknown> frame;
            auto queried = element->QueryInterface(iid, &frame);
            std::cout << "interface=" << std::hex << iid.Data1 << "; hresult=" << static_cast<unsigned long>(queried);
            if (frame) {
                auto vtable = *reinterpret_cast<void ***>(frame.Get());
                std::cout << "; tableRva=" << reinterpret_cast<uintptr_t>(vtable) - reinterpret_cast<uintptr_t>(gf);
                if (Readable(vtable, 56 * sizeof(void *)))
                    std::cout << "; slot55Rva="
                              << reinterpret_cast<uintptr_t>(vtable[55]) - reinterpret_cast<uintptr_t>(gf);
            }
            std::cout << std::endl;
        }
        // The verified IGFFrame count wrapper delegates through this exact
        // inherited vtable slot. Do not treat arbitrary GF COM objects as frames.
        auto table = *reinterpret_cast<void ***>(element.Get());
        if (Readable(table, 56 * sizeof(void *)) && table[55] == image + 0x4120) {
            auto count = reinterpret_cast<HRESULT(__cdecl *)(IUnknown *, unsigned *)>(
                GetProcAddress(gf, "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
            unsigned children{};
            result = count(element.Get(), &children);
            std::cout << "detachedFrame=true; children=" << children << "; hresult=" << std::hex
                      << static_cast<unsigned long>(result) << std::endl;
            if (contract || sysContract)
                Transactions(gf, element.Get());
            if (contract)
                ChildTransactions(gf, element.Get(), frameFactory.Get());
            if (sysContract) {
                Check(type && !wcscmp(type, L"IGFSysButton"), "original creator produces a real IGFSysButton");
                int windows = WindowCount();
                // Original constructor stores the three real IGFButton
                // interfaces here, in the verified SysButton native base.
                std::set<IUnknown *> identities;
                for (unsigned offset : {0x17cu, 0x180u, 0x184u}) {
                    auto child = *reinterpret_cast<IUnknown **>(reinterpret_cast<BYTE *>(element.Get()) + offset);
                    Check(child != nullptr, "original SysButton constructor creates an inner button");
                    if (!child)
                        continue;
                    ComPtr<IUnknown> button;
                    Check(SUCCEEDED(child->QueryInterface(ButtonId, &button)),
                          "inner control exposes the original IGFButton interface");
                    auto childType = reinterpret_cast<const wchar_t *(__cdecl *)(IUnknown *)>(image + 0x1b0600)(child);
                    Check(childType && !wcscmp(childType, L"IGFButton"), "inner control has the original button type");
                    Check(identities.insert(child).second, "each system operation owns a distinct original button");
                    FrameState state{};
                    Check(ReadFrame(child, state), "read original inner button frame state");
                    EventTransactions(child, element.Get(), image);
                    TooltipTransactions(child, image);
                }
                Check(identities.size() == 3, "all three native system-button controls exist");
                Check(WindowCount() == windows && windows == windowsBefore,
                      "detached system-button creation and inspection create no top-level HWNDs");
                if (identities.size() == 3)
                    SystemButtonTransactions(gf, element.Get());
            }
            if (stackContract)
                StackPanelTransactions(gf, element.Get());
            if (reparentContract)
                txreparent::Run(gf, element.Get());
            if (leftContract)
                txlefttest::Run(gf, element.Get());
            else if (stackOrderContract)
                txstacktest::Run(gf, element.Get());
            else if (orderContract)
                txordertest::Run(gf, element.Get());
            if (titleContract)
                txtitlecontract::Run(gf, element.Get());
            if (paintContract)
                txpaint::Run(gf, element.Get(), passiveContract);
        } else if (contract || sysContract || stackContract || reparentContract || orderContract || titleContract ||
                   paintContract) {
            Check(false, "fixture has the verified inherited IGFFrame vtable");
        }
        element.Reset();
    }
    if (contract || sysContract || stackContract || reparentContract || orderContract || titleContract ||
        paintContract) {
        Check(SUCCEEDED(result), "fixture creation and child enumeration succeed");
        Check(counter && counter() == framesBefore,
              "framework frame count returns to its original value after release");
        std::cout << "checks=" << std::dec << checks << "; failures=" << failures << std::endl;
        if (failures)
            result = E_FAIL;
    }
    // A framework can retain factory/TLS state. Let fixture process teardown
    // release the module naturally rather than manually unloading callback code.
    if (sysContract || stackInspect || stackContract || reparentContract || orderContract || paintContract) {
        using Factory = BOOL(__cdecl *)(REFCLSID, REFIID, void **);
        auto setFactory = reinterpret_cast<void(__cdecl *)(Factory)>(
            GetProcAddress(gf, "?SetCustomObjectFactory@GF@Util@@YAXP6AHABU_GUID@@0PAPAX@Z@Z"));
        setFactory(nullptr);
    }
    CoUninitialize();
    return static_cast<int>(result);
}
