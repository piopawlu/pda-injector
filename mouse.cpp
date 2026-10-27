#include "mouse.h"
#include "PDA-Injector.h"

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

typedef DPI_AWARENESS_CONTEXT(WINAPI* PFN_SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef DPI_AWARENESS_CONTEXT(WINAPI* PFN_GetWindowDpiAwarenessContext)(HWND);

static PFN_SetThreadDpiAwarenessContext pSetThreadDpiAwarenessContext = nullptr;
static PFN_GetWindowDpiAwarenessContext pGetWindowDpiAwarenessContext = nullptr;

static void ResolveDpiFunctions() {
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE u32 = GetModuleHandleA("user32.dll");
        if (u32) {
            pSetThreadDpiAwarenessContext = (PFN_SetThreadDpiAwarenessContext)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
            pGetWindowDpiAwarenessContext = (PFN_GetWindowDpiAwarenessContext)GetProcAddress(u32, "GetWindowDpiAwarenessContext");
        }
    }
}

bool SendMouseToBestTarget(HWND hParentHWND, const POINT& appMousePos, UINT uMsg, WPARAM wParam) {
    constexpr auto SKIPMASK = CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT | CWP_SKIPINVISIBLE;

    // appMousePos lives in the XC_WIDTH x XC_HEIGHT space of the image shown in the PDA,
    // which is the XCSoar client area scaled to that size. Map it back to the real client
    // area, measured in the target's own DPI awareness context: posted coordinates are
    // not translated, so they must be in the pixels the target works in. On a 150%
    // display, XCSoar 7.45 (SDL, DPI aware) works in physical pixels (992x1584), XCSoar
    // 7.43 (GDI, DPI unaware) in logical ones, and Condor itself is DPI unaware.
    ResolveDpiFunctions();
    auto pSetDpi = pSetThreadDpiAwarenessContext;
    DPI_AWARENESS_CONTEXT targetDpiCtx = pGetWindowDpiAwarenessContext
        ? pGetWindowDpiAwarenessContext(hParentHWND) : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
    DPI_AWARENESS_CONTEXT prevDpiCtx = pSetDpi ? pSetDpi(targetDpiCtx) : nullptr;

    HWND hTargetWnd = hParentHWND;
    POINT adjustedPos = appMousePos;
    HWND hTmpWnd = NULL;

    RECT xcRect;

    GetClientRect(hParentHWND, &xcRect);
    const long width = xcRect.right - xcRect.left;
    const long height = xcRect.bottom - xcRect.top;
    if (width > 0 && height > 0) {
        adjustedPos.x = MulDiv(appMousePos.x, width, XC_WIDTH);
        adjustedPos.y = MulDiv(appMousePos.y, height, XC_HEIGHT);
    }
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

    const LPARAM lParam = MAKELPARAM(adjustedPos.x, adjustedPos.y);

    // SDL2 ignores the coordinates of button messages: a click lands at the position of
    // the last WM_MOUSEMOVE. Moves are only forwarded while the button is held, so move
    // the pointer to the click position first, as a real mouse would.
    if (uMsg != WM_MOUSEMOVE) {
        WPARAM moveFlags = wParam;
        if (uMsg == WM_LBUTTONUP) moveFlags |= MK_LBUTTON;
        if (uMsg == WM_RBUTTONUP) moveFlags |= MK_RBUTTON;
        PostMessage(hTargetWnd, WM_MOUSEMOVE, moveFlags, lParam);
    }

    const BOOL posted = PostMessage(hTargetWnd, uMsg, WithButtonFlags(uMsg, wParam), lParam);

    if (pSetDpi && prevDpiCtx) {
        pSetDpi(prevDpiCtx);
    }

    return posted;
}