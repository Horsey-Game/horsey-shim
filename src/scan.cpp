#include <windows.h>
#include <vector>
#include <cstdlib>
#include "scan.h"

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;
};

static Pattern Parse(const char* s)
{
    Pattern p;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(0);
            while (*s == '?') s++;
        } else {
            char* end;
            p.bytes.push_back((uint8_t)strtoul(s, &end, 16));
            p.mask.push_back(1);
            s = end;
        }
    }
    return p;
}

uintptr_t FindPattern(const char* pattern)
{
    Pattern p = Parse(pattern);
    size_t n = p.bytes.size();
    if (n == 0) return 0;

    auto base = (uint8_t*)GetModuleHandleW(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);

    uintptr_t found = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* start = base + sec->VirtualAddress;
        size_t size = sec->Misc.VirtualSize;
        if (size < n) continue;
        for (size_t off = 0; off <= size - n; off++) {
            size_t j = 0;
            for (; j < n; j++) {
                if (p.mask[j] && start[off + j] != p.bytes[j]) break;
            }
            if (j == n) {
                if (found) return 0;
                found = (uintptr_t)(start + off);
            }
        }
    }
    return found;
}
