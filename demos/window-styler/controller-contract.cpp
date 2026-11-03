#include "auto-manage.h"
#include "controller-settings.h"
#include <iostream>

int wmain() {
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *name) {
        ++checks;
        failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    };
    styler::AutoManageQueue queue;
    check(!queue.Ready(1, 10, 1000, true), "new windows wait for framework initialization");
    check(!queue.Ready(1, 10, 1499, true), "initialization wait is not shortened");
    check(queue.Ready(1, 10, 1500, true), "stable supported window becomes eligible");
    queue.Failed(1, 1500);
    check(!queue.Ready(1, 10, 6499, true) && queue.Ready(1, 10, 6500, true),
          "temporary failure is retried after five seconds");
    queue.Failed(1, 6500);
    check(!queue.Ready(1, 10, 16499, true) && queue.Ready(1, 10, 16500, true),
          "retry delay backs off instead of busy polling");
    queue.Failed(1, 16500);
    queue.Failed(1, 36500);
    queue.Failed(1, 76500);
    check(!queue.Ready(1, 10, 136499, true) && queue.Ready(1, 10, 136500, true),
          "retry delay is bounded at one minute");
    queue.Exclude(1, 10);
    check(!queue.Ready(1, 10, 999999, true), "manual restore is not immediately undone by auto management");
    check(!queue.Ready(1, 11, 1000000, true) && queue.Ready(1, 11, 1000500, true),
          "reused HWND with a new PID does not inherit exclusions");
    check(!queue.Ready(2, 20, 2000, false) && !queue.Ready(2, 20, 9000, false),
          "minimized or hidden windows remain deferred");
    check(!queue.Ready(2, 20, 9499, true) && queue.Ready(2, 20, 9500, true),
          "restored windows get their initialization grace period");
    queue.Exclude(2, 20);
    queue.Allow(2);
    check(!queue.Ready(2, 20, 10000, true) && queue.Ready(2, 20, 10500, true),
          "explicit apply removes a manual exclusion safely");
    queue.Prune({{2, 20}});
    check(queue.Size() == 1, "destroyed windows and reused PIDs are pruned");
    queue.Reset();
    check(queue.Size() == 0, "resuming automatic mode resets discovery state");
    check(styler::settings::ValidTestId(L"controller-test-123"), "test settings accept a scoped safe identifier");
    check(!styler::settings::ValidTestId(L"..\\OtherTask") && !styler::settings::ValidTestId(L"") &&
              !styler::settings::ValidTestId(std::wstring(65, L'a')),
          "test identifiers cannot escape their settings namespace");
    auto original = styler::settings::LoadAutoManage({});
    styler::settings::Scope scope{L"unit-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(GetTickCount64())};
    check(styler::settings::LoadAutoManage(scope), "first run enables automatic management by default");
    check(styler::settings::SaveAutoManage(scope, false) == ERROR_SUCCESS && !styler::settings::LoadAutoManage(scope),
          "paused auto preference is persisted per user");
    check(styler::settings::SaveAutoManage(scope, true) == ERROR_SUCCESS && styler::settings::LoadAutoManage(scope),
          "automatic preference can be enabled again");
    HKEY malformed{};
    auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, scope.RegistryPath().c_str(), 0, KEY_SET_VALUE, &malformed);
    const wchar_t invalid[] = L"not-a-boolean";
    auto written = opened == ERROR_SUCCESS ? RegSetValueExW(malformed, L"AutoManage", 0, REG_SZ,
                                                            reinterpret_cast<const BYTE *>(invalid), sizeof(invalid))
                                           : opened;
    if (malformed)
        RegCloseKey(malformed);
    check(written == ERROR_SUCCESS && !styler::settings::LoadAutoManage(scope),
          "corrupt automatic preferences fail closed instead of silently enabling takeover");
    check(RegDeleteTreeW(HKEY_CURRENT_USER, scope.RegistryPath().c_str()) == ERROR_SUCCESS,
          "remove only the fixture-created settings key");
    check(styler::settings::LoadAutoManage({}) == original, "real user automatic preference remains unchanged");
    std::cout << "checks=" << checks << "; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
