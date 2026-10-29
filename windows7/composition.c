/* w7-composition: turns the Windows 7 session's desktop composition off or on
 * again, as Windows 7 does when its DWM stops or comes back: the windows go
 * to Windows 7 Basic and back to Aero.
 *
 * The session's supervisor runs it when w7-dwm (the session's compositor)
 * exits and when it runs again. It sets HKCU\Software\Win4Wine Composition,
 * which dwmapi, uxtheme and win32u read (a volatile value: a new session
 * starts with composition), then tells every top-level window: off, their
 * glass goes (winex11 makes their surfaces again without it); either way they
 * get WM_DWMCOMPOSITIONCHANGED, as from Windows 7's DWM (Explorer's taskbar
 * and folder windows choose their looks again), and their frames are drawn
 * again by uxtheme, glass or Basic.
 *
 * Usage: w7-composition on|off */
#include <windows.h>
#include <stdio.h>

#ifndef WM_DWMCOMPOSITIONCHANGED
#define WM_DWMCOMPOSITIONCHANGED 0x031e
#endif

static BOOL enabled;

static BOOL CALLBACK tell(HWND hwnd, LPARAM param)
{
    static const WCHAR *glass[] = {
        L"__win4wine_glass", L"__win4wine_glass_frame", L"__win4wine_glass_frame2",
        L"__win4wine_glass_margins", L"__win4wine_glass_margins2", L"__win4wine_glass_sheet",
    };
    DWORD_PTR result;
    unsigned int i;
    (void)param;
    if (!enabled)
        for (i = 0; i < ARRAYSIZE(glass); i++) RemovePropW(hwnd, glass[i]);
    SendMessageTimeoutW(hwnd, WM_DWMCOMPOSITIONCHANGED, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
    SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    HKEY key;
    DWORD value;
    if (argc != 2 || (wcscmp(argv[1], L"on") && wcscmp(argv[1], L"off")))
    {
        fprintf(stderr, "usage: w7-composition on|off\n");
        return 2;
    }
    enabled = !wcscmp(argv[1], L"on");
    value = enabled;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Win4Wine", 0, NULL, REG_OPTION_VOLATILE, KEY_SET_VALUE,
                        NULL, &key, NULL))
        return 1;
    RegSetValueExW(key, L"Composition", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    /* the programs read it again within a quarter of a second */
    Sleep(300);
    EnumWindows(tell, 0);
    return 0;
}
