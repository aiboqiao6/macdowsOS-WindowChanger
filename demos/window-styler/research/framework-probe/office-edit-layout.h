#pragma once

// Bounded, research-only transaction. This is not reachable from the shipping
// lease backend: an editor title must earn its own layout and lifetime checks.
namespace {
DWORD OfficeEditLayoutExperiment(const styler::AdapterContextV1 &context) {
    OfficeLayoutApi api{modules[0].base};
    auto root = reinterpret_cast<BYTE *>(GetWindowLongPtrW(selection.root, 0));
    RECT client{};
    GetClientRect(selection.root, &client);
    if (GetDpiForWindow(context.window) != 144 || IsIconic(context.window) ||
        !IsWindowEnabled(context.window) || (GetWindowLongPtrW(context.window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL))
        return ERROR_NOT_SUPPORTED;
    BYTE *content{}, *title{};
    unsigned depth{};
    for (auto n = selection.parent; n && n != root && ++depth <= 64; n = Value<BYTE *>(n + 8)) {
        auto p = Value<BYTE *>(n + 8);
        std::string a, b;
        if (!p || !ClassName(n, a) || a != "AppFrameHelper" || !ClassName(p, b) || b != a)
            continue;
        auto r = OfficeLocalRect(n), s = OfficeLocalRect(p);
        auto children = OfficeChildren(p);
        if (r.left || r.top || s.left || s.top || !EqualRect(&r, &s) || r.right != client.right ||
            r.bottom != 51 || children.size() != 1 || children[0] != n)
            continue;
        if (title)
            return ERROR_NOT_SUPPORTED;
        content = n;
        title = p;
    }
    auto row = title ? Value<BYTE *>(title + 8) : nullptr;
    std::string rowName;
    if (!title || !row || !ClassName(row, rowName) || rowName != "AppFrameHelper" ||
        OfficeChildren(content).size() < 2 || Value<POINT>(row + 0x78).x || Value<POINT>(row + 0x78).y)
        return ERROR_NOT_SUPPORTED;
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&OfficeEditLayoutExperiment), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    auto directory = std::filesystem::path(path).parent_path();
    auto prefix = L"office-layout-" + std::to_wstring(GetCurrentProcessId());
    unsigned checks{}, failures{};
    std::vector<std::string> failed;
    auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            ++failures;
            failed.emplace_back(label);
        }
        return ok;
    };
    struct Saved {
        BYTE *node;
        size_t property;
        BYTE *value;
    };
    std::vector<Saved> saved;
    std::vector<BYTE *> pins;
    auto retain = [&](BYTE *n) {
        if (!n || (Value<BYTE>(n + 0x64) & 0xc) >= 8)
            return false;
        auto pin = api.Fn<BYTE *(*)(BYTE *)>(0x170340)(n);
        if (pin)
            pins.push_back(pin);
        return pin != nullptr;
    };
    auto save = [&](BYTE *n, size_t property) {
        auto value = n ? api.Get(n, property) : nullptr;
        if (value)
            saved.push_back({n, property, value});
        return value != nullptr;
    };
    auto childOrder = OfficeChildren(title), contentOrder = OfficeChildren(content), rowOrder = OfficeChildren(row);
    std::vector<RECT> childRects;
    for (auto n : contentOrder)
        childRects.push_back(OfficeLocalRect(n));
    bool ready = check(retain(root) && retain(title) && retain(content) && retain(row), "retain-editor-title-lifetime");
    ready &= check(save(title, 0x83f398), "save-editor-padding");
    for (auto button : selection.buttons) {
        auto glyph = Value<BYTE *>(button + 0x10);
        ready &= check(retain(button) && retain(glyph), "retain-original-button-and-glyph");
        for (auto property : {size_t(0x83fb10), size_t(0x83fba0), size_t(0x837128), size_t(0x948250)})
            ready &= check(save(button, property), "save-original-button-property");
        ready &= check(save(glyph, 0x83fb10), "save-original-glyph-visibility");
    }
    auto before = OfficeCaptureTop(selection.root, context.window);
    ready &= check(before.Save(directory / (prefix + L"-before.bmp")), "capture-editor-before");
    RECT outerBefore{}, clientBefore{};
    GetWindowRect(context.window, &outerBefore);
    GetClientRect(context.window, &clientBefore);
    bool modified{};
    RECT measuredContent{}, measuredTitle{};
    OfficeFrameScope frame;
    std::array<BYTE *, 3> created{};
    if (ready) {
        // The original padding rectangle uses physical pixels in this ABI.
        auto margin = api.Fn<BYTE *(*)(int, int, int, int)>(0xb82a0)(126, 0, 0, 0);
        modified = true;
        ready &= check(margin && Value<short>(margin + 4) == 10 &&
                           SUCCEEDED(api.Set(title, 0x83f398, margin)),
                       "set-original-rect-padding");
        api.Release(margin);
        // Obtain the runtime's own cached false Value from its verified bool
        // wrapper. A bool must not be replaced with an int-typed Value.
        auto falseValue = Value<BYTE *>(api.base + 0xac8490);
        ready &= check(falseValue && Value<short>(falseValue + 4) == 3, "original-boolean-value-type");
        if (ready)
            for (auto button : selection.buttons) {
                check(SUCCEEDED(api.Visible(button, false)), "hide-original-editor-button");
                check(SUCCEEDED(api.Visible(Value<BYTE *>(button + 0x10), false)), "hide-original-editor-glyph");
                check(SUCCEEDED(api.Set(button, 0x83fba0, falseValue)), "disable-original-editor-button");
                check(SUCCEEDED(api.Int(button, 0x837128, 0)), "remove-original-caption-part");
                check(SUCCEEDED(api.Int(button, 0x948250, HTCAPTION)), "remove-original-caption-hit");
                auto enabled = api.Get(button, 0x83fba0);
                check(enabled == falseValue, "verify-original-button-disabled");
                api.Release(enabled);
            }
        constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
        constexpr int commands[]{SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE}, hits[]{HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
        for (unsigned i = 0; ready && i < 3; ++i) {
            ready &= check(SUCCEEDED(api.Fn<HRESULT (*)(BYTE *, BYTE **)>(0x1c1180)(api.base + 0xd959f0, &created[i])) &&
                               created[i] && retain(created[i]), "create-original-editor-light");
            if (!ready)
                break;
            for (auto property : {std::pair<size_t, int>{0x838600, 2}, {0x949f20, 15 + static_cast<int>(i) * 30},
                                  {0x949f60, 0}, {0x83f290, 20}, {0x83f250, 34}, {0x83fd30, 5}, {0x837128, 1},
                                  {0x8371a0, commands[i]}, {0x948250, hits[i]}, {0x8387c0, 18}})
                ready &= check(SUCCEEDED(api.Int(created[i], property.first, property.second)), "set-editor-light-property");
            ready &= check(SUCCEEDED(api.Text(created[i], 0x83ff48, L"\x25cf")) &&
                               SUCCEEDED(api.Text(created[i], 0x83f3d8, L"Segoe UI Symbol")) &&
                               SUCCEEDED(api.Color(created[i], colors[i])) && SUCCEEDED(api.Visible(created[i], true)),
                           "set-original-editor-light-appearance");
        }
        if (ready)
            ready &= check(SUCCEEDED(api.Insert(row, created.data(), 3)) && frame.Start(context.window, selection.root),
                           "insert-editor-lights-and-suppress-native-caption");
        OfficeRefresh(selection.root);
        SetWindowPos(context.window, nullptr, 0, 0, outerBefore.right - outerBefore.left - 1,
                     outerBefore.bottom - outerBefore.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(context.window, nullptr, 0, 0, outerBefore.right - outerBefore.left,
                     outerBefore.bottom - outerBefore.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        OfficeRefresh(selection.root);
        auto r = OfficeLocalRect(content), s = OfficeLocalRect(title);
        measuredContent = r;
        measuredTitle = s;
        check(r.left == 0 && r.top == 0 && r.right + 126 == s.right && r.bottom == s.bottom,
              "126-pixel-padding-reserves-left-space-without-right-overflow");
        auto painted = OfficeCaptureTop(selection.root, context.window);
        for (unsigned i = 0; ready && i < 3; ++i) {
            auto size = Value<SIZE>(created[i] + 0x80);
            LONG x = 15 + static_cast<LONG>(i) * 30;
            RECT rect{x, 0, x + size.cx, size.cy};
            frame.buttons[i] = rect;
            check(rect.left >= 0 && rect.right <= 126 && rect.top >= 0 && rect.bottom <= 51,
                  "editor-light-in-reserved-left-area");
            check(painted.Count(colors[i], rect) >= 3, "editor-native-light-pixels");
        }
        check(painted.Save(directory / (prefix + L"-painted.bmp")), "capture-editor-margin-and-suppression");
        RECT nowOuter{}, nowClient{};
        GetWindowRect(context.window, &nowOuter);
        GetClientRect(context.window, &nowClient);
        check(EqualRect(&outerBefore, &nowOuter) && EqualRect(&clientBefore, &nowClient),
              "editor-window-and-client-geometry-preserved");
    }
    bool restored = true;
    for (auto n : created)
        if (n) {
            if (Value<BYTE *>(n + 8) == row)
                restored &= check(SUCCEEDED(api.Remove(row, n)), "remove-owned-editor-light");
            api.Destroy(n);
        }
    for (auto &value : saved) {
        if (modified) {
            auto hr = Value<short>(value.value + 4) == -1
                          ? api.Fn<HRESULT (*)(BYTE *, BYTE *, bool, void *)>(0x285fa0)(
                                value.node, api.base + value.property, false, nullptr)
                          : api.Set(value.node, value.property, value.value);
            restored &= check(SUCCEEDED(hr), "restore-original-editor-property");
            auto actual = api.Get(value.node, value.property);
            restored &= check(actual == value.value, "verify-original-editor-property-restored");
            api.Release(actual);
        }
        api.Release(value.value);
    }
    frame.Restore();
    OfficeRefresh(selection.root);
    SetWindowPos(context.window, nullptr, 0, 0, outerBefore.right - outerBefore.left - 1,
                 outerBefore.bottom - outerBefore.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(context.window, nullptr, 0, 0, outerBefore.right - outerBefore.left,
                 outerBefore.bottom - outerBefore.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    OfficeRefresh(selection.root);
    restored &= check(OfficeChildren(title) == childOrder && OfficeChildren(content) == contentOrder,
                      "original-title-child-order-restored");
    restored &= check(OfficeChildren(row) == rowOrder, "original-editor-row-order-restored");
    for (size_t i = 0; i < contentOrder.size(); ++i) {
        auto actual = OfficeLocalRect(contentOrder[i]);
        restored &= check(EqualRect(&actual, &childRects[i]), "original-title-content-geometry-restored");
    }
    auto after = OfficeCaptureTop(selection.root, context.window);
    check(after.Save(directory / (prefix + L"-restored.bmp")), "capture-editor-restored");
    for (auto pin : pins)
        api.Release(pin);
    std::ofstream report(directory / (prefix + L".json"));
    report << "{\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (restored ? "true" : "false")
           << ",\"editorPaddingExperiment\":true,\"persistentSession\":false,\"failed\":[";
    for (size_t i = 0; i < failed.size(); ++i) {
        if (i)
            report << ',';
        report << '"' << failed[i] << '"';
    }
    auto getInt = api.Fn<int (*)(BYTE *, BYTE *, bool *)>(0x333d0);
    report << "],\"contentRect\":[" << measuredContent.left << ',' << measuredContent.top << ','
           << measuredContent.right << ',' << measuredContent.bottom << "],\"titleRect\":[" << measuredTitle.left
           << ',' << measuredTitle.top << ',' << measuredTitle.right << ',' << measuredTitle.bottom
           << "],\"titleLayoutChildren\":" << getInt(title, api.base + 0x8384a8, nullptr)
           << ",\"contentLayoutPosition\":" << getInt(content, api.base + 0x838600, nullptr)
           << ",\"paddingPixels\":126,\"layoutVerified\":" << (ready && !failures ? "true" : "false") << "}\n";
    selection = {};
    return ERROR_NOT_SUPPORTED;
}
} // namespace
