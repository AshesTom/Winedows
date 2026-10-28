/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Control Panel's GTK/QT Themes and Icons items (Appearance and Themes): how
 * the session's Linux programs look. They edit ~/.config/win4wine/linux-look.conf,
 * "name=value" lines that the Qt style and the GTK theme's generator
 * (qt-style/src/session.cpp) watch, so open programs follow at once:
 *
 *   style=standard|accurate   the Standard or the Accurate style
 *   icon_theme=NAME           an icon theme's folder name; none: the desktop's
 *   theme_build=N             raised by Rebuild: the saved GTK theme is made again
 *
 * The icon themes are the folders with an index.theme of icons (not only
 * cursors) in ~/.local/share/icons, ~/.icons and /usr/share/icons; Windows XP's
 * is the one the session made from the user's XP files (session/icon_theme.py).
 * Their samples are drawn by xpstyle-gtk (~/.cache/win4wine/icon-previews).
 * The items' icons are XP's, from the user's media.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <prsht.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "linuxlook.h"

#define MAX_THEMES 256
#define XP_ICONS L"win4wine-xp-icons"

struct settings { BOOL accurate; WCHAR icons[128]; int build; };
struct theme { WCHAR folder[128]; WCHAR name[128]; WCHAR comment[256]; };

static HINSTANCE instance;
static struct settings saved, chosen;
static struct theme themes[MAX_THEMES];
static unsigned theme_count;
static HIMAGELIST toolbar;
static HBITMAP sample;
static UINT sample_width, sample_height;

/* The Linux home directory: Wine names it in WINEHOMEDIR ("\\??\\H:\\"). */
static void home_path(const WCHAR *relative, WCHAR *out, size_t size)
{
    WCHAR home[MAX_PATH] = L"", *base = home;
    size_t length;
    if (!GetEnvironmentVariableW(L"WINEHOMEDIR", home, MAX_PATH) || !home[0])
        lstrcpynW(home, L"H:\\", MAX_PATH);
    if (!wcsncmp(base, L"\\??\\", 4)) base += 4;
    length = wcslen(base);
    swprintf(out, size, L"%ls%ls%ls", base, length && base[length - 1] == '\\' ? L"" : L"\\", relative);
}

static void load_settings(struct settings *s)
{
    WCHAR path[MAX_PATH + 64];
    char line[512];
    FILE *file;
    memset(s, 0, sizeof(*s));
    home_path(L".config\\win4wine\\linux-look.conf", path, ARRAYSIZE(path));
    if (!(file = _wfopen(path, L"rb"))) return;
    while (fgets(line, sizeof(line), file))
    {
        char *equals = strchr(line, '='), *value;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || !equals) continue;
        *equals = 0;
        value = equals + 1;
        if (!strcmp(line, "style")) s->accurate = !_stricmp(value, "accurate");
        else if (!strcmp(line, "icon_theme"))
            MultiByteToWideChar(CP_UTF8, 0, value, -1, s->icons, ARRAYSIZE(s->icons));
        else if (!strcmp(line, "theme_build")) s->build = atoi(value);
    }
    fclose(file);
}

static BOOL save_settings(HWND owner, const struct settings *s)
{
    WCHAR path[MAX_PATH + 64], temporary[MAX_PATH + 64], *slash;
    char icons[512];
    FILE *file;
    home_path(L".config\\win4wine\\linux-look.conf", path, ARRAYSIZE(path));
    swprintf(temporary, ARRAYSIZE(temporary), L"%ls.new", path);
    slash = wcsrchr(temporary, '\\');
    if (slash) { *slash = 0; CreateDirectoryW(temporary, NULL); *slash = '\\'; }
    WideCharToMultiByte(CP_UTF8, 0, s->icons, -1, icons, sizeof(icons), NULL, NULL);
    if ((file = _wfopen(temporary, L"wb")))
    {
        fprintf(file, "# How Linux programs look in the XP session: Control Panel > Appearance and Themes >\n"
                      "# GTK/QT Themes and Icons edit this file.\n"
                      "style=%s\nicon_theme=%s\ntheme_build=%d\n", s->accurate ? "accurate" : "standard", icons, s->build);
        fclose(file);
        /* Renamed over the old file: the programs watching it never read half of it. */
        if (MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING)) return TRUE;
    }
    MessageBoxW(owner, L"The settings could not be saved.", L"Control Panel", MB_ICONERROR);
    return FALSE;
}

/* An icon of one of XP's own files (the user's media in C:\xp\native), or a plain one. */
static HICON xp_icon(const WCHAR *file, int group)
{
    WCHAR path[MAX_PATH];
    HMODULE module;
    HICON icon = NULL;
    swprintf(path, ARRAYSIZE(path), L"C:\\xp\\native\\%ls", file);
    if (!(module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE)))
        module = LoadLibraryExW(file, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (module)
    {
        icon = LoadImageW(module, MAKEINTRESOURCEW(group), IMAGE_ICON, 32, 32, 0);
        FreeLibrary(module);
    }
    return icon ? icon : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
}

/* Explorer's 16 x 16 toolbar pictures (shell32's bitmap 216: Cut, Copy, Paste, ...). */
static HIMAGELIST toolbar_images(void)
{
    HMODULE module = LoadLibraryExW(L"C:\\xp\\native\\shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    HIMAGELIST list = NULL;
    HBITMAP strip;
    if (!module) return NULL;
    if ((strip = LoadImageW(module, MAKEINTRESOURCEW(216), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION)))
    {
        if ((list = ImageList_Create(16, 16, ILC_COLOR32, 48, 0))) ImageList_Add(list, strip, NULL);
        DeleteObject(strip);
    }
    FreeLibrary(module);
    return list;
}

/* ---- GTK/QT Themes ---- */

/* A push button of the visual style (or Windows Classic's), its text and, in the Standard style, its picture. */
static void preview_button(HWND window, HDC dc, RECT r, const WCHAR *text, int picture, BOOL defaulted)
{
    HTHEME theme = OpenThemeData(window, L"BUTTON");
    SIZE extent;
    int x;
    if (theme)
    {
        DrawThemeBackground(theme, dc, BP_PUSHBUTTON, defaulted ? PBS_DEFAULTED : PBS_NORMAL, &r, NULL);
        CloseThemeData(theme);
    }
    else
    {
        if (defaulted) { FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME)); InflateRect(&r, -1, -1); }
        DrawFrameControl(dc, &r, DFC_BUTTON, DFCS_BUTTONPUSH);
    }
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &extent);
    x = (r.left + r.right - extent.cx - (chosen.accurate || !toolbar ? 0 : 20)) / 2;
    if (!chosen.accurate && toolbar)
    {
        ImageList_Draw(toolbar, picture, dc, x, (r.top + r.bottom - 16) / 2, ILD_TRANSPARENT);
        x += 20;
    }
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    TextOutW(dc, x, (r.top + r.bottom - extent.cy) / 2, text, (int)wcslen(text));
}

/* What the chosen style changes: a context menu and a dialog's buttons, in
 * the shell's visual style and colours. The Standard style's sizes are
 * GTK's and Qt's own (Adwaita's menu items, about 22 pixels at 96 DPI); the
 * Accurate one's are XP's (17 pixels). */
static void draw_preview(HWND window, const DRAWITEMSTRUCT *item)
{
    static const struct { const WCHAR *text; int picture; } items[] = {
        { L"Open", 12 }, { L"Copy", 6 }, { L"Paste", 7 }, { NULL, 0 }, { L"Delete", 10 }, { L"Properties", 15 },
    };
    HDC dc = item->hDC;
    RECT r = item->rcItem, menu, button;
    UINT dpi = GetDeviceCaps(dc, LOGPIXELSY);
    int height = MulDiv(chosen.accurate ? 17 : 22, dpi, 96), separator = MulDiv(chosen.accurate ? 9 : 11, dpi, 96);
    int left = MulDiv(chosen.accurate ? 20 : 30, dpi, 96), y;
    HFONT old = SelectObject(dc, (HFONT)SendMessageW(window, WM_GETFONT, 0, 0));
    unsigned i;
    BOOL flat = FALSE;

    SystemParametersInfoW(SPI_GETFLATMENU, 0, &flat, 0);
    DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(dc, TRANSPARENT);
    /* The menu: COLOR_MENU in a COLOR_BTNSHADOW line (flat menus), 3 pixels inside it. */
    menu.left = r.left + MulDiv(12, dpi, 96);
    menu.top = r.top + MulDiv(8, dpi, 96);
    menu.right = menu.left + MulDiv(150, dpi, 96);
    menu.bottom = menu.top + 6 + 5 * height + separator;
    if (menu.bottom > r.bottom - 4) menu.bottom = r.bottom - 4;
    FillRect(dc, &menu, GetSysColorBrush(COLOR_MENU));
    FrameRect(dc, &menu, GetSysColorBrush(COLOR_BTNSHADOW));
    y = menu.top + 3;
    for (i = 0; i < ARRAYSIZE(items) && y + (items[i].text ? height : separator) <= menu.bottom - 3; i++)
    {
        RECT line = { menu.left + 3, y, menu.right - 3, y + (items[i].text ? height : separator) };
        if (!items[i].text)
        {
            RECT etch = { line.left + 1, y + separator / 2 - 1, line.right - 1, y + separator / 2 + 1 };
            DrawEdge(dc, &etch, BDR_SUNKENOUTER, BF_TOP | BF_BOTTOM);
            y += separator;
            continue;
        }
        if (i == 1)
        {
            FillRect(dc, &line, GetSysColorBrush(flat ? COLOR_MENUHILIGHT : COLOR_HIGHLIGHT));
            if (flat) FrameRect(dc, &line, GetSysColorBrush(COLOR_HIGHLIGHT));
        }
        if (!chosen.accurate && toolbar)
            ImageList_Draw(toolbar, items[i].picture, dc, line.left + MulDiv(5, dpi, 96), y + (height - 16) / 2, ILD_TRANSPARENT);
        SetTextColor(dc, GetSysColor(i == 1 ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        line.left = menu.left + left;
        DrawTextW(dc, items[i].text, -1, &line, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        y += height;
    }
    /* A dialog's buttons: 75 x 23 at 96 DPI. */
    button.right = r.right - MulDiv(12, dpi, 96);
    button.bottom = r.bottom - MulDiv(10, dpi, 96);
    button.left = button.right - MulDiv(75, dpi, 96);
    button.top = button.bottom - MulDiv(23, dpi, 96);
    preview_button(window, dc, button, L"Cancel", 10, FALSE);
    OffsetRect(&button, -MulDiv(81, dpi, 96), 0);
    preview_button(window, dc, button, L"OK", 15, TRUE);
    SelectObject(dc, old);
}

static INT_PTR CALLBACK style_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        load_settings(&saved);
        chosen = saved;
        if (!toolbar) toolbar = toolbar_images();
        CheckRadioButton(page, IDC_STANDARD, IDC_ACCURATE, saved.accurate ? IDC_ACCURATE : IDC_STANDARD);
        return TRUE;
    case WM_DRAWITEM:
        if (wparam == IDC_PREVIEW)
        {
            draw_preview(page, (const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_STANDARD:
        case IDC_ACCURATE:
            if (HIWORD(wparam) != BN_CLICKED) break;
            chosen.accurate = LOWORD(wparam) == IDC_ACCURATE;
            /* The descriptions between the two buttons end Wine's automatic group. */
            CheckRadioButton(page, IDC_STANDARD, IDC_ACCURATE, LOWORD(wparam));
            InvalidateRect(GetDlgItem(page, IDC_PREVIEW), NULL, FALSE);
            if (chosen.accurate != saved.accurate) PropSheet_Changed(GetParent(page), page);
            else PropSheet_UnChanged(GetParent(page), page);
            return TRUE;
        case IDC_REBUILD:
        {
            /* At once, as Internet Options' Delete Files: the GTK theme of the
             * style in use is made again; the choice of style still waits for OK. */
            struct settings now;
            load_settings(&now);
            now.build++;
            if (save_settings(page, &now))
            {
                saved.build = chosen.build = now.build;
                MessageBoxW(page, L"The GTK theme will be made again from the current visual style. "
                                  L"Open GTK programs take it in a few seconds.", L"GTK/QT Themes", MB_ICONINFORMATION);
            }
            return TRUE;
        }
        }
        break;
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == PSN_APPLY)
        {
            struct settings now;
            load_settings(&now);
            now.accurate = chosen.accurate;
            SetWindowLongPtrW(page, DWLP_MSGRESULT, save_settings(page, &now) ? PSNRET_NOERROR : PSNRET_INVALID);
            if (GetWindowLongPtrW(page, DWLP_MSGRESULT) == PSNRET_NOERROR) saved = now;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Icons ---- */

/* The [Icon Theme] group of an index.theme: its name and comment, and whether it has icons. */
static BOOL read_theme(const WCHAR *path, struct theme *theme)
{
    char line[1024];
    BOOL group = FALSE, directories = FALSE, hidden = FALSE;
    FILE *file = _wfopen(path, L"rb");
    if (!file) return FALSE;
    theme->name[0] = theme->comment[0] = 0;
    while (fgets(line, sizeof(line), file))
    {
        char *value;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { if (group) break; group = !strcmp(line, "[Icon Theme]"); continue; }
        if (!group || !(value = strchr(line, '='))) continue;
        *value++ = 0;
        if (!strcmp(line, "Name"))
            MultiByteToWideChar(CP_UTF8, 0, value, -1, theme->name, ARRAYSIZE(theme->name));
        else if (!strcmp(line, "Comment"))
            MultiByteToWideChar(CP_UTF8, 0, value, -1, theme->comment, ARRAYSIZE(theme->comment));
        else if (!strcmp(line, "Directories") || !strcmp(line, "ScaledDirectories"))
            directories |= value[0] != 0;
        else if (!strcmp(line, "Hidden"))
            hidden = !_stricmp(value, "true");
    }
    fclose(file);
    if (!theme->name[0]) lstrcpynW(theme->name, theme->folder, ARRAYSIZE(theme->name));
    return directories && !hidden;
}

static int compare_themes(const void *a, const void *b)
{
    const struct theme *x = a, *y = b;
    /* Windows XP's first, then by name */
    if (!wcscmp(x->folder, XP_ICONS) != !wcscmp(y->folder, XP_ICONS)) return wcscmp(x->folder, XP_ICONS) ? 1 : -1;
    return lstrcmpiW(x->name, y->name);
}

static void find_themes(void)
{
    WCHAR roots[4][MAX_PATH + 32];
    unsigned r, i;
    theme_count = 0;
    home_path(L".local\\share\\icons", roots[0], ARRAYSIZE(roots[0]));
    home_path(L".icons", roots[1], ARRAYSIZE(roots[1]));
    lstrcpyW(roots[2], L"Z:\\usr\\local\\share\\icons");
    lstrcpyW(roots[3], L"Z:\\usr\\share\\icons");
    for (r = 0; r < ARRAYSIZE(roots); r++)
    {
        WCHAR pattern[MAX_PATH + 64];
        WIN32_FIND_DATAW found;
        HANDLE search;
        swprintf(pattern, ARRAYSIZE(pattern), L"%ls\\*", roots[r]);
        if ((search = FindFirstFileW(pattern, &found)) == INVALID_HANDLE_VALUE) continue;
        do
        {
            WCHAR index[MAX_PATH * 2];
            BOOL known = FALSE;
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.' ||
                !lstrcmpiW(found.cFileName, L"hicolor") || !lstrcmpiW(found.cFileName, L"locolor") ||
                !lstrcmpiW(found.cFileName, L"default") || theme_count >= MAX_THEMES)
                continue;
            /* The user's own folder of a name comes first, as freedesktop's lookup takes it. */
            for (i = 0; i < theme_count && !known; i++) known = !wcscmp(themes[i].folder, found.cFileName);
            if (known) continue;
            lstrcpynW(themes[theme_count].folder, found.cFileName, ARRAYSIZE(themes[0].folder));
            swprintf(index, ARRAYSIZE(index), L"%ls\\%ls\\index.theme", roots[r], found.cFileName);
            if (read_theme(index, &themes[theme_count])) theme_count++;
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    qsort(themes, theme_count, sizeof(themes[0]), compare_themes);
}

/* The sample xpstyle-gtk drew of an icon theme: a PNG strip of 32 x 32 icons, as a premultiplied DIB. */
static void load_sample(const WCHAR *folder)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    WCHAR relative[MAX_PATH], path[MAX_PATH * 2];
    BITMAPINFO info;
    void *bits = NULL;
    UINT width = 0, height = 0;
    HRESULT com;

    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    if (sample) { DeleteObject(sample); sample = NULL; }
    if (!folder[0]) return;
    swprintf(relative, ARRAYSIZE(relative), L".cache\\win4wine\\icon-previews\\%ls.png", folder);
    home_path(relative, path, ARRAYSIZE(path));
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                   (void **)&factory)) &&
        SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA,
                                                 WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(IWICFormatConverter_GetSize(converter, &width, &height)) && width && height <= 256 && width <= 4096)
    {
        info.bmiHeader.biWidth = (LONG)width;
        info.bmiHeader.biHeight = -(LONG)height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        if ((sample = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0)) &&
            FAILED(IWICFormatConverter_CopyPixels(converter, NULL, width * 4, width * 4 * height, bits)))
        {
            DeleteObject(sample);
            sample = NULL;
        }
        sample_width = width;
        sample_height = height;
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (SUCCEEDED(com)) CoUninitialize();
}

/* The sample's icons over the dialog's face, each with its name below, as Explorer's Icons view shows them. */
static void draw_sample(HWND page, const DRAWITEMSTRUCT *item)
{
    static const WCHAR *names[] = { L"Folder", L"Document", L"Picture", L"Computer", L"Hard Disk", L"Recycle Bin" };
    HDC dc = item->hDC, memory;
    RECT r = item->rcItem;
    int cell = (r.right - r.left) / (int)ARRAYSIZE(names), i;
    HFONT old = SelectObject(dc, (HFONT)SendMessageW(page, WM_GETFONT, 0, 0));

    /* The tab page's own background (the visual style's), as the group box around it has. */
    if (FAILED(DrawThemeParentBackground(item->hwndItem, dc, &r))) FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    if (!sample)
    {
        DrawTextW(dc, chosen.icons[0] ? L"No sample of this icon theme is available yet." :
                                        L"Linux programs show the icons of the Linux desktop's theme.",
                  -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        return;
    }
    memory = CreateCompatibleDC(dc);
    SelectObject(memory, sample);
    for (i = 0; i < (int)ARRAYSIZE(names) && (UINT)(i + 1) * sample_height <= sample_width; i++)
    {
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        RECT label = { r.left + i * cell, r.top + (int)sample_height + 4, r.left + (i + 1) * cell, r.bottom };
        AlphaBlend(dc, r.left + i * cell + (cell - (int)sample_height) / 2, r.top + 2, sample_height, sample_height,
                   memory, i * sample_height, 0, sample_height, sample_height, blend);
        DrawTextW(dc, names[i], -1, &label, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
    DeleteDC(memory);
    SelectObject(dc, old);
}

/* The combo box's choice: its item data is the index in themes, or -1 for the desktop's. */
static void show_choice(HWND page)
{
    HWND combo = GetDlgItem(page, IDC_THEMES);
    LRESULT index = SendMessageW(combo, CB_GETITEMDATA, SendMessageW(combo, CB_GETCURSEL, 0, 0), 0);
    if (index >= 0 && (unsigned)index < theme_count)
    {
        lstrcpynW(chosen.icons, themes[index].folder, ARRAYSIZE(chosen.icons));
        SetDlgItemTextW(page, IDC_COMMENT, themes[index].comment);
    }
    else
    {
        chosen.icons[0] = 0;
        SetDlgItemTextW(page, IDC_COMMENT, L"The icons that the Linux desktop's settings choose.");
    }
    load_sample(chosen.icons);
    InvalidateRect(GetDlgItem(page, IDC_SAMPLE), NULL, FALSE);
}

static INT_PTR CALLBACK icons_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        HWND combo = GetDlgItem(page, IDC_THEMES);
        LRESULT position, selected;
        unsigned i;
        load_settings(&saved);
        chosen = saved;
        find_themes();
        selected = position = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"(Linux desktop default)");
        SendMessageW(combo, CB_SETITEMDATA, position, -1);
        for (i = 0; i < theme_count; i++)
        {
            position = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)themes[i].name);
            SendMessageW(combo, CB_SETITEMDATA, position, i);
            if (!wcscmp(themes[i].folder, saved.icons)) selected = position;
        }
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        show_choice(page);
        return TRUE;
    }
    case WM_DRAWITEM:
        if (wparam == IDC_SAMPLE)
        {
            draw_sample(page, (const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_THEMES && HIWORD(wparam) == CBN_SELCHANGE)
        {
            show_choice(page);
            if (wcscmp(chosen.icons, saved.icons)) PropSheet_Changed(GetParent(page), page);
            else PropSheet_UnChanged(GetParent(page), page);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == PSN_APPLY)
        {
            struct settings now;
            load_settings(&now);
            lstrcpynW(now.icons, chosen.icons, ARRAYSIZE(now.icons));
            SetWindowLongPtrW(page, DWLP_MSGRESULT, save_settings(page, &now) ? PSNRET_NOERROR : PSNRET_INVALID);
            if (GetWindowLongPtrW(page, DWLP_MSGRESULT) == PSNRET_NOERROR) saved = now;
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (sample) { DeleteObject(sample); sample = NULL; }
        break;
    }
    return FALSE;
}

static void show(HWND owner, int applet)
{
    /* XP's themed controls: the common controls 6 of this module's manifest. */
    ACTCTXW context = { 0 };
    HANDLE handle;
    ULONG_PTR cookie = 0;
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES };
    PROPSHEETPAGEW page = { 0 };
    PROPSHEETHEADERW sheet = { 0 };
    HPROPSHEETPAGE pages[1];
    context.cbSize = sizeof(context);
    context.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
    context.hModule = instance;
    context.lpResourceName = MAKEINTRESOURCEW(2);
    handle = CreateActCtxW(&context);
    if (handle != INVALID_HANDLE_VALUE) ActivateActCtx(handle, &cookie);
    InitCommonControlsEx(&controls);
    page.dwSize = sizeof(page);
    page.hInstance = instance;
    page.pszTemplate = MAKEINTRESOURCEW(applet ? IDD_ICONS : IDD_STYLE);
    page.pfnDlgProc = applet ? icons_proc : style_proc;
    pages[0] = CreatePropertySheetPageW(&page);
    sheet.dwSize = sizeof(sheet);
    sheet.dwFlags = PSH_NOCONTEXTHELP | PSH_USEHICON;
    sheet.hwndParent = owner;
    sheet.hInstance = instance;
    sheet.hIcon = applet ? xp_icon(L"shell32.dll", 4) : xp_icon(L"themeui.dll", 701);
    /* As XP names an item's sheet ("Display Properties"); comctl32's PSH_PROPTITLE would say "Properties for". */
    sheet.pszCaption = applet ? L"Icons Properties" : L"GTK/QT Themes Properties";
    sheet.nPages = 1;
    sheet.phpage = pages;
    if (pages[0]) PropertySheetW(&sheet);
    if (toolbar) { ImageList_Destroy(toolbar); toolbar = NULL; }
    if (handle != INVALID_HANDLE_VALUE)
    {
        DeactivateActCtx(0, cookie);
        ReleaseActCtx(handle);
    }
}

LONG CALLBACK CPlApplet(HWND owner, UINT message, LPARAM first, LPARAM second)
{
    switch (message)
    {
    case CPL_INIT:
        return TRUE;
    case CPL_GETCOUNT:
        return 2;
    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)second;
        info->idIcon = CPL_DYNAMIC_RES;
        info->idName = first ? IDS_ICONS_NAME : IDS_STYLE_NAME;
        info->idInfo = first ? IDS_ICONS_INFO : IDS_STYLE_INFO;
        info->lData = first;
        return 0;
    }
    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)second;
        memset(info, 0, sizeof(*info));
        info->dwSize = sizeof(*info);
        info->lData = first;
        /* XP's Themes picture (themeui.dll's), and its folder for icons (shell32's). */
        info->hIcon = first ? xp_icon(L"shell32.dll", 4) : xp_icon(L"themeui.dll", 701);
        LoadStringW(instance, first ? IDS_ICONS_NAME : IDS_STYLE_NAME, info->szName, ARRAYSIZE(info->szName));
        LoadStringW(instance, first ? IDS_ICONS_INFO : IDS_STYLE_INFO, info->szInfo, ARRAYSIZE(info->szInfo));
        return 0;
    }
    case CPL_DBLCLK:
        show(owner, (int)first);
        return 0;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        instance = module;
        DisableThreadLibraryCalls(module);
    }
    (void)reserved;
    return TRUE;
}
