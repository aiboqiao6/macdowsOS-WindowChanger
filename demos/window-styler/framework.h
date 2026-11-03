#pragma once
#include <windows.h>
#include <string>
namespace styler {
struct FrameworkInfo {
    std::wstring id, name, runtime;
};
FrameworkInfo DetectFramework(HWND window);
} // namespace styler
