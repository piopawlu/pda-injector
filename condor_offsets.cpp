#include "condor_offsets.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Logging helper
// ============================================================================
static FILE* g_log = nullptr;

static void open_log() {
    if (g_log) return;
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "condor-offsets.log");
    else strcpy(path, "condor-offsets.log");
    g_log = fopen(path, "a");
}

static void log_line(const char* fmt, ...) {
    open_log();
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ============================================================================
// Layer 1: Auto-detect Condor window via EnumWindows + prefix match
// ============================================================================
struct EnumCtx {
    HWND found;
    char version[64];
};

static BOOL CALLBACK EnumProc(HWND hWnd, LPARAM lParam) {
    if (!IsWindowVisible(hWnd)) return TRUE;
    char title[256] = {0};
    int len = GetWindowTextA(hWnd, title, sizeof(title));
    if (len <= 0) return TRUE;
    static const char prefix[] = "Condor version ";
    static const size_t prefixLen = sizeof(prefix) - 1;
    if (strncmp(title, prefix, prefixLen) != 0) return TRUE;
    EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lParam);
    ctx->found = hWnd;
    strncpy(ctx->version, title + prefixLen, sizeof(ctx->version) - 1);
    ctx->version[sizeof(ctx->version) - 1] = 0;
    return FALSE; // stop enumeration
}

HWND FindCondorWindow(char* outVersion, size_t outVersionSize) {
    EnumCtx ctx = {0};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found) {
        log_line("FindCondorWindow: no window with prefix 'Condor version ' found");
        if (outVersion && outVersionSize) outVersion[0] = 0;
        return nullptr;
    }
    log_line("FindCondorWindow: hWnd=0x%p version='%s'", ctx.found, ctx.version);
    if (outVersion && outVersionSize) {
        strncpy(outVersion, ctx.version, outVersionSize - 1);
        outVersion[outVersionSize - 1] = 0;
    }
    return ctx.found;
}

// ============================================================================
// Layer 3: PE parsing for IAT + signature scan for immediates
// ============================================================================

// Find IAT entry for a given function in a given module's import table.
// Walks IMPORT_DESCRIPTORs, then for the matching DLL name walks the OriginalFirstThunk
// to locate the function index, and returns &IAT[index].
static DWORD* FindIATEntry(HMODULE hModule, const char* dllName, const char* funcName) {
    BYTE* base = reinterpret_cast<BYTE*>(hModule);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    DWORD importDirRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importDirRVA) return nullptr;
    IMAGE_IMPORT_DESCRIPTOR* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirRVA);

    for (; desc->Name; ++desc) {
        const char* name = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(name, dllName) != 0) continue;

        IMAGE_THUNK_DATA* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA* iatThunk  = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; origThunk->u1.AddressOfData; ++origThunk, ++iatThunk) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            IMAGE_IMPORT_BY_NAME* imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(imp->Name), funcName) == 0) {
                return reinterpret_cast<DWORD*>(&iatThunk->u1.Function);
            }
        }
        return nullptr; // DLL found, but not the function
    }
    return nullptr;
}

// Find the .text section bounds in the loaded module.
static bool GetTextSection(HMODULE hModule, BYTE*& outStart, size_t& outSize) {
    BYTE* base = reinterpret_cast<BYTE*>(hModule);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            outStart = base + sec->VirtualAddress;
            outSize = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// Memmem for byte patterns.
static BYTE* MemSearch(BYTE* haystack, size_t hayLen, const BYTE* needle, size_t needleLen) {
    if (needleLen == 0 || hayLen < needleLen) return nullptr;
    BYTE first = needle[0];
    BYTE* end = haystack + hayLen - needleLen;
    for (BYTE* p = haystack; p <= end; ++p) {
        if (*p != first) continue;
        if (memcmp(p, needle, needleLen) == 0) return p;
    }
    return nullptr;
}

// Signature scan. Pattern is the unique 10-byte sequence:
//   push 498 (68 F2 01 00 00) + mov ecx, 298 (B9 2A 01 00 00)
// This sequence appears verified-unique in Condor.exe and survives across versions
// because it's the source-level "size of PDA texture" function body.
//
// The 4 immediates we need to double are at canonical relative offsets (BASE_ADDR + N + 1):
//   BASE_ADDR + 0x96 + 1 -> first  push 498
//   BASE_ADDR + 0xAA + 1 -> first  mov ecx, 298
//   BASE_ADDR + 0xB9 + 1 -> second push 498
//   BASE_ADDR + 0xBE + 1 -> second mov ecx, 298
//
// The anchor (10-byte pattern) IS the second push 498 + second mov ecx 298.
// So relative to anchor:
//   imm[0] = anchor - 0x23  (first push 498 immediate)
//   imm[1] = anchor - 0x0F  (first mov ecx, 298 immediate)
//   imm[2] = anchor + 0x01  (anchor + 1)
//   imm[3] = anchor + 0x06  (anchor + 6)
static bool ScanImmediates(HMODULE hModule, uint32_t* outImms[4]) {
    BYTE* textStart = nullptr;
    size_t textSize = 0;
    if (!GetTextSection(hModule, textStart, textSize)) {
        log_line("ScanImmediates: .text section not found");
        return false;
    }
    static const BYTE pattern[] = {
        0x68, 0xF2, 0x01, 0x00, 0x00,  // push 498
        0xB9, 0x2A, 0x01, 0x00, 0x00   // mov ecx, 298
    };
    BYTE* anchor = MemSearch(textStart, textSize, pattern, sizeof(pattern));
    if (!anchor) {
        log_line("ScanImmediates: anchor pattern not found in .text");
        return false;
    }
    // Verify uniqueness (anchor must be unique)
    BYTE* second = MemSearch(anchor + 1, textSize - (anchor - textStart) - 1, pattern, sizeof(pattern));
    if (second) {
        log_line("ScanImmediates: anchor not unique (second hit at 0x%p)", second);
        return false;
    }
    log_line("ScanImmediates: anchor at 0x%p (.text base 0x%p)", anchor, textStart);

    BYTE* candidates[4] = {
        anchor - 0x22,  // first push 498 immediate (opcode 0x68 at -0x23)
        anchor - 0x0E,  // first mov ecx, 298 immediate (opcode 0xB9 at -0x0F)
        anchor + 0x01,  // second push 498 immediate (anchor itself = opcode 0x68)
        anchor + 0x06   // second mov ecx, 298 immediate
    };
    static const uint32_t expected[4] = { 498, 298, 498, 298 };
    for (int i = 0; i < 4; ++i) {
        if (candidates[i] < textStart || candidates[i] + 4 > textStart + textSize) {
            log_line("ScanImmediates: candidate[%d] out of .text bounds", i);
            return false;
        }
        uint32_t val = *reinterpret_cast<uint32_t*>(candidates[i]);
        if (val != expected[i]) {
            log_line("ScanImmediates: candidate[%d] value mismatch: 0x%08X expected 0x%08X",
                i, val, expected[i]);
            return false;
        }
        outImms[i] = reinterpret_cast<uint32_t*>(candidates[i]);
    }
    log_line("ScanImmediates: 4 immediates validated (498/298/498/298)");
    return true;
}

// ============================================================================
// Layer 4: External override file condor-offsets.ini
// Format (next to Condor.exe):
//   [3.0.9]
//   iat_ext_text_out = 0x007DB500
//   imm0 = 0x007363E3
//   imm1 = 0x007363F7
//   imm2 = 0x00736406
//   imm3 = 0x0073640B
// (Values are virtual addresses to write to.)
// ============================================================================
static bool TryLoadOverrideINI(const char* version, CondorOffsets& out) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "condor-offsets.ini");
    else strcpy(path, "condor-offsets.ini");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;

    char buf[64];
    auto readVA = [&](const char* key) -> uintptr_t {
        GetPrivateProfileStringA(version, key, "", buf, sizeof(buf), path);
        if (buf[0] == 0) return 0;
        return static_cast<uintptr_t>(_strtoui64(buf, nullptr, 0));
    };
    uintptr_t iat = readVA("iat_ext_text_out");
    uintptr_t i0 = readVA("imm0");
    uintptr_t i1 = readVA("imm1");
    uintptr_t i2 = readVA("imm2");
    uintptr_t i3 = readVA("imm3");
    if (!iat || !i0 || !i1 || !i2 || !i3) {
        log_line("INI override: section [%s] incomplete in %s", version, path);
        return false;
    }
    out.iat_ext_text_out = reinterpret_cast<DWORD*>(iat);
    out.immediates[0] = reinterpret_cast<uint32_t*>(i0);
    out.immediates[1] = reinterpret_cast<uint32_t*>(i1);
    out.immediates[2] = reinterpret_cast<uint32_t*>(i2);
    out.immediates[3] = reinterpret_cast<uint32_t*>(i3);
    out.valid = true;
    strcpy(out.source, "ini");
    log_line("INI override: loaded section [%s] from %s", version, path);
    return true;
}

// ============================================================================
// Public entry point
// ============================================================================
bool DiscoverCondorOffsets(CondorOffsets& out) {
    memset(&out, 0, sizeof(out));
    strcpy(out.version, "?");
    strcpy(out.source, "fail");

    // Detect version (informational; affects only INI lookup and logging)
    char ver[64] = {0};
    HWND hCondor = FindCondorWindow(ver, sizeof(ver));
    if (ver[0]) strncpy(out.version, ver, sizeof(out.version) - 1);

    // Layer 4 first: explicit user override wins
    if (ver[0] && TryLoadOverrideINI(ver, out)) {
        return true;
    }

    // Layer 3: signature scan + PE parsing on this process
    HMODULE hCondorExe = GetModuleHandleA(nullptr);
    if (!hCondorExe) {
        log_line("DiscoverCondorOffsets: GetModuleHandle(NULL) returned NULL");
        return false;
    }

    DWORD* iat = FindIATEntry(hCondorExe, "gdi32.dll", "ExtTextOutA");
    if (!iat) {
        log_line("DiscoverCondorOffsets: IAT entry for gdi32!ExtTextOutA not found");
        return false;
    }
    log_line("DiscoverCondorOffsets: IAT entry at 0x%p (current value 0x%08X)", iat, *iat);

    uint32_t* imms[4] = {0};
    if (!ScanImmediates(hCondorExe, imms)) {
        log_line("DiscoverCondorOffsets: signature scan failed");
        return false;
    }

    out.iat_ext_text_out = iat;
    for (int i = 0; i < 4; ++i) out.immediates[i] = imms[i];
    out.valid = true;
    strcpy(out.source, "scan");
    log_line("DiscoverCondorOffsets: success via scan, version='%s'", out.version);
    return true;
}
