#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <array>
#include <vector>
#include <mutex>
#include <cstring>
#include <commctrl.h>
#include "nonclient-frame.h"

namespace styler::style_view {
inline constexpr wchar_t StyleProperty[] = L"Macdows.WindowStyler.NetUI.SystemMenu.v1";
// Optional extended mask for frameworks that let DWM draw minimize/maximize
// independently of WS_SYSMENU. The stored value is the requested box bits + 1.
inline constexpr wchar_t CaptionBoxesProperty[] = L"Macdows.WindowStyler.NetUI.CaptionBoxes.v1";
inline constexpr wchar_t PreviousCaptionBoxesProperty[] = L"Macdows.WindowStyler.NetUI.PreviousCaptionBoxes.v1";
inline constexpr wchar_t RestoreCaptionBoxesProperty[] = L"Macdows.WindowStyler.NetUI.RestoreCaptionBoxes.v1";
inline constexpr LONG_PTR CaptionBoxMask = WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
inline constexpr const wchar_t *BoundsProperties[] = {
    L"Macdows.WindowStyler.NetUI.ButtonWidth", L"Macdows.WindowStyler.NetUI.ButtonTop",
    L"Macdows.WindowStyler.NetUI.ButtonRight", L"Macdows.WindowStyler.NetUI.ButtonHeight"};
inline constexpr wchar_t PreviousMenuProperty[] = L"Macdows.WindowStyler.NativeMenu.Previous";
inline constexpr wchar_t InternalMenuProperty[] = L"Macdows.WindowStyler.NativeMenu.Internal";
inline LONG_PTR MenuValue(LONG_PTR value, HANDLE requested) {
    return requested == reinterpret_cast<HANDLE>(1) ? value | WS_SYSMENU : value & ~WS_SYSMENU;
}
inline LONG_PTR CaptionBoxesValue(LONG_PTR value, HANDLE boxes) {
    if (boxes)
        value = (value & ~CaptionBoxMask) | ((LONG_PTR(boxes) - 1) & CaptionBoxMask);
    return value;
}
inline LONG_PTR Style(HWND w, int n, LONG_PTR value) {
    auto requested = GetPropW(w, StyleProperty);
    if (n == GWL_STYLE) {
        if (requested)
            value = MenuValue(value, requested);
        value = CaptionBoxesValue(value, GetPropW(w, CaptionBoxesProperty));
    }
    return value;
}
// The host owns its style, including changes made while a lease is active.
// Virtualize its view, but strip the native buttons from the REAL changing
// STYLESTRUCT before user32 commits it. A notification-only copy is too late.
inline LRESULT MenuStyleMessage(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    auto actual = reinterpret_cast<STYLESTRUCT *>(lp);
    auto requested = GetPropW(w, StyleProperty);
    bool internal = GetPropW(w, InternalMenuProperty) != nullptr;
    auto previous = GetPropW(w, PreviousMenuProperty);
    if (!previous)
        previous = requested;
    auto boxes = GetPropW(w, CaptionBoxesProperty);
    auto previousBoxes = GetPropW(w, PreviousCaptionBoxesProperty);
    STYLESTRUCT host{DWORD(CaptionBoxesValue(MenuValue(actual->styleOld, m == WM_STYLECHANGED ? previous : requested),
                                             m == WM_STYLECHANGED && previousBoxes ? previousBoxes : boxes)),
                     actual->styleNew};
    if (m == WM_STYLECHANGED || internal)
        host.styleNew = DWORD(CaptionBoxesValue(MenuValue(host.styleNew, requested), boxes));
    auto result = DefSubclassProc(w, m, wp, reinterpret_cast<LPARAM>(&host));
    if (m == WM_STYLECHANGING && GetPropW(w, StyleProperty)) {
        SetPropW(w, PreviousMenuProperty, requested);
        if (!internal)
            SetPropW(w, StyleProperty, reinterpret_cast<HANDLE>(INT_PTR((host.styleNew & WS_SYSMENU) ? 1 : 2)));
        actual->styleNew = host.styleNew & ~WS_SYSMENU;
        if (boxes) {
            SetPropW(w, PreviousCaptionBoxesProperty, boxes);
            if (!internal)
                SetPropW(w, CaptionBoxesProperty,
                         reinterpret_cast<HANDLE>(LONG_PTR((host.styleNew & CaptionBoxMask) + 1)));
            if (!GetPropW(w, RestoreCaptionBoxesProperty))
                actual->styleNew &= ~DWORD(CaptionBoxMask);
        }
    }
    return result;
}
inline void HideMenu(HWND w) {
    SetPropW(w, InternalMenuProperty, reinterpret_cast<HANDLE>(1));
    NonclientFrame::SystemMenu(w, false);
    RemovePropW(w, InternalMenuProperty);
}
inline void RestoreMenu(HWND w, bool original) {
    auto requested = GetPropW(w, StyleProperty);
    if (auto boxes = GetPropW(w, CaptionBoxesProperty)) {
        SetPropW(w, InternalMenuProperty, reinterpret_cast<HANDLE>(1));
        SetPropW(w, RestoreCaptionBoxesProperty, reinterpret_cast<HANDLE>(1));
        auto actual = GetWindowLongPtrW(w, GWL_STYLE);
        SetWindowLongPtrW(w, GWL_STYLE, (actual & ~CaptionBoxMask) | ((LONG_PTR(boxes) - 1) & CaptionBoxMask));
        RemovePropW(w, RestoreCaptionBoxesProperty);
        RemovePropW(w, CaptionBoxesProperty);
    }
    RemovePropW(w, StyleProperty);
    RemovePropW(w, PreviousMenuProperty);
    RemovePropW(w, PreviousCaptionBoxesProperty);
    RemovePropW(w, InternalMenuProperty);
    NonclientFrame::SystemMenu(w, requested ? requested == reinterpret_cast<HANDLE>(1) : original);
}
inline LONG_PTR WINAPI LongPtrW(HWND w, int n) {
    return Style(w, n, GetWindowLongPtrW(w, n));
}
inline LONG_PTR WINAPI LongPtrA(HWND w, int n) {
    return Style(w, n, GetWindowLongPtrA(w, n));
}
inline LONG WINAPI LongW(HWND w, int n) {
    return LONG(Style(w, n, GetWindowLongW(w, n)));
}
inline LONG WINAPI LongA(HWND w, int n) {
    return LONG(Style(w, n, GetWindowLongA(w, n)));
}
inline BOOL WINAPI Info(HWND w, PWINDOWINFO i) {
    auto ok = GetWindowInfo(w, i);
    if (ok)
        i->dwStyle = DWORD(Style(w, GWL_STYLE, i->dwStyle));
    return ok;
}
inline HRESULT WINAPI Dwm(HWND w, DWORD n, PVOID value, DWORD size) {
    auto hr = DwmGetWindowAttribute(w, n, value, size);
    if (n == DWMWA_CAPTION_BUTTON_BOUNDS && value && size >= sizeof(RECT) && GetPropW(w, StyleProperty)) {
        int metrics[4]{};
        bool valid = true;
        for (int i = 0; i < 4; i++) {
            auto p = GetPropW(w, BoundsProperties[i]);
            if (!p)
                valid = false;
            metrics[i] = int(INT_PTR(p)) - 1;
        }
        RECT outer{};
        if (valid && GetWindowRect(w, &outer)) {
            auto r = static_cast<RECT *>(value);
            r->right = outer.right - outer.left - metrics[2];
            r->left = r->right - metrics[0];
            r->top = metrics[1];
            r->bottom = r->top + metrics[3];
            return S_OK;
        }
    }
    return hr;
}
struct Entry {
    const char *name;
    void *replacement;
    void *original;
};
struct Slot {
    void **address;
    void *replacement;
    void *original;
};
inline std::mutex mutex;
inline unsigned users{};
inline std::vector<Slot> slots;
inline std::vector<HMODULE> modules;
inline std::array<Entry, 6> Entries() {
    return {
        {{"GetWindowLongPtrW", reinterpret_cast<void *>(&LongPtrW), reinterpret_cast<void *>(&GetWindowLongPtrW)},
         {"GetWindowLongPtrA", reinterpret_cast<void *>(&LongPtrA), reinterpret_cast<void *>(&GetWindowLongPtrA)},
         {"GetWindowLongW", reinterpret_cast<void *>(&LongW), reinterpret_cast<void *>(&GetWindowLongW)},
         {"GetWindowLongA", reinterpret_cast<void *>(&LongA), reinterpret_cast<void *>(&GetWindowLongA)},
         {"GetWindowInfo", reinterpret_cast<void *>(&Info), reinterpret_cast<void *>(&GetWindowInfo)},
         {"DwmGetWindowAttribute", reinterpret_cast<void *>(&Dwm), reinterpret_cast<void *>(&DwmGetWindowAttribute)}}};
}
inline void Restore() {
    for (auto &s : slots) {
        DWORD old{};
        if (VirtualProtect(s.address, sizeof(void *), PAGE_READWRITE, &old)) {
            InterlockedCompareExchangePointer(s.address, s.original, s.replacement);
            DWORD ignored{};
            VirtualProtect(s.address, sizeof(void *), old, &ignored);
        }
    }
    slots.clear();
    for (auto m : modules)
        FreeLibrary(m);
    modules.clear();
}
inline bool AcquireReads() {
    std::scoped_lock lock(mutex);
    if (users) {
        ++users;
        return true;
    }
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&AcquireReads), &self);
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W item{sizeof(item)};
    auto entries = Entries();
    bool ok = true;
    for (BOOL found = Module32FirstW(snapshot, &item); found; found = Module32NextW(snapshot, &item)) {
        if (item.hModule == self)
            continue;
        HMODULE retained{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(item.modBaseAddr),
                                &retained))
            continue;
        auto base = reinterpret_cast<BYTE *>(retained);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        auto count = slots.size();
        auto patch = [&](DWORD iat, DWORD names) {
            if (!iat || !names)
                return;
            auto name = reinterpret_cast<IMAGE_THUNK_DATA *>(base + names);
            auto slot = reinterpret_cast<IMAGE_THUNK_DATA *>(base + iat);
            for (; name->u1.AddressOfData; ++name, ++slot) {
                if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal))
                    continue;
                auto symbol = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + name->u1.AddressOfData)->Name;
                for (auto &e : entries)
                    if (strcmp(reinterpret_cast<char *>(symbol), e.name) == 0) {
                        auto address = reinterpret_cast<void **>(&slot->u1.Function);
                        // Do not replace unresolved delay thunks or another component's hook.
                        if (*address != e.original)
                            continue;
                        DWORD old{};
                        if (!VirtualProtect(address, sizeof(void *), PAGE_READWRITE, &old)) {
                            ok = false;
                            continue;
                        }
                        if (InterlockedCompareExchangePointer(address, e.replacement, e.original) == e.original)
                            slots.push_back({address, e.replacement, e.original});
                        DWORD ignored{};
                        VirtualProtect(address, sizeof(void *), old, &ignored);
                    }
            }
        };
        auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imports.VirtualAddress)
            for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress); d->Name; ++d)
                patch(d->FirstThunk, d->OriginalFirstThunk);
        struct Delay {
            DWORD attributes, name, handle, iat, names, bound, unload, time;
        };
        auto delay = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (delay.VirtualAddress)
            for (auto d = reinterpret_cast<Delay *>(base + delay.VirtualAddress); d->name; ++d)
                if (d->attributes & 1)
                    patch(d->iat, d->names);
        if (slots.size() != count)
            modules.push_back(retained);
        else
            FreeLibrary(retained);
    }
    CloseHandle(snapshot);
    if (!ok || slots.empty()) {
        Restore();
        return false;
    }
    users = 1;
    return true;
}
inline void ReleaseReads() {
    std::scoped_lock lock(mutex);
    if (users && !--users)
        Restore();
}
} // namespace styler::style_view
