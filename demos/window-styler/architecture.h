#pragma once
#include <windows.h>
#include <string>
namespace styler {
bool DifferentArchitecture(HWND window);
std::wstring PeerExecutable(HWND window, const std::wstring &executable);
} // namespace styler
