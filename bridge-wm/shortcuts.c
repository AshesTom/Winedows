/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Control Panel's Keyboard Shortcuts item (Printers and Other Hardware):
 * shortcut keys that run a command on the Linux system, whatever window is
 * active. It edits ~/.config/win4wine/shortcuts.tsv, one "keys<TAB>command"
 * per line, which bridge-wm/hotkeys.py reads again when it changes; the
 * shortcut key is XP's own hotkey control, with a check box for the Windows
 * key it has not. Its icon is XP's keyboard (main.cpl, from the user's media).
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cpl.h>
#include <stdio.h>
#include <wchar.h>
#include "shortcuts.h"

#define MAX_ENTRIES 256

struct entry { WCHAR keys[64]; WCHAR command[1024]; };

static HINSTANCE instance;
static struct entry entries[MAX_ENTRIES];
static unsigned count;
static BOOL changed;
static struct entry editing;

/* Key names as hotkeys.py reads them, for the virtual keys of XP's hotkey control. */
static const struct { UINT vk; const WCHAR *name; } names[] = {
    { VK_SNAPSHOT, L"Print" }, { VK_SPACE, L"Space" }, { VK_RETURN, L"Return" }, { VK_TAB, L"Tab" },
    { VK_ESCAPE, L"Escape" }, { VK_BACK, L"Backspace" }, { VK_DELETE, L"Delete" }, { VK_INSERT, L"Insert" },
    { VK_HOME, L"Home" }, { VK_END, L"End" }, { VK_PRIOR, L"PageUp" }, { VK_NEXT, L"PageDown" },
    { VK_LEFT, L"Left" }, { VK_RIGHT, L"Right" }, { VK_UP, L"Up" }, { VK_DOWN, L"Down" }, { VK_PAUSE, L"Pause" },
};

static void key_name(UINT vk, WCHAR *out, size_t size)
{
    unsigned i;
    out[0] = 0;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { swprintf(out, size, L"%c", vk); return; }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf(out, size, L"F%u", vk - VK_F1 + 1); return; }
    for (i = 0; i < ARRAYSIZE(names); i++)
        if (names[i].vk == vk) { lstrcpynW(out, names[i].name, size); return; }
}

static UINT key_vk(const WCHAR *name)
{
    unsigned i;
    if (!name[1] && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return name[0];
    if (!name[1] && name[0] >= 'a' && name[0] <= 'z') return name[0] - 'a' + 'A';
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9')
    {
        int n = _wtoi(name + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    for (i = 0; i < ARRAYSIZE(names); i++)
        if (!lstrcmpiW(names[i].name, name)) return names[i].vk;
    if (!lstrcmpiW(name, L"Esc")) return VK_ESCAPE;
    if (!lstrcmpiW(name, L"Enter")) return VK_RETURN;
    if (!lstrcmpiW(name, L"Del")) return VK_DELETE;
    if (!lstrcmpiW(name, L"PgUp") || !lstrcmpiW(name, L"Prior")) return VK_PRIOR;
    if (!lstrcmpiW(name, L"PgDown") || !lstrcmpiW(name, L"Next")) return VK_NEXT;
    return 0;
}

/* "Ctrl+Shift+S" from the hotkey control's value and the Windows key. */
static BOOL format_keys(WORD hotkey, BOOL windows, WCHAR *out, size_t size)
{
    WCHAR key[16];
    key_name(LOBYTE(hotkey), key, ARRAYSIZE(key));
    if (!key[0]) return FALSE;
    swprintf(out, size, L"%ls%ls%ls%ls%ls", windows ? L"Win+" : L"", HIBYTE(hotkey) & HOTKEYF_CONTROL ? L"Ctrl+" : L"",
             HIBYTE(hotkey) & HOTKEYF_ALT ? L"Alt+" : L"", HIBYTE(hotkey) & HOTKEYF_SHIFT ? L"Shift+" : L"", key);
    return TRUE;
}

/* The hotkey control's value and the Windows key of "Meta+Shift+S" (0 if it has none). */
static WORD parse_keys(const WCHAR *text, BOOL *windows)
{
    WCHAR copy[64], *part, *next, *context = NULL;
    BYTE modifiers = 0;
    UINT vk = 0;
    *windows = FALSE;
    lstrcpynW(copy, text, ARRAYSIZE(copy));
    for (part = wcstok(copy, L"+", &context); part; part = next)
    {
        next = wcstok(NULL, L"+", &context);
        if (next)
        {
            if (!lstrcmpiW(part, L"Ctrl") || !lstrcmpiW(part, L"Control")) modifiers |= HOTKEYF_CONTROL;
            else if (!lstrcmpiW(part, L"Shift")) modifiers |= HOTKEYF_SHIFT;
            else if (!lstrcmpiW(part, L"Alt")) modifiers |= HOTKEYF_ALT;
            else if (!lstrcmpiW(part, L"Win") || !lstrcmpiW(part, L"Meta") || !lstrcmpiW(part, L"Super")) *windows = TRUE;
            else return 0;
        }
        else vk = key_vk(part);
    }
    return vk ? MAKEWORD(vk, modifiers) : 0;
}

/* The list's file, ~/.config/win4wine/shortcuts.tsv: Wine names the Linux
 * home directory in WINEHOMEDIR ("\\??\\H:\\" in the session's prefix). */
static void list_path(WCHAR *out, size_t size)
{
    WCHAR home[MAX_PATH] = L"", *base = home;
    size_t length;
    if (!GetEnvironmentVariableW(L"WINEHOMEDIR", home, MAX_PATH) || !home[0])
        lstrcpynW(home, L"H:\\", MAX_PATH);
    if (!wcsncmp(base, L"\\??\\", 4)) base += 4;
    length = wcslen(base);
    swprintf(out, size, L"%ls%ls.config\\win4wine\\shortcuts.tsv", base,
             length && base[length - 1] == '\\' ? L"" : L"\\");
}

static void load(void)
{
    WCHAR path[MAX_PATH + 64];
    char line[4096];
    FILE *file;
    count = 0;
    list_path(path, ARRAYSIZE(path));
    if (!(file = _wfopen(path, L"rb"))) return;
    while (count < MAX_ENTRIES && fgets(line, sizeof(line), file))
    {
        char *tab = strchr(line, '\t');
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || !tab) continue;
        *tab = 0;
        if (!MultiByteToWideChar(CP_UTF8, 0, line, -1, entries[count].keys, ARRAYSIZE(entries[count].keys))) continue;
        if (!MultiByteToWideChar(CP_UTF8, 0, tab + 1, -1, entries[count].command, ARRAYSIZE(entries[count].command)))
            continue;
        count++;
    }
    fclose(file);
}

static BOOL save(HWND owner)
{
    WCHAR path[MAX_PATH + 64], temporary[MAX_PATH + 64], *slash;
    char text[4096];
    FILE *file;
    unsigned i;
    list_path(path, ARRAYSIZE(path));
    swprintf(temporary, ARRAYSIZE(temporary), L"%ls.new", path);
    /* Its folder may not exist yet. */
    slash = wcsrchr(temporary, '\\');
    if (slash) { *slash = 0; CreateDirectoryW(temporary, NULL); *slash = '\\'; }
    if (!(file = _wfopen(temporary, L"wb")))
    {
        MessageBoxW(owner, L"The shortcuts could not be saved.", L"Keyboard Shortcuts", MB_ICONERROR);
        return FALSE;
    }
    fputs("# Keyboard shortcuts of the XP session: a key combination, a tab, a command.\n"
          "# Control Panel > Printers and Other Hardware > Keyboard Shortcuts edits this file.\n", file);
    for (i = 0; i < count; i++)
    {
        WideCharToMultiByte(CP_UTF8, 0, entries[i].keys, -1, text, sizeof(text), NULL, NULL);
        fputs(text, file);
        fputc('\t', file);
        WideCharToMultiByte(CP_UTF8, 0, entries[i].command, -1, text, sizeof(text), NULL, NULL);
        fputs(text, file);
        fputc('\n', file);
    }
    fclose(file);
    if (!MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING))
    {
        MessageBoxW(owner, L"The shortcuts could not be saved.", L"Keyboard Shortcuts", MB_ICONERROR);
        return FALSE;
    }
    changed = FALSE;
    return TRUE;
}

static void fill_list(HWND list, int select)
{
    unsigned i;
    ListView_DeleteAllItems(list);
    for (i = 0; i < count; i++)
    {
        LVITEMW item = { 0 };
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        /* Shown with spaces as XP's hotkey control shows them, KDE's Meta as Win. */
        {
            WCHAR spaced[128] = L"", copy[64], *part, *context = NULL;
            lstrcpynW(copy, entries[i].keys, ARRAYSIZE(copy));
            for (part = wcstok(copy, L"+", &context); part; part = wcstok(NULL, L"+", &context))
            {
                if (spaced[0]) wcsncat(spaced, L" + ", ARRAYSIZE(spaced) - wcslen(spaced) - 1);
                wcsncat(spaced, !lstrcmpiW(part, L"Meta") || !lstrcmpiW(part, L"Super") ? L"Win" : part,
                        ARRAYSIZE(spaced) - wcslen(spaced) - 1);
            }
            item.pszText = spaced;
            ListView_InsertItem(list, &item);
        }
        ListView_SetItemText(list, i, 1, entries[i].command);
    }
    if (select >= 0 && select < (int)count)
    {
        ListView_SetItemState(list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, select, FALSE);
    }
}

static void update_buttons(HWND dialog)
{
    BOOL selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_LIST), -1, LVNI_SELECTED) >= 0;
    EnableWindow(GetDlgItem(dialog, IDC_EDIT), selected);
    EnableWindow(GetDlgItem(dialog, IDC_REMOVE), selected);
    EnableWindow(GetDlgItem(dialog, IDC_APPLY), changed);
}

/* A file chosen with the Browse button, as the Linux path of a command. */
static void browse(HWND dialog)
{
    WCHAR file[MAX_PATH] = L"Z:\\usr\\bin\\", command[MAX_PATH + 8], *p;
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Browse";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    p = file;
    if ((p[0] == 'Z' || p[0] == 'z') && p[1] == ':') p += 2;
    for (WCHAR *q = p; *q; q++) if (*q == '\\') *q = '/';
    if (wcschr(p, ' ')) swprintf(command, ARRAYSIZE(command), L"'%ls'", p);
    else lstrcpynW(command, p, ARRAYSIZE(command));
    SetDlgItemTextW(dialog, IDC_COMMAND, command);
}

static INT_PTR CALLBACK entry_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        BOOL windows;
        WORD hotkey = parse_keys(editing.keys, &windows);
        SendDlgItemMessageW(dialog, IDC_HOTKEY, HKM_SETHOTKEY, hotkey, 0);
        CheckDlgButton(dialog, IDC_WIN, windows ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dialog, IDC_COMMAND, editing.command);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_BROWSE:
            browse(dialog);
            return TRUE;
        case IDOK:
        {
            WORD hotkey = (WORD)SendDlgItemMessageW(dialog, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);
            BOOL windows = IsDlgButtonChecked(dialog, IDC_WIN) == BST_CHECKED;
            WCHAR keys[64];
            GetDlgItemTextW(dialog, IDC_COMMAND, editing.command, ARRAYSIZE(editing.command));
            if (!format_keys(hotkey, windows, keys, ARRAYSIZE(keys)))
            {
                MessageBoxW(dialog, L"Press the keys of the shortcut in the Shortcut key box.", L"Keyboard Shortcuts",
                            MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(dialog, IDC_HOTKEY));
                return TRUE;
            }
            if (!editing.command[0])
            {
                MessageBoxW(dialog, L"Type the command that the shortcut runs.", L"Keyboard Shortcuts", MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(dialog, IDC_COMMAND));
                return TRUE;
            }
            lstrcpynW(editing.keys, keys, ARRAYSIZE(editing.keys));
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Edit entry INDEX, or a new one with -1; whether it changed. */
static BOOL edit_entry(HWND dialog, int index)
{
    unsigned i;
    if (index < 0 && count >= MAX_ENTRIES) return FALSE;
    if (index >= 0) editing = entries[index];
    else memset(&editing, 0, sizeof(editing));
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ENTRY), dialog, entry_proc, 0) != IDOK) return FALSE;
    for (i = 0; i < count; i++)
        if ((int)i != index && !lstrcmpiW(entries[i].keys, editing.keys))
        {
            WCHAR text[256];
            swprintf(text, ARRAYSIZE(text), L"%ls already starts \"%ls\". Replace it?", entries[i].keys, entries[i].command);
            if (MessageBoxW(dialog, text, L"Keyboard Shortcuts", MB_YESNO | MB_ICONQUESTION) != IDYES) return FALSE;
            memmove(&entries[i], &entries[i + 1], (count - i - 1) * sizeof(entries[0]));
            count--;
            if (index > (int)i) index--;
            break;
        }
    if (index < 0) index = (int)count++;
    entries[index] = editing;
    changed = TRUE;
    fill_list(GetDlgItem(dialog, IDC_LIST), index);
    return TRUE;
}

static INT_PTR CALLBACK main_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    HWND list = GetDlgItem(dialog, IDC_LIST);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        LVCOLUMNW column = { 0 };
        WCHAR text[64];
        RECT r;
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        GetClientRect(list, &r);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        LoadStringW(instance, IDS_KEY, text, ARRAYSIZE(text));
        column.pszText = text;
        column.cx = (r.right - r.left) * 2 / 5;
        ListView_InsertColumn(list, 0, &column);
        LoadStringW(instance, IDS_COMMAND, text, ARRAYSIZE(text));
        column.cx = (r.right - r.left) - column.cx - GetSystemMetrics(SM_CXVSCROLL);
        ListView_InsertColumn(list, 1, &column);
        load();
        changed = FALSE;
        fill_list(list, 0);
        update_buttons(dialog);
        return TRUE;
    }
    case WM_NOTIFY:
    {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom == IDC_LIST && header->code == LVN_ITEMCHANGED) update_buttons(dialog);
        if (header->idFrom == IDC_LIST && header->code == NM_DBLCLK)
        {
            int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (index >= 0) edit_entry(dialog, index);
            update_buttons(dialog);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_NEW:
            edit_entry(dialog, -1);
            update_buttons(dialog);
            return TRUE;
        case IDC_EDIT:
        {
            int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (index >= 0) edit_entry(dialog, index);
            update_buttons(dialog);
            return TRUE;
        }
        case IDC_REMOVE:
        {
            int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (index < 0) return TRUE;
            memmove(&entries[index], &entries[index + 1], (count - index - 1) * sizeof(entries[0]));
            count--;
            changed = TRUE;
            fill_list(list, index < (int)count ? index : (int)count - 1);
            update_buttons(dialog);
            return TRUE;
        }
        case IDC_APPLY:
            save(dialog);
            update_buttons(dialog);
            return TRUE;
        case IDOK:
            if (changed && !save(dialog)) return TRUE;
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* XP's keyboard icon from the user's main.cpl (prefix/extras.py), or a plain one. */
static HICON keyboard_icon(void)
{
    HMODULE xp = LoadLibraryExW(L"C:\\xp\\res\\main.cpl", NULL, LOAD_LIBRARY_AS_DATAFILE);
    HICON icon = xp ? LoadImageW(xp, MAKEINTRESOURCEW(200), IMAGE_ICON, 32, 32, 0) : NULL;
    return icon ? icon : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
}

static void show(HWND owner)
{
    /* XP's themed controls: the common controls 6 of this module's manifest. */
    ACTCTXW context = { 0 };
    HANDLE handle;
    ULONG_PTR cookie = 0;
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_HOTKEY_CLASS };
    context.cbSize = sizeof(context);
    context.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
    context.hModule = instance;
    context.lpResourceName = MAKEINTRESOURCEW(2);
    handle = CreateActCtxW(&context);
    if (handle != INVALID_HANDLE_VALUE) ActivateActCtx(handle, &cookie);
    InitCommonControlsEx(&controls);
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), owner, main_proc, 0);
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
        return 1;
    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)second;
        info->idIcon = CPL_DYNAMIC_RES;
        info->idName = IDS_NAME;
        info->idInfo = IDS_INFO;
        info->lData = 0;
        return 0;
    }
    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)second;
        memset(info, 0, sizeof(*info));
        info->dwSize = sizeof(*info);
        info->hIcon = keyboard_icon();
        LoadStringW(instance, IDS_NAME, info->szName, ARRAYSIZE(info->szName));
        LoadStringW(instance, IDS_INFO, info->szInfo, ARRAYSIZE(info->szInfo));
        return 0;
    }
    case CPL_DBLCLK:
        show(owner);
        return 0;
    }
    (void)first;
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
