/*
Condor 2 PDA Injector
Copyright(C) 2024 Piotr Gertz

This program is free software; you can redistribute it and /or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
*/


#include "PDA-Injector.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <vector>

#ifdef min 
#undef min
#endif

#define DIVIDER 1

constexpr long XC_WIDTH = 564 / DIVIDER;
constexpr long XC_HEIGHT = 966 / DIVIDER;

extern PDAInjectorOptions pdaOptions;

std::vector<uint8_t> pixel_buffer;

BOOL LoadSettings(PDAInjectorOptions& settings, std::string ini_path);

void SwapRedAndBlue(HDC hDC, HBITMAP hBitmap)
{
    constexpr auto bytesPerRow = XC_WIDTH  * 4;
    constexpr auto bufferSize = bytesPerRow * XC_HEIGHT;

    if (pixel_buffer.size() != bufferSize) {
        pixel_buffer = std::move(std::vector<uint8_t>(bufferSize));
    }

    BYTE* pBits = pixel_buffer.data();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = XC_WIDTH;
    bmi.bmiHeader.biHeight = -XC_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (GetDIBits(hDC, hBitmap, 0, XC_HEIGHT, pBits, &bmi, DIB_RGB_COLORS) == 0) {
        return;
    }

    for (int row = 0; row < XC_HEIGHT; row++) {
        auto* row_bytes = &pBits[bytesPerRow * row];
        for (int x = 0; x < XC_WIDTH; x++) {
            std::swap(row_bytes[x * 4], row_bytes[x * 4 + 2]);
        }
    }

    SetDIBits(hDC, hBitmap, 0, XC_HEIGHT, pBits, &bmi, DIB_RGB_COLORS);
}

BOOL CopyXCSoarToPDA(HWND hWnd, HDC srcDC, HDC dstDC) {

    extern HBITMAP captureBMP;

    HDC hTempDC = CreateCompatibleDC(dstDC);
    SelectObject(hTempDC, captureBMP);

    RECT rect;
    GetClientRect(hWnd, &rect);
    const long width = rect.right - rect.left;
    const long height = rect.bottom - rect.top;

    BitBlt(hTempDC, 0, 0, width, height, srcDC, 0, 0, SRCCOPY);

    if (pdaOptions.swap_colours) {
        SwapRedAndBlue(hTempDC, captureBMP);
    }

    const auto result = BitBlt(dstDC, 30 / DIVIDER, 30 / DIVIDER, std::min(XC_WIDTH, width),
        std::min(XC_HEIGHT, height), hTempDC, 0, 0, SRCCOPY);

    if (width != XC_WIDTH || height != XC_HEIGHT) {
        GetWindowRect(hWnd, &rect);
        MoveWindow(hWnd, rect.left, rect.top,
            (XC_WIDTH)+(rect.right - rect.left - width),
            (XC_HEIGHT)+(rect.bottom - rect.top - height), TRUE);
    }

    DeleteDC(hTempDC);

    return result;
}

bool ResizeOriginalPDA(HDC hDC)
{
    extern HBITMAP backgroundBMP;

    HDC hdcMem = CreateCompatibleDC(hDC);
    SelectObject(hdcMem, backgroundBMP);
    BitBlt(hdcMem, 0, 0, 256, 256, hDC, 0, 0, SRCCOPY);
    StretchBlt(hDC, 0, 0, 1024 / DIVIDER, 1024 / DIVIDER, hdcMem, 0, 0, 256, 256, SRCCOPY);
    DeleteDC(hdcMem);

    return true;
}

static HWND hXCSoarWnd = NULL;
static HWND hCondorWnd = NULL;
static WNDPROC condorWndProc = NULL;

LRESULT CALLBACK CaptureWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && hXCSoarWnd != NULL)
    {
        const uint8_t key = static_cast<uint8_t>(wParam);
        if (pdaOptions.pass_keys[key] != 0 || (pdaOptions.pass_all_with_shift && (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            const WPARAM mappedKey = static_cast<WPARAM>(pdaOptions.pass_keys[key]);
            PostMessageA(hXCSoarWnd, msg, mappedKey, lParam);
            return 1;
        }
    }
    else if (msg == WM_DESTROY) {
        const auto result = CallWindowProc(condorWndProc, hWnd, msg, wParam, lParam);
        SetWindowLong(hWnd, GWL_WNDPROC, (LONG)(condorWndProc));
        condorWndProc = NULL;

        hCondorWnd = NULL;
        return result;
    }

    return CallWindowProc(condorWndProc, hWnd, msg, wParam, lParam);
}

PDAINJECTOR_API BOOL __stdcall PDAInject(HDC hPdaDC, int page)
{
    if (hPdaDC == NULL) {
        return FALSE;
    }

    if (pdaOptions.pass_input && condorWndProc == NULL) {
        hCondorWnd = FindWindowA(NULL, "Condor version 2.2.0");
        if (hCondorWnd != NULL) {
            condorWndProc = reinterpret_cast<WNDPROC>(GetWindowLong(hCondorWnd, GWL_WNDPROC));
            if (condorWndProc != NULL) {
                SetWindowLong(hCondorWnd, GWL_WNDPROC, reinterpret_cast<LONG>(CaptureWndProc));
                LoadSettings(pdaOptions, "pda.ini");
            }
        }
    }

    if (page == pdaOptions.page && pdaOptions.enabled)
    {
        if (hXCSoarWnd == NULL) {
            hXCSoarWnd = FindWindowA(NULL, pdaOptions.window.c_str());
        }

        if (hXCSoarWnd != NULL) {
            HDC hXCSoarDC = GetDC(hXCSoarWnd);
            if (!hXCSoarDC) {
                hXCSoarWnd = NULL;
                return TRUE;
            }

            CopyXCSoarToPDA(hXCSoarWnd, hXCSoarDC, hPdaDC);
            ReleaseDC(hXCSoarWnd, hXCSoarDC);
            return TRUE;
        }
        else if (pdaOptions.show_waiting_screen == true)
        {
            extern HBITMAP waitingForXCSoarBMP;

            HDC hdcMem = CreateCompatibleDC(hPdaDC);
            SelectObject(hdcMem, waitingForXCSoarBMP);
            BitBlt(hPdaDC, 30 / DIVIDER, 30 / DIVIDER, XC_WIDTH, XC_HEIGHT, hdcMem, 0, 0, SRCCOPY);
            DeleteDC(hdcMem);
            return TRUE;
        }
    }

    GdiFlush();

#if DIVIDER != 4
    ResizeOriginalPDA(hPdaDC);
#endif
    return TRUE;
}
