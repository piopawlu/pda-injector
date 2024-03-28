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

constexpr long XC_WIDTH = 564;
constexpr long XC_HEIGHT = 966;

extern PDAInjectorOptions pdaOptions;

std::vector<uint8_t> pixel_buffer;

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

    const auto result = BitBlt(dstDC, 30, 30, std::min(XC_WIDTH, width),
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
    StretchBlt(hDC, 0, 0, 1024, 1024, hdcMem, 0, 0, 256, 256, SRCCOPY);
    DeleteDC(hdcMem);

    return true;
}

PDAINJECTOR_API BOOL __stdcall PDAInject(HDC hPdaDC, int page)
{
    static HWND hXCSoarWnd = NULL;

    if (hPdaDC == NULL) {
        return FALSE;
    }

    if (page == pdaOptions.page && pdaOptions.enabled)
    {
        if (hXCSoarWnd == NULL) {
            hXCSoarWnd = FindWindowA(NULL, "XCSoar");
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
            BitBlt(hPdaDC, 30, 30, XC_WIDTH, XC_HEIGHT, hdcMem, 0, 0, SRCCOPY);
            DeleteDC(hdcMem);
            return TRUE;
        }
    }

    GdiFlush();
    ResizeOriginalPDA(hPdaDC);
    return TRUE;
}
