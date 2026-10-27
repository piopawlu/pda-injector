#include <Windows.h>
#include <dxgi.h>
#include "PDA-Injector.h"
#include "condor_offsets.h"
//#include <cstdint>

HMODULE hTrueDX11_43Module = 0;
extern PDAInjectorOptions pdaOptions;

#define EXPORT comment(linker,"/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__)

#define HEAD(name) \
static unsigned long addr_ ## name = 0; \
static const char* name_ ## name = #name; \
__declspec( naked ) void __stdcall name() {

HEAD(D3DX11CompileFromMemory)
#pragma EXPORT
__asm {
__asm mov ebx, addr_D3DX11CompileFromMemory
__asm test ebx, ebx
__asm jz LookupD3DX11CompileFromMemory
__asm jmp ebx
__asm ret(13 * 4)
__asm LookupD3DX11CompileFromMemory:
__asm mov ebx, name_D3DX11CompileFromMemory
__asm push ebx
__asm mov ebx, hTrueDX11_43Module
__asm push ebx
__asm call GetProcAddress
__asm mov addr_D3DX11CompileFromMemory, eax
__asm jmp eax
__asm ret(13 * 4)
}
};


HEAD(D3DX11SaveTextureToFileA)
#pragma EXPORT
__asm {
	__asm mov ebx, addr_D3DX11SaveTextureToFileA
	__asm test ebx, ebx
	__asm jz LookupD3DX11SaveTextureToFileA
	__asm jmp ebx
	__asm ret(4 * 4)
	__asm LookupD3DX11SaveTextureToFileA:
	__asm mov ebx, name_D3DX11SaveTextureToFileA
	__asm push ebx
	__asm mov ebx, hTrueDX11_43Module
	__asm push ebx
	__asm call GetProcAddress
	__asm mov addr_D3DX11SaveTextureToFileA, eax
	__asm jmp eax
	__asm ret(4 * 4)
}
};

typedef int(__stdcall *D3DX11CSRVFFA)(void*, const void*, void*, void*, void*, void*);
static D3DX11CSRVFFA addr_D3DX11CreateShaderResourceViewFromFileA = NULL;
static bool gdi_hooked = false;

bool PDAInject(HDC hdc, int page);



BOOL WINAPI ExtTextOutAOverride(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ UINT options, _In_opt_ CONST RECT* lprect,
	_In_reads_opt_(c) LPCSTR lpString, _In_ UINT c, _In_reads_opt_(c) CONST INT* lpDx)
{
	static HDC hStoredHDC = NULL;
	static int iStoredPage = -1;


	const auto result = ExtTextOutA(hdc, x, y, options, lprect, lpString, c, lpDx);

	if (!lpString) {
		return result;
	}

	if (hStoredHDC == hdc)
	{
		if (y == 366 && x >= 180) {
			PDAInject(hdc, iStoredPage);
			hStoredHDC = NULL;
		}
		return TRUE;
	}

	if (x == 227 && y == 447 && strncmp(lpString, "ReqE", 4) == 0) {
		iStoredPage = 1;
		hStoredHDC = hdc;
		return result;
	} else if (y == 447 && strncmp(lpString, "tDelta", 6) == 0) {
		iStoredPage = 2;
		hStoredHDC = hdc;
		return result;
	} else if (y == 447 && strncmp(lpString, "TGain", 5) == 0) {
		iStoredPage = 3;
		hStoredHDC = hdc;
		return result;
	}

	return result;
}

void HookGDIFunctions() {
	// Auto-discover offsets at runtime: works for any Condor 3.x version that
	// preserves the source-level "double PDA texture" function shape (validated
	// on 3.0.9 and 3.1.0). Falls back to condor-offsets.ini override if scan fails.
	CondorOffsets off;
	if (!DiscoverCondorOffsets(off)) {
		// Fail-safe: log and disable PDA hack rather than crash on unknown version
		MessageBoxA(nullptr,
			"PDA-Injector: failed to auto-detect Condor offsets.\n"
			"PDA injection disabled. See condor-offsets.log for details, or\n"
			"provide condor-offsets.ini override next to Condor.exe.",
			"PDA-Injector", MB_OK | MB_ICONWARNING);
		return;
	}

	auto hProc = GetCurrentProcess();
	DWORD oldprotect = 0;

	// 1) Hook gdi32!ExtTextOutA via IAT patching
	VirtualProtectEx(hProc, off.iat_ext_text_out, sizeof(DWORD), PAGE_READWRITE, &oldprotect);
	*off.iat_ext_text_out = reinterpret_cast<DWORD>(ExtTextOutAOverride);
	VirtualProtectEx(hProc, off.iat_ext_text_out, sizeof(DWORD), oldprotect, &oldprotect);

	// 2) Double the 4 PDA texture-size immediates (298->596, 498->996)
	for (int i = 0; i < 4; ++i) {
		VirtualProtectEx(hProc, off.immediates[i], sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldprotect);
		off.immediates[i][0] *= 2;
		VirtualProtectEx(hProc, off.immediates[i], sizeof(uint32_t), oldprotect, &oldprotect);
	}
}

HRESULT WINAPI D3DX11CreateShaderResourceViewFromFileA(
	_In_  void* pDevice,
	_In_  const char* pSrcFile,
	_In_  void* pLoadInfo,
	_In_  void* pPump,
	_Out_ void** ppShaderResourceView,
	_Out_ void* pHResult
)
{
#pragma EXPORT
	if (!addr_D3DX11CreateShaderResourceViewFromFileA) {
		addr_D3DX11CreateShaderResourceViewFromFileA = (D3DX11CSRVFFA)GetProcAddress(hTrueDX11_43Module, "D3DX11CreateShaderResourceViewFromFileA");
	}

	if (pdaOptions.pda.enabled == true && false == gdi_hooked) {
		HookGDIFunctions();
		gdi_hooked = true;
	}

	return addr_D3DX11CreateShaderResourceViewFromFileA(pDevice, pSrcFile, pLoadInfo, pPump, ppShaderResourceView, pHResult);
}