#include "../../adapter-api.h"
#include "../../pe-identity.h"
#include "txgui-events.h"
#include "txgui-caption.h"
#include "txgui-tree-budget.h"
#include <oleauto.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <array>
#include <map>
#include <windowsx.h>
#ifdef TXGUI_PAINT_OBSERVATION
#include "txgui-paint-observer.h"
#endif
#if defined(TXGUI_ACTION_EXPERIMENT) || defined(TXGUI_PAINT_OBSERVATION) || defined(TXGUI_D2D_DRAWING) ||              \
    defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT) || defined(TXGUI_LEFT_EXPERIMENT)
#define TXGUI_CAPTION_DISCOVERY
#endif

namespace {
using Microsoft::WRL::ComPtr;
thread_local std::ofstream output;
thread_local txtree::Budget treeBudget;
using FrameCount = HRESULT(__cdecl *)(IUnknown *, unsigned *);
using FrameAt = HRESULT(__cdecl *)(IUnknown *, int, IUnknown **);
thread_local BYTE *gfImage{};
#ifdef TXGUI_CAPTION_DISCOVERY
struct CaptionGroup {
    HWND window{};
    ComPtr<IUnknown> root, parent;
    std::array<ComPtr<IUnknown>, 3> buttons;
    bool ambiguous{};
    bool Ready() const {
        return window && root && parent && !ambiguous && buttons[0] && buttons[1] && buttons[2];
    }
};
thread_local CaptionGroup captionGroup;
thread_local std::map<uintptr_t, ComPtr<IUnknown>> buttonParents;
#endif
#if defined(TXGUI_PAINT_OBSERVATION) || defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT)
std::map<uintptr_t, ComPtr<IUnknown>> paintParents;
#endif

std::string Utf8(const wchar_t *text, int length) {
    if (!text || length <= 0)
        return {};
    auto size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}
std::string Text(BSTR value) {
    auto text = Utf8(value, value ? static_cast<int>(SysStringLen(value)) : 0);
    std::string escaped = "\"";
    for (unsigned char character : text) {
        if (character == '\\' || character == '"')
            escaped += '\\';
        if (character >= 32)
            escaped += static_cast<char>(character);
    }
    return escaped + '"';
}
std::string StaticText(const wchar_t *value) {
    if (!value)
        return "null";
    size_t length{};
    while (length < 128 && value[length])
        ++length;
    auto copy = SysAllocStringLen(value, static_cast<UINT>(length));
    auto result = Text(copy);
    SysFreeString(copy);
    return result;
}
std::string Guid(REFGUID value) {
    wchar_t text[40]{};
    StringFromGUID2(value, text, 40);
    return Utf8(text, static_cast<int>(wcslen(text)));
}
bool Readable(const void *address, size_t length) {
    MEMORY_BASIC_INFORMATION info{};
    auto begin = reinterpret_cast<uintptr_t>(address);
    return address && VirtualQuery(address, &info, sizeof(info)) && info.State == MEM_COMMIT &&
           !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && begin + length >= begin &&
           begin + length <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}
template <class Function> Function Slot(IUnknown *object, size_t slot) {
    if (!Readable(object, sizeof(void *)))
        return nullptr;
    auto table = *reinterpret_cast<void ***>(object);
    if (!Readable(table, (slot + 1) * sizeof(void *)))
        return nullptr;
    return reinterpret_cast<Function>(table[slot]);
}

void TypeInfo(ITypeInfo *info, unsigned depth) {
    if (depth > 8)
        return;
    TYPEATTR *attributes{};
    if (FAILED(info->GetTypeAttr(&attributes)))
        return;
    auto funcs = attributes->cFuncs;
    auto parents = attributes->cImplTypes;
    auto guid = attributes->guid;
    info->ReleaseTypeAttr(attributes);
    BSTR name{};
    info->GetDocumentation(MEMBERID_NIL, &name, nullptr, nullptr, nullptr);
    output << "{\"name\":" << Text(name) << ",\"iid\":\"" << Guid(guid) << "\",\"members\":[";
    SysFreeString(name);
    bool first = true;
    for (unsigned i = 0; i < funcs && i < 512; ++i) {
        FUNCDESC *function{};
        if (FAILED(info->GetFuncDesc(i, &function)))
            continue;
        if (!first)
            output << ',';
        first = false;
        BSTR names[16]{};
        UINT count{};
        info->GetNames(function->memid, names, 16, &count);
        output << "{\"dispid\":" << function->memid << ",\"invoke\":" << function->invkind
               << ",\"vtableOffset\":" << function->oVft << ",\"returnVt\":" << function->elemdescFunc.tdesc.vt
               << ",\"names\":[";
        for (UINT n = 0; n < count; ++n) {
            if (n)
                output << ',';
            output << Text(names[n]);
            SysFreeString(names[n]);
        }
        output << "],\"parameters\":[";
        for (int n = 0; n < function->cParams; ++n) {
            if (n)
                output << ',';
            auto &parameter = function->lprgelemdescParam[n];
            output << "{\"vt\":" << parameter.tdesc.vt << ",\"flags\":" << parameter.paramdesc.wParamFlags;
            if (parameter.tdesc.vt == VT_PTR)
                output << ",\"pointsTo\":" << parameter.tdesc.lptdesc->vt;
            output << '}';
        }
        output << "]}";
        info->ReleaseFuncDesc(function);
    }
    output << "],\"parents\":[";
    bool firstParent = true;
    for (unsigned p = 0; p < parents && p < 16; ++p) {
        HREFTYPE reference{};
        ComPtr<ITypeInfo> parent;
        if (SUCCEEDED(info->GetRefTypeOfImplType(p, &reference)) &&
            SUCCEEDED(info->GetRefTypeInfo(reference, &parent))) {
            if (!firstParent)
                output << ',';
            firstParent = false;
            TypeInfo(parent.Get(), depth + 1);
        }
    }
    output << "]}";
}

void Tree(IUnknown *frame, FrameCount countFrames, FrameAt frameAt, unsigned depth, IUnknown *parent = nullptr) {
    if (!treeBudget.Enter(reinterpret_cast<uintptr_t>(frame), depth)) {
        output << "null";
        return;
    }
    unsigned count{};
    auto countResult = countFrames(frame, &count);
    output << "{\"childrenHresult\":" << static_cast<unsigned long>(countResult) << ",\"childCount\":" << count;
    using TypeName = const wchar_t *(__cdecl *)(IUnknown *);
    auto type = reinterpret_cast<TypeName>(gfImage + 0x1b0600)(frame);
    output << ",\"frameworkType\":" << StaticText(type);
    ComPtr<IUnknown> identity;
    frame->QueryInterface(IID_PPV_ARGS(&identity));
    output << ",\"identity\":" << reinterpret_cast<uintptr_t>(identity.Get());
#if defined(TXGUI_PAINT_OBSERVATION) || defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT)
    if (parent && identity) {
        ComPtr<IUnknown> canonical;
        parent->QueryInterface(IID_PPV_ARGS(&canonical));
        paintParents[reinterpret_cast<uintptr_t>(identity.Get())] = canonical;
    }
#endif
    if (type && !wcscmp(type, L"IGFButton")) {
#ifdef TXGUI_CAPTION_DISCOVERY
        if (parent) {
            ComPtr<IUnknown> canonical;
            parent->QueryInterface(IID_PPV_ARGS(&canonical));
            buttonParents[reinterpret_cast<uintptr_t>(identity.Get())] = canonical;
        }
#endif
        auto events = txevents::Inspect(frame, gfImage);
        auto second = txevents::Inspect(frame, gfImage);
        bool stable = second.profile == events.profile && second.complete == events.complete &&
                      second.errors == events.errors && second.numeric == events.numeric &&
                      second.named == events.named && second.listeners == events.listeners;
        output << ",\"eventBindings\":{\"profileVerified\":" << (events.profile ? "true" : "false")
               << ",\"complete\":" << (events.complete ? "true" : "false") << ",\"errors\":" << events.errors
               << ",\"stableRead\":" << (stable ? "true" : "false");
        auto write = [&](const char *name, const std::vector<txevents::Binding> &bindings) {
            output << ",\"" << name << "\":[";
            bool first = true;
            for (const auto &binding : bindings) {
                if (!first)
                    output << ',';
                first = false;
                std::string escaped;
                for (auto c : binding.name) {
                    if (c == '"' || c == '\\')
                        escaped += '\\';
                    escaped += c;
                }
                output << "{\"id\":" << binding.id << ",\"eventName\":\"" << escaped << "\",\"sink\":" << binding.sink
                       << ",\"vtable\":" << binding.table << ",\"numericCallback\":" << binding.numericCallback
                       << ",\"namedCallback\":" << binding.namedCallback << '}';
            }
            output << ']';
        };
        write("numeric", events.numeric);
        write("named", events.named);
        write("listeners", events.listeners);
        output << '}';
    }
    using ReadRect = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, RECT *);
    for (const auto &property :
         {std::pair{"rect", 0x1e0u}, {"screenRect", 0x254u}, {"padding", 0x210u}, {"margin", 0x208u}}) {
        auto getter = Slot<ReadRect>(frame, property.second / sizeof(void *));
        RECT rect{};
        if (getter && SUCCEEDED(getter(frame, &rect)))
            output << ",\"" << property.first << "\":[" << rect.left << ',' << rect.top << ',' << rect.right << ','
                   << rect.bottom << ']';
    }
    using ReadBool = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BOOL *);
    for (const auto &property : {std::pair{"hidden", 0x158u}, {"visible", 0x160u}, {"enabled", 0x168u}}) {
        auto getter = Slot<ReadBool>(frame, property.second / sizeof(void *));
        BOOL value{};
        if (getter && SUCCEEDED(getter(frame, &value)))
            output << ",\"" << property.first << "\":" << (value ? "true" : "false");
    }
    if (type && !wcscmp(type, L"IGFSysButton") && Readable(frame, 0x188) &&
        *reinterpret_cast<BYTE ***>(frame) == reinterpret_cast<BYTE **>(gfImage + 0x201e5c)) {
        bool eventProfile = !memcmp(gfImage + 0x20263c, "onMinimize", 11) &&
                            !memcmp(gfImage + 0x202648, "onRestore", 10) &&
                            !memcmp(gfImage + 0x202654, "onMaximize", 11) && !memcmp(gfImage + 0x202660, "onClose", 8);
        output << ",\"originalEventProfileVerified\":" << (eventProfile ? "true" : "false");
        using GetMask = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, unsigned *);
        auto getter = Slot<GetMask>(frame, 228);
        unsigned mask{};
        if (getter && SUCCEEDED(getter(frame, &mask)))
            output << ",\"nativeSystemButtonMask\":" << mask;
        output << ",\"originalSystemButtons\":[";
        const char *roles[]{"minimize", "maximizeOrRestore", "close"};
        for (unsigned i = 0; i < 3; ++i) {
            if (i)
                output << ',';
            IUnknown *button{};
            SIZE_T read{};
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<BYTE *>(frame) + 0x17c + i * 4, &button,
                              sizeof(button), &read);
            output << "{\"role\":\"" << roles[i] << "\",\"bound\":" << (button ? "true" : "false");
            if (eventProfile) {
                if (i == 0)
                    output << ",\"eventIds\":[101],\"events\":[\"onMinimize\"]";
                if (i == 1)
                    output << ",\"eventIds\":[102,104],\"events\":[\"onMaximize\",\"onRestore\"]";
                if (i == 2)
                    output << ",\"eventIds\":[103],\"events\":[\"onClose\"]";
            }
            if (button && Readable(button, sizeof(void *))) {
                ComPtr<IUnknown> canonical;
                button->QueryInterface(IID_PPV_ARGS(&canonical));
                output << ",\"identity\":" << reinterpret_cast<uintptr_t>(canonical.Get());
                for (auto property : {std::pair{"hidden", 0x158u}, {"visible", 0x160u}, {"enabled", 0x168u}}) {
                    auto readBool = Slot<ReadBool>(button, property.second / sizeof(void *));
                    BOOL value{};
                    if (readBool && SUCCEEDED(readBool(button, &value)))
                        output << ",\"" << property.first << "\":" << (value ? "true" : "false");
                }
            }
            output << '}';
        }
        output << ']';
    }
    ComPtr<IAccessible> accessible;
    if (type && !wcscmp(type, L"IGFStackPanel") &&
        *reinterpret_cast<void ***>(frame) == reinterpret_cast<void **>(gfImage + 0x220f58)) {
        using GetInt = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int *);
        using Item = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, int, IUnknown **);
        auto get = Slot<GetInt>(frame, 0x30c / 4);
        auto item = Slot<Item>(frame, 0x32c / 4);
        int orientation{};
        bool complete = get == reinterpret_cast<GetInt>(gfImage + 0x93b00) &&
                        item == reinterpret_cast<Item>(gfImage + 0x17cc30) && SUCCEEDED(get(frame, &orientation)) &&
                        SUCCEEDED(countResult) && count <= treeBudget.maxChildren;
        output << ",\"layoutOrientation\":" << orientation << ",\"layoutOrder\":[";
        if (complete) {
            for (unsigned i = 0; i < count; ++i) {
                if (i)
                    output << ',';
                ComPtr<IUnknown> child, canonical;
                bool found = SUCCEEDED(item(frame, static_cast<int>(i), &child)) && child &&
                             SUCCEEDED(child->QueryInterface(IID_PPV_ARGS(&canonical))) && canonical;
                complete &= found;
                output << (found ? reinterpret_cast<uintptr_t>(canonical.Get()) : 0);
            }
        }
        output << "],\"layoutItemsComplete\":" << (complete ? "true" : "false");
    }
    if (SUCCEEDED(frame->QueryInterface(IID_PPV_ARGS(&accessible)))) {
        VARIANT self{}, role{}, state{};
        self.vt = VT_I4;
        self.lVal = CHILDID_SELF;
        accessible->get_accRole(self, &role);
        accessible->get_accState(self, &state);
        LONG x{}, y{}, width{}, height{};
        auto locationResult = accessible->accLocation(&x, &y, &width, &height, self);
        output << ",\"accessible\":true,\"role\":" << (role.vt == VT_I4 ? role.lVal : -1)
               << ",\"state\":" << (state.vt == VT_I4 ? state.lVal : -1)
               << ",\"locationHresult\":" << static_cast<unsigned long>(locationResult) << ",\"rect\":[" << x << ','
               << y << ',' << width << ',' << height << ']';
        VariantClear(&role);
        VariantClear(&state);
    }
    output << ",\"children\":[";
    bool first = true;
    if (treeBudget.Children(SUCCEEDED(countResult), count))
        for (unsigned i = 0; i < count; ++i) {
            if (!treeBudget.Next())
                break;
            ComPtr<IUnknown> child;
            if (FAILED(frameAt(frame, static_cast<int>(i), &child)) || !child) {
                treeBudget.ChildFailure();
                continue;
            }
            if (!first)
                output << ',';
            first = false;
            Tree(child.Get(), countFrames, frameAt, depth + 1, frame);
        }
    output << "]}";
}

void CaptionHits(HWND window, IUnknown *root) {
    // TopFrameInfoFromPoint's verified implementation calls this read-only
    // screen-point helper before formatting its private tagFrameInfo structure.
    // Use the returned real frame interface, never guess that private struct.
    constexpr BYTE entry[]{0x55, 0x8b, 0xec, 0x5d, 0xe9, 0x57, 0x29, 0x00, 0x00};
    bool profile = !memcmp(gfImage + 0x66e20, entry, sizeof(entry));
    output << ",\"captionHitProfileVerified\":" << (profile ? "true" : "false") << ",\"captionHitSamples\":[";
    RECT client{};
    if (profile && !IsIconic(window) && GetClientRect(window, &client) && client.right > 0) {
        using Hit = HRESULT(__cdecl *)(IUnknown *, const POINT *, IUnknown **);
        auto hit = reinterpret_cast<Hit>(gfImage + 0x66e20);
        bool first = true;
        auto dpi = GetDpiForWindow(window);
        for (int y : {12, 28, 44})
            for (int fromRight : {12, 36, 60, 84, 108, 132, 156}) {
                POINT point{client.right - MulDiv(fromRight, dpi, 96), MulDiv(y, dpi, 96)};
                if (!PtInRect(&client, point) || !ClientToScreen(window, &point))
                    continue;
                ComPtr<IUnknown> found;
                auto result = hit(root, &point, &found);
                if (!first)
                    output << ',';
                first = false;
                output << "{\"screenPoint\":[" << point.x << ',' << point.y
                       << "],\"hresult\":" << static_cast<unsigned long>(result);
                if (SUCCEEDED(result) && found) {
                    ComPtr<IUnknown> identity;
                    found.As(&identity);
                    auto type =
                        reinterpret_cast<const wchar_t *(__cdecl *)(IUnknown *)>(gfImage + 0x1b0600)(found.Get());
                    output << ",\"identity\":" << reinterpret_cast<uintptr_t>(identity.Get())
                           << ",\"frameworkType\":" << StaticText(type);
                    if (y == 12 && type && !wcscmp(type, L"IGFButton")) {
                        using Getter = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, BSTR *);
                        auto getter = Slot<Getter>(found.Get(), 0x1bc / 4);
                        bool known =
                            *reinterpret_cast<BYTE ***>(found.Get()) == reinterpret_cast<BYTE **>(gfImage + 0x1f4c00) &&
                            getter == reinterpret_cast<Getter>(gfImage + 0x3690);
                        output << ",\"tooltipProfileVerified\":" << (known ? "true" : "false");
                        if (known) {
                            BSTR value{};
                            auto hr = getter(found.Get(), &value);
                            output << ",\"tooltipHresult\":" << static_cast<unsigned long>(hr)
                                   << ",\"toolTip\":" << Text(value);
                            auto intent =
                                hr == S_OK ? txcaption::Hint(value, SysStringLen(value)) : txcaption::Intent::Unknown;
                            output << ",\"tooltipIntent\":\"" << txcaption::Name(intent) << '"';
#ifdef TXGUI_CAPTION_DISCOVERY
                            if (intent != txcaption::Intent::Unknown) {
                                auto index = static_cast<unsigned>(intent);
                                auto &selected = captionGroup.buttons[index];
                                if (selected && selected.Get() != identity.Get())
                                    captionGroup.ambiguous = true;
                                else
                                    selected = identity;
                            }
#endif
                            SysFreeString(value);
                        }
                    }
                }
                output << '}';
            }
    }
    output << ']';
#ifdef TXGUI_CAPTION_DISCOVERY
    for (auto &button : captionGroup.buttons)
        if (button) {
            auto it = buttonParents.find(reinterpret_cast<uintptr_t>(button.Get()));
            if (it == buttonParents.end() || !it->second ||
                (captionGroup.parent && captionGroup.parent.Get() != it->second.Get()))
                captionGroup.ambiguous = true;
            else
                captionGroup.parent = it->second;
        }
    buttonParents.clear();
#endif
}

HRESULT Inspect(HWND window) {
    auto gf = GetModuleHandleW(L"GF.dll");
    constexpr GUID identity{0xd262c6b9, 0x151b, 0x4748, {0x85, 0xa4, 0xaf, 0x86, 0x94, 0x3c, 0xfc, 0x63}};
    if (!styler::MatchesPdb(gf, identity, 24))
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    gfImage = reinterpret_cast<BYTE *>(gf);
    auto common = GetModuleHandleW(L"Common.dll");
    auto getCore = reinterpret_cast<BOOL(__cdecl *)(IUnknown **)>(
        common ? GetProcAddress(common, "?GetPlatformCore@Core@Util@@YAHPAPAUITXCore@@@Z") : nullptr);
    // The exact verified GF export calls this same import, then Core slot 8,
    // then the returned window service's slot 9. Check every HRESULT/null here;
    // unlike GFSpy's wrapper, a missing service must not be dereferenced.
    if (!getCore ||
        *reinterpret_cast<void **>(reinterpret_cast<BYTE *>(gf) + 0x1e82b4) != reinterpret_cast<void *>(getCore))
        return E_NOINTERFACE;
    ComPtr<IUnknown> core;
    getCore(&core);
    if (!core)
        return E_NOINTERFACE;
    constexpr GUID windowService{0x6c1f3073, 0x19c0, 0x47f5, {0xaa, 0xc8, 0x8f, 0x30, 0x38, 0x1d, 0x1b, 0x73}};
    using GetService = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, REFGUID, IUnknown **);
    auto getService = Slot<GetService>(core.Get(), 8);
    ComPtr<IUnknown> service;
    if (!getService)
        return E_NOINTERFACE;
    auto result = getService(core.Get(), windowService, &service);
    if (FAILED(result) || !service)
        return FAILED(result) ? result : E_NOINTERFACE;
    using GetWindow = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, HWND, IUnknown **);
    auto getWindow = Slot<GetWindow>(service.Get(), 9);
    ComPtr<IUnknown> popup;
    if (!getWindow)
        return E_NOINTERFACE;
    result = getWindow(service.Get(), window, &popup);
    if (FAILED(result) || !popup)
        return FAILED(result) ? result : E_NOINTERFACE;
#ifdef TXGUI_CAPTION_DISCOVERY
    captionGroup.window = window;
    captionGroup.root = popup;
#endif
    output << ",\"rootFound\":true";
    ComPtr<IDispatch> dispatch;
    if (FAILED(popup.As(&dispatch)))
        return E_NOINTERFACE;
    ComPtr<ITypeInfo> info;
    result = dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &info);
    output << ",\"schemaHresult\":" << static_cast<unsigned long>(result);
    if (SUCCEEDED(result) && info) {
        output << ",\"rootSchema\":";
        TypeInfo(info.Get(), 0);
    }
    auto count =
        reinterpret_cast<FrameCount>(GetProcAddress(gf, "?GetFrameCount@GFSpyFuncHelper@@YAJPAUIGFFrame@@PAI@Z"));
    auto at =
        reinterpret_cast<FrameAt>(GetProcAddress(gf, "?GetFrameByIndex@GFSpyFuncHelper@@YAJPAUIGFFrame@@HPAPAU2@@Z"));
    if (!count || !at)
        return E_NOINTERFACE;
    // These inherited IGFFrame methods are independently verified in GF's
    // popup vtable: the wrappers delegate to the same CFrame implementation.
    treeBudget = {};
    output << ",\"tree\":";
    Tree(popup.Get(), count, at, 0);
    output << ",\"nodes\":" << treeBudget.nodes << ",\"treeComplete\":" << (treeBudget.Complete() ? "true" : "false")
           << ",\"treeErrors\":" << treeBudget.errors << ",\"treeLimited\":" << treeBudget.limited
           << ",\"treeDuplicates\":" << treeBudget.duplicates << ",\"treeDepth\":" << treeBudget.deepest;
    CaptionHits(window, popup.Get());
    return treeBudget.Complete() ? S_OK : HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
#ifdef TXGUI_CAPTION_DISCOVERY
    captionGroup = {};
    buttonParents.clear();
#endif
#if defined(TXGUI_PAINT_OBSERVATION) || defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT)
    paintParents.clear();
#endif
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    DWORD thread = GetWindowThreadProcessId(context->window, nullptr);
    if (thread != GetCurrentThreadId())
        return ERROR_INVALID_THREAD_ID;
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterProbeV1), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    auto report = std::filesystem::path(path).parent_path() /
                  (L"txgui-readonly-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    output.open(report, std::ios::trunc);
    if (!output)
        return ERROR_WRITE_FAULT;
    output << "{\"pid\":" << GetCurrentProcessId() << ",\"guiThread\":" << thread << ",\"layoutModified\":false";
    HRESULT result = E_FAIL;
    try {
        result = Inspect(context->window);
    } catch (...) {
        result = E_UNEXPECTED;
    }
    output << ",\"hresult\":" << static_cast<unsigned long>(result) << "}\n";
    output.close();
#ifdef TXGUI_CAPTION_DISCOVERY
    buttonParents.clear();
    constexpr UINT_PTR expectedAdapter =
#ifdef TXGUI_LEFT_LEASE
        279;
#elif defined(TXGUI_LEFT_EXPERIMENT)
        278;
#elif defined(TXGUI_TITLE_EXPERIMENT)
        277;
#elif defined(TXGUI_ORDER_EXPERIMENT)
        276;
#elif defined(TXGUI_D2D_DRAWING)
        275;
#elif defined(TXGUI_PAINT_OBSERVATION)
        274;
#else
        273;
#endif
    if (context->adapterId == expectedAdapter && SUCCEEDED(result) && captionGroup.Ready())
        return ERROR_SUCCESS;
    captionGroup = {};
#endif
#if defined(TXGUI_PAINT_OBSERVATION) || defined(TXGUI_ORDER_EXPERIMENT) || defined(TXGUI_TITLE_EXPERIMENT)
    paintParents.clear();
#endif
    // Schema discovery never claims to have attached a layout backend.
    return ERROR_NOT_SUPPORTED;
}
#ifdef TXGUI_LEFT_EXPERIMENT
#include "txgui-left-live.h"
#elif defined(TXGUI_TITLE_EXPERIMENT)
#include "txgui-title-live.h"
#elif defined(TXGUI_ORDER_EXPERIMENT)
#include "txgui-order-live.h"
#elif defined(TXGUI_D2D_DRAWING)
#include "txgui-d2d-live.h"
#elif defined(TXGUI_PAINT_OBSERVATION)
#include "txgui-paint-live.h"
#elif defined(TXGUI_ACTION_EXPERIMENT)
#include "txgui-actions.h"
#else
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *) {
    return ERROR_NOT_SUPPORTED;
}
#endif

#ifdef FRAMEWORK_TYPELIB_TOOL
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return ERROR_INVALID_PARAMETER;
    auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return static_cast<int>(initialized);
    HRESULT result;
    {
        ComPtr<ITypeLib> library;
        result = LoadTypeLibEx(argv[1], REGKIND_NONE, &library);
        if (SUCCEEDED(result)) {
            output.open(std::filesystem::path(argv[2]), std::ios::trunc);
            output << "{\"registryModified\":false,\"types\":[";
            bool first = true;
            for (UINT i = 0; i < library->GetTypeInfoCount(); ++i) {
                TYPEKIND kind{};
                if (FAILED(library->GetTypeInfoType(i, &kind)) || (kind != TKIND_INTERFACE && kind != TKIND_DISPATCH))
                    continue;
                ComPtr<ITypeInfo> info;
                if (FAILED(library->GetTypeInfo(i, &info)))
                    continue;
                if (!first)
                    output << ',';
                first = false;
                TypeInfo(info.Get(), 0);
            }
            output << "]}\n";
            output.close();
        }
    }
    CoUninitialize();
    return static_cast<int>(result);
}
#endif
