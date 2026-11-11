#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace styler {
// Managed PE32 assemblies can be mapped in a PE64 process. Their import
// directories and thunk widths must never be interpreted as native PE64.
class NativeImportImage {
    BYTE *base;
    size_t size;
    IMAGE_NT_HEADERS headers{};
    bool valid{};

    bool Read(size_t offset, void *value, size_t bytes) const {
        SIZE_T copied{};
        return offset <= size && bytes <= size - offset &&
               ReadProcessMemory(GetCurrentProcess(), base + offset, value, bytes, &copied) && copied == bytes;
    }

    template <class Visitor> void Thunks(DWORD iat, DWORD names, Visitor &visit) const {
        if (!iat || !names || iat >= size || names >= size || iat % alignof(void *))
            return;
        const size_t count =
            std::min((size - iat) / sizeof(IMAGE_THUNK_DATA), (size - names) / sizeof(IMAGE_THUNK_DATA));
        for (size_t i = 0; i < count; ++i) {
            IMAGE_THUNK_DATA thunk{}, target{};
            if (!Read(size_t(names) + i * sizeof(thunk), &thunk, sizeof(thunk)) || !thunk.u1.AddressOfData ||
                !Read(size_t(iat) + i * sizeof(target), &target, sizeof(target)))
                return;
            if (IMAGE_SNAP_BY_ORDINAL(thunk.u1.Ordinal))
                continue;
            if (thunk.u1.AddressOfData >= size || size - size_t(thunk.u1.AddressOfData) <= sizeof(WORD))
                return;
            const auto offset = size_t(thunk.u1.AddressOfData) + sizeof(WORD);
            std::array<char, 256> symbol{};
            const auto length = std::min(symbol.size(), size - offset);
            if (!Read(offset, symbol.data(), length))
                return;
            const auto end = static_cast<const char *>(memchr(symbol.data(), 0, length));
            if (!end)
                return;
            visit(std::string_view(symbol.data(), size_t(end - symbol.data())),
                  reinterpret_cast<void **>(base + iat + i * sizeof(target)));
        }
    }

  public:
    NativeImportImage(HMODULE module, size_t imageSize) : base(reinterpret_cast<BYTE *>(module)), size(imageSize) {
        IMAGE_DOS_HEADER dos{};
        if (!Read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
            !Read(size_t(dos.e_lfanew), &headers, sizeof(headers)) || headers.Signature != IMAGE_NT_SIGNATURE)
            return;
#ifdef _WIN64
        constexpr WORD magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        constexpr WORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
        constexpr WORD magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        constexpr WORD machine = IMAGE_FILE_MACHINE_I386;
#endif
        valid = headers.OptionalHeader.Magic == magic && headers.FileHeader.Machine == machine &&
                headers.FileHeader.SizeOfOptionalHeader >= sizeof(headers.OptionalHeader) &&
                headers.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT &&
                headers.OptionalHeader.SizeOfImage != 0 && headers.OptionalHeader.SizeOfImage <= size;
        if (valid)
            size = headers.OptionalHeader.SizeOfImage;
    }

    template <class Visitor> void Visit(Visitor visit) const {
        if (!valid)
            return;
        auto imports = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imports.VirtualAddress && imports.VirtualAddress <= size && imports.Size <= size - imports.VirtualAddress)
            for (size_t i = 0; i < imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++i) {
                IMAGE_IMPORT_DESCRIPTOR entry{};
                if (!Read(size_t(imports.VirtualAddress) + i * sizeof(entry), &entry, sizeof(entry)) || !entry.Name)
                    break;
                // An absent original table means FirstThunk already contains
                // resolved addresses; those are never symbol-name RVAs.
                Thunks(entry.FirstThunk, entry.OriginalFirstThunk, visit);
            }
        struct Delay {
            DWORD attributes, name, handle, iat, names, bound, unload, time;
        };
        auto delay = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (delay.VirtualAddress && delay.VirtualAddress <= size && delay.Size <= size - delay.VirtualAddress)
            for (size_t i = 0; i < delay.Size / sizeof(Delay); ++i) {
                Delay entry{};
                if (!Read(size_t(delay.VirtualAddress) + i * sizeof(entry), &entry, sizeof(entry)) || !entry.name)
                    break;
                if (entry.attributes & 1)
                    Thunks(entry.iat, entry.names, visit);
            }
    }
};
} // namespace styler
