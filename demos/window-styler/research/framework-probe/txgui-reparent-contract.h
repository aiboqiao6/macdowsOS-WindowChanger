#pragma once
#include "txgui-order.h"

// Included only by the owned-process fixture. No external HWND/PID is accepted.
namespace txreparent {
inline void Run(HMODULE gf, IUnknown *header) {
    auto image = reinterpret_cast<BYTE *>(gf);
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using Layout = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    using Parent = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown **);
    using PutInt = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int);
    using Set = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    using Count = HRESULT(__cdecl *)(IUnknown *, unsigned *);
    using At = HRESULT(__cdecl *)(IUnknown *, int, IUnknown **);
    auto set = reinterpret_cast<Set>(GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    auto count = reinterpret_cast<Count>(GetProcAddress(gf, "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    auto at = reinterpret_cast<At>(GetProcAddress(gf, "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    constexpr GUID frameId{0xdda0ed51, 0xb482, 0x4a5b, {0xa2, 0xb4, 0x31, 0xa3, 0x17, 0x68, 0xcd, 0x1d}};
    auto getFactory =
        reinterpret_cast<HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **)>(GetProcAddress(gf, "DllGetClassObject"));
    ComPtr<IClassFactory> factory;
    if (!Check(set && count && at && getFactory && SUCCEEDED(getFactory(frameId, IID_PPV_ARGS(&factory))) && factory,
               "owned reparent fixture obtains the original Frame factory and tree methods"))
        return;
    auto frame = [&] {
        ComPtr<IUnknown> result;
        factory->CreateInstance(nullptr, IID_PPV_ARGS(&result));
        return result;
    };
    auto source = frame(), target = frame(), beforeSibling = frame(), afterSibling = frame(), spacer = frame();
    ComPtr<IUnknown> row, button, hidden;
    hidden = frame();
    if (!Check(source && target && beforeSibling && afterSibling && spacer && hidden &&
                   FixtureFactory(StackPanelId, StackPanelId, reinterpret_cast<void **>(row.GetAddressOf())) && row &&
                   FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(button.GetAddressOf())) && button,
               "create only real owned original containers content and spacer objects"))
        return;
    auto table = *reinterpret_cast<void ***>(header), rowTable = *reinterpret_cast<void ***>(row.Get());
    auto sourceTable = *reinterpret_cast<void ***>(source.Get());
    if (!Check(table == reinterpret_cast<void **>(image + 0x220f58) && rowTable == table &&
                   table[0x310 / 4] == image + 0x17ca90 && table[0x2f4 / 4] == image + 0x4430 &&
                   table[0x338 / 4] == image + 0x17caf0 && sourceTable[55] == image + 0x4120,
               "guard original typed StackPanel layout insertion and inherited Frame ABI"))
        return;
    auto insert = [&](IUnknown *parent, IUnknown *child) {
        return SUCCEEDED(reinterpret_cast<Insert>((*reinterpret_cast<void ***>(parent))[52])(parent, child, nullptr));
    };
    auto remove = [&](IUnknown *parent, IUnknown *child) {
        return SUCCEEDED(reinterpret_cast<Remove>((*reinterpret_cast<void ***>(parent))[53])(parent, child));
    };
    auto layout = [&](IUnknown *parent) {
        return SUCCEEDED(reinterpret_cast<Layout>((*reinterpret_cast<void ***>(parent))[0x2f4 / 4])(parent, nullptr));
    };
    auto parentOf = [&](IUnknown *child, IUnknown *expected) {
        ComPtr<IUnknown> parent;
        auto status = reinterpret_cast<Parent>((*reinterpret_cast<void ***>(child))[0x100 / 4])(child, &parent);
        return SUCCEEDED(status) && parent.Get() == expected;
    };
    auto dimensions = [&](IUnknown *object, RECT rect) {
        return SUCCEEDED(set(object, L"rect", RectText(rect).c_str()));
    };
    auto order = [&](IUnknown *parent, std::initializer_list<IUnknown *> expected) {
        unsigned total{};
        if (FAILED(count(parent, &total)) || total != expected.size())
            return false;
        unsigned index{};
        for (auto value : expected) {
            ComPtr<IUnknown> actual;
            if (FAILED(at(parent, index++, &actual)) || actual.Get() != value)
                return false;
        }
        return true;
    };
    struct Cleanup {
        decltype(remove) &remove;
        IUnknown *source, *target, *header, *row, *before, *after, *spacer, *button, *hidden;
        ~Cleanup() {
            remove(row, header);
            remove(source, header);
            remove(row, spacer);
            remove(target, row);
            remove(header, button);
            remove(header, hidden);
            remove(source, before);
            remove(source, after);
        }
    } cleanup{remove,       source.Get(), target.Get(), header, row.Get(), beforeSibling.Get(), afterSibling.Get(),
              spacer.Get(), button.Get(), hidden.Get()};
    bool configured = dimensions(source.Get(), {0, 0, 232, 800}) && dimensions(target.Get(), {0, 0, 1226, 76}) &&
                      dimensions(header, {18, 23, 218, 55}) && SUCCEEDED(set(header, L"margin", L"18,23,0,0")) &&
                      dimensions(row.Get(), {0, 0, 316, 76}) && dimensions(spacer.Get(), {0, 0, 84, 56}) &&
                      dimensions(button.Get(), {0, 0, 200, 32}) && dimensions(hidden.Get(), {0, 0, 196, 36}) &&
                      SUCCEEDED(set(hidden.Get(), L"hidden", L"true")) &&
                      SUCCEEDED(reinterpret_cast<PutInt>(table[0x310 / 4])(header, 0)) &&
                      SUCCEEDED(reinterpret_cast<PutInt>(rowTable[0x310 / 4])(row.Get(), 1));
    if (!Check(configured && insert(source.Get(), beforeSibling.Get()) && insert(source.Get(), header) &&
                   insert(source.Get(), afterSibling.Get()) && insert(header, button.Get()) &&
                   insert(header, hidden.Get()) && layout(header),
               "populate original source with a nested header visible content and hidden row"))
        return;
    FrameState saved{}, buttonState{}, hiddenState{};
    if (!Check(ReadFrame(header, saved) && ReadFrame(button.Get(), buttonState) &&
                   ReadFrame(hidden.Get(), hiddenState) &&
                   order(source.Get(), {beforeSibling.Get(), header, afterSibling.Get()}) &&
                   order(header, {button.Get(), hidden.Get()}) && parentOf(header, source.Get()),
               "save original raw sibling order parent and all nested original frame states"))
        return;
    txorder::Api ordering(image);
    txorder::Snapshot originalOrder;
    if (!Check(ordering.Capture(source.Get(), originalOrder),
               "capture both original Element and Frame chains with pinned siblings before migration"))
        return;
    constexpr GUID eventsId{0x6ccbbc7b, 0x5c0a, 0x49aa, {0xb1, 0x99, 0x67, 0x33, 0xfd, 0xcd, 0x7d, 0xd4}};
    constexpr GUID sinkId{0x78b5f698, 0x78d3, 0x46f0, {0x88, 0x34, 0xe7, 0x01, 0x76, 0xd0, 0x34, 0x40}};
    ComPtr<IUnknown> group, events, sink;
    if (!Check(FixtureFactory(SysButtonId, SysButtonId, reinterpret_cast<void **>(group.GetAddressOf())) && group &&
                   SUCCEEDED(group->QueryInterface(sinkId, &sink)) &&
                   SUCCEEDED(button->QueryInterface(eventsId, &events)),
               "obtain an owned original click notifier without invoking an action"))
        return;
    auto eventTable = *reinterpret_cast<void ***>(events.Get());
    if (!Check(eventTable[7] == image + 0x93910 && eventTable[14] == image + 0x93a10,
               "guard original owned event subscription and cleanup wrappers"))
        return;
    using Add = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned, IUnknown *);
    using Clear = HRESULT(STDMETHODCALLTYPE *)(IUnknown *);
    struct EventCleanup {
        IUnknown *events;
        Clear clear;
        ~EventCleanup() {
            clear(events);
        }
    } eventCleanup{events.Get(), reinterpret_cast<Clear>(eventTable[14])};
    if (!Check(SUCCEEDED(reinterpret_cast<Add>(eventTable[7])(events.Get(), 0x1003, sink.Get())),
               "populate a real original click subscription before any container migration"))
        return;
    auto bindings = txevents::Inspect(button.Get(), image);
    Check(bindings.complete && bindings.numeric.size() == 1 && bindings.numeric[0].id == 0x1003,
          "snapshot a populated original click container rather than an empty stand-in");
    auto windows = WindowCount();
    auto fits = [&](int available) {
        return available >= 84 + saved.margin.left + saved.rect.right - saved.rect.left + saved.margin.right;
    };
    Check(fits(316) && !fits(232) && !fits(280),
          "reject insufficient original left space before any reparent mutation");
    if (!Check(insert(target.Get(), row.Get()) && insert(row.Get(), spacer.Get()),
               "attach a real horizontal layout wrapper only inside the owned existing title frame"))
        return;
    auto move = [&] {
        return remove(source.Get(), header) && insert(row.Get(), header) && layout(row.Get()) && layout(header);
    };
    auto restore = [&] {
        return remove(row.Get(), header) && insert(source.Get(), header) && ordering.Restore(originalOrder) &&
               dimensions(header, saved.rect) && layout(header);
    };
    if (!Check(move(), "move the existing header with original remove insert and native horizontal reflow"))
        return;
    FrameState moved{};
    ReadFrame(header, moved);
    std::wcout << L"native reparent header=" << RectText(moved.rect) << std::endl;
    Check(parentOf(header, row.Get()) && order(source.Get(), {beforeSibling.Get(), afterSibling.Get()}) &&
              order(row.Get(), {spacer.Get(), header}) && order(header, {button.Get(), hidden.Get()}),
          "original header identity and nested raw order survive the cross-container move");
    Check(moved.rect.left == saved.rect.left + 84 && moved.rect.right == saved.rect.right + 84 &&
              moved.rect.top == saved.rect.top && moved.rect.bottom == saved.rect.bottom &&
              EqualRect(&moved.margin, &saved.margin) && EqualRect(&moved.padding, &saved.padding),
          "native reflow reserves 84 DIP without shrinking or vertically moving original header content");
    FrameState currentButton{}, currentHidden{};
    Check(ReadFrame(button.Get(), currentButton) && Equal(buttonState, currentButton) &&
              ReadFrame(hidden.Get(), currentHidden) && Equal(hiddenState, currentHidden),
          "original nested geometry hidden state and padding remain unchanged during reparent");
    Check(restore() && parentOf(header, source.Get()) &&
              order(source.Get(), {beforeSibling.Get(), header, afterSibling.Get()}) &&
              order(row.Get(), {spacer.Get()}) && ReadFrame(header, moved) && Equal(saved, moved),
          "original link setters restore both sibling orders without removing any unrelated sibling");
    bool repeated = true;
    for (unsigned i = 0; i < 32 && repeated; ++i)
        repeated = move() && restore() && parentOf(header, source.Get()) &&
                   order(source.Get(), {beforeSibling.Get(), header, afterSibling.Get()});
    Check(repeated, "32 original cross-container move and ordered-restore cycles leave no duplicate or stale child");
    auto afterBindings = txevents::Inspect(button.Get(), image);
    Check(ReadFrame(header, moved) && Equal(saved, moved) && ReadFrame(button.Get(), currentButton) &&
              Equal(buttonState, currentButton) && ReadFrame(hidden.Get(), currentHidden) &&
              Equal(hiddenState, currentHidden) && afterBindings.complete && bindings.complete &&
              afterBindings.numeric == bindings.numeric && afterBindings.named == bindings.named &&
              afterBindings.listeners == bindings.listeners,
          "reparent cycles restore exact original frame states and preserve original event containers");
    Check(WindowCount() == windows, "cross-container layout transactions create no top-level HWNDs or caption bands");
}
} // namespace txreparent
