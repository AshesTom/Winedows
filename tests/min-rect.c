/* WM_KLUDGEMINRECT across processes (0182): another process asks a shell hook
 * window for a window's taskbar button, and DefWindowProc answers it with the
 * window's own HSHELL_GETMINRECT, its rectangle packed as two POINTS. */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define WM_KLUDGEMINRECT 0x008b

typedef struct
{
    HWND hwnd;
    RECT rc;
} shell_hook_info;

static const RECT button = {-20, 770, 140, 795};
static UINT shell_message;
static HWND asked;

static LRESULT CALLBACK band(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == shell_message && wp == HSHELL_GETMINRECT)
    {
        shell_hook_info *info = (shell_hook_info *)lp;
        POINTS *points = (POINTS *)&info->rc;
        asked = info->hwnd;
        points[0].x = (SHORT)button.left;
        points[0].y = (SHORT)button.top;
        points[1].x = (SHORT)button.right;
        points[1].y = (SHORT)button.bottom;
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int ask(HWND hook)
{
    RECT rect = {1, 2, 3, 4};
    DWORD_PTR result;

    if (!SendMessageTimeoutW(hook, WM_KLUDGEMINRECT, (WPARAM)GetDesktopWindow(), (LPARAM)&rect,
                             SMTO_NORMAL, 2000, &result))
        return 10;
    printf("rect=%ld,%ld,%ld,%ld\n", rect.left, rect.top, rect.right, rect.bottom);
    return EqualRect(&rect, &button) ? 0 : 11;
}

int wmain(int argc, WCHAR **argv)
{
    WNDCLASSW cls = {0};
    WCHAR command[MAX_PATH + 32], path[MAX_PATH];
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    DWORD code = 1;
    HWND hook;
    MSG msg;

    if (argc == 3 && !wcscmp(argv[1], L"ask")) return ask((HWND)(ULONG_PTR)wcstoul(argv[2], NULL, 16));

    cls.lpfnWndProc = band;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"Win4WineTestMinRect";
    if (!RegisterClassW(&cls)) return 1;
    shell_message = RegisterWindowMessageW(L"SHELLHOOK");
    hook = CreateWindowW(cls.lpszClassName, L"min rect", 0, 0, 0, 1, 1, NULL, NULL, cls.hInstance, NULL);
    if (!hook) return 2;
    GetModuleFileNameW(NULL, path, MAX_PATH);
    swprintf(command, ARRAYSIZE(command), L"\"%ls\" ask %lx", path, (unsigned long)(ULONG_PTR)hook);
    if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return 3;
    while (MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    GetExitCodeProcess(process.hProcess, &code);
    printf("child=%lu asked=%s\n", code, asked == GetDesktopWindow() ? "yes" : "no");
    DestroyWindow(hook);
    return code ? (int)code : asked == GetDesktopWindow() ? 0 : 12;
}
