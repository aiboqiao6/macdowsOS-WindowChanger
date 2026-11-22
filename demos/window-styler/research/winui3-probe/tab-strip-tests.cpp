// Background-only public TabView slot contract on five owned XAML islands and
// one native EDIT. This is not a registered adapter or an installed-app test.
#include <windows.h>
BOOL WINAPI ShowTabFixture(HWND window, int command);
#define ShowWindow ShowTabFixture
#define wWinMain OwnedFixtureMain
#include "native-island-fixture.cpp"
#undef wWinMain
#undef ShowWindow
#include "tab-strip-slot.h"
#include "tab-caption-guard.h"
#include "../../adapter-api.h"
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using styler::research::winui::TabCaptionGuard;
using styler::research::winui::TabStripSlot;
bool captionTest{}, customClient{};
struct Fixture {
    App *app{};
    HWND edit{};
    c::TabView tabs{nullptr};
    std::array<c::TabViewItem, 2> items{nullptr, nullptr};
    std::array<x::Hosting::DesktopWindowXamlSource, 4> sources{nullptr, nullptr, nullptr, nullptr};
    std::array<x::FrameworkElement, 4> roots{nullptr, nullptr, nullptr, nullptr};
    unsigned tabLoads{}, tabUnloads{}, rootLoads{}, rootUnloads{};
    std::vector<UINT> commands;
    unsigned oldCaptionMessages{};
    LONG tabOffset{60};
    bool reflowWhenHidden{};
    void Layout() {
        RECT r{};
        winrt::check_bool(GetClientRect(app->hwnd, &r));
        app->source.SiteBridge().MoveAndResize({tabOffset, 14, std::max(340L, r.right - 240), 50});
        sources[0].SiteBridge().MoveAndResize({0, 64, r.right, 50});
        sources[1].SiteBridge().MoveAndResize({0, r.bottom - 48, r.right, 48});
        sources[2].SiteBridge().MoveAndResize({r.right - 24, 114, 24, r.bottom - 186});
        sources[3].SiteBridge().MoveAndResize({0, r.bottom - 72, r.right - 24, 24});
        winrt::check_bool(
            SetWindowPos(edit, nullptr, 0, 114, r.right - 24, r.bottom - 186, SWP_NOZORDER | SWP_NOACTIVATE));
    }
    void Theme(bool light) {
        auto theme = light ? x::ElementTheme::Light : x::ElementTheme::Dark;
        app->body.RequestedTheme(theme);
        for (auto const &root : roots)
            root.RequestedTheme(theme);
    }
    static LRESULT CALLBACK Window(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
        auto self = reinterpret_cast<Fixture *>(data);
        try {
            if (customClient && m == WM_NCCALCSIZE && lp) {
                auto rect = wp ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(lp)->rgrc[0] : reinterpret_cast<RECT *>(lp);
                const auto originalTop = rect->top;
                DefSubclassProc(h, m, wp, lp);
                rect->top = originalTop;
                if (self->reflowWhenHidden) {
                    self->reflowWhenHidden = false;
                    ++rect->top; // One-shot incompatible owned-host layout; rollback uses the normal host path.
                }
                return 0;
            }
            if (m == WM_NCHITTEST && lp == -1)
                return static_cast<LRESULT>(wp); // Explicit synthetic stale-hit contract only.
            if ((wp == HTCLOSE || wp == HTMINBUTTON || wp == HTMAXBUTTON) &&
                (m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP || m == WM_NCLBUTTONDBLCLK)) {
                ++self->oldCaptionMessages;
                return 0;
            }
            if (m == WM_SYSCOMMAND) {
                auto command = static_cast<UINT>(wp & 0xfff0);
                if (command == SC_CLOSE || command == SC_MINIMIZE || command == SC_MAXIMIZE || command == SC_RESTORE) {
                    // Validate the routed click and posted command without
                    // closing, activating, minimizing or maximizing a window.
                    self->commands.push_back(command);
                    return 0;
                }
            }
            if (m == WM_SIZE && self->edit) {
                if (wp != SIZE_MINIMIZED)
                    self->Layout();
                return 0;
            }
            if (m == WM_CLOSE) {
                self->items = {nullptr, nullptr};
                self->tabs = nullptr;
                self->roots = {nullptr, nullptr, nullptr, nullptr};
                for (auto &source : self->sources) {
                    source.Close();
                    source = nullptr;
                }
                RemoveWindowSubclass(h, Window, id);
            }
        } catch (...) {
            std::cout << "ERROR host-layout HRESULT=" << std::hex << winrt::to_hresult() << '\n';
            ExitProcess(ERROR_INVALID_STATE);
        }
        return DefSubclassProc(h, m, wp, lp);
    }
    void Create(HWND window) {
        app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        app->body = c::Grid();
        tabs = c::TabView();
        tabs.VerticalAlignment(x::VerticalAlignment::Top);
        tabs.TabItems().Clear();
        for (unsigned i = 0; i != items.size(); ++i) {
            items[i] = c::TabViewItem();
            items[i].Header(winrt::box_value(i ? L"Second owned tab" : L"First owned tab"));
            items[i].Loaded([this](auto const &, auto const &) { ++tabLoads; });
            items[i].Unloaded([this](auto const &, auto const &) { ++tabUnloads; });
            tabs.TabItems().Append(items[i]);
        }
        tabs.SelectedItem(items[1]);
        app->body.Children().Append(tabs);
        app->body.Loaded([this](auto const &, auto const &) { ++rootLoads; });
        app->body.Unloaded([this](auto const &, auto const &) { ++rootUnloads; });
        app->source.Content(app->body);
        for (auto &source : sources) {
            source = x::Hosting::DesktopWindowXamlSource();
            source.Initialize({reinterpret_cast<uint64_t>(window)});
        }
        c::MenuBar menu;
        c::MenuBarItem file;
        file.Title(L"Owned menu");
        menu.Items().Append(file);
        c::Grid menuRoot;
        menuRoot.Children().Append(menu);
        roots[0] = menuRoot;
        c::Grid status;
        c::TextBlock statusText;
        statusText.Text(L"Owned status island");
        status.Children().Append(statusText);
        roots[1] = status;
        for (unsigned i = 2; i != roots.size(); ++i) {
            c::Primitives::ScrollBar scroll;
            scroll.Orientation(i == 2 ? c::Orientation::Vertical : c::Orientation::Horizontal);
            scroll.Maximum(100);
            roots[i] = scroll;
        }
        for (unsigned i = 0; i != sources.size(); ++i)
            sources[i].Content(roots[i]);
        edit = CreateWindowExW(0, L"EDIT", L"Owned original document", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 114,
                               600, 400, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        winrt::check_bool(edit != nullptr);
        winrt::check_bool(SetWindowSubclass(window, Window, 0x54414253, reinterpret_cast<DWORD_PTR>(this)));
        Theme(wcsstr(GetCommandLineW(), L"--light") != nullptr);
        Layout();
    }
} fixture;
UINT_PTR timer{};
ULONGLONG started{};
unsigned stage{}, checks{};
bool checking{};
std::unique_ptr<TabStripSlot> slot;
std::unique_ptr<TabCaptionGuard> captionGuard;
std::vector<RectInt32> seedCaption, seedPass;
std::array<LONG, 10> captionGeometry{};
std::array<RECT, 6> before{};
std::array<x::UIElement, 5> originalRoots{nullptr, nullptr, nullptr, nullptr, nullptr};
winrt::Windows::Foundation::IInspectable selected{nullptr};
c::Border replacement{nullptr};
c::Button retained{nullptr};
std::shared_ptr<TabStripSlot::Commands> retiredCommands;
RECT originalOuter{};
LONG_PTR originalStyle{};
struct Raster {
    int32_t width{}, height{};
    std::vector<uint8_t> pixels;
    bool operator==(Raster const &) const = default;
} captured, baseline, stableRaster;
winrt::Windows::Foundation::IAsyncAction capture{nullptr};
ULONGLONG rasterStableSince{};
unsigned rasterSamples{};

void SaveRaster(Raster const &raster, const wchar_t *label) {
    wchar_t executable[32768]{};
    winrt::check_bool(GetModuleFileNameW(nullptr, executable, 32768) != 0);
    static const auto prefix =
        L"tab-raster-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-";
    auto file = std::filesystem::path(executable).parent_path() / (prefix + label + L".bmp");
    if (std::filesystem::exists(file))
        throw std::runtime_error("refusing to overwrite owned raster evidence");
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(raster.pixels.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = raster.width;
    info.biHeight = -raster.height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    output.write(reinterpret_cast<const char *>(&info), sizeof(info));
    output.write(reinterpret_cast<const char *>(raster.pixels.data()), raster.pixels.size());
    if (!output)
        throw std::runtime_error("cannot save owned raster evidence");
    std::cout << "tab-raster-file=" << winrt::to_string(file.wstring()) << '\n';
}
void RasterDifference() {
    if (captured.width != baseline.width || captured.height != baseline.height) {
        std::cout << "tab-raster-diff=different-dimensions\n";
        return;
    }
    size_t count{};
    int left = captured.width, top = captured.height, right = -1, bottom = -1;
    for (int y = 0; y < captured.height; ++y)
        for (int x = 0; x < captured.width; ++x) {
            auto offset = (size_t(y) * captured.width + x) * 4;
            if (memcmp(captured.pixels.data() + offset, baseline.pixels.data() + offset, 4)) {
                ++count;
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    std::cout << "tab-raster-diff=" << count << " bounds=" << left << ',' << top << ',' << right << ',' << bottom
              << " tab-widths=" << fixture.items[0].ActualWidth() << ',' << fixture.items[1].ActualWidth() << '\n';
}
winrt::Windows::Foundation::IAsyncAction CaptureTab(std::wstring label) {
    auto root = fixture.app->body;
    x::Media::Imaging::RenderTargetBitmap image;
    co_await image.RenderAsync(root);
    auto buffer = co_await image.GetPixelsAsync();
    Raster result{image.PixelWidth(), image.PixelHeight(), std::vector<uint8_t>(buffer.Length())};
    winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer).ReadBytes(result.pixels);
    std::cout << "tab-raster=" << result.width << 'x' << result.height << " bytes=" << result.pixels.size() << '\n';
    captured = std::move(result);
    SaveRaster(captured, label.c_str());
}
bool CaptureReady() {
    if (capture.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
        return false;
    capture.GetResults();
    capture = nullptr;
    return true;
}
bool RasterStable(const wchar_t *prefix) {
    if (!rasterSamples || captured != stableRaster) {
        stableRaster = captured;
        rasterStableSince = GetTickCount64();
    }
    ++rasterSamples;
    const auto duration = GetTickCount64() - rasterStableSince;
    if (duration >= 300) {
        std::cout << "tab-raster-stable-ms=" << duration << " samples=" << rasterSamples << '\n';
        return true;
    }
    // Establish stability independently of the reference. Do not search for a
    // matching frame, modify pixels or disable the host's native animations.
    capture = CaptureTab(std::wstring(prefix) + L"-" + std::to_wstring(rasterSamples));
    return false;
}
size_t SolidPixels(uint8_t red, uint8_t green, uint8_t blue) {
    size_t count{};
    for (size_t i = 0; i + 3 < captured.pixels.size(); i += 4)
        if (captured.pixels[i] == blue && captured.pixels[i + 1] == green && captured.pixels[i + 2] == red &&
            captured.pixels[i + 3] == 255)
            ++count;
    std::cout << "tab-raster-solid=" << unsigned(red) << ',' << unsigned(green) << ',' << unsigned(blue)
              << " count=" << count << '\n';
    return count;
}

void Finish(bool success) {
    KillTimer(nullptr, timer);
    if (capture) {
        capture.Cancel();
        capture = nullptr;
    }
    captionGuard.reset();
    slot.reset();
    originalRoots = {nullptr, nullptr, nullptr, nullptr, nullptr};
    selected = nullptr;
    replacement = nullptr;
    retained = nullptr;
    retiredCommands.reset();
    stage = success ? 100 : 999;
    if (fixture.app)
        PostMessageW(fixture.app->hwnd, WM_CLOSE, 0, 0);
}

void Check(bool condition, const char *name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    if (!condition)
        throw std::runtime_error(name);
    ++checks;
}
std::array<RECT, 6> Geometry() {
    std::array<RECT, 6> result{};
    winrt::check_bool(GetWindowRect(fixture.edit, &result[0]));
    winrt::check_bool(
        GetWindowRect(reinterpret_cast<HWND>(fixture.app->source.SiteBridge().WindowId().Value), &result[1]));
    for (unsigned i = 0; i != fixture.sources.size(); ++i)
        winrt::check_bool(
            GetWindowRect(reinterpret_cast<HWND>(fixture.sources[i].SiteBridge().WindowId().Value), &result[i + 2]));
    return result;
}
void StableContent() {
    auto geometry = Geometry();
    Check(memcmp(geometry.data(), before.data(), sizeof(before)) == 0, "all five islands and the EDIT retain geometry");
    Check(fixture.app->source.Content() == originalRoots[0], "the original tabs root stays installed");
    for (unsigned i = 0; i != fixture.sources.size(); ++i)
        Check(fixture.sources[i].Content() == originalRoots[i + 1], "other island content remains installed");
    Check(fixture.rootLoads == 1 && fixture.rootUnloads == 0 && fixture.tabLoads == 2 && fixture.tabUnloads == 0,
          "original root and tabs are never unloaded or reparented");
    Check(fixture.tabs.SelectedItem() == selected, "selected tab identity stays intact");
    wchar_t text[128]{};
    GetWindowTextW(fixture.edit, text, 128);
    Check(wcscmp(text, L"Owned original document edited") == 0, "native document text stays intact");
    Check(SendMessageW(fixture.edit, EM_CANUNDO, 0, 0) != 0, "native document undo history stays intact");
    auto style = GetWindowLongPtrW(fixture.app->hwnd, GWL_STYLE);
    if (captionGuard && captionGuard->active)
        Check((style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME)) ==
                  (originalStyle & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME)),
              "hidden native title retains the original window capabilities and resize border");
    else
        Check(style == originalStyle, "the original native caption style is unchanged or fully restored");
    Check(GetForegroundWindow() != fixture.app->hwnd, "the owned fixture needs no foreground activation");
}
void Reject(const char *label) {
    auto local = fixture.tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty());
    auto header = fixture.tabs.TabStripHeader();
    TabStripSlot candidate(fixture.app->hwnd, fixture.tabs);
    HRESULT error{};
    try {
        candidate.Apply();
    } catch (winrt::hresult_error const &failure) {
        error = failure.code();
    }
    Check(error == E_NOTIMPL && !candidate.active && fixture.tabs.TabStripHeader() == header &&
              fixture.tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty()) == local,
          label);
}
void Invoke(c::Button const &button) {
    x::Automation::Peers::ButtonAutomationPeer peer(button);
    peer.GetPattern(x::Automation::Peers::PatternInterface::Invoke)
        .as<x::Automation::Provider::IInvokeProvider>()
        .Invoke();
}
void ReadOnlyTitle() {
    const std::filesystem::path file(STYLER_TAB_TITLE_READER);
    struct Library {
        HMODULE handle;
        ~Library() {
            if (handle)
                FreeLibrary(handle);
        }
    } library{
        LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
    winrt::check_bool(library.handle != nullptr);
    auto probe = reinterpret_cast<styler::AdapterProbeV1>(GetProcAddress(library.handle, "MacdowsAdapterProbeV1"));
    auto attach = reinterpret_cast<styler::AdapterAttachV1>(GetProcAddress(library.handle, "MacdowsAdapterAttachV1"));
    Check(probe && attach, "the built read-only module exposes its public adapter ABI");
    styler::AdapterContextV1 context;
    context.window = context.owner = fixture.app->hwnd;
    context.adapterId = 286;
    Check(probe(&context) == ERROR_NOT_SUPPORTED && attach(&context) == ERROR_NOT_SUPPORTED,
          "the diagnostic probe and attach entry points both refuse a persistent attachment");
    auto report = file.parent_path() / (L"winui3-title-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                        std::to_wstring(reinterpret_cast<UINT_PTR>(fixture.app->hwnd)) + L".txt");
    std::ifstream input(report);
    std::string output((std::istreambuf_iterator<char>(input)), {});
    Check(output.find("guardedSources=5") != std::string::npos &&
              output.find("scanCompleted=true") != std::string::npos,
          "guarded diagnostic enumeration reads all five owned islands and completes");
    Check(output.find("tabHeaderEmpty=1 tabHeaderLocalUnset=1 tabHeaderBound=0 tabHeaderTemplate=0 tabFlow=0") !=
                  std::string::npos &&
              output.find("tabCount=2") != std::string::npos,
          "the diagnostic reports empty unbound LTR TabView slot metadata");
    Check(output.find("Owned original document") == std::string::npos &&
              output.find("Second owned tab") == std::string::npos,
          "the diagnostic does not collect document or tab title text");
    Check(!GetPropW(fixture.app->hwnd, styler::OwnerProperty) && !GetPropW(fixture.app->hwnd, styler::InlineProperty),
          "the read-only module leaves no owner or inline lease");
    StableContent();
}
void RejectOtherWindow() {
    auto other = CreateWindowW(L"STATIC", L"Owned message-only mismatch", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    winrt::check_bool(other != nullptr);
    HRESULT error{};
    try {
        TabStripSlot candidate(other, fixture.tabs);
        candidate.Apply();
    } catch (winrt::hresult_error const &failure) {
        error = failure.code();
    }
    DestroyWindow(other);
    Check(error == E_NOTIMPL && !fixture.tabs.TabStripHeader(),
          "a TabView owned by another HWND cannot control the requested target");
}
auto Pointer() {
    return input::InputNonClientPointerSource::GetForWindowId({reinterpret_cast<uint64_t>(fixture.app->hwnd)});
}
void CheckCaptionRestored() {
    auto window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(fixture.app->hwnd)});
    Check(window.Presenter().as<w::OverlappedPresenter>().HasTitleBar(),
          "the actual presenter restores its native title");
    Check(
        TabCaptionGuard::Same(TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Caption), seedCaption) &&
            TabCaptionGuard::Same(TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Passthrough), seedPass),
        "undo restores the exact original caption and passthrough rectangle lists");
    DWORD_PTR data{};
    Check(!GetWindowSubclass(fixture.app->hwnd, TabCaptionGuard::Filter, TabCaptionGuard::Subclass, &data),
          "undo removes the native-button input filter");
    Check(captionGuard->Geometry() == captionGeometry, "outer, client and screen-origin geometry are restored exactly");
    StableContent();
}
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
    if (checking)
        return;
    checking = true;
    try {
        if (GetTickCount64() - started > 20000)
            throw std::runtime_error("background tab slot test timed out");
        if (!fixture.app || !fixture.tabs.IsLoaded() || !fixture.items[1].IsLoaded()) {
            checking = false;
            return;
        }
        auto &tabs = fixture.tabs;
        if (stage == 0) {
            Check(fixture.rootLoads == 1 && fixture.tabLoads == 2, "five-island fixture loads its original tabs once");
            Check(tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty()) == x::DependencyProperty::UnsetValue(),
                  "initial slot is an unset local value");
            before = Geometry();
            originalRoots[0] = fixture.app->source.Content();
            for (unsigned i = 0; i != fixture.sources.size(); ++i)
                originalRoots[i + 1] = fixture.sources[i].Content();
            selected = tabs.SelectedItem();
            originalStyle = GetWindowLongPtrW(fixture.app->hwnd, GWL_STYLE);
            if (captionTest) {
                seedCaption = {{0, 0, 900, 64}, {0, 80, 120, 20}};
                seedPass = {{800, 10, 20, 20}};
                TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Caption, seedCaption);
                TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Passthrough, seedPass);
            }
            winrt::check_bool(GetWindowRect(fixture.app->hwnd, &originalOuter));
            auto end = GetWindowTextLengthW(fixture.edit);
            SendMessageW(fixture.edit, EM_SETSEL, end, end);
            SendMessageW(fixture.edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" edited"));
            StableContent();
            ReadOnlyTitle();
            RejectOtherWindow();
            stage = 30;
            rasterSamples = 0;
            capture = CaptureTab(L"original");
        } else if (stage == 30 && CaptureReady() && RasterStable(L"original")) {
            Check(captured.width > 0 && captured.height > 0 &&
                      captured.pixels.size() == size_t(captured.width) * captured.height * 4,
                  "framework rendering exports the complete nonempty original tab island");
            baseline = captured;
            SaveRaster(baseline, L"original-stable");
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = captionTest ? 50 : 1;
        } else if (stage == 50 && slot->Ready()) {
            captionGuard = std::make_unique<TabCaptionGuard>(fixture.app->hwnd, *slot);
            captionGeometry = captionGuard->Geometry();
            HRESULT error{};
            try {
                captionGuard->Apply(fixture.app->source);
            } catch (winrt::hresult_error const &failure) {
                error = failure.code();
            }
            if (!customClient) {
                Check(error == E_NOTIMPL && !captionGuard->active,
                      "an ordinary native-caption layout is refused before title mutation");
                CheckCaptionRestored();
                slot->Restore();
                stage = 52;
                rasterSamples = 0;
                capture = CaptureTab(L"caption-rejected");
            } else {
                Check(error == S_OK && captionGuard->active, "custom-top-client caption transaction attaches");
                Check(!captionGuard->presenter.HasTitleBar() && captionGuard->presenter.HasBorder(),
                      "the actual presenter hides the native title and keeps its border");
                Check(captionGuard->Geometry() == captionGeometry,
                      "removing the native title changes no outer or client geometry");
                Check(captionGuard->Refresh(), "caption ownership and the complete protected input region are ready");
                {
                    TabCaptionGuard duplicate(fixture.app->hwnd, *slot);
                    HRESULT rejected{};
                    try {
                        duplicate.Apply(fixture.app->source);
                    } catch (winrt::hresult_error const &failure) {
                        rejected = failure.code();
                    }
                    Check(rejected == E_NOTIMPL && !duplicate.active,
                          "a second caption transaction cannot replace an active filter");
                }
                DWORD_PTR filterOwner{};
                Check(GetWindowSubclass(fixture.app->hwnd, TabCaptionGuard::Filter, TabCaptionGuard::Subclass,
                                        &filterOwner) &&
                          filterOwner == reinterpret_cast<DWORD_PTR>(captionGuard.get()),
                      "destroying a refused transaction leaves the actual owner's filter installed");
                for (auto const &rect : TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Caption))
                    Check(!TabCaptionGuard::Overlaps(rect, captionGuard->protectedRect),
                          "original drag regions exclude the complete offset light group");
                auto pass = TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Passthrough);
                Check(std::any_of(
                          pass.begin(), pass.end(),
                          [&](auto const &rect) { return TabCaptionGuard::Same(rect, captionGuard->protectedRect); }),
                      "passthrough contains the real island-relative light group in top-level coordinates");
                Check(captionGuard->protectedRect.X >= fixture.tabOffset,
                      "the input rectangle accounts for the tab island's horizontal offset");
                TITLEBARINFOEX info{sizeof(info)};
                SendMessageW(fixture.app->hwnd, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
                for (auto index : {2, 3, 5})
                    Check((info.rgstate[index] &
                           (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN)) ==
                                  (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN) &&
                              IsRectEmpty(&info.rgrect[index]),
                          "old native button queries are hidden disabled and have empty bounds");
                for (auto hit : {HTCLOSE, HTMINBUTTON, HTMAXBUTTON}) {
                    for (auto message : {WM_NCLBUTTONDOWN, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK})
                        SendMessageW(fixture.app->hwnd, message, hit, 0);
                    Check(SendMessageW(fixture.app->hwnd, WM_NCHITTEST, hit, -1) == HTCLIENT,
                          "a synthetic stale native-button hit is filtered to client input");
                }
                Check(fixture.oldCaptionMessages == 0,
                      "all nine synthetic old-button mouse messages are blocked before the host");
                Check(SendMessageW(fixture.app->hwnd, WM_NCHITTEST, HTTOPRIGHT, -1) == HTTOPRIGHT,
                      "the native resize-corner hit is preserved");
                StableContent();
                for (auto const &button : slot->buttons)
                    Invoke(button);
                stage = 51;
                rasterSamples = 0;
                capture = CaptureTab(L"caption-hidden");
            }
        } else if (stage == 51 && CaptureReady() && RasterStable(L"caption-hidden")) {
            Check(fixture.commands == std::vector<UINT>{SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE},
                  "the three new Click handlers still route their intercepted commands while native buttons are "
                  "disabled");
            Check(SolidPixels(255, 95, 87) >= 40 && SolidPixels(255, 189, 46) >= 40 && SolidPixels(40, 200, 64) >= 40,
                  "all three lights remain rendered with the native title hidden");
            captionGuard->Restore();
            slot->Restore();
            CheckCaptionRestored();
            SendMessageW(fixture.app->hwnd, WM_NCLBUTTONDOWN, HTCLOSE, 0);
            Check(fixture.oldCaptionMessages == 1, "undo releases the old-button mouse path back to the owned host");
            Check(SendMessageW(fixture.app->hwnd, WM_NCHITTEST, HTCLOSE, -1) == HTCLOSE,
                  "undo restores the owned host's synthetic native-button hit-test path");
            stage = 52;
            rasterSamples = 0;
            capture = CaptureTab(L"caption-restored");
        } else if (stage == 52 && CaptureReady() && RasterStable(L"caption-restored")) {
            RasterDifference();
            Check(captured == baseline, "caption undo or refusal restores every byte of the original tab root bitmap");
            if (customClient) {
                captionGuard.reset();
                slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
                slot->Apply();
                stage = 53;
            } else
                Finish(true);
        } else if (stage == 53 && slot->Ready()) {
            captionGuard = std::make_unique<TabCaptionGuard>(fixture.app->hwnd, *slot);
            captionGuard->Apply(fixture.app->source);
            const std::vector<RectInt32> hostCaption{{300, 4, 120, 24}}, hostPass{{750, 8, 25, 25}};
            TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Caption, hostCaption);
            TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Passthrough, hostPass);
            captionGuard->window.TitleBar().ForegroundColor(winrt::Windows::UI::Color{255, 121, 63, 184});
            Check(!captionGuard->Refresh() && !captionGuard->active,
                  "host input-region replacement ends caption ownership");
            Check(TabCaptionGuard::Same(TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Caption),
                                        hostCaption) &&
                      TabCaptionGuard::Same(TabCaptionGuard::Read(Pointer(), input::NonClientRegionKind::Passthrough),
                                            hostPass),
                  "rollback preserves the host's newer input regions instead of its stale snapshot");
            auto color = captionGuard->window.TitleBar().ForegroundColor();
            Check(color && color.Value().R == 121 && color.Value().G == 63 && color.Value().B == 184,
                  "rollback preserves the host's newer native title palette");
            slot->Restore();
            TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Caption, seedCaption);
            TabCaptionGuard::Write(Pointer(), input::NonClientRegionKind::Passthrough, seedPass);
            CheckCaptionRestored();
            captionGuard.reset();
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 54;
        } else if (stage == 54 && slot->Ready()) {
            captionGuard = std::make_unique<TabCaptionGuard>(fixture.app->hwnd, *slot);
            captionGuard->Apply(fixture.app->source);
            fixture.tabOffset += 6;
            fixture.Layout();
            Check(!captionGuard->Refresh() && !captionGuard->active,
                  "moving the host tab island invalidates old light input coordinates and restores the title");
            RECT moved{};
            GetWindowRect(reinterpret_cast<HWND>(fixture.app->source.SiteBridge().WindowId().Value), &moved);
            Check(moved.left == before[1].left + 6, "caption rollback does not overwrite a host-owned island move");
            slot->Restore();
            fixture.tabOffset -= 6;
            fixture.Layout();
            CheckCaptionRestored();
            captionGuard.reset();
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 55;
        } else if (stage == 55 && slot->Ready()) {
            captionGuard = std::make_unique<TabCaptionGuard>(fixture.app->hwnd, *slot);
            HRESULT error{};
            fixture.reflowWhenHidden = true;
            try {
                captionGuard->Apply(fixture.app->source);
            } catch (winrt::hresult_error const &failure) {
                error = failure.code();
            }
            Check(!fixture.reflowWhenHidden,
                  "the one-shot custom-client layout fault actually executes during title removal");
            std::cout << "caption-reflow HRESULT=" << std::hex << error << std::dec
                      << " active=" << captionGuard->active << '\n';
            Check(error == E_NOTIMPL && !captionGuard->active,
                  "a host which shifts its client when the title hides is rejected and rolled back");
            slot->Restore();
            CheckCaptionRestored();
            Check(SendMessageW(fixture.edit, EM_UNDO, 0, 0) != 0,
                  "the native document undo survives all caption transactions");
            wchar_t text[128]{};
            GetWindowTextW(fixture.edit, text, 128);
            Check(wcscmp(text, L"Owned original document") == 0,
                  "native undo restores the exact original document after caption recovery");
            Finish(true);
        } else if (stage == 1 && slot->Ready()) {
            stage = 31;
            rasterSamples = 0;
            capture = CaptureTab(L"attached");
        } else if (stage == 31 && CaptureReady() && RasterStable(L"attached")) {
            SaveRaster(captured, L"attached-stable");
            Check(captured.width == baseline.width && captured.height == baseline.height,
                  "the rendered island retains its complete original bitmap dimensions");
            Check(SolidPixels(255, 95, 87) >= 40, "the complete island bitmap contains the rendered red light");
            Check(SolidPixels(255, 189, 46) >= 40, "the complete island bitmap contains the rendered yellow light");
            Check(SolidPixels(40, 200, 64) >= 40, "the complete island bitmap contains the rendered green light");
            StableContent();
            Check(slot->Refresh(), "explicit layout refresh confirms the live header and updates capabilities");
            auto position = fixture.items[0].TransformToVisual(tabs).TransformPoint({0, 0});
            Check(position.X >= 84 && slot->lights.ActualWidth() == 84,
                  "TabView allocates real width so original tabs avoid the complete light group");
            Check(slot->lights.ActualTheme() == tabs.ActualTheme(), "slot inherits the existing island theme");
            for (auto const &button : slot->buttons)
                Check(!button.IsTabStop() && !button.AllowFocusOnInteraction(),
                      "caption controls do not take document focus");
            for (auto const &button : slot->buttons)
                Invoke(button);
            stage = 10;
        } else if (stage == 10 && fixture.commands.size() >= 3) {
            Check(fixture.commands == std::vector<UINT>{SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE},
                  "public Invoke reaches each real Click handler and the matching intercepted system command");
            Check(!slot->commands->Execute(3), "an invalid button index cannot enqueue a command");
            auto menu = GetSystemMenu(fixture.app->hwnd, FALSE);
            auto previous = EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            Check(previous != UINT(-1), "the owned fixture can temporarily disable its close command");
            Check(!slot->commands->Execute(0), "a newly disabled close command is checked at execution time");
            EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | previous);
            SetWindowLongPtrW(fixture.app->hwnd, GWL_STYLE, originalStyle & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
            slot->UpdateCapabilities();
            Check(!slot->buttons[1].IsEnabled() && !slot->buttons[2].IsEnabled(),
                  "updated capability state disables unavailable minimize and maximize lights");
            Check(!slot->commands->Execute(1) && !slot->commands->Execute(2),
                  "removed minimize and maximize capabilities cannot enqueue a command");
            SetWindowLongPtrW(fixture.app->hwnd, GWL_STYLE, originalStyle);
            slot->UpdateCapabilities();
            StableContent();
            slot->Restore();
            stage = 32;
            rasterSamples = 0;
            capture = CaptureTab(L"restored");
        } else if (stage == 32 && CaptureReady() && RasterStable(L"restored")) {
            SaveRaster(captured, L"restored-stable");
            RasterDifference();
            Check(captured == baseline, "normal undo restores every byte of the complete original tab island bitmap "
                                        "without masking or tolerance");
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 33;
        } else if (stage == 33 && slot->Ready()) {
            auto theme = tabs.ActualTheme() != x::ElementTheme::Light;
            fixture.Theme(theme);
            stage = 2;
        } else if (stage == 2 && slot->lights.ActualTheme() == tabs.ActualTheme()) {
            StableContent();
            Check(slot->lights.ActualTheme() == fixture.app->body.ActualTheme(),
                  "theme follows a host-owned theme change");
            retained = slot->buttons[0];
            retiredCommands = slot->commands;
            slot->Restore();
            Check(tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty()) == x::DependencyProperty::UnsetValue(),
                  "normal undo restores an unset local value exactly");
            Check(!retiredCommands->Execute(0), "a retained command is inactive immediately after undo");
            Invoke(retained);
            tabs.TabStripHeader(nullptr);
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 3;
        } else if (stage == 3 && slot->Ready()) {
            Check(fixture.commands.size() == 3, "invoking a retained old control after undo posts no command");
            slot->Restore();
            Check(!tabs.TabStripHeader() &&
                      tabs.ReadLocalValue(c::TabView::TabStripHeaderProperty()) != x::DependencyProperty::UnsetValue(),
                  "normal undo preserves an explicitly set null");
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 4;
        } else if (stage == 4 && slot->Ready()) {
            replacement = c::Border();
            replacement.Width(36);
            tabs.TabStripHeader(replacement);
            Check(!slot->OwnsSlot(), "host replacement invalidates slot ownership");
            Check(!slot->commands->Execute(0), "a host replacement blocks commands before the next refresh or undo");
            slot->Restore();
            Check(tabs.TabStripHeader() == replacement, "undo preserves the host's replacement header");
            Reject("occupied header refuses attachment without changing the host value");
            tabs.ClearValue(c::TabView::TabStripHeaderProperty());
            x::Data::Binding binding;
            binding.Source(replacement);
            tabs.SetBinding(c::TabView::TabStripHeaderProperty(), binding);
            Reject("a bound header refuses attachment and retains its expression");
            Check(static_cast<bool>(tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty())),
                  "the host binding expression survives rejection");
            tabs.ClearValue(c::TabView::TabStripHeaderProperty());
            tabs.TabStripHeaderTemplate(
                x::Markup::XamlReader::Load(
                    L"<DataTemplate xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">"
                    L"<Border Width=\"12\"/></DataTemplate>")
                    .as<x::DataTemplate>());
            Reject("a host header template refuses attachment without replacing the template");
            tabs.ClearValue(c::TabView::TabStripHeaderTemplateProperty());
            tabs.FlowDirection(x::FlowDirection::RightToLeft);
            Reject("RTL layout refuses an unverified left slot");
            tabs.FlowDirection(x::FlowDirection::LeftToRight);
            StableContent();
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 20;
        } else if (stage == 20 && slot->Ready()) {
            // A binding can evaluate to our very same header. Identity alone
            // cannot establish ownership or permission to clear the binding.
            x::Data::Binding binding;
            binding.Source(slot->lights);
            tabs.SetBinding(c::TabView::TabStripHeaderProperty(), binding);
            stage = 21;
        } else if (stage == 21) {
            Check(tabs.TabStripHeader() == slot->lights, "host binding can retain the identical light grid");
            Check(!slot->OwnsSlot() && !slot->commands->Execute(0),
                  "a same-value binding relinquishes slot ownership and command authority");
            slot->Restore();
            Check(static_cast<bool>(tabs.GetBindingExpression(c::TabView::TabStripHeaderProperty())),
                  "undo preserves a host binding even when its effective header is the old light grid");
            tabs.ClearValue(c::TabView::TabStripHeaderProperty());
            slot = std::make_unique<TabStripSlot>(fixture.app->hwnd, tabs);
            slot->Apply();
            stage = 22;
        } else if (stage == 22 && slot->Ready()) {
            winrt::check_bool(SetWindowPos(
                fixture.app->hwnd, nullptr, 0, 0, originalOuter.right - originalOuter.left + 240,
                originalOuter.bottom - originalOuter.top + 80, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            stage = 23;
        } else if (stage == 23 && slot->Ready()) {
            Check(slot->Refresh(), "the slot remains ready after a nonactivating host resize");
            auto position = fixture.items[0].TransformToVisual(tabs).TransformPoint({0, 0});
            Check(position.X >= 84, "resized original tabs still avoid the entire light group");
            winrt::check_bool(SetWindowPos(fixture.app->hwnd, nullptr, 0, 0, originalOuter.right - originalOuter.left,
                                           originalOuter.bottom - originalOuter.top,
                                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            stage = 24;
        } else if (stage == 24 && slot->Ready()) {
            StableContent();
            tabs.Width(200);
            stage = 25;
        } else if (stage == 25 && tabs.ActualWidth() == 200) {
            Check(!slot->commands->Execute(0), "narrow layout blocks commands before refresh");
            Check(!slot->Refresh() && !slot->active && !tabs.TabStripHeader(),
                  "refresh removes the owned slot when the host layout becomes too narrow");
            Reject("a narrow TabView refuses a fresh attachment");
            tabs.ClearValue(x::FrameworkElement::WidthProperty());
            stage = 26;
        } else if (stage == 26 && tabs.ActualWidth() >= 220) {
            StableContent();
            Check(fixture.commands.size() == 3, "only the three intercepted live-button commands were posted");
            Check(SendMessageW(fixture.edit, EM_UNDO, 0, 0) != 0,
                  "the native EDIT can undo its original edit after the complete slot transactions");
            wchar_t text[128]{};
            GetWindowTextW(fixture.edit, text, 128);
            Check(wcscmp(text, L"Owned original document") == 0,
                  "native undo restores the exact pre-attachment document");
            Finish(true);
        }
    } catch (std::exception const &error) {
        std::cout << "ERROR " << error.what() << '\n';
        Finish(false);
    } catch (...) {
        std::cout << "ERROR HRESULT=" << std::hex << winrt::to_hresult() << '\n';
        Finish(false);
    }
    checking = false;
}
} // namespace
BOOL WINAPI ShowTabFixture(HWND window, int command) {
    if (command == SW_SHOW) {
        fixture.Create(window);
        if (customClient)
            winrt::check_bool(SetWindowPos(window, nullptr, 0, 0, 0, 0,
                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
        SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        command = SW_SHOWNOACTIVATE;
    }
    return ShowWindow(window, command);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << std::unitbuf;
    captionTest = wcsstr(GetCommandLineW(), L"--caption-guard") != nullptr;
    customClient = wcsstr(GetCommandLineW(), L"--custom-client") != nullptr;
    started = GetTickCount64();
    timer = SetTimer(nullptr, 0, 60, Tick);
    if (!timer)
        return GetLastError();
    auto result = OwnedFixtureMain(instance, previous, command, show);
    KillTimer(nullptr, timer);
    std::cout << "tab-slot checks=" << checks << " completed=" << (stage == 100) << '\n';
    return result ? result : stage == 100 ? 0 : ERROR_INVALID_STATE;
}
