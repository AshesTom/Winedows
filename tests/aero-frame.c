/* Aero frame regressions: first paint, resize, maximize/restore and Explorer's
 * titleless caption. Run in an external Windows 7 Aero session. */
#include <windows.h>
#include <uxtheme.h>
#include <stdio.h>

#ifndef WTNCA_NODRAWCAPTION
#define WTNCA_NODRAWCAPTION 1
#define WTNCA_NODRAWICON 2
#endif

static BOOL (WINAPI *draw_frame)(HWND, HDC);

static int check_inner_outline(HWND hwnd)
{
    struct { DWORD flags, mask; } options = {WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON,
                                             WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON};
    HRESULT (WINAPI *set_options)(HWND, int, void *, DWORD) =
        (void *)GetProcAddress(GetModuleHandleW(L"uxtheme.dll"), "SetWindowThemeAttribute");
    RECT window, client, screen;
    POINT origin = {0};
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap, old;
    BITMAPINFO info = {0};
    DWORD *bits;
    int i, failed = 0, bx = GetSystemMetrics(SM_CXFRAME), by = GetSystemMetrics(SM_CYFRAME);
    POINT samples[4];
    GetWindowRect(hwnd, &screen);
    window = screen;
    OffsetRect(&window, -window.left, -window.top);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x - screen.left, origin.y - screen.top);
    samples[0].x = window.right / 2; samples[0].y = client.top - 1;
    samples[1].x = bx - 1; samples[1].y = window.bottom / 2;
    samples[2].x = window.right - bx; samples[2].y = window.bottom / 2;
    samples[3].x = window.right / 2; samples[3].y = window.bottom - by;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window.right;
    info.bmiHeader.biHeight = -window.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    old = SelectObject(dc, bitmap);
    if (!set_options || FAILED(set_options(hwnd, 1, &options, sizeof(options)))) failed = 1;
    draw_frame(hwnd, dc);
    GdiFlush();
    for (i = 0; i < 4; i++)
        if (bits[samples[i].y * window.right + samples[i].x])
        {
            printf("inner outline remains at %ld,%ld\n", samples[i].x, samples[i].y);
            failed = 1;
        }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    printf("Explorer inner outline: %s\n", failed ? "FAIL" : "PASS");
    return failed;
}

static int check_app_region(HWND hwnd)
{
    HRGN custom = CreateRectRgn(10, 10, 300, 200), actual = CreateRectRgn(0, 0, 0, 0);
    int failed;
    SetWindowRgn(hwnd, CreateRectRgn(10, 10, 300, 200), FALSE);
    draw_frame(hwnd, NULL);
    SetWindowPos(hwnd, NULL, 0, 0, 600, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    failed = GetWindowRgn(hwnd, actual) == ERROR || !EqualRgn(custom, actual);
    DeleteObject(custom); DeleteObject(actual);
    printf("application region preserved: %s\n", failed ? "FAIL" : "PASS");
    return failed;
}

static int check_frame_seams(HWND hwnd)
{
    RECT window, client;
    POINT origin = {0};
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap, old;
    BITMAPINFO info = {0};
    DWORD *bits;
    int width, height, caption_y, side_y, x, failed = 0;
    WCHAR title[512];
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    width = window.right - window.left; height = window.bottom - window.top;
    caption_y = (origin.y - window.top) / 2; side_y = height / 2;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bitmap) { DeleteDC(dc); return 1; }
    old = SelectObject(dc, bitmap);
    GetWindowTextW(hwnd, title, 512);
    SetWindowTextW(hwnd, L"");
    draw_frame(hwnd, dc); GdiFlush();
    SetWindowTextW(hwnd, title);
    /* The dark outer rim and light highlight must continue down both sides
     * with exactly the same pixels as on the vertical ends of the caption. */
    for (x = 0; x < 2; x++)
    {
        DWORD left_title = bits[caption_y * width + x], left_side = bits[side_y * width + x];
        DWORD right_title = bits[caption_y * width + width - 1 - x];
        DWORD right_side = bits[side_y * width + width - 1 - x];
        if (left_title != left_side || right_title != right_side)
        {
            printf("frame seam x=%d left=%08lx/%08lx right=%08lx/%08lx\n",
                   x, left_title, left_side, right_title, right_side);
            failed = 1;
        }
    }
    printf("caption and side outlines align: %s\n", failed ? "FAIL" : "PASS");
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    return failed;
}

static int check_fixed_geometry(HWND hwnd)
{
    RECT window, client, adjusted = {0, 0, 200, 150};
    POINT origin = {0};
    int border = GetSystemMetrics(SM_CXDLGFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    int failed;
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    failed = origin.x - window.left != border || window.right - origin.x - client.right != border ||
             window.bottom - origin.y - client.bottom != border;
    AdjustWindowRectEx(&adjusted, GetWindowLongW(hwnd, GWL_STYLE), FALSE,
                      GetWindowLongW(hwnd, GWL_EXSTYLE));
    if (adjusted.left != -border || adjusted.right != 200 + border || adjusted.bottom != 150 + border)
        failed = 1;
    if (SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(window.left + border / 2,
                                                     (window.top + window.bottom) / 2)) != HTBORDER)
        failed = 1;
    printf("fixed left/right/bottom borders (%d px), layout and hit testing: %s\n",
           border, failed ? "FAIL" : "PASS");
    return failed;
}

static int check_frame(HWND hwnd, const char *stage)
{
    RECT window, client;
    MSG msg;
    POINT origin = {0};
    HDC actual, expected;
    HBITMAP bitmap, old;
    int x, y, failed = 0, sampled = 0;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    GetWindowRect(hwnd, &window);
    OffsetRect(&window, -window.left, -window.top);
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, &origin, 1);
    {
        RECT screen;
        GetWindowRect(hwnd, &screen);
        OffsetRect(&client, origin.x - screen.left, origin.y - screen.top);
    }
    actual = GetWindowDC(hwnd);
    expected = CreateCompatibleDC(actual);
    bitmap = CreateCompatibleBitmap(actual, window.right, window.bottom);
    old = SelectObject(expected, bitmap);
    PatBlt(expected, 0, 0, window.right, window.bottom, BLACKNESS);
    draw_frame(hwnd, expected);
    /* Sample the caption and all three borders away from rounded corners,
     * icons, caption text, buttons and the resize grip. */
    for (y = 0; y < window.bottom; y++)
        for (x = 0; x < window.right; x++)
        {
            BOOL caption = y >= 4 && y < client.top - 3 && x > window.right / 2 && x < window.right - 150;
            BOOL side = y > client.top + 8 && y < client.bottom - 8 &&
                        (x < client.left || x >= client.right);
            BOOL bottom = y >= client.bottom && x >= 8 && x < window.right - 8;
            BOOL border = side || bottom;
            if (!caption && !border) continue;
            sampled++;
            if (GetPixel(actual, x, y) != GetPixel(expected, x, y))
            {
                if (failed < 8) printf("  mismatch %d,%d: %06lx expected %06lx\n", x, y,
                                       GetPixel(actual, x, y), GetPixel(expected, x, y));
                failed++;
            }
        }
    printf("%s: %d/%d frame pixels differ\n", stage, failed, sampled);
    if (!sampled) failed = 1;
    SelectObject(expected, old);
    DeleteObject(bitmap);
    DeleteDC(expected);
    ReleaseDC(hwnd, actual);
    return failed != 0;
}

int main(void)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    HMODULE theme = LoadLibraryW(L"uxtheme.dll");
    int failed = 0;
    draw_frame = (void *)GetProcAddress(theme, "Win4WineDrawNonClient");
    if (!draw_frame || !IsThemeActive()) return 2;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"AeroFrameRegression";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, L"Aero frame regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         60, 60, 640, 420, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("CreateWindow failed: %lu\n", GetLastError()); return 2; }
    UpdateWindow(hwnd);
    GdiFlush();
    failed |= check_frame(hwnd, "first paint");
    failed |= check_frame_seams(hwnd);
    SetWindowPos(hwnd, NULL, 0, 0, 800, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateWindow(hwnd);
    failed |= check_frame(hwnd, "grow");
    SetWindowPos(hwnd, NULL, 0, 0, 500, 340, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateWindow(hwnd);
    failed |= check_frame(hwnd, "shrink");
    SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
    failed |= check_frame(hwnd, "inactive");
    SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
    failed |= check_frame(hwnd, "active");
    ShowWindow(hwnd, SW_MAXIMIZE);
    failed |= check_frame(hwnd, "maximize");
    ShowWindow(hwnd, SW_RESTORE);
    failed |= check_frame(hwnd, "restore");
    failed |= check_inner_outline(hwnd);
    failed |= check_frame_seams(hwnd);
    failed |= check_app_region(hwnd);
    DestroyWindow(hwnd);
    /* Property sheets use a fixed dialog frame, narrower than the atlas's
     * corner margins. Exercise that geometry separately from Explorer. */
    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Properties frame regression",
                           WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 80, 80, 380, 500,
                           NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;
    UpdateWindow(hwnd);
    failed |= check_fixed_geometry(hwnd);
    failed |= check_frame_seams(hwnd);
    failed |= check_frame(hwnd, "fixed dialog");
    SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
    failed |= check_frame(hwnd, "fixed dialog inactive");
    failed |= check_frame_seams(hwnd);
    SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
    failed |= check_frame(hwnd, "fixed dialog active");
    DestroyWindow(hwnd);
    return failed;
}
