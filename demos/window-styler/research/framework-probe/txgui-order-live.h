#pragma once
#include "txgui-live-state.h"

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) try {
    if (!styler::ValidContext(context) || context->adapterId != 276 || !captionGroup.Ready() ||
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
    auto path = std::filesystem::path(module).parent_path() /
                (L"txgui-order-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    std::ofstream report(path, std::ios::trunc);
    if (!self || !report) {
        paintParents.clear();
        return ERROR_WRITE_FAULT;
    }
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
    bool ready = check(!IsIconic(window) && !IsZoomed(window) && IsWindowEnabled(window) && !GetCapture() &&
                           !GetPropW(window, styler::OwnerProperty),
                       "original-normal-enabled-window-without-capture-or-owner");
    OrderTree before;
    ready &= check(ReadOrderTree(selected.root.Get(), before),
                   "complete-original-tree-state-bindings-and-topology-captured");
    auto parentId = [&](IUnknown *object) -> uintptr_t {
        auto found = paintParents.find(reinterpret_cast<uintptr_t>(object));
        return found == paintParents.end() ? 0 : reinterpret_cast<uintptr_t>(found->second.Get());
    };
    auto title = before.find(parentId(selected.parent.Get()));
    auto body = title == before.end() ? before.end() : before.find(title->second.parent);
    std::vector<uintptr_t> sidebars, headers;
    if (title != before.end() && body != before.end()) {
        auto bounds = title->second.rect;
        for (auto id : body->second.children) {
            auto &node = before.at(id);
            if (node.visible && !node.hidden && node.rect.left == 0 && node.rect.top == 0 &&
                node.rect.right == bounds.left && node.rect.bottom > bounds.bottom)
                sidebars.push_back(id);
        }
        if (sidebars.size() == 1) {
            for (auto id : before.at(sidebars[0]).children) {
                auto &node = before.at(id);
                if (*reinterpret_cast<void ***>(node.object.Get()) == reinterpret_cast<void **>(gfImage + 0x220f58) &&
                    node.visible && !node.hidden && node.rect.top >= 0 && node.rect.bottom <= bounds.bottom &&
                    node.rect.right > node.rect.left && !node.children.empty())
                    headers.push_back(id);
            }
        }
    }
    ready &= check(sidebars.size() == 1 && headers.size() == 1,
                   "unique-original-left-header-from-caption-ancestry-and-geometry-not-business-names");
    paintParents.clear();
    uintptr_t source = sidebars.size() == 1 ? sidebars[0] : 0, header = headers.size() == 1 ? headers[0] : 0;
    bool modified{}, restored{}, duringState{};
    txorder::Snapshot original, changed, after;
    if (ready) {
        txorder::Api api(gfImage);
        txorder::Api::ScopedRestore rollback(api, before.at(source).object.Get());
        ready &=
            check(rollback.Ready(), "both-original-Element-and-Frame-chain-profiles-and-parent-identities-verified");
        if (ready) {
            original = rollback.Original();
            auto desired = original;
            bool targetFound = true;
            for (auto chain : {&desired.elements, &desired.frames}) {
                auto item = std::find_if(chain->items.begin(), chain->items.end(), [&](const auto &value) {
                    return reinterpret_cast<uintptr_t>(value.Get()) == header;
                });
                targetFound &= item != chain->items.end() && item + 1 != chain->items.end();
                if (item != chain->items.end())
                    std::rotate(item, item + 1, chain->items.end());
            }
            ready &=
                check(targetFound && !desired.elements.Same(original.elements) && !desired.frames.Same(original.frames),
                      "target-is-an-interior-original-child-in-both-orders");
            if (ready) {
                rollback.Arm();
                modified = api.Restore(desired);
                check(modified && api.Capture(before.at(source).object.Get(), changed) &&
                          desired.elements.Same(changed.elements) && desired.frames.Same(changed.frames),
                      "bounded-real-original-link-setters-change-both-orders-without-Remove-Insert-or-parent-setters");
                OrderTree during;
                duringState = ReadOrderTree(selected.root.Get(), during) && SameOrderTree(before, during, source);
                check(duringState,
                      "all-original-parent-identities-geometry-state-bindings-and-other-child-orders-stay-unchanged");
                restored = rollback.Finish();
                check(restored && api.Capture(before.at(source).object.Get(), after) &&
                          original.elements.Same(after.elements) && original.frames.Same(after.frames),
                      "restore-and-read-back-both-exact-original-orders");
            }
        }
    }
    // The no-allocation scope rollback has also completed before this check.
    OrderTree final;
    check(ReadOrderTree(selected.root.Get(), final) && SameOrderTree(before, final),
          "complete-original-tree-topology-geometry-and-all-original-button-bindings-restored");
    RECT finalOuter{}, finalClient{};
    check(GetWindowRect(window, &finalOuter) && GetClientRect(window, &finalClient) && EqualRect(&outer, &finalOuter) &&
              EqualRect(&client, &finalClient) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
              GetWindowLongPtrW(window, GWL_EXSTYLE) == exStyle && !GetCapture() &&
              !GetPropW(window, styler::OwnerProperty),
          "original-window-geometry-styles-capture-and-owner-unchanged");
    report << "{\"pid\":" << GetCurrentProcessId() << ",\"nodes\":" << before.size()
           << ",\"geometryModified\":false,\"parentsChanged\":false,\"pixelsWritten\":false,\"inputSent\":false,"
              "\"persistentSession\":false,\"orderModified\":"
           << (modified ? "true" : "false") << ",\"duringStatePreserved\":" << (duringState ? "true" : "false")
           << ",\"originalParent\":" << source << ",\"originalHeader\":" << header << ",\"ElementBefore\":";
    OrderIds(report, original.elements);
    report << ",\"ElementDuring\":";
    OrderIds(report, changed.elements);
    report << ",\"ElementAfter\":";
    OrderIds(report, after.elements);
    report << ",\"FrameBefore\":";
    OrderIds(report, original.frames);
    report << ",\"FrameDuring\":";
    OrderIds(report, changed.frames);
    report << ",\"FrameAfter\":";
    OrderIds(report, after.frames);
    report << ",\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (restored && failures == 0 ? "true" : "false") << ",\"failedChecks\":[";
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
