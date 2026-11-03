#pragma once
#include "txgui-left-layout.h"
#include "txgui-live-state.h"
#include "txgui-caption-pixels.h"
#include "txgui-d2d-filter.h"
#include <sstream>

namespace {
struct LeftDrawLease {
    ComPtr<IUnknown> list;
    ComPtr<txd2d::Filter> filter;
    bool attached{};
    bool Attach(IUnknown *button, RECT circle, DWORD color, POINT origin) {
        constexpr GUID id{0x85675a56, 0xf6f5, 0x4d26, {0x83, 0x34, 0x88, 0x8f, 0xf3, 0x1d, 0xde, 0x43}};
        if (FAILED(button->QueryInterface(id, &list)) || !list)
            return false;
        auto table = *reinterpret_cast<void ***>(list.Get());
        if (table != reinterpret_cast<void **>(gfImage + 0x1f50fc) || table[7] != gfImage + 0x937d0 ||
            table[8] != gfImage + 0x937f0)
            return false;
        filter.Attach(new txd2d::Filter(button, gfImage, circle, color));
        filter->RequireOrigin(origin);
        if (!filter->Ready())
            return false;
        attached = SUCCEEDED(reinterpret_cast<txobserve::FilterChange>(table[7])(list.Get(), filter.Get()));
        filter->Enable(attached);
        return attached && filter->References() == 2;
    }
    bool Detach() {
        if (!attached)
            return true;
        auto table = *reinterpret_cast<void ***>(list.Get());
        if (FAILED(reinterpret_cast<txobserve::FilterChange>(table[8])(list.Get(), filter.Get())))
            return false;
        attached = false;
        filter->Enable(false);
        return filter->References() == 1;
    }
    ~LeftDrawLease() {
        Detach();
    }
};
bool LeftDescendant(const OrderTree &tree, uintptr_t child, uintptr_t ancestor) {
    for (unsigned depth = 0; child && depth <= 48; ++depth) {
        if (child == ancestor)
            return true;
        auto found = tree.find(child);
        if (found == tree.end())
            return false;
        child = found->second.parent;
    }
    return false;
}
bool LeftSelection(const OrderTree &tree, const CaptionGroup &selected, txleft::Objects &objects) {
    objects.buttons = selected.buttons;
    objects.caption = selected.parent;
    auto caption = tree.find(reinterpret_cast<uintptr_t>(selected.parent.Get()));
    if (caption == tree.end())
        return false;
    auto title = tree.find(caption->second.parent);
    auto body = title == tree.end() ? tree.end() : tree.find(title->second.parent);
    if (title == tree.end() || body == tree.end())
        return false;
    objects.title = title->second.object;
    objects.body = body->second.object;
    for (auto id : body->second.children) {
        auto &node = tree.at(id);
        if (node.visible && !node.hidden && node.rect.left == 0 && node.rect.top == 0 &&
            node.rect.right == title->second.rect.left && node.rect.bottom > title->second.rect.bottom) {
            if (objects.sidebar)
                return false;
            objects.sidebar = node.object;
        }
    }
    if (!objects.sidebar)
        return false;
    for (auto id : tree.at(reinterpret_cast<uintptr_t>(objects.sidebar.Get())).children) {
        auto &node = tree.at(id);
        if (*reinterpret_cast<void ***>(node.object.Get()) == reinterpret_cast<void **>(gfImage + 0x220f58) &&
            node.visible && !node.hidden && node.rect.top >= 0 && node.rect.bottom <= title->second.rect.bottom &&
            node.rect.right > node.rect.left && !node.children.empty()) {
            if (objects.header)
                return false;
            objects.header = node.object;
        }
    }
    for (auto id : title->second.children) {
        auto &node = tree.at(id);
        if (node.object != objects.caption && node.visible && !node.hidden && node.rect.left == 0 &&
            node.rect.right > 0 &&
            node.rect.bottom - node.rect.top == title->second.rect.bottom - title->second.rect.top) {
            if (objects.content)
                return false;
            objects.content = node.object;
        }
    }
    return objects.header && objects.content;
}
bool LeftHit(IUnknown *root, POINT point, IUnknown *expected) {
    constexpr BYTE entry[]{0x55, 0x8b, 0xec, 0x5d, 0xe9, 0x57, 0x29, 0x00, 0x00};
    if (memcmp(gfImage + 0x66e20, entry, sizeof(entry)))
        return false;
    using Hit = HRESULT(__cdecl *)(IUnknown *, const POINT *, IUnknown **);
    ComPtr<IUnknown> found;
    return SUCCEEDED(reinterpret_cast<Hit>(gfImage + 0x66e20)(root, &point, &found)) && found.Get() == expected;
}
} // namespace

#ifdef TXGUI_LEFT_LEASE
#include "txgui-left-lease.h"
#else
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    if (!styler::ValidContext(context) || context->adapterId != 278 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId()) {
        captionGroup = {};
        return ERROR_NOT_SUPPORTED;
    }
    auto selected = std::move(captionGroup);
    captionGroup = {};
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
    wchar_t module[32768]{};
    GetModuleFileNameW(self, module, 32768);
    auto stem = std::filesystem::path(module).parent_path() / (L"txgui-left-" + std::to_wstring(GetCurrentProcessId()));
    std::ofstream report(stem.wstring() + L".json", std::ios::trunc);
    if (!report)
        return ERROR_WRITE_FAULT;
    unsigned checks{}, failures{};
    std::vector<std::string> failed;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            ++failures;
            failed.emplace_back(name);
        }
        return ok;
    };
    auto window = context->window;
    auto dpi = GetDpiForWindow(window);
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    bool ready = check(dpi == 144 && !IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) &&
                           !GetCapture() && !GetPropW(window, styler::OwnerProperty),
                       "original-window-profile-and-idle-input");
    OrderTree before;
    ready &= check(ReadOrderTree(selected.root.Get(), before), "complete-original-tree-before-combined-layout");
    txleft::Objects objects;
    ready &= check(LeftSelection(before, selected, objects),
                   "unique-semantic-caption-and-geometric-header-content-selection");
    RECT leftCapture{}, rightCapture{};
    if (ready) {
        auto title = before.at(reinterpret_cast<uintptr_t>(objects.title.Get()));
        POINT origin{};
        ClientToScreen(window, &origin);
        leftCapture = {origin.x + MulDiv(8, dpi, 96), origin.y + MulDiv(8, dpi, 96), origin.x + MulDiv(316, dpi, 96),
                       origin.y + MulDiv(68, dpi, 96)};
        for (size_t i = 0; i < 3; ++i) {
            auto rect = before.at(reinterpret_cast<uintptr_t>(objects.buttons[i].Get())).screen;
            if (!i)
                rightCapture = rect;
            else
                UnionRect(&rightCapture, &rightCapture, &rect);
        }
    }
    CaptionPixels leftBefore, leftPainted, leftAfter, rightBefore, rightPainted, rightAfter;
    auto repaint = [&] {
        return RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE;
    };
    bool preflight{}, modified{}, restored{};
    if (ready) {
        CaptionPixels warmLeft, warmRight;
        preflight = repaint() && leftBefore.Read(window, leftCapture) && rightBefore.Read(window, rightCapture) &&
                    repaint() && warmLeft.Read(window, leftCapture) && warmRight.Read(window, rightCapture) &&
                    leftBefore.pixels == warmLeft.pixels && rightBefore.pixels == warmRight.pixels;
        ready &= check(preflight, "unoccluded-bounded-original-header-and-caption-pixels-stable");
    }
    std::array<RECT, 3> buttonRects{};
    std::array<txd2d::Drawing, 3> drawings;
    std::ostringstream mismatches;
    unsigned mismatchCount{};
    if (ready) {
        struct Repaint {
            HWND window;
            ~Repaint() {
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        } repaintOnExit{window};
        txleft::Scope transaction(gfImage, objects);
        std::array<LeftDrawLease, 3> leases;
        ready &= check(transaction.Ready(), "combined-original-layout-transaction-ready-before-first-live-write");
        if (ready) {
            modified = transaction.Apply();
            ready &= check(modified, "migrate-real-header-and-original-three-buttons-through-original-layout");
        }
        if (ready) {
            OrderTree during;
            ready &= check(ReadOrderTree(selected.root.Get(), during) && during.size() == before.size() + 3,
                           "complete-original-tree-retains-every-original-node-plus-three-native-layout-containers");
            bool states = true;
            auto titleId = reinterpret_cast<uintptr_t>(objects.title.Get());
            auto headerId = reinterpret_cast<uintptr_t>(objects.header.Get());
            auto contentId = reinterpret_cast<uintptr_t>(objects.content.Get());
            auto captionId = reinterpret_cast<uintptr_t>(objects.caption.Get());
            auto sidebarId = reinterpret_cast<uintptr_t>(objects.sidebar.Get());
            for (auto &[id, node] : before) {
                auto found = during.find(id);
                if (found == during.end()) {
                    states = false;
                    continue;
                }
                auto actual = found->second;
                bool button = std::any_of(objects.buttons.begin(), objects.buttons.end(),
                                          [&](const auto &b) { return b == node.object; });
                if (button) {
                    actual.parent = node.parent;
                    actual.rect = node.rect;
                    actual.screen = node.screen;
                    actual.margin = node.margin;
                    actual.padding = node.padding;
                } else if (id == titleId) {
                    actual.rect = node.rect;
                    actual.screen = node.screen;
                    actual.margin = node.margin;
                } else if (LeftDescendant(before, id, headerId) || LeftDescendant(before, id, contentId)) {
                    OffsetRect(&actual.screen, -MulDiv(84, dpi, 96), 0);
                    if (id == headerId) {
                        actual.parent = node.parent;
                        OffsetRect(&actual.rect, -84, 0);
                    }
                    if (id == contentId) {
                        OffsetRect(&actual.rect, -316, 0);
                        actual.margin = node.margin;
                    }
                } else if (id == captionId) {
                    OffsetRect(&actual.rect, -232, 0);
                }
                auto expectedChildren = node.children;
                if (id == titleId || id == sidebarId || id == captionId)
                    actual.children = expectedChildren;
                bool same = node.SameState(actual) && expectedChildren == actual.children;
                if (!same) {
                    if (mismatchCount++)
                        mismatches << ',';
                    auto rect = [&](RECT value) {
                        mismatches << '[' << value.left << ',' << value.top << ',' << value.right << ',' << value.bottom
                                   << ']';
                    };
                    mismatches << "{\"id\":" << id << ",\"parentBefore\":" << node.parent
                               << ",\"parentAfter\":" << actual.parent << ",\"rectBefore\":";
                    rect(node.rect);
                    mismatches << ",\"rectAfter\":";
                    rect(actual.rect);
                    mismatches << ",\"screenBefore\":";
                    rect(node.screen);
                    mismatches << ",\"screenAfter\":";
                    rect(actual.screen);
                    mismatches << ",\"marginBefore\":";
                    rect(node.margin);
                    mismatches << ",\"marginAfter\":";
                    rect(actual.margin);
                    mismatches << ",\"paddingBefore\":";
                    rect(node.padding);
                    mismatches << ",\"paddingAfter\":";
                    rect(actual.padding);
                    mismatches << ",\"visibleBefore\":" << node.visible << ",\"visibleAfter\":" << actual.visible
                               << '}';
                }
                states = same && states;
            }
            check(states, "all-original-bindings-filters-visibility-and-unrelated-geometry-preserved");
            constexpr std::array<DWORD, 3> colors{0xffff5f57, 0xffffbd2e, 0xff28c840};
            POINT origin{};
            ClientToScreen(window, &origin);
            for (size_t i = 0; i < 3; ++i) {
                auto &node = during.at(reinterpret_cast<uintptr_t>(objects.buttons[i].Get()));
                buttonRects[i] = node.screen;
                int width = node.screen.right - node.screen.left, height = node.screen.bottom - node.screen.top;
                int diameter = MulDiv(12, dpi, 96);
                RECT circle{(width - diameter) / 2, (height - diameter) / 2, (width + diameter) / 2,
                            (height + diameter) / 2};
                POINT point{node.screen.left + width / 2, node.screen.top + height / 2};
                check(width == MulDiv(28, dpi, 96) && height == MulDiv(56, dpi, 96) &&
                          node.screen.left == origin.x + MulDiv(static_cast<int>(i) * 28, dpi, 96) &&
                          LeftHit(selected.root.Get(), point, objects.buttons[i].Get()),
                      "relocated-original-caption-has-left-geometry-and-original-native-hit-identity");
                ready &= check(leases[i].Attach(objects.buttons[i].Get(), circle, colors[i],
                                                {node.screen.left - origin.x, node.screen.top - origin.y}),
                               "attach-only-owned-original-D2D-content-filter-to-relocated-button");
            }
            if (ready) {
                check(repaint() && leftPainted.Read(window, leftCapture) && rightPainted.Read(window, rightCapture),
                      "normal-original-renderer-paints-relocated-controls-without-synthetic-notifications");
                for (size_t i = 0; i < 3; ++i) {
                    drawings[i] = leases[i].filter->ReadDrawing();
                    auto r = buttonRects[i];
                    int x = (r.left + r.right) / 2 - leftCapture.left, y = (r.top + r.bottom) / 2 - leftCapture.top;
                    auto offset = static_cast<size_t>(y) * leftPainted.width + x;
                    check(drawings[i].paints > 0 && !drawings[i].rejected && !drawings[i].stateChanges && x >= 0 &&
                              y >= 0 && offset < leftPainted.pixels.size() &&
                              leftPainted.pixels[offset] == (colors[i] & 0xffffff),
                          "actual-relocated-light-center-color-and-original-D2D-state-verified");
                }
            }
        }
        for (auto &lease : leases)
            check(lease.Detach(), "remove-only-owned-left-light-paint-filter-before-layout-rollback");
        restored = transaction.Finish();
        check(restored, "combined-rollback-restores-original-orders-and-complete-layout-records");
    }
    OrderTree final;
    check(ReadOrderTree(selected.root.Get(), final) && SameOrderTree(before, final),
          "all-original-tree-state-and-topology-restored-exactly");
    if (modified) {
        check(repaint() && leftAfter.Read(window, leftCapture) && rightAfter.Read(window, rightCapture) &&
                  leftAfter.pixels == leftBefore.pixels && rightAfter.pixels == rightBefore.pixels,
              "native-repaint-after-undo-restores-both-bounded-pixel-regions-exactly");
        check(leftBefore.Save(stem.wstring() + L"-left-before.bmp") &&
                  leftPainted.Save(stem.wstring() + L"-left-painted.bmp") &&
                  leftAfter.Save(stem.wstring() + L"-left-after.bmp") &&
                  rightBefore.Save(stem.wstring() + L"-right-before.bmp") &&
                  rightPainted.Save(stem.wstring() + L"-right-painted.bmp") &&
                  rightAfter.Save(stem.wstring() + L"-right-after.bmp"),
              "bounded-original-header-and-caption-pixel-evidence-saved");
    }
    RECT finalOuter{}, finalClient{};
    check(GetWindowRect(window, &finalOuter) && GetClientRect(window, &finalClient) && EqualRect(&outer, &finalOuter) &&
              EqualRect(&client, &finalClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
              GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle && !GetCapture() &&
              !GetPropW(window, styler::OwnerProperty),
          "original-HWND-geometry-style-input-capture-and-owner-preserved");
    report << "{\"pid\":" << GetCurrentProcessId() << ",\"nodes\":" << before.size()
           << ",\"pixelPreflight\":" << (preflight ? "true" : "false")
           << ",\"layoutModified\":" << (modified ? "true" : "false")
           << ",\"inputSent\":false,\"persistentSession\":false,\"syntheticNotifications\":false,\"lights\":[";
    for (size_t i = 0; i < 3; ++i) {
        if (i)
            report << ',';
        auto r = buttonRects[i];
        report << "{\"id\":" << reinterpret_cast<uintptr_t>(objects.buttons[i].Get()) << ",\"screen\":[" << r.left
               << ',' << r.top << ',' << r.right << ',' << r.bottom << "],\"paints\":" << drawings[i].paints
               << ",\"rejected\":" << drawings[i].rejected << '}';
    }
    report << "],\"mismatches\":[" << mismatches.str() << "],\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (restored ? "true" : "false") << ",\"failedChecks\":[";
    for (size_t i = 0; i < failed.size(); ++i) {
        if (i)
            report << ',';
        report << '"' << failed[i] << '"';
    }
    report << "]}\n";
    return ERROR_NOT_SUPPORTED;
} catch (...) {
    captionGroup = {};
    return ERROR_UNHANDLED_EXCEPTION;
}
#endif
