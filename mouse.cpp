#include "mouse.h"


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

    return PostMessage(hTargetWnd, uMsg, wParam, MAKELPARAM(adjustedPos.x, adjustedPos.y));
}