#pragma once
// Public, read-only selection for a top-client TabView island. The caller must
// obtain sources through the guarded runtime entry, on the owning GUI thread.
#include "tab-strip-slot.h"

namespace styler::research::winui {
struct TabTitleSource {
    x::Hosting::DesktopWindowXamlSource source{nullptr};
    c::TabView tabs{nullptr};
    explicit operator bool() const {
        return source && tabs;
    }
};
inline bool TabTitleNodes(x::DependencyObject const &node, unsigned depth, unsigned &remaining,
                          std::vector<c::TabView> &tabs) {
    if (!node || !remaining || depth > 24)
        return false;
    --remaining;
    if (auto view = node.try_as<c::TabView>())
        tabs.push_back(view);
    const auto count = x::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; ++i)
        if (!TabTitleNodes(x::Media::VisualTreeHelper::GetChild(node, i), depth + 1, remaining, tabs))
            return false;
    return true;
}
inline TabTitleSource MatchTabTitle(HWND hwnd, std::vector<x::Hosting::DesktopWindowXamlSource> const &sources) {
    if (!IsWindow(hwnd) || GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId() || !IsWindowEnabled(hwnd) ||
        IsIconic(hwnd) || IsZoomed(hwnd) || GetDpiForWindow(hwnd) != 144 || GetMenu(hwnd) ||
        (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION || sources.empty() || sources.size() > 16)
        return {};
    RECT outer{}, client{};
    POINT origin{};
    if (!GetWindowRect(hwnd, &outer) || !GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin) ||
        origin.y != outer.top || client.right < 330 || client.bottom <= 0)
        return {};
    auto window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)});
    auto presenter = window.Presenter().try_as<w::OverlappedPresenter>();
    if (!presenter || !presenter.HasBorder() || !presenter.HasTitleBar() ||
        window.TitleBar().ExtendsContentIntoTitleBar())
        return {};
    TabTitleSource result;
    unsigned matches{}, remaining = 2000;
    std::vector<uint64_t> seen;
    for (auto const &source : sources) {
        if (!source)
            continue;
        auto bridge = source.SiteBridge();
        if (!bridge || bridge.IsClosed())
            continue; // Never query Content on a closed source.
        auto root = source.Content();
        auto xamlRoot = root ? root.XamlRoot() : nullptr;
        if (!xamlRoot || xamlRoot.ContentIslandEnvironment().AppWindowId().Value != reinterpret_cast<uint64_t>(hwnd))
            continue;
        const auto childId = bridge.WindowId().Value;
        if (std::find(seen.begin(), seen.end(), childId) != seen.end())
            return {}; // Duplicate enumeration cannot establish unique ownership.
        seen.push_back(childId);
        auto child = reinterpret_cast<HWND>(childId);
        RECT bounds{};
        if (GetParent(child) != hwnd || !GetWindowRect(child, &bounds))
            return {};
        // Only a short island at the client's top can host this caption slot.
        if (bounds.top < origin.y || bounds.top - origin.y > 96 || bounds.bottom - bounds.top > 120)
            continue;
        auto element = root.try_as<x::FrameworkElement>();
        if (!element || !element.IsLoaded())
            return {};
        std::vector<c::TabView> tabs;
        if (!TabTitleNodes(root, 0, remaining, tabs))
            return {};
        for (auto const &view : tabs) {
            ++matches;
            const auto scale = xamlRoot.RasterizationScale();
            auto point = view.TransformToVisual(root).TransformPoint({0, 0});
            if (!view.IsLoaded() || view.Visibility() != x::Visibility::Visible || view.Opacity() != 1 ||
                view.FlowDirection() != x::FlowDirection::LeftToRight ||
                element.FlowDirection() != x::FlowDirection::LeftToRight || view.ActualWidth() < 220 ||
                view.ActualHeight() < 24 || view.ActualHeight() > 64 || !view.TabItems().Size() ||
                view.TabStripHeader() || view.TabStripHeaderTemplate() ||
                view.GetBindingExpression(c::TabView::TabStripHeaderProperty()) || std::abs(scale - 1.5) > 0.001 ||
                bounds.left < origin.x || bounds.right > origin.x + client.right ||
                bounds.bottom > origin.y + client.bottom || point.X < 0 || point.Y < 0 ||
                (point.X + view.ActualWidth()) * scale > bounds.right - bounds.left + 2 ||
                (point.Y + view.ActualHeight()) * scale > bounds.bottom - bounds.top + 2)
                return {};
            result = {source, view};
        }
    }
    return matches == 1 ? result : TabTitleSource{};
}
} // namespace styler::research::winui
