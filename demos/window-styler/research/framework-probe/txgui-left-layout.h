#pragma once
#include "txgui-frame-layout.h"
#include "txgui-stack-order.h"

namespace txleft {
using Microsoft::WRL::ComPtr;
struct Objects {
    ComPtr<IUnknown> body, title, sidebar, header, content, caption;
    std::array<ComPtr<IUnknown>, 3> buttons;
};
class Scope {
    BYTE *image_;
    txlayout::Api layout_;
    txorder::Api order_;
    txstack::Api stack_;
    Objects objects_;
    ComPtr<IUnknown> row_, lights_, rightSpacer_;
    txlayout::State body_, title_, header_, content_, caption_;
    std::array<txlayout::State, 3> buttons_;
    txorder::Snapshot sidebarOrder_, titleOrder_;
    txstack::Snapshot captionOrder_;
    bool ready_{}, armed_{}, rowAttached_{}, headerMoved_{}, spacerAttached_{};
    std::array<bool, 3> buttonMoved_{};
    static IUnknown *Parent(IUnknown *object, ComPtr<IUnknown> &parent) {
        parent.Reset();
        using Get = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown **);
        if (!object || FAILED(reinterpret_cast<Get>((*reinterpret_cast<void ***>(object))[0x100 / 4])(object, &parent)))
            return nullptr;
        return parent.Get();
    }
    static bool Insert(IUnknown *parent, IUnknown *child) {
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *);
        return SUCCEEDED(reinterpret_cast<Function>((*reinterpret_cast<void ***>(parent))[52])(parent, child, nullptr));
    }
    static bool Remove(IUnknown *parent, IUnknown *child) {
        using Function = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
        return SUCCEEDED(reinterpret_cast<Function>((*reinterpret_cast<void ***>(parent))[53])(parent, child));
    }
    bool CreateStack(ComPtr<IUnknown> &object, int width, int height) {
        constexpr BYTE entry[]{0x55, 0x8b, 0xec, 0x6a, 0xff};
        constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
        if (!styler::MatchesPdb(reinterpret_cast<HMODULE>(image_), identity, 24) ||
            memcmp(image_ + 0x17ce70, entry, sizeof(entry)))
            return false;
        // Original class creator, without replacing the process-global custom
        // object factory or constructing any window/caption overlay.
        using Creator = HRESULT(__cdecl *)(IUnknown **);
        if (FAILED(reinterpret_cast<Creator>(image_ + 0x17ce70)(&object)) || !object)
            return false;
        txstack::Snapshot empty;
        if (!stack_.Capture(object.Get(), empty) || !empty.typed.items.empty() ||
            !layout_.Size(object.Get(), width, height) || !layout_.Anchor(object.Get(), L"LEFT,TOP"))
            return false;
        using Orientation = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int);
        auto table = *reinterpret_cast<void ***>(object.Get());
        return table[0x310 / 4] == image_ + 0x17ca90 &&
               SUCCEEDED(reinterpret_cast<Orientation>(table[0x310 / 4])(object.Get(), 1));
    }
    bool RestoreLayout(IUnknown *object, const txlayout::State &saved) {
        return layout_.PhysicalSize(object, saved.policy[4], saved.policy[5]) &&
               layout_.Padding(object, saved.padding) && layout_.Margin(object, saved.margin);
    }
    bool Restore() {
        bool ok = true;
        if (spacerAttached_) {
            bool removed = stack_.Remove(objects_.caption.Get(), rightSpacer_.Get());
            ok = removed && ok;
            if (removed)
                spacerAttached_ = false;
        }
        for (size_t index = 0; index < captionOrder_.typed.items.size(); ++index) {
            for (size_t i = 0; i < 3; ++i) {
                if (!buttonMoved_[i] || captionOrder_.typed.items[index] != objects_.buttons[i])
                    continue;
                auto button = objects_.buttons[i].Get();
                ComPtr<IUnknown> parent;
                auto owner = Parent(button, parent);
                bool detached = !owner || (owner == lights_.Get() && stack_.Remove(lights_.Get(), button));
                bool restored = detached && RestoreLayout(button, buttons_[i]) &&
                                stack_.InsertAt(objects_.caption.Get(), button, index);
                ok = restored && ok;
                if (restored)
                    buttonMoved_[i] = false;
            }
        }
        if (headerMoved_) {
            ComPtr<IUnknown> parent;
            auto owner = Parent(objects_.header.Get(), parent);
            bool detached = !owner || (owner == row_.Get() && stack_.Remove(row_.Get(), objects_.header.Get()));
            bool restored = detached && Insert(objects_.sidebar.Get(), objects_.header.Get());
            ok = restored && ok;
            if (restored)
                headerMoved_ = false;
        }
        if (rowAttached_) {
            bool restored = Remove(objects_.title.Get(), row_.Get());
            ok = restored && ok;
            if (restored)
                rowAttached_ = false;
        }
        ok = stack_.Restore(captionOrder_) && ok;
        ok = order_.Restore(sidebarOrder_) && order_.Restore(titleOrder_) && ok;
        ok = layout_.Margin(objects_.content.Get(), content_.margin) &&
             layout_.Padding(objects_.title.Get(), title_.padding) &&
             layout_.Margin(objects_.title.Get(), title_.margin) && ok;
        ok = layout_.Layout(objects_.body.Get(), objects_.title.Get()) && layout_.Layout(objects_.title.Get()) &&
             layout_.Rect(objects_.header.Get(), header_.rect) && layout_.Layout(objects_.header.Get()) &&
             layout_.Layout(objects_.caption.Get()) && ok;
        return ok;
    }

  public:
    Scope(BYTE *image, const Objects &objects)
        : image_(image), layout_(image), order_(image), stack_(image), objects_(objects) {
        auto &o = objects_;
        if (!layout_.Read(o.body.Get(), body_) || !layout_.Read(o.title.Get(), title_) ||
            !layout_.Read(o.header.Get(), header_) || !layout_.Read(o.content.Get(), content_) ||
            !layout_.Read(o.caption.Get(), caption_) || !order_.Capture(o.sidebar.Get(), sidebarOrder_) ||
            !order_.Capture(o.title.Get(), titleOrder_) || !stack_.Capture(o.caption.Get(), captionOrder_))
            return;
        ComPtr<IUnknown> parent;
        if (Parent(o.title.Get(), parent) != o.body.Get() || Parent(o.header.Get(), parent) != o.sidebar.Get() ||
            Parent(o.content.Get(), parent) != o.title.Get() || Parent(o.caption.Get(), parent) != o.title.Get())
            return;
        std::set<IUnknown *> seen;
        for (size_t i = 0; i < 3; ++i)
            if (!layout_.Read(o.buttons[i].Get(), buttons_[i]) ||
                Parent(o.buttons[i].Get(), parent) != o.caption.Get() || !seen.insert(o.buttons[i].Get()).second)
                return;
        const LONG span = title_.margin.left + 84;
        if (title_.margin.left != title_.rect.left || title_.margin.left <= 0 || title_.padding.left != 0 ||
            84 + header_.margin.left + header_.rect.right - header_.rect.left + header_.margin.right > span ||
            span + content_.rect.right - content_.rect.left > title_.rect.left + caption_.rect.left ||
            title_.rect.bottom != 76 || captionOrder_.typed.items.size() != 4)
            return;
        LONG removedWidth{};
        for (auto &saved : buttons_) {
            if (saved.margin.left || saved.margin.right || saved.rect.bottom - saved.rect.top != 56)
                return;
            removedWidth += saved.rect.right - saved.rect.left;
        }
        ready_ = CreateStack(row_, span, 76) && CreateStack(lights_, 84, 56) &&
                 CreateStack(rightSpacer_, removedWidth, 56) && stack_.InsertAt(row_.Get(), lights_.Get(), 0);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
    bool Ready() const {
        return ready_;
    }
    IUnknown *Row() const {
        return row_.Get();
    }
    IUnknown *Lights() const {
        return lights_.Get();
    }
    bool Healthy() const {
        if (!armed_)
            return false;
        const auto &o = objects_;
        ComPtr<IUnknown> parent;
        txlayout::State title, content, caption, row, lights;
        if (Parent(o.title.Get(), parent) != o.body.Get() || Parent(o.header.Get(), parent) != row_.Get() ||
            Parent(o.content.Get(), parent) != o.title.Get() || Parent(o.caption.Get(), parent) != o.title.Get() ||
            Parent(row_.Get(), parent) != o.title.Get() || Parent(lights_.Get(), parent) != row_.Get() ||
            Parent(rightSpacer_.Get(), parent) != o.caption.Get() || !layout_.Read(o.title.Get(), title) ||
            !layout_.Read(o.content.Get(), content) || !layout_.Read(o.caption.Get(), caption) ||
            !layout_.Read(row_.Get(), row) || !layout_.Read(lights_.Get(), lights))
            return false;
        auto margin = title_.margin;
        margin.left = 0;
        auto contentMargin = content_.margin;
        contentMargin.left += title_.margin.left + 84;
        if (!EqualRect(&title.margin, &margin) || !EqualRect(&title.padding, &title_.padding) ||
            !EqualRect(&content.margin, &contentMargin) || content.rect.right > caption.rect.left ||
            row.rect.right - row.rect.left != title_.margin.left + 84 || lights.rect.right - lights.rect.left != 84)
            return false;
        for (auto &button : o.buttons) {
            txlayout::State state;
            if (Parent(button.Get(), parent) != lights_.Get() || !layout_.Read(button.Get(), state) ||
                state.rect.right - state.rect.left != 28 || state.rect.bottom - state.rect.top != 56)
                return false;
        }
        return true;
    }
    bool Apply() {
        if (!ready_ || armed_)
            return false;
        armed_ = true;
        auto &o = objects_;
        auto margin = title_.margin;
        margin.left = 0;
        auto contentMargin = content_.margin;
        contentMargin.left += title_.margin.left + 84;
        if (!layout_.Margin(o.title.Get(), margin) || !layout_.Margin(o.content.Get(), contentMargin) ||
            !Insert(o.title.Get(), row_.Get()))
            return false;
        rowAttached_ = true;
        if (!Remove(o.sidebar.Get(), o.header.Get()))
            return false;
        // Arm before inserting so even a partial insert is noticed by rollback.
        headerMoved_ = true;
        if (!stack_.InsertAt(row_.Get(), o.header.Get(), 1))
            return false;
        for (size_t i = 0; i < 3; ++i) {
            auto button = o.buttons[i].Get();
            if (!stack_.Remove(o.caption.Get(), button))
                return false;
            buttonMoved_[i] = true;
            if (!layout_.Size(button, 28, 56) || !layout_.Margin(button, {}) || !layout_.Padding(button, {}) ||
                !stack_.InsertAt(lights_.Get(), button, i))
                return false;
        }
        // Preserve the original auto-measured right group width. Its unrelated
        // first Button must not move just because the three caption controls
        // left. A real empty original layout item replaces only their space.
        txstack::Snapshot remaining;
        if (!stack_.Capture(o.caption.Get(), remaining) ||
            !stack_.InsertAt(o.caption.Get(), rightSpacer_.Get(), remaining.typed.items.size()))
            return false;
        spacerAttached_ = true;
        return layout_.Layout(o.body.Get(), o.title.Get()) && layout_.Layout(o.title.Get()) &&
               layout_.Layout(row_.Get()) && layout_.Layout(lights_.Get()) && layout_.Layout(o.header.Get()) &&
               layout_.Layout(o.caption.Get());
    }
    bool Finish() {
        if (!armed_)
            return ready_;
        if (!Restore())
            return false;
        txlayout::State state;
        txlayout::State currentBody;
        bool viewportChanged =
            layout_.Read(objects_.body.Get(), currentBody) && !EqualRect(&body_.rect, &currentBody.rect);
        auto sameLayout = [&](const txlayout::State &saved, const txlayout::State &actual) {
            if (!viewportChanged)
                return saved.Same(actual);
            // Native relayout after a user resize changes measured size and
            // position. Preserve the new viewport; restore original layout
            // constraints and properties instead of imposing historical bounds.
            if (!EqualRect(&saved.margin, &actual.margin) || !EqualRect(&saved.padding, &actual.padding))
                return false;
            for (size_t i : {0u, 1u, 2u, 3u, 8u, 9u, 10u, 11u})
                if (saved.policy[i] != actual.policy[i])
                    return false;
            return true;
        };
        bool same = layout_.Read(objects_.title.Get(), state) && sameLayout(title_, state) &&
                    layout_.Read(objects_.header.Get(), state) && header_.Same(state) &&
                    layout_.Read(objects_.content.Get(), state) && sameLayout(content_, state) &&
                    layout_.Read(objects_.caption.Get(), state) && sameLayout(caption_, state);
        for (size_t i = 0; i < 3; ++i)
            same = layout_.Read(objects_.buttons[i].Get(), state) && buttons_[i].Same(state) && same;
        if (same)
            armed_ = false;
        return same;
    }
    ~Scope() noexcept {
        if (armed_)
            try {
                Restore();
            } catch (...) {
            }
        try {
            if (row_ && lights_)
                stack_.Remove(row_.Get(), lights_.Get());
        } catch (...) {
        }
    }
};
} // namespace txleft
