#include "inline.h"
#include "adapter-registry.h"
#include "architecture.h"
#include "framework.h"
#include "embedded-frame.h"
#include <dwmapi.h>
#include <algorithm>
#include <filesystem>

namespace styler {
Candidate InspectInline(HWND window) {
    auto c = Inspect(window, true, !PeerExecutable(window, L"WindowStyleHost.exe").empty());
    auto framework = DetectFramework(window);
    c.framework = framework.name;
    c.runtime = framework.runtime;
    if (!c.supported)
        return c;
    // Explicit local registrations can add a framework without changing the
    // injector. Their read-only Probe still validates the actual target layout.
    auto registryDirectory = ExecutableDirectory();
    if (DifferentArchitecture(window)) {
        auto peer = PeerExecutable(window, L"WindowStyleHost.exe");
        if (!peer.empty())
            registryDirectory = std::filesystem::path(peer).parent_path().wstring();
    }
    for (const auto &adapter : ReadAdapterRegistry(registryDirectory))
        if (adapter.Matches(window) && adapter.RuntimeAvailable(window)) {
            c.adapterMode = Adapter::Automatic;
            c.adapter = framework.name + L" · 通用后端自动探测 / 原控件树待验证";
            return c;
        }
    // Direct-only mode must never silently choose the old extra-band path.
    // CabinetWClass also hosts Control Panel, which has a real native caption.
    // Ribbon Explorer consumes that caption in its existing rendering tree.
    bool ribbon = false;
    EnumChildWindows(
        window,
        [](HWND child, LPARAM p) -> BOOL {
            wchar_t name[80]{};
            GetClassNameW(child, name, 80);
            if (wcscmp(name, L"UIRibbonCommandBar") == 0) {
                *reinterpret_cast<bool *>(p) = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ribbon));
    if (c.adapterMode == Adapter::Extended || ribbon || framework.id == L"winui3" || framework.id == L"office-netui" ||
        framework.id == L"mfc-bcg" || framework.id == L"txgui") {
        c.supported = false;
        c.reason = framework.id == L"chromium-views"
                       ? L"当前 Chromium Views 编译版本缺少已验证 ABI profile，原控件布局尚未适配。"
                   : framework.id == L"winui3"
                       ? L"已识别 Windows App SDK / WinUI 3；原控件树入口仍在隔离测试，尚未启用正式后端。"
                   : framework.id == L"office-netui"
                       ? L"当前 Office NetUI 架构、模块 ABI 或窗口类型不在已验证的原树起始页 profile 中。"
                   : framework.id == L"mfc-bcg" ? L"当前 MFC / BCG 架构或框架 ABI 不匹配已验证的原顶栏 profile。"
                   : framework.id == L"txgui"
                       ? L"当前 TXGui 架构或 GF ABI 不匹配已验证的原树 profile；请使用匹配架构的完整包。"
                       : L"尚无此自绘框架的原控件布局适配器；跳过，不使用覆盖/跟随窗口。";
        c.adapter = framework.name + L" / 待适配";
    } else {
        c.adapterMode = Adapter::Native;
        c.adapter = L"原生非客户区 / 进程内绘制";
    }
    return c;
}
std::vector<Candidate> EnumerateInline() {
    auto windows = Enumerate();
    for (auto &c : windows)
        c = InspectInline(c.window);
    std::stable_sort(windows.begin(), windows.end(), [](auto &a, auto &b) { return a.supported > b.supported; });
    return windows;
}
Result InlineSession::Apply(HWND window) {
    auto c = InspectInline(window);
    if (!c.supported)
        return {false, c.reason};
    if (WindowClass(window, L"ApplicationFrameWindow") && !session_.Owns(window)) {
        auto core = FrameContent(window);
        if (!core)
            return {false, L"ApplicationFrame 未找到唯一可见的 CoreWindow 内容，已跳过。"};
        embedded_[window] = core;
        auto inset = session_.ApplyEmbedded(core, window);
        if (!inset.ok) {
            if (!session_.Owns(core))
                embedded_.erase(window);
            return inset;
        }
        auto result = session_.Apply(window, c.adapterMode);
        if (!result.ok) {
            auto rollback = Restore(window);
            if (!rollback.ok)
                result.message += L" 内容撤销仍待确认。";
        }
        return result;
    }
    return session_.Apply(window, c.adapterMode);
}
Result InlineSession::Restore(HWND window) {
    auto result = session_.Restore(window);
    if (!result.ok)
        return result;
    auto item = embedded_.find(window);
    if (item != embedded_.end()) {
        auto content = session_.Restore(item->second);
        if (!content.ok)
            return content;
        embedded_.erase(item);
    }
    return result;
}
Result InlineSession::RestoreAll() {
    std::vector<HWND> frames;
    for (auto const &item : embedded_)
        frames.push_back(item.first);
    bool ok = true;
    for (auto frame : frames)
        if (!Restore(frame).ok)
            ok = false;
    auto remaining = session_.RestoreAll();
    return {ok && remaining.ok, remaining.message};
}
} // namespace styler
