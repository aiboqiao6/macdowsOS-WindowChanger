// Owned native HWND + full-client XAML island. This is a layout experiment,
// never an adapter and never a reason to enable native GDI fallback for WinUI.
#include <windows.h>
#include <appmodel.h>
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <cmath>
#include <fstream>
#include <vector>

namespace x = winrt::Microsoft::UI::Xaml;
namespace c = x::Controls;
namespace w = winrt::Microsoft::UI::Windowing;
namespace input = winrt::Microsoft::UI::Input;
using winrt::Windows::Graphics::RectInt32;
constexpr UINT Command = WM_APP + 73, Metric = WM_APP + 74;
void Failure(const char *stage, HRESULT hr, winrt::hstring const &message = {}) {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring file(path);
    file.resize(file.find_last_of(L'\\') + 1);
    std::ofstream log(file + L"native-island-error-" + std::to_wstring(GetCurrentProcessId()) + L".txt", std::ios::app);
    log << stage << " HRESULT=" << std::hex << hr << '\n';
    if (!message.empty())
        log << winrt::to_string(message) << '\n';
}

#include "native-island-frame.h"
#include <memory>

struct App : x::ApplicationT<App, x::Markup::IXamlMetadataProvider> {
    x::Markup::IXamlMetadataProvider metadata{nullptr};
    x::Markup::IXamlMetadataProvider Provider() {
        if (!metadata)
            metadata = x::XamlTypeInfo::XamlControlsXamlMetaDataProvider();
        return metadata;
    }
    x::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const &type) {
        return wcsstr(GetCommandLineW(), L"--text-editor") ? Provider().GetXamlType(type) : nullptr;
    }
    x::Markup::IXamlType GetXamlType(winrt::hstring const &name) {
        return wcsstr(GetCommandLineW(), L"--text-editor") ? Provider().GetXamlType(name) : nullptr;
    }
    winrt::com_array<x::Markup::XmlnsDefinition> GetXmlnsDefinitions() {
        if (!wcsstr(GetCommandLineW(), L"--text-editor"))
            return {};
        return Provider().GetXmlnsDefinitions();
    }
    App() {
        UnhandledException([](auto const &, x::UnhandledExceptionEventArgs const &args) {
            Failure("unhandled-xaml", args.Exception());
        });
    }
    HWND hwnd{};
    x::Hosting::DesktopWindowXamlSource source{nullptr};
    c::Grid body{nullptr};
    std::unique_ptr<styler::research::winui::NativeIslandFrame> native;
    UINT dpi{};
    unsigned generation{};
    c::Button editor{nullptr};
    c::Button action{nullptr};
    c::TextBox document{nullptr};
    struct DocumentEvents {
        unsigned loads{}, unloads{}, changes{};
    };
    std::shared_ptr<DocumentEvents> documentEvents{std::make_shared<DocumentEvents>()}, retiredDocument;
    bool resourcesLoaded{}, rebuildPending{};
    unsigned sourceGeneration{1};
    static constexpr UINT_PTR RebuildTimer = 0x4e495342;
    unsigned activations{}, characters{};
    unsigned initializationFault{}, initializationFaults{};
    bool faultBeforeLease{};
    LRESULT faultDetachResult{};
    bool cancelNextClose{};
    unsigned cancelledCloses{};
    void ReplaceRoot() {
        auto light = body.ActualTheme() == x::ElementTheme::Light;
        body = c::Grid();
        Theme(light);
        source.Content(body);
        ++generation;
    }
    void Rebuild(unsigned mode) {
        const auto light = body.ActualTheme() == x::ElementTheme::Light;
        retiredDocument = documentEvents;
        if (mode != 22) {
            auto previous = source;
            source = nullptr; // Nested WM_SIZE must not use the closing bridge.
            previous.Close();
            ++sourceGeneration;
            if (mode == 24) {
                rebuildPending = true;
                winrt::check_bool(SetTimer(hwnd, RebuildTimer, 750, nullptr) != 0);
                return;
            }
            source = x::Hosting::DesktopWindowXamlSource();
            source.Initialize({reinterpret_cast<uint64_t>(hwnd)});
        }
        BuildContent(light);
        ++generation;
        Resize();
    }
    static LRESULT CALLBACK InitializationFault(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
        auto self = reinterpret_cast<App *>(data);
        if (m == WM_NCDESTROY) {
            RemoveWindowSubclass(h, InitializationFault, id);
            return DefSubclassProc(h, m, wp, lp);
        }
        try {
            if (self->initializationFault && m == WM_WINDOWPOSCHANGING &&
                w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(h)})
                    .TitleBar()
                    .ExtendsContentIntoTitleBar()) {
                const auto mode = self->initializationFault;
                self->initializationFault = 0;
                RemoveWindowSubclass(h, InitializationFault, id);
                ++self->initializationFaults;
                self->faultBeforeLease = GetPropW(h, L"Macdows.WindowStyler.Owner.v1") == nullptr;
                // This callback executes inside the public title-mode setter on
                // Apply's stack, before the external adapter publishes its lease.
                wchar_t path[32768]{};
                GetModuleFileNameW(nullptr, path, 32768);
                std::wstring file(path);
                file.resize(file.find_last_of(L'\\') + 1);
                std::ofstream log(file + L"native-island-initialization-" + std::to_wstring(GetCurrentProcessId()) +
                                      L".txt",
                                  std::ios::app);
                log << "mode=" << mode << " beforeLease=" << self->faultBeforeLease << '\n';
                log.flush();
                try {
                    if (mode == 12)
                        self->ReplaceRoot();
                    else if (mode == 13 || mode == 18)
                        SendMessageW(h, WM_CLOSE, 0, 0);
                    else if (mode == 17)
                        SendMessageW(h, WM_SYSCOMMAND, SC_CLOSE, 0);
                    else if (mode == 14) {
                        auto owner = GetPropW(h, L"Macdows.WindowStyler.Request.v1");
                        self->faultDetachResult =
                            SendMessageW(h, RegisterWindowMessageW(L"Macdows.WindowStyler.Detach.v1"),
                                         reinterpret_cast<WPARAM>(owner), 0);
                    }
                    log << "completed=1 aliveAfterCallback=" << IsWindow(h)
                        << " detachResult=" << self->faultDetachResult << '\n';
                } catch (...) {
                    log << "HRESULT=" << std::hex << winrt::to_hresult() << '\n';
                }
                if (!IsWindow(h))
                    return 0;
            }
        } catch (...) {
            Failure("initialization-fault", winrt::to_hresult());
        }
        return DefSubclassProc(h, m, wp, lp);
    }
    void Palette(bool clear) {
        auto bar = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)}).TitleBar();
        using Color = styler::research::winui::TitlePalette::Color;
        auto color = [clear](unsigned i) -> Color {
            if (clear)
                return nullptr;
            const winrt::Windows::UI::Color colors[]{{255, 32, 64, 96},    {255, 32, 64, 96},    {255, 255, 255, 255},
                                                     {255, 64, 96, 128},   {255, 255, 255, 255}, {255, 24, 48, 72},
                                                     {255, 192, 208, 224}, {255, 16, 32, 48},    {255, 255, 255, 255},
                                                     {255, 255, 255, 255}, {255, 24, 48, 72},    {255, 192, 208, 224}};
            return winrt::box_value(colors[i]).as<Color>();
        };
        bar.BackgroundColor(color(0));
        bar.ButtonBackgroundColor(color(1));
        bar.ButtonForegroundColor(color(2));
        bar.ButtonHoverBackgroundColor(color(3));
        bar.ButtonHoverForegroundColor(color(4));
        bar.ButtonInactiveBackgroundColor(color(5));
        bar.ButtonInactiveForegroundColor(color(6));
        bar.ButtonPressedBackgroundColor(color(7));
        bar.ButtonPressedForegroundColor(color(8));
        bar.ForegroundColor(color(9));
        bar.InactiveBackgroundColor(color(10));
        bar.InactiveForegroundColor(color(11));
    }
    void Resize() {
        if (!source || IsIconic(hwnd))
            return;
        if (native && native->active)
            native->Resize();
        else {
            RECT r{};
            GetClientRect(hwnd, &r);
            source.SiteBridge().MoveAndResize({0, 0, r.right, r.bottom});
        }
    }
    void Theme(bool light) {
        body.RequestedTheme(light ? x::ElementTheme::Light : x::ElementTheme::Dark);
        body.Background(x::Media::SolidColorBrush(light ? winrt::Windows::UI::Color{255, 243, 243, 243}
                                                        : winrt::Windows::UI::Color{255, 32, 32, 32}));
        if (document) {
            document.Foreground(x::Media::SolidColorBrush(light ? winrt::Windows::UI::Color{255, 16, 16, 16}
                                                                : winrt::Windows::UI::Color{255, 240, 240, 240}));
            document.Background(x::Media::SolidColorBrush(light ? winrt::Windows::UI::Color{255, 255, 255, 255}
                                                                : winrt::Windows::UI::Color{255, 48, 48, 48}));
        }
    }
    void Apply() {
        if (!native)
            native = std::make_unique<styler::research::winui::NativeIslandFrame>(
                hwnd, source, wcsstr(GetCommandLineW(), L"--preserve-root") != nullptr,
                wcsstr(GetCommandLineW(), L"--title-island") != nullptr);
        native->Apply();
    }
    void Restore() {
        if (native) {
            native->Restore();
            native.reset();
        }
    }
    static LRESULT CALLBACK BoundarySize(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
        if (m == WM_NCDESTROY) {
            RemoveWindowSubclass(h, BoundarySize, id);
            return DefSubclassProc(h, m, wp, lp);
        }
        if (m == WM_WINDOWPOSCHANGING && lp) {
            auto position = reinterpret_cast<WINDOWPOS *>(lp);
            const auto width = position->cx, height = position->cy;
            auto result = DefSubclassProc(h, m, wp, lp);
            if (!(position->flags & SWP_NOSIZE)) {
                position->cx = width;
                position->cy = height;
            }
            return result;
        }
        return DefSubclassProc(h, m, wp, lp);
    }
    LRESULT ReadMetric(WPARAM index) {
        if (index >= 0x1000 && document) {
            auto text = document.Text();
            return index - 0x1000 < text.size() ? text[index - 0x1000] : -1;
        }
        RECT r{};
        POINT origin{};
        GetClientRect(hwnd, &r);
        if (source)
            ClientToScreen(reinterpret_cast<HWND>(source.SiteBridge().WindowId().Value), &origin);
        const double scale = double(dpi) / 96;
        auto p = body.TransformToVisual(nullptr).TransformPoint({0, 0});
        if (index >= 20 && index < 36) {
            styler::research::winui::TitlePalette values(
                w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)}).TitleBar());
            if (index < 32) {
                auto color = values.colors[index - 20];
                if (!color)
                    return -1;
                auto v = color.Value();
                return (uint32_t(v.A) << 24) | (uint32_t(v.R) << 16) | (uint32_t(v.G) << 8) | v.B;
            }
            if (index == 32)
                return static_cast<int>(values.icon);
            if (index == 33)
                return values.height ? static_cast<int>(*values.height) : -1;
            if (index == 34)
                return values.theme ? static_cast<int>(*values.theme) : -1;
            return w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)})
                .TitleBar()
                .ExtendsContentIntoTitleBar();
        }
        switch (index) {
        case 1:
            return native && native->active;
        case 2:
            return native ? native->delta : 0;
        case 3:
            return origin.y + std::lround(p.Y * scale);
        case 4:
            return origin.x + std::lround(p.X * scale);
        case 5:
            return std::lround(body.ActualWidth() * scale);
        case 6:
            return std::lround(body.ActualHeight() * scale);
        case 7:
            return static_cast<int>(body.ActualTheme());
        case 8:
            return native && native->active ? std::lround(native->lights.ActualHeight() * scale) : 0;
        case 9:
            return native && native->active ? native->OwnsSource() : source && source.Content() == body;
        case 10:
            return w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)})
                .Presenter()
                .as<w::OverlappedPresenter>()
                .HasTitleBar();
        case 11:
            return generation;
        case 12:
            return native && native->active ? static_cast<int>(native->frame.ActualTheme()) : 0;
        case 13:
        case 14: {
            RECT island{};
            if (source)
                GetWindowRect(reinterpret_cast<HWND>(source.SiteBridge().WindowId().Value), &island);
            return index == 13 ? island.right - island.left : island.bottom - island.top;
        }
        case 40: {
            auto focused = x::Input::FocusManager::GetFocusedElement(body.XamlRoot());
            return focused == editor ? 1 : focused == action ? 2 : 0;
        }
        case 41:
            return characters;
        case 42:
            return activations;
        case 43:
            return source && source.HasFocus();
        case 44:
            return initializationFaults;
        case 45:
            return faultBeforeLease;
        case 46:
            return faultDetachResult;
        case 47:
            return cancelledCloses;
        case 50:
            return document && x::Input::FocusManager::GetFocusedElement(body.XamlRoot()) == document;
        case 51:
            return document ? document.Text().size() : -1;
        case 52:
            return document ? document.SelectionStart() : -1;
        case 53:
            return document ? document.SelectionLength() : -1;
        case 54:
            return documentEvents->loads;
        case 55:
            return documentEvents->unloads;
        case 56:
            return documentEvents->changes;
        case 57:
            return document ? std::lround(document.ActualWidth() * scale) : -1;
        case 58:
            return document ? std::lround(document.ActualHeight() * scale) : -1;
        case 59:
            return document && document.CanUndo();
        case 60:
            return document && document.CanRedo();
        case 61:
            return document ? static_cast<int>(document.ActualTheme()) : -1;
        case 70:
            return sourceGeneration;
        case 71:
            return source ? static_cast<LRESULT>(source.SiteBridge().WindowId().Value) : 0;
        case 72:
            return retiredDocument ? retiredDocument->loads : -1;
        case 73:
            return retiredDocument ? retiredDocument->unloads : -1;
        case 74:
            return rebuildPending;
        default:
            return 0;
        }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        auto self = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd = h;
        }
        if (!self)
            return DefWindowProcW(h, m, wp, lp);
        try {
            if (m == Command) {
                if (wp == 1)
                    self->Apply();
                else if (wp == 2)
                    self->Restore();
                else if (wp == 3)
                    self->Theme(true);
                else if (wp == 4)
                    self->Theme(false);
                else if (wp == 5)
                    self->ReplaceRoot();
                else if (wp == 6) {
                    winrt::check_bool(self->editor && self->editor.Focus(x::FocusState::Programmatic));
                } else if (wp == 7)
                    self->Palette(false);
                else if (wp == 8)
                    self->Palette(true);
                else if (wp == 9 || wp == 11) {
                    auto bar = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(h)}).TitleBar();
                    if (wp == 11)
                        bar.ExtendsContentIntoTitleBar(true);
                    bar.IconShowOptions(w::IconShowOptions::HideIconAndSystemMenu);
                    bar.PreferredHeightOption(w::TitleBarHeightOption::Tall);
                    bar.PreferredTheme(w::TitleBarTheme::Light);
                    if (wp == 11)
                        bar.ExtendsContentIntoTitleBar(false);
                } else if ((wp >= 12 && wp <= 14) || wp == 17 || wp == 18) {
                    self->initializationFault = static_cast<unsigned>(wp);
                    self->cancelNextClose = wp == 18;
                    winrt::check_bool(
                        SetWindowSubclass(h, InitializationFault, 0x4e495346, reinterpret_cast<DWORD_PTR>(self)));
                } else if (wp == 15) {
                    auto bar = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(h)}).TitleBar();
                    bar.ExtendsContentIntoTitleBar(true);
                    bar.ExtendsContentIntoTitleBar(false);
                } else if (wp == 16) {
                    auto window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(h)});
                    auto token = window.Closing([](auto const &, auto const &) {});
                    window.Closing(token);
                } else if (wp == 21) {
                    winrt::check_bool(self->document && self->document.Focus(x::FocusState::Programmatic));
                } else if (wp >= 22 && wp <= 24) {
                    self->Rebuild(static_cast<unsigned>(wp));
                }
                return 0;
            }
            if (m == Metric)
                return self->ReadMetric(wp);
            if (m == WM_TIMER && wp == RebuildTimer && self->rebuildPending) {
                KillTimer(h, RebuildTimer);
                self->rebuildPending = false;
                const auto light = self->body.ActualTheme() == x::ElementTheme::Light;
                self->source = x::Hosting::DesktopWindowXamlSource();
                self->source.Initialize({reinterpret_cast<uint64_t>(h)});
                self->BuildContent(light);
                ++self->generation;
                self->Resize();
                return 0;
            }
            if (m == WM_SIZE)
                self->Resize();
            if (m == WM_GETTITLEBARINFOEX && (self->native && self->native->active) && lp) {
                auto result = DefWindowProcW(h, m, wp, lp);
                auto info = reinterpret_cast<TITLEBARINFOEX *>(lp);
                if (info->cbSize == sizeof(*info))
                    for (auto i : {2, 3, 5}) {
                        info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                        SetRectEmpty(&info->rgrect[i]);
                    }
                return result;
            }
            if ((self->native && self->native->active) && (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) &&
                (m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP || m == WM_NCLBUTTONDBLCLK))
                return 0;
            if (m == WM_CLOSE) {
                if (self->cancelNextClose) {
                    self->cancelNextClose = false;
                    ++self->cancelledCloses;
                    return 0;
                }
                self->Restore();
                if (self->source)
                    self->source.Close();
                self->source = nullptr;
                DestroyWindow(h);
                return 0;
            }
            if (m == WM_DESTROY) {
                self->Exit();
                return 0;
            }
        } catch (...) {
            return static_cast<LRESULT>(winrt::to_hresult());
        }
        return DefWindowProcW(h, m, wp, lp);
    }
    void OnLaunched(x::LaunchActivatedEventArgs const &) {
        try {
            Launch();
        } catch (winrt::hresult_error const &error) {
            Failure("launch", error.code(), error.message());
            Exit();
        } catch (...) {
            Failure("launch", winrt::to_hresult());
            Exit();
        }
    }
    void Launch() {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = Proc;
        cls.lpszClassName = L"Macdows.Research.NativeIslandFixture";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        auto h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, cls.lpszClassName, L"Owned native XAML island fixture",
                                 WS_OVERLAPPEDWINDOW, 160, 160, 1200, 720, nullptr, nullptr, cls.hInstance, this);
        winrt::check_bool(h != nullptr);
        dpi = GetDpiForWindow(h);
        source = x::Hosting::DesktopWindowXamlSource();
        source.Initialize({reinterpret_cast<uint64_t>(h)});
        BuildContent(wcsstr(GetCommandLineW(), L"--light") != nullptr);
        SetPropW(h, L"Macdows.Research.NativeIsland.Owned", reinterpret_cast<HANDLE>(1));
        Resize();
        // The owned boundary test deliberately allows widths below the native
        // caption minimum, without changing non-client geometry or capabilities.
        if (wcsstr(GetCommandLineW(), L"--boundary-sizes"))
            winrt::check_bool(SetWindowSubclass(h, BoundarySize, 0x4e495342, 0));
        ShowWindow(h, SW_SHOW);
    }
    void BuildContent(bool light) {
        document = nullptr;
        documentEvents = std::make_shared<DocumentEvents>();
        body = c::Grid();
        c::RowDefinition toolbarRow;
        toolbarRow.Height({52, x::GridUnitType::Pixel});
        body.RowDefinitions().Append(toolbarRow);
        body.RowDefinitions().Append(c::RowDefinition());
        c::Border toolbar;
        toolbar.Background(x::Media::SolidColorBrush({255, 45, 105, 165}));
        c::TextBlock label;
        label.Text(L"Original toolbar: geometry and pixels must stay in place");
        label.Margin({16, 12, 16, 12});
        label.Foreground(x::Media::SolidColorBrush({255, 255, 255, 255}));
        toolbar.Child(label);
        body.Children().Append(toolbar);
        c::TextBlock content;
        content.Text(L"Original XAML island content");
        content.FontSize(26);
        content.HorizontalAlignment(x::HorizontalAlignment::Center);
        content.VerticalAlignment(x::VerticalAlignment::Center);
        c::Grid::SetRow(content, 1);
        body.Children().Append(content);
        if (wcsstr(GetCommandLineW(), L"--text-editor") && !resourcesLoaded) {
            source.Content(body);
            Resources().MergedDictionaries().Append(c::XamlControlsResources());
            resourcesLoaded = true;
        }
        if (wcsstr(GetCommandLineW(), L"--interactive") || wcsstr(GetCommandLineW(), L"--text-editor")) {
            c::StackPanel panel;
            panel.HorizontalAlignment(x::HorizontalAlignment::Center);
            panel.VerticalAlignment(x::VerticalAlignment::Center);
            panel.Spacing(12);
            panel.Width(320);
            auto controlTemplate =
                x::Markup::XamlReader::Load(
                    L"<ControlTemplate xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                    L"TargetType=\"Button\"><Border Background=\"#FF335577\" Padding=\"12\">"
                    L"<ContentPresenter Content=\"{TemplateBinding Content}\"/></Border></ControlTemplate>")
                    .as<c::ControlTemplate>();
            editor = c::Button();
            editor.Template(controlTemplate);
            editor.UseSystemFocusVisuals(false);
            c::TextBlock inputLabel;
            inputLabel.Text(L"Owned character-input target");
            inputLabel.Foreground(x::Media::SolidColorBrush({255, 255, 255, 255}));
            editor.Content(inputLabel);
            editor.CharacterReceived([this](auto const &, auto const &args) {
                if (args.Character() >= L'a' && args.Character() <= L'z') {
                    ++characters;
                    args.Handled(true);
                }
            });
            action = c::Button();
            action.Template(controlTemplate);
            action.UseSystemFocusVisuals(false);
            c::TextBlock actionLabel;
            actionLabel.Text(L"Owned keyboard action");
            actionLabel.Foreground(x::Media::SolidColorBrush({255, 255, 255, 255}));
            action.Content(actionLabel);
            action.Click([this](auto const &, auto const &) { ++activations; });
            if (wcsstr(GetCommandLineW(), L"--text-editor")) {
                panel.Width(480);
                document = c::TextBox();
                document.UseSystemFocusVisuals(false);
                document.Height(160);
                document.FontSize(18);
                document.AcceptsReturn(true);
                document.TextWrapping(x::TextWrapping::Wrap);
                document.IsSpellCheckEnabled(false);
                document.Text(L"Original document");
                auto events = documentEvents;
                document.Loaded([events](auto const &, auto const &) { ++events->loads; });
                document.Unloaded([events](auto const &, auto const &) { ++events->unloads; });
                document.TextChanged([events](auto const &, auto const &) { ++events->changes; });
                panel.Children().Append(document);
            } else
                panel.Children().Append(editor);
            panel.Children().Append(action);
            c::Grid::SetRow(panel, 1);
            body.Children().RemoveAtEnd();
            body.Children().Append(panel);
        }
        Theme(light);
        if (source.Content() != body)
            source.Content(body);
    }
};
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        PACKAGE_VERSION version{};
        version.Major = 2;
        PWSTR id{};
        PACKAGEDEPENDENCY_CONTEXT context{};
        auto kernel = GetModuleHandleW(L"kernelbase.dll");
        auto create = reinterpret_cast<decltype(&TryCreatePackageDependency)>(
            GetProcAddress(kernel, "TryCreatePackageDependency"));
        auto add = reinterpret_cast<decltype(&AddPackageDependency)>(GetProcAddress(kernel, "AddPackageDependency"));
        if (!create || !add)
            winrt::throw_hresult(E_NOTIMPL);
        winrt::check_hresult(create(nullptr, L"Microsoft.WindowsAppRuntime.2_8wekyb3d8bbwe", version,
                                    PackageDependencyProcessorArchitectures_X64, PackageDependencyLifetimeKind_Process,
                                    nullptr, CreatePackageDependencyOptions_None, &id));
        winrt::check_hresult(add(id, 0, AddPackageDependencyOptions_None, &context, nullptr));
        HeapFree(GetProcessHeap(), 0, id);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        x::Application::Start([](auto const &) { winrt::make<App>(); });
        return 0;
    } catch (...) {
        Failure("main", winrt::to_hresult());
        return static_cast<int>(winrt::to_hresult());
    }
}
