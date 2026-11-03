#pragma once
#include <cwchar>
#include <initializer_list>

namespace txcaption {
enum class Intent {
    Unknown = -1,
    Close,
    Minimize,
    MaximizeOrRestore
};
// Original action hints, not business-control names or substrings. This is
// intent evidence only; a hint does not prove the callback's effects.
inline Intent Hint(const wchar_t *text) {
    if (!text)
        return Intent::Unknown;
    for (auto word : {L"Minimize", L"\x6700\x5c0f\x5316"})
        if (!_wcsicmp(text, word))
            return Intent::Minimize;
    for (auto word : {L"Maximize", L"Restore", L"Restore Down", L"\x6700\x5927\x5316", L"\x8fd8\x539f",
                      L"\x5411\x4e0b\x8fd8\x539f", L"\x9084\x539f"})
        if (!_wcsicmp(text, word))
            return Intent::MaximizeOrRestore;
    for (auto word : {L"Close", L"\x5173\x95ed", L"\x95dc\x9589"})
        if (!_wcsicmp(text, word))
            return Intent::Close;
    return Intent::Unknown;
}
inline Intent Hint(const wchar_t *text, size_t length) {
    if (!text || length > 128 || std::wmemchr(text, L'\0', length) || text[length] != L'\0')
        return Intent::Unknown;
    return Hint(text);
}
inline const char *Name(Intent intent) {
    switch (intent) {
    case Intent::Close:
        return "close-intent";
    case Intent::Minimize:
        return "minimize-intent";
    case Intent::MaximizeOrRestore:
        return "maximize-or-restore-intent";
    default:
        return "unknown";
    }
}
} // namespace txcaption
