#pragma once
#include "protocol.h"

namespace styler::chrome {
// Call only from the HWND's owning GUI thread. All rendering stays in the
// non-client area; application controls, menus and client painting are retained.
DWORD Attach(HWND window, HWND controller, Adapter adapter = Adapter::Native);
bool Detach(HWND window, HWND controller);
} // namespace styler::chrome
