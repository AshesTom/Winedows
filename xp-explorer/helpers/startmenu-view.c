/* Draw XP's most-used programs in the lower part of its Start menu's left
 * column. Wine keeps the Start menu's native MFU list empty, so startmenu.exe
 * supplies the XP usage-ranked shortcuts and this child paints them with the
 * regular-weight font XP uses for recent programs.
 *
 * The view lives inside explorer's pinned list (a SysListView32 of another
 * process) and follows it as XP's own list would: the visual style's
 * separator under the pins, the system highlight colours for the hot item,
 * one selection across both lists, and a frame drawn as soon as the menu
 * opens (a WinEvent hook, the timer only as a fallback). */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <wchar.h>

#define MAX_ITEMS 64
#define ITEM_PATH_CHARS 260
#define RECENT_KEY L"Software\\XP Explorer\\Recent"
#define VIEW_CLASS L"XPRecentProgramsView"
#define HOST_CLASS L"XPRecentProgramsHost"

/* XP's left column: 40-pixel rows whose highlight is 36 pixels tall and
 * inset 2 pixels from the left, 3 from the right; 32-pixel icons. */
#define ROW_PITCH 40
#define ROW_HEIGHT 36
#define SEPARATOR_SPACE 5
#define ICON_LEFT 6
#define TEXT_LEFT 40

typedef struct { WCHAR path[ITEM_PATH_CHARS]; WCHAR name[ITEM_PATH_CHARS]; HICON icon; } RecentItem;
static RecentItem items[MAX_ITEMS];
static unsigned item_count;
static HWND menu_window, list_view, overlay;
static BOOL menu_was_visible;
static int hover_index = -1;
static RECT overlay_rect;
static BOOL overlay_positioned;
static HINSTANCE instance;
static HFONT item_font;
/* The pinned list's background, which this view continues: the visual
 * style's white, or the menu grey of Windows Classic. */
static COLORREF list_background = CLR_NONE;

/* A page in explorer's address space, for the list view messages that take
 * a pointer (item rectangles, item states). */
static HANDLE explorer_process;
static DWORD explorer_pid;
static BYTE *remote_page;

static void clear_items(void)
{
    for (unsigned i = 0; i < item_count; i++)
        if (items[i].icon) DestroyIcon(items[i].icon);
    ZeroMemory(items, sizeof(items));
    item_count = 0;
}

static BOOL same_items(WCHAR paths[][ITEM_PATH_CHARS], unsigned count)
{
    if (count != item_count) return FALSE;
    for (unsigned i = 0; i < count; i++)
        if (_wcsicmp(paths[i], items[i].path)) return FALSE;
    return TRUE;
}

static HICON shortcut_icon(const WCHAR *path)
{
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;
    WCHAR icon_path[MAX_PATH] = L"";
    int icon_index = 0;
    HICON icon = NULL;
    BOOL has_location = FALSE;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void **)&link)))
    {
        if (SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist)))
        {
            if (SUCCEEDED(IPersistFile_Load(persist, path, STGM_READ)) &&
                SUCCEEDED(IShellLinkW_GetIconLocation(link, icon_path, ARRAYSIZE(icon_path), &icon_index)) &&
                *icon_path)
            {
                has_location = TRUE;
                const WCHAR *ext = wcsrchr(icon_path, L'.');
                if (ext && !_wcsicmp(ext, L".ico"))
                    icon = (HICON)LoadImageW(NULL, icon_path, IMAGE_ICON, 32, 32,
                                             LR_LOADFROMFILE | LR_DEFAULTSIZE);
                else
                {
                    HICON large = NULL, small = NULL;
                    if (ExtractIconExW(icon_path, icon_index, &large, &small, 1) && large)
                        icon = large;
                    if (small) DestroyIcon(small);
                }
            }
            IPersistFile_Release(persist);
        }
        IShellLinkW_Release(link);
    }
    /* A broken custom icon should leave clean space instead of a generic
     * white file square. XP shortcuts without a custom icon use shell lookup. */
    if (!icon && !has_location)
    {
        SHFILEINFOW info;
        if (SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
            icon = info.hIcon;
    }
    return icon;
}

static void load_items(void)
{
    WCHAR data[MAX_ITEMS * ITEM_PATH_CHARS];
    WCHAR paths[MAX_ITEMS][ITEM_PATH_CHARS];
    DWORD bytes = sizeof(data), count = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, RECENT_KEY, L"Items", RRF_RT_REG_MULTI_SZ,
                     NULL, data, &bytes))
    {
        clear_items();
        return;
    }
    for (const WCHAR *p = data; *p && count < MAX_ITEMS; p += wcslen(p) + 1)
        if (wcslen(p) < ITEM_PATH_CHARS)
            wcscpy(paths[count++], p);
    if (same_items(paths, count)) return;
    clear_items();
    for (unsigned i = 0; i < count; i++)
    {
        const WCHAR *base;
        wcscpy(items[i].path, paths[i]);
        base = wcsrchr(paths[i], L'\\');
        base = base ? base + 1 : paths[i];
        wcsncpy(items[i].name, base, ITEM_PATH_CHARS - 1);
        items[i].name[ITEM_PATH_CHARS - 1] = 0;
        WCHAR *extension = wcsrchr(items[i].name, L'.');
        if (extension && !_wcsicmp(extension, L".lnk")) *extension = 0;
        items[i].icon = shortcut_icon(paths[i]);
    }
    item_count = count;
    hover_index = -1;
    if (overlay) InvalidateRect(overlay, NULL, FALSE);
}

static BOOL remote_buffer(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(list_view, &pid);
    if (!pid) return FALSE;
    if (pid == explorer_pid && remote_page) return TRUE;
    if (explorer_process)
    {
        if (remote_page) VirtualFreeEx(explorer_process, remote_page, 0, MEM_RELEASE);
        CloseHandle(explorer_process);
    }
    remote_page = NULL;
    explorer_pid = pid;
    explorer_process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (explorer_process)
        remote_page = VirtualAllocEx(explorer_process, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return remote_page != NULL;
}

static BOOL list_message(UINT message, WPARAM wparam, void *data, SIZE_T size)
{
    DWORD_PTR result = 0;
    if (!remote_buffer() || !WriteProcessMemory(explorer_process, remote_page, data, size, NULL)) return FALSE;
    if (!SendMessageTimeoutW(list_view, message, wparam, (LPARAM)remote_page,
                             SMTO_ABORTIFHUNG, 200, &result) || !result)
        return FALSE;
    return ReadProcessMemory(explorer_process, remote_page, data, size, NULL);
}

/* The pins' bottom edge in the list view, where XP's separator and its most
 * used programs follow. */
static int pins_bottom(void)
{
    DWORD_PTR count = 0;
    RECT rect = { .left = LVIR_BOUNDS };
    if (!SendMessageTimeoutW(list_view, LVM_GETITEMCOUNT, 0, 0, SMTO_ABORTIFHUNG, 200, &count) || !count)
        return 0;
    if (list_message(LVM_GETITEMRECT, count - 1, &rect, sizeof(rect))) return rect.bottom;
    return (int)count * ROW_PITCH;
}

static BOOL native_selection(void)
{
    DWORD_PTR selected = 0;
    return list_view && SendMessageTimeoutW(list_view, LVM_GETSELECTEDCOUNT, 0, 0, SMTO_ABORTIFHUNG, 200, &selected) &&
           selected;
}

/* XP's two program lists share a single highlight: hovering a recent program
 * takes it from the pin that had it. */
static void clear_native_selection(int y)
{
    LVITEMW item = { .stateMask = LVIS_SELECTED | LVIS_FOCUSED };
    if (!list_view) return;
    /* The Start menu tracks its hot pin itself, from the list's mouse moves:
     * tell it that the pointer is below the pins, or pointing at the same pin
     * again would not select it. */
    PostMessageW(list_view, WM_MOUSEMOVE, 0, MAKELPARAM(ICON_LEFT, overlay_rect.top + y));
    if (native_selection()) list_message(LVM_SETITEMSTATE, (WPARAM)-1, &item, sizeof(item));
}

/* IsWindowVisible is already false once the menu is hidden: test the view's
 * own style, or it stays shown and is not shown again when the menu opens. */
static void hide_overlay(void)
{
    if (overlay && (GetWindowLongW(overlay, GWL_STYLE) & WS_VISIBLE)) ShowWindow(overlay, SW_HIDE);
}

static void show_overlay(void)
{
    RECT client;
    int top;
    if (!list_view || !IsWindowVisible(list_view) || !GetClientRect(list_view, &client))
    {
        hide_overlay();
        return;
    }
    /* The list view paints over its children unless it clips them: without
     * this, the menu erases this view every other time it opens. */
    LONG style = GetWindowLongW(list_view, GWL_STYLE);
    if (!(style & WS_CLIPCHILDREN)) SetWindowLongW(list_view, GWL_STYLE, style | WS_CLIPCHILDREN);
    if (!overlay || !IsWindow(overlay) || GetParent(overlay) != list_view)
    {
        if (overlay && IsWindow(overlay)) DestroyWindow(overlay);
        overlay = CreateWindowExW(0, VIEW_CLASS, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                  list_view, NULL, instance, NULL);
        if (!overlay) return;
        overlay_positioned = FALSE;
    }
    load_items();
    DWORD_PTR background = CLR_NONE;
    list_background = SendMessageTimeoutW(list_view, LVM_GETBKCOLOR, 0, 0, SMTO_ABORTIFHUNG, 200, &background)
                      ? (COLORREF)background : CLR_NONE;
    top = pins_bottom();
    if (client.bottom - top < ROW_PITCH) return;
    RECT desired = { 0, top, client.right, client.bottom };
    if (!overlay_positioned || !EqualRect(&overlay_rect, &desired))
    {
        SetWindowPos(overlay, HWND_TOP, desired.left, desired.top, desired.right - desired.left,
                     desired.bottom - desired.top, SWP_NOACTIVATE);
        overlay_rect = desired;
        overlay_positioned = TRUE;
    }
    if (!(GetWindowLongW(overlay, GWL_STYLE) & WS_VISIBLE)) ShowWindow(overlay, SW_SHOWNOACTIVATE);
    RedrawWindow(overlay, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

static BOOL CALLBACK find_left_list(HWND hwnd, LPARAM param)
{
    HWND menu = (HWND)param;
    WCHAR class_name[64];
    RECT r, m;
    if (!GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) || wcscmp(class_name, L"SysListView32") ||
        !GetWindowRect(hwnd, &r) || !GetWindowRect(menu, &m)) return TRUE;
    if (r.left < (m.left + m.right) / 2 && r.right > m.left && r.bottom - r.top > 120)
    {
        list_view = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void menu_closed(void)
{
    /* XP records a program after it is launched from the Start menu.
     * Rebuild the ranked list for the next time the menu opens. */
    WCHAR command[] = L"\"C:\\xp\\bin\\startmenu.exe\" recent";
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process;
    if (CreateProcessW(L"C:\\xp\\bin\\startmenu.exe", command, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

static void refresh_overlay(void)
{
    HWND found = FindWindowW(L"DV2ControlHost", L"Start Menu");
    BOOL visible = found && IsWindowVisible(found);
    if (!visible)
    {
        if (menu_was_visible) menu_closed();
        menu_was_visible = FALSE;
        hover_index = -1;
        hide_overlay();
        menu_window = NULL;
        list_view = NULL;
        return;
    }
    if (menu_was_visible && list_view && IsWindow(list_view) && overlay && IsWindowVisible(overlay)) return;
    menu_window = found;
    list_view = NULL;
    EnumChildWindows(menu_window, find_left_list, (LPARAM)menu_window);
    if (!list_view) return;
    menu_was_visible = TRUE;
    show_overlay();
}

static void draw_separator(HWND hwnd, HDC dc, int width, int y)
{
    HTHEME theme = OpenThemeData(hwnd, L"StartPanel");
    SIZE size = { 0, 1 };
    if (theme) GetThemePartSize(theme, dc, SPP_PROGLISTSEPARATOR, 0, NULL, TS_TRUE, &size);
    if (size.cy <= 0) size.cy = 1;
    RECT line = { 0, y + (SEPARATOR_SPACE - size.cy) / 2, width, y + (SEPARATOR_SPACE - size.cy) / 2 + size.cy };
    if (theme && SUCCEEDED(DrawThemeBackground(theme, dc, SPP_PROGLISTSEPARATOR, 0, &line, NULL)))
    {
        CloseThemeData(theme);
        return;
    }
    if (theme) CloseThemeData(theme);
    /* Windows Classic: a plain line in the 3D shadow colour. */
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, 6, line.top, NULL); LineTo(dc, width - 6, line.top);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static RECT row_rect(const RECT *client, int index)
{
    int y = SEPARATOR_SPACE + index * ROW_PITCH;
    return (RECT){ 2, y, client->right - 3, y + ROW_HEIGHT };
}

static void draw_recent(HWND hwnd, HDC dc, const RECT *client)
{
    if (list_background == CLR_NONE)
        FillRect(dc, client, GetSysColorBrush(COLOR_WINDOW));
    else
    {
        HBRUSH background = CreateSolidBrush(list_background);
        FillRect(dc, client, background);
        DeleteObject(background);
    }
    if (overlay_rect.top > 0) draw_separator(hwnd, dc, client->right, 0);

    HGDIOBJ old_font = SelectObject(dc, item_font);
    SetBkMode(dc, TRANSPARENT);
    for (unsigned i = 0; i < item_count; i++)
    {
        RECT row = row_rect(client, (int)i);
        BOOL hot = (int)i == hover_index;
        if (row.bottom > client->bottom) break;
        if (hot) FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
        if (items[i].icon)
            DrawIconEx(dc, ICON_LEFT, row.top + (ROW_HEIGHT - 32) / 2, items[i].icon, 32, 32, 0, NULL, DI_NORMAL);
        RECT text = { TEXT_LEFT, row.top, row.right - 2, row.bottom };
        RECT measured = { 0, 0, text.right - text.left, ROW_HEIGHT * 3 };
        DrawTextW(dc, items[i].name, -1, &measured,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        int text_height = measured.bottom - measured.top;
        if (text_height > ROW_HEIGHT) text_height = ROW_HEIGHT;
        text.top = row.top + (ROW_HEIGHT - text_height) / 2;
        text.bottom = text.top + text_height;
        SetTextColor(dc, GetSysColor(hot ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawTextW(dc, items[i].name, -1, &text,
                  DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    /* XP closes the left column with the same separator above All Programs. */
    if (item_count > 0)
    {
        int bottom = SEPARATOR_SPACE + (int)item_count * ROW_PITCH;
        if (bottom + SEPARATOR_SPACE <= client->bottom)
            draw_separator(hwnd, dc, client->right, bottom);
    }
    SelectObject(dc, old_font);
}

static void paint_recent(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right - client.left, height = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : NULL;
    HGDIOBJ old_bitmap = bitmap ? SelectObject(buffer, bitmap) : NULL;
    HDC target = bitmap ? buffer : dc;
    draw_recent(hwnd, target, &client);
    if (bitmap) BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    if (bitmap)
    {
        SelectObject(buffer, old_bitmap);
        DeleteObject(bitmap);
    }
    if (buffer) DeleteDC(buffer);
    EndPaint(hwnd, &ps);
}

static int row_at(HWND hwnd, int y)
{
    RECT client;
    GetClientRect(hwnd, &client);
    if (y < SEPARATOR_SPACE) return -1;
    int row = (y - SEPARATOR_SPACE) / ROW_PITCH;
    if ((unsigned)row >= item_count || row_rect(&client, row).bottom > client.bottom) return -1;
    return row;
}

/* Hot tracking repaints the two rows at once, as XP's list does, instead of
 * waiting for the next WM_PAINT. */
static void set_hover(int row)
{
    RECT client, old_row, new_row;
    int old = hover_index;
    if (!overlay || old == row) return;
    hover_index = row;
    GetClientRect(overlay, &client);
    if (old >= 0)
    {
        old_row = row_rect(&client, old);
        RedrawWindow(overlay, &old_row, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    if (row >= 0)
    {
        new_row = row_rect(&client, row);
        RedrawWindow(overlay, &new_row, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        clear_native_selection(new_row.top + ROW_HEIGHT / 2);
    }
}

static LRESULT CALLBACK view_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_PAINT:
        paint_recent(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONUP:
    {
        int row = row_at(hwnd, (int)(short)HIWORD(lparam));
        if (row >= 0)
        {
            WCHAR path[ITEM_PATH_CHARS];
            wcscpy(path, items[row].path);
            /* Close the menu the way the Start menu does itself (Esc in its
             * list): hiding its window from here left explorer believing it
             * open, and the Start button then no longer opened it. */
            if (list_view)
            {
                PostMessageW(list_view, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
                PostMessageW(list_view, WM_KEYUP, VK_ESCAPE, 0xC0010001);
            }
            ShellExecuteW(hwnd, L"open", path, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        /* Between rows and below the last one, XP keeps the last hot item. */
        int row = row_at(hwnd, (int)(short)HIWORD(lparam));
        if (row >= 0) set_hover(row);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        /* A pin under the cursor takes the highlight; leaving the menu
         * elsewhere keeps it, as in XP. */
        POINT cursor;
        RECT bounds;
        if (list_view && GetCursorPos(&cursor) && GetWindowRect(list_view, &bounds) && PtInRect(&bounds, cursor))
            set_hover(-1);
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        return TRUE;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void CALLBACK menu_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                                DWORD thread, DWORD time)
{
    WCHAR class_name[32];
    (void)hook; (void)child; (void)thread; (void)time;
    /* A pin that takes the highlight takes it from this view at once; the
     * mouse leave comes later, from Wine's tracking timer. */
    if (event == EVENT_OBJECT_SELECTION || event == EVENT_OBJECT_FOCUS)
    {
        if (hwnd == list_view && hover_index >= 0 && native_selection()) set_hover(-1);
        return;
    }
    if (object != OBJID_WINDOW || !hwnd || !GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) ||
        wcscmp(class_name, L"DV2ControlHost"))
        return;
    refresh_overlay();
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_TIMER)
    {
        refresh_overlay();
        return 0;
    }
    if (message == WM_DESTROY)
    {
        if (item_font) DeleteObject(item_font);
        clear_items();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE previous, WCHAR *command_line, int show)
{
    WNDCLASSW cls = {0};
    MSG message;
    (void)previous; (void)command_line; (void)show;
    instance = app;
    CoInitialize(NULL);
    cls.hInstance = app;
    cls.lpfnWndProc = view_proc;
    cls.lpszClassName = VIEW_CLASS;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&cls)) return 1;
    cls.lpfnWndProc = host_proc;
    cls.lpszClassName = HOST_CLASS;
    if (!RegisterClassW(&cls)) return 1;
    NONCLIENTMETRICSW metrics = { .cbSize = sizeof(metrics) };
    /* XP's recent programs use the menu font at regular weight. */
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        metrics.lfMenuFont.lfWeight = FW_NORMAL;
        item_font = CreateFontIndirectW(&metrics.lfMenuFont);
    }
    if (!item_font)
        item_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                FF_SWISS, L"Tahoma");
    HWND host = CreateWindowExW(0, HOST_CLASS, L"", 0, 0, 0, 0, 0,
                                NULL, NULL, app, NULL);
    if (!host) return 1;
    load_items();
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SELECTION, NULL, menu_event, 0, 0,
                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    /* The timer catches what the hook misses (a menu shown before this
     * started, or a Wine without cross-process WinEvents). */
    SetTimer(host, 1, 150, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return (int)message.wParam;
}
