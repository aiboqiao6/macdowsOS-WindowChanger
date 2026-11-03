#pragma once
#include "txgui-left-layout.h"

namespace txlefttest {
inline void Run(HMODULE gf, IUnknown *header) {
    auto image = reinterpret_cast<BYTE *>(gf);
    txleft::Objects o;
    o.header = header;
    using GetFactory = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **);
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(gf, "DllGetClassObject"));
    constexpr GUID frameId{0xdda0ed51, 0xb482, 0x4a5b, {0xa2, 0xb4, 0x31, 0xa3, 0x17, 0x68, 0xcd, 0x1d}};
    ComPtr<IClassFactory> factory;
    if (!Check(get && SUCCEEDED(get(frameId, IID_PPV_ARGS(&factory))),
               "obtain original Frame factory for combined layout"))
        return;
    auto frame = [&](ComPtr<IUnknown> &value) {
        return SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&value)));
    };
    auto stack = [&](ComPtr<IUnknown> &value) {
        return FixtureFactory(StackPanelId, StackPanelId, reinterpret_cast<void **>(value.GetAddressOf())) != FALSE;
    };
    ComPtr<IUnknown> unrelated, before, after, headerButton, headerHidden, contentChild;
    if (!Check(frame(o.body) && frame(o.title) && frame(o.sidebar) && stack(o.content) && stack(o.caption) &&
                   frame(before) && frame(after) && frame(headerHidden) && frame(contentChild) &&
                   FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(headerButton.GetAddressOf())) &&
                   FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(unrelated.GetAddressOf())),
               "create real owned title sidebar nested header content and unrelated caption control"))
        return;
    for (auto &button : o.buttons)
        if (!Check(FixtureFactory(ButtonId, ButtonId, reinterpret_cast<void **>(button.GetAddressOf())) != FALSE,
                   "create an original owned caption Button before migration"))
            return;
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    auto insert = [](IUnknown *parent, IUnknown *child) {
        return SUCCEEDED(reinterpret_cast<Insert>((*reinterpret_cast<void ***>(parent))[52])(parent, child, nullptr));
    };
    std::vector<std::pair<IUnknown *, IUnknown *>> attached;
    auto attach = [&](IUnknown *parent, IUnknown *child) {
        if (!insert(parent, child))
            return false;
        attached.emplace_back(parent, child);
        return true;
    };
    struct Cleanup {
        decltype(attached) &attached;
        ~Cleanup() {
            for (auto i = attached.rbegin(); i != attached.rend(); ++i)
                reinterpret_cast<Remove>((*reinterpret_cast<void ***>(i->first))[53])(i->first, i->second);
        }
    } cleanup{attached};
    txlayout::Api layout(image);
    auto size = [&](IUnknown *object, int width, int height, const wchar_t *anchor = L"LEFT,TOP") {
        bool sized = layout.Size(object, width, height), anchored = layout.Anchor(object, anchor);
        if (!sized || !anchored) {
            auto native = *reinterpret_cast<BYTE ***>(reinterpret_cast<BYTE *>(object) + 8);
            std::cout << "size=" << sized << "; anchor=" << anchored << "; native-size=" << std::hex
                      << native[0xb0 / 4] - image << "; native-anchor=" << native[0xd0 / 4] - image << std::endl;
        }
        return sized && anchored;
    };
    auto horizontal = [&](IUnknown *object) {
        using Set = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int);
        return SUCCEEDED(reinterpret_cast<Set>((*reinterpret_cast<void ***>(object))[0x310 / 4])(object, 1));
    };
    using Set = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    auto set = reinterpret_cast<Set>(GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    bool configured =
        size(o.body.Get(), 1226, 800) && layout.Rect(o.body.Get(), {0, 0, 1226, 800}) &&
        size(o.title.Get(), -1, 76, L"LEFT,TOP,RIGHT") && layout.Margin(o.title.Get(), {232, 0, 0, 0}) &&
        size(o.sidebar.Get(), 232, 800) && size(header, 200, 32) && layout.Margin(header, {18, 23, 0, 0}) &&
        size(o.content.Get(), 569, 76) && horizontal(o.content.Get()) && size(o.caption.Get(), 183, 76, L"RIGHT,TOP") &&
        horizontal(o.caption.Get()) && size(headerButton.Get(), 200, 32) && size(headerHidden.Get(), 196, 36) && set &&
        SUCCEEDED(set(headerHidden.Get(), L"hidden", L"true")) && size(contentChild.Get(), 569, 32) &&
        size(unrelated.Get(), 41, 56) && size(o.buttons[0].Get(), 62, 56) && size(o.buttons[1].Get(), 40, 56) &&
        size(o.buttons[2].Get(), 40, 56) && layout.Padding(o.buttons[0].Get(), {0, 20, 22, 0});
    if (!Check(configured, "configure original typed layout dimensions and constraints"))
        return;
    if (!Check(attach(o.body.Get(), o.sidebar.Get()) && attach(o.body.Get(), o.title.Get()) &&
                   attach(o.sidebar.Get(), before.Get()) && attach(o.sidebar.Get(), header) &&
                   attach(o.sidebar.Get(), after.Get()) && attach(header, headerButton.Get()) &&
                   attach(header, headerHidden.Get()) && attach(o.title.Get(), o.content.Get()) &&
                   attach(o.title.Get(), o.caption.Get()) && attach(o.content.Get(), contentChild.Get()) &&
                   attach(o.caption.Get(), unrelated.Get()) && attach(o.caption.Get(), o.buttons[1].Get()) &&
                   attach(o.caption.Get(), o.buttons[2].Get()) && attach(o.caption.Get(), o.buttons[0].Get()),
               "populate original nested layout with the real caption topology and dimensions"))
        return;
    for (auto object : {o.body.Get(), o.sidebar.Get(), o.title.Get(), header, o.content.Get(), o.caption.Get()}) {
        bool laidOut = layout.Layout(object);
        if (!laidOut) {
            auto native = *reinterpret_cast<BYTE ***>(reinterpret_cast<BYTE *>(object) + 8);
            std::cout << "layout-native=" << std::hex << native[0x224 / 4] - image << std::endl;
        }
        if (!Check(laidOut, "original reflow accepts the verified combined container"))
            return;
    }
    std::vector<std::pair<IUnknown *, txlayout::State>> states;
    for (auto object :
         {o.title.Get(), o.sidebar.Get(), header, o.content.Get(), o.caption.Get(), unrelated.Get(), headerButton.Get(),
          headerHidden.Get(), contentChild.Get(), o.buttons[0].Get(), o.buttons[1].Get(), o.buttons[2].Get()}) {
        txlayout::State state;
        if (!Check(layout.Read(object, state), "snapshot complete original layout record before combined transaction"))
            return;
        states.emplace_back(object, state);
    }
    auto exact = [&] {
        bool ok = true;
        for (auto &[object, saved] : states) {
            txlayout::State state;
            ok = layout.Read(object, state) && saved.Same(state) && ok;
            if (!saved.Same(state))
                std::wcout << L"mismatch=" << object << L" rect=" << RectText(state.rect) << L" saved="
                           << RectText(saved.rect) << std::endl;
        }
        return ok;
    };
    auto verify = [&] {
        txlayout::State title, movedHeader, content, caption, other;
        if (!layout.Read(o.title.Get(), title) || !layout.Read(header, movedHeader) ||
            !layout.Read(o.content.Get(), content) || !layout.Read(o.caption.Get(), caption) ||
            !layout.Read(unrelated.Get(), other))
            return false;
        std::wcout << L"combined title=" << RectText(title.rect) << L" header=" << RectText(movedHeader.rect)
                   << L" content=" << RectText(content.rect) << L" caption=" << RectText(caption.rect) << std::endl;
        bool ok = title.rect.left == 0 && title.rect.right == 1226 && title.rect.bottom == 76 &&
                  movedHeader.rect.left == 102 && movedHeader.rect.right == 302 && movedHeader.rect.top == 23 &&
                  content.rect.left == 316 && content.rect.right == 885 && caption.rect.left == 1043 &&
                  other.Same(states[5].second);
        for (size_t i = 0; i < 3; ++i) {
            txlayout::State button;
            ok = layout.Read(o.buttons[i].Get(), button) && button.rect.left == static_cast<LONG>(i) * 28 &&
                 button.rect.right == static_cast<LONG>(i + 1) * 28 && button.rect.top == 0 &&
                 button.rect.bottom == 56 && ok;
        }
        return ok;
    };
    auto windows = WindowCount();
    {
        txleft::Scope transaction(image, o);
        if (!Check(transaction.Ready(), "combined original layout validates fit and pins every rollback object"))
            return;
        Check(transaction.Apply(), "expand existing title migrate original header and all three existing Buttons");
        Check(verify(), "original reflow places 84 DIP lights header and original content without overlap");
        Check(transaction.Finish() && exact(), "combined rollback restores every original 48-byte layout record");
        Check(transaction.Finish(), "combined rollback is idempotent");
    }
    bool cycles = true;
    for (unsigned i = 0; i < 32 && cycles; ++i) {
        txleft::Scope transaction(image, o);
        cycles = transaction.Ready() && transaction.Apply() && transaction.Finish() && exact();
    }
    Check(cycles, "32 combined migration draw-space and exact rollback cycles succeed");
    try {
        txleft::Scope transaction(image, o);
        if (transaction.Ready() && transaction.Apply())
            throw 1;
    } catch (int) {
    }
    Check(exact(), "exception unwinding restores the complete combined original layout");
    auto invalid = o;
    invalid.buttons[1] = invalid.buttons[0];
    txleft::Scope duplicate(image, invalid);
    Check(!duplicate.Ready() && !duplicate.Apply() && exact(),
          "duplicate original caption identity fails before writes");
    Check(WindowCount() == windows, "combined original layout creates no overlay HWND or caption band");
}
} // namespace txlefttest
