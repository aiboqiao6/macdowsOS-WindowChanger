// Read the installed application's title tree using the same guarded runtime
// discovery and layout matcher as the product. Never attach or change XAML.
#ifdef WINUI3_TITLE_READ_DLL
#define MacdowsAdapterProbeV1 ProductWinUI3FixtureProbe
#define MacdowsAdapterAttachV1 ProductWinUI3FixtureAttach
#include "../../winui3-inline.cpp"
#undef MacdowsAdapterProbeV1
#undef MacdowsAdapterAttachV1
#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path TitleReadDirectory() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&TitleReadDirectory), &module);
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, 32768);
    return std::filesystem::path(path).parent_path();
}
void ReadTitleNode(std::ostream &log, x::DependencyObject const &node, x::UIElement const &root, unsigned &budget,
                   unsigned depth = 0) {
    if (!node || !budget || depth > 24)
        return;
    --budget;
    auto element = node.try_as<x::FrameworkElement>();
    winrt::Windows::Foundation::Point point{};
    if (element) {
        point = element.TransformToVisual(root).TransformPoint({0, 0});
        if (point.Y > 120)
            return;
    }
    log << std::string(depth, ' ') << winrt::to_string(winrt::get_class_name(node));
    if (element)
        log << " name=" << winrt::to_string(element.Name()) << " xy=" << point.X << ',' << point.Y
            << " size=" << element.ActualWidth() << ',' << element.ActualHeight()
            << " visible=" << static_cast<int>(element.Visibility())
            << " enabled=" << (element.try_as<c::Control>() ? element.as<c::Control>().IsEnabled() : true);
    if (auto tabs = node.try_as<c::TabView>())
        log << " tabHeaderEmpty=" << !tabs.TabStripHeader() << " tabHeaderLocalUnset="
            << (tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty()) == x::DependencyProperty::UnsetValue())
            << " tabHeaderBound=" << static_cast<bool>(tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty()))
            << " tabHeaderTemplate=" << static_cast<bool>(tabs.TabStripHeaderTemplate())
            << " tabFlow=" << static_cast<int>(tabs.FlowDirection())
            << " tabTheme=" << static_cast<int>(tabs.ActualTheme()) << " tabCount=" << tabs.TabItems().Size();
    log << '\n';
    for (int i = 0, count = x::Media::VisualTreeHelper::GetChildrenCount(node); i < count && budget; ++i)
        ReadTitleNode(log, x::Media::VisualTreeHelper::GetChild(node, i), root, budget, depth + 1);
}
void ReadInstalledTitle(HWND target) {
    auto file = L"winui3-title-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(reinterpret_cast<UINT_PTR>(target)) + L".txt";
    std::ofstream log(TitleReadDirectory() / file);
    log << "readOnly=true hwnd=" << target << " dpi=" << GetDpiForWindow(target) << '\n';
    RECT outer{}, client{};
    POINT origin{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    ClientToScreen(target, &origin);
    log << "outer=" << outer.left << ',' << outer.top << ',' << outer.right << ',' << outer.bottom
        << " client=" << client.right << ',' << client.bottom << " clientOrigin=" << origin.x << ',' << origin.y
        << '\n';
    try {
        auto sources = Roots();
        log << "guardedSources=" << sources.size() << '\n';
        for (auto const &source : sources) {
            // A closed source's Content getter need not return or throw safely.
            // Inspect its bridge before touching the original content.
            auto bridge = source.SiteBridge();
            if (!bridge || bridge.IsClosed()) {
                log << "closedSourceSkipped=true\n";
                continue;
            }
            if (auto root = source.Content()) {
                auto xamlRoot = root.XamlRoot();
                if (!xamlRoot) {
                    log << "unloadedSourceSkipped=true\n";
                    continue;
                }
                auto id = xamlRoot.ContentIslandEnvironment().AppWindowId();
                log << "windowId=" << id.Value << " target=" << reinterpret_cast<uint64_t>(target)
                    << " scale=" << root.XamlRoot().RasterizationScale() << '\n';
                if (id.Value != reinterpret_cast<uint64_t>(target))
                    continue;
                auto window = w::AppWindow::GetFromWindowId(id);
                log << "extends=" << window.TitleBar().ExtendsContentIntoTitleBar() << '\n';
                if (auto presenter = window.Presenter().try_as<w::OverlappedPresenter>())
                    log << "hasTitleBar=" << presenter.HasTitleBar() << " hasBorder=" << presenter.HasBorder() << '\n';
                auto child = reinterpret_cast<HWND>(bridge.WindowId().Value);
                RECT rect{};
                GetWindowRect(child, &rect);
                log << "islandHwnd=" << child << " parent=" << GetParent(child)
                    << " root=" << GetAncestor(child, GA_ROOT) << " rect=" << rect.left << ',' << rect.top << ','
                    << rect.right << ',' << rect.bottom << '\n';
                if (auto element = root.try_as<x::FrameworkElement>())
                    log << "actualTheme=" << static_cast<int>(element.ActualTheme()) << '\n';
                std::vector<c::Grid> candidates;
                Candidates(root, root, candidates);
                log << "productTitleCandidates=" << candidates.size() << '\n';
                unsigned budget = 1000;
                ReadTitleNode(log, root, root, budget);
                log << "remainingBudget=" << budget << '\n';
                auto pointer = input::InputNonClientPointerSource::GetForWindowId(id);
                for (auto rect : pointer.GetRegionRects(input::NonClientRegionKind::Caption))
                    log << "caption=" << rect.X << ',' << rect.Y << ',' << rect.Width << ',' << rect.Height << '\n';
            }
        }
        // Do not enumerate Content again through the old product matcher: this
        // report has already read each live bridge and its title candidates.
        log << "productLayoutMatcherRun=false\n";
        log << "scanCompleted=true\n";
    } catch (...) {
        log << "HRESULT=" << std::hex << winrt::to_hresult() << '\n';
    }
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId())
        return ERROR_INVALID_PARAMETER;
    ReadInstalledTitle(context->window);
    return ERROR_NOT_SUPPORTED; // Read-only discovery must never reach Attach.
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *) {
    return ERROR_NOT_SUPPORTED;
}
#else
#include "../../session.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return ERROR_INVALID_PARAMETER;
    wchar_t *end{};
    auto value = wcstoull(argv[1], &end, 0);
    if (!value || !end || *end)
        return ERROR_INVALID_PARAMETER;
    auto target = reinterpret_cast<HWND>(value);
    if (!IsWindow(target))
        return ERROR_INVALID_WINDOW_HANDLE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (GetPropW(target, styler::OwnerProperty) || GetPropW(target, styler::InlineProperty))
        return ERROR_ALREADY_EXISTS;
    DWORD pid{};
    GetWindowThreadProcessId(target, &pid);
    auto report = std::filesystem::path(styler::ExecutableDirectory()) /
                  (L"winui3-title-" + std::to_wstring(pid) + L"-" +
                   std::to_wstring(reinterpret_cast<UINT_PTR>(target)) + L".txt");
    auto previous = std::filesystem::exists(report) ? std::filesystem::last_write_time(report)
                                                    : std::filesystem::file_time_type::min();
    RECT outer{}, client{}, outerAfter{}, clientAfter{};
    GetWindowRect(target, &outer);
    GetClientRect(target, &client);
    auto style = GetWindowLongPtrW(target, GWL_STYLE);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    auto owner = CreateWindowW(cls.lpszClassName, L"Read-only WinUI title research", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return GetLastError();
    bool unexpectedlyAttached{};
    {
        styler::Session session(owner);
        auto result = session.Apply(target, static_cast<styler::Adapter>(286));
        auto length = WideCharToMultiByte(CP_UTF8, 0, result.message.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string message(length, '\0');
        if (length)
            WideCharToMultiByte(CP_UTF8, 0, result.message.c_str(), -1, message.data(), length, nullptr, nullptr);
        std::cout << "Attach accepted: " << result.ok << "; " << message.c_str() << '\n';
        unexpectedlyAttached = result.ok || session.Owns(target);
        session.RestoreAll();
    }
    DestroyWindow(owner);
    GetWindowRect(target, &outerAfter);
    GetClientRect(target, &clientAfter);
    if (unexpectedlyAttached || GetPropW(target, styler::OwnerProperty) || GetPropW(target, styler::InlineProperty) ||
        !EqualRect(&outer, &outerAfter) || !EqualRect(&client, &clientAfter) ||
        GetWindowLongPtrW(target, GWL_STYLE) != style)
        return ERROR_INVALID_STATE;
    if (!std::filesystem::exists(report) || std::filesystem::last_write_time(report) <= previous)
        return ERROR_FILE_NOT_FOUND;
    std::ifstream input(report);
    std::string text((std::istreambuf_iterator<char>(input)), {});
    std::cout << text;
    return text.find("scanCompleted=true") != std::string::npos ? 0 : ERROR_INVALID_DATA;
}
#endif
