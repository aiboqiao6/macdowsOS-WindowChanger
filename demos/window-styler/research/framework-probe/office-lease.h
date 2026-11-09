#pragma once

#include "office-edit-title.h"

// Separate from v18's bounded experiment. No timer changes layout: the sole
// timer checks ownership and retained tree identities, and fails closed.
namespace {
constexpr UINT_PTR OfficeLeaseSubclass = 0x4f464c53;
UINT OfficeLeaseMetrics() {
    return RegisterWindowMessageW(L"Macdows.OfficeLease.Metrics.v1");
}
struct OfficeLease {
    OfficeLayoutApi api{};
    OfficeFrameScope frame;
    HWND window{}, rootWindow{}, owner{};
    DWORD ownerPid{};
    HANDLE process{};
    BYTE *root{}, *parent{};
    OfficeEditorTitle editor;
    bool editing{};
    std::array<BYTE *, 3> created{};
    struct Anchor {
        BYTE *node{}, *parent{}, *reference{};
    };
    struct Saved {
        BYTE *node{};
        size_t property{};
        BYTE *original{}, *applied{};
        bool changed{};
    };
    std::vector<Anchor> anchors;
    std::vector<Saved> saved;
    std::vector<BYTE *> ownedReferences;
    std::filesystem::path report;
    unsigned calls{}, working{}, cleanupFailures{}, skipped{}, forwarded{}, canceledInput{};
    size_t mismatchProperty{};
    int mismatchValue{};
    bool active{}, stopping{}, finished{}, pending{}, topGone{}, rootGone{}, timer{}, topSubclass{}, rootSubclass{},
        published{};
    int pressed = -1;
    const char *reason = "attach-failed";
    void CancelInput() {
        if (pressed < 0)
            return;
        pressed = -1;
        ++canceledInput;
        if (!rootGone && IsWindow(rootWindow) && reinterpret_cast<BYTE *>(GetWindowLongPtrW(rootWindow, 0)) == root) {
            // Cancel the original framework's pressed/captured state BEFORE
            // destroying our nodes. Never send a synthetic button-up/command.
            SendMessageW(rootWindow, WM_CANCELMODE, 0, 0);
            if (GetCapture() == rootWindow)
                ReleaseCapture();
        }
    }

    static bool Alive(BYTE *node) {
        BYTE flags{};
        return node && Read(node + 0x64, &flags, 1) && (flags & 0xc) < 8;
    }
    bool Retain(BYTE *node) {
        if (!Alive(node))
            return false;
        for (auto &anchor : anchors)
            if (anchor.node == node)
                return true;
        auto reference = api.Fn<BYTE *(*)(BYTE *)>(0x170340)(node);
        if (!reference)
            return false;
        anchors.push_back({node, Value<BYTE *>(node + 8), reference});
        return true;
    }
    bool Save(BYTE *node, size_t property) {
        if (!Retain(node))
            return false;
        auto value = api.Get(node, property);
        if (!value)
            return false;
        saved.push_back({node, property, value});
        return true;
    }
    bool RestoreValue(Saved &value) {
        return SUCCEEDED(Value<short>(value.original + 4) == -1
                             ? api.Fn<HRESULT (*)(BYTE *, BYTE *, bool, void *)>(0x285fa0)(
                                   value.node, api.base + value.property, false, nullptr)
                             : api.Set(value.node, value.property, value.original));
    }
    bool HideOriginals(bool saveLatest) {
        for (auto &value : saved) {
            if (!Alive(value.node))
                return false;
            if (saveLatest) {
                auto latest = api.Get(value.node, value.property);
                if (!latest)
                    return false;
                api.Release(value.original);
                value.original = latest;
            }
            auto hr = value.property == 0x83fb10
                          ? api.Visible(value.node, false)
                          : value.property == 0x83fba0
                          ? api.Set(value.node, value.property, api.False())
                          : value.property == 0x83f398
                          ? api.Padding(value.node, 126)
                          : api.Int(value.node, value.property, value.property == 0x837128 ? 0 : HTCAPTION);
            value.changed = true;
            api.Release(value.applied);
            value.applied = api.Get(value.node, value.property);
            if (FAILED(hr) || !value.applied)
                return false;
        }
        return true;
    }
    bool SuspendOriginals() {
        for (auto &value : saved) {
            if (!Alive(value.node))
                return false;
            auto actual = api.Get(value.node, value.property);
            if (!actual)
                return false;
            bool ours = actual == value.applied;
            if (!ours) {
                // Preserve a newer host-local value rather than rolling it back.
                api.Release(value.original);
                value.original = actual;
            } else
                api.Release(actual);
            if (ours && !RestoreValue(value))
                return false;
            value.changed = false;
            api.Release(value.applied);
            value.applied = nullptr;
        }
        return true;
    }
    bool Health() {
        if (topGone || rootGone || !IsWindow(rootWindow) ||
            reinterpret_cast<BYTE *>(GetWindowLongPtrW(rootWindow, 0)) != root || GetDpiForWindow(window) != 144 ||
            (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
            GetPropW(window, styler::style_view::StyleProperty) != reinterpret_cast<HANDLE>(1) ||
            GetPropW(window, styler::style_view::CaptionBoxesProperty) !=
                reinterpret_cast<HANDLE>(styler::style_view::CaptionBoxMask + 1))
            return false;
        for (auto &anchor : anchors)
            if (!Alive(anchor.node) || Value<BYTE *>(anchor.node + 8) != anchor.parent)
                return false;
        for (auto node : created)
            if (!Alive(node) || Value<BYTE *>(node + 8) != parent)
                return false;
        for (auto &value : saved) {
            auto actual = api.Get(value.node, value.property);
            bool matches = actual && actual == value.applied;
            api.Release(actual);
            if (!matches) {
                mismatchProperty = value.property;
                mismatchValue =
                    api.Fn<int (*)(BYTE *, BYTE *, bool *)>(0x333d0)(value.node, api.base + value.property, nullptr);
                return false;
            }
        }
        if (editing && !IsIconic(window) && !editor.Layout(rootWindow, 126))
            return false;
        return true;
    }
    bool OwnerAlive() const {
        DWORD pid{};
        return IsWindow(owner) && GetWindowThreadProcessId(owner, &pid) && pid == ownerPid && process &&
               WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
    void WriteReport() const {
        std::ofstream out(report);
        out << "{\"active\":" << (active ? "true" : "false") << ",\"reason\":\"" << reason
            << "\",\"cleanupFailures\":" << cleanupFailures << ",\"skippedDestroyedOrHostChanged\":" << skipped
            << ",\"forwardedInputMessages\":" << forwarded << ",\"retainedNodes\":" << anchors.size()
            << ",\"savedProperties\":" << saved.size() << ",\"mismatchProperty\":" << mismatchProperty
            << ",\"mismatchValue\":" << mismatchValue << ",\"canceledInput\":" << canceledInput << "}\n";
    }
    // Only quiescent, outermost callbacks may restore. The current callback
    // keeps this allocation alive through DefSubclassProc and nested messages.
    void Stop(const char *why) {
        if (stopping || finished)
            return;
        reason = why;
        if (working || calls > 1) {
            pending = true;
            return;
        }
        stopping = true;
        active = false;
        pending = false;
        bool refresh = frame.window != nullptr;
        for (const auto &value : saved)
            refresh = refresh || value.changed;
        for (auto node : created)
            refresh = refresh || (Alive(node) && Value<BYTE *>(node + 8) == parent);
        if (timer && !topGone)
            KillTimer(window, OfficeLeaseSubclass);
        CancelInput();
        if (topSubclass && !topGone)
            RemoveWindowSubclass(window, Dispatch, OfficeLeaseSubclass);
        if (rootSubclass && !rootGone)
            RemoveWindowSubclass(rootWindow, Dispatch, OfficeLeaseSubclass);
        timer = topSubclass = rootSubclass = false;
        if (topGone) {
            // Never touch a potentially recycled HWND after WM_NCDESTROY.
            frame.window = nullptr;
            frame.menu = frame.metrics = frame.subclass = false;
        }
        for (auto &node : created)
            if (node) {
                if (Alive(node)) {
                    auto actualParent = Value<BYTE *>(node + 8);
                    if (actualParent == parent && Alive(parent)) {
                        if (FAILED(api.Remove(parent, node)))
                            ++cleanupFailures;
                    } else if (actualParent) {
                        // A host-reparented instance no longer belongs to us.
                        ++skipped;
                        node = nullptr;
                        continue;
                    }
                    if (Alive(node) && !Value<BYTE *>(node + 8))
                        api.Destroy(node);
                    else if (Alive(node))
                        ++cleanupFailures;
                } else
                    ++skipped;
                node = nullptr;
            }
        for (auto &value : saved) {
            if (value.changed && Alive(value.node)) {
                auto actual = api.Get(value.node, value.property);
                bool ours = actual && actual == value.applied;
                api.Release(actual);
                if (ours) {
                    if (!RestoreValue(value))
                        ++cleanupFailures;
                    auto restored = api.Get(value.node, value.property);
                    if (restored != value.original)
                        ++cleanupFailures;
                    api.Release(restored);
                } else
                    ++skipped;
            } else if (value.changed)
                ++skipped;
            api.Release(value.original);
            api.Release(value.applied);
        }
        frame.Restore();
        if (refresh && !topGone && !rootGone && IsWindow(rootWindow) &&
            reinterpret_cast<BYTE *>(GetWindowLongPtrW(rootWindow, 0)) == root && Alive(root))
            OfficeRefresh(rootWindow);
        WriteReport();
        for (auto reference : ownedReferences)
            api.Release(reference);
        for (auto &anchor : anchors)
            api.Release(anchor.reference);
        if (process)
            CloseHandle(process);
        process = nullptr;
        // Keep ownership published until all restoration and reference release
        // has finished. External observers must not mistake teardown for done.
        if (!topGone) {
            if (published && GetPropW(window, styler::InlineProperty) == reinterpret_cast<HANDLE>(19))
                RemovePropW(window, styler::InlineProperty);
            if (GetPropW(window, styler::OwnerProperty) == owner) {
                RemovePropW(window, styler::AdapterProperty);
                RemovePropW(window, styler::OwnerProperty);
            }
        }
        finished = true;
        if (!calls)
            delete this;
    }
    struct Callback {
        OfficeLease *self;
        explicit Callback(OfficeLease *s) : self(s) {
            ++self->calls;
        }
        ~Callback() {
            if (!--self->calls) {
                if (self->finished)
                    delete self;
                else if (self->pending && !self->working)
                    self->Stop(self->reason);
            }
        }
    };
    int Hit(POINT screen) const {
        if (!ScreenToClient(rootWindow, &screen))
            return -1;
        for (int i = 0; i < 3; ++i)
            if (PtInRect(&frame.buttons[i], screen))
                return i;
        return -1;
    }
    static LRESULT CALLBACK Dispatch(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto s = reinterpret_cast<OfficeLease *>(data);
        Callback callback(s);
        if (m == WM_NCDESTROY) {
            RemoveWindowSubclass(w, Dispatch, OfficeLeaseSubclass);
            if (w == s->window)
                s->topGone = true;
            else
                s->rootGone = true;
            s->Stop("window-destroyed");
            return DefSubclassProc(w, m, wp, lp);
        }
        if (m == styler::QueryMessage())
            return s->active && !s->pending ? styler::Acknowledged : 0;
        if (m == styler::DetachMessage()) {
            if (reinterpret_cast<HWND>(wp) != s->owner)
                return 0;
            if (s->calls > 1 || s->working || s->stopping)
                return styler::TransitionBusy;
            s->Stop("explicit-detach");
            return styler::Acknowledged;
        }
        if (s->stopping)
            return DefSubclassProc(w, m, wp, lp);
        if (m == OfficeLeaseMetrics()) {
            if (wp == 0)
                return reinterpret_cast<LRESULT>(s->rootWindow);
            if (wp == 1)
                return s->active && s->Health();
            if (wp == 2)
                return s->forwarded;
            if (wp == 4)
                return s->pressed >= 0;
            if (wp == 5)
                return GetCapture() == s->rootWindow;
            if (wp == 6)
                return s->canceledInput;
            if (wp == 7) {
                unsigned disabled{};
                for (auto &value : s->saved)
                    if (value.property == 0x83fba0) {
                        auto actual = s->api.Get(value.node, value.property);
                        bool off = actual && actual == s->api.False();
                        s->api.Release(actual);
                        if (!off)
                            return 0;
                        ++disabled;
                    }
                return disabled == 3;
            }
            if (wp == 8)
                return s->editing;
            if (wp == 9 && s->editing) {
                RECT outer{}, client{};
                GetWindowRect(s->window, &outer);
                GetClientRect(s->rootWindow, &client);
                return OfficeLocalRect(s->editor.children.front()).right + 126 + 600 + outer.right - outer.left - client.right;
            }
            if (wp == 3 && lp >= 0 && lp < 5) {
                DwmFlush();
                auto capture = OfficeCaptureTop(s->rootWindow, s->window);
                constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
                unsigned mask{};
                for (unsigned i = 0; i < 3; ++i)
                    if (capture.Count(colors[i], s->frame.buttons[i]) >= 3)
                        mask |= 1u << i;
                capture.Save(s->report.parent_path() / (L"office-lease-" + std::to_wstring(GetCurrentProcessId()) +
                                                        L"-paint-" + std::to_wstring(lp) + L".bmp"));
                return mask;
            }
            if (wp == 40)
                return SendMessageW(s->window, styler::DetachMessage(), reinterpret_cast<WPARAM>(s->owner), 0);
            if (wp == 41)
                return SendMessageW(s->window, OfficeLeaseMetrics(), 42, 0);
            if (wp == 42) {
                s->Stop("nested-invalidation-contract");
                return styler::TransitionBusy;
            }
            if (wp >= 10 && wp < 16) {
                auto r = s->frame.buttons[(wp - 10) / 2];
                POINT point{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                ClientToScreen(s->rootWindow, &point);
                return (wp - 10) % 2 ? point.y : point.x;
            }
            return 0;
        }
        if (m == WM_TIMER && wp == OfficeLeaseSubclass && w == s->window) {
            if (s->working || s->calls > 1)
                return 0;
            if (!s->OwnerAlive())
                s->Stop("owner-lost");
            else if (!s->Health())
                s->Stop("tree-or-property-changed");
            return 0;
        }
        if (s->active && m == WM_DPICHANGED) {
            s->Stop("unsupported-environment-change");
            return DefSubclassProc(w, m, wp, lp);
        }
        if (!s->active || s->pending)
            return DefSubclassProc(w, m, wp, lp);
        if (w == s->rootWindow && m == WM_LBUTTONDOWN) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(w, &point);
            auto hit = s->Hit(point);
            if (hit >= 0) {
                if (!s->Health()) {
                    s->Stop("input-tree-invalid");
                    return DefSubclassProc(w, m, wp, lp);
                }
                s->pressed = hit;
            }
        }
        if (w == s->rootWindow && m == WM_LBUTTONUP)
            s->pressed = -1;
        if (w == s->window && (m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_NCMOUSEMOVE ||
                               m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP)) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            bool client = m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP;
            if (client)
                ClientToScreen(w, &point);
            auto hit = s->Hit(point);
            if (hit >= 0 || s->pressed >= 0) {
                if (!s->Health()) {
                    s->Stop("input-tree-invalid");
                    return DefSubclassProc(w, m, wp, lp);
                }
                ScreenToClient(s->rootWindow, &point);
                auto coordinates = MAKELPARAM(point.x, point.y);
                ++s->forwarded;
                if (m == WM_LBUTTONDOWN || m == WM_NCLBUTTONDOWN) {
                    s->pressed = hit;
                    SendMessageW(s->rootWindow, WM_MOUSEMOVE, 0, coordinates);
                    if (!s->pending)
                        SendMessageW(s->rootWindow, WM_LBUTTONDOWN, MK_LBUTTON, coordinates);
                } else if (m == WM_LBUTTONUP || m == WM_NCLBUTTONUP) {
                    s->pressed = -1;
                    SendMessageW(s->rootWindow, WM_LBUTTONUP, 0, coordinates);
                } else
                    SendMessageW(s->rootWindow, WM_MOUSEMOVE, s->pressed >= 0 ? MK_LBUTTON : 0, coordinates);
                return 0;
            }
        }
        if (m == WM_CANCELMODE && w == s->window)
            s->CancelInput();
        if (m == WM_CANCELMODE || m == WM_CAPTURECHANGED)
            s->pressed = -1;
        bool layout = m == WM_SIZE || m == WM_WINDOWPOSCHANGING || m == WM_WINDOWPOSCHANGED || m == WM_GETMINMAXINFO ||
                      m == WM_THEMECHANGED || m == WM_SETTINGCHANGE ||
                      (w == s->window && m == WM_SYSCOMMAND &&
                       ((wp & 0xfff0) == SC_MAXIMIZE || (wp & 0xfff0) == SC_MINIMIZE || (wp & 0xfff0) == SC_RESTORE));
        if (layout && !s->working) {
            ++s->working;
            bool suspended = s->SuspendOriginals();
            auto result = DefSubclassProc(w, m, wp, lp);
            bool reapplied = suspended && !s->pending && !s->rootGone && !s->topGone && s->HideOriginals(true);
            --s->working;
            if (!reapplied || !s->Health())
                s->Stop("layout-tree-invalid");
            return result;
        }
        auto result = DefSubclassProc(w, m, wp, lp);
        if (!s->pending && !s->working && s->active && s->calls == 1 && w == s->window &&
            (m == WM_SIZE || m == WM_WINDOWPOSCHANGED) && !s->Health())
            s->Stop("layout-tree-invalid");
        return result;
    }
    bool Start(const styler::AdapterContextV1 &context) {
        ++working;
        window = context.window;
        rootWindow = selection.root;
        owner = context.owner;
        api.base = modules[0].base;
        root = reinterpret_cast<BYTE *>(GetWindowLongPtrW(rootWindow, 0));
        parent = root ? Value<BYTE *>(root + 0x10) : nullptr;
        GetWindowThreadProcessId(owner, &ownerPid);
        process = OpenProcess(SYNCHRONIZE, FALSE, ownerPid);
        HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Dispatch), &self);
        wchar_t path[32768]{};
        GetModuleFileNameW(self, path, 32768);
        report = std::filesystem::path(path).parent_path() /
                 (L"office-lease-" + std::to_wstring(GetCurrentProcessId()) + L".json");
        auto prepare = [&]() {
            std::string name;
            constexpr LONG_PTR bits = WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            if (!process || !api.False() || GetPropW(window, styler::OwnerProperty) || GetPropW(window, styler::InlineProperty) ||
                IsIconic(window) || !IsWindowEnabled(window) || GetDpiForWindow(window) != 144 ||
                (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
                (GetWindowLongPtrW(window, GWL_STYLE) & bits) != bits || !ClassName(root, name))
                return false;
            editing = name == "HWNDElement";
            if (editing) {
                if (!editor.Find(root, selection.parent, rootWindow, api.base))
                    return false;
                parent = editor.row;
                if (!Retain(root) || !Retain(parent) || !Retain(editor.title) || !Retain(editor.content) ||
                    !Save(editor.title, 0x83f398))
                    return false;
                auto padding = saved.back().original;
                if (Value<short>(padding + 4) != -1 &&
                    (Value<short>(padding + 4) != 10 || Value<RECT>(padding + 8).left ||
                     Value<RECT>(padding + 8).top || Value<RECT>(padding + 8).right || Value<RECT>(padding + 8).bottom))
                    return false;
                for (auto child : editor.children)
                    if (!Retain(child))
                        return false;
            } else if (name != "FullpageUIWindow" || !parent ||
                Value<BYTE **>(parent) != reinterpret_cast<BYTE **>(api.base + 0x84ba60) ||
                Value<BYTE *>(parent + 8) != root || !Retain(root) || !Retain(parent))
                return false;
            // Pin the complete path from the selected original group to root.
            BYTE *caption{};
            unsigned count{};
            for (auto node = selection.parent; node && node != root; node = Value<BYTE *>(node + 8)) {
                if (++count > 64 || !Retain(node))
                    return false;
                if (ClassName(node, name) && name == "AppFrameHelper" &&
                    api.Fn<int (*)(BYTE *, BYTE *, bool *)>(0x333d0)(node, api.base + 0x948250, nullptr) == HTCAPTION)
                    caption = node;
            }
            bool editingTitle = editing;
            if (!caption && !editingTitle)
                return false;
            POINT origin{};
            for (auto node = editingTitle ? nullptr : caption; node && node != root; node = Value<BYTE *>(node + 8)) {
                name.clear();
                ClassName(node, name);
                if (Value<BYTE **>(node) != reinterpret_cast<BYTE **>(api.base + 0x84ba60) && name != "AppFrameHelper")
                    return false;
                auto p = Value<POINT>(node + 0x78);
                origin.x += p.x;
                origin.y += p.y;
            }
            if (editingTitle)
                origin.x = 126;
            if (origin.x < MulDiv(84, 144, 96))
                return false;
            for (auto button : selection.buttons) {
                auto glyph = button ? Value<BYTE *>(button + 0x10) : nullptr;
                if (!glyph || !Save(button, 0x83fb10) || !Save(glyph, 0x83fb10) || !Save(button, 0x837128) ||
                    !Save(button, 0x948250))
                    return false;
                if (!Save(button, 0x83fba0))
                    return false;
            }
            for (unsigned i = 0; i < 3; ++i) {
                auto hr = api.Fn<HRESULT (*)(BYTE *, BYTE **)>(0x1c1180)(api.base + 0xd959f0, &created[i]);
                if (FAILED(hr) || !created[i])
                    return false;
                auto ref = api.Fn<BYTE *(*)(BYTE *)>(0x170340)(created[i]);
                if (!ref)
                    return false;
                ownedReferences.push_back(ref);
                if (!ClassName(created[i], name) || name != "AppFrameHelper")
                    return false;
                constexpr int commands[]{SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE}, hits[]{HTCLOSE, HTMINBUTTON, HTMAXBUTTON};
                for (auto property : {std::pair<size_t, int>{0x838600, 2},
                                      {0x949f20, MulDiv(10 + static_cast<int>(i) * 20, 144, 96)},
                                      {0x949f60, editingTitle ? 0 : MulDiv(4, 144, 96)},
                                      {0x83f290, 20},
                                      {0x83f250, editingTitle ? 34 : 24},
                                      {0x837128, 1},
                                      {0x8371a0, commands[i]},
                                      {0x948250, hits[i]},
                                      {0x8387c0, MulDiv(12, 144, 96)}})
                    if (FAILED(api.Int(created[i], property.first, property.second)))
                        return false;
                if (editingTitle && FAILED(api.Int(created[i], 0x83fd30, 5)))
                    return false;
                constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
                if (FAILED(api.Text(created[i], 0x83ff48, L"\x25cf")) ||
                    FAILED(api.Text(created[i], 0x83f3d8, L"Segoe UI Symbol")) ||
                    FAILED(api.Color(created[i], colors[i])) || FAILED(api.Visible(created[i], true)))
                    return false;
            }
            topSubclass = SetWindowSubclass(window, Dispatch, OfficeLeaseSubclass, reinterpret_cast<DWORD_PTR>(this));
            rootSubclass =
                SetWindowSubclass(rootWindow, Dispatch, OfficeLeaseSubclass, reinterpret_cast<DWORD_PTR>(this));
            if (!topSubclass || !rootSubclass || FAILED(api.Insert(parent, created.data(), 3)) ||
                !frame.Start(window, rootWindow))
                return false;
            if (!HideOriginals(false))
                return false;
            OfficeRefresh(rootWindow);
            for (unsigned i = 0; i < 3; ++i) {
                auto size = Value<SIZE>(created[i] + 0x80);
                int x = MulDiv(10 + static_cast<int>(i) * 20, 144, 96), y = editingTitle ? 0 : MulDiv(4, 144, 96);
                frame.buttons[i] = {x, y, x + size.cx, y + size.cy};
                if (size.cx <= 0 || size.cy <= 0 || x + size.cx > origin.x || y + size.cy > MulDiv(48, 144, 96))
                    return false;
            }
            timer = SetTimer(window, OfficeLeaseSubclass, 250, nullptr) != 0;
            if (!timer || pending || !OwnerAlive() || !Health())
                return false;
            published = SetPropW(window, styler::InlineProperty, reinterpret_cast<HANDLE>(19)) != FALSE;
            if (!published || !SetPropW(window, styler::OwnerProperty, owner))
                return false;
            active = true;
            reason = "active";
            WriteReport();
            return true;
        };
        bool result = prepare();
        --working;
        return result && !pending;
    }
};
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || context->adapterId != 272 || !selection.root ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId() || !Runtime())
        return ERROR_NOT_SUPPORTED;
    auto base = modules[0].base;
    if (Value<BYTE *>(Value<BYTE **>(base + 0xd959f0)) != base + 0x1c1180)
        return ERROR_REVISION_MISMATCH;
    auto state = new OfficeLease;
    bool ok = state->Start(*context);
    selection = {};
    if (!ok) {
        state->Stop("attach-failed");
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_SUCCESS;
}
