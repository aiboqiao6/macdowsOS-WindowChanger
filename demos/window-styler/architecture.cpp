#include "architecture.h"
#include "session.h"
#include <filesystem>
namespace styler {
namespace {
bool Architecture(HWND window, USHORT &own, USHORT &target) {
    DWORD pid{};
    WindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    USHORT on{}, tn{};
    bool ok = IsWow64Process2(GetCurrentProcess(), &own, &on) && IsWow64Process2(process, &target, &tn) && on == tn;
    CloseHandle(process);
    return ok;
}
} // namespace
bool DifferentArchitecture(HWND window) {
    USHORT own{}, target{};
    return Architecture(window, own, target) && own != target;
}
std::wstring PeerExecutable(HWND window, const std::wstring &executable) {
    USHORT own{}, target{};
    if (!Architecture(window, own, target) || own == target)
        return {};
    auto root = std::filesystem::path(ExecutableDirectory());
    bool x86 = target == IMAGE_FILE_MACHINE_I386;
    auto file = root / (x86 ? L"x86" : L"x64") / executable;
    if (!std::filesystem::exists(file))
        file = root.parent_path() / executable;
    DWORD type{};
    if (!GetBinaryTypeW(file.c_str(), &type) || type != DWORD(x86 ? SCS_32BIT_BINARY : SCS_64BIT_BINARY))
        return {};
    return file.wstring();
}
} // namespace styler
