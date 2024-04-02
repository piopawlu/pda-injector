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

extern "C" {
	BOOL __stdcall PDAInject(HDC hdc, int page);
}

static HDC hStoredEllipseHDC = NULL;
static int currentPage = 0;

BOOL WINAPI EllipseOverride(_In_ HDC hdc, _In_ int left, _In_ int top, _In_ int right, _In_ int bottom)
{
	const auto result = Ellipse(hdc, left, top, right, bottom);

	if ((right - left) == 18 && (bottom - top) == 18) {
		DWORD tmpDW;
		BYTE  tmpB;
		__asm mov eax, [ebp + 0x2c];
		__asm mov tmpDW, eax
		if (tmpDW == 0x006548b7) {
			__asm mov eax, [ebp + 0x28];
			__asm mov al, [eax - 0x62];
			__asm mov tmpB, al;
			if (tmpB != 1) {
				PDAInject(hdc, 2);
				hStoredEllipseHDC = NULL;
			}
			else {
				hStoredEllipseHDC = hdc;
			}
		} else {
			hStoredEllipseHDC = hdc;
		}
	} else {
		hStoredEllipseHDC = NULL;
	}

	return result;
}

BOOL WINAPI ExtTextOutAOverride(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ UINT options, _In_opt_ CONST RECT* lprect,
	_In_reads_opt_(c) LPCSTR lpString, _In_ UINT c, _In_reads_opt_(c) CONST INT* lpDx)
{
	const auto result = ExtTextOutA(hdc, x, y, options, lprect, lpString, c, lpDx);

	if (!lpString) {
		return result;
	}

	if (hdc == hStoredEllipseHDC && strncmp(lpString, "ETA:", 4) == 0 && y == 0x0CB) {
		PDAInject(hdc, 1);
		currentPage = 1;
		hStoredEllipseHDC = NULL;
	}
	else if (lpString[0] == '-' || lpString[0] >= '0' && lpString[0] <= '9') {
		const auto len = strlen(lpString);
		if (len >= 4 && (strcmp(lpString + (len - 4), " m/s") == 0 ||
			strcmp(lpString + (len - 4), " kts") == 0)) {
			PDAInject(hdc, 3);
			currentPage = 3;
		}
		else if (len >= 4 && x == 0x0C && y == 0x0c && 
			(strcmp(lpString + (len - 3), " km") == 0 || 
			 strcmp(lpString + (len - 3), " nm") == 0)) {
			PDAInject(hdc, 0);
			currentPage = 0;
		}
	}
	else if (lpString[0] == 'E' && lpString[1] == '\0' && lprect == NULL) {
		PDAInject(hdc, 3);
		currentPage = 3;
	}

	return result;
}

BOOL WINAPI LineToOverride(_In_ HDC hdc, _In_ int x, _In_ int y)
{
	const auto result = LineTo(hdc, x, y);

	if (y == 0x6B && hdc == hStoredEllipseHDC) {
		PDAInject(hdc, 2);
		currentPage = 2;
		hStoredEllipseHDC = NULL;
	}

	return result;
}

void HookGDIFunctions() {
	(*(DWORD*)(0x00744300)) = reinterpret_cast<DWORD>(EllipseOverride);
	(*(DWORD*)(0x007442E8)) = reinterpret_cast<DWORD>(ExtTextOutAOverride);
	(*(DWORD*)(0x00744268)) = reinterpret_cast<DWORD>(LineToOverride);

	auto hProc = GetCurrentProcess();

	DWORD oldprotect = 0;
	VirtualProtectEx(hProc, (char*)0x00663160, 0x100, PAGE_EXECUTE_READWRITE, &oldprotect);
	((unsigned char*)0x00663162)[0] = 0x04; //modify texture width 1024px
	((unsigned char*)0x0066316F)[0] = 0xFC; //modify texture height -1024px
	VirtualProtectEx(hProc, (char*)0x00663160, 0x100, oldprotect, &oldprotect);
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

	if (pdaOptions.enabled == true && 
		strcmp(pSrcFile, "PLANES\\TEXTURES\\IPAQSCREEN.BMP") == 0)
	{
		pSrcFile = "PLANES\\TEXTURES\\IPAQ1024.BMP";

		if (!gdi_hooked) {
			HookGDIFunctions();
			gdi_hooked = true;
		}
	}

	return addr_D3DX11CreateShaderResourceViewFromFileA(pDevice, pSrcFile, pLoadInfo, pPump, ppShaderResourceView, pHResult);
}