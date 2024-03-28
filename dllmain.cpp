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

#include <Windows.h>
#include "resource.h"
#include "PDA-Injector.h"


HBITMAP waitingForXCSoarBMP = NULL;
HBITMAP backgroundBMP = NULL;
HBITMAP captureBMP = NULL;

PDAInjectorOptions pdaOptions;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        waitingForXCSoarBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP1));
        backgroundBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP2));
        captureBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP3));
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        DeleteObject(waitingForXCSoarBMP);
        DeleteObject(backgroundBMP);
        DeleteObject(captureBMP);
        break;
    }
    return TRUE;
}

