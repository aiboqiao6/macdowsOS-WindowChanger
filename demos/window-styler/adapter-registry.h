#pragma once
#include "adapter-api.h"
#include <string>
#include <vector>
namespace styler {
struct AdapterRegistration {
    UINT_PTR id{};
    std::wstring name, className, classPrefix, childClass, requiredProperty, file, requiredModules, modulesAny,
        moduleExports, moduleCodeView, qtVersion, framework;
    bool anyWindow{};
    bool Matches(HWND window) const;
    bool RuntimeAvailable() const;
    bool RuntimeAvailable(HWND window) const;
};
std::vector<AdapterRegistration> ReadAdapterRegistry(const std::wstring &directory);
} // namespace styler
