#include "pe-imports.h"
#include <iostream>

namespace {
int failures{};
void Check(bool value, const char *name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    failures += !value;
}
struct Image {
    BYTE *base = static_cast<BYTE *>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    IMAGE_NT_HEADERS &Header() {
        return *reinterpret_cast<IMAGE_NT_HEADERS *>(base + 128);
    }
    IMAGE_IMPORT_DESCRIPTOR &Import() {
        return *reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + 1024);
    }
    IMAGE_THUNK_DATA &Name() {
        return *reinterpret_cast<IMAGE_THUNK_DATA *>(base + 2048);
    }
    Image() {
        if (!base)
            throw std::bad_alloc();
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 128;
        auto &nt = Header();
        nt.Signature = IMAGE_NT_SIGNATURE;
#ifdef _WIN64
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
#else
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
#endif
        nt.FileHeader.SizeOfOptionalHeader = sizeof(nt.OptionalHeader);
        nt.OptionalHeader.SizeOfImage = 8192;
        nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {1024, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
        Import().Name = 1536;
        Import().OriginalFirstThunk = 2048;
        Import().FirstThunk = 3072;
        Name().u1.AddressOfData = 4096;
        memcpy(base + 4098, "GetWindowLongPtrW", sizeof("GetWindowLongPtrW"));
    }
    ~Image() {
        VirtualFree(base, 0, MEM_RELEASE);
    }
    int Count() {
        int count{};
        styler::NativeImportImage(reinterpret_cast<HMODULE>(base), 8192).Visit([&](std::string_view name, void **slot) {
            Check(name == "GetWindowLongPtrW" && slot == reinterpret_cast<void **>(base + 3072),
                  "exact import name and IAT address");
            ++count;
        });
        return count;
    }
};
} // namespace
int main() {
    {
        Image i;
        i.Import().FirstThunk = 3073;
        Check(i.Count() == 0, "unaligned atomic IAT slots are rejected");
    }
    {
        Image i;
        i.Header().OptionalHeader.SizeOfImage = 4096;
        Check(i.Count() == 0, "declared image boundary limits import reads");
    }
    {
        Image i;
        Check(i.Count() == 1, "native architecture import is enumerated");
    }
    {
        Image i;
#ifdef _WIN64
        i.Header().OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
#else
        i.Header().OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
#endif
        Check(i.Count() == 0, "opposite optional-header format is skipped");
    }
    {
        Image i;
        i.Header().FileHeader.Machine ^= 1;
        Check(i.Count() == 0, "opposite machine type is skipped");
    }
    {
        Image i;
        i.Name().u1.AddressOfData = ULONG_PTR(0x435f000000000000ULL);
        Check(i.Count() == 0, "managed mixed-width thunk cannot become a pointer");
    }
    {
        Image i;
        i.Name().u1.AddressOfData = 8191;
        Check(i.Count() == 0, "hint crossing the image boundary is rejected");
    }
    {
        Image i;
        i.Import().OriginalFirstThunk = 0;
        Check(i.Count() == 0, "resolved IAT is never parsed as import names");
    }
    {
        Image i;
        i.Name().u1.Ordinal = IMAGE_ORDINAL_FLAG | 12;
        Check(i.Count() == 0, "ordinal imports are skipped");
    }
    {
        Image i;
        memset(i.base + 4098, 'x', 256);
        Check(i.Count() == 0, "unterminated names are bounded");
    }
    {
        Image i;
        i.Header().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {8190, 40};
        Check(i.Count() == 0, "import directory crossing the image boundary is rejected");
    }
    {
        Image i;
        i.Import().OriginalFirstThunk = 8190;
        Check(i.Count() == 0, "truncated thunk table is rejected");
    }
    {
        Image i;
        DWORD old{};
        VirtualProtect(i.base + 4096, 4096, PAGE_NOACCESS, &old);
        Check(i.Count() == 0, "unreadable mapped page is skipped without a fault");
    }
    {
        Image i;
        reinterpret_cast<IMAGE_DOS_HEADER *>(i.base)->e_lfanew = LONG_MAX;
        Check(i.Count() == 0, "invalid NT-header offset is bounded");
    }
    {
        Image i;
        auto &dir = i.Header().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        dir = {1024, 64};
        i.Header().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {};
        auto fields = reinterpret_cast<DWORD *>(i.base + 1024);
        memset(fields, 0, 64);
        fields[0] = 1;
        fields[1] = 1536;
        fields[3] = 3072;
        fields[4] = 2048;
        Check(i.Count() == 1, "RVA delay imports are enumerated");
        fields[0] = 0;
        Check(i.Count() == 0, "legacy VA delay tables are skipped");
    }
    std::cout << "failures=" << failures << '\n';
    return failures ? 1 : 0;
}
