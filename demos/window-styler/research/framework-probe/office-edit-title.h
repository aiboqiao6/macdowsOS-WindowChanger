#pragma once

namespace {
// Structure and units verified against the matched Office ABI. Never select a
// title by an address, text, executable name, or the root's data firstChild.
struct OfficeEditorTitle {
    BYTE *row{}, *title{}, *content{};
    std::vector<BYTE *> children;
    static bool Visual(BYTE *node, BYTE *base) {
        std::string name;
        return node && (Value<BYTE **>(node) == reinterpret_cast<BYTE **>(base + 0x84ba60) ||
                        (ClassName(node, name) && name == "AppFrameHelper"));
    }
    bool Find(BYTE *root, BYTE *selected, HWND window, BYTE *base) {
        RECT client{};
        if (!GetClientRect(window, &client))
            return false;
        unsigned depth{};
        for (auto n = selected; n && n != root && ++depth <= 64; n = Value<BYTE *>(n + 8)) {
            auto p = Value<BYTE *>(n + 8);
            std::string a, b;
            if (!p || !ClassName(n, a) || a != "AppFrameHelper" || !ClassName(p, b) || b != a)
                continue;
            auto r = OfficeLocalRect(n), s = OfficeLocalRect(p);
            auto group = OfficeChildren(p);
            if (r.left || r.top || s.left || s.top || !EqualRect(&r, &s) || r.right != client.right ||
                r.bottom != 51 || group.size() != 1 || group[0] != n)
                continue;
            if (title)
                return false;
            content = n;
            title = p;
        }
        row = title ? Value<BYTE *>(title + 8) : nullptr;
        std::string name;
        if (!row || !ClassName(row, name) || name != "AppFrameHelper")
            return false;
        auto r = OfficeLocalRect(row);
        if (r.left || r.top || r.right != client.right || r.bottom != 96)
            return false;
        children = OfficeChildren(content);
        if (children.size() != 2)
            return false;
        for (auto child : children)
            if (!Visual(child, base))
                return false;
        return Layout(window, 0) && client.right - OfficeLocalRect(children.front()).right >= 126 + 600;
    }
    bool Layout(HWND window, LONG leftPadding) const {
        RECT client{};
        if (!GetClientRect(window, &client) || OfficeChildren(content) != children)
            return false;
        auto r = OfficeLocalRect(content), t = OfficeLocalRect(title), p = OfficeLocalRect(row);
        if (t.left || t.top || t.right != client.right || t.bottom != 51 || p.left || p.top ||
            p.right != client.right || p.bottom != 96 || r.left || r.top || r.bottom != 51 ||
            r.right != client.right - leftPadding || r.right < 0)
            return false;
        LONG previous{};
        for (auto child : children) {
            auto bounds = OfficeLocalRect(child);
            if (Value<BYTE *>(child + 8) != content || bounds.left < previous || bounds.top < 0 ||
                bounds.right < bounds.left || bounds.right > r.right || bounds.bottom < bounds.top ||
                bounds.bottom > r.bottom)
                return false;
            previous = bounds.right;
        }
        // Reserve the verified title/search/account envelope after the fixed
        // quick-access group. A narrow host can otherwise clip descendants
        // while its two immediate child rectangles still tile perfectly.
        if (leftPadding && r.right - OfficeLocalRect(children.front()).right < 600)
            return false;
        return true;
    }
};
} // namespace
