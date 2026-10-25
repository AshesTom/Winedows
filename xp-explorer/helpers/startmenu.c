/* XP Start panel settings and pinned programs (the left column, above the
 * separator). Runs with XP's native shell32 (an AppDefaults override set by
 * scripts/prefix.py), whose Start menu pin list XP Explorer reads.
 *
 * Usage: startmenu.exe panel            turn XP's two-column Start panel on
 *        startmenu.exe webview          turn the folder task pane on (common tasks)
 *        startmenu.exe list             print the pinned items
 *        startmenu.exe clear            unpin everything
 *        startmenu.exe pin PATH...      pin shortcuts or programs, in order
 *        startmenu.exe unpin PATH...    unpin them
 *        startmenu.exe recent           update the Start menu's recent programs
 *
 * "recent" supplies XP's list of most used programs, which XP Explorer
 * leaves empty under Wine: like XP, it counts a program's uses from XP's own
 * UserAssist data (UEME_RUNPIDL entries for Start menu shortcuts), adds up
 * shortcuts that run the same executable, excludes helper programs, the
 * Internet and E-mail programs, and the user's pins. Recent\Items contains
 * shortcuts for startmenu-view.exe to draw below Internet and E-mail.
 * Recent\Pins tracks old versions' synthetic pins, removed during transition.
 *
 * XPStartMenuPin is the interface behind "Pin to Start menu"; its layout is
 * the one published in ReactOS's shlobj_undoc.h. */
#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>
#ifndef SSF_STARTPANELON
#define SSF_STARTPANELON 0x00200000
#endif

static const CLSID CLSID_XPStartMenuPin =
    {0xa2a9545d, 0xa0c2, 0x42b4, {0x97, 0x08, 0xa0, 0xb2, 0xba, 0xdd, 0x77, 0xc8}};
static const IID IID_XPStartMenuPin =
    {0xec35e37a, 0x6579, 0x4f3c, {0x93, 0xcd, 0x6e, 0x62, 0xc4, 0xef, 0x76, 0x36}};

typedef struct XPStartMenuPin XPStartMenuPin;
typedef struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(XPStartMenuPin *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(XPStartMenuPin *);
    ULONG (STDMETHODCALLTYPE *Release)(XPStartMenuPin *);
    HRESULT (STDMETHODCALLTYPE *EnumObjects)(XPStartMenuPin *, IEnumIDList **);
    HRESULT (STDMETHODCALLTYPE *Modify)(XPStartMenuPin *, LPCITEMIDLIST, LPCITEMIDLIST);
    HRESULT (STDMETHODCALLTYPE *GetChangeCount)(XPStartMenuPin *, ULONG *);
    HRESULT (STDMETHODCALLTYPE *IsPinnable)(XPStartMenuPin *, IDataObject *, DWORD, LPITEMIDLIST *);
    HRESULT (STDMETHODCALLTYPE *Resolve)(XPStartMenuPin *, HWND, DWORD, LPCITEMIDLIST, LPITEMIDLIST *);
} XPStartMenuPinVtbl;
struct XPStartMenuPin { const XPStartMenuPinVtbl *lpVtbl; };

static int webview(void)
{
    SHELLSTATE state = {0};
    state.fWebView = TRUE;
    SHGetSetSettings(&state, SSF_WEBVIEW, TRUE);
    state.fWebView = FALSE;
    SHGetSetSettings(&state, SSF_WEBVIEW, FALSE);
    if (!state.fWebView)
    {
        fprintf(stderr, "startmenu: the task pane setting was not saved\n");
        return 1;
    }
    return 0;
}

static int panel(void)
{
    SHELLSTATE state = {0};
    state.fStartPanelOn = TRUE;
    SHGetSetSettings(&state, SSF_STARTPANELON, TRUE);
    state.fStartPanelOn = FALSE;
    SHGetSetSettings(&state, SSF_STARTPANELON, FALSE);
    if (!state.fStartPanelOn)
    {
        fprintf(stderr, "startmenu: the Start panel setting was not saved\n");
        return 1;
    }
    return 0;
}

/* Calls VISIT for each pinned item; stops when it returns FALSE. */
static HRESULT each_pin(XPStartMenuPin *pins, BOOL (*visit)(XPStartMenuPin *, LPITEMIDLIST))
{
    IEnumIDList *list;
    LPITEMIDLIST item;
    HRESULT hr = pins->lpVtbl->EnumObjects(pins, &list);
    if (FAILED(hr)) return hr;
    while (IEnumIDList_Next(list, 1, &item, NULL) == S_OK)
    {
        BOOL more = visit(pins, item);
        CoTaskMemFree(item);
        if (!more) break;
    }
    IEnumIDList_Release(list);
    return S_OK;
}

static BOOL print_pin(XPStartMenuPin *pins, LPITEMIDLIST item)
{
    WCHAR path[MAX_PATH];
    char utf8[4 * MAX_PATH];
    (void)pins;
    if (!SHGetPathFromIDListW(item, path)) wcscpy(path, L"(virtual item)");
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
    printf("%s\n", utf8);
    return TRUE;
}

static BOOL remove_pin(XPStartMenuPin *pins, LPITEMIDLIST item)
{
    pins->lpVtbl->Modify(pins, item, NULL);
    return TRUE;
}

/* ---- "recent": the most used programs, as pins ---- */
#define RECENT_KEY L"Software\\XP Explorer\\Recent"
#define USERASSIST_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\" \
                       L"{75048700-EF1F-11D0-9888-006097DEACF9}\\Count"
#define MAX_PROGRAMS 512
#define MAX_LIST 64

struct program { WCHAR link[MAX_PATH], target[MAX_PATH]; DWORD runs, link_runs; ULONGLONG last; };
static struct program programs[MAX_PROGRAMS];
static unsigned program_count;
static WCHAR excluded[MAX_LIST][MAX_PATH];
static unsigned excluded_count;

static void rot13(WCHAR *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = 'a' + (*s - 'a' + 13) % 26;
        else if (*s >= 'A' && *s <= 'Z') *s = 'A' + (*s - 'A' + 13) % 26;
}

/* "%csidl2%\Accessories\KCalc.lnk" as a path; XP records shortcuts of the
 * common Programs folder under the user's CSIDL too. */
static BOOL expand_csidl(const WCHAR *in, WCHAR *out)
{
    static const int common[][2] = {{CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS}, {CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU},
                                    {CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY}};
    WCHAR folder[MAX_PATH];
    const WCHAR *rest;
    int csidl;
    if (_wcsnicmp(in, L"%csidl", 6) || !(rest = wcschr(in + 6, '%'))) return FALSE;
    csidl = _wtoi(in + 6);
    rest++;
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, 0, folder)))
    {
        swprintf(out, MAX_PATH, L"%ls%ls", folder, rest);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }
    for (unsigned i = 0; i < ARRAYSIZE(common); i++)
        if (common[i][0] == csidl && SUCCEEDED(SHGetFolderPathW(NULL, common[i][1], NULL, 0, folder)))
        {
            swprintf(out, MAX_PATH, L"%ls%ls", folder, rest);
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
        }
    return FALSE;
}

static BOOL link_target(const WCHAR *link, WCHAR *target)
{
    IShellLinkW *shell_link;
    IPersistFile *file;
    WCHAR raw[MAX_PATH] = L"";
    BOOL ok = FALSE;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&shell_link)))
        return FALSE;
    if (SUCCEEDED(IShellLinkW_QueryInterface(shell_link, &IID_IPersistFile, (void **)&file)))
    {
        if (SUCCEEDED(IPersistFile_Load(file, link, STGM_READ)) &&
            SUCCEEDED(IShellLinkW_GetPath(shell_link, raw, MAX_PATH, NULL, SLGP_RAWPATH)) && *raw)
            ok = ExpandEnvironmentStringsW(raw, target, MAX_PATH) && *target;
        IPersistFile_Release(file);
    }
    IShellLinkW_Release(shell_link);
    return ok;
}

/* Whether the ";"-separated list in FileAssociation's value NAME (or FALLBACK
 * when it is not set, or without NAME) has ITEM, or with SUBSTRING an entry
 * that ITEM contains. */
static BOOL in_list(const WCHAR *name, const WCHAR *fallback, const WCHAR *item, BOOL substring)
{
    WCHAR list[2048], lower_item[MAX_PATH], *entry, *next;
    DWORD size = sizeof(list);
    if (!name || RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileAssociation",
                              name, RRF_RT_REG_SZ, NULL, list, &size) || !*list)
        wcscpy(list, fallback);
    wcscpy(lower_item, item);
    CharLowerW(lower_item);
    CharLowerW(list);
    /* Split by hand: wcstok takes two or three arguments depending on the C runtime. */
    for (entry = list; entry; entry = next)
    {
        if ((next = wcschr(entry, ';'))) *next++ = 0;
        if (*entry && (substring ? wcsstr(lower_item, entry) != NULL : !wcscmp(lower_item, entry))) return TRUE;
    }
    return FALSE;
}

static BOOL is_noise(const WCHAR *link, const WCHAR *target)
{
    WCHAR name[MAX_PATH];
    const WCHAR *file = wcsrchr(target, '\\') ? wcsrchr(target, '\\') + 1 : target;
    /* Helper programs that open a program's documents (Raymond Chen, "Windows
     * Confidential: Determining What Is Frequently Used") and set-up programs. */
    if (in_list(L"AddRemoveApps", L"setup.exe;install.exe;isuninst.exe;uninst.exe;uninstal.exe;msiexec.exe;"
                L"srunonce.exe;rundll32.exe;rundll.exe;hh.exe;winhlp32.exe;winhelp.exe", file, FALSE) ||
        in_list(NULL, L"notepad.exe;wordpad.exe;write.exe;linux-launch.exe", file, FALSE))
        return TRUE;
    wcscpy(name, wcsrchr(link, '\\') ? wcsrchr(link, '\\') + 1 : link);
    return in_list(L"AddRemoveNames", L"documentation;help;install;more info;readme;read me;read first;setup;"
                   L"support;what's new;remove;uninstall", name, TRUE);
}

static BOOL is_excluded(const WCHAR *path)
{
    for (unsigned i = 0; i < excluded_count; i++) if (!_wcsicmp(excluded[i], path)) return TRUE;
    return FALSE;
}

static void exclude(const WCHAR *path)
{
    if (*path && excluded_count < MAX_LIST && !is_excluded(path)) wcscpy(excluded[excluded_count++], path);
}

/* XP leaves setup programs out of the most used list (Explorer\\FileAssociation's
 * AddRemoveApps); MSN's sign-up (msnsusii.exe, the "MSN" shortcut) only
 * installs MSN Explorer. */
static BOOL is_setup(const WCHAR *target)
{
    static const WCHAR *names[] = { L"msnsusii.exe", L"setup.exe", L"install.exe", L"uninst.exe",
                                    L"uninstall.exe", L"unwise.exe", L"msoobe.exe" };
    const WCHAR *file = wcsrchr(target, '\\');
    file = file ? file + 1 : target;
    for (unsigned i = 0; i < ARRAYSIZE(names); i++) if (!_wcsicmp(file, names[i])) return TRUE;
    return FALSE;
}

/* The program a Start Menu Internet Application (or mail client) runs. */
static void exclude_client(const WCHAR *kind)
{
    WCHAR key[MAX_PATH], name[MAX_PATH], command[MAX_PATH * 2], path[MAX_PATH], *end;
    DWORD size = sizeof(name);
    swprintf(key, MAX_PATH, L"Software\\Clients\\%ls", kind);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, name, &size)) return;
    swprintf(key, MAX_PATH, L"Software\\Clients\\%ls\\%ls\\shell\\open\\command", kind, name);
    size = sizeof(command);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, command, &size)) return;
    if (*command == '"' && (end = wcschr(command + 1, '"'))) { *end = 0; wcscpy(path, command + 1); }
    else { if ((end = wcschr(command, ' '))) *end = 0; wcscpy(path, command); }
    exclude(path);
}

static DWORD read_list(const WCHAR *name, WCHAR list[][MAX_PATH], DWORD max)
{
    WCHAR data[MAX_LIST * MAX_PATH];
    DWORD size = sizeof(data) - 2 * sizeof(WCHAR), count = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, RECENT_KEY, name, RRF_RT_REG_MULTI_SZ, NULL, data, &size)) return 0;
    for (const WCHAR *p = data; *p && count < max; p += wcslen(p) + 1)
        if (wcslen(p) < MAX_PATH) wcscpy(list[count++], p);
    return count;
}

static void write_list(const WCHAR *name, WCHAR list[][MAX_PATH], DWORD count)
{
    WCHAR data[MAX_LIST * MAX_PATH], *p = data;
    for (DWORD i = 0; i < count; i++) { wcscpy(p, list[i]); p += wcslen(p) + 1; }
    *p++ = 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, RECENT_KEY, name, REG_MULTI_SZ, data, (DWORD)((p - data) * sizeof(WCHAR)));
}

static WCHAR pinned_paths[MAX_LIST][MAX_PATH];
static unsigned pinned_count;

static BOOL collect_pin(XPStartMenuPin *pins, LPITEMIDLIST item)
{
    (void)pins;
    if (pinned_count < MAX_LIST && SHGetPathFromIDListW(item, pinned_paths[pinned_count])) pinned_count++;
    return TRUE;
}

static void count_uses(void)
{
    HKEY key;
    WCHAR name[1024], link[MAX_PATH], target[MAX_PATH];
    BYTE data[64];
    if (RegOpenKeyExW(HKEY_CURRENT_USER, USERASSIST_KEY, 0, KEY_READ, &key)) return;
    for (DWORD i = 0;; i++)
    {
        DWORD name_size = ARRAYSIZE(name), data_size = sizeof(data), type, count, runs;
        ULONGLONG last;
        LONG status = RegEnumValueW(key, i, name, &name_size, NULL, &type, data, &data_size);
        unsigned j;
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status || type != REG_BINARY || data_size < 16) continue;
        rot13(name);
        if (_wcsnicmp(name, L"UEME_RUNPIDL:", 13) || wcslen(name) < 17 || _wcsicmp(name + wcslen(name) - 4, L".lnk"))
            continue;
        memcpy(&count, data + 4, 4);
        memcpy(&last, data + 8, 8);
        /* XP stores the uses plus 5. */
        if (count <= 5 || !expand_csidl(name + 13, link) || !link_target(link, target) || is_noise(link, target))
            continue;
        runs = count - 5;
        for (j = 0; j < program_count && _wcsicmp(programs[j].target, target); j++);
        if (j == program_count)
        {
            if (program_count == MAX_PROGRAMS) continue;
            memset(&programs[j], 0, sizeof(programs[j]));
            wcscpy(programs[j].target, target);
            program_count++;
        }
        programs[j].runs += runs;
        if (last > programs[j].last) programs[j].last = last;
        /* The program's most used shortcut represents it, as on XP. */
        if (runs > programs[j].link_runs) { programs[j].link_runs = runs; wcscpy(programs[j].link, link); }
    }
    RegCloseKey(key);
}

static int compare_programs(const void *a, const void *b)
{
    const struct program *x = a, *y = b;
    if (x->runs != y->runs) return x->runs > y->runs ? -1 : 1;
    return x->last > y->last ? -1 : x->last < y->last ? 1 : 0;
}

static HRESULT modify(XPStartMenuPin *pins, const WCHAR *path, BOOL add)
{
    LPITEMIDLIST item = NULL;
    HRESULT hr = SHParseDisplayName(path, NULL, &item, 0, NULL);
    if (SUCCEEDED(hr)) hr = add ? pins->lpVtbl->Modify(pins, NULL, item) : pins->lpVtbl->Modify(pins, item, NULL);
    if (item) CoTaskMemFree(item);
    return hr;
}

static int recent(XPStartMenuPin *pins)
{
    static WCHAR managed[MAX_LIST][MAX_PATH], seeds[MAX_LIST][MAX_PATH], chosen[MAX_LIST][MAX_PATH],
                 targets[MAX_LIST][MAX_PATH];
    DWORD managed_count = read_list(L"Pins", managed, MAX_LIST), seed_count = read_list(L"Seeds", seeds, MAX_LIST);
    DWORD wanted = 6, size = sizeof(wanted), chosen_count = 0;

    /* XP's setup populates the lower column with familiar programs until
     * enough usage history exists. Use the shortcuts installed by extras.py
     * (XP's own, Windows Messenger itself rather than MSN's setup); after
     * that, UserAssist's most-used programs take their places. */
    if (!seed_count)
    {
        static const WCHAR *defaults[] = {
            L"Windows Media Player.lnk",
            L"Windows Messenger.lnk",
            L"Accessories\\Tour Windows XP.lnk",
            L"Accessories\\System Tools\\Files and Settings Transfer Wizard.lnk"
        };
        WCHAR folder[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, folder)))
            for (unsigned i = 0; i < ARRAYSIZE(defaults) && seed_count < MAX_LIST; i++)
            {
                swprintf(seeds[seed_count], MAX_PATH, L"%ls\\%ls", folder, defaults[i]);
                if (GetFileAttributesW(seeds[seed_count]) != INVALID_FILE_ATTRIBUTES) seed_count++;
            }
    }

    RegGetValueW(HKEY_CURRENT_USER, RECENT_KEY, L"Count", RRF_RT_REG_DWORD, NULL, &wanted, &size);
    if (wanted > MAX_LIST) wanted = MAX_LIST;
    each_pin(pins, collect_pin);
    /* The user's own pins and the Internet and E-mail programs are left out. */
    for (unsigned i = 0; i < pinned_count; i++)
    {
        BOOL ours = FALSE;
        for (DWORD j = 0; j < managed_count; j++) if (!_wcsicmp(managed[j], pinned_paths[i])) ours = TRUE;
        if (ours) continue;
        exclude(pinned_paths[i]);
        if (link_target(pinned_paths[i], targets[0])) exclude(targets[0]);
    }
    exclude_client(L"StartMenuInternet");
    exclude_client(L"Mail");
    count_uses();
    qsort(programs, program_count, sizeof(programs[0]), compare_programs);
    for (unsigned i = 0; i < program_count && chosen_count < wanted; i++)
    {
        if (is_excluded(programs[i].link) || is_excluded(programs[i].target) || is_setup(programs[i].target)) continue;
        wcscpy(targets[chosen_count], programs[i].target);
        wcscpy(chosen[chosen_count++], programs[i].link);
    }
    /* Until enough programs are used, the seeds fill the list. */
    for (DWORD i = 0; i < seed_count && chosen_count < wanted; i++)
    {
        WCHAR target[MAX_PATH];
        BOOL dup = FALSE;
        if (GetFileAttributesW(seeds[i]) == INVALID_FILE_ATTRIBUTES || !link_target(seeds[i], target) ||
            is_excluded(seeds[i]) || is_excluded(target) || is_setup(target))
            continue;
        for (DWORD j = 0; j < chosen_count; j++) if (!_wcsicmp(targets[j], target)) dup = TRUE;
        if (dup) continue;
        wcscpy(targets[chosen_count], target);
        wcscpy(chosen[chosen_count++], seeds[i]);
    }
    /* Earlier versions inserted recent programs as native pins, making the
     * whole Start menu taller. Remove only those recorded by this helper and
     * keep the current list separate for the regular-weight viewer. */
    for (DWORD i = 0; i < managed_count; i++) modify(pins, managed[i], FALSE);
    write_list(L"Pins", managed, 0);
    write_list(L"Items", chosen, chosen_count);
    for (DWORD i = 0; i < chosen_count; i++)
    {
        char utf8[4 * MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, chosen[i], -1, utf8, sizeof(utf8), NULL, NULL);
        printf("%s\n", utf8);
    }
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    XPStartMenuPin *pins;
    HRESULT hr;
    int failures = 0;

    if (argc == 2 && !wcscmp(argv[1], L"panel")) return panel();
    if (argc == 2 && !wcscmp(argv[1], L"webview")) return webview();
    if (argc < 2 || (wcscmp(argv[1], L"list") && wcscmp(argv[1], L"clear") && wcscmp(argv[1], L"pin")
                     && wcscmp(argv[1], L"unpin") && wcscmp(argv[1], L"recent")))
    {
        fprintf(stderr, "usage: startmenu.exe panel | list | clear | pin PATH... | unpin PATH... | recent\n");
        return 2;
    }
    if (FAILED(CoInitialize(NULL))) return 2;
    hr = CoCreateInstance(&CLSID_XPStartMenuPin, NULL, CLSCTX_INPROC_SERVER, &IID_XPStartMenuPin, (void **)&pins);
    if (FAILED(hr))
    {
        fprintf(stderr, "startmenu: XP's Start menu pin list is unavailable (%#lx)\n", (unsigned long)hr);
        return 1;
    }
    if (!wcscmp(argv[1], L"recent")) return recent(pins);
    if (!wcscmp(argv[1], L"list")) hr = each_pin(pins, print_pin);
    else if (!wcscmp(argv[1], L"clear")) hr = each_pin(pins, remove_pin);
    else
    {
        for (int i = 2; i < argc; i++)
        {
            LPITEMIDLIST item = NULL;
            char utf8[4 * MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, utf8, sizeof(utf8), NULL, NULL);
            hr = SHParseDisplayName(argv[i], NULL, &item, 0, NULL);
            /* Modify(NULL, item) appends ITEM at the end of the pinned list;
             * Modify(item, NULL) removes it. */
            if (SUCCEEDED(hr)) hr = argv[1][0] == 'u' ? pins->lpVtbl->Modify(pins, item, NULL)
                                                      : pins->lpVtbl->Modify(pins, NULL, item);
            if (item) CoTaskMemFree(item);
            if (SUCCEEDED(hr)) printf("%s %s\n", argv[1][0] == 'u' ? "unpinned" : "pinned", utf8);
            else
            {
                fprintf(stderr, "startmenu: could not %ls %s (%#lx)\n", argv[1], utf8, (unsigned long)hr);
                failures++;
            }
        }
        hr = S_OK;
    }
    pins->lpVtbl->Release(pins);
    CoUninitialize();
    return FAILED(hr) || failures ? 1 : 0;
}
