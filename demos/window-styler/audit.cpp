#include "inline.h"
#include "host-client.h"
#include "visual.h"
#include <dwmapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// No input automation or application-content messages. One HWND per worker.
namespace {
namespace fs = std::filesystem;
std::string Utf8(const std::wstring &value) {
    if (value.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring Json(const std::wstring &value) {
    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'"' || c == L'\\') {
            result += L'\\';
            result += c;
        } else if (c == L'\n')
            result += L"\\n";
        else if (c == L'\r')
            result += L"\\r";
        else if (c == L'\t')
            result += L"\\t";
        else if (c >= 32)
            result += c;
    }
    return result + L"\"";
}
std::wstring Hex(UINT_PTR value) {
    std::wostringstream out;
    out << L"0x" << std::hex << value;
    return out.str();
}
std::wstring RectJson(const RECT &r) {
    return L"[" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," +
           std::to_wstring(r.bottom) + L"]";
}
void Write(const fs::path &path, const std::wstring &value) {
    std::ofstream out(path, std::ios::binary);
    out << Utf8(value);
    if (!out)
        throw std::runtime_error("Cannot write audit output");
}
std::string Read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[32768]{};
    DWORD n = 32768;
    bool ok = process && QueryFullProcessImageNameW(process, 0, path, &n);
    if (process)
        CloseHandle(process);
    return ok ? std::wstring(path, n) : L"";
}
std::wstring Architecture(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    USHORT machine{}, native{};
    bool ok = process && IsWow64Process2(process, &machine, &native);
    if (process)
        CloseHandle(process);
    if (!ok)
        return L"unknown";
    USHORT effective = machine == IMAGE_FILE_MACHINE_UNKNOWN ? native : machine;
    return effective == IMAGE_FILE_MACHINE_AMD64 ? L"x64" : effective == IMAGE_FILE_MACHINE_I386 ? L"x86" : L"arm64";
}
std::wstring Integrity(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), token{};
    alignas(TOKEN_MANDATORY_LABEL) BYTE buffer[1024]{};
    DWORD size{}, level{};
    bool ok = process && OpenProcessToken(process, TOKEN_QUERY, &token) &&
              GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &size);
    if (ok) {
        PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer)->Label.Sid;
        level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }
    if (token)
        CloseHandle(token);
    if (process)
        CloseHandle(process);
    if (!ok)
        return L"unknown";
    return level < SECURITY_MANDATORY_MEDIUM_RID   ? L"low"
           : level < SECURITY_MANDATORY_HIGH_RID   ? L"medium"
           : level < SECURITY_MANDATORY_SYSTEM_RID ? L"high"
                                                   : L"system";
}
bool Identity(HWND window, DWORD pid, DWORD thread) {
    DWORD current{};
    DWORD currentThread = styler::WindowThreadProcessId(window, &current);
    return IsWindow(window) && current == pid && currentThread == thread;
}
bool Send(HWND window, UINT message, WPARAM param = 0, LPARAM data = 0, DWORD_PTR *result = nullptr) {
    DWORD_PTR ignored{};
    return SendMessageTimeoutW(window, message, param, data, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500,
                               result ? result : &ignored) != 0;
}
void Pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}
struct Snapshot {
    RECT rect{}, client{};
    POINT origin{};
    LONG_PTR style{}, exStyle{};
    HMENU menu{};
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    DWORD cloaked{}, corner{};
    BOOL rendering{};
    UINT dpi{};
    bool hasRendering{}, hasCorner{}, iconic{}, zoomed{};
    BOOL dark{};
    std::array<DWORD, 4> attributes{};
    std::array<bool, 4> available{};
    int hookModules = -1;
};
Snapshot Capture(HWND window) {
    Snapshot s;
    GetWindowRect(window, &s.rect);
    GetClientRect(window, &s.client);
    ClientToScreen(window, &s.origin);
    s.style = GetWindowLongPtrW(window, GWL_STYLE);
    s.exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    s.menu = GetMenu(window);
    GetWindowPlacement(window, &s.placement);
    s.dpi = GetDpiForWindow(window);
    s.iconic = IsIconic(window);
    s.zoomed = IsZoomed(window);
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &s.cloaked, sizeof(s.cloaked));
    s.hasRendering =
        SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &s.rendering, sizeof(s.rendering)));
    s.hasCorner = SUCCEEDED(DwmGetWindowAttribute(window, 33, &s.corner, sizeof(s.corner)));
    DwmGetWindowAttribute(window, 20, &s.dark, sizeof(s.dark));
    for (size_t i = 0; i < s.attributes.size(); ++i)
        s.available[i] =
            SUCCEEDED(DwmGetWindowAttribute(window, styler::VisualAttributes[i], &s.attributes[i], sizeof(DWORD)));
    DWORD pid{};
    styler::WindowThreadProcessId(window, &pid);
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modules != INVALID_HANDLE_VALUE) {
        s.hookModules = 0;
        MODULEENTRY32W entry{sizeof(entry)};
        if (Module32FirstW(modules, &entry))
            do {
                if (std::wstring(entry.szModule).starts_with(L"MacdowsStyleHook"))
                    ++s.hookModules;
            } while (Module32NextW(modules, &entry));
        CloseHandle(modules);
    }
    return s;
}
std::wstring SnapshotJson(const Snapshot &s) {
    std::wstring attributes = L"[";
    for (size_t i = 0; i < s.attributes.size(); ++i) {
        if (i)
            attributes += L",";
        attributes += s.available[i] ? std::to_wstring(s.attributes[i]) : L"null";
    }
    attributes += L"]";
    return L"{\"style\":" + Json(Hex(s.style)) + L",\"exStyle\":" + Json(Hex(s.exStyle)) + L",\"dpi\":" +
           std::to_wstring(s.dpi) + L",\"rect\":" + RectJson(s.rect) + L",\"client\":" + RectJson(s.client) +
           L",\"clientOrigin\":[" + std::to_wstring(s.origin.x) + L"," + std::to_wstring(s.origin.y) + L"]" +
           L",\"minimized\":" + (s.iconic ? L"true" : L"false") + L",\"maximized\":" + (s.zoomed ? L"true" : L"false") +
           L",\"cloaked\":" + std::to_wstring(s.cloaked) + L",\"normalRect\":" +
           RectJson(s.placement.rcNormalPosition) + L",\"dwmNcEnabled\":" + (s.rendering ? L"true" : L"false") +
           L",\"corner\":" + std::to_wstring(s.corner) + L",\"visualAttributes\":" + attributes + L",\"hookModules\":" +
           std::to_wstring(s.hookModules) + L"}";
}
std::vector<HWND> Windows() {
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            if (IsWindowVisible(window) && GetWindowTextLengthW(window))
                reinterpret_cast<std::vector<HWND> *>(param)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}
std::wstring Describe(HWND window, bool direct = false) {
    auto c = direct ? styler::InspectInline(window) : styler::Inspect(window);
    return L"{\"hwnd\":" + Json(Hex(reinterpret_cast<UINT_PTR>(window))) + L",\"pid\":" + std::to_wstring(c.pid) +
           L",\"title\":" + Json(c.title) + L",\"class\":" + Json(c.className) + L",\"process\":" +
           Json(ProcessPath(c.pid)) + L",\"framework\":" + Json(c.framework) + L",\"runtime\":" + Json(c.runtime) +
           L",\"architecture\":" + Json(Architecture(c.pid)) + L",\"integrity\":" + Json(Integrity(c.pid)) +
           L",\"geometry\":" + SnapshotJson(Capture(window)) + L",\"owner\":" +
           Json(Hex(reinterpret_cast<UINT_PTR>(GetPropW(window, styler::OwnerProperty)))) + L",\"eligible\":" +
           (c.supported ? L"true" : L"false") + L",\"adapter\":" + Json(c.adapter) + L",\"reason\":" + Json(c.reason) +
           L"}";
}
std::wstring Inventory(const std::vector<HWND> &windows, bool direct = false) {
    std::wstring out = L"{\"controllerIntegrity\":" + Json(Integrity(GetCurrentProcessId())) + L",\"foreground\":" +
                       Json(Hex(reinterpret_cast<UINT_PTR>(GetForegroundWindow()))) + L",\"windows\":[";
    bool first = true;
    for (HWND window : windows) {
        if (!first)
            out += L",";
        first = false;
        out += Describe(window, direct);
    }
    return out + L"]}\n";
}
struct Check {
    std::wstring name;
    bool ok;
};
struct Outcome {
    std::wstring identity, title, className, status, reason;
    DWORD pid{};
    std::vector<Check> checks;
    std::wstring observations = L"[]", before = L"null", restored = L"null";
    void Test(std::wstring name, bool ok) {
        checks.push_back({std::move(name), ok});
    }
    void Save(const fs::path &base) const {
        std::wstring json = L"{\"hwnd\":" + Json(identity) + L",\"pid\":" + std::to_wstring(pid) + L",\"title\":" +
                            Json(title) + L",\"class\":" + Json(className) + L",\"status\":" + Json(status) +
                            L",\"reason\":" + Json(reason) + L",\"checks\":[";
        std::wstring md = L"### " + title + L" (" + identity + L", PID " + std::to_wstring(pid) + L")\n\n" +
                          L"窗口类：`" + className + L"`  \n结果：**" + status + L"**。" + reason + L"\n\n";
        bool first = true;
        for (const auto &c : checks) {
            if (!first)
                json += L",";
            first = false;
            json += L"{\"name\":" + Json(c.name) + L",\"ok\":" + (c.ok ? L"true" : L"false") + L"}";
            md += (c.ok ? L"- [x] " : L"- [ ] ") + c.name + L"\n";
        }
        json += L"],\"before\":" + before + L",\"observations\":" + observations + L",\"restored\":" + restored + L"}";
        Write(base.wstring() + L".json", json);
        Write(base.wstring() + L".md", md + L"\n");
        Write(base.wstring() + L".status", status);
    }
};
Outcome MakeOutcome(HWND window) {
    auto c = styler::Inspect(window);
    Outcome out;
    out.identity = Hex(reinterpret_cast<UINT_PTR>(window));
    out.pid = c.pid;
    out.title = c.title;
    out.className = c.className;
    out.before = SnapshotJson(Capture(window));
    return out;
}
HWND Controller() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    return CreateWindowExW(0, cls.lpszClassName, L"Window audit worker", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, nullptr,
                           nullptr, cls.hInstance, nullptr);
}
bool SameAttributes(const Snapshot &before, const Snapshot &after) {
    for (size_t i = 0; i < before.attributes.size(); ++i)
        if (before.available[i] && (!after.available[i] || before.attributes[i] != after.attributes[i]))
            return false;
    return true;
}
void Observe(HWND window, const Snapshot &baseline, Outcome &out, const wchar_t *phase, bool first) {
    auto s = Capture(window);
    std::wstring prefix = std::wstring(phase) + L"：";
    out.Test(prefix + L"窗口外框尺寸和位置不变", EqualRect(&baseline.rect, &s.rect));
    out.Test(prefix + L"客户区尺寸和起点不变", EqualRect(&baseline.client, &s.client) &&
                                                   baseline.origin.x == s.origin.x && baseline.origin.y == s.origin.y);
    out.Test(prefix + L"正常位置及最小化/最大化状态不变",
             baseline.iconic == s.iconic && baseline.zoomed == s.zoomed &&
                 EqualRect(&baseline.placement.rcNormalPosition, &s.placement.rcNormalPosition));
    out.Test(prefix + L"样式位和菜单不变",
             baseline.style == s.style && baseline.exStyle == s.exStyle && baseline.menu == s.menu);
    out.Test(prefix + L"DWM 非客户区渲染策略不变", !baseline.hasRendering || baseline.rendering == s.rendering);
    if (baseline.hookModules >= 0 && s.hookModules >= 0)
        out.Test(prefix + L"没有增加注入模块", baseline.hookModules == s.hookModules);
    out.Test(prefix + L"使用原位 DWM 适配器",
             GetPropW(window, styler::AdapterProperty) == reinterpret_cast<HANDLE>(styler::Adapter::Dwm));
    std::array<DWORD, 4> desired{2, baseline.dark ? RGB(78, 78, 82) : RGB(212, 210, 207),
                                 baseline.dark ? RGB(40, 40, 42) : RGB(244, 243, 241),
                                 baseline.dark ? RGB(232, 232, 234) : RGB(58, 57, 55)};
    size_t accepted{};
    for (size_t i = 0; i < desired.size(); ++i)
        if (s.available[i] && s.attributes[i] == desired[i])
            ++accepted;
    out.Test(prefix + L"系统接受并能读回外观属性", accepted > 0);
    if (first)
        out.observations = L"[";
    else
        out.observations += L",";
    out.observations += L"{\"phase\":" + Json(phase) + L",\"geometry\":" + SnapshotJson(s) +
                        L",\"acceptedVisualAttributes\":" + std::to_wstring(accepted) + L",\"changedMask\":" +
                        std::to_wstring(reinterpret_cast<UINT_PTR>(GetPropW(window, styler::VisualChangedProperty))) +
                        L",\"adapter\":3}";
}
int Worker(HWND window, DWORD expectedPid, const fs::path &base) {
    Outcome out = MakeOutcome(window);
    DWORD pid{};
    DWORD thread = styler::WindowThreadProcessId(window, &pid);
    if (pid != expectedPid || !IsWindow(window)) {
        out.status = L"closed";
        out.reason = L"窗口已关闭或 HWND 已复用。";
        out.Save(base);
        return 2;
    }
    auto candidate = styler::Inspect(window);
    if (!candidate.supported || GetPropW(window, styler::OwnerProperty)) {
        out.status = candidate.reason.find(L"完整性") != std::wstring::npos ? L"restricted" : L"excluded";
        out.reason = candidate.supported ? L"已有控制器管理，请先还原。" : candidate.reason;
        out.Save(base);
        return 2;
    }
    Snapshot baseline = Capture(window);
    HWND foreground = GetForegroundWindow();
    HWND controller = Controller();
    if (!controller) {
        out.status = L"failed";
        out.reason = L"不能建立检查控制器。";
        out.Save(base);
        return 1;
    }
    {
        styler::GuardedSession session(controller, true);
        auto result = session.Apply(window);
        out.Test(L"守护进程确认原位接管", result.ok);
        if (result.ok && Identity(window, pid, thread)) {
            Observe(window, baseline, out, L"首次应用", true);
            Pump(1200);
            if (Identity(window, pid, thread))
                Observe(window, baseline, out, L"延后稳定性", false);
            else
                out.Test(L"检查期间 HWND 身份未变", false);
            out.observations += L"]";
        } else
            out.reason = result.message;
        out.Test(L"还原确认", session.Restore(window).ok);
        Pump(150);
        if (Identity(window, pid, thread)) {
            auto s = Capture(window);
            out.restored = SnapshotJson(s);
            out.Test(L"原始 DWM 属性完整还原", SameAttributes(baseline, s));
            out.Test(L"还原后外框和客户区仍不变",
                     EqualRect(&baseline.rect, &s.rect) && EqualRect(&baseline.client, &s.client) &&
                         baseline.origin.x == s.origin.x && baseline.origin.y == s.origin.y);
            out.Test(L"正常位置、窗口状态和样式位仍不变",
                     baseline.iconic == s.iconic && baseline.zoomed == s.zoomed && baseline.style == s.style &&
                         baseline.exStyle == s.exStyle && baseline.menu == s.menu &&
                         EqualRect(&baseline.placement.rcNormalPosition, &s.placement.rcNormalPosition));
            out.Test(L"会话属性已清理",
                     !GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::AdapterProperty) &&
                         !GetPropW(window, styler::RequestProperty) && !GetPropW(window, styler::VisualMaskProperty) &&
                         !GetPropW(window, styler::VisualChangedProperty));
        } else
            out.Test(L"还原时窗口仍存在", false);
    }
    DestroyWindow(controller);
    DWORD activePid{};
    styler::WindowThreadProcessId(GetForegroundWindow(), &activePid);
    out.Test(L"未主动抢占目标前台", GetForegroundWindow() == foreground || activePid != pid);
    out.status =
        std::all_of(out.checks.begin(), out.checks.end(), [](const Check &c) { return c.ok; }) ? L"passed" : L"failed";
    out.reason +=
        L" 仅调整 DWM 外观属性；没有注入、加标题栏、改尺寸或显示最小化窗口。自绘标题控件保留；系统接受属性不等于 GPU "
        L"最终画面认证。";
    out.Save(base);
    return out.status == L"passed" ? 0 : 1;
}
int Batch(const std::vector<HWND> &windows, const fs::path &directory) {
    fs::create_directories(directory);
    Write(directory / L"inventory-before.json", Inventory(windows));
    std::ofstream json(directory / L"report.json", std::ios::binary), md(directory / L"report.md", std::ios::binary);
    json << "{\"windows\":[";
    md << Utf8(L"# "
               L"当前桌面逐窗口检查\n\n原位模式：检查系统外观属性、尺寸/客户区/状态/"
               L"样式保持、无新增注入模块和还原。最小化窗口保持最小化；不添加标题栏、不显示或移动窗口。自绘标题控件保留"
               L"，未采集 GPU 最终像素。\n\n");
    int passed{}, failed{}, skipped{};
    bool first = true;
    for (HWND window : windows) {
        auto out = MakeOutcome(window);
        DWORD pid{};
        styler::WindowThreadProcessId(window, &pid);
        fs::path base = directory / out.identity;
        fs::remove(base.wstring() + L".status");
        std::wstring exe = styler::ExecutableDirectory() + L"\\WindowAudit.exe";
        std::wstring cmd = L"\"" + exe + L"\" --worker " + out.identity + L" " + std::to_wstring(pid) + L" \"" +
                           base.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process)) {
            CloseHandle(process.hThread);
            DWORD wait = WaitForSingleObject(process.hProcess, 25000), code{};
            if (wait == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 99);
                WaitForSingleObject(process.hProcess, 2000);
            }
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hProcess);
            if (wait != WAIT_OBJECT_0 || code > 2 || !fs::exists(base.wstring() + L".status")) {
                Pump(1700);
                // The independent appearance host restores on worker death; never move the target.
                out.status = L"failed";
                out.reason = L"检查进程超时或异常退出；已等待目标自动还原，需核对原程序状态。";
                out.Test(L"异常后没有残留样式所有者", !GetPropW(window, styler::OwnerProperty));
                out.Save(base);
            }
        } else {
            out.status = L"failed";
            out.reason = L"无法创建独立检查进程。";
            out.Save(base);
        }
        auto status = Read(base.wstring() + L".status");
        if (status == "passed")
            ++passed;
        else if (status == "failed")
            ++failed;
        else
            ++skipped;
        std::cout << Utf8(out.identity + L" " + out.title) << " : " << status << std::endl;
        if (!first)
            json << ",";
        first = false;
        json << Read(base.wstring() + L".json");
        md << Read(base.wstring() + L".md");
        json.flush();
        md.flush();
    }
    json << "],\"passed\":" << passed << ",\"failed\":" << failed << ",\"unverifiedOrExcluded\":" << skipped << "}\n";
    auto summary = L"通过 " + std::to_wstring(passed) + L"；失败 " + std::to_wstring(failed) +
                   L"；权限限制/排除/未验证 " + std::to_wstring(skipped) + L"。";
    md << Utf8(L"## 统计\n\n" + summary + L"\n");
    Write(directory / L"summary.txt", summary);
    Write(directory / L"inventory-after.json", Inventory(Windows()));
    std::cout << Utf8(summary) << std::endl;
    return failed ? 1 : 0;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        if (argc == 5 && wcscmp(argv[1], L"--worker") == 0) {
            HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 0)));
            return Worker(window, wcstoul(argv[3], nullptr, 10), argv[4]);
        }
        if (argc == 3 && wcscmp(argv[1], L"--all") == 0)
            return Batch(Windows(), fs::absolute(argv[2]));
        if (argc == 3 && wcscmp(argv[1], L"--elevated") == 0) {
            auto windows = Windows();
            std::erase_if(windows, [](HWND w) {
                DWORD pid{};
                styler::WindowThreadProcessId(w, &pid);
                return Integrity(pid) != L"high";
            });
            return Batch(windows, fs::absolute(argv[2]));
        }
        if (argc == 4 && wcscmp(argv[1], L"--window") == 0)
            return Batch({reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2], nullptr, 0)))},
                         fs::absolute(argv[3]));
        if (argc == 3 && wcscmp(argv[1], L"--direct-inventory") == 0)
            Write(argv[2], Inventory(Windows(), true));
        else if (argc == 3 && wcscmp(argv[1], L"--inventory") == 0)
            Write(argv[2], Inventory(Windows()));
        else
            std::cout << Utf8(Inventory(Windows()));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 3;
    }
}
