#include "adapter-api.h"
#include "pe-identity.h"
#include "views-layout.h"
#include "views-profiles.h"
#include "third_party/minhook/include/MinHook.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Chromium Views ABI profile. The module is found through the framework's
// HWNDMessageHandler, never by executable/module/product/control names.
// All object offsets below belong to framework View / Widget / WindowImpl.
// New builds require another verified CodeView profile; no signature guessing.
namespace {
using P = BYTE *;
using Rect = styler::views::Rect;
using Role = styler::views::Role;
constexpr size_t SubclassId = 0x4d435657;
P module{};
styler::views::Profile const *abi{};
using BoundsFn = void (*)(P, const Rect &);
using PaintFn = void (*)(P, P);
BoundsFn originalBounds{};
using LayoutFn = void (*)(P);
LayoutFn originalLayout{};
using VisibleFn = void (*)(P, bool);
VisibleFn originalVisible{};
std::mutex hooksMutex;
size_t sessions{};
bool enabled{};
bool Readable(const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION i{};
    return p && VirtualQuery(p, &i, sizeof(i)) && i.State == MEM_COMMIT &&
           !(i.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && uintptr_t(p) + n >= uintptr_t(p) &&
           uintptr_t(p) + n <= uintptr_t(i.BaseAddress) + i.RegionSize;
}
template <class T> T Read(P p, size_t offset = 0) {
    T v{};
    if (p && Readable(p + offset, sizeof(v)))
        memcpy(&v, p + offset, sizeof(v));
    return v;
}
template <class F> F Virtual(P p, size_t offset) {
    return reinterpret_cast<F>(Read<P>(Read<P>(p), offset));
}
bool Code(P p) {
    return module && abi && p >= module + 0x1000 && p < module + abi->textEnd;
}
bool Life(P view) {
    return abi && Readable(view, 0x330) && Read<DWORD>(view, abi->life) == 0x600d600d;
}
UINT RebindMessage() {
    return RegisterWindowMessageW(L"Macdows.WindowStyler.Views.Rebind.v12");
}
struct Node {
    P view{}, parent{}, vtable{};
    Rect rect{};
    int x{}, y{}, depth{}, hit{};
    bool visible{};
};
struct State;
struct Watch {
    State *state{};
    P view{}, parent{}, vtable{};
    Role role{};
    Rect original{}, applied{};
    Rect paintedBounds{};
    bool painted{};
    std::vector<P> clone;
    bool Button() const {
        return role != Role::Region && role != Role::Container;
    }
    P Table() const {
        return clone.empty() ? vtable : reinterpret_cast<P>(const_cast<P *>(clone.data()));
    }
};
struct State {
    HWND hwnd{}, owner{};
    P root{}, handler{};
    DWORD thread{};
    HANDLE process{};
    UINT_PTR timer{};
    bool changing{}, queued{}, released{};
    unsigned callbacks{}, layouts{}, mask{}, paints{}, rebinds{};
    DWORD paintError{};
    std::vector<std::unique_ptr<Watch>> watches;
    std::unordered_set<P> known;
};
thread_local std::vector<State *> states;
thread_local std::unordered_map<P, Watch *> watched;
thread_local std::unordered_map<P, Watch *> paintWatches;
// Instance vtables/trampolines can still appear on a framework stack during a
// synchronous close. Retire them for the lifetime of this pinned DLL.
thread_local std::vector<std::unique_ptr<Watch>> retired;
void Debug(const char *text) {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Debug), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L'\\') + 1);
    if (GetFileAttributesW((directory + L"layout-diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::ofstream log(directory + L"views-" + std::to_wstring(GetCurrentProcessId()) + L".txt", std::ios::app);
    log << GetTickCount64() << " " << text << "\n";
}

bool Framework(HWND hwnd, P &root, P *handlerOut = nullptr) {
    if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
        return false;
    P handler = reinterpret_cast<P>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!Readable(handler, 0x78) || Read<HWND>(handler, 0x40) != hwnd)
        return false;
    HMODULE handle{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(Read<P>(handler)), &handle))
        return false;
    auto profile = styler::views::FindProfile(handle);
    if (!profile)
        return false;
    auto base = reinterpret_cast<P>(handle);
    {
        std::scoped_lock lock(hooksMutex);
        if (module && module != base)
            return false;
        if (!module) {
            module = base;
            abi = profile;
        }
    }
    BYTE accessible[]{0x48, 0x83, 0xec, 0x28, 0x48, 0x81, 0xc1, profile->accessibleAdjustment, 0xfe, 0xff, 0xff, 0xe8};
    constexpr BYTE rootGetter[]{0x48, 0x8b, 0x81, 0xa0, 0x01, 0, 0, 0xc3};
    if (Read<P>(handler) != base + abi->handlerTable ||
        memcmp(base + abi->accessible, accessible, sizeof(accessible)) ||
        memcmp(base + abi->rootGetter, rootGetter, sizeof(rootGetter)))
        return false;
    P delegate = Read<P>(handler, abi->delegate);
    if (!Readable(delegate, 8) || Read<P>(Read<P>(delegate), 0xb8) != base + abi->accessible)
        return false;
    // The same framework weak-reference flag used by GetNativeViewAccessible.
    // Read it before dereferencing the cached Widget; do not bypass expiry.
    P host = delegate - abi->hostAdjust, flag = Read<P>(host, abi->weak);
    if (!Readable(flag, 8) || Read<BYTE>(flag, 4))
        return false;
    P widget = Read<P>(host, abi->widget);
    if (!Readable(widget, 0x1a8))
        return false;
    root = Read<P>(widget, 0x1a0);
    if (!Life(root) || Read<P>(Read<P>(root), abi->vectorEnds ? 0x220 : 0x238) != base + abi->rootAccess)
        return false;
    if (handlerOut)
        *handlerOut = handler;
    return true;
}
bool Walk(P view, P parent, std::vector<Node> &nodes, int depth = 0, int x = 0, int y = 0, bool visible = true) {
    if (depth > 24 || nodes.size() > 4096 || !Life(view) || Read<P>(view, abi->parent) != parent ||
        std::any_of(nodes.begin(), nodes.end(), [&](auto const &n) { return n.view == view; }))
        return false;
    Node n{};
    n.view = view;
    n.parent = parent;
    n.vtable = Read<P>(view);
    n.rect = Read<Rect>(view, abi->bounds);
    n.x = x + n.rect.x;
    n.y = y + n.rect.y;
    n.depth = depth;
    n.visible = visible && Read<bool>(view, abi->visible);
    nodes.push_back(n);
    auto children = Read<P>(view, abi->children);
    auto count = Read<size_t>(view, abi->children + 8), capacity = Read<size_t>(view, abi->children + 16);
    if (abi->vectorEnds) {
        auto begin = reinterpret_cast<size_t>(children);
        if (count < begin || capacity < count || (count - begin) % 8 || (capacity - begin) % 8)
            return false;
        count = (count - begin) / 8;
        capacity = (capacity - begin) / 8;
    }
    if (count > 4096 || capacity < count || (count && !Readable(children, count * 8)))
        return false;
    for (size_t i = 0; i < count; i++)
        if (!Walk(Read<P>(children, i * 8), view, nodes, depth + 1, n.x, n.y, n.visible))
            return false;
    return true;
}
bool Descendant(P view, P root) {
    for (int i = 0; view && i < 26; i++) {
        if (view == root)
            return true;
        if (!Life(view))
            return false;
        view = Read<P>(view, abi->parent);
    }
    return false;
}
int RootX(P view) {
    int x = 0;
    for (int i = 0; view && i < 26; i++) {
        if (!Life(view))
            break;
        x += Read<Rect>(view, abi->bounds).x;
        view = Read<P>(view, abi->parent);
    }
    return x;
}
int RootY(P view) {
    int y = 0;
    for (int i = 0; view && i < 26; i++) {
        if (!Life(view))
            break;
        y += Read<Rect>(view, abi->bounds).y;
        view = Read<P>(view, abi->parent);
    }
    return y;
}
Watch *Lookup(P view) {
    auto found = watched.find(view);
    if (found == watched.end())
        return nullptr;
    auto w = found->second;
    return !w->state->released && Life(view) && Read<P>(view) == w->Table() &&
                   Read<P>(view, abi->parent) == w->parent && Descendant(view, w->state->root)
               ? w
               : nullptr;
}
Rect Applied(Watch *w) {
    auto r = styler::views::Apply(w->original, w->role, RootX(w->parent));
    // A never-shown restore View can have empty bounds until its first layout.
    // Its caption row already defines the button height. Keep the original empty
    // rectangle for rollback and measurement, but give both native actions the
    // same row geometry when the framework switches their visibility.
    if (w->Button() && r.h == 0) {
        auto row = Read<Rect>(w->parent, abi->bounds);
        if (row.h >= 16 && row.h <= 64) {
            r.y = 0;
            r.h = row.h;
        }
    }
    return r;
}
void Queue(State *s) {
    if (!s->released && !s->changing && !s->queued) {
        s->queued = PostMessageW(s->hwnd, RebindMessage(), 0, 0) != FALSE;
    }
}
void Damage(Watch *w) {
    if (!w->Button() || !abi->schedulePaint || !Read<bool>(w->view, abi->visible))
        return;
    if (!w->painted || Read<Rect>(w->view, abi->bounds) != w->paintedBounds)
        reinterpret_cast<void (*)(P)>(module + abi->schedulePaint)(w->view);
}
void BoundsHook(P view, const Rect &requested) {
    auto w = Lookup(view);
    if (w && !w->state->changing) {
        auto s = w->state;
        ++s->callbacks;
        auto r = styler::views::OriginalRequest(requested, w->original, w->applied);
        w->original = r;
        if (s->layouts) {
            w->original = requested;
            originalBounds(view, requested);
            --s->callbacks;
            return;
        }
        bool region = w->role != Role::Region || (r.h > 0 && r.h <= 64 && RootY(w->parent) + r.y < 64);
        w->applied = region ? Applied(w) : r;
        originalBounds(view, w->applied);
        --s->callbacks;
        return;
    }
    originalBounds(view, requested);
    for (auto s : states)
        if (!s->changing && !s->released && (!s->known.contains(view) || watched.contains(view)) && Life(view) &&
            requested.h > 0 && requested.h <= 64 && RootY(view) < 64 && Descendant(view, s->root)) {
            Queue(s);
            break;
        }
}
void Geometry(State *s, bool apply) {
    for (auto &item : s->watches) {
        auto w = item.get();
        if (!Lookup(w->view))
            continue;
        Rect r = apply ? Applied(w) : w->original;
        if (apply)
            w->applied = r;
        originalBounds(w->view, r);
        if (apply)
            Damage(w);
    }
}
void BeginGeometry(State *s) {
    ++s->callbacks;
    if (s->layouts++ == 0) {
        s->changing = true;
        Geometry(s, false);
        s->changing = false;
    }
}
void EndGeometry(State *s) {
    if (--s->layouts == 0 && !s->released) {
        s->changing = true;
        Geometry(s, true);
        s->changing = false;
    }
    --s->callbacks;
}
void VisibleHook(P view, bool visible) {
    auto w = Lookup(view);
    if (!w || w->state->changing) {
        originalVisible(view, visible);
        return;
    }
    auto s = w->state;
    bool changed = Read<bool>(view, abi->visible) != visible;
    if (changed)
        w->painted = false;
    BeginGeometry(s);
    originalVisible(view, visible);
    EndGeometry(s);
    if (changed)
        Queue(s);
}
void LayoutHook(P view) {
    State *s{};
    for (auto state : states)
        if (!state->released && !state->changing && Descendant(view, state->root)) {
            s = state;
            break;
        }
    if (!s) {
        originalLayout(view);
        return;
    }
    BeginGeometry(s);
    originalLayout(view);
    EndGeometry(s);
}
struct Float4 {
    float x, y, z, w;
};
struct RoundRect {
    Float4 bounds;
    float radii[8];
    int type;
};
static_assert(sizeof(RoundRect) == 52);
void PaintHook(P view, P canvas) {
    auto entry = paintWatches.find(view);
    if (entry == paintWatches.end())
        return;
    auto w = entry->second;
    if (Read<P>(view) != w->Table())
        return;
    auto s = w->state;
    auto fallback = reinterpret_cast<PaintFn>(Read<P>(w->vtable, abi->paint));
    if (s->changing || s->layouts || !Lookup(view)) {
        fallback(view, canvas);
        return;
    }
    ++s->callbacks;
    P recording = Read<P>(canvas, 0xc0);
    P table = Read<P>(recording);
    bool valid = Readable(table, 0xc0) && Code(Read<P>(table, 0x20)) && Code(Read<P>(table, 0x50)) &&
                 Code(Read<P>(table, 0x98)) && Code(Read<P>(table, 0xb8));
    if (!valid) {
        s->paintError = ERROR_REVISION_MISMATCH;
        fallback(view, canvas);
        --s->callbacks;
        Queue(s);
        return;
    }
    auto bounds = Read<Rect>(view, abi->bounds);
    float cx = bounds.w * .5f, cy = bounds.h * .5f;
    RoundRect oval{{cx - 6, cy - 6, cx + 6, cy + 6}, {6, 6, 6, 6, 6, 6, 6, 6}, 2};
    Float4 color = w->role == Role::Close      ? Float4{1, .373f, .341f, 1}
                   : w->role == Role::Minimize ? Float4{1, .741f, .18f, 1}
                                               : Float4{.157f, .784f, .251f, 1};
    if (!Read<bool>(view, abi->visible + 1) || !IsWindowEnabled(s->hwnd))
        color = {.5f, .5f, .5f, 1};
    Virtual<int (*)(P)>(recording, 0x20)(recording);
    Virtual<void (*)(P, const RoundRect &, int, bool)>(recording, 0x98)(recording, oval, 1, true);
    Virtual<void (*)(P, Float4, int)>(recording, 0xb8)(recording, color, 3);
    Virtual<void (*)(P)>(recording, 0x50)(recording);
    w->painted = true;
    w->paintedBounds = bounds;
    s->mask |= w->role == Role::Close ? 1 : w->role == Role::Minimize ? 2 : 4;
    ++s->paints;
    --s->callbacks;
}
bool Hooks() {
    std::scoped_lock lock(hooksMutex);
    if (enabled) {
        ++sessions;
        return true;
    }
    if (memcmp(module + abi->boundsFunction, abi->boundsEntry.data(), abi->boundsEntry.size()) ||
        memcmp(module + abi->layoutFunction, abi->layoutEntry.data(), abi->layoutEntrySize))
        return false;
    constexpr BYTE damageEntry[]{0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0xce};
    if (abi->schedulePaint && memcmp(module + abi->schedulePaint, damageEntry, sizeof(damageEntry)))
        return false;
    constexpr BYTE visibleEntry[]{0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
                                  0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x48};
    if (abi->setVisible && memcmp(module + abi->setVisible, visibleEntry, sizeof(visibleEntry)))
        return false;
    auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    if (!originalBounds && MH_CreateHook(module + abi->boundsFunction, reinterpret_cast<void *>(&BoundsHook),
                                         reinterpret_cast<void **>(&originalBounds)) != MH_OK)
        return false;
    if (!originalLayout && MH_CreateHook(module + abi->layoutFunction, reinterpret_cast<void *>(&LayoutHook),
                                         reinterpret_cast<void **>(&originalLayout)) != MH_OK)
        return false;
    if (abi->setVisible && !originalVisible &&
        MH_CreateHook(module + abi->setVisible, reinterpret_cast<void *>(&VisibleHook),
                      reinterpret_cast<void **>(&originalVisible)) != MH_OK)
        return false;
    MH_QueueEnableHook(module + abi->boundsFunction);
    MH_QueueEnableHook(module + abi->layoutFunction);
    if (abi->setVisible)
        MH_QueueEnableHook(module + abi->setVisible);
    if (MH_ApplyQueued() != MH_OK) {
        MH_DisableHook(module + abi->boundsFunction);
        MH_DisableHook(module + abi->layoutFunction);
        if (abi->setVisible)
            MH_DisableHook(module + abi->setVisible);
        return false;
    }
    enabled = true;
    ++sessions;
    return true;
}
void Unhook() {
    std::scoped_lock lock(hooksMutex);
    if (sessions)
        --sessions;
    if (!sessions && enabled) {
        MH_QueueDisableHook(module + abi->boundsFunction);
        MH_QueueDisableHook(module + abi->layoutFunction);
        if (abi->setVisible)
            MH_QueueDisableHook(module + abi->setVisible);
        if (MH_ApplyQueued() == MH_OK)
            enabled = false;
    }
}
struct Selection {
    P container{};
    std::vector<std::pair<size_t, Role>> nodes;
};
Selection Select(HWND hwnd, std::vector<Node> &nodes) {
    POINT origin{};
    ClientToScreen(hwnd, &origin);
    double scale = GetDpiForWindow(hwnd) / 96.;
    for (auto &n : nodes)
        if (n.visible && n.rect.w > 0 && n.rect.w < 160 && n.rect.h >= 16 && n.rect.h <= 64 && n.y >= 0 && n.y < 64) {
            int x = origin.x + int((n.x + n.rect.w * .5) * scale), y = origin.y + int((n.y + n.rect.h * .5) * scale);
            n.hit = int(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(x, y)));
        }
    Selection result;
    size_t groups{};
    for (size_t c = 0; c < nodes.size(); c++)
        if (nodes[c].hit == HTCLOSE) {
            auto &close = nodes[c];
            size_t min = nodes.size(), max = nodes.size();
            unsigned mins{}, maxes{};
            for (size_t i = 0; i < nodes.size(); i++)
                if (nodes[i].parent == close.parent) {
                    if (nodes[i].hit == HTMINBUTTON) {
                        min = i;
                        ++mins;
                    }
                    if (nodes[i].hit == HTMAXBUTTON) {
                        max = i;
                        ++maxes;
                    }
                }
            if (mins != 1 || maxes != 1)
                continue;
            auto parent =
                std::find_if(nodes.begin(), nodes.end(), [&](auto const &n) { return n.view == close.parent; });
            if (parent == nodes.end() || parent->y > 16 || parent->y < 0 || parent->rect.h < 20 || parent->rect.h > 64)
                continue;
            Selection found;
            found.container = parent->view;
            found.nodes.push_back({size_t(parent - nodes.begin()), Role::Container});
            size_t buttons{};
            bool valid = true;
            for (size_t i = 0; i < nodes.size(); i++)
                if (nodes[i].parent == parent->view) {
                    auto &n = nodes[i];
                    if (n.vtable != nodes[max].vtable) {
                        valid = false;
                        break;
                    }
                    found.nodes.push_back({i, i == c ? Role::Close : i == min ? Role::Minimize : Role::Maximize});
                    ++buttons;
                }
            if (!valid || buttons < 3 || buttons > 4)
                continue;
            std::vector<P> regions;
            for (size_t i = 0; i < nodes.size(); i++) {
                auto &n = nodes[i];
                if (!n.visible || n.x >= 72 || n.y < 0 || n.y >= parent->y + parent->rect.h ||
                    n.rect.w <= nodes[0].rect.w / 2 || n.rect.h < 20 || n.rect.h > 64 ||
                    Descendant(parent->view, n.view))
                    continue;
                if (std::any_of(regions.begin(), regions.end(), [&](P region) { return Descendant(n.view, region); }))
                    continue;
                regions.push_back(n.view);
                found.nodes.push_back({i, Role::Region});
            }
            if (regions.empty())
                continue;
            result = std::move(found);
            ++groups;
        }
    return groups == 1 ? result : Selection{};
}
bool RestoreWatches(State *s) {
    std::vector<Node> nodes;
    P root{};
    bool valid = Framework(s->hwnd, root) && root == s->root && Walk(root, nullptr, nodes);
    std::vector<P> layout;
    for (auto &w : s->watches) {
        watched.erase(w->view);
        auto n = std::find_if(nodes.begin(), nodes.end(), [&](auto const &n) { return n.view == w->view; });
        if (valid && n != nodes.end() && n->parent == w->parent && Read<P>(w->view) == w->Table()) {
            if (!w->clone.empty())
                memcpy(w->view, &w->vtable, 8);
            if (Read<Rect>(w->view, abi->bounds) == w->applied)
                originalBounds(w->view, w->original);
            if (w->role == Role::Container || w->role == Role::Region)
                layout.push_back(w->view);
        } else if (!w->clone.empty() && Life(w->view) && Read<P>(w->view) == w->Table()) {
            // A live View may have moved to another Widget. Restore its original
            // virtual table while preserving the new parent's geometry.
            memcpy(w->view, &w->vtable, 8);
        }
        retired.push_back(std::move(w));
    }
    s->watches.clear();
    if (valid) {
        for (auto view : layout)
            if (Life(view) && Descendant(view, root))
                originalLayout(view);
        originalLayout(root);
    }
    return valid;
}
bool Bind(State *s, std::vector<Node> &nodes, Selection const &selection) {
    if (!selection.container)
        return false;
    for (auto [index, role] : selection.nodes) {
        auto &n = nodes[index];
        auto w = std::make_unique<Watch>();
        w->state = s;
        w->view = n.view;
        w->parent = n.parent;
        w->vtable = n.vtable;
        w->role = role;
        w->original = n.rect;
        if (w->Button()) {
            for (size_t i = 0; i < 256; i++) {
                P f = Read<P>(w->vtable, i * 8);
                if (!Code(f))
                    break;
                w->clone.push_back(f);
            }
            if (w->clone.size() <= abi->paint / 8)
                return false;
            w->clone[abi->paint / 8] = reinterpret_cast<P>(&PaintHook);
        }
        s->watches.push_back(std::move(w));
    }
    for (auto &w : s->watches) {
        watched[w->view] = w.get();
        if (w->Button()) {
            paintWatches[w->view] = w.get();
            P table = w->Table();
            memcpy(w->view, &table, 8);
        }
        w->applied = Applied(w.get());
        originalBounds(w->view, w->applied);
        Damage(w.get());
    }
    s->known.clear();
    for (auto const &n : nodes)
        s->known.insert(n.view);
    s->mask = 0;
    ++s->rebinds;
    InvalidateRect(s->hwnd, nullptr, FALSE);
    return true;
}
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void Release(State *s, bool destroying = false) {
    if (s->released)
        return;
    s->changing = true;
    KillTimer(s->hwnd, s->timer);
    if (!destroying)
        RestoreWatches(s);
    else {
        for (auto &w : s->watches) {
            watched.erase(w->view);
            retired.push_back(std::move(w));
        }
        s->watches.clear();
    }
    RemoveWindowSubclass(s->hwnd, Subclass, SubclassId);
    for (auto name : {styler::OwnerProperty, styler::InlineProperty, styler::AdapterProperty})
        RemovePropW(s->hwnd, name);
    std::erase(states, s);
    s->released = true;
    CloseHandle(s->process);
    Unhook();
    if (!destroying)
        InvalidateRect(s->hwnd, nullptr, FALSE);
    // Retired Watch entries refer back to State while returning from callbacks.
}
LRESULT CALLBACK Subclass(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto s = reinterpret_cast<State *>(data);
    if (m == styler::QueryMessage())
        return !s->changing && !s->paintError && s->mask == 7 ? styler::Acknowledged : 0;
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(wp) != s->owner)
            return 0;
        if (s->changing || s->callbacks)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (m == styler::DiagnosticMessage()) {
        int y = 0, h = 0;
        for (auto const &w : s->watches)
            if (w->role == Role::Container && Lookup(w->view)) {
                y = RootY(w->view);
                h = Read<Rect>(w->view, abi->bounds).h;
                break;
            }
        UINT dpi = GetDpiForWindow(hwnd);
        switch (static_cast<styler::Metric>(wp)) {
        case styler::Metric::InlineKind:
            return 9;
        case styler::Metric::InlineLeft:
            return 0;
        case styler::Metric::InlineTop:
            return MulDiv(y, dpi, 96);
        case styler::Metric::InlineWidth:
            return MulDiv(72, dpi, 96);
        case styler::Metric::InlineHeight:
            return MulDiv(h, dpi, 96);
        case styler::Metric::InlineTabsLeft:
            return MulDiv(72, dpi, 96);
        case styler::Metric::InlineOriginalButtonsVisible:
            return s->mask == 7 && !s->paintError ? 0 : 3;
        case styler::Metric::CaptionPaints:
            return s->paints;
        case styler::Metric::Refreshes:
            return s->rebinds;
        default:
            return 0;
        }
    }
    if (m == RebindMessage() || m == styler::RefreshMessage()) {
        s->queued = false;
        if (s->changing || s->callbacks) {
            Queue(s);
            return 0;
        }
        if (s->paintError) {
            Release(s);
            return 0;
        }
        if (IsIconic(hwnd) || !IsWindowVisible(hwnd))
            return 0;
        // Fullscreen intentionally removes the native caption and its Views. Keep
        // the lease dormant while that layout is absent; the caption/style/layout
        // notifications on return will validate and rebind the ordinary title row.
        if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION)
            return 0;
        // Existing geometry is maintained synchronously by the original setter.
        // Rebuild only when a framework object or top-row content actually changes.
        std::vector<Node> live;
        bool stable = Walk(s->root, nullptr, live);
        P container{};
        for (auto const &w : s->watches) {
            stable = stable && Lookup(w->view);
            if (w->role == Role::Container)
                container = w->view;
        }
        if (stable)
            for (auto const &n : live) {
                if (n.visible && n.y >= 0 && n.y < 64 && !s->known.contains(n.view)) {
                    stable = false;
                    break;
                }
                if (n.visible && n.x < 72 && n.y >= 0 && n.y < 40 && n.rect.h >= 20 && n.rect.h <= 64 &&
                    n.rect.w > live[0].rect.w / 2 && !Descendant(container, n.view)) {
                    bool reserved = std::any_of(s->watches.begin(), s->watches.end(), [&](auto const &w) {
                        return w->role == Role::Region && Descendant(n.view, w->view);
                    });
                    if (!reserved) {
                        stable = false;
                        break;
                    }
                }
            }
        if (stable)
            return 0;
        s->changing = true;
        bool valid = RestoreWatches(s);
        std::vector<Node> nodes;
        auto selection = valid && Walk(s->root, nullptr, nodes) ? Select(hwnd, nodes) : Selection{};
        bool bound = Bind(s, nodes, selection);
        s->changing = false;
        if (!bound)
            Release(s);
        return 0;
    }
    if (m == WM_TIMER && wp == s->timer) {
        if (!s->changing && !s->callbacks &&
            (!IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT ||
             reinterpret_cast<P>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) != s->handler))
            Release(s);
        return 0;
    }
    if (m == WM_NCDESTROY) {
        Release(s, true);
        return DefSubclassProc(hwnd, m, wp, lp);
    }
    // Minimum-size measurement reads the same View geometry as layout. Expose
    // original coordinates for this native query as well; otherwise a host can
    // cache our inset as a new minimum width after maximize or repeated resizing.
    if (m == WM_GETMINMAXINFO && !s->changing) {
        BeginGeometry(s);
        auto result = DefSubclassProc(hwnd, m, wp, lp);
        EndGeometry(s);
        return result;
    }
    bool refresh =
        m == WM_SIZE || m == WM_DPICHANGED || m == WM_THEMECHANGED || m == WM_SETTINGCHANGE || m == WM_STYLECHANGED;
    auto result = DefSubclassProc(hwnd, m, wp, lp);
    if (refresh && !s->changing)
        Queue(s);
    return result;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    P root{};
    std::vector<Node> nodes;
    if (!Framework(c->window, root)) {
        Debug("probe: framework declined");
        return ERROR_NOT_SUPPORTED;
    }
    if (!Walk(root, nullptr, nodes)) {
        Debug("probe: tree declined");
        return ERROR_NOT_SUPPORTED;
    }
    if (!Select(c->window, nodes).container) {
        Debug("probe: semantic group declined");
        return ERROR_NOT_SUPPORTED;
    }
    Debug("probe accepted");
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    Debug("attach entered");
    P root{}, handler{};
    std::vector<Node> nodes;
    if (!Framework(c->window, root, &handler) || !Walk(root, nullptr, nodes))
        return ERROR_NOT_SUPPORTED;
    auto selection = Select(c->window, nodes);
    if (!selection.container || GetPropW(c->window, styler::OwnerProperty))
        return ERROR_NOT_SUPPORTED;
    auto s = new State;
    s->hwnd = c->window;
    s->owner = c->owner;
    s->root = root;
    s->handler = handler;
    s->thread = GetCurrentThreadId();
    s->changing = true;
    DWORD pid{};
    GetWindowThreadProcessId(s->owner, &pid);
    s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->process) {
        delete s;
        return ERROR_ACCESS_DENIED;
    }
    if (!Hooks()) {
        CloseHandle(s->process);
        delete s;
        return ERROR_NOT_SUPPORTED;
    }
    states.push_back(s);
    if (!SetWindowSubclass(s->hwnd, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s)) ||
        !Bind(s, nodes, selection)) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    s->changing = false;
    s->timer = SetTimer(s->hwnd, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
    if (!s->timer || !SetPropW(s->hwnd, styler::OwnerProperty, s->owner) ||
        !SetPropW(s->hwnd, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(9))) ||
        !SetPropW(s->hwnd, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId))) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}
