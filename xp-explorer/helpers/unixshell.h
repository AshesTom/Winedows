/* Linux shell commands from the Win32 helpers (xptray.exe, xpmix.dll).
 *
 * Wine runs a Unix executable named in CreateProcess natively (as
 * linux-launch.c does), but cannot wait for it nor give it a Win32 pipe as
 * its output: unix_capture lets the shell write a file, then a marker. */
#ifndef UNIXSHELL_H
#define UNIXSHELL_H

#include <windows.h>

/* Starts SCRIPT with /bin/sh, without Wine's debug channels and overrides. */
BOOL unix_spawn(const WCHAR *script);

/* Runs SCRIPT and returns what it printed, standard error too (at most
 * SIZE - 1 bytes into BUFFER, always terminated); FALSE when it could not
 * start or did not finish within ten seconds. */
BOOL unix_capture(const WCHAR *script, char *buffer, DWORD size);

/* SOURCE as one single-quoted shell word in TARGET (CAPACITY characters),
 * cut short rather than overflowing. */
void unix_quote(const WCHAR *source, WCHAR *target, size_t capacity);

#endif
