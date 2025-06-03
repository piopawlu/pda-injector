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
#include "mouse.h"

#include <algorithm>
#include <array>
#include <vector>
#include <chrono>

#include <windowsx.h>

#include "fmt/format.h"

#ifdef min
#undef min
#endif

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

POINT appMousePos = { 0,0 };
time_t appLastMouseMove = 0;
bool appMouseButtonDown = false;

BOOL CopyXCSoarToPDA(HWND hWnd, HDC srcDC, HDC dstDC) {

    extern HBITMAP captureBMP;

    HDC hTempDC = CreateCompatibleDC(dstDC);
    SelectObject(hTempDC, captureBMP);

    RECT rect;
    GetClientRect(hWnd, &rect);
    const long width = rect.right - rect.left;
    const long height = rect.bottom - rect.top;

    BitBlt(hTempDC, 0, 0, width, height, srcDC, 0, 0, SRCCOPY);

    if ((pdaOptions.app.pass_mouse || pdaOptions.joystick.mouse_enabled) &&
        (time(nullptr) - appLastMouseMove) < 15) {

        auto brush = CreateSolidBrush(appMouseButtonDown ? RGB(0,0, 255) : RGB(255, 0, 0));
        SelectObject(hTempDC, brush);
        Ellipse(hTempDC, appMousePos.x - 6, appMousePos.y - 6, appMousePos.x + 6, appMousePos.y + 6);
        GdiFlush();
        DeleteObject(brush);
    }

    if (pdaOptions.pda.swap_colours) {
        SwapRedAndBlue(hTempDC, captureBMP);
    }

    const auto result = StretchBlt(dstDC, 0, 0, XC_WIDTH, XC_HEIGHT, hTempDC, 0, 0, width, height, SRCCOPY);

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
    BitBlt(hdcMem, 0, 0, XC_WIDTH / 2, XC_HEIGHT / 2, hDC, 0, 0, SRCCOPY);
    StretchBlt(hDC, 0, 0, XC_WIDTH, XC_HEIGHT, hdcMem, 0, 0, XC_WIDTH / 2, XC_HEIGHT / 2, SRCCOPY);
    DeleteDC(hdcMem);

    return true;
}

static HHOOK hMouseHook;
static HWND hXCSoarWnd = NULL;
static HINSTANCE hXCSoarInstance = NULL;
static HWND hCondorWnd = NULL;
static WNDPROC condorWndProc = NULL;
static bool condorWindowActive = true;

static std::chrono::steady_clock::time_point last_mouse_input;

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    static POINT prevPos = { 0,0 };
    static auto prevClick = std::chrono::steady_clock::time_point();

    const auto now = std::chrono::steady_clock::now();
    last_mouse_input = now;

    if (nCode >= 0 && condorWindowActive == true) {

        appLastMouseMove = time(nullptr);

        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        if (pMouseStruct != nullptr) {

            const auto dx = pMouseStruct->pt.x - prevPos.x;
            const auto dy = pMouseStruct->pt.y - prevPos.y;

            appMousePos.x = std::clamp(appMousePos.x + dx, 0l, XC_WIDTH);
            appMousePos.y = std::clamp(appMousePos.y + dy, 0l, XC_HEIGHT);

            prevPos = { pMouseStruct->pt.x , pMouseStruct->pt.y };
        }

        if (wParam == WM_RBUTTONDOWN) {
            appMouseButtonDown = true;
            SendMouseToBestTarget(hXCSoarWnd, appMousePos, WM_LBUTTONDOWN, 0);
        }
        else if (wParam == WM_RBUTTONUP) {
            appMouseButtonDown = false;
            SendMouseToBestTarget(hXCSoarWnd, appMousePos, WM_LBUTTONUP, 0);

            if (now - prevClick < std::chrono::milliseconds(300)) {
                SendMouseToBestTarget(hXCSoarWnd, appMousePos, WM_LBUTTONDBLCLK, 0);
            }
            prevClick = now;
        }
        else if (wParam == WM_MOUSEMOVE && appMouseButtonDown == true) {
            SendMouseToBestTarget(hXCSoarWnd, appMousePos, WM_MOUSEMOVE, appMouseButtonDown ? MK_LBUTTON : 0);
        }
        else if (wParam == WM_MOUSEWHEEL) {
            PostMessage(hXCSoarWnd, WM_MOUSEWHEEL, wParam, MAKELPARAM(appMousePos.x, appMousePos.y));
        }
    }

    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK CaptureWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && hXCSoarWnd != NULL)
    {
        const uint8_t key = static_cast<uint8_t>(wParam);
        if (pdaOptions.app.pass_keys[key] != 0 || ((pdaOptions.app.pass_all_with_shift && (GetKeyState(VK_SHIFT) & 0x8000) != 0) && (key != VK_SHIFT))) {
            const WPARAM mappedKey = static_cast<WPARAM>(pdaOptions.app.pass_keys[key]);
            if (msg == WM_KEYDOWN) {
                PostMessage(hXCSoarWnd, WM_ACTIVATE, WA_ACTIVE, 0);
            }
            PostMessageA(hXCSoarWnd, msg, mappedKey, lParam);
            return 1;
        }
    }
    else if (msg == WM_SYSKEYUP && static_cast<uint8_t>(wParam) == VK_F10) {
        pdaOptions.pda.enabled = !pdaOptions.pda.enabled;
    }
    else if (msg == WM_ACTIVATE) {
        condorWindowActive = (wParam != WA_INACTIVE);
    }
    else if (msg == WM_DESTROY)
    {
        const auto result = CallWindowProc(condorWndProc, hWnd, msg, wParam, lParam);
        SetWindowLong(hWnd, GWL_WNDPROC, (LONG)(condorWndProc));
        condorWndProc = NULL;

        if (pdaOptions.app.pass_mouse) {
            UnhookWindowsHookEx(hMouseHook);
        }

        hCondorWnd = NULL;
        return result;
    }

    return CallWindowProc(condorWndProc, hWnd, msg, wParam, lParam);
}

bool PDAInject(HDC hPdaDC, int page)
{
    if (hPdaDC == NULL) {
        return false;
    }

    if (pdaOptions.app.pass_input && condorWndProc == NULL) {
        hCondorWnd = FindWindowA(NULL, "Condor version 3.0.6");
        if (hCondorWnd != NULL) {
            condorWndProc = reinterpret_cast<WNDPROC>(GetWindowLong(hCondorWnd, GWL_WNDPROC));
            if (condorWndProc != NULL) {
                SetWindowLong(hCondorWnd, GWL_WNDPROC, reinterpret_cast<LONG>(CaptureWndProc));
                LoadSettings(pdaOptions, "pda.ini");
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const auto since_last_mouse_input = now - last_mouse_input;

    if (pdaOptions.app.pass_mouse && since_last_mouse_input >= std::chrono::seconds(30)) {
        if (hMouseHook != NULL) {
            UnhookWindowsHookEx(hMouseHook);
        }
        hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, NULL, 0);
        last_mouse_input = now;
    }

    if (pdaOptions.pda.enabled)
    {
        if (hXCSoarWnd == NULL) {
            hXCSoarWnd = FindWindowA(NULL, pdaOptions.app.window.c_str());
            if (hXCSoarWnd != NULL) {
                hXCSoarInstance = (HINSTANCE)GetWindowLong(hXCSoarWnd, GWL_HINSTANCE);
            }
        }

        if (hXCSoarWnd != NULL) {
            if (hXCSoarInstance != (HINSTANCE)GetWindowLong(hXCSoarWnd, GWL_HINSTANCE)) {
                hXCSoarWnd = NULL;
                return true;
            }

            HDC hXCSoarDC = GetDC(hXCSoarWnd);
            if (!hXCSoarDC) {
                hXCSoarWnd = NULL;
                return true;
            }

            CopyXCSoarToPDA(hXCSoarWnd, hXCSoarDC, hPdaDC);
            ReleaseDC(hXCSoarWnd, hXCSoarDC);
            return true;
        }
        else if (pdaOptions.pda.show_waiting_screen == true)
        {
            extern HBITMAP waitingForXCSoarBMP;

            HDC hdcMem = CreateCompatibleDC(hPdaDC);
            SelectObject(hdcMem, waitingForXCSoarBMP);
            BitBlt(hPdaDC, 0, 0, XC_WIDTH, XC_HEIGHT, hdcMem, 0, 0, SRCCOPY);
            DeleteDC(hdcMem);
            return true;
        }
    }

    GdiFlush();
    ResizeOriginalPDA(hPdaDC);
    return true;
}
