#pragma once
#include "protocol.h"
#include <map>
#include <string>
#include <vector>

namespace styler {
struct Candidate {
    HWND window{};
    DWORD pid{};
    std::wstring title;
    std::wstring className;
    std::wstring framework;
    std::wstring runtime;
    std::wstring adapter;
    std::wstring reason;
    Adapter adapterMode = Adapter::Native;
    bool supported{};
    UINT visualMask{};
};
struct Result {
    bool ok{};
    std::wstring message;
};
std::wstring ExecutableDirectory();
std::wstring WindowTitle(HWND window);
Candidate Inspect(HWND window, bool forInjection = false, bool allowOtherArchitecture = false,
                  HWND embeddedFrame = nullptr);
std::vector<Candidate> Enumerate();

class Session {
  public:
    explicit Session(HWND controller, bool bootstrapOnly = false);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Result Apply(HWND window, Adapter requested = static_cast<Adapter>(0));
    Result ApplyEmbedded(HWND core, HWND frame);
    Result Restore(HWND window);
    Result RestoreAll();
    bool Owns(HWND window) const;
    size_t Count() const;

  private:
    HWND controller_{};
    bool bootstrapOnly_{};
    DWORD HookSource(DWORD pid, HMODULE &module, HOOKPROC &procedure);
    std::map<std::wstring, HMODULE> modules_;
    struct Attachment {
        HHOOK hook{};
        DWORD pid{};
        DWORD thread{};
    };
    std::map<HWND, Attachment> attachments_;
};
} // namespace styler
