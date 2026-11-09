#include "session.h"
#include "adapter-api.h"
#include "embedded-frame.h"
#include <algorithm>
#include <cwchar>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <array>
#include <cstring>

namespace styler {
namespace {
std::wstring ErrorText(DWORD error) {
    wchar_t buffer[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, buffer, 512, nullptr);
    return std::wstring(buffer) + L" (" + std::to_wstring(error) + L")";
}
bool SameBitness(HANDLE process) {
    USHORT own{}, ownNative{}, other{}, otherNative{};
    return IsWow64Process2(GetCurrentProcess(), &own, &ownNative) && IsWow64Process2(process, &other, &otherNative) &&
           own == other && ownNative == otherNative;
}
bool CompatibleToken(HANDLE process) {
    HANDLE ownToken{}, otherToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ownToken))
        return false;
    if (!OpenProcessToken(process, TOKEN_QUERY, &otherToken)) {
        CloseHandle(ownToken);
        return false;
    }
    alignas(TOKEN_USER) BYTE ownUser[1024]{}, otherUser[1024]{};
    alignas(TOKEN_MANDATORY_LABEL) BYTE ownLevel[1024]{}, otherLevel[1024]{};
    DWORD bytes{};
    bool result = GetTokenInformation(ownToken, TokenUser, ownUser, sizeof(ownUser), &bytes) &&
                  GetTokenInformation(otherToken, TokenUser, otherUser, sizeof(otherUser), &bytes) &&
                  EqualSid(reinterpret_cast<TOKEN_USER *>(ownUser)->User.Sid,
                           reinterpret_cast<TOKEN_USER *>(otherUser)->User.Sid) &&
                  GetTokenInformation(ownToken, TokenIntegrityLevel, ownLevel, sizeof(ownLevel), &bytes) &&
                  GetTokenInformation(otherToken, TokenIntegrityLevel, otherLevel, sizeof(otherLevel), &bytes);
    if (result) {
        auto a = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(ownLevel)->Label.Sid;
        auto b = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(otherLevel)->Label.Sid;
        result = *GetSidSubAuthority(a, *GetSidSubAuthorityCount(a) - 1) >=
                 *GetSidSubAuthority(b, *GetSidSubAuthorityCount(b) - 1);
    }
    CloseHandle(ownToken);
    CloseHandle(otherToken);
    return result;
}
bool Send(HWND window, UINT message, WPARAM wParam, LPARAM lParam, DWORD_PTR *result) {
    return SendMessageTimeoutW(window, message, wParam, lParam, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, result) != 0;
}
bool SameFileBytes(const std::wstring &a, const std::wstring &b) {
    HANDLE first = CreateFileW(a.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE second = CreateFileW(b.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    LARGE_INTEGER firstSize{}, secondSize{};
    bool same = first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE && GetFileSizeEx(first, &firstSize) &&
                GetFileSizeEx(second, &secondSize) && firstSize.QuadPart == secondSize.QuadPart;
    std::array<BYTE, 16384> left{}, right{};
    while (same) {
        DWORD n{}, m{};
        same = ReadFile(first, left.data(), static_cast<DWORD>(left.size()), &n, nullptr) &&
               ReadFile(second, right.data(), static_cast<DWORD>(right.size()), &m, nullptr) && n == m &&
               !memcmp(left.data(), right.data(), n);
        if (!same || !n)
            break;
    }
    if (first != INVALID_HANDLE_VALUE)
        CloseHandle(first);
    if (second != INVALID_HANDLE_VALUE)
        CloseHandle(second);
    return same;
}
BYTE *RemoteImage(DWORD pid, const std::wstring &path) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;
    MODULEENTRY32W entry{sizeof(entry)};
    BYTE *base{};
    if (Module32FirstW(snapshot, &entry))
        do {
            if (!_wcsicmp(entry.szExePath, path.c_str())) {
                base = entry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return base;
}
DWORD RemoteCall(HANDLE process, BYTE *entry, const void *argument, size_t size, ULONGLONG deadline, DWORD &result) {
    void *buffer = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer)
        return GetLastError();
    SIZE_T written{};
    if (!WriteProcessMemory(process, buffer, argument, size, &written) || written != size) {
        DWORD e = GetLastError();
        VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
        return e ? e : ERROR_WRITE_FAULT;
    }
    HANDLE thread =
        CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(entry), buffer, 0, nullptr);
    if (!thread) {
        DWORD e = GetLastError();
        VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
        return e;
    }
    auto now = GetTickCount64();
    DWORD wait = WaitForSingleObject(thread, now < deadline ? static_cast<DWORD>(deadline - now) : 0);
    DWORD error = ERROR_SUCCESS;
    if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeThread(thread, &result))
            error = GetLastError();
        VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
    } else
        error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    // A timed-out thread may still consume its argument. Never terminate it or
    // free live arguments. The small allocation remains until target exit.
    CloseHandle(thread);
    return error;
}
DWORD Bootstrap(HWND window, HWND owner, HMODULE module) {
    DWORD pid{};
    WindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process)
        return GetLastError();
    struct Handle {
        HANDLE value;
        ~Handle() {
            CloseHandle(value);
        }
    } handle{process};
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(module, path, 32768))
        return GetLastError();
    auto entry = GetProcAddress(module, "MacdowsThreadBootstrapV1");
    if (!entry)
        entry = GetProcAddress(module, "_MacdowsThreadBootstrapV1@4");
    if (!entry)
        return ERROR_PROC_NOT_FOUND;
    auto deadline = GetTickCount64() + 2000;
    DWORD code{};
    BYTE *remote = RemoteImage(pid, path);
    if (!remote) {
        auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HMODULE system{};
        if (!load ||
            !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(load), &system))
            return ERROR_PROC_NOT_FOUND;
        wchar_t systemPath[32768]{};
        if (!GetModuleFileNameW(system, systemPath, 32768))
            return GetLastError();
        auto remoteSystem = RemoteImage(pid, systemPath);
        if (!remoteSystem)
            return ERROR_MOD_NOT_FOUND;
        auto remoteLoad = remoteSystem + (reinterpret_cast<BYTE *>(load) - reinterpret_cast<BYTE *>(system));
        if (auto error = RemoteCall(process, remoteLoad, path, (wcslen(path) + 1) * sizeof(wchar_t), deadline, code))
            return error;
        remote = RemoteImage(pid, path);
        if (!remote)
            return ERROR_MOD_NOT_FOUND;
    }
    styler::ThreadBootstrapV1 request;
    request.window = window;
    request.owner = owner;
    auto remoteEntry = remote + (reinterpret_cast<BYTE *>(entry) - reinterpret_cast<BYTE *>(module));
    if (auto error = RemoteCall(process, remoteEntry, &request, sizeof(request), deadline, code))
        return error;
    return code;
}
} // namespace

std::wstring ExecutableDirectory() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring result(path, length);
    return result.substr(0, result.find_last_of(L"\\/"));
}
std::wstring WindowTitle(HWND window) {
    wchar_t title[1024]{};
    GetWindowTextW(window, title, 1024);
    return title;
}

Candidate Inspect(HWND window, bool forInjection, bool allowOtherArchitecture, HWND embeddedFrame) {
    Candidate c;
    c.window = window;
    c.title = WindowTitle(window);
    WindowThreadProcessId(window, &c.pid);
    wchar_t className[256]{};
    GetClassNameW(window, className, 256);
    c.className = className;
    c.adapter = L"Win32 标题栏 / 实验性";
    DWORD cloaked{};
    COLORREF layeredKey{};
    BYTE layeredAlpha{};
    DWORD layeredFlags{};
    bool uniformLayered = GetLayeredWindowAttributes(window, &layeredKey, &layeredAlpha, &layeredFlags) != FALSE;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    const bool embedded = embeddedFrame && EmbeddedCore(window, embeddedFrame);
    RECT bounds{};
    const bool hasBounds = GetWindowRect(window, &bounds) != FALSE;
    if (embeddedFrame && !embedded)
        c.reason = L"嵌入内容与 ApplicationFrame 的窗口关系未通过验证";
    else if (!IsWindow(window) || !IsWindowVisible(window) ||
             (!embedded &&
              (GetAncestor(window, GA_ROOT) != window || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD))))
        c.reason = L"需要可见的顶层窗口";
    else if (c.pid == GetCurrentProcessId())
        c.reason = L"控制器自身";
    else if (window == GetShellWindow() || c.className == L"Shell_TrayWnd" || c.className == L"WorkerW" ||
             c.className == L"Progman" || c.className == L"Shell_SecondaryTrayWnd" ||
             (c.className == L"Button" && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)))
        c.reason = L"桌面、任务栏及其按钮不属于应用窗口";
    else if (cloaked && !embedded)
        c.reason = L"DWM 隐藏窗口（其他桌面或系统后台），当前没有可见画面可检查";
    else if (!IsIconic(window) && (!hasBounds || IsRectEmpty(&bounds)))
        c.reason = L"零面积辅助窗口，当前没有可见画面可适配";
    else if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) &&
             !(GetWindowLongPtrW(window, GWL_STYLE) & (WS_CAPTION | WS_SYSMENU)))
        c.reason = L"工具 / 托盘辅助窗口，不作为主应用窗口替换标题栏";
    else if (forInjection && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) && !uniformLayered)
        c.reason = L"分层透明窗口需要独立的 alpha 合成适配器";
    else {
        RECT outer{};
        POINT origin{};
        GetWindowRect(window, &outer);
        ClientToScreen(window, &origin);
        bool clientDrawnTop = !IsIconic(window) && !GetMenu(window) &&
                              origin.y - outer.top < GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(window));
        bool custom = (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) != WS_CAPTION || clientDrawnTop ||
                      GetPropW(window, AdapterProperty) == reinterpret_cast<HANDLE>(Adapter::Extended) ||
                      c.className.starts_with(L"Chrome_") || c.className.starts_with(L"Qt") ||
                      c.className.starts_with(L"HwndWrapper");
        if (custom && !GetMenu(window)) {
            c.adapterMode = Adapter::Extended;
            c.adapter = L"自绘 / 无边框 · 扩展标题栏";
        } else if (c.className == L"#32770")
            c.adapter = L"Win32 对话框 · 原生标题栏";
        if (!forInjection) {
            c.adapterMode = Adapter::Dwm;
            const DWORD attributes[]{33, 34, 35, 36};
            for (UINT i = 0; i < 4; ++i) {
                DWORD value{};
                if (SUCCEEDED(DwmGetWindowAttribute(window, attributes[i], &value, sizeof(value))))
                    c.visualMask |= 1u << i;
            }
            c.adapter = c.visualMask == 1 ? L"DWM 原位 · 仅圆角可回读" : L"DWM 原位 · 保留原有布局";
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, c.pid);
        if (!process)
            c.reason = L"无法查询目标进程：" + ErrorText(GetLastError());
        else {
            if (forInjection && !allowOtherArchitecture && !SameBitness(process))
                c.reason = L"目标与控制器架构不同；请使用完整的 x64 + x86 包或对应架构构建";
            else if (!CompatibleToken(process))
                c.reason = L"需要同一用户且控制器完整性级别不低于目标；请使用管理员入口";
            else {
                c.supported = forInjection || c.visualMask != 0;
                if (!c.supported)
                    c.reason = L"系统没有提供可回读、可安全还原的外观属性";
                if (forInjection && c.className == TargetClass && c.adapterMode == Adapter::Native)
                    c.adapter = L"Win32 标题栏 / 内置测试窗口";
            }
            CloseHandle(process);
        }
    }
    return c;
}

std::vector<Candidate> Enumerate() {
    std::vector<Candidate> candidates;
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            if (IsWindowVisible(window) && GetWindowTextLengthW(window) > 0) {
                Candidate c = Inspect(window);
                if (c.pid != GetCurrentProcessId())
                    reinterpret_cast<std::vector<Candidate> *>(param)->push_back(std::move(c));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&candidates));
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        bool sampleA = a.className == TargetClass, sampleB = b.className == TargetClass;
        return sampleA != sampleB ? sampleA : a.supported > b.supported;
    });
    return candidates;
}

Session::Session(HWND controller, bool bootstrapOnly) : controller_(controller), bootstrapOnly_(bootstrapOnly) {}
DWORD Session::HookSource(DWORD pid, HMODULE &module, HOOKPROC &procedure) {
    auto local = ExecutableDirectory() + L"\\" + STYLER_HOOK_NAME, path = local;
    // Pinned hook modules can outlive the controller. Register the same image
    // origin when another installation takes over; do not assume a copied DLL
    // with the same basename is an independent bootstrap module.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return GetLastError();
    MODULEENTRY32W entry{sizeof(entry)};
    DWORD error{};
    if (Module32FirstW(snapshot, &entry))
        do {
            if (_wcsicmp(entry.szModule, STYLER_HOOK_NAME))
                continue;
            if (_wcsicmp(entry.szExePath, local.c_str())) {
                if (!SameFileBytes(local, entry.szExePath)) {
                    error = ERROR_REVISION_MISMATCH;
                    break;
                }
                path = entry.szExePath;
            }
            break;
        } while (Module32NextW(snapshot, &entry));
    else
        error = GetLastError();
    CloseHandle(snapshot);
    if (error)
        return error;
    auto found = modules_.find(path);
    if (found == modules_.end()) {
        module =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
            return GetLastError();
        modules_.emplace(path, module);
    } else
        module = found->second;
    procedure = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "MacdowsStyleHook"));
    if (!procedure)
        procedure = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "_MacdowsStyleHook@12"));
    return procedure ? ERROR_SUCCESS : ERROR_PROC_NOT_FOUND;
}
Session::~Session() {
    RestoreAll();
    for (const auto &[window, attachment] : attachments_) {
        (void)window;
        UnhookWindowsHookEx(attachment.hook);
    }
    for (const auto &[path, module] : modules_) {
        (void)path;
        FreeLibrary(module);
    }
}
bool Session::Owns(HWND window) const {
    return GetPropW(window, OwnerProperty) == controller_;
}
size_t Session::Count() const {
    return std::count_if(attachments_.begin(), attachments_.end(),
                         [this](const auto &item) { return Owns(item.first); });
}

Result Session::Apply(HWND window, Adapter requested) {
    auto candidate = Inspect(window, true, false,
                             static_cast<UINT_PTR>(requested) == UwpInsetAdapter
                                 ? reinterpret_cast<HWND>(GetPropW(window, EmbeddedFrameProperty))
                                 : nullptr);
    if (!candidate.supported)
        return {false, candidate.reason};
    if (requested != static_cast<Adapter>(0))
        candidate.adapterMode = requested;
    if (Owns(window))
        return {true, L"该窗口已应用 macOS 标题栏。"};
    if (GetPropW(window, OwnerProperty))
        return {false, L"该窗口由另一个控制器管理，请先在原控制器还原。"};
    if (auto stale = attachments_.find(window); stale != attachments_.end()) {
        UnhookWindowsHookEx(stale->second.hook);
        attachments_.erase(stale);
    }
    DWORD pid{};
    DWORD thread = WindowThreadProcessId(window, &pid);
    HMODULE module{};
    HOOKPROC procedure{};
    if (auto error = HookSource(pid, module, procedure))
        return {false, L"无法核验或加载同架构的线程 Hook DLL：" + ErrorText(error)};
    if (!SetPropW(window, RequestProperty, controller_))
        return {false, L"无法创建窗口附加请求：" + ErrorText(GetLastError())};
    if (!SetPropW(window, AdapterProperty, reinterpret_cast<HANDLE>(candidate.adapterMode))) {
        DWORD error = GetLastError();
        RemovePropW(window, RequestProperty);
        return {false, L"无法设置窗口适配方式：" + ErrorText(error)};
    }
    HHOOK hook = bootstrapOnly_ ? nullptr : SetWindowsHookExW(WH_CALLWNDPROC, procedure, module, thread);
    if (!hook) {
        DWORD error = bootstrapOnly_ ? ERROR_MOD_NOT_FOUND : GetLastError();
        if (error != ERROR_MOD_NOT_FOUND) {
            RemovePropW(window, RequestProperty);
            RemovePropW(window, AdapterProperty);
            return {false, L"安装线程 Hook 失败：" + ErrorText(error)};
        }
    }
    attachments_[window] = {hook, pid, thread};
    RemovePropW(window, ResultProperty);
    DWORD_PTR ignored{};
    DWORD bootstrapError{};
    auto sendApply = [&]() {
        if (hook)
            return Send(window, ApplyMessage(), reinterpret_cast<WPARAM>(controller_), Version, &ignored);
        bootstrapError = Bootstrap(window, controller_, module);
        return bootstrapError == ERROR_SUCCESS;
    };
    if (!sendApply()) {
        if (bootstrapError && bootstrapError != ERROR_TIMEOUT) {
            RemovePropW(window, RequestProperty);
            if (!Owns(window)) {
                RemovePropW(window, AdapterProperty);
                attachments_.erase(window);
            }
            return {false, L"进程内线程引导失败：" + ErrorText(bootstrapError)};
        }
        // A timed-out sent message may still execute later. Keep its hook and
        // ownership record until restore/shutdown; the target also watches us.
        return {false, L"目标未在 2 秒内响应；可能稍后生效，可点击还原或关闭控制器恢复。"};
    }
    auto result = reinterpret_cast<ULONG_PTR>(RemovePropW(window, ResultProperty));
    // Some public toolkit diagnostics enumerate roots asynchronously. Retry
    // only read-only Probe, from the controller, without blocking the GUI.
    auto probeDeadline = GetTickCount64() + 2000;
    while (result == ERROR_RETRY + 1 && GetTickCount64() < probeDeadline && IsWindow(window)) {
        Sleep(50);
        if (!sendApply())
            break;
        result = reinterpret_cast<ULONG_PTR>(RemovePropW(window, ResultProperty));
    }
    RemovePropW(window, RequestProperty);
    DWORD_PTR acknowledgement{};
    // Renderer adapters acknowledge only after their asynchronous layout has
    // completed. Poll from the controller process; never block the target UI.
    auto acknowledgementDeadline = GetTickCount64() + 2000;
    while (Owns(window) && FrameworkAdapter(candidate.adapterMode) &&
           Send(window, QueryMessage(), 0, 0, &acknowledgement) && acknowledgement != Acknowledged &&
           GetTickCount64() < acknowledgementDeadline)
        Sleep(15);
    if (Owns(window) && Send(window, QueryMessage(), 0, 0, &acknowledgement) && acknowledgement == Acknowledged)
        return {true, FrameworkAdapter(candidate.adapterMode)      ? L"进程内后端已探测并确认原布局附加。"
                      : candidate.adapterMode == Adapter::Extended ? L"已添加扩展标题栏，保留自绘内容和原有工具栏。"
                                                                   : L"已应用 macOS 标题栏，并立即重绘客户区和边框。"};
    if (Owns(window))
        return {false, L"已附加，但目标未确认状态。可点击还原或关闭控制器恢复。"};
    UnhookWindowsHookEx(hook);
    RemovePropW(window, AdapterProperty);
    attachments_.erase(window);
    return {false, result > 1 ? L"目标适配失败：" + ErrorText(static_cast<DWORD>(result - 1))
                              : L"目标未确认附加。可能是自绘标题栏、进程保护或系统 Hook 限制。"};
}

Result Session::ApplyEmbedded(HWND core, HWND frame) {
    auto candidate = Inspect(core, true, false, frame);
    if (!candidate.supported)
        return {false, candidate.reason};
    if (!EmbeddedCore(core, frame))
        return {false, L"ApplicationFrame 内容窗口关系不匹配。"};
    if (Owns(core))
        return {GetPropW(core, EmbeddedFrameProperty) == frame, L"嵌入内容已由此会话管理。"};
    if (GetPropW(core, OwnerProperty) || GetPropW(core, EmbeddedFrameProperty))
        return {false, L"嵌入内容由另一会话管理。"};
    if (!SetPropW(core, EmbeddedFrameProperty, frame))
        return {false, L"无法发布嵌入内容请求。"};
    auto result = Apply(core, static_cast<Adapter>(UwpInsetAdapter));
    if (!result.ok) {
        Restore(core);
        if (!GetPropW(core, OwnerProperty))
            RemovePropW(core, EmbeddedFrameProperty);
    }
    return result;
}

Result Session::Restore(HWND window) {
    auto item = attachments_.find(window);
    if (item != attachments_.end()) {
        DWORD pid{};
        DWORD thread = WindowThreadProcessId(window, &pid);
        if (!IsWindow(window) || pid != item->second.pid || thread != item->second.thread) {
            UnhookWindowsHookEx(item->second.hook);
            attachments_.erase(item);
            return {true, L"原窗口已销毁，已释放其会话记录。"};
        }
    }
    if (GetPropW(window, RequestProperty) == controller_)
        RemovePropW(window, RequestProperty);
    if (item == attachments_.end() && !Owns(window))
        return {true, L"该窗口无需还原。"};
    if (Owns(window)) {
        DWORD_PTR result{};
        auto deadline = GetTickCount64() + 1500;
        bool sent{};
        do {
            sent = Send(window, DetachMessage(), reinterpret_cast<WPARAM>(controller_), 0, &result);
            if (!sent || result != TransitionBusy)
                break;
            Sleep(15);
        } while (GetTickCount64() < deadline);
        if (!sent || result != Acknowledged || Owns(window))
            return {false, L"目标暂时未响应还原；恢复响应后可重试，控制器退出后也会自动还原。"};
    }
    if (item != attachments_.end()) {
        UnhookWindowsHookEx(item->second.hook);
        attachments_.erase(item);
    }
    if (!GetPropW(window, OwnerProperty))
        RemovePropW(window, AdapterProperty);
    return {true, L"已恢复原生标题栏。"};
}
Result Session::RestoreAll() {
    std::vector<HWND> windows;
    for (const auto &[window, attachment] : attachments_) {
        (void)attachment;
        windows.push_back(window);
    }
    bool success = true;
    for (HWND window : windows)
        if (!Restore(window).ok)
            success = false;
    return {success, success ? L"所有窗口已还原。" : L"部分窗口未响应，将在恢复响应且控制器退出后自动还原。"};
}
} // namespace styler
