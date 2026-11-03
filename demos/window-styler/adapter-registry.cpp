#include "adapter-registry.h"
#include "module-capabilities.h"
#include <cwchar>
#include <algorithm>
#include <tlhelp32.h>
namespace styler {
bool AdapterRegistration::Matches(HWND window) const {
    wchar_t cls[256]{};
    GetClassNameW(window, cls, 256);
    if (!className.empty() && className != cls)
        return false;
    if (!classPrefix.empty() && !std::wstring(cls).starts_with(classPrefix))
        return false;
    if (!requiredProperty.empty() && !GetPropW(window, requiredProperty.c_str()))
        return false;
    if (!childClass.empty()) {
        struct Search {
            const std::wstring *name;
            bool found{};
        } search{&childClass};
        EnumChildWindows(
            window,
            [](HWND w, LPARAM p) -> BOOL {
                auto &s = *reinterpret_cast<Search *>(p);
                wchar_t name[256]{};
                GetClassNameW(w, name, 256);
                if (*s.name == name) {
                    s.found = true;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (!search.found)
            return false;
    }
    return anyWindow || !className.empty() || !classPrefix.empty() || !childClass.empty() || !requiredProperty.empty();
}
namespace {
std::string ExportName(const std::wstring &name) {
    std::string symbol;
    symbol.reserve(name.size());
    for (wchar_t character : name) {
        if (character > 0x7f)
            return {};
        symbol.push_back(static_cast<char>(character));
    }
    return symbol;
}

template <class F> bool Every(const std::wstring &list, F check) {
    size_t start{};
    while (start < list.size()) {
        auto end = list.find(L',', start);
        if (end == std::wstring::npos)
            end = list.size();
        if (!check(list.substr(start, end - start)))
            return false;
        start = end + 1;
    }
    return true;
}
} // namespace
bool AdapterRegistration::RuntimeAvailable() const {
    if (!Every(requiredModules, [](const auto &name) { return GetModuleHandleW(name.c_str()) != nullptr; }))
        return false;
    if (!modulesAny.empty()) {
        bool found = false;
        Every(modulesAny, [&](const auto &name) {
            found |= GetModuleHandleW(name.c_str()) != nullptr;
            return true;
        });
        if (!found)
            return false;
    }
    if (!moduleExports.empty() || !moduleCodeView.empty()) {
        bool found = false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W entry{sizeof(entry)};
            if (Module32FirstW(snapshot, &entry))
                do {
                    if ((moduleCodeView.empty() || RemoteCodeView(GetCurrentProcess(), entry, moduleCodeView)) &&
                        Every(moduleExports, [&](const auto &name) {
                            auto symbol = ExportName(name);
                            return !symbol.empty() && GetProcAddress(entry.hModule, symbol.c_str()) != nullptr;
                        })) {
                        found = true;
                        break;
                    }
                } while (Module32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }
        if (!found)
            return false;
    }
    return true;
}
bool AdapterRegistration::RuntimeAvailable(HWND window) const {
    if (requiredModules.empty() && modulesAny.empty() && moduleExports.empty() && moduleCodeView.empty())
        return true;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId())
        return RuntimeAvailable();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    std::vector<MODULEENTRY32W> modules;
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot, &entry))
        do {
            modules.push_back(entry);
        } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    auto has = [&](auto const &name) {
        return std::any_of(modules.begin(), modules.end(),
                           [&](auto const &m) { return _wcsicmp(m.szModule, name.c_str()) == 0; });
    };
    if (!Every(requiredModules, has))
        return false;
    if (!modulesAny.empty()) {
        bool found = false;
        Every(modulesAny, [&](auto const &name) {
            found |= has(name);
            return true;
        });
        if (!found)
            return false;
    }
    if (!moduleExports.empty() || !moduleCodeView.empty()) {
        HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
            return false;
        bool found = std::any_of(modules.begin(), modules.end(), [&](auto const &m) {
            return (moduleCodeView.empty() || RemoteCodeView(process, m, moduleCodeView)) &&
                   Every(moduleExports, [&](auto const &name) {
                       auto symbol = ExportName(name);
                       return !symbol.empty() && RemoteExport(process, m, symbol.c_str());
                   });
        });
        CloseHandle(process);
        if (!found)
            return false;
    }
    return true;
}
std::vector<AdapterRegistration> ReadAdapterRegistry(const std::wstring &directory) {
    auto path = directory + L"\\adapters.ini";
    auto read = [&](const wchar_t *section, const wchar_t *key) {
        wchar_t text[2048]{};
        GetPrivateProfileStringW(section, key, L"", text, 2048, path.c_str());
        return std::wstring(text);
    };
    auto ids = read(L"Adapters", L"Ids");
    std::vector<AdapterRegistration> result;
    size_t start{};
    while (start < ids.size()) {
        auto end = ids.find(L',', start);
        if (end == std::wstring::npos)
            end = ids.size();
        auto token = ids.substr(start, end - start);
        wchar_t *tail{};
        auto id = wcstoull(token.c_str(), &tail, 10);
        start = end + 1;
        if (!tail || *tail || id < PluginAdapterBase || id > 65535)
            continue;
        auto section = L"Adapter" + std::to_wstring(id);
        AdapterRegistration r;
        r.id = static_cast<UINT_PTR>(id);
        r.name = read(section.c_str(), L"Name");
        r.className = read(section.c_str(), L"Class");
        r.classPrefix = read(section.c_str(), L"ClassPrefix");
        r.file = read(section.c_str(), L"File");
        r.requiredModules = read(section.c_str(), L"RequiredModules");
        r.qtVersion = read(section.c_str(), L"QtVersion");
        r.framework = read(section.c_str(), L"Framework");
        r.childClass = read(section.c_str(), L"ChildClass");
        r.requiredProperty = read(section.c_str(), L"RequiredProperty");
        r.modulesAny = read(section.c_str(), L"ModulesAny");
        r.moduleExports = read(section.c_str(), L"ModuleExports");
        r.anyWindow = read(section.c_str(), L"AnyWindow") == L"1";
        r.moduleCodeView = read(section.c_str(), L"ModuleCodeView");
        // Registrations point to a sibling DLL, never an arbitrary relative path.
        const bool scoped = !r.className.empty() || !r.classPrefix.empty() || !r.childClass.empty() ||
                            !r.requiredProperty.empty() ||
                            (r.anyWindow && (!r.requiredModules.empty() || !r.modulesAny.empty() ||
                                             !r.moduleExports.empty() || !r.moduleCodeView.empty()));
        if (r.file.empty() || r.file.find_first_of(L"/\\:") != std::wstring::npos || !r.file.ends_with(L".dll") ||
            !scoped || std::any_of(result.begin(), result.end(), [&](auto &v) { return v.id == r.id; }))
            continue;
        result.push_back(std::move(r));
    }
    return result;
}
} // namespace styler
