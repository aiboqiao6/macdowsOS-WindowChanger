#include "../../adapter-api.h"
#include "../../pe-identity.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace {
bool Read(const void *address, void *destination, size_t length) {
    SIZE_T read{};
    return address && ReadProcessMemory(GetCurrentProcess(), address, destination, length, &read) && read == length;
}
template <class T> T Value(const void *address) {
    T value{};
    Read(address, &value, sizeof(value));
    return value;
}
struct Module {
    BYTE *base{};
    size_t size{};
};
thread_local std::vector<Module> modules;
struct CaptionSelection {
    HWND root{};
    BYTE *parent{};
    BYTE *buttons[3]{}; // close, minimize, maximize
};
thread_local CaptionSelection selection;
bool Inside(const void *address, size_t length = 1) {
    auto pointer = reinterpret_cast<uintptr_t>(address);
    for (auto module : modules) {
        auto base = reinterpret_cast<uintptr_t>(module.base);
        if (pointer >= base && pointer + length >= pointer && pointer + length <= base + module.size)
            return true;
    }
    return false;
}
bool Runtime() {
    modules.clear();
    const struct Profile {
        const wchar_t *name;
        GUID id;
    } profiles[]{
        {L"mso40uiwin32client.dll", {0x2d264595, 0x7ea8, 0x4318, {0x8a, 0xa1, 0x89, 0x76, 0x91, 0xf9, 0xd8, 0xee}}},
        {L"mso98win32client.dll", {0x7da1c564, 0x0bee, 0x44f1, {0x83, 0x3b, 0x50, 0x36, 0x83, 0x82, 0x11, 0xd4}}},
        {L"mso.dll", {0x609d7177, 0x47d3, 0x4508, {0xb5, 0x9d, 0xab, 0x1f, 0xd4, 0xfb, 0x16, 0x24}}}};
    for (const auto &profile : profiles) {
        auto handle = GetModuleHandleW(profile.name);
        if (!styler::MatchesPdb(handle, profile.id, 2))
            return false;
        auto base = reinterpret_cast<BYTE *>(handle);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        modules.push_back({base, nt->OptionalHeader.SizeOfImage});
    }
    BYTE entry[16]{};
    constexpr BYTE expected[]{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                              0xec, 0x20, 0x48, 0x83, 0x64, 0x24, 0x48, 0x00};
    return Read(modules[0].base + 0x333d0, entry, sizeof(entry)) && !memcmp(entry, expected, sizeof(entry));
}
bool ClassName(BYTE *object, std::string &name) {
    auto table = Value<BYTE **>(object);
    if (!Inside(table, 26 * sizeof(void *)))
        return false;
    auto getter = Value<BYTE *>(table + 25);
    BYTE code[18]{};
    if (!Inside(getter, sizeof(code)) || !Read(getter, code, sizeof(code)))
        return false;
    BYTE *info{};
    int displacement{};
    if (!memcmp(code, "\x48\x8d\x05", 3) && code[7] == 0xc3) {
        memcpy(&displacement, code + 3, 4);
        info = getter + 7 + displacement;
    } else if (!memcmp(code, "\x48\x83\x79\x68\x00\x48\x8d\x05", 8) &&
               !memcmp(code + 12, "\x48\x0f\x45\x41\x68\xc3", 6)) {
        memcpy(&displacement, code + 8, 4);
        info = Value<BYTE *>(object + 0x68);
        if (!info)
            info = getter + 12 + displacement;
    }
    if (!Inside(info, 40))
        return false;
    auto label = Value<const wchar_t *>(info + 0x20);
    wchar_t text[80]{};
    if (!Inside(label, sizeof(text)) || !Read(label, text, sizeof(text)))
        return false;
    name.clear();
    for (size_t i = 0; i < 79 && text[i]; ++i) {
        if (text[i] < 32 || text[i] > 126)
            return false;
        name += static_cast<char>(text[i]);
    }
    return !name.empty();
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    selection = {};
    if (!styler::ValidContext(context) || GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId())
        return ERROR_INVALID_PARAMETER;
    if (!Runtime())
        return ERROR_REVISION_MISMATCH;
    struct Root {
        HWND window;
        BYTE *object;
    };
    std::vector<Root> roots;
    EnumChildWindows(
        context->window,
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(window, name, 128);
            if (!wcscmp(name, L"NetUIHWND") && GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId())
                reinterpret_cast<std::vector<Root> *>(data)->push_back(
                    {window, reinterpret_cast<BYTE *>(GetWindowLongPtrW(window, 0))});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&roots));
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterProbeV1), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::ofstream output(std::filesystem::path(path).parent_path() /
                         (L"office-readonly-" + std::to_wstring(GetCurrentProcessId()) + L".json"));
    if (!output)
        return ERROR_WRITE_FAULT;
    output << "{\"pid\":" << GetCurrentProcessId() << ",\"guiThread\":" << GetCurrentThreadId()
           << ",\"layoutModified\":false,\"frameHelpers\":[";
    std::set<BYTE *> visited;
    unsigned helperCount{}, errors{}, activeGroups{};
    auto getInt = reinterpret_cast<int (*)(BYTE *, BYTE *, bool *)>(modules[0].base + 0x333d0);
    struct Helper {
        BYTE *object, *parent;
        HWND root;
        int part, command, hit;
        RECT candidate;
    };
    std::vector<Helper> helpers;
    std::function<bool(BYTE *, BYTE *, HWND, unsigned)> walk = [&](BYTE *object, BYTE *parent, HWND root,
                                                                   unsigned depth) {
        if (!object || depth > 64 || visited.size() >= 5000 || !visited.insert(object).second ||
            Value<BYTE *>(object + 8) != parent || !Inside(Value<BYTE **>(object), 26 * sizeof(void *)))
            return false;
        std::string name;
        ClassName(object, name);
        if (name == "AppFrameHelper") {
            if (helperCount++)
                output << ',';
            auto part = getInt(object, modules[0].base + 0x837128, nullptr);
            auto command = getInt(object, modules[0].base + 0x8371a0, nullptr);
            auto hit = getInt(object, modules[0].base + 0x948250, nullptr);
            auto point = Value<POINT>(object + 0x78);
            auto size = Value<SIZE>(object + 0x80);
            helpers.push_back(
                {object, parent, root, part, command, hit, {point.x, point.y, point.x + size.cx, point.y + size.cy}});
            output << "{\"object\":" << reinterpret_cast<uintptr_t>(object)
                   << ",\"parent\":" << reinterpret_cast<uintptr_t>(parent)
                   << ",\"rootHwnd\":" << reinterpret_cast<uintptr_t>(root)
                   << ",\"rootVisible\":" << (IsWindowVisible(root) ? "true" : "false") << ",\"framePart\":" << part
                   << ",\"sysCommand\":" << command << ",\"hitTest\":" << hit << ",\"localLayoutCandidate\":["
                   << point.x << ',' << point.y << ',' << size.cx << ',' << size.cy << "]}";
        }
        auto first = Value<BYTE *>(object + 0x10);
        for (auto child = first; child;) {
            if (!walk(child, object, root, depth + 1))
                return false;
            auto next = Value<BYTE *>(child + 0x18);
            if (!next)
                return Value<BYTE *>(first + 0x20) == child;
            if (Value<BYTE *>(next + 0x20) != child)
                return false;
            child = next;
        }
        return true;
    };
    for (const auto &root : roots)
        if (!walk(root.object, nullptr, root.window, 0))
            ++errors;
    for (const auto &candidate : helpers) {
        if (candidate.part != 1 || candidate.command != SC_MINIMIZE || candidate.hit != HTMINBUTTON ||
            !IsWindowVisible(candidate.root) || IsRectEmpty(&candidate.candidate))
            continue;
        unsigned matchingMin{}, matchingMax{}, matchingClose{};
        for (const auto &item : helpers) {
            if (item.parent != candidate.parent || item.root != candidate.root || item.part != 1 ||
                IsRectEmpty(&item.candidate) || item.candidate.top != candidate.candidate.top ||
                item.candidate.bottom != candidate.candidate.bottom)
                continue;
            if (item.command == SC_MINIMIZE && item.hit == HTMINBUTTON)
                ++matchingMin;
            if (item.command == SC_MAXIMIZE && item.hit == HTMAXBUTTON)
                ++matchingMax;
            if (item.command == SC_CLOSE && item.hit == HTCLOSE)
                ++matchingClose;
        }
        if (matchingMin == 1 && matchingMax == 1 && matchingClose == 1) {
            ++activeGroups;
            selection.root = candidate.root;
            selection.parent = candidate.parent;
            for (const auto &item : helpers)
                if (item.parent == candidate.parent && item.root == candidate.root && item.part == 1) {
                    if (item.command == SC_CLOSE && item.hit == HTCLOSE)
                        selection.buttons[0] = item.object;
                    if (item.command == SC_MINIMIZE && item.hit == HTMINBUTTON)
                        selection.buttons[1] = item.object;
                    if (item.command == SC_MAXIMIZE && item.hit == HTMAXBUTTON)
                        selection.buttons[2] = item.object;
                }
        }
    }
    output << "],\"helperCount\":" << helperCount << ",\"objectsVisited\":" << visited.size()
           << ",\"treeErrors\":" << errors << ",\"treeComplete\":" << (errors ? "false" : "true")
           << ",\"activeCaptionGroups\":" << activeGroups << "}\n";
    if (errors || activeGroups != 1)
        selection = {};
    return (context->adapterId == 270 || context->adapterId == 271 || context->adapterId == 272) && selection.root
               ? ERROR_SUCCESS
               : ERROR_NOT_SUPPORTED;
}

#if defined(OFFICE_LAYOUT_EXPERIMENT) || defined(OFFICE_LEASE_EXPERIMENT)
#include "office-layout.h"
#else
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context) || context->adapterId != 270 || !selection.root ||
        GetWindowThreadProcessId(context->window, nullptr) != GetCurrentThreadId() || !Runtime())
        return ERROR_NOT_SUPPORTED;
    const struct Entry {
        size_t rva;
        const char *bytes;
    } entries[]{{0x49c30, "\x48\x8b\xc4\x55\x53\x56\x57\x41"},  {0x484c0, "\x48\x89\x5c\x24\x08\x48\x89\x6c"},
                {0x285fa0, "\x48\x8b\xc4\x48\x89\x58\x08\x48"}, {0x4ab00, "\x40\x53\x48\x83\xec\x20\x48\x8b"},
                {0x32e60, "\x48\x8b\xc4\x48\x89\x58\x08\x48"},  {0x135480, "\x40\x53\x48\x83\xec\x20\x8b\xd9"},
                {0x9cba0, "\x48\x89\x5c\x24\x08\x57\x48\x83"}};
    for (const auto &entry : entries) {
        char bytes[8]{};
        if (!Read(modules[0].base + entry.rva, bytes, 8) || memcmp(bytes, entry.bytes, 8))
            return ERROR_REVISION_MISMATCH;
    }
    auto base = modules[0].base;
    auto get = reinterpret_cast<BYTE *(*)(BYTE *, BYTE *, int, unsigned char)>(base + 0x49c30);
    auto set = reinterpret_cast<HRESULT (*)(BYTE *, BYTE *, BYTE *, bool, void *)>(base + 0x484c0);
    auto remove = reinterpret_cast<HRESULT (*)(BYTE *, BYTE *, bool, void *)>(base + 0x285fa0);
    auto release = reinterpret_cast<void (*)(BYTE *)>(base + 0x4ab00);
    auto string = reinterpret_cast<BYTE *(*)(const wchar_t *, HINSTANCE, int, int)>(base + 0x32e60);
    auto color = reinterpret_cast<BYTE *(*)(COLORREF)>(base + 0x135480);
    auto integer = reinterpret_cast<BYTE *(*)(int)>(base + 0x9cba0);
    struct Property {
        BYTE *object{}, *descriptor{}, *original{}, *applied{};
        bool changed{};
    };
    std::vector<Property> properties;
    unsigned checks{}, failures{};
    auto check = [&](bool ok) {
        ++checks;
        if (!ok)
            ++failures;
        return ok;
    };
    constexpr COLORREF colors[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
    auto add = [&](BYTE *object, size_t rva, BYTE *value) {
        auto original = get(object, base + rva, 1, 0);
        properties.push_back({object, base + rva, original, value});
        return original && value;
    };
    bool prepared = true;
    for (unsigned i = 0; i < 3; ++i) {
        auto button = selection.buttons[i];
        std::string name;
        if (!button || !ClassName(button, name) || name != "AppFrameHelper" ||
            Value<BYTE *>(button + 8) != selection.parent) {
            prepared = false;
            break;
        }
        auto child = Value<BYTE *>(button + 0x10);
        if (!child || Value<BYTE *>(child + 8) != button || Value<BYTE *>(child + 0x18) ||
            Value<BYTE **>(child) != reinterpret_cast<BYTE **>(base + 0x84ba60)) {
            prepared = false;
            break;
        }
        prepared &= add(child, 0x83ff48, string(L"\x25cf", nullptr, 1, -1));
        prepared &= add(child, 0x83f3d8, string(L"Segoe UI Symbol", nullptr, 1, -1));
        prepared &= add(child, 0x8387c0, integer(12));
        prepared &= add(child, 0x83b790, color(colors[i]));
    }
    check(prepared && properties.size() == 12);
    if (prepared) {
        for (auto &property : properties) {
            // Only the explicit transaction adapter enters this mutating path.
            property.changed = true; // A failing setter can still notify listeners.
            if (!check(SUCCEEDED(set(property.object, property.descriptor, property.applied, false, nullptr))))
                break;
            auto actual = get(property.object, property.descriptor, 1, 0);
            bool same = actual == property.applied;
            if (actual)
                release(actual);
            if (!check(same))
                break;
        }
    }
    unsigned writes{};
    for (const auto &property : properties)
        writes += property.changed;
    // Roll back in reverse order. A property without a local value must lose
    // the temporary local override, not acquire a copy of its inherited value.
    bool restored = true;
    for (auto item = properties.rbegin(); item != properties.rend(); ++item) {
        if (item->changed) {
            auto unset = Value<short>(item->original + 4) == -1;
            restored &= check(SUCCEEDED(unset ? remove(item->object, item->descriptor, false, nullptr)
                                              : set(item->object, item->descriptor, item->original, false, nullptr)));
            auto actual = get(item->object, item->descriptor, 1, 0);
            restored &= check(actual == item->original);
            if (actual)
                release(actual);
        }
        if (item->original)
            release(item->original);
        if (item->applied)
            release(item->applied);
    }
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&MacdowsAdapterAttachV1), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::ofstream report(std::filesystem::path(path).parent_path() /
                         (L"office-transactions-" + std::to_wstring(GetCurrentProcessId()) + L".json"));
    report << "{\"checks\":" << checks << ",\"failures\":" << failures << ",\"propertiesWritten\":" << writes
           << ",\"localValuesRestored\":" << (restored ? "true" : "false")
           << ",\"layoutMoved\":false,\"renderVerified\":false,\"sessionPublished\":false}\n";
    selection = {};
    // This bounded transaction is not a persistent caption backend.
    return ERROR_NOT_SUPPORTED;
}
#endif
