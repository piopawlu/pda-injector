#include <Windows.h>
#include <dxgi.h>
#include "PDA-Injector.h"
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

bool PDAInject(HDC hdc);

static HDC hStoredHDC = NULL;

BOOL WINAPI ExtTextOutAOverride(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ UINT options, _In_opt_ CONST RECT* lprect,
	_In_reads_opt_(c) LPCSTR lpString, _In_ UINT c, _In_reads_opt_(c) CONST INT* lpDx)
{

	if (hStoredHDC == hdc)
	{
		if (y == 366) {
			hStoredHDC = NULL;
		}
		return TRUE;
	}

	const auto result = ExtTextOutA(hdc, x, y, options, lprect, lpString, c, lpDx);

	if (!lpString) {
		return result;
	}

	if (x == 227 && y == 447 && strncmp(lpString, "ReqE", 4) == 0) {
		PDAInject(hdc);
		hStoredHDC = hdc;
		return result;
	}

	return result;
}

void HookGDIFunctions() {
	//(*(DWORD*)(0x007D6540)) = reinterpret_cast<DWORD>(ExtTextOutAOverride); //condor 3.0.0
	(*(DWORD*)(0x007CE530)) = reinterpret_cast<DWORD>(ExtTextOutAOverride); //condor 3.0.2

	DWORD oldprotect = 0;
	auto hProc = GetCurrentProcess();

	// double the PDA texture size
	VirtualProtectEx(hProc, (char*)0x0072D700, 0x100, PAGE_EXECUTE_READWRITE, &oldprotect);
	((uint32_t*)0x0072D7A3)[0] *= 2;
	((uint32_t*)0x0072D7B7)[0] *= 2;
	((uint32_t*)0x0072D7C6)[0] *= 2;
	((uint32_t*)0x0072D7CB)[0] *= 2;
	VirtualProtectEx(hProc, (char*)0x0072D700, 0x100, oldprotect, &oldprotect);
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