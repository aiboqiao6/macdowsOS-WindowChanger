#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstring>
#include <string>

namespace styler {
// CodeView identity is an ABI capability, independent of the module filename.
inline bool RemoteCodeView(HANDLE process, const MODULEENTRY32W &module, const std::wstring &key) {
    auto read = [&](size_t offset, void *out, size_t size) {
        SIZE_T n{};
        return offset <= module.modBaseSize && size <= module.modBaseSize - offset &&
               ReadProcessMemory(process, module.modBaseAddr + offset, out, size, &n) && n == size;
    };
    IMAGE_DOS_HEADER dos{};
    if (!read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        return false;
    struct Header {
        DWORD signature;
        IMAGE_FILE_HEADER file;
        WORD magic;
    } header{};
    if (!read(dos.e_lfanew, &header, sizeof(header)) || header.signature != IMAGE_NT_SIGNATURE)
        return false;
    size_t directory = header.magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory)
                                                                     : offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    IMAGE_DATA_DIRECTORY debug{};
    if (!read(dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + directory +
                  IMAGE_DIRECTORY_ENTRY_DEBUG * sizeof(debug),
              &debug, sizeof(debug)) ||
        debug.Size > 4096)
        return false;
    for (size_t i = 0; i + sizeof(IMAGE_DEBUG_DIRECTORY) <= debug.Size; i += sizeof(IMAGE_DEBUG_DIRECTORY)) {
        IMAGE_DEBUG_DIRECTORY entry{};
        if (!read(debug.VirtualAddress + i, &entry, sizeof(entry)))
            return false;
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < 24)
            continue;
        struct Cv {
            char signature[4];
            GUID id;
            DWORD age;
        } cv{};
        if (!read(entry.AddressOfRawData, &cv, sizeof(cv)) || memcmp(cv.signature, "RSDS", 4))
            continue;
        wchar_t text[64]{};
        swprintf_s(text, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", cv.id.Data1, cv.id.Data2, cv.id.Data3,
                   cv.id.Data4[0], cv.id.Data4[1], cv.id.Data4[2], cv.id.Data4[3], cv.id.Data4[4], cv.id.Data4[5],
                   cv.id.Data4[6], cv.id.Data4[7], cv.age);
        if (_wcsicmp(text, key.c_str()) == 0)
            return true;
    }
    return false;
}
inline HMODULE FindElectronRuntime() {
    HMODULE result{};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;
    MODULEENTRY32W entry{sizeof(entry)};
    if (Module32FirstW(snapshot, &entry))
        do {
            if (GetProcAddress(entry.hModule, "napi_get_cb_info") &&
                GetProcAddress(entry.hModule, "?TryGetCurrent@Isolate@v8@@SAPEAV12@XZ") &&
                GetProcAddress(entry.hModule, "?GetVersion@V8@v8@@SAPEBDXZ")) {
                result = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}
// Read the PE export names without loading the target's rendering library into
// the controller. No executable names or product-specific module names.
inline bool RemoteExport(HANDLE process, const MODULEENTRY32W &module, const char *symbol) {
    auto read = [&](size_t offset, void *out, size_t size) {
        SIZE_T n{};
        return offset <= module.modBaseSize && size <= module.modBaseSize - offset &&
               ReadProcessMemory(process, module.modBaseAddr + offset, out, size, &n) && n == size;
    };
    IMAGE_DOS_HEADER dos{};
    if (!read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        return false;
    struct Header {
        DWORD signature;
        IMAGE_FILE_HEADER file;
        WORD magic;
    } header{};
    if (!read(dos.e_lfanew, &header, sizeof(header)) || header.signature != IMAGE_NT_SIGNATURE)
        return false;
    IMAGE_DATA_DIRECTORY exports{};
    const auto optional = dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    size_t directory = header.magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory)
                                                                     : offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    if (!read(optional + directory, &exports, sizeof(exports)) || !exports.Size)
        return false;
    IMAGE_EXPORT_DIRECTORY e{};
    if (!read(exports.VirtualAddress, &e, sizeof(e)) || e.NumberOfNames > 1000000)
        return false;
    DWORD first = 0, last = e.NumberOfNames;
    while (first < last) {
        DWORD mid = first + (last - first) / 2, rva{};
        if (!read(size_t(e.AddressOfNames) + mid * 4, &rva, 4))
            return false;
        std::array<char, 256> name{};
        if (!read(rva, name.data(), name.size()))
            return false;
        name.back() = 0;
        int order = strcmp(name.data(), symbol);
        if (!order)
            return true;
        if (order < 0)
            first = mid + 1;
        else
            last = mid;
    }
    return false;
}
} // namespace styler
