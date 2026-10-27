#pragma once
#include <Windows.h>
#include <stdint.h>

// 4 layers of Condor version adaptation:
// (1) Auto-detect window title via EnumWindows + prefix match "Condor version "
// (2) Fail-safe: log + disable hack if discovery fails (no crash)
// (3) Signature scan + PE parsing to discover offsets at runtime
// (4) External override file condor-offsets.ini (next to Condor.exe)

struct CondorOffsets {
    char version[64];          // detected version string (e.g. "3.0.9", "3.1.0", or "?")
    DWORD* iat_ext_text_out;   // IAT entry to patch for ExtTextOutA
    uint32_t* immediates[4];   // 4 pointers to 32-bit immediates (298/498) to double
    bool valid;                // true only if all 5 pointers were resolved and validated
    char source[32];           // discovery method: "scan", "ini", "fallback"
};

// Auto-detect Condor window via EnumWindows.
// Returns NULL if no window with title prefix "Condor version " is found.
// On success, fills outVersion (e.g. "3.0.9") if buffer non-null.
HWND FindCondorWindow(char* outVersion, size_t outVersionSize);

// Discover offsets in injected Condor.exe (this process).
// Strategy: signature scan + PE parsing, with optional override via condor-offsets.ini.
// Returns true and fills `out` on success, false on failure (out.valid = false).
bool DiscoverCondorOffsets(CondorOffsets& out);
