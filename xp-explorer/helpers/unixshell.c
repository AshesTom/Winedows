/* Linux shell commands from the Win32 helpers; see unixshell.h. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include "unixshell.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

BOOL unix_spawn(const WCHAR *script)
{
    static const WCHAR prefix[] = L"/usr/bin/env -u WINEDEBUG -u WINEDLLOVERRIDES /bin/sh -c \"";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    /* Each character takes two at most once quoted. */
    size_t capacity = wcslen(prefix) + wcslen(script) * 2 + 3;
    WCHAR *line = malloc(capacity * sizeof(WCHAR)), *write;
    BOOL ok;
    if (!line) return FALSE;
    wcscpy(line, prefix);
    write = line + wcslen(line);
    /* Windows' quoting: backslashes are doubled only before a quote or the
     * closing quote, and a quote gets one more. */
    while (*script)
    {
        size_t slashes = 0;
        while (*script == L'\\') { slashes++; script++; }
        if (*script == L'\"')
        {
            for (size_t i = 0; i < slashes * 2 + 1; i++) *write++ = L'\\';
            *write++ = *script++;
        }
        else if (!*script)
        {
            for (size_t i = 0; i < slashes * 2; i++) *write++ = L'\\';
        }
        else
        {
            for (size_t i = 0; i < slashes; i++) *write++ = L'\\';
            *write++ = *script++;
        }
    }
    *write++ = L'\"';
    *write = 0;
    ok = CreateProcessW(L"Z:\\usr\\bin\\env", line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL,
                        &startup, &process);
    free(line);
    if (!ok) return FALSE;
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    return TRUE;
}

BOOL unix_capture(const WCHAR *script, char *buffer, DWORD size)
{
    WCHAR path[MAX_PATH], done_path[MAX_PATH + 8], unix_path[MAX_PATH], *command;
    DWORD done = 0;
    BOOL ok = FALSE;
    *buffer = 0;
    if (!GetTempFileNameW(L"Z:\\tmp", L"xpu", 0, path)) return FALSE;
    swprintf(done_path, ARRAYSIZE(done_path), L"%ls.done", path);
    /* Z:\tmp\... is /tmp/... for the shell. */
    wcscpy(unix_path, path + 2);
    for (WCHAR *p = unix_path; *p; p++) if (*p == L'\\') *p = L'/';
    size_t capacity = wcslen(script) + wcslen(unix_path) * 2 + 48;
    if ((command = malloc(capacity * sizeof(WCHAR))))
    {
        swprintf(command, capacity, L"(%ls) > '%ls' 2>&1; : > '%ls.done'", script, unix_path, unix_path);
        if (unix_spawn(command))
        {
            ULONGLONG started = GetTickCount64();
            while (GetFileAttributesW(done_path) == INVALID_FILE_ATTRIBUTES && GetTickCount64() - started < 10000)
                Sleep(20);
            /* Only a finished script's output: a slower one would be cut short. */
            if (GetFileAttributesW(done_path) != INVALID_FILE_ATTRIBUTES)
            {
                HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                if (file != INVALID_HANDLE_VALUE)
                {
                    ok = ReadFile(file, buffer, size - 1, &done, NULL);
                    CloseHandle(file);
                }
            }
        }
        free(command);
    }
    DeleteFileW(path);
    DeleteFileW(done_path);
    buffer[done] = 0;
    return ok;
}

void unix_quote(const WCHAR *source, WCHAR *target, size_t capacity)
{
    size_t used = 0;
    if (!capacity) return;
    target[used++] = L'\'';
    while (*source && used + 5 < capacity)
    {
        /* ' ends the word, \' is the quote, ' starts it again. */
        if (*source == L'\'')
        {
            target[used++] = L'\'';
            target[used++] = L'\\';
            target[used++] = L'\'';
            target[used++] = L'\'';
            source++;
        }
        else target[used++] = *source++;
    }
    if (used + 2 <= capacity) target[used++] = L'\'';
    target[used] = 0;
}
