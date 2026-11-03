#pragma once
#include "protocol.h"

// Stable C ABI. The injector calls both functions on the target window's GUI
// thread. Pointers here are local to that process, never cross-process messages.
namespace styler {
inline constexpr DWORD AdapterAbiV1 = 0x00010000;
inline constexpr UINT_PTR PluginAdapterBase = 256;
struct AdapterContextV1 {
    DWORD size = sizeof(AdapterContextV1);
    DWORD abi = AdapterAbiV1;
    HWND window{};
    HWND owner{};
    UINT_PTR adapterId{};
};
using AdapterProbeV1 = DWORD(WINAPI *)(const AdapterContextV1 *);
using AdapterAttachV1 = DWORD(WINAPI *)(const AdapterContextV1 *);
inline bool ValidContext(const AdapterContextV1 *c) {
    return c && c->size == sizeof(AdapterContextV1) && c->abi == AdapterAbiV1 && c->adapterId >= PluginAdapterBase &&
           IsWindow(c->window) && IsWindow(c->owner);
}
} // namespace styler

// Required exports:
// DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1*);
// DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1*);
// Probe must be read-only. Unsupported layouts return ERROR_NOT_SUPPORTED.
// Attach saves originals before changing layout and publishes OwnerProperty on
// success. It must implement QueryMessage/DetachMessage/DiagnosticMessage, reject
// nested detach with TransitionBusy, and restore if owner HWND/process dies.
// All visuals must belong to the existing target rendering tree. Top-level
// popups, follow timers and added nonclient caption bands are forbidden.
