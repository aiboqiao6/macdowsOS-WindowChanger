#pragma once
// Research only. A matching build identity is necessary but not sufficient:
// also check export ordinals, function bytes and immutable property names.
#include "../../pe-identity.h"
#include <cstring>

inline bool Office21048VerifyLoaded() {
    const struct Identity {
        const wchar_t *name;
        GUID id;
    } identities[]{
        {L"mso40uiwin32client.dll", {0x3e4b8c5a, 0xb16a, 0x4de5, {0xb0, 0x63, 0x33, 0x8e, 0xdf, 0x2b, 0xb5, 0x46}}},
        {L"mso98win32client.dll", {0x587d5bed, 0x5243, 0x4e42, {0x9e, 0x0e, 0x87, 0xc8, 0x7c, 0x6a, 0x4e, 0x20}}},
        {L"mso.dll", {0x6ff021ed, 0x7c7b, 0x4196, {0xbc, 0x82, 0x49, 0xf0, 0x56, 0xff, 0x3c, 0xf8}}}};
    for (const auto &identity : identities)
        if (!styler::MatchesPdb(GetModuleHandleW(identity.name), identity.id, 2))
            return false;
    auto module = GetModuleHandleW(identities[0].name);
    auto base = reinterpret_cast<const BYTE *>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const size_t size = nt->OptionalHeader.SizeOfImage;
    auto read = [&](size_t rva, void *destination, size_t length) {
        SIZE_T actual{};
        return rva < size && length <= size - rva &&
               ReadProcessMemory(GetCurrentProcess(), base + rva, destination, length, &actual) && actual == length;
    };
    const struct Entry {
        WORD ordinal;
        size_t rva;
        const char *bytes;
        size_t length;
    } entries[]{{365, 0x333d0, "\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x83\x64\x24\x48\x00", 16},
                {1013, 0x49ee0, "\x48\x8b\xc4\x55\x53\x56\x57\x41", 8},
                {1709, 0x48770, "\x48\x89\x5c\x24\x08\x48\x89\x6c", 8},
                {7681, 0x289330, "\x48\x8b\xc4\x48\x89\x58\x08\x48", 8},
                {1807, 0x4adb0, "\x40\x53\x48\x83\xec\x20\x48\x8b", 8},
                {1447, 0x32e60, "\x48\x8b\xc4\x48\x89\x58\x08\x48", 8},
                {1381, 0x135690, "\x40\x53\x48\x83\xec\x20\x8b\xd9", 8},
                {3487, 0x9cdb0, "\x48\x89\x5c\x24\x08\x57\x48\x83", 8},
                {1605, 0xb84b0, "\x48\x8b\xc4\x48\x89\x58\x08\x48", 8},
                {3171, 0x430d0, "\x4c\x89\x4c\x24\x20\x44\x89\x44", 8},
                {2070, 0xe4790, "\x48\x89\x5c\x24\x08\x48\x89\x6c", 8},
                {4538, 0x100630, "\x8a\x41\x64\x24\x0c\x3c\x08\x73", 8},
                {2985, 0x170500, "\x40\x53\x48\x83\xec\x20\x48\x8b", 8},
                {2683, 0x1af9a0, "\x48\x8b\x41\x10\xc3", 5},
                {6217, 0x1dc680, "\x48\x8b\x41\x08\xc3", 5},
                {1486, 0x1dbe70, "\x48\x8b\x41\x18\xc3", 5},
                {35, 0x1dc6b0, "\x48\x8b\x41\x20\xc3", 5},
                {0, 0x1c1390, "\x48\x89\x5c\x24\x08\x48\x89\x74", 8},
                {0, 0x72a18, "\x48\x83\xec\x38\x4c\x8b\x05\x6d\x5a\xa5\x00\x84\xd2\x48\x8d\x15\xe4\xd0\x7c\x00", 20}};
    for (const auto &entry : entries) {
        BYTE code[32]{};
        if (!read(entry.rva, code, entry.length) || memcmp(code, entry.bytes, entry.length) ||
            (entry.ordinal && GetProcAddress(module, MAKEINTRESOURCEA(entry.ordinal)) !=
                                  reinterpret_cast<FARPROC>(const_cast<BYTE *>(base + entry.rva))))
            return false;
    }
    const struct Property {
        size_t rva;
        const char *name;
    } properties[]{{0x837128, "FramePart"}, {0x8371a0, "SysCommand"}, {0x948250, "HitTest"},    {0x83ff48, "content"},
                   {0x83f3d8, "fontface"},  {0x8387c0, "fontsize"},   {0x83b790, "foreground"}, {0x83f398, "padding"},
                   {0x83fb10, "isvisible"}, {0x83fba0, "isenabled"}};
    for (const auto &property : properties) {
        uintptr_t label{};
        char name[32]{};
        const auto length = strlen(property.name) + 1;
        if (!read(property.rva, &label, sizeof(label)) || label < reinterpret_cast<uintptr_t>(base) ||
            !read(label - reinterpret_cast<uintptr_t>(base), name, length) || memcmp(name, property.name, length))
            return false;
    }
    return true;
}
