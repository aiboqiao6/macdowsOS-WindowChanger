#pragma once
#include "pe-identity.h"
#include <wrl/client.h>
#include <vector>

// Framework ABI, never a host application ABI. Public PDB identity and exact
// entry bytes are mandatory; an unknown framework build declines this path.
// Source signature: Diagnostics::DiagnosticsInterop::GetXamlIslandRoots(
//     std::vector<Microsoft::WRL::ComPtr<IInspectable>>&).
// This discovers existing sources without creating a diagnostics overlay.
namespace xaml_abi {
using Objects = std::vector<Microsoft::WRL::ComPtr<::IInspectable>>;
inline HMODULE Runtime() {
    auto module = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    constexpr GUID id{0xa5e5662d, 0xb273, 0x6140, {0xb6, 0x33, 0xbc, 0x2d, 0x19, 0x8b, 0x5a, 0x28}};
    if (!styler::MatchesPdb(module, id))
        return nullptr;
    auto bytes = reinterpret_cast<const BYTE *>(module);
    constexpr BYTE entry[]{0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57,
                           0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48};
    return std::memcmp(bytes + 0x86307c, entry, sizeof(entry)) == 0 ? module : nullptr;
}
inline bool Enumerate(Objects &objects) {
    auto module = Runtime();
    if (!module)
        return false;
    auto bytes = reinterpret_cast<const BYTE *>(module);
    auto current = reinterpret_cast<void *(*)()>(bytes + 0xe5fc4);
    if (!current())
        return false; // No XAML dispatcher on this GUI thread.
    auto get = reinterpret_cast<HRESULT (*)(Objects &)>(bytes + 0x86307c);
    return SUCCEEDED(get(objects));
}
} // namespace xaml_abi
