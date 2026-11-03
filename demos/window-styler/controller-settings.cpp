#include "controller-settings.h"
#include <taskschd.h>
#include <sddl.h>
#include <wrl/client.h>
#include <oleauto.h>
#include <algorithm>
#include <vector>
#include <filesystem>

namespace styler::settings {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto Marker = L"macdowsOS WindowChanger; per-user interactive startup; v1";
struct Bstr {
    BSTR value{};
    explicit Bstr(const wchar_t *text) : value(SysAllocString(text)) {}
    explicit Bstr(const std::wstring &text) : Bstr(text.c_str()) {}
    ~Bstr() {
        SysFreeString(value);
    }
    operator BSTR() const {
        return value;
    }
};
std::wstring Sid() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> data(size);
    std::wstring result;
    if (GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
        LPWSTR text{};
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &text)) {
            result = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return result;
}
struct Scheduler {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    HRESULT Open() {
        auto result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
        VARIANT empty{};
        if (SUCCEEDED(result))
            result = service->Connect(empty, empty, empty, empty);
        if (SUCCEEDED(result))
            result = service->GetFolder(Bstr(L"\\"), &folder);
        return result;
    }
};
bool Missing(HRESULT status) {
    return status == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}
StartupStatus Read(ITaskFolder *folder, const Scope &scope, const std::wstring &executable) {
    StartupStatus result;
    ComPtr<IRegisteredTask> task;
    result.result = folder->GetTask(Bstr(scope.TaskName()), &task);
    if (Missing(result.result)) {
        result.result = S_OK;
        return result;
    }
    if (FAILED(result.result))
        return result;
    result.exists = true;
    ComPtr<ITaskDefinition> definition;
    ComPtr<IRegistrationInfo> info;
    BSTR description{};
    result.result = task->get_Definition(&definition);
    if (SUCCEEDED(result.result))
        result.result = definition->get_RegistrationInfo(&info);
    if (SUCCEEDED(result.result))
        result.result = info->get_Description(&description);
    result.owned = SUCCEEDED(result.result) && description && !wcscmp(description, Marker);
    SysFreeString(description);
    if (!result.owned) {
        result.result = E_ACCESSDENIED;
        return result;
    }
    VARIANT_BOOL enabled{};
    result.result = task->get_Enabled(&enabled);
    result.enabled = enabled != VARIANT_FALSE;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (SUCCEEDED(result.result))
        result.result = definition->get_Actions(&actions);
    LONG count{};
    if (SUCCEEDED(result.result))
        result.result = actions->get_Count(&count);
    if (SUCCEEDED(result.result) && count != 1)
        result.result = E_UNEXPECTED;
    if (SUCCEEDED(result.result))
        result.result = actions->get_Item(1, &action);
    if (SUCCEEDED(result.result))
        result.result = action.As(&exec);
    BSTR path{}, arguments{};
    if (SUCCEEDED(result.result))
        result.result = exec->get_Path(&path);
    if (SUCCEEDED(result.result))
        result.result = exec->get_Arguments(&arguments);
    if (path)
        result.executable = path;
    auto expected = L"--background" + (scope.testId.empty() ? std::wstring{} : L" --test-mode " + scope.testId);
    result.currentPath = path && !_wcsicmp(path, executable.c_str()) && arguments && expected == arguments;
    SysFreeString(path);
    SysFreeString(arguments);
    return result;
}
} // namespace
bool ValidTestId(const std::wstring &value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](wchar_t c) {
        return c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'-';
    });
}
std::wstring Scope::RegistryPath() const {
    return L"Software\\macdowsOS\\WindowChanger" + (testId.empty() ? std::wstring{} : L"\\Tests\\" + testId);
}
std::wstring Scope::TaskName() const {
    return L"macdowsOS-WindowChanger-" + Sid() + (testId.empty() ? std::wstring{} : L"-Test-" + testId);
}
bool LoadAutoManage(const Scope &scope) {
    DWORD value{1}, size{sizeof(value)};
    auto status = RegGetValueW(HKEY_CURRENT_USER, scope.RegistryPath().c_str(), L"AutoManage", RRF_RT_REG_DWORD,
                               nullptr, &value, &size);
    if (status == ERROR_SUCCESS)
        return value != 0;
    // First use defaults on; inaccessible or corrupt preferences fail closed.
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}
DWORD SaveAutoManage(const Scope &scope, bool enabled) {
    HKEY key{};
    auto status = RegCreateKeyExW(HKEY_CURRENT_USER, scope.RegistryPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                                  nullptr, &key, nullptr);
    DWORD value = enabled ? 1 : 0;
    if (status == ERROR_SUCCESS) {
        status =
            RegSetValueExW(key, L"AutoManage", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
        RegCloseKey(key);
    }
    return status;
}
StartupStatus QueryStartup(const Scope &scope, const std::wstring &executable) {
    Scheduler scheduler;
    auto status = scheduler.Open();
    if (FAILED(status)) {
        StartupStatus result;
        result.result = status;
        return result;
    }
    return Read(scheduler.folder.Get(), scope, executable);
}
HRESULT SetStartup(const Scope &scope, const std::wstring &executable, bool enabled) {
    if (executable.empty() || (!scope.testId.empty() && !ValidTestId(scope.testId)))
        return E_INVALIDARG;
    auto sid = Sid();
    if (sid.empty())
        return E_ACCESSDENIED;
    Scheduler scheduler;
    auto status = scheduler.Open();
    if (FAILED(status))
        return status;
    auto existing = Read(scheduler.folder.Get(), scope, executable);
    if (FAILED(existing.result))
        return existing.result;
    if (!enabled)
        return existing.exists ? scheduler.folder->DeleteTask(Bstr(scope.TaskName()), 0) : S_OK;
    ComPtr<ITaskDefinition> task;
    ComPtr<IRegistrationInfo> info;
    ComPtr<IPrincipal> principal;
    ComPtr<ITaskSettings> settings;
    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    status = scheduler.service->NewTask(0, &task);
    if (SUCCEEDED(status))
        status = task->get_RegistrationInfo(&info);
    if (SUCCEEDED(status))
        status = info->put_Description(Bstr(Marker));
    if (SUCCEEDED(status))
        status = info->put_Author(Bstr(L"macdowsOS WindowChanger"));
    if (SUCCEEDED(status))
        status = task->get_Principal(&principal);
    if (SUCCEEDED(status))
        status = principal->put_UserId(Bstr(sid));
    if (SUCCEEDED(status))
        status = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (SUCCEEDED(status))
        status = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    if (SUCCEEDED(status))
        status = task->get_Settings(&settings);
    if (SUCCEEDED(status))
        status = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(status))
        status = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(status))
        status = settings->put_StartWhenAvailable(VARIANT_TRUE);
    if (SUCCEEDED(status))
        status = settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
    if (SUCCEEDED(status))
        status = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if (SUCCEEDED(status))
        status = task->get_Triggers(&triggers);
    if (SUCCEEDED(status))
        status = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
    if (SUCCEEDED(status))
        status = trigger.As(&logon);
    if (SUCCEEDED(status))
        status = logon->put_UserId(Bstr(sid));
    if (SUCCEEDED(status))
        status = task->get_Actions(&actions);
    if (SUCCEEDED(status))
        status = actions->Create(TASK_ACTION_EXEC, &action);
    if (SUCCEEDED(status))
        status = action.As(&exec);
    if (SUCCEEDED(status))
        status = exec->put_Path(Bstr(executable));
    if (SUCCEEDED(status))
        status = exec->put_WorkingDirectory(Bstr(std::filesystem::path(executable).parent_path().wstring()));
    auto arguments = L"--background" + (scope.testId.empty() ? std::wstring{} : L" --test-mode " + scope.testId);
    if (SUCCEEDED(status))
        status = exec->put_Arguments(Bstr(arguments));
    VARIANT user{}, empty{};
    user.vt = VT_BSTR;
    user.bstrVal = SysAllocString(sid.c_str());
    ComPtr<IRegisteredTask> registered;
    if (SUCCEEDED(status))
        status =
            scheduler.folder->RegisterTaskDefinition(Bstr(scope.TaskName()), task.Get(), TASK_CREATE_OR_UPDATE, user,
                                                     empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    VariantClear(&user);
    if (FAILED(status))
        return status;
    auto after = Read(scheduler.folder.Get(), scope, executable);
    return SUCCEEDED(after.result) && after.enabled && after.currentPath ? S_OK : E_FAIL;
}
HRESULT RemoveTestSettings(const Scope &scope) {
    if (!ValidTestId(scope.testId))
        return E_INVALIDARG;
    auto status = SetStartup(scope, L"test-cleanup", false);
    auto removed = RegDeleteTreeW(HKEY_CURRENT_USER, scope.RegistryPath().c_str());
    return FAILED(status)                                                ? status
           : removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND ? S_OK
                                                                         : HRESULT_FROM_WIN32(removed);
}
} // namespace styler::settings
