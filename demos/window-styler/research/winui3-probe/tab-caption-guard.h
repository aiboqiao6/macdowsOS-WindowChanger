#pragma once
// Owned-fixture research only: no product lease, timer or application matcher.
#include "tab-strip-slot.h"

namespace styler::research::winui {
struct TabCaptionGuard {
    static constexpr UINT_PTR Subclass = 0x54434744;
    HWND hwnd{};
    TabStripSlot &slot;
    w::AppWindow window{nullptr};
    w::OverlappedPresenter presenter{nullptr};
    input::InputNonClientPointerSource pointer{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    std::optional<TitlePalette> palette, appliedPalette;
    std::vector<RectInt32> originalCaption, originalPass, appliedCaption, appliedPass;
    std::optional<std::vector<RectInt32>> rollbackCaption, rollbackPass;
    RectInt32 protectedRect{};
    bool active{}, used{}, changing{}, restoring{}, invalid{}, captionWritten{}, passWritten{}, paletteMerged{};
    UINT closeMessage{};
    WPARAM closeWP{};
    LPARAM closeLP{};
    unsigned operationDepth{};
    bool filterInstalled{};
    explicit TabCaptionGuard(HWND target, TabStripSlot &value) : hwnd(target), slot(value) {}
    TabCaptionGuard(TabCaptionGuard const &) = delete;
    TabCaptionGuard &operator=(TabCaptionGuard const &) = delete;
    struct Operation {
        TabCaptionGuard &guard;
        explicit Operation(TabCaptionGuard &value) : guard(value) {
            ++guard.operationDepth;
        }
        ~Operation() {
            if (--guard.operationDepth == 0 && guard.closeMessage && IsWindow(guard.hwnd) &&
                PostMessageW(guard.hwnd, guard.closeMessage, guard.closeWP, guard.closeLP))
                guard.closeMessage = 0;
        }
    };
    static std::vector<RectInt32> Read(input::InputNonClientPointerSource const &source,
                                       input::NonClientRegionKind kind) {
        auto values = source.GetRegionRects(kind);
        return {values.begin(), values.end()};
    }
    static void Write(input::InputNonClientPointerSource const &source, input::NonClientRegionKind kind,
                      std::vector<RectInt32> const &values) {
        if (values.empty())
            source.ClearRegionRects(kind);
        else
            source.SetRegionRects(kind, values);
    }
    static bool Same(RectInt32 const &a, RectInt32 const &b) {
        return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
    }
    static bool Same(std::vector<RectInt32> const &a, std::vector<RectInt32> const &b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](auto const &x, auto const &y) { return Same(x, y); });
    }
    static bool Overlaps(RectInt32 const &a, RectInt32 const &b) {
        return int64_t(a.X) < int64_t(b.X) + b.Width && int64_t(b.X) < int64_t(a.X) + a.Width &&
               int64_t(a.Y) < int64_t(b.Y) + b.Height && int64_t(b.Y) < int64_t(a.Y) + a.Height;
    }
    static std::vector<RectInt32> Exclude(std::vector<RectInt32> const &values, RectInt32 const &cut) {
        std::vector<RectInt32> result;
        for (auto const &r : values) {
            if (!Overlaps(r, cut)) {
                result.push_back(r);
                continue;
            }
            const auto left = std::max(r.X, cut.X), top = std::max(r.Y, cut.Y);
            const auto right = std::min(r.X + r.Width, cut.X + cut.Width);
            const auto bottom = std::min(r.Y + r.Height, cut.Y + cut.Height);
            auto add = [&](int x, int y, int width, int height) {
                if (width > 0 && height > 0)
                    result.push_back({x, y, width, height});
            };
            add(r.X, r.Y, r.Width, top - r.Y);
            add(r.X, bottom, r.Width, r.Y + r.Height - bottom);
            add(r.X, top, left - r.X, bottom - top);
            add(right, top, r.X + r.Width - right, bottom - top);
        }
        return result;
    }
    std::array<LONG, 10> Geometry() const {
        RECT outer{}, client{};
        POINT origin{};
        winrt::check_bool(GetWindowRect(hwnd, &outer));
        winrt::check_bool(GetClientRect(hwnd, &client));
        winrt::check_bool(ClientToScreen(hwnd, &origin));
        return {outer.left, outer.top,    outer.right,   outer.bottom, client.left,
                client.top, client.right, client.bottom, origin.x,     origin.y};
    }
    RectInt32 GroupRect() const {
        if (!bridge || bridge.IsClosed())
            winrt::throw_hresult(RO_E_CLOSED);
        if (!slot.Ready())
            winrt::throw_hresult(E_NOTIMPL);
        auto child = reinterpret_cast<HWND>(bridge.WindowId().Value);
        if (GetParent(child) != hwnd)
            winrt::throw_hresult(E_NOTIMPL);
        auto root = slot.lights.XamlRoot();
        auto point = slot.lights.TransformToVisual(nullptr).TransformPoint({0, 0});
        auto scale = root.RasterizationScale();
        POINT origin{};
        winrt::check_bool(ClientToScreen(child, &origin));
        winrt::check_bool(ScreenToClient(hwnd, &origin));
        auto left = int(std::floor(point.X * scale)), top = int(std::floor(point.Y * scale));
        return {left + origin.x, top + origin.y, int(std::ceil((point.X + slot.lights.ActualWidth()) * scale)) - left,
                int(std::ceil((point.Y + slot.lights.ActualHeight()) * scale)) - top};
    }
    static LRESULT CALLBACK Filter(HWND h, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<TabCaptionGuard *>(data);
        if (message == WM_NCDESTROY) {
            self->invalid = true;
            self->filterInstalled = false;
            RemoveWindowSubclass(h, Filter, Subclass);
            return DefSubclassProc(h, message, wp, lp);
        }
        if (self->operationDepth && NativeIslandFrame::IsClose(message, wp)) {
            self->closeMessage = message;
            self->closeWP = wp;
            self->closeLP = lp;
            return 0;
        }
        if (self->active) {
            if ((wp == HTCLOSE || wp == HTMINBUTTON || wp == HTMAXBUTTON) &&
                (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK))
                return 0;
            if (message == WM_NCHITTEST) {
                auto hit = DefSubclassProc(h, message, wp, lp);
                return hit == HTCLOSE || hit == HTMINBUTTON || hit == HTMAXBUTTON ? HTCLIENT : hit;
            }
            if (message == WM_GETTITLEBARINFOEX && lp) {
                auto result = DefSubclassProc(h, message, wp, lp);
                auto info = reinterpret_cast<TITLEBARINFOEX *>(lp);
                if (info->cbSize == sizeof(*info))
                    for (auto i : {2, 3, 5}) {
                        info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                        SetRectEmpty(&info->rgrect[i]);
                    }
                return result;
            }
        }
        return DefSubclassProc(h, message, wp, lp);
    }
    void Apply(x::Hosting::DesktopWindowXamlSource const &source) {
        if (used || changing || hwnd != slot.window || !slot.Ready() || IsIconic(hwnd) || IsZoomed(hwnd) ||
            GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
            winrt::throw_hresult(E_NOTIMPL);
        Operation operation(*this);
        DWORD_PTR previous{};
        if (GetWindowSubclass(hwnd, Filter, Subclass, &previous))
            winrt::throw_hresult(E_NOTIMPL);
        auto geometry = Geometry();
        // Ordinary native-caption windows must use their own layout path.
        if (geometry[9] != geometry[1])
            winrt::throw_hresult(E_NOTIMPL);
        bridge = source.SiteBridge();
        if (!bridge || bridge.IsClosed())
            winrt::throw_hresult(RO_E_CLOSED);
        if (source.Content() != slot.tabs.XamlRoot().Content())
            winrt::throw_hresult(E_NOTIMPL);
        protectedRect = GroupRect();
        if (protectedRect.X < 0 || protectedRect.Y < 0 || protectedRect.Width <= 0 || protectedRect.Height <= 0 ||
            protectedRect.X + protectedRect.Width > geometry[6] || protectedRect.Y + protectedRect.Height > geometry[7])
            winrt::throw_hresult(E_NOTIMPL);
        window = w::AppWindow::GetFromWindowId({reinterpret_cast<uint64_t>(hwnd)});
        presenter = window.Presenter().try_as<w::OverlappedPresenter>();
        if (!presenter || !presenter.HasBorder() || !presenter.HasTitleBar() ||
            window.TitleBar().ExtendsContentIntoTitleBar())
            winrt::throw_hresult(E_NOTIMPL);
        pointer = input::InputNonClientPointerSource::GetForWindowId(window.Id());
        originalCaption = Read(pointer, input::NonClientRegionKind::Caption);
        originalPass = Read(pointer, input::NonClientRegionKind::Passthrough);
        appliedCaption = Exclude(originalCaption, protectedRect);
        appliedPass = originalPass;
        appliedPass.push_back(protectedRect);
        palette.emplace(window.TitleBar());
        if (!SetWindowSubclass(hwnd, Filter, Subclass, reinterpret_cast<DWORD_PTR>(this)))
            winrt::throw_last_error();
        filterInstalled = true;
        used = active = changing = true;
        try {
            presenter.SetBorderAndTitleBar(true, false);
            appliedPalette.emplace(window.TitleBar());
            if (Geometry() != geometry || !slot.Ready() || !Same(GroupRect(), protectedRect) ||
                !Same(Read(pointer, input::NonClientRegionKind::Caption), originalCaption) ||
                !Same(Read(pointer, input::NonClientRegionKind::Passthrough), originalPass))
                winrt::throw_hresult(E_NOTIMPL);
            captionWritten = true;
            Write(pointer, input::NonClientRegionKind::Caption, appliedCaption);
            passWritten = true;
            Write(pointer, input::NonClientRegionKind::Passthrough, appliedPass);
            changing = false;
        } catch (...) {
            Restore();
            throw;
        }
    }
    bool Refresh() {
        if (!active)
            return false;
        Operation operation(*this);
        bool valid{};
        try {
            valid = !invalid && bridge && !bridge.IsClosed() && slot.Ready() && !presenter.HasTitleBar() &&
                    presenter.HasBorder() && Same(GroupRect(), protectedRect) &&
                    Same(Read(pointer, input::NonClientRegionKind::Caption), appliedCaption) &&
                    Same(Read(pointer, input::NonClientRegionKind::Passthrough), appliedPass);
        } catch (...) {
        }
        if (!valid) {
            Restore();
            return false;
        }
        slot.UpdateCapabilities();
        return true;
    }
    void Restore() {
        if (!active || restoring)
            return;
        Operation operation(*this);
        restoring = changing = true;
        struct Guard {
            TabCaptionGuard &self;
            ~Guard() {
                self.restoring = self.changing = false;
            }
        } guard{*this};
        if (!paletteMerged && appliedPalette) {
            palette->PreserveHostChanges(TitlePalette(window.TitleBar()), *appliedPalette);
            paletteMerged = true;
        }
        // Capture once before presenter restoration; keep rollback data on retry.
        if (!rollbackCaption) {
            auto value = Read(pointer, input::NonClientRegionKind::Caption);
            rollbackCaption = captionWritten && Same(value, appliedCaption) ? originalCaption : value;
        }
        if (!rollbackPass) {
            auto value = Read(pointer, input::NonClientRegionKind::Passthrough);
            rollbackPass = passWritten && Same(value, appliedPass) ? originalPass : value;
        }
        presenter.SetBorderAndTitleBar(true, true);
        palette->Restore(window.TitleBar());
        Write(pointer, input::NonClientRegionKind::Caption, *rollbackCaption);
        Write(pointer, input::NonClientRegionKind::Passthrough, *rollbackPass);
        active = false;
        RemoveFilter();
    }
    void RemoveFilter() {
        DWORD_PTR value{};
        if (filterInstalled && GetWindowSubclass(hwnd, Filter, Subclass, &value) &&
            value == reinterpret_cast<DWORD_PTR>(this))
            RemoveWindowSubclass(hwnd, Filter, Subclass);
        filterInstalled = false;
    }
    ~TabCaptionGuard() {
        try {
            Restore();
        } catch (...) {
        }
        RemoveFilter();
    }
};
} // namespace styler::research::winui
