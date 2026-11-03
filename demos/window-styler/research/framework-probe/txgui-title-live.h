#pragma once
#include "txgui-live-state.h"
#include "txgui-frame-layout.h"
#include "txgui-caption-pixels.h"

namespace {
struct TitleLeaseState {
    ComPtr<IUnknown> object;
    txlayout::State layout;
};
bool SameTitleTree(const OrderTree &before, const OrderTree &during, uintptr_t title) {
    if (before.size() != during.size())
        return false;
    auto oldTitle = before.find(title), newTitle = during.find(title);
    if (oldTitle == before.end() || newTitle == during.end())
        return false;
    auto shift = oldTitle->second.rect.left - newTitle->second.rect.left;
    for (auto &[id, node] : before) {
        auto found = during.find(id);
        if (found == during.end())
            return false;
        auto actual = found->second;
        if (id == title) {
            actual.rect = node.rect;
            actual.screen = node.screen;
            actual.margin = node.margin;
            actual.padding = node.padding;
        } else if (node.parent == title) {
            OffsetRect(&actual.rect, -shift, 0);
        }
        if (!node.SameState(actual) || node.children != actual.children)
            return false;
    }
    return true;
}
void TitleRect(std::ostream &report, RECT rect) {
    report << '[' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << ']';
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    if (!styler::ValidContext(context) || context->adapterId != 277 || !captionGroup.Ready() ||
        captionGroup.window != context->window ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId()) {
        captionGroup = {};
        paintParents.clear();
        return ERROR_NOT_SUPPORTED;
    }
    auto selected = std::move(captionGroup);
    captionGroup = {};
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
    wchar_t module[32768]{};
    GetModuleFileNameW(self, module, 32768);
    auto stem =
        std::filesystem::path(module).parent_path() / (L"txgui-title-" + std::to_wstring(GetCurrentProcessId()));
    std::ofstream report(stem.wstring() + L".json", std::ios::trunc);
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
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!self || !report) {
        paintParents.clear();
        return ERROR_WRITE_FAULT;
    }
    bool ready = check(!IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) && !GetCapture() &&
                           !GetPropW(window, styler::OwnerProperty),
                       "original-normal-window-and-fresh-report-without-input-capture-or-owner");
    OrderTree before;
    ready &= check(ReadOrderTree(selected.root.Get(), before), "complete-original-tree-before-layout-transaction");
    auto titleId = paintParents.find(reinterpret_cast<uintptr_t>(selected.parent.Get()));
    auto title =
        titleId == paintParents.end() ? before.end() : before.find(reinterpret_cast<uintptr_t>(titleId->second.Get()));
    auto body = title == before.end() ? before.end() : before.find(title->second.parent);
    ready &= check(title != before.end() && body != before.end() && title->second.rect.left > 0 &&
                       title->second.rect.top == 0 && title->second.rect.bottom <= 100 &&
                       title->second.rect.right == body->second.rect.right - body->second.rect.left,
                   "original-semantic-caption-title-frame-and-existing-full-width-parent-identified");
    paintParents.clear();
    txlayout::Api api(gfImage);
    txlayout::State original{}, expanded{}, restored{};
    std::vector<TitleLeaseState> layouts;
    std::vector<std::array<LONG, 12>> duringPolicies;
    uintptr_t target{};
    RECT leftCapture{}, rightCapture{};
    if (ready) {
        target = title->first;
        ready &= check(api.Read(title->second.object.Get(), original) && original.margin.left == original.rect.left &&
                           original.padding.left == 0 && original.rect.left <= 512,
                       "original-title-layout-profile-margin-and-padding-preconditions");
        for (auto id : title->second.children) {
            TitleLeaseState saved;
            saved.object = before.at(id).object;
            ready &= check(api.Read(saved.object.Get(), saved.layout), "original-direct-child-layout-policy-captured");
            layouts.push_back(saved);
        }
        auto screen = title->second.screen;
        int oldLeft = MulDiv(original.rect.left, GetDpiForWindow(window), 96);
        int inset = MulDiv(8, GetDpiForWindow(window), 96);
        leftCapture = {screen.left - oldLeft + inset, screen.top + inset,
                       screen.left + MulDiv(64, GetDpiForWindow(window), 96), screen.bottom};
        rightCapture = before.at(reinterpret_cast<uintptr_t>(selected.buttons[0].Get())).screen;
        for (unsigned i = 1; i < 3; ++i) {
            auto rect = before.at(reinterpret_cast<uintptr_t>(selected.buttons[i].Get())).screen;
            UnionRect(&rightCapture, &rightCapture, &rect);
        }
    }
    CaptionPixels leftBefore, leftDuring, leftAfter, rightBefore, rightDuring, rightAfter;
    bool pixelPreflight{}, leftReadable{}, rightReadable{};
    auto repaint = [&] {
        return RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE;
    };
    if (ready) {
        CaptionPixels warmLeft, warmRight;
        bool repainted = repaint();
        leftReadable = leftBefore.Read(window, leftCapture);
        rightReadable = rightBefore.Read(window, rightCapture);
        pixelPreflight = repainted && leftReadable && rightReadable && repaint() &&
                         warmLeft.Read(window, leftCapture) && warmRight.Read(window, rightCapture) &&
                         leftBefore.pixels == warmLeft.pixels && rightBefore.pixels == warmRight.pixels;
        ready &= check(pixelPreflight, "unoccluded-original-header-and-caption-pixels-stable-before-transaction");
    }
    bool modified{}, didRestore{};
    if (ready) {
        struct Repaint {
            HWND window;
            ~Repaint() {
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        } repaintOnExit{window};
        txlayout::Scope transaction(api, body->second.object.Get(), title->second.object.Get());
        if (check(transaction.Ready(), "original-title-scoped-rollback-ready-before-first-write")) {
            modified = transaction.ExpandLeft();
            check(modified && api.Read(title->second.object.Get(), expanded) && expanded.rect.left == 0 &&
                      expanded.rect.right == original.rect.right && expanded.rect.top == original.rect.top &&
                      expanded.rect.bottom == original.rect.bottom && expanded.margin.left == 0 &&
                      expanded.padding.left == original.margin.left,
                  "real-original-frame-expands-left-through-own-layout-without-new-caption-height");
            OrderTree during;
            check(ReadOrderTree(selected.root.Get(), during) && SameTitleTree(before, during, target),
                  "all-original-topology-bindings-filters-and-absolute-child-geometry-stay-unchanged");
            bool childPolicies = true;
            for (auto &saved : layouts) {
                txlayout::State current;
                childPolicies &= api.Read(saved.object.Get(), current);
                auto oldPolicy = saved.layout.policy;
                duringPolicies.push_back(current.policy);
                // Original 0x2f8 layout records contain physical pixels;
                // public frame rect/margin getters return logical DIP.
                oldPolicy[6] += MulDiv(original.rect.left, GetDpiForWindow(window), 96);
                childPolicies &= oldPolicy == current.policy && EqualRect(&saved.layout.margin, &current.margin) &&
                                 EqualRect(&saved.layout.padding, &current.padding);
            }
            check(childPolicies, "all-original-child-layout-constraints-preserved-with-only-local-x-origin-adjusted");
            check(repaint() && leftDuring.Read(window, leftCapture) && rightDuring.Read(window, rightCapture) &&
                      leftDuring.pixels == leftBefore.pixels && rightDuring.pixels == rightBefore.pixels,
                  "original-renderer-keeps-existing-left-header-and-caption-pixels-identical-during-expansion");
            didRestore = transaction.Finish();
            check(didRestore && api.Read(title->second.object.Get(), restored) && original.Same(restored),
                  "restore-exact-original-title-bounds-margin-padding-and-layout-policy");
        }
    }
    OrderTree final;
    check(ReadOrderTree(selected.root.Get(), final) && SameOrderTree(before, final),
          "complete-original-tree-state-and-topology-restored-after-layout-scope");
    if (target) {
        bool childPolicies = true;
        for (auto &saved : layouts) {
            txlayout::State current;
            childPolicies &= api.Read(saved.object.Get(), current) && current.Same(saved.layout);
        }
        check(childPolicies, "restore-every-direct-child-original-layout-record-exactly");
    }
    if (modified) {
        check(repaint() && leftAfter.Read(window, leftCapture) && rightAfter.Read(window, rightCapture) &&
                  !leftBefore.pixels.empty() && leftAfter.pixels == leftBefore.pixels &&
                  rightAfter.pixels == rightBefore.pixels,
              "original-repaint-restores-both-bounded-pixel-regions-exactly");
        check(leftBefore.Save(stem.wstring() + L"-left-before.bmp") &&
                  leftDuring.Save(stem.wstring() + L"-left-during.bmp") &&
                  leftAfter.Save(stem.wstring() + L"-left-after.bmp") &&
                  rightBefore.Save(stem.wstring() + L"-right-before.bmp") &&
                  rightDuring.Save(stem.wstring() + L"-right-during.bmp") &&
                  rightAfter.Save(stem.wstring() + L"-right-after.bmp"),
              "bounded-original-header-pixel-evidence-saved");
    }
    RECT finalOuter{}, finalClient{};
    check(GetWindowRect(window, &finalOuter) && GetClientRect(window, &finalClient) && EqualRect(&outer, &finalOuter) &&
              EqualRect(&client, &finalClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
              GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle && !GetCapture() &&
              !GetPropW(window, styler::OwnerProperty),
          "original-HWND-client-geometry-style-capture-and-owner-preserved");
    report << "{\"pid\":" << GetCurrentProcessId() << ",\"nodes\":" << before.size() << ",\"originalTitle\":" << target
           << ",\"pixelPreflight\":" << (pixelPreflight ? "true" : "false")
           << ",\"leftReadable\":" << (leftReadable ? "true" : "false")
           << ",\"rightReadable\":" << (rightReadable ? "true" : "false")
           << ",\"titleExpanded\":" << (modified ? "true" : "false")
           << ",\"parentsChanged\":false,\"inputSent\":false,\"customPixelsWritten\":false,\"persistentSession\":false,"
              "\"before\":";
    TitleRect(report, original.rect);
    report << ",\"during\":";
    TitleRect(report, expanded.rect);
    report << ",\"after\":";
    TitleRect(report, restored.rect);
    report << ",\"leftCapture\":";
    TitleRect(report, leftCapture);
    report << ",\"rightCapture\":";
    TitleRect(report, rightCapture);
    report << ",\"childPolicies\":[";
    for (size_t i = 0; i < layouts.size(); ++i) {
        if (i)
            report << ',';
        report << "{\"id\":" << reinterpret_cast<uintptr_t>(layouts[i].object.Get()) << ",\"before\":[";
        for (size_t j = 0; j < 12; ++j) {
            if (j)
                report << ',';
            report << layouts[i].layout.policy[j];
        }
        report << "],\"during\":[";
        if (i < duringPolicies.size())
            for (size_t j = 0; j < 12; ++j) {
                if (j)
                    report << ',';
                report << duringPolicies[i][j];
            }
        report << "]}";
    }
    report << ']';
    report << ",\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (didRestore && failures == 0 ? "true" : "false") << ",\"failedChecks\":[";
    for (size_t i = 0; i < failed.size(); ++i) {
        if (i)
            report << ',';
        report << '"' << failed[i] << '"';
    }
    report << "]}\n";
    return ERROR_NOT_SUPPORTED;
} catch (...) {
    captionGroup = {};
    paintParents.clear();
    return ERROR_UNHANDLED_EXCEPTION;
}
