/* Write Start menu shortcuts for session/linux_apps.py through IShellLinkW.
 * Reads UTF-8 lines from standard input, one shortcut each, with tab-separated
 * fields: LINK TARGET ARGUMENTS WORKING-DIRECTORY ICON DESCRIPTION (Windows
 * paths, ICON as FILE or FILE,INDEX; empty fields are left unset). Creates the
 * link's folder as needed and prints "ok LINK" or "fail HRESULT LINK" per line.
 * Public Win32 APIs only. */
#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum { LINK, TARGET, ARGUMENTS, DIRECTORY, ICON, DESCRIPTION, FIELDS };

static HRESULT save(WCHAR **field)
{
    WCHAR folder[MAX_PATH], *slash;
    IShellLinkW *link;
    IPersistFile *persist;
    HRESULT hr;
    int error;

    if (wcslen(field[LINK]) >= MAX_PATH) return E_INVALIDARG;
    wcscpy(folder, field[LINK]);
    if (!(slash = wcsrchr(folder, '\\'))) return E_INVALIDARG;
    *slash = 0;
    error = SHCreateDirectoryExW(NULL, folder, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
    if (FAILED(hr)) return hr;
    hr = IShellLinkW_SetPath(link, field[TARGET]);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetArguments(link, field[ARGUMENTS]);
    if (SUCCEEDED(hr) && *field[DIRECTORY]) hr = IShellLinkW_SetWorkingDirectory(link, field[DIRECTORY]);
    if (SUCCEEDED(hr) && *field[ICON])
    {
        /* "FILE,INDEX" names an icon other than the file's first */
        WCHAR *comma = wcsrchr(field[ICON], ','), *end;
        int index = 0;
        if (comma && comma[1] && (index = wcstol(comma + 1, &end, 10), !*end)) *comma = 0;
        else index = 0;
        hr = IShellLinkW_SetIconLocation(link, field[ICON], index);
    }
    if (SUCCEEDED(hr) && *field[DESCRIPTION]) hr = IShellLinkW_SetDescription(link, field[DESCRIPTION]);
    if (SUCCEEDED(hr)) hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Save(persist, field[LINK], TRUE);
        IPersistFile_Release(persist);
    }
    IShellLinkW_Release(link);
    if (SUCCEEDED(hr)) SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, field[LINK], NULL);
    return hr;
}

int wmain(void)
{
    static char line[32768];
    static WCHAR wide[32768];
    int failures = 0;

    if (FAILED(CoInitialize(NULL))) return 2;
    while (fgets(line, sizeof(line), stdin))
    {
        WCHAR *field[FIELDS], *cursor = wide;
        char utf8[4 * MAX_PATH];
        int count;

        line[strcspn(line, "\r\n")] = 0;
        if (!*line) continue;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line, -1, wide, ARRAYSIZE(wide)))
        {
            printf("fail invalid-utf8\n");
            failures++;
            continue;
        }
        for (count = 0; count < FIELDS; count++)
        {
            field[count] = cursor;
            if (!(cursor = wcschr(cursor, '\t'))) break;
            *cursor++ = 0;
        }
        WideCharToMultiByte(CP_UTF8, 0, field[LINK], -1, utf8, sizeof(utf8), NULL, NULL);
        if (count != FIELDS - 1)
        {
            printf("fail fields %s\n", utf8);
            failures++;
            continue;
        }
        HRESULT hr = save(field);
        if (SUCCEEDED(hr)) printf("ok %s\n", utf8);
        else
        {
            printf("fail %#lx %s\n", (unsigned long)hr, utf8);
            failures++;
        }
        fflush(stdout);
    }
    CoUninitialize();
    return failures ? 1 : 0;
}
