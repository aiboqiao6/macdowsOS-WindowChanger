#pragma once
#include "txgui-frame-layout.h"

namespace txtitlecontract {
inline void Run(HMODULE gf, IUnknown *owned) {
    using Factory = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **);
    using Set = HRESULT(__cdecl *)(IUnknown *, const wchar_t *, const wchar_t *);
    using Insert = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
    using Remove = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
    constexpr GUID frameId{0xdda0ed51, 0xb482, 0x4a5b, {0xa2, 0xb4, 0x31, 0xa3, 0x17, 0x68, 0xcd, 0x1d}};
    auto get = reinterpret_cast<Factory>(GetProcAddress(gf, "DllGetClassObject"));
    auto set = reinterpret_cast<Set>(GetProcAddress(gf, "?SetFrameAttribute@GFSpyFuncHelper@@YAJPAUIGFFrame@@PB_W1@Z"));
    ComPtr<IClassFactory> factory;
    if (!Check(get && set && SUCCEEDED(get(frameId, IID_PPV_ARGS(&factory))) && factory,
               "obtain original ordinary Frame factory for owned title expansion contract"))
        return;
    ComPtr<IUnknown> body, title, content, caption;
    if (!Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&body))) && body &&
                   SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&title))) && title &&
                   SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&content))) && content &&
                   SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&caption))) && caption,
               "create real original owned parent title and two existing child Frames"))
        return;
    auto insert = [](IUnknown *parent, IUnknown *child) {
        return SUCCEEDED(reinterpret_cast<Insert>((*reinterpret_cast<void ***>(parent))[52])(parent, child, nullptr));
    };
    struct Cleanup {
        IUnknown *body, *title, *content, *caption;
        ~Cleanup() {
            auto remove = [](IUnknown *parent, IUnknown *child) {
                reinterpret_cast<Remove>((*reinterpret_cast<void ***>(parent))[53])(parent, child);
            };
            remove(title, caption);
            remove(title, content);
            remove(body, title);
        }
    } cleanup{body.Get(), title.Get(), content.Get(), caption.Get()};
    txlayout::Api api(reinterpret_cast<BYTE *>(gf));
    constexpr GUID filtersId{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
    ComPtr<IUnknown> filters;
    if (SUCCEEDED(title->QueryInterface(filtersId, &filters)) && filters) {
        auto table = *reinterpret_cast<BYTE ***>(filters.Get());
        auto image = reinterpret_cast<BYTE *>(gf);
        std::cout << "frame filters=" << std::hex << (reinterpret_cast<BYTE *>(table) - image)
                  << "; add=" << (table[7] - image) << "; remove=" << (table[8] - image) << std::endl;
    }
    using Size = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int, int);
    auto size = [](IUnknown *object, int width, int height) {
        auto table = *reinterpret_cast<void ***>(object);
        return SUCCEEDED(reinterpret_cast<Size>(table[0x1ec / 4])(object, width, height));
    };
    using Anchor = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR);
    auto anchor = [](IUnknown *object, const wchar_t *text) {
        auto table = *reinterpret_cast<void ***>(object);
        auto value = SysAllocString(text);
        auto result = value ? reinterpret_cast<Anchor>(table[0x21c / 4])(object, value) : E_OUTOFMEMORY;
        SysFreeString(value);
        return SUCCEEDED(result);
    };
    bool configured = size(body.Get(), 1226, 800) && SUCCEEDED(set(body.Get(), L"rect", L"0,0,1226,800")) &&
                      size(title.Get(), -1, 76) && SUCCEEDED(set(title.Get(), L"margin", L"232,0,0,0")) &&
                      size(content.Get(), 569, 76) && size(caption.Get(), 183, 76) &&
                      anchor(title.Get(), L"LEFT,TOP,RIGHT") && anchor(content.Get(), L"LEFT,TOP") &&
                      anchor(caption.Get(), L"RIGHT,TOP");
    Check(configured && insert(body.Get(), title.Get()) && insert(title.Get(), content.Get()) &&
              insert(title.Get(), caption.Get()) && api.Layout(body.Get()) && api.Layout(title.Get()),
          "configure an owned original existing title row through original properties and native reflow");
    txlayout::State original{}, left{}, right{}, bodyState{};
    if (!Check(api.Read(title.Get(), original) && api.Read(content.Get(), left) && api.Read(caption.Get(), right),
               "read typed original geometry margin padding and all 48 layout-policy bytes"))
        return;
    std::wcout << L"originalTitle=" << RectText(original.rect) << L"; content=" << RectText(left.rect) << L"; caption="
               << RectText(right.rect) << std::endl;
    api.Read(body.Get(), bodyState);
    std::wcout << L"body=" << RectText(bodyState.rect) << L"; margin=" << RectText(original.margin) << std::endl;
    for (auto value : original.policy)
        std::cout << value << ',';
    std::cout << std::endl;
    Check(original.rect.left == 232 && original.rect.right == 1226 && original.rect.top == 0 &&
              original.rect.bottom == 76,
          "owned original margin and height constraints reproduce the real title rectangle");
    auto windows = WindowCount();
    txlayout::Scope scope(api, body.Get(), title.Get());
    Check(scope.Ready() && scope.ExpandLeft(),
          "expand only the original title frame via padding margin and original relayout");
    txlayout::State expanded{}, newLeft{}, newRight{};
    bool read =
        api.Read(title.Get(), expanded) && api.Read(content.Get(), newLeft) && api.Read(caption.Get(), newRight);
    std::wcout << L"expandedTitle=" << RectText(expanded.rect) << L"; content=" << RectText(newLeft.rect)
               << L"; caption=" << RectText(newRight.rect) << std::endl;
    Check(read && expanded.rect.left == 0 && expanded.rect.right == original.rect.right &&
              expanded.rect.top == original.rect.top && expanded.rect.bottom == original.rect.bottom &&
              expanded.padding.left == original.padding.left + original.margin.left && expanded.margin.left == 0,
          "original layout expands the frame to its existing left boundary without increasing title height");
    auto sameAbsolute = [&](const txlayout::State &before, const txlayout::State &after) {
        auto oldRect = before.rect, newRect = after.rect;
        auto oldPolicy = before.policy, newPolicy = after.policy;
        OffsetRect(&oldRect, original.rect.left, original.rect.top);
        OffsetRect(&newRect, expanded.rect.left, expanded.rect.top);
        oldPolicy[6] += original.rect.left;
        oldPolicy[7] += original.rect.top;
        newPolicy[6] += expanded.rect.left;
        newPolicy[7] += expanded.rect.top;
        return EqualRect(&oldRect, &newRect) && EqualRect(&before.margin, &after.margin) &&
               EqualRect(&before.padding, &after.padding) && oldPolicy == newPolicy;
    };
    Check(read && sameAbsolute(left, newLeft) && sameAbsolute(right, newRight),
          "existing original child screen-equivalent geometry and layout policies remain unchanged");
    Check(scope.Finish() && api.Read(title.Get(), expanded) && original.Same(expanded) &&
              api.Read(content.Get(), newLeft) && left.Same(newLeft) && api.Read(caption.Get(), newRight) &&
              right.Same(newRight),
          "rollback restores original parent bounds margins padding and complete original child state");
    Check(scope.Finish(), "original title rollback is idempotent");
    bool repeated = true;
    for (unsigned i = 0; i < 32 && repeated; ++i) {
        txlayout::Scope cycle(api, body.Get(), title.Get());
        repeated = cycle.Ready() && cycle.ExpandLeft() && cycle.Finish();
    }
    Check(repeated, "32 original frame expansion and rollback cycles preserve all layout-policy values");
    try {
        txlayout::Scope unwind(api, body.Get(), title.Get());
        if (unwind.Ready() && unwind.ExpandLeft())
            throw 1;
    } catch (int) {
    }
    Check(api.Read(title.Get(), expanded) && original.Same(expanded),
          "scope unwinding restores the original title frame");
    txlayout::Scope foreign(api, owned, title.Get());
    Check(!foreign.Ready() && !foreign.ExpandLeft() && api.Read(title.Get(), expanded) && original.Same(expanded),
          "reject a foreign parent before any layout or property write");
    txlayout::Scope zeroMargin(api, title.Get(), content.Get());
    Check(zeroMargin.Ready() && !zeroMargin.ExpandLeft() && api.Read(content.Get(), newLeft) && left.Same(newLeft),
          "reject a zero-left-margin expansion without changing original content");
    txlayout::Scope missing(api, nullptr, title.Get());
    Check(!missing.Ready() && !missing.ExpandLeft(), "missing original parent fails closed without dereferencing it");
    Check(WindowCount() == windows, "owned original title expansion creates no new HWND or caption band");
}
} // namespace txtitlecontract
