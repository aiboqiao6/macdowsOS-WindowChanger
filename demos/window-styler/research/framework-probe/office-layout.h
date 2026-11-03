#pragma once
#include <algorithm>
#include <array>
#include <dwmapi.h>
#include "../../native-style-view.h"
#include <windowsx.h>

// Shared by the independently registered bounded and persistent experiments.
// All pixels and input remain in Office's own Element machinery.
namespace {
struct OfficeFrameScope {
    HWND window{}, root{};
    std::array<RECT, 3> buttons{};
    bool reads{}, subclass{}, menu{}, metrics{};
    static LRESULT CALLBACK Dispatch(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<OfficeFrameScope *>(data);
        using namespace styler::style_view;
        if (GetPropW(w, StyleProperty) && (m == WM_STYLECHANGING || m == WM_STYLECHANGED) && wp == WPARAM(GWL_STYLE))
            return MenuStyleMessage(w, m, wp, lp);
        if (m == WM_NCHITTEST) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(self->root, &point);
            for (auto r : self->buttons)
                if (PtInRect(&r, point))
                    return HTCLIENT;
        }
        return DefSubclassProc(w, m, wp, lp);
    }
    bool Start(HWND frame, HWND child) {
        using namespace styler::style_view;
        if (GetPropW(frame, StyleProperty) || GetPropW(frame, CaptionBoxesProperty))
            return false;
        for (auto property : BoundsProperties)
            if (GetPropW(frame, property))
                return false;
        window = frame;
        root = child;
        RECT bounds{}, outer{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds))) ||
            bounds.right <= bounds.left || !GetWindowRect(window, &outer))
            return false;
        int metrics[]{bounds.right - bounds.left, bounds.top, outer.right - outer.left - bounds.right,
                      bounds.bottom - bounds.top};
        this->metrics = true;
        for (unsigned i = 0; i < 4; ++i)
            if (!SetPropW(window, BoundsProperties[i], reinterpret_cast<HANDLE>(INT_PTR(metrics[i] + 1))))
                return false;
        reads = AcquireReads();
        if (!reads)
            return false;
        subclass = SetWindowSubclass(window, Dispatch, 0x4f464652, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
        if (!subclass ||
            !SetPropW(window, CaptionBoxesProperty,
                      reinterpret_cast<HANDLE>((GetWindowLongPtrW(window, GWL_STYLE) & CaptionBoxMask) + 1)) ||
            !SetPropW(window, StyleProperty, reinterpret_cast<HANDLE>(1)))
            return false;
        menu = true;
        HideMenu(window);
        return true;
    }
    void Restore() {
        using namespace styler::style_view;
        if (menu) {
            RestoreMenu(window, true);
            menu = false;
        }
        if (subclass) {
            RemoveWindowSubclass(window, Dispatch, 0x4f464652);
            subclass = false;
        }
        if (window && metrics)
            RemovePropW(window, CaptionBoxesProperty);
        if (window && metrics)
            for (auto property : BoundsProperties)
                RemovePropW(window, property);
        metrics = false;
        if (reads) {
            ReleaseReads();
            reads = false;
        }
    }
    ~OfficeFrameScope() {
        Restore();
    }
};
struct OfficeLayoutApi {
    BYTE *base;
    template <class T> T Fn(size_t rva) const {
        return reinterpret_cast<T>(base + rva);
    }
    BYTE *Get(BYTE *node, size_t property) const {
        return Fn<BYTE *(*)(BYTE *, BYTE *, int, unsigned char)>(0x49c30)(node, base + property, 1, 0);
    }
    void Release(BYTE *value) const {
        if (value)
            Fn<void (*)(BYTE *)>(0x4ab00)(value);
    }
    HRESULT Set(BYTE *node, size_t property, BYTE *value) const {
        return value ? Fn<HRESULT (*)(BYTE *, BYTE *, BYTE *, bool, void *)>(0x484c0)(node, base + property, value,
                                                                                      false, nullptr)
                     : E_OUTOFMEMORY;
    }
    HRESULT Int(BYTE *node, size_t property, int number) const {
        auto value = Fn<BYTE *(*)(int)>(0x9cba0)(number);
        auto result = Set(node, property, value);
        Release(value);
        return result;
    }
    HRESULT Text(BYTE *node, size_t property, const wchar_t *text) const {
        auto value = Fn<BYTE *(*)(const wchar_t *, HINSTANCE, int, int)>(0x32e60)(text, nullptr, 1, -1);
        auto result = Set(node, property, value);
        Release(value);
        return result;
    }
    HRESULT Color(BYTE *node, COLORREF color) const {
        auto value = Fn<BYTE *(*)(COLORREF)>(0x135480)(color);
        auto result = Set(node, 0x83b790, value);
        Release(value);
        return result;
    }
    HRESULT Visible(BYTE *node, bool visible) const {
        return Fn<HRESULT (*)(BYTE *, bool)>(0x72808)(node, visible);
    }
    HRESULT Insert(BYTE *parent, BYTE **nodes, unsigned count) const {
        return Fn<HRESULT (*)(BYTE *, BYTE **, unsigned, BYTE *, unsigned)>(0x430d0)(parent, nodes, count, nullptr, 4);
    }
    HRESULT Remove(BYTE *parent, BYTE *node) const {
        return Fn<HRESULT (*)(BYTE *, BYTE **, unsigned)>(0xe4580)(parent, &node, 1);
    }
    void Destroy(BYTE *node) const {
        Fn<void (*)(BYTE *, bool)>(0x100420)(node, true);
    }
};
std::vector<BYTE *> OfficeChildren(BYTE *parent) {
    std::vector<BYTE *> nodes;
    for (auto node = Value<BYTE *>(parent + 0x10); node && nodes.size() < 256; node = Value<BYTE *>(node + 0x18))
        nodes.push_back(node);
    return nodes;
}
RECT OfficeLocalRect(BYTE *node) {
    auto point = Value<POINT>(node + 0x78);
    auto size = Value<SIZE>(node + 0x80);
    return {point.x, point.y, point.x + size.cx, point.y + size.cy};
}
struct OfficeCapture {
    int width{}, height{};
    std::vector<BYTE> pixels;
    bool Save(const std::filesystem::path &path) const {
        if (pixels.empty())
            return false;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(&file), sizeof(file));
        out.write(reinterpret_cast<const char *>(&info), sizeof(info));
        out.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
        return out.good();
    }
    unsigned Count(COLORREF color, RECT area) const {
        unsigned count{};
        for (int y = std::max(0L, area.top); y < std::min(static_cast<LONG>(height), area.bottom); ++y)
            for (int x = std::max(0L, area.left); x < std::min(static_cast<LONG>(width), area.right); ++x) {
                auto pixel = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
                if (pixel[0] == GetBValue(color) && pixel[1] == GetGValue(color) && pixel[2] == GetRValue(color))
                    ++count;
            }
        return count;
    }
};
OfficeCapture OfficeCaptureTop(HWND root, HWND window) {
    OfficeCapture result;
    RECT client{};
    POINT origin{};
    if (GetForegroundWindow() != window || IsIconic(window) || !GetClientRect(root, &client) ||
        !ClientToScreen(root, &origin) || client.right <= 0 || client.right > 10000)
        return result;
    result.width = client.right;
    result.height = std::min(120L, client.bottom);
    if (result.height <= 0)
        return {};
    auto screen = GetDC(nullptr);
    auto memory = CreateCompatibleDC(screen);
    auto bitmap = CreateCompatibleBitmap(screen, result.width, result.height);
    if (!screen || !memory || !bitmap) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memory)
            DeleteDC(memory);
        if (screen)
            ReleaseDC(nullptr, screen);
        return {};
    }
    auto old = SelectObject(memory, bitmap);
    auto copied = BitBlt(memory, 0, 0, result.width, result.height, screen, origin.x, origin.y, SRCCOPY);
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = result.width;
    info.bmiHeader.biHeight = -result.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
    auto read = GetDIBits(memory, bitmap, 0, result.height, result.pixels.data(), &info, DIB_RGB_COLORS);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied || !read)
        return {};
    return result;
}
void OfficeRefresh(HWND root) {
    RECT r{};
    GetClientRect(root, &r);
    SendMessageW(root, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DwmFlush();
}
} // namespace

#ifdef OFFICE_LEASE_EXPERIMENT
#include "office-lease.h"
#else
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || context->adapterId != 271 || !selection.root ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId() || !Runtime())
        return ERROR_NOT_SUPPORTED;
    auto base = modules[0].base;
    const std::pair<size_t, const char *> entries[]{{0x1c1180, "\x48\x89\x5c\x24\x08\x48\x89\x74"},
                                                    {0x100420, "\x8a\x41\x64\x24\x0c\x3c\x08\x73"},
                                                    {0x72808, "\x48\x83\xec\x38\x4c\x8b\x05"},
                                                    {0x430d0, "\x4c\x89\x4c\x24\x20\x44\x89\x44"},
                                                    {0xe4580, "\x48\x89\x5c\x24\x08\x48\x89\x6c"}};
    for (auto entry : entries) {
        auto length = entry.first == 0x72808 ? 7u : 8u;
        if (memcmp(base + entry.first, entry.second, length))
            return ERROR_REVISION_MISMATCH;
    }
    auto root = reinterpret_cast<BYTE *>(GetWindowLongPtrW(selection.root, 0));
    auto parent = Value<BYTE *>(root + 0x10);
    std::string rootName;
    constexpr LONG_PTR requiredCaption = WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (!ClassName(root, rootName) || rootName != "FullpageUIWindow" || !parent ||
        Value<BYTE **>(parent) != reinterpret_cast<BYTE **>(base + 0x84ba60) || Value<BYTE *>(parent + 8) != root ||
        IsIconic(context->window) || !IsWindowEnabled(context->window) ||
        (GetWindowLongPtrW(context->window, GWL_STYLE) & requiredCaption) != requiredCaption)
        return ERROR_NOT_SUPPORTED;
    auto info = base + 0xd959f0;
    if (Value<BYTE *>(Value<BYTE **>(info)) != base + 0x1c1180)
        return ERROR_REVISION_MISMATCH;
    OfficeLayoutApi api{base};
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
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
    const auto originalChildren = OfficeChildren(parent);
    std::array<BYTE *, 3> created{};
    std::array<BYTE *, 6> visible{}, originalVisuals{};
    std::array<BYTE *, 3> parts{}, originalHits{};
    std::array<RECT, 3> rectangles{};
    std::array<LRESULT, 3> nativeHits{};
    bool maximizeInput{}, restoreInput{}, minimizeInput{};
    std::vector<BYTE *> retained;
    std::vector<MSG> deferredCommands;
    constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
    constexpr int commands[]{SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE}, hits[]{HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
    bool prepared = true, attached{}, restored = true;
    OfficeFrameScope frame;
    RECT originalClient{}, originalOuter{};
    GetClientRect(context->window, &originalClient);
    GetWindowRect(context->window, &originalOuter);
    // The existing full-page title row starts to the right of a reserved
    // sidebar. Restrict this experiment to an original empty caption area.
    BYTE *caption{};
    for (auto node = Value<BYTE *>(selection.parent + 8); node && node != root; node = Value<BYTE *>(node + 8)) {
        std::string name;
        if (ClassName(node, name) && name == "AppFrameHelper" &&
            api.Fn<int (*)(BYTE *, BYTE *, bool *)>(0x333d0)(node, base + 0x948250, nullptr) == HTCAPTION) {
            caption = node;
            break;
        }
    }
    prepared &= check(caption != nullptr, "original-caption-container");
    POINT captionOrigin{};
    for (auto node = caption; node && node != root; node = Value<BYTE *>(node + 8)) {
        std::string name;
        ClassName(node, name);
        if (Value<BYTE **>(node) != reinterpret_cast<BYTE **>(base + 0x84ba60) && name != "AppFrameHelper") {
            prepared = false;
            break;
        }
        auto point = Value<POINT>(node + 0x78);
        captionOrigin.x += point.x;
        captionOrigin.y += point.y;
    }
    prepared &=
        check(captionOrigin.x >= MulDiv(84, GetDpiForWindow(context->window), 96), "reserved-original-caption-space");
    auto before = OfficeCaptureTop(selection.root, context->window);
    check(before.Save(directory / (prefix + L"-before.bmp")), "capture-before");
    for (unsigned i = 0; prepared && i < 3; ++i) {
        originalVisuals[i] = selection.buttons[i];
        originalVisuals[i + 3] = Value<BYTE *>(selection.buttons[i] + 0x10);
        visible[i] = api.Get(selection.buttons[i], 0x83fb10);
        parts[i] = api.Get(selection.buttons[i], 0x837128);
        originalHits[i] = api.Get(selection.buttons[i], 0x948250);
        prepared &= check(parts[i] && originalHits[i], "save-original-frame-semantics");
        for (auto node : {originalVisuals[i], originalVisuals[i + 3]}) {
            auto ref = node ? api.Fn<BYTE *(*)(BYTE *)>(0x170340)(node) : nullptr;
            retained.push_back(ref);
            prepared &= check(ref != nullptr, "retain-original-node-lifetime");
        }
        if (originalVisuals[i + 3])
            visible[i + 3] = api.Get(originalVisuals[i + 3], 0x83fb10);
        prepared &= check(visible[i] != nullptr, "save-original-visibility");
        prepared &= check(visible[i + 3] != nullptr, "save-original-glyph-visibility");
        auto hr = api.Fn<HRESULT (*)(BYTE *, BYTE **)>(0x1c1180)(info, &created[i]);
        prepared &= check(SUCCEEDED(hr) && created[i], "create-real-app-frame-helper");
        if (!created[i])
            break;
        auto ref = api.Fn<BYTE *(*)(BYTE *)>(0x170340)(created[i]);
        retained.push_back(ref);
        prepared &= check(ref != nullptr, "retain-owned-node-lifetime");
        std::string name;
        prepared &= check(ClassName(created[i], name) && name == "AppFrameHelper", "verify-native-button-type");
        for (auto item : {std::pair<size_t, int>{0x838600, 2},
                          {0x949f20, MulDiv(10 + static_cast<int>(i) * 20, GetDpiForWindow(context->window), 96)},
                          {0x949f60, MulDiv(4, GetDpiForWindow(context->window), 96)},
                          {0x83f290, 20},
                          {0x83f250, 24},
                          {0x837128, 1},
                          {0x8371a0, commands[i]},
                          {0x948250, hits[i]},
                          {0x8387c0, MulDiv(12, GetDpiForWindow(context->window), 96)}})
            prepared &= check(SUCCEEDED(api.Int(created[i], item.first, item.second)), "set-native-button-property");
        prepared &= check(SUCCEEDED(api.Text(created[i], 0x83ff48, L"\x25cf")), "set-circle-content");
        prepared &= check(SUCCEEDED(api.Text(created[i], 0x83f3d8, L"Segoe UI Symbol")), "set-circle-font");
        prepared &= check(SUCCEEDED(api.Color(created[i], colors[i])), "set-native-circle-color");
        prepared &= check(SUCCEEDED(api.Visible(created[i], true)), "show-native-button");
    }
    if (prepared) {
        attached = check(SUCCEEDED(api.Insert(parent, created.data(), 3)), "insert-native-buttons");
        if (attached) {
            check(frame.Start(context->window, selection.root), "suppress-native-caption-buttons");
            check(!(GetWindowLongPtrW(context->window, GWL_STYLE) & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)),
                  "all-native-caption-style-bits-suppressed");
            for (auto node : originalVisuals)
                check(SUCCEEDED(api.Visible(node, false)), "hide-original-button-and-glyph");
            for (unsigned i = 0; i < 3; ++i) {
                check(SUCCEEDED(api.Int(selection.buttons[i], 0x837128, 0)), "disable-original-caption-part");
                check(SUCCEEDED(api.Int(selection.buttons[i], 0x948250, HTCAPTION)), "disable-original-hit-test");
            }
            OfficeRefresh(selection.root);
            auto painted = OfficeCaptureTop(selection.root, context->window);
            check(painted.Save(directory / (prefix + L"-painted.bmp")), "capture-native-paint");
            for (unsigned i = 0; i < 3; ++i) {
                auto r = OfficeLocalRect(created[i]);
                auto x = MulDiv(10 + static_cast<int>(i) * 20, GetDpiForWindow(context->window), 96);
                auto y = MulDiv(4, GetDpiForWindow(context->window), 96);
                rectangles[i] = {x, y, x + r.right - r.left, y + r.bottom - r.top};
                frame.buttons[i] = rectangles[i];
                check(Value<BYTE *>(created[i] + 8) == parent && rectangles[i].left >= 0 &&
                          rectangles[i].right <= captionOrigin.x &&
                          rectangles[i].bottom <= MulDiv(48, GetDpiForWindow(context->window), 96),
                      "left-layout-in-original-reserved-space");
                check(!painted.pixels.empty() && painted.Count(colors[i], rectangles[i]) >= 3, "native-circle-pixels");
                POINT hit{(rectangles[i].left + rectangles[i].right) / 2,
                          (rectangles[i].top + rectangles[i].bottom) / 2};
                ClientToScreen(selection.root, &hit);
                nativeHits[i] = SendMessageW(context->window, WM_NCHITTEST, 0, MAKELPARAM(hit.x, hit.y));
                check(nativeHits[i] == HTCLIENT, "original-window-accepts-client-input-at-new-button");
            }
            RECT client{}, outer{};
            GetClientRect(context->window, &client);
            GetWindowRect(context->window, &outer);
            check(EqualRect(&client, &originalClient) && EqualRect(&outer, &originalOuter),
                  "caption-suppression-preserves-window-geometry");
            WINDOWPLACEMENT placement{sizeof(placement)};
            GetWindowPlacement(context->window, &placement);
            auto click = [&](unsigned index) {
                auto r = rectangles[index];
                auto coordinates = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
                SendMessageW(selection.root, WM_MOUSEMOVE, 0, coordinates);
                SendMessageW(selection.root, WM_LBUTTONDOWN, MK_LBUTTON, coordinates);
                SendMessageW(selection.root, WM_LBUTTONUP, 0, coordinates);
                auto deadline = GetTickCount64() + 150;
                do {
                    MSG message{};
                    // Process only system commands posted by Office's own
                    // button route, leaving the keyboard/mouse queue alone.
                    while (PeekMessageW(&message, context->window, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE)) {
                        auto command = message.wParam & 0xfff0;
                        if ((index == 1 && command == SC_MINIMIZE) ||
                            (index == 2 && (command == SC_MAXIMIZE || command == SC_RESTORE)))
                            DispatchMessageW(&message);
                        else
                            deferredCommands.push_back(message);
                    }
                    Sleep(5);
                } while (GetTickCount64() < deadline);
            };
            click(2);
            maximizeInput = IsZoomed(context->window) != FALSE;
            check(maximizeInput, "native-framework-maximize-input");
            if (maximizeInput) {
                OfficeRefresh(selection.root);
                OfficeCaptureTop(selection.root, context->window).Save(directory / (prefix + L"-maximized.bmp"));
                click(2);
                restoreInput = !IsZoomed(context->window);
            }
            check(restoreInput, "native-framework-restore-input");
            if (maximizeInput && restoreInput) {
                click(1);
                minimizeInput = IsIconic(context->window) != FALSE;
            }
            check(minimizeInput, "native-framework-minimize-input");
            SetWindowPlacement(context->window, &placement);
            SetForegroundWindow(context->window);
            OfficeRefresh(selection.root);
        }
    }
    // New instances are the only nodes removed or destroyed. Original helpers
    // and their child trees stay in place throughout this bounded transaction.
    for (unsigned i = 0; i < 3; ++i)
        if (created[i]) {
            if ((Value<BYTE>(created[i] + 0x64) & 0xc) < 8) {
                if (Value<BYTE *>(created[i] + 8) == parent)
                    restored &= check(SUCCEEDED(api.Remove(parent, created[i])), "remove-owned-button");
                api.Destroy(created[i]);
            }
            created[i] = nullptr;
        }
    for (unsigned i = 0; i < 6; ++i)
        if (visible[i]) {
            if ((Value<BYTE>(originalVisuals[i] + 0x64) & 0xc) >= 8) {
                restored = false;
                api.Release(visible[i]);
                continue;
            }
            auto hr = Value<short>(visible[i] + 4) == -1 ? api.Fn<HRESULT (*)(BYTE *, BYTE *, bool, void *)>(0x285fa0)(
                                                               originalVisuals[i], base + 0x83fb10, false, nullptr)
                                                         : api.Set(originalVisuals[i], 0x83fb10, visible[i]);
            restored &= check(SUCCEEDED(hr), "restore-original-visibility");
            auto actual = api.Get(originalVisuals[i], 0x83fb10);
            restored &= check(actual == visible[i], "verify-original-visibility");
            api.Release(actual);
            api.Release(visible[i]);
        }
    for (unsigned i = 0; i < 3; ++i)
        for (auto property : {std::pair<size_t, BYTE *>{0x837128, parts[i]}, {0x948250, originalHits[i]}})
            if (property.second) {
                if ((Value<BYTE>(selection.buttons[i] + 0x64) & 0xc) >= 8) {
                    restored = false;
                    api.Release(property.second);
                    continue;
                }
                auto hr = Value<short>(property.second + 4) == -1
                              ? api.Fn<HRESULT (*)(BYTE *, BYTE *, bool, void *)>(0x285fa0)(
                                    selection.buttons[i], base + property.first, false, nullptr)
                              : api.Set(selection.buttons[i], property.first, property.second);
                restored &= check(SUCCEEDED(hr), "restore-original-frame-semantics");
                auto actual = api.Get(selection.buttons[i], property.first);
                restored &= check(actual == property.second, "verify-original-frame-semantics");
                api.Release(actual);
                api.Release(property.second);
            }
    frame.Restore();
    for (auto value : retained)
        api.Release(value);
    OfficeRefresh(selection.root);
    restored &= check(OfficeChildren(parent) == originalChildren, "restore-original-child-order");
    auto after = OfficeCaptureTop(selection.root, context->window);
    check(after.Save(directory / (prefix + L"-restored.bmp")), "capture-restored");
    std::ofstream report(directory / (prefix + L".json"));
    report << "{\"checks\":" << checks << ",\"failures\":" << failures
           << ",\"restored\":" << (restored ? "true" : "false") << ",\"attached\":" << (attached ? "true" : "false")
           << ",\"captionOrigin\":[" << captionOrigin.x << ',' << captionOrigin.y << "],\"rectangles\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            report << ',';
        auto r = rectangles[i];
        report << '[' << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ']';
    }
    report << "],\"failed\":[";
    for (size_t i = 0; i < failed.size(); ++i) {
        if (i)
            report << ',';
        report << '"' << failed[i] << '"';
    }
    report << "],\"nativeHits\":[" << nativeHits[0] << ',' << nativeHits[1] << ',' << nativeHits[2]
           << "],\"maximizeInput\":" << (maximizeInput ? "true" : "false")
           << ",\"restoreInput\":" << (restoreInput ? "true" : "false")
           << ",\"minimizeInput\":" << (minimizeInput ? "true" : "false") << ",\"persistentSession\":false}\n";
    for (auto message : deferredCommands)
        PostMessageW(message.hwnd, message.message, message.wParam, message.lParam);
    selection = {};
    return ERROR_NOT_SUPPORTED;
}
#endif
