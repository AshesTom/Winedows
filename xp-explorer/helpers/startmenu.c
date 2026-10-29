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
 *
 * XP's most-used programs fill the lower block natively, once the prefix
 * has shell32's .lnk\ShellEx handlers (prefix/extras.py).
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

int wmain(int argc, WCHAR **argv)
{
    XPStartMenuPin *pins;
    HRESULT hr;
    int failures = 0;

    if (argc == 2 && !wcscmp(argv[1], L"panel")) return panel();
    if (argc == 2 && !wcscmp(argv[1], L"webview")) return webview();
    if (argc < 2 || (wcscmp(argv[1], L"list") && wcscmp(argv[1], L"clear") && wcscmp(argv[1], L"pin")
                     && wcscmp(argv[1], L"unpin")))
    {
        fprintf(stderr, "usage: startmenu.exe panel | list | clear | pin PATH... | unpin PATH...\n");
        return 2;
    }
    if (FAILED(CoInitialize(NULL))) return 2;
    hr = CoCreateInstance(&CLSID_XPStartMenuPin, NULL, CLSCTX_INPROC_SERVER, &IID_XPStartMenuPin, (void **)&pins);
    if (FAILED(hr))
    {
        fprintf(stderr, "startmenu: XP's Start menu pin list is unavailable (%#lx)\n", (unsigned long)hr);
        return 1;
    }
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
