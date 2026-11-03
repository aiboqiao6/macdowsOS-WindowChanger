#include "../../adapter-api.h"
#include "../../pe-identity.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
// Instance-local caption paint and original BCG element rectangle transaction.
// No additional HWND, caption row, external renderer or positioning timer.
constexpr size_t HwndOffset = 0x40, CommandOffset = 0xbc, RectOffset = 0xec, DrawSlot = 118;
constexpr size_t ExpectedDrawRva = 0xa6ec00;
constexpr size_t ClickRva = 0xa6ec50;
constexpr UINT_PTR SubclassId = 0x4d434250;
using DrawFunction = void (*)(void *, void *);

bool Read(const void *address, void *destination, size_t length) {
    SIZE_T read{};
    return address && ReadProcessMemory(GetCurrentProcess(), address, destination, length, &read) && read == length;
}
template <class T> T Value(const void *address) {
    T value{};
    Read(address, &value, sizeof(value));
    return value;
}
bool Executable(const void *address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) && info.State == MEM_COMMIT &&
           !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (info.Protect & 0xf0);
}
struct RuntimeClass {
    const char *name{};
    int size{};
    UINT schema{};
    void *create{};
    RuntimeClass *base{};
};
RuntimeClass *Class(const void *object) {
    auto table = Value<BYTE **>(object);
    auto getter = Value<BYTE *>(table);
    BYTE code[8]{};
    if (!Read(getter, code, sizeof(code)) || memcmp(code, "\x48\x8d\x05", 3) || code[7] != 0xc3)
        return nullptr;
    int displacement{};
    memcpy(&displacement, code + 3, sizeof(displacement));
    return reinterpret_cast<RuntimeClass *>(getter + 7 + displacement);
}
bool Named(RuntimeClass *info, const char *name) {
    for (unsigned depth = 0; info && depth < 20; ++depth) {
        RuntimeClass current{};
        char text[96]{};
        if (!Read(info, &current, sizeof(current)) || !Read(current.name, text, sizeof(text)) || current.size < 8 ||
            current.size > 1024 * 1024)
            return false;
        text[95] = 0;
        if (!strcmp(text, name))
            return true;
        info = current.base;
    }
    return false;
}
BYTE *Image(void *object) {
    HMODULE module{};
    auto table = Value<void **>(object);
    if (!table ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(table), &module))
        return nullptr;
    constexpr GUID id{0x73d590b1, 0x7fa3, 0x4f9d, {0x9c, 0xe2, 0xe2, 0xa6, 0xd8, 0x01, 0xc1, 0x1c}};
    return styler::MatchesPdb(module, id) ? reinterpret_cast<BYTE *>(module) : nullptr;
}
void *RibbonObject(HWND ribbon) {
    BYTE buffer[65536]{};
    uintptr_t address{};
    size_t bytes{};
    auto deadline = GetTickCount64() + 1200;
    while (bytes < 256 * 1024 * 1024 && GetTickCount64() < deadline) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void *>(address), &region, sizeof(region)))
            break;
        auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (end <= address)
            break;
        address = end;
        if (region.State != MEM_COMMIT || region.Type != MEM_PRIVATE ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)))
            continue;
        for (size_t offset = 0; offset < region.RegionSize; offset += sizeof(buffer)) {
            auto size = std::min(sizeof(buffer), region.RegionSize - offset);
            auto start = reinterpret_cast<BYTE *>(region.BaseAddress) + offset;
            if (!Read(start, buffer, size))
                continue;
            bytes += size;
            for (size_t at = 0; at + sizeof(HWND) <= size; at += sizeof(void *)) {
                HWND candidate{};
                memcpy(&candidate, buffer + at, sizeof(candidate));
                if (candidate != ribbon || reinterpret_cast<uintptr_t>(start + at) < HwndOffset)
                    continue;
                auto object = start + at - HwndOffset;
                if (Image(object) && Named(Class(object), "CBCGPRibbonBar"))
                    return object;
            }
        }
    }
    return nullptr;
}
struct Button {
    BYTE *object{};
    void **original{};
    std::vector<void *> table;
    UINT command{};
    RECT rect{};
    unsigned paints{};
};
struct Geometry {
    BYTE *object{};
    RECT original{}, applied{};
    int commandIndex = -1;
};
struct State {
    BYTE *image{};
    HWND window{}, ribbon{}, owner{};
    DWORD pid{};
    HANDLE process{};
    std::array<Button, 3> buttons;
    std::vector<Geometry> geometry;
    bool active{};
    bool translated{};
    unsigned layoutDepth{};
    int pressed = -1;
    unsigned depth{}, dcOffset{};
};
thread_local std::vector<std::unique_ptr<State>> leases;

void OriginalGeometry(State *state) {
    if (!state->translated)
        return;
    state->translated = false;
    for (auto &item : state->geometry) {
        auto current = Value<RECT>(item.object + RectOffset);
        if (EqualRect(&current, &item.applied))
            memcpy(item.object + RectOffset, &item.original, sizeof(RECT));
        else
            item.original = current;
    }
}
void TranslateGeometry(State *state) {
    if (!state->active || state->translated)
        return;
    auto dpi = GetDpiForWindow(state->window);
    RECT caption = Value<RECT>(state->buttons[0].object + RectOffset);
    for (auto &item : state->geometry) {
        item.original = Value<RECT>(item.object + RectOffset);
        item.applied = item.original;
        if (IsRectEmpty(&item.original))
            continue;
        if (item.commandIndex >= 0) {
            item.applied = {MulDiv(8 + 20 * item.commandIndex, dpi, 96), caption.top,
                            MulDiv(28 + 20 * item.commandIndex, dpi, 96), caption.bottom};
        } else {
            OffsetRect(&item.applied, MulDiv(84, dpi, 96), 0);
        }
        memcpy(item.object + RectOffset, &item.applied, sizeof(RECT));
    }
    state->translated = true;
}

void CollectGeometry(State *state, BYTE *ribbon, int size) {
    std::vector<BYTE *> queue{ribbon}, visited;
    RECT caption = state->buttons[0].rect;
    RECT client{};
    GetClientRect(state->ribbon, &client);
    auto add = [&](BYTE *object) {
        if (!object || std::find(visited.begin(), visited.end(), object) != visited.end() ||
            std::find(queue.begin(), queue.end(), object) != queue.end())
            return;
        auto info = Class(object);
        if (Named(info, "CBCGPBaseRibbonElement") || Named(info, "CBCGPRibbonCategory"))
            queue.push_back(object);
    };
    while (!queue.empty() && visited.size() < 192) {
        auto object = queue.back();
        queue.pop_back();
        visited.push_back(object);
        auto info = Class(object);
        RuntimeClass runtime{};
        if (!Read(info, &runtime, sizeof(runtime)))
            continue;
        bool element = Named(info, "CBCGPBaseRibbonElement");
        if (element) {
            auto rect = Value<RECT>(object + RectOffset);
            if (!IsRectEmpty(&rect) && rect.top >= caption.top && rect.bottom <= caption.bottom &&
                rect.left < client.right / 2 && !Named(info, "CBCGPRibbonCaptionButton"))
                state->geometry.push_back({object, rect, rect, -1});
        }
        auto extent = object == ribbon ? size : std::min(runtime.size, 4096);
        for (int offset = sizeof(void *); offset + sizeof(void *) <= static_cast<size_t>(extent);
             offset += sizeof(void *)) {
            auto pointer = Value<BYTE *>(object + offset);
            if (reinterpret_cast<uintptr_t>(pointer) < 65536)
                continue;
            add(object + offset);
            add(pointer);
            if (!Image(pointer))
                for (unsigned at = 0; at < 16; ++at)
                    add(Value<BYTE *>(pointer + at * sizeof(void *)));
        }
    }
    for (size_t i = 0; i < 3; ++i)
        state->geometry.push_back(
            {state->buttons[i].object, state->buttons[i].rect, state->buttons[i].rect, static_cast<int>(i)});
}

HDC DrawingDC(void *context, unsigned &offset) {
    auto info = Class(context);
    if (!Named(info, "CDC"))
        return nullptr;
    // m_hDC is the first valid DC in the public CDC portion. Discover it from
    // the verified runtime class; do not use a host or window userdata offset.
    for (unsigned at = sizeof(void *); at < 96; at += sizeof(void *)) {
        auto dc = Value<HDC>(reinterpret_cast<BYTE *>(context) + at);
        auto type = GetObjectType(dc);
        if (type == OBJ_DC || type == OBJ_MEMDC || type == OBJ_ENHMETADC) {
            offset = at;
            return dc;
        }
    }
    return nullptr;
}
void Draw(void *object, void *context) {
    State *state{};
    Button *button{};
    for (auto it = leases.rbegin(); it != leases.rend(); ++it) {
        for (auto &item : (*it)->buttons)
            if (item.object == object) {
                state = it->get();
                button = &item;
                break;
            }
        if (button)
            break;
    }
    if (!state || !button)
        return;
    auto original = reinterpret_cast<DrawFunction>(button->original[DrawSlot]);
    auto dc = state->active ? DrawingDC(context, state->dcOffset) : nullptr;
    RECT rect{};
    if (!dc || !Read(static_cast<BYTE *>(object) + RectOffset, &rect, sizeof(rect)) || IsRectEmpty(&rect)) {
        original(object, context);
        return;
    }
    auto saved = SaveDC(dc);
    auto color = button->command == SC_CLOSE      ? RGB(255, 95, 87)
                 : button->command == SC_MINIMIZE ? RGB(255, 189, 46)
                                                  : RGB(40, 200, 64);
    auto brush = CreateSolidBrush(color);
    if (!saved || !brush) {
        if (saved)
            RestoreDC(dc, saved);
        if (brush)
            DeleteObject(brush);
        original(object, context);
        return;
    }
    ++state->depth;
    SelectObject(dc, brush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    int radius = MulDiv(6, GetDpiForWindow(state->window), 96);
    int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
    Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
    RestoreDC(dc, saved);
    DeleteObject(brush);
    ++button->paints;
    --state->depth;
}
LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data);
void Release(State *state) {
    if (!state->active)
        return;
    OriginalGeometry(state);
    state->active = false;
    state->pressed = -1;
    if (GetCapture() == state->window || GetCapture() == state->ribbon)
        ReleaseCapture();
    for (auto &button : state->buttons)
        if (Value<void **>(button.object) == button.table.data() + 1)
            *reinterpret_cast<void ***>(button.object) = button.original;
    KillTimer(state->window, SubclassId);
    RemoveWindowSubclass(state->window, Subclass, SubclassId);
    RemoveWindowSubclass(state->ribbon, Subclass, SubclassId);
    RemovePropW(state->window, styler::OwnerProperty);
    RemovePropW(state->window, styler::InlineProperty);
    RemovePropW(state->window, styler::AdapterProperty);
    if (state->process) {
        CloseHandle(state->process);
        state->process = nullptr;
    }
    RedrawWindow(state->window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    // Keep retired instance tables until the GUI thread exits. A third-party
    // wrapper may have retained a pointer to one; never unload callback code.
}
UINT MetricsMessage() {
    return RegisterWindowMessageW(L"Macdows.MfcPaintProbe.Metrics.v1");
}
int Hit(State *state, POINT point) {
    if (!state->translated || !ScreenToClient(state->ribbon, &point))
        return -1;
    for (int i = 0; i < 3; ++i) {
        auto rect = Value<RECT>(state->buttons[i].object + RectOffset);
        if (PtInRect(&rect, point))
            return i;
    }
    return -1;
}
LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto state = reinterpret_cast<State *>(data);
    if (message == WM_NCHITTEST && Hit(state, {GET_X_LPARAM(l), GET_Y_LPARAM(l)}) >= 0)
        return HTCLIENT;
    if (window == state->window &&
        (message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE || message == WM_LBUTTONDOWN ||
         message == WM_NCLBUTTONDOWN || message == WM_LBUTTONUP || message == WM_NCLBUTTONUP)) {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
            ClientToScreen(window, &point);
        auto hit = Hit(state, point);
        if (hit >= 0 || state->pressed >= 0) {
            ScreenToClient(state->ribbon, &point);
            auto coordinates = MAKELPARAM(point.x, point.y);
            if (message == WM_LBUTTONDOWN || message == WM_NCLBUTTONDOWN) {
                state->pressed = hit;
                SendMessageW(state->ribbon, WM_MOUSEMOVE, 0, coordinates);
                SendMessageW(state->ribbon, WM_LBUTTONDOWN, MK_LBUTTON, coordinates);
            } else if (message == WM_LBUTTONUP || message == WM_NCLBUTTONUP) {
                state->pressed = -1;
                SendMessageW(state->ribbon, WM_LBUTTONUP, 0, coordinates);
            } else {
                SendMessageW(state->ribbon, WM_MOUSEMOVE, state->pressed >= 0 ? MK_LBUTTON : 0, coordinates);
            }
            return 0;
        }
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE)
        state->pressed = -1;
    if (message == styler::QueryMessage())
        return state->active ? styler::Acknowledged : 0;
    if (message == MetricsMessage()) {
        if (w < 3)
            return state->buttons[w].paints;
        if (w == 3)
            return state->dcOffset;
        if (w >= 10 && w < 16) {
            auto rect = Value<RECT>(state->buttons[(w - 10) / 2].object + RectOffset);
            POINT point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
            ClientToScreen(state->ribbon, &point);
            return (w - 10) % 2 ? point.y : point.x;
        }
        if (w == 4)
            return state->geometry.size();
        if (w == 5)
            return reinterpret_cast<LRESULT>(state->ribbon);
        return 0;
    }
    if (message == styler::DetachMessage() && reinterpret_cast<HWND>(w) == state->owner) {
        if (state->depth)
            return styler::TransitionBusy;
        Release(state);
        return styler::Acknowledged;
    }
    if ((message == WM_TIMER && w == SubclassId &&
         (!IsWindow(state->owner) || GetWindowThreadProcessId(state->owner, nullptr) == 0 ||
          WaitForSingleObject(state->process, 0) != WAIT_TIMEOUT)) ||
        message == WM_NCDESTROY) {
        Release(state);
        return DefSubclassProc(window, message, w, l);
    }
    bool layout = message == WM_SIZE || message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED ||
                  message == WM_GETMINMAXINFO || message == WM_STYLECHANGED || message == WM_DPICHANGED ||
                  message == WM_THEMECHANGED || message == WM_SETTINGCHANGE;
    if (layout && !state->layoutDepth++)
        OriginalGeometry(state);
    if (message == WM_PAINT && !state->layoutDepth)
        TranslateGeometry(state);
    auto result = DefSubclassProc(window, message, w, l);
    if (layout && !--state->layoutDepth)
        TranslateGeometry(state);
    return result;
}
std::unique_ptr<State> Find(HWND window) {
    if (IsIconic(window))
        return {};
    HWND ribbon{};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM data) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(child, name, 128);
            if (wcsncmp(name, L"BCGPRibbonBar:", 14) == 0 && IsWindowVisible(child)) {
                *reinterpret_cast<HWND *>(data) = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ribbon));
    if (!ribbon || GetWindowThreadProcessId(ribbon, nullptr) != GetCurrentThreadId())
        return {};
    auto object = static_cast<BYTE *>(RibbonObject(ribbon));
    auto image = Image(object);
    RuntimeClass info{};
    if (!image || !Read(Class(object), &info, sizeof(info)) || info.size < 256 || info.size > 65536)
        return {};
    constexpr BYTE clickEntry[]{0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01};
    BYTE actual[sizeof(clickEntry)]{};
    if (!Read(image + ClickRva, actual, sizeof(actual)) || memcmp(actual, clickEntry, sizeof(actual)))
        return {};
    auto result = std::make_unique<State>();
    result->image = image;
    result->window = window;
    result->ribbon = ribbon;
    unsigned found{};
    for (int offset = 0; offset + 0x3a8 <= info.size; offset += sizeof(void *)) {
        auto candidate = object + offset;
        if (!Named(Class(candidate), "CBCGPRibbonCaptionButton"))
            continue;
        auto command = Value<UINT>(candidate + CommandOffset);
        size_t index = command == SC_CLOSE ? 0 : command == SC_MINIMIZE ? 1 : command == SC_MAXIMIZE ? 2 : 3;
        RECT rect{};
        if (index == 3 || !Read(candidate + RectOffset, &rect, sizeof(rect)) || IsRectEmpty(&rect))
            continue;
        auto table = Value<void **>(candidate);
        if (Value<void *>(table + DrawSlot) != image + ExpectedDrawRva || result->buttons[index].object)
            return {};
        size_t count{};
        while (count < 512 && Executable(Value<void *>(table + count)))
            ++count;
        if (count <= DrawSlot || count == 512)
            return {};
        auto &button = result->buttons[index];
        button.object = candidate;
        button.original = table;
        button.command = command;
        button.rect = rect;
        button.table.resize(count + 1);
        if (!Read(table - 1, button.table.data(), (count + 1) * sizeof(void *)))
            return {};
        button.table[DrawSlot + 1] = reinterpret_cast<void *>(&Draw);
        ++found;
    }
    if (found != 3)
        return {};
    CollectGeometry(result.get(), object, info.size);
    return result->geometry.size() > 3 ? std::move(result) : nullptr;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId())
        return ERROR_INVALID_PARAMETER;
    return Find(context->window) ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId())
        return ERROR_INVALID_PARAMETER;
    auto state = Find(context->window);
    if (!state)
        return ERROR_NOT_SUPPORTED;
    state->owner = context->owner;
    GetWindowThreadProcessId(state->owner, &state->pid);
    state->process = OpenProcess(SYNCHRONIZE, FALSE, state->pid);
    if (!state->process)
        return GetLastError();
    auto pointer = state.get();
    if (!SetWindowSubclass(state->window, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(pointer)) ||
        !SetWindowSubclass(state->ribbon, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(pointer))) {
        RemoveWindowSubclass(state->window, Subclass, SubclassId);
        CloseHandle(state->process);
        return ERROR_GEN_FAILURE;
    }
    state->active = true;
    for (auto &button : state->buttons)
        *reinterpret_cast<void ***>(button.object) = button.table.data() + 1;
    leases.push_back(std::move(state));
    if (!SetPropW(pointer->window, styler::OwnerProperty, pointer->owner) ||
        !SetPropW(pointer->window, styler::InlineProperty, reinterpret_cast<HANDLE>(16)) ||
        !SetTimer(pointer->window, SubclassId, 500, nullptr)) {
        Release(pointer);
        return ERROR_GEN_FAILURE;
    }
    TranslateGeometry(pointer);
    RedrawWindow(pointer->window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return ERROR_SUCCESS;
}
