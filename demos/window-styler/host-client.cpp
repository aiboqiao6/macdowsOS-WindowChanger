#include "host-client.h"
#include "inline.h"
#include "architecture.h"

namespace styler {
GuardedSession::GuardedSession(HWND controller, bool visualOnly, const std::wstring &helper)
    : controller_(controller), direct_(!visualOnly), remote_(!helper.empty()) {
    auto exe = helper.empty() ? ExecutableDirectory() + L"\\WindowStyleHost.exe" : helper;
    auto line = L"\"" + exe + L"\" --owner " + std::to_wstring(reinterpret_cast<UINT_PTR>(controller));
    if (visualOnly)
        line += L" --visual-only";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &info))
        return;
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    processId_ = info.dwProcessId;
    ULONGLONG deadline = GetTickCount64() + 3000;
    do {
        struct Find {
            DWORD pid;
            HWND parent;
            HWND found;
        } find{processId_, controller_, nullptr};
        EnumWindows(
            [](HWND w, LPARAM data) -> BOOL {
                auto &f = *reinterpret_cast<Find *>(data);
                DWORD pid{};
                GetWindowThreadProcessId(w, &pid);
                if (pid == f.pid && GetPropW(w, HostParentProperty) == f.parent) {
                    f.found = w;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&find));
        if (find.found) {
            host_ = find.found;
            return;
        }
        if (WaitForSingleObject(process_, 15) != WAIT_TIMEOUT)
            return;
    } while (GetTickCount64() < deadline);
}
LRESULT GuardedSession::Request(HostCommand command, HWND target) const {
    if (!host_ || !process_ || WaitForSingleObject(process_, 0) != WAIT_TIMEOUT)
        return -1;
    DWORD_PTR result{};
    if (!SendMessageTimeoutW(host_, HostMessage(), static_cast<WPARAM>(command), reinterpret_cast<LPARAM>(target),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 2500, &result))
        return -1;
    return static_cast<LRESULT>(result);
}
GuardedSession::~GuardedSession() {
    Request(HostCommand::Stop);
    if (process_)
        CloseHandle(process_); // host also watches our HWND/process
}
Result GuardedSession::Apply(HWND window) {
    if (direct_ && !remote_ && DifferentArchitecture(window)) {
        auto helper = PeerExecutable(window, L"WindowStyleHost.exe");
        if (helper.empty())
            return {false, L"缺少匹配目标位数的注入辅助进程，请使用完整 x64 + x86 包。"};
        if (!peer_)
            peer_ = std::make_unique<GuardedSession>(controller_, false, helper);
        return peer_->Apply(window);
    }
    auto c = direct_ && !remote_ ? InspectInline(window) : Inspect(window);
    if (!c.supported)
        return {false, c.reason};
    if (Request(HostCommand::Apply, window) != 1) {
        auto reason = WindowTitle(host_);
        return {false, reason.empty() ? L"目标进程未确认布局附加，请查看权限、运行时和布局检查结果。" : reason};
    }
    if (direct_)
        return {true, GetPropW(window, InlineProperty) ? L"已直接注入目标进程，修改原顶栏控件布局。"
                                                       : L"已在目标进程内重绘原标题区域。"};
    auto mask = reinterpret_cast<UINT_PTR>(GetPropW(window, VisualMaskProperty));
    auto changes = reinterpret_cast<UINT_PTR>(GetPropW(window, VisualChangedProperty));
    return {true, mask == 1 ? changes ? L"已接管：仅调整圆角，无法安全读取的配色保留。"
                                      : L"已接管：原本已是圆角，未新增外观改动；自绘内容和配色保留。"
                            : L"已原位接管；保留原始尺寸、客户区与标题控件。"};
}
Result GuardedSession::Restore(HWND window) {
    if (peer_ && DifferentArchitecture(window))
        return peer_->Restore(window);
    bool ok = Request(HostCommand::Restore, window) == 1;
    return {ok, ok ? L"已还原原始外观。" : L"还原未确认，守护进程将重试。"};
}
Result GuardedSession::RestoreAll() {
    bool ok = Request(HostCommand::RestoreAll) == 1;
    if (peer_)
        ok = peer_->RestoreAll().ok && ok;
    return {ok, ok ? L"所有外观已还原。" : L"部分外观等待还原。"};
}
bool GuardedSession::Owns(HWND window) const {
    return host_ && GetPropW(window, OwnerProperty) == host_ || peer_ && peer_->Owns(window);
}
size_t GuardedSession::Count() const {
    auto value = Request(HostCommand::Count);
    return (value < 0 ? 0 : static_cast<size_t>(value)) + (peer_ ? peer_->Count() : 0);
}
} // namespace styler
