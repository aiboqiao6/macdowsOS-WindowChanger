#pragma once
#include <windows.h>
#include <string>

namespace styler::settings {
struct Scope {
    std::wstring testId;
    std::wstring RegistryPath() const;
    std::wstring TaskName() const;
};
bool ValidTestId(const std::wstring &value);
bool LoadAutoManage(const Scope &scope);
DWORD SaveAutoManage(const Scope &scope, bool enabled);
struct StartupStatus {
    HRESULT result{S_OK};
    bool exists{}, owned{}, enabled{}, currentPath{};
    std::wstring executable;
};
// The caller initializes COM. Tasks use the current SID, interactive token,
// highest privileges, and no password. Only tasks bearing our marker may change.
StartupStatus QueryStartup(const Scope &scope, const std::wstring &executable);
HRESULT SetStartup(const Scope &scope, const std::wstring &executable, bool enabled);
HRESULT RemoveTestSettings(const Scope &scope);
} // namespace styler::settings
