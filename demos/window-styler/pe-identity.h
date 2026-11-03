#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
namespace styler {
inline bool MatchesPdb(HMODULE handle, GUID const &id, DWORD age = 1) {
    if (!handle)
        return false;
    auto base = reinterpret_cast<const BYTE *>(handle);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
        return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
        return false;
    auto image = nt->OptionalHeader.SizeOfImage;
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (uint64_t(directory.VirtualAddress) + directory.Size > image)
        return false;
    for (size_t i = 0; i + sizeof(IMAGE_DEBUG_DIRECTORY) <= directory.Size; i += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        auto entry = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + directory.VirtualAddress + i);
        if (entry->Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry->SizeOfData < 24 ||
            uint64_t(entry->AddressOfRawData) + entry->SizeOfData > image)
            continue;
        auto cv = base + entry->AddressOfRawData;
        DWORD actualAge{};
        memcpy(&actualAge, cv + 20, 4);
        if (memcmp(cv, "RSDS", 4) == 0 && memcmp(cv + 4, &id, sizeof(GUID)) == 0 && actualAge == age)
            return true;
    }
    return false;
}
} // namespace styler
