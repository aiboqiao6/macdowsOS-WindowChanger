#pragma once
#include "session.h"
#include <memory>

namespace styler {
inline constexpr wchar_t HostClass[] = L"Macdows.WindowStyler.VisualHost.v2";
inline UINT HostMessage() {
    static UINT id = RegisterWindowMessageW(L"Macdows.WindowStyler.Host.v2");
    return id;
}
enum class HostCommand : UINT {
    Apply = 1,
    Restore,
    RestoreAll,
    Count,
    Stop,
    Query
};
inline constexpr wchar_t HostParentProperty[] = L"Macdows.WindowStyler.Host.Parent.v2";
// The hidden helper owns all originals; it restores them if this process dies.
class GuardedSession {
  public:
    explicit GuardedSession(HWND controller, bool visualOnly = false, const std::wstring &helper = {});
    ~GuardedSession();
    GuardedSession(const GuardedSession &) = delete;
    GuardedSession &operator=(const GuardedSession &) = delete;
    Result Apply(HWND window);
    Result Restore(HWND window);
    Result RestoreAll();
    bool Owns(HWND window) const;
    size_t Count() const;
    DWORD HostPid() const {
        return processId_;
    }

  private:
    LRESULT Request(HostCommand command, HWND target = nullptr) const;
    HWND controller_{}, host_{};
    HANDLE process_{};
    DWORD processId_{};
    bool direct_{};
    bool remote_{};
    std::unique_ptr<GuardedSession> peer_;
};
} // namespace styler
