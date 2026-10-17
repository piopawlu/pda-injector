#include "mouse.h"

// SDL2 (XCSoar 7.x) derives the button state from the MK_* flags in wParam, not from
// the message type: a WM_LBUTTONDOWN without MK_LBUTTON is read as "no button pressed".
// Set the flags a real mouse message would carry. WM_LBUTTONDBLCLK is left alone: it is
// posted after the second WM_LBUTTONUP, with the button already released, and setting
// MK_LBUTTON there would leave SDL with the button stuck down.
static WPARAM WithButtonFlags(UINT uMsg, WPARAM wParam) {
    switch (uMsg) {
    case WM_LBUTTONDOWN:
        return wParam | MK_LBUTTON;
    case WM_LBUTTONUP:
        return wParam & ~static_cast<WPARAM>(MK_LBUTTON);
    case WM_RBUTTONDOWN:
        return wParam | MK_RBUTTON;
    case WM_RBUTTONUP:
        return wParam & ~static_cast<WPARAM>(MK_RBUTTON);
    default:
        return wParam;
    }
}

bool SendMouseToBestTarget(HWND hParentHWND, const POINT& appMousePos, UINT uMsg, WPARAM wParam) {
    constexpr auto SKIPMASK = CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT | CWP_SKIPINVISIBLE;

    HWND hTargetWnd = hParentHWND;
    POINT adjustedPos = appMousePos;
    HWND hTmpWnd = NULL;

    RECT xcRect;

    GetClientRect(hParentHWND, &xcRect);
    adjustedPos.x += xcRect.left;
    adjustedPos.y += xcRect.top;

    do {
        hTmpWnd = ChildWindowFromPointEx(hTargetWnd, POINT(adjustedPos.x, adjustedPos.y), SKIPMASK);
        if (hTmpWnd == NULL || hTmpWnd == hTargetWnd) {
            break;
        }

        MapWindowPoints(hTargetWnd, hTmpWnd, &adjustedPos, 1);
        hTargetWnd = hTmpWnd;
    } while (hTmpWnd != NULL);

    return PostMessage(hTargetWnd, uMsg, WithButtonFlags(uMsg, wParam), MAKELPARAM(adjustedPos.x, adjustedPos.y));
}