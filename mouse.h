#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool SendMouseToBestTarget(HWND hParentHWND, const POINT& appMousePos, UINT uMsg, WPARAM wParam);