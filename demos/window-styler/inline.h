#pragma once
#include "session.h"

namespace styler {
Candidate InspectInline(HWND window);
std::vector<Candidate> EnumerateInline();
class InlineSession {
  public:
    explicit InlineSession(HWND owner) : session_(owner) {}
    ~InlineSession() {
        RestoreAll();
    }
    Result Apply(HWND window);
    Result Restore(HWND window);
    Result RestoreAll();
    bool Owns(HWND window) const {
        return session_.Owns(window);
    }
    size_t Count() const {
        size_t count = session_.Count();
        for (auto const &pair : embedded_)
            if (session_.Owns(pair.second) && count)
                --count;
        return count;
    }

  private:
    Session session_;
    std::map<HWND, HWND> embedded_;
};
} // namespace styler
