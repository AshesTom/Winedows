/* Network, power and volume icons for XP's notification area.
 *
 * On XP these came from systray.exe/stobject.dll and netshell.dll; under Wine
 * there are none, so this program reads the Linux state instead:
 *   power    /sys/class/power_supply (battery level, AC, time remaining)
 *   network  /sys/class/net, /proc/net/route and nmcli (SSID, signal, speed)
 *   volume   wpctl (PipeWire) or pactl (PulseAudio)
 * and shows XP's own icons, flyouts and the Network Connections panel, from
 * the user's ISO extracted into C:\xp\tray.
 *
 * The icons are sent to XP's Shell_TrayWnd in XP's own WM_COPYDATA format,
 * since Wine's builtin Shell_NotifyIcon speaks Wine's format, not XP's. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WM_TRAY      (WM_APP + 1)
#define WM_OPEN_CONNECTIONS (WM_APP + 2)
#define WM_OPEN_WIRELESS (WM_APP + 3)
#define ID_POWER     1
#define ID_NETWORK   2
#define ID_VOLUME    3
#define ID_BLUETOOTH 4
#define ID_COUNT     5
#define TIMER_POLL   1
#define TIMER_FIND   2
#define TIMER_APPLY  3
#define TIMER_CONNECTIONS 5
#define TIMER_WIRELESS 6
#define TIMER_WIRELESS_CONNECT 7
#define CMD_MIXER    100
#define CMD_SOUND    101
#define CMD_NETWORK  102
#define CMD_POWER    103
#define CMD_STATUS   105
#define CMD_METER    106
#define CMD_CONNECTIONS 108
#define CMD_NETWORK_SETTINGS 109

#define CONNECTION_LIMIT 64
#define CONN_CONNECT 500
#define CONN_STATUS  501
#define CONN_SETTINGS 502
#define CONN_CLOSE   504
#define WIFI_TASKS 600
#define WIFI_LIST 601
#define WIFI_REFRESH 602
#define WIFI_SETTINGS 603
#define WIFI_CONNECT 604

/* XP's NOTIFYICONDATAW (version 3, 956 bytes) in a 32-bit process, wrapped as
 * its tray expects it in WM_COPYDATA. */
typedef struct
{
    DWORD cbSize;
    DWORD hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    UINT uTimeout;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
} XPNOTIFYICONDATA;

typedef struct
{
    DWORD dwSignature;
    DWORD dwMessage;
    XPNOTIFYICONDATA nid;
} XPTRAYDATA;

typedef struct
{
    BOOL shown;
    HICON icon;
    WCHAR tip[128];
} TRAYICON;

static HWND window, popup;
static HWND connections_window, connections_list, connections_hint, connections_connect;
static HWND wireless_window, wireless_list, wireless_hint, wireless_connect_button;
static HFONT wireless_title_font;
static HFONT wireless_text_font;
static TRAYICON icons[ID_COUNT];
static UINT taskbar_created;
static int tick;

typedef struct
{
    WCHAR name[128];
    WCHAR uuid[40];
    WCHAR type[48];
    WCHAR device[48];
    BOOL active;
} NMCONNECTION;

typedef struct
{
    WCHAR ssid[128];
    WCHAR bssid[32];
    WCHAR security[64];
    int signal;
    BOOL secured;
    BOOL active;
} NMWIRELESS;

static NMCONNECTION connections[CONNECTION_LIMIT];
static unsigned connection_count;
static int nmcli_checked, nmcli_available;
static NMWIRELESS wireless_networks[CONNECTION_LIMIT];
static unsigned wireless_count;
static ULONGLONG wireless_connect_started, wireless_opened;
static WCHAR wireless_connect_ssid[128];

/* Linux state */
static int volume = -1, muted;
static int wanted_volume = -1;
static int battery = -1, charging, on_ac, minutes_left = -1, warned_low;
/* A network interface's state: the tray's (the interface with the default
 * route) and the status sheet's (the one it was opened for). */
typedef struct
{
    WCHAR name[32];
    int up, wireless, limited, signal, speed;
    WCHAR ssid[64];
} NETSTATE;
static NETSTATE net = {.up = -1, .signal = -1}, sheet = {.up = -1, .signal = -1};
/* When each interface came up, for the sheet's Duration. */
static struct { WCHAR name[32]; ULONGLONG since; } net_times[16];
static unsigned long long rx_last, tx_last;

/* XP icons, by resource ID in the files extracted from the user's ISO. */
static struct { const WCHAR *file; int id; HICON icon; } xp_icons[] = {
    { L"stobject.dll", 230, NULL },  /* 0 volume */
    { L"stobject.dll", 231, NULL },  /* 1 volume muted */
    { L"stobject.dll", 200, NULL },  /* 2 plugged in, with a battery */
    { L"powercfg.cpl", 202, NULL },  /* 3 running on battery */
    { L"netshell.dll", 190, NULL },  /* 4 LAN idle */
    { L"netshell.dll", 191, NULL },  /* 5 LAN sending */
    { L"netshell.dll", 192, NULL },  /* 6 LAN receiving */
    { L"netshell.dll", 193, NULL },  /* 7 LAN sending and receiving */
    { L"netshell.dll", 195, NULL },  /* 8 LAN cable unplugged */
    { L"netshell.dll", 196, NULL },  /* 9 LAN limited connectivity */
    { L"xpsp2res.dll", 1003, NULL }, /* 10 wireless connected */
    { L"xpsp2res.dll", 1004, NULL }, /* 11 wireless not connected */
    { L"xpsp2res.dll", 1018, NULL }, /* 12 wireless limited connectivity */
    { L"xpsp2res.dll", 200, NULL },  /* 13 Bluetooth */
};

static HICON xp_icon(int index)
{
    if (!xp_icons[index].icon)
    {
        WCHAR path[MAX_PATH];
        HMODULE module;
        swprintf(path, MAX_PATH, L"C:\\xp\\tray\\%ls", xp_icons[index].file);
        if ((module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE)))
            xp_icons[index].icon = LoadImageW(module, MAKEINTRESOURCEW(xp_icons[index].id), IMAGE_ICON, 16, 16, 0);
        if (!xp_icons[index].icon) xp_icons[index].icon = LoadIconW(NULL, IDI_APPLICATION);
    }
    return xp_icons[index].icon;
}

/* ---- XP's notification area ------------------------------------------- */

static HWND find_tray(void)
{
    HWND tray = NULL;
    /* Wine's desktop keeps a hidden Shell_TrayWnd of its own; XP's is the visible one. */
    while ((tray = FindWindowExW(NULL, tray, L"Shell_TrayWnd", NULL)))
        if (IsWindowVisible(tray)) return tray;
    return NULL;
}

static BOOL tray_send(DWORD message, UINT id, const WCHAR *info_title, const WCHAR *info, DWORD info_flags)
{
    XPTRAYDATA data = {0};
    COPYDATASTRUCT copy;
    HWND tray = find_tray();
    if (!tray) return FALSE;
    data.dwSignature = 0x34753423;
    data.dwMessage = message;
    data.nid.cbSize = sizeof(data.nid);
    data.nid.hWnd = (DWORD)(ULONG_PTR)window;
    data.nid.uID = id;
    if (message != NIM_DELETE)
    {
        data.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.nid.uCallbackMessage = WM_TRAY;
        data.nid.hIcon = (DWORD)(ULONG_PTR)icons[id].icon;
        wcsncpy(data.nid.szTip, icons[id].tip, 127);
    }
    if (info)
    {
        data.nid.uFlags |= NIF_INFO;
        wcsncpy(data.nid.szInfo, info, 255);
        wcsncpy(data.nid.szInfoTitle, info_title, 63);
        data.nid.uTimeout = 10000;
        data.nid.dwInfoFlags = info_flags;
    }
    copy.dwData = 1;
    copy.cbData = sizeof(data);
    copy.lpData = &data;
    return SendMessageTimeoutW(tray, WM_COPYDATA, (WPARAM)window, (LPARAM)&copy,
                               SMTO_ABORTIFHUNG, 3000, NULL) != 0;
}

/* Shows, updates or hides icon ID; a balloon goes with it when INFO is given. */
static void set_icon(UINT id, BOOL show, HICON icon, const WCHAR *tip, const WCHAR *info_title, const WCHAR *info,
                     DWORD info_flags)
{
    TRAYICON *t = &icons[id];
    if (!show)
    {
        if (t->shown) tray_send(NIM_DELETE, id, NULL, NULL, 0);
        t->shown = FALSE;
        return;
    }
    if (t->shown && t->icon == icon && !wcscmp(t->tip, tip) && !info) return;
    t->icon = icon;
    wcsncpy(t->tip, tip, 127);
    t->tip[127] = 0;
    if (!t->shown) t->shown = tray_send(NIM_ADD, id, info_title, info, info_flags);
    else tray_send(NIM_MODIFY, id, info_title, info, info_flags);
}

/* ---- Linux side --------------------------------------------------------- */

/* Reads a small file such as one of sysfs; returns FALSE if it is missing. */
static BOOL read_text(const WCHAR *path, char *buffer, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD done = 0;
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(file, buffer, size - 1, &done, NULL);
    CloseHandle(file);
    buffer[done] = 0;
    buffer[strcspn(buffer, "\r\n")] = 0;
    return TRUE;
}

static long long read_number(const WCHAR *directory, const WCHAR *name, long long fallback)
{
    WCHAR path[MAX_PATH];
    char text[64];
    swprintf(path, MAX_PATH, L"%ls\\%ls", directory, name);
    return read_text(path, text, sizeof(text)) && *text ? atoll(text) : fallback;
}

static BOOL exists(const WCHAR *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void shell_quote_w(const WCHAR *source, WCHAR *target, size_t capacity);

/* Wine runs a Unix executable named in CreateProcess natively (as
 * linux-launch.c does); env drops the Wine debug channels. */
static BOOL spawn(const WCHAR *script, HANDLE unused)
{
    static const WCHAR prefix[] = L"/usr/bin/env -u WINEDEBUG -u WINEDLLOVERRIDES /bin/sh -c \"";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    size_t script_length = wcslen(script), capacity = wcslen(prefix) + script_length * 2 + 3;
    WCHAR *line = malloc(capacity * sizeof(WCHAR));
    WCHAR *write;
    BOOL ok;
    (void)unused;
    if (!line) return FALSE;
    wcscpy(line, prefix);
    write = line + wcslen(line);
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
            break;
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
    return ok;
}

/* Runs SCRIPT and returns what it printed (at most SIZE - 1 bytes). */
static BOOL capture(const WCHAR *script, char *buffer, DWORD size)
{
    WCHAR path[MAX_PATH], done_path[MAX_PATH + 8], unix_path[MAX_PATH], *command;
    HANDLE file;
    DWORD done = 0;
    BOOL ok = FALSE;
    *buffer = 0;
    /* Wine's Unix subprocesses do not inherit a Win32 anonymous pipe as stdout.
     * Let the Unix shell redirect to a temporary file visible through Z: instead. */
    if (!GetTempFileNameW(L"Z:\\tmp", L"xpt", 0, path)) return FALSE;
    swprintf(done_path, ARRAYSIZE(done_path), L"%ls.done", path);
    wcscpy(unix_path, path + 2);
    for (WCHAR *p = unix_path; *p; p++) if (*p == L'\\') *p = L'/';
    size_t capacity = wcslen(script) + wcslen(unix_path) * 2 + 48;
    command = malloc(capacity * sizeof(WCHAR));
    if (command)
    {
        swprintf(command, capacity, L"(%ls) > '%ls' 2>&1; : > '%ls.done'", script, unix_path, unix_path);
        if (spawn(command, NULL))
        {
            ULONGLONG started = GetTickCount64();
            /* Wine cannot wait on the process handle of a native Unix child. */
            while (GetFileAttributesW(done_path) == INVALID_FILE_ATTRIBUTES &&
                   GetTickCount64() - started < 10000) Sleep(20);
            if (GetFileAttributesW(done_path) != INVALID_FILE_ATTRIBUTES)
            {
                file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
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

static void poll_volume(void)
{
    char out[1024], *p;
    /* Their warnings (PipeWire's about RTKit) would come before the volume. */
    if (!capture(L"if command -v wpctl >/dev/null; then wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null; "
                 L"elif command -v pactl >/dev/null; then "
                 L"pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | awk '/Volume/{gsub(\"%\",\"\",$5); "
                 L"printf \"Volume: %.2f\", $5/100; exit}'; "
                 L"pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -q yes && echo ' [MUTED]'; fi",
                 out, sizeof(out)) || !(p = strstr(out, "Volume:")))
    {
        volume = -1;
        return;
    }
    volume = (int)(atof(p + 7) * 100 + 0.5);
    muted = strstr(out, "MUTED") != NULL;
}

static void poll_power(void)
{
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(L"Z:\\sys\\class\\power_supply\\*", &found);
    long long energy = 0, full = 0, power = 0;
    int batteries = 0;
    battery = -1;
    on_ac = 0;
    charging = 0;
    minutes_left = -1;
    if (search == INVALID_HANDLE_VALUE) return;
    do
    {
        WCHAR directory[MAX_PATH], path[MAX_PATH];
        char text[64];
        if (found.cFileName[0] == '.') continue;
        swprintf(directory, MAX_PATH, L"Z:\\sys\\class\\power_supply\\%ls", found.cFileName);
        swprintf(path, MAX_PATH, L"%ls\\type", directory);
        if (!read_text(path, text, sizeof(text))) continue;
        if (!strcmp(text, "Mains"))
        {
            if (read_number(directory, L"online", 0)) on_ac = 1;
        }
        else if (!strcmp(text, "Battery") && read_number(directory, L"present", 1))
        {
            /* Peripherals (mice, headsets) report a scope of Device. */
            swprintf(path, MAX_PATH, L"%ls\\scope", directory);
            if (read_text(path, text, sizeof(text)) && !strcmp(text, "Device")) continue;
            batteries++;
            energy += read_number(directory, L"energy_now", read_number(directory, L"charge_now", 0));
            full += read_number(directory, L"energy_full", read_number(directory, L"charge_full", 0));
            power += llabs(read_number(directory, L"power_now", read_number(directory, L"current_now", 0)));
            swprintf(path, MAX_PATH, L"%ls\\status", directory);
            if (read_text(path, text, sizeof(text)) && !strcmp(text, "Charging")) charging = 1;
            if (battery < 0) battery = (int)read_number(directory, L"capacity", -1);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (!batteries) return;
    if (full > 0) battery = (int)(energy * 100 / full);
    if (battery > 100) battery = 100;
    if (!on_ac && !charging && power > 0) minutes_left = (int)(energy * 60 / power);
}

/* The whole routing table: read_text keeps only a first line. */
static void read_routes(char *route, DWORD size)
{
    HANDLE file = CreateFileW(L"Z:\\proc\\net\\route", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD done = 0;
    route[0] = 0;
    if (file == INVALID_HANDLE_VALUE) return;
    ReadFile(file, route, size - 1, &done, NULL);
    CloseHandle(file);
    route[done] = 0;
}

/* Whether NAME has a route in ROUTE, or with DEFAULT_ONLY the default route. */
static BOOL has_route(const char *route, const WCHAR *name, BOOL default_only)
{
    char narrow[32], needle[48];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), NULL, NULL);
    snprintf(needle, sizeof(needle), default_only ? "\n%s\t00000000\t" : "\n%s\t", narrow);
    return strstr(route, needle) != NULL;
}

/* An interface is up on "up", or on "unknown" with a carrier (WireGuard and
 * other tunnels). */
static BOOL interface_up(const WCHAR *directory)
{
    WCHAR path[MAX_PATH];
    char text[64];
    swprintf(path, MAX_PATH, L"%ls\\operstate", directory);
    if (!read_text(path, text, sizeof(text))) return FALSE;
    if (!strcmp(text, "up")) return TRUE;
    return !strcmp(text, "unknown") && read_number(directory, L"carrier", 0) == 1;
}

/* When interface NAME came up, as first seen by the poll. */
static ULONGLONG *net_time(const WCHAR *name)
{
    int free_slot = -1;
    for (int i = 0; i < (int)ARRAYSIZE(net_times); i++)
    {
        if (!wcscmp(net_times[i].name, name)) return &net_times[i].since;
        if (free_slot < 0 && !net_times[i].name[0]) free_slot = i;
    }
    if (free_slot < 0) return NULL;
    wcsncpy(net_times[free_slot].name, name, 31);
    net_times[free_slot].since = 0;
    return &net_times[free_slot].since;
}

/* STATE for the interface in its name: up, wireless, limited (no address, or
 * no default route when no interface has one), speed, and with DETAIL the
 * SSID and signal of a wireless one. */
static void poll_interface(NETSTATE *state, const char *route, BOOL detail)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    char text[4096], *line;
    swprintf(directory, MAX_PATH, L"Z:\\sys\\class\\net\\%ls", state->name);
    if (!exists(directory))
    {
        state->up = -1;
        return;
    }
    swprintf(path, MAX_PATH, L"%ls\\wireless", directory);
    state->wireless = exists(path);
    state->up = interface_up(directory);
    state->limited = state->up && !has_route(route, state->name, TRUE)
                     && (!strstr(route, "\t00000000\t") || !has_route(route, state->name, FALSE));
    /* Wired links report their speed in sysfs; nmcli gives a wireless one below. */
    if (!state->wireless || !state->up) state->speed = (int)read_number(directory, L"speed", 0);
    if (!state->wireless || !state->up)
    {
        state->ssid[0] = 0;
        state->signal = -1;
        return;
    }
    if (!detail) return;
    /* nmcli: "yes:SSID:SIGNAL:RATE" for the interface's active network; no rescan. */
    {
        WCHAR script[256], name[80];
        shell_quote_w(state->name, name, ARRAYSIZE(name));
        swprintf(script, ARRAYSIZE(script),
                 L"nmcli -t -f ACTIVE,SSID,SIGNAL,RATE dev wifi list --rescan no ifname %ls 2>/dev/null | grep '^yes:'", name);
        if (capture(script, text, sizeof(text)) && !strncmp(text, "yes:", 4))
        {
            char ssid[128], *out = ssid, *p = text + 4;
            while (*p && *p != ':' && out < ssid + sizeof(ssid) - 1)
            {
                if (*p == '\\' && p[1]) p++;
                *out++ = *p++;
            }
            *out = 0;
            MultiByteToWideChar(CP_UTF8, 0, ssid, -1, state->ssid, 64);
            if (*p == ':') state->signal = atoi(++p);
            if ((p = strchr(p, ':'))) state->speed = atoi(p + 1);
            return;
        }
    }
    /* Without NetworkManager: the link quality of /proc/net/wireless (out of 70). */
    {
        HANDLE file = CreateFileW(L"Z:\\proc\\net\\wireless", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD done = 0;
        char name[32];
        state->signal = -1;
        state->ssid[0] = 0;
        if (file == INVALID_HANDLE_VALUE) return;
        ReadFile(file, text, sizeof(text) - 1, &done, NULL);
        CloseHandle(file);
        text[done] = 0;
        WideCharToMultiByte(CP_UTF8, 0, state->name, -1, name, sizeof(name), NULL, NULL);
        for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
        {
            while (*line == ' ') line++;
            if (!strncmp(line, name, strlen(name)) && line[strlen(name)] == ':')
            {
                float quality;
                if (sscanf(line + strlen(name) + 1, "%*s %f", &quality) == 1) state->signal = (int)(quality * 100 / 70);
            }
        }
    }
}

/* The tray's interface: the one that carries the default route, else the
 * first wireless or wired adapter; virtual interfaces (no device link) are
 * left out. The poll also notes when each interface came up. */
static void poll_network(BOOL detail)
{
    WIN32_FIND_DATAW found;
    HANDLE search;
    char route[8192];
    WCHAR best[32] = L"", path[MAX_PATH], directory[MAX_PATH];
    int best_rank = 0;

    read_routes(route, sizeof(route));
    search = FindFirstFileW(L"Z:\\sys\\class\\net\\*", &found);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            ULONGLONG *since;
            int rank;
            if (found.cFileName[0] == '.') continue;
            swprintf(directory, MAX_PATH, L"Z:\\sys\\class\\net\\%ls", found.cFileName);
            if ((since = net_time(found.cFileName)))
            {
                if (!interface_up(directory)) *since = 0;
                else if (!*since) *since = GetTickCount64();
            }
            swprintf(path, MAX_PATH, L"%ls\\device", directory);
            if (!exists(path)) continue;
            swprintf(path, MAX_PATH, L"%ls\\wireless", directory);
            rank = has_route(route, found.cFileName, TRUE) ? 3 : exists(path) ? 2 : 1;
            if (rank > best_rank)
            {
                best_rank = rank;
                wcsncpy(best, found.cFileName, 31);
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    if (!best_rank)
    {
        net.up = -1;
        return;
    }
    if (wcscmp(best, net.name))
    {
        wcscpy(net.name, best);
        rx_last = tx_last = 0;
    }
    poll_interface(&net, route, detail);
    if (sheet.name[0])
    {
        if (!wcscmp(sheet.name, net.name) && detail) sheet = net;
        else poll_interface(&sheet, route, detail);
    }
}

/* ---- Icons -------------------------------------------------------------- */

static HMODULE xp_module(const WCHAR *file);

/* A string of XP's FILE in the user's language, formatted as XP formats it
 * (FormatMessage inserts: %1!s!, %1!u!), or FALLBACK in the same form. */
static void xp_text(const WCHAR *file, UINT id, const WCHAR *fallback, WCHAR *out, int size, ...)
{
    WCHAR format[256];
    HMODULE module = xp_module(file);
    va_list args;
    if (!module || !LoadStringW(module, id, format, ARRAYSIZE(format))) lstrcpynW(format, fallback, ARRAYSIZE(format));
    va_start(args, size);
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_STRING, format, 0, 0, out, size, &args)) lstrcpynW(out, format, size);
    va_end(args);
}

/* The text of item COMMAND in menu MENU of XP's FILE, or FALLBACK. */
static void xp_menu_text(const WCHAR *file, UINT menu_id, UINT command, const WCHAR *fallback, WCHAR *out, int size)
{
    HMODULE module = xp_module(file);
    HMENU menu = module ? LoadMenuW(module, MAKEINTRESOURCEW(menu_id)) : NULL;
    if (!menu || !GetMenuStringW(menu, command, out, size, MF_BYCOMMAND)) lstrcpynW(out, fallback, size);
    if (menu) DestroyMenu(menu);
}

/* The text of control CONTROL (or with -1 the title) in dialog DIALOG of
 * XP's FILE, read from its template, or FALLBACK. */
static const WORD *template_string(const WORD *p, const WCHAR **text)
{
    if (*p == 0xffff)
    {
        *text = NULL;
        return p + 2;
    }
    *text = (const WCHAR *)p;
    return p + wcslen((const WCHAR *)p) + 1;
}

static void xp_dialog_text(const WCHAR *file, UINT dialog, int control, const WCHAR *fallback, WCHAR *out, int size)
{
    HMODULE module = xp_module(file);
    HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(dialog), (LPCWSTR)RT_DIALOG) : NULL;
    const WORD *p = resource ? LockResource(LoadResource(module, resource)) : NULL;
    const WCHAR *text = NULL;
    lstrcpynW(out, fallback, size);
    if (!p) return;
    BOOL extended = p[0] == 1 && p[1] == 0xffff;
    DWORD style = extended ? *(const DWORD *)(p + 6) : *(const DWORD *)p;
    WORD count = extended ? p[8] : p[4];
    p += extended ? 13 : 9;
    p = template_string(p, &text);          /* menu */
    p = template_string(p, &text);          /* class */
    p = template_string(p, &text);          /* title */
    if (control == -1)
    {
        if (text) lstrcpynW(out, text, size);
        return;
    }
    if (style & DS_SETFONT)
    {
        p += extended ? 3 : 1;
        p = template_string(p, &text);
    }
    for (WORD i = 0; i < count; i++)
    {
        DWORD id;
        p = (const WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
        if (extended)
        {
            id = *(const DWORD *)(p + 10);
            p += 12;
        }
        else
        {
            id = p[8];
            p += 9;
        }
        p = template_string(p, &text);      /* class */
        p = template_string(p, &text);
        if ((int)id == control && text)
        {
            lstrcpynW(out, text, size);
            return;
        }
        p = (const WORD *)((const BYTE *)p + 2 + *p);
    }
}

/* xptray's own words, for what XP's files have no text: Japanese in a
 * Japanese session, otherwise English. */
static const WCHAR *own_text(const WCHAR *english, const WCHAR *japanese)
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? japanese : english;
}

/* FORMAT with its printf-style %s (XP's wireless strings) replaced by TEXT;
 * FORMAT is the string as it is, not through FormatMessage. */
static void insert_text(const WCHAR *format, const WCHAR *text, WCHAR *out, size_t size)
{
    const WCHAR *at = wcsstr(format, L"%s");
    if (!at)
    {
        lstrcpynW(out, format, (int)size);
        return;
    }
    swprintf(out, size, L"%.*ls%ls%ls", (int)(at - format), format, text, at + 2);
}

static void signal_words(int signal, WCHAR *out, int size)
{
    /* netshell's Very Low (2601) to Excellent (2605). */
    static const WCHAR *words[] = { L"Very Low", L"Low", L"Good", L"Very Good", L"Excellent" };
    int level = signal >= 80 ? 4 : signal >= 60 ? 3 : signal >= 40 ? 2 : signal >= 20 ? 1 : 0;
    xp_text(L"netshell.dll", 2601 + level, words[level], out, size);
}

static void speed_text(int speed, WCHAR *out, size_t size)
{
    if (speed >= 1000) swprintf(out, size, L"%.1f Gbps", speed / 1000.0);
    else if (speed > 0) swprintf(out, size, L"%d.0 Mbps", speed);
    else xp_text(L"netshell.dll", 1026, L"Unknown", out, size);
}

static void show_volume(void)
{
    WCHAR tip[64];
    if (volume < 0)
    {
        set_icon(ID_VOLUME, FALSE, NULL, L"", NULL, NULL, 0);
        return;
    }
    /* XP's tip names the control only (stobject's "Volume", "Volume (muted)"). */
    if (muted) xp_text(L"stobject.dll", 258, L"Volume (muted)", tip, ARRAYSIZE(tip));
    else xp_text(L"stobject.dll", 252, L"Volume", tip, ARRAYSIZE(tip));
    set_icon(ID_VOLUME, TRUE, xp_icon(muted || !volume ? 1 : 0), tip, NULL, NULL, 0);
    if (popup && IsWindowVisible(popup) && wanted_volume < 0)
    {
        SendDlgItemMessageW(popup, 1001, TBM_SETPOS, TRUE, 100 - min(volume, 100));
        CheckDlgButton(popup, 1000, muted ? BST_CHECKED : BST_UNCHECKED);
    }
}

static void show_power(void)
{
    WCHAR tip[128];
    if (battery < 0)
    {
        set_icon(ID_POWER, FALSE, NULL, L"", NULL, NULL, 0);
        return;
    }
    /* stobject's tips, as XP's power meter shows them. */
    if (!on_ac && minutes_left > 0 && minutes_left < 60 * 24)
    {
        if (minutes_left >= 60)
            xp_text(L"stobject.dll", 162, L"%1!u!:%2!02u! hours (%3!u!%%) remaining", tip, ARRAYSIZE(tip),
                    minutes_left / 60, minutes_left % 60, battery);
        else xp_text(L"stobject.dll", 163, L"%1!u! min (%2!u!%%) remaining", tip, ARRAYSIZE(tip),
                     minutes_left, battery);
    }
    else xp_text(L"stobject.dll", 158, L"%1!u!%% remaining", tip, ARRAYSIZE(tip), battery);
    if (charging)
    {
        WCHAR suffix[32];
        xp_text(L"stobject.dll", 159, L" (charging)", suffix, ARRAYSIZE(suffix));
        wcsncat(tip, suffix, ARRAYSIZE(tip) - wcslen(tip) - 1);
    }
    if (!on_ac && !charging && battery <= 10 && !warned_low)
    {
        WCHAR title[64];
        warned_low = 1;
        xp_text(L"powercfg.cpl", 94, L"Low Battery Alarm", title, ARRAYSIZE(title));
        set_icon(ID_POWER, TRUE, xp_icon(3), tip, title, tip, NIIF_WARNING);
        return;
    }
    if (on_ac || battery > 15) warned_low = 0;
    set_icon(ID_POWER, TRUE, xp_icon(on_ac || charging ? 2 : 3), tip, NULL, NULL, 0);
}

static void show_network(void)
{
    static int last_up = -2;
    WCHAR tip[128], speed[32], speed_line[64], signal[32], signal_line[64], info[256], title[64], name[64];
    WCHAR line[128];
    WCHAR directory[MAX_PATH];
    unsigned long long rx, tx;
    int index;
    BOOL balloon = FALSE;

    if (net.up < 0)
    {
        set_icon(ID_NETWORK, FALSE, NULL, L"", NULL, NULL, 0);
        last_up = -2;
        return;
    }
    /* The texts are XP's own (netshell.dll, and xpsp2res.dll for wireless). */
    if (net.wireless) xp_text(L"xpsp2res.dll", 16022, L"Wireless Network Connection", name, ARRAYSIZE(name));
    else xp_text(L"netshell.dll", 1811, L"Local Area Connection", name, ARRAYSIZE(name));
    speed_text(net.speed, speed, 32);
    xp_text(L"netshell.dll", 2202, L"Speed: %1!s!", speed_line, ARRAYSIZE(speed_line), speed);
    if (net.signal >= 0) signal_words(net.signal, signal, ARRAYSIZE(signal));
    else xp_text(L"netshell.dll", 1026, L"Unknown", signal, ARRAYSIZE(signal));
    xp_text(L"xpsp2res.dll", 16008, L"Signal Strength:", line, ARRAYSIZE(line));
    swprintf(signal_line, ARRAYSIZE(signal_line), L"%ls %ls", line, signal);
    if (!net.up)
    {
        index = net.wireless ? 11 : 8;
        if (net.wireless) xp_text(L"xpsp2res.dll", 144, L"Not connected", line, ARRAYSIZE(line));
        else xp_text(L"netshell.dll", 2201, L"A network cable is unplugged.", line, ARRAYSIZE(line));
        swprintf(tip, 128, L"%ls\n%ls", name, line);
        if (last_up == 1)
        {
            balloon = TRUE;
            wcscpy(title, name);
            if (net.wireless) xp_text(L"netshell.dll", 2208, L"Wireless connection unavailable", info, ARRAYSIZE(info));
            else wcscpy(info, line);
        }
    }
    else
    {
        if (net.wireless)
        {
            index = net.limited ? 12 : 10;
            if (net.ssid[0]) swprintf(tip, 128, L"%ls (%ls)\n%ls\n%ls", name, net.ssid, speed_line, signal_line);
            else swprintf(tip, 128, L"%ls\n%ls", name, signal_line);
        }
        else
        {
            /* XP lights the two screens for sending and receiving. */
            swprintf(directory, MAX_PATH, L"Z:\\sys\\class\\net\\%ls\\statistics", net.name);
            rx = read_number(directory, L"rx_bytes", 0);
            tx = read_number(directory, L"tx_bytes", 0);
            index = net.limited ? 9 : 4 + (rx_last && tx > tx_last ? 1 : 0) + (rx_last && rx > rx_last ? 2 : 0);
            rx_last = rx;
            tx_last = tx;
            swprintf(tip, 128, L"%ls\n%ls", name, speed_line);
        }
        if (net.limited)
        {
            xp_text(L"xpsp2res.dll", 142, L"Limited or no connectivity", line, ARRAYSIZE(line));
            wcsncat(tip, L"\n", 127 - wcslen(tip));
            wcsncat(tip, line, 127 - wcslen(tip));
        }
        if (last_up == 0)
        {
            balloon = TRUE;
            xp_text(L"netshell.dll", 2223, L"%1 is now connected", title, ARRAYSIZE(title), name);
            if (net.wireless && net.ssid[0])
            {
                xp_text(L"netshell.dll", 2234, L"Connected to: %1!s!\r\nSignal Strength: %2!s!", line,
                        ARRAYSIZE(line), net.ssid, signal);
                swprintf(info, 256, L"%ls\n%ls", line, speed_line);
            }
            else wcscpy(info, speed_line);
        }
    }
    last_up = net.up;
    set_icon(ID_NETWORK, TRUE, xp_icon(index), tip, balloon ? title : NULL, balloon ? info : NULL, NIIF_INFO);
}

/* ---- Bluetooth ---------------------------------------------------------
 * XP SP2's Bluetooth icon (xpsp2res.dll) while the system has an adapter,
 * with XP's own menu of it (xpsp2res.dll's menu 2140): the radio through
 * bluetoothctl, the devices and the wizard with the desktop's Bluetooth
 * tools (KDE's bluedevil, else blueman or GNOME's); above XP's items, the
 * paired devices, checked when connected, which a click connects or
 * disconnects. */
#define BT_ADD      2141
#define BT_SHOW     2142
#define BT_SEND     2143
#define BT_RECEIVE  2144
#define BT_PAN      2146
#define BT_SETTINGS 2147
#define BT_REMOVE   2158
#define BT_ON       2159
#define BT_OFF      2160
#define BT_DEVICE   3000
#define BT_DEVICES  24

static int bluetooth = -1;   /* -1 no adapter, 0 radio off, 1 on */
static BOOL bluetooth_removed;
static struct { char address[18]; WCHAR name[64]; BOOL connected; } bt_devices[BT_DEVICES];
static unsigned bt_count;

/* The adapter (sysfs) and its radio (rfkill's soft block). */
static void poll_bluetooth(void)
{
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(L"Z:\\sys\\class\\bluetooth\\hci*", &found);
    int state = -1;
    if (find == INVALID_HANDLE_VALUE)
    {
        bluetooth = -1;
        return;
    }
    FindClose(find);
    state = 1;
    find = FindFirstFileW(L"Z:\\sys\\class\\rfkill\\rfkill*", &found);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR path[MAX_PATH];
            char text[32];
            swprintf(path, MAX_PATH, L"Z:\\sys\\class\\rfkill\\%ls\\type", found.cFileName);
            if (!read_text(path, text, sizeof(text)) || strncmp(text, "bluetooth", 9)) continue;
            swprintf(path, MAX_PATH, L"Z:\\sys\\class\\rfkill\\%ls\\soft", found.cFileName);
            if (read_text(path, text, sizeof(text)) && text[0] == '1') state = 0;
            swprintf(path, MAX_PATH, L"Z:\\sys\\class\\rfkill\\%ls\\hard", found.cFileName);
            if (read_text(path, text, sizeof(text)) && text[0] == '1') state = 0;
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    bluetooth = state;
}

static void show_bluetooth(void)
{
    WCHAR tip[128];
    if (bluetooth < 0 || bluetooth_removed)
    {
        set_icon(ID_BLUETOOTH, FALSE, NULL, L"", NULL, NULL, 0);
        return;
    }
    xp_text(L"xpsp2res.dll", 1015, L"Bluetooth Devices", tip, ARRAYSIZE(tip));
    set_icon(ID_BLUETOOTH, TRUE, xp_icon(13), tip, NULL, NULL, 0);
}

/* The paired devices and whether each is connected (bluetoothctl). */
static void read_bluetooth_devices(void)
{
    static char text[16384];
    char *line, *next;
    bt_count = 0;
    if (bluetooth <= 0 ||
        !capture(L"for d in $(bluetoothctl devices Paired 2>/dev/null | awk '{print $2}'); do "
                 L"info=$(bluetoothctl info \"$d\" 2>/dev/null); "
                 L"c=$(printf '%s' \"$info\" | awk '/Connected:/{print $2; exit}'); "
                 L"n=$(printf '%s' \"$info\" | sed -n 's/^[[:space:]]*Alias: //p' | head -1); "
                 L"printf '%s %s %s\\n' \"$d\" \"${c:-no}\" \"$n\"; done", text, sizeof(text)))
        return;
    for (line = text; line && *line && bt_count < BT_DEVICES; line = next)
    {
        char address[18], connected[8];
        int offset = 0;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (sscanf(line, "%17s %7s %n", address, connected, &offset) < 2) continue;
        lstrcpynA(bt_devices[bt_count].address, address, sizeof(bt_devices[bt_count].address));
        bt_devices[bt_count].connected = !strcmp(connected, "yes");
        if (!MultiByteToWideChar(CP_UTF8, 0, offset ? line + offset : address, -1, bt_devices[bt_count].name,
                                 ARRAYSIZE(bt_devices[bt_count].name)) || !bt_devices[bt_count].name[0])
            MultiByteToWideChar(CP_UTF8, 0, address, -1, bt_devices[bt_count].name, ARRAYSIZE(bt_devices[bt_count].name));
        bt_count++;
    }
}

static void bluetooth_command(UINT command)
{
    static const WCHAR devices[] = L"kcmshell6 kcm_bluetooth 2>/dev/null || systemsettings kcm_bluetooth 2>/dev/null "
                                   L"|| blueman-manager 2>/dev/null || gnome-control-center bluetooth";
    WCHAR script[256];
    switch (command)
    {
    case BT_ADD:
        spawn(L"bluedevil-wizard 2>/dev/null || blueman-assistant 2>/dev/null || blueman-manager 2>/dev/null "
              L"|| gnome-control-center bluetooth", NULL);
        break;
    case BT_SHOW:
    case BT_SETTINGS:
        spawn(devices, NULL);
        break;
    case BT_SEND:
        spawn(L"bluedevil-sendfile 2>/dev/null || blueman-sendto", NULL);
        break;
    /* XP's radio switch is rfkill's block, which the icon reads (as bluedevil's). */
    case BT_ON:
        spawn(L"rfkill unblock bluetooth; sleep 1; bluetoothctl power on", NULL);
        break;
    case BT_OFF:
        spawn(L"bluetoothctl power off; rfkill block bluetooth", NULL);
        break;
    case BT_REMOVE:
        /* Until the next session, as XP's own is gone until Bluetooth Devices brings it back. */
        bluetooth_removed = TRUE;
        show_bluetooth();
        break;
    default:
        if (command >= BT_DEVICE && command < BT_DEVICE + bt_count)
        {
            unsigned i = command - BT_DEVICE;
            WCHAR address[18];
            MultiByteToWideChar(CP_ACP, 0, bt_devices[i].address, -1, address, ARRAYSIZE(address));
            swprintf(script, ARRAYSIZE(script), L"bluetoothctl %ls %ls",
                     bt_devices[i].connected ? L"disconnect" : L"connect", address);
            spawn(script, NULL);
        }
    }
}

/* XP's menu of its Bluetooth icon, with the paired devices above it. */
static void bluetooth_menu(void)
{
    HMODULE module = xp_module(L"xpsp2res.dll");
    HMENU loaded = NULL, menu;
    POINT cursor;
    UINT command;
    if (module) loaded = LoadMenuW(module, MAKEINTRESOURCEW(2140));
    menu = loaded ? GetSubMenu(loaded, 0) : CreatePopupMenu();
    if (!loaded)
    {
        AppendMenuW(menu, MF_STRING, BT_ADD, L"&Add a Bluetooth Device");
        AppendMenuW(menu, MF_STRING, BT_SHOW, L"Show Bluetooth &Devices");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, BT_SEND, L"&Send a File");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, BT_SETTINGS, L"&Open Bluetooth Settings");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, BT_ON, L"&Turn Bluetooth Radio On");
        AppendMenuW(menu, MF_STRING, BT_OFF, L"Turn &Bluetooth Radio Off");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, BT_REMOVE, L"Remove Bluetooth &Icon");
    }
    poll_bluetooth();
    /* The radio's state decides which of its two items applies. */
    EnableMenuItem(menu, BT_ON, MF_BYCOMMAND | (bluetooth == 0 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, BT_OFF, MF_BYCOMMAND | (bluetooth == 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, BT_ADD, MF_BYCOMMAND | (bluetooth == 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, BT_SEND, MF_BYCOMMAND | (bluetooth == 1 ? MF_ENABLED : MF_GRAYED));
    /* No Linux counterpart here: receiving is the desktop's service, PAN NetworkManager's. */
    EnableMenuItem(menu, BT_RECEIVE, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, BT_PAN, MF_BYCOMMAND | MF_GRAYED);
    read_bluetooth_devices();
    if (bt_count)
    {
        InsertMenuW(menu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        for (unsigned i = bt_count; i-- > 0;)
            InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING | (bt_devices[i].connected ? MF_CHECKED : 0),
                        BT_DEVICE + i, bt_devices[i].name);
    }
    SetMenuDefaultItem(menu, BT_SHOW, FALSE);
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, NULL);
    PostMessageW(window, WM_NULL, 0, 0);
    if (loaded) DestroyMenu(loaded);
    else DestroyMenu(menu);
    if (command) bluetooth_command(command);
}

static void poll(void)
{
    static int last_up = -2;
    poll_power();
    show_power();
    /* nmcli for the SSID only every 10 seconds, or when the link changes. */
    poll_network(FALSE);
    if (net.up != last_up || tick % 10 == 0) poll_network(TRUE);
    last_up = net.up;
    show_network();
    if (tick % 2 == 0 || volume < 0)
    {
        poll_volume();
        show_volume();
    }
    if (tick % 3 == 0)
    {
        poll_bluetooth();
        show_bluetooth();
    }
    tick++;
}

static void add_all(void)
{
    for (int i = 0; i < ID_COUNT; i++) icons[i].shown = FALSE;
    tick = 0;
    poll();
}

static void run_settings(UINT command);

/* ---- XP's own flyouts --------------------------------------------------
 * The windows XP opened from these icons, built from the dialog templates in
 * the user's own XP files (C:\xp\tray, extracted from their ISO) and filled
 * with the Linux state:
 *   volume   sndvol32.exe dialog 106, the single-click Volume window
 *   power    stobject.dll dialog 100 "Power Meter", around batmeter.dll 100
 *   network  netshell.dll pages 1801 (General) and 1806 (Support) of the
 *            "<connection> Status" property sheet */

static HMODULE xp_module(const WCHAR *file)
{
    /* Loaded once each: the tips read their strings every second. */
    static struct { const WCHAR *file; HMODULE module; } loaded[8];
    static unsigned count;
    WCHAR path[MAX_PATH];
    HMODULE module;
    for (unsigned i = 0; i < count; i++)
        if (!wcscmp(loaded[i].file, file)) return loaded[i].module;
    swprintf(path, MAX_PATH, L"C:\\xp\\tray\\%ls", file);
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (module && count < ARRAYSIZE(loaded))
    {
        loaded[count].file = file;
        loaded[count++].module = module;
    }
    return module;
}

static void set_volume(int value)
{
    WCHAR script[256];
    swprintf(script, 256, L"if command -v wpctl >/dev/null; then wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%; "
             L"else pactl set-sink-volume @DEFAULT_SINK@ %d%%; fi", value, value);
    spawn(script, NULL);
    volume = value;
}

static void set_mute(int value)
{
    WCHAR script[256];
    swprintf(script, 256, L"if command -v wpctl >/dev/null; then wpctl set-mute @DEFAULT_AUDIO_SINK@ %d; "
             L"else pactl set-sink-mute @DEFAULT_SINK@ %d; fi", value, value);
    spawn(script, NULL);
    muted = value;
    show_volume();
}

/* Places a flyout above the notification area, by the cursor, as XP did. */
static void place_flyout(HWND dialog)
{
    POINT cursor;
    RECT work, rect;
    int width, height;
    GetWindowRect(dialog, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    GetCursorPos(&cursor);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    cursor.x = max(work.left, min(cursor.x - width / 2, work.right - width));
    cursor.y = max(work.top, work.bottom - height);
    SetWindowPos(dialog, HWND_TOPMOST, cursor.x, cursor.y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

#define IDC_VOLUME_SLIDER 1001
#define IDC_VOLUME_MUTE   1000

static INT_PTR CALLBACK volume_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        HWND slider_control = GetDlgItem(dialog, IDC_VOLUME_SLIDER);
        SendMessageW(slider_control, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(slider_control, TBM_SETTICFREQ, 10, 0);
        SendMessageW(slider_control, TBM_SETPAGESIZE, 0, 10);
        SendMessageW(slider_control, TBM_SETPOS, TRUE, 100 - max(0, min(volume, 100)));
        CheckDlgButton(dialog, IDC_VOLUME_MUTE, muted ? BST_CHECKED : BST_UNCHECKED);
        place_flyout(dialog);
        SetForegroundWindow(dialog);
        SetFocus(slider_control);
        return FALSE;
    }
    case WM_VSCROLL:
        wanted_volume = 100 - (int)SendDlgItemMessageW(dialog, IDC_VOLUME_SLIDER, TBM_GETPOS, 0, 0);
        SetTimer(window, TIMER_APPLY, 80, NULL);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_VOLUME_MUTE)
            set_mute(IsDlgButtonChecked(dialog, IDC_VOLUME_MUTE) == BST_CHECKED);
        else if (LOWORD(wparam) == IDCANCEL) DestroyWindow(dialog);
        return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) DestroyWindow(dialog);
        return TRUE;
    case WM_DESTROY:
        popup = NULL;
        return TRUE;
    }
    return FALSE;
}

static void show_popup(void)
{
    static HMODULE sndvol;
    if (popup) return;
    if (!sndvol)
    {
        /* The dialog names sndvol32's own window class, which sndvol32 registers
         * when it runs: a dialog class here, global so the template finds it. */
        WNDCLASSW tray_volume = {.style = CS_GLOBALCLASS, .lpfnWndProc = DefDlgProcW,
                                 .cbWndExtra = DLGWINDOWEXTRA, .hInstance = GetModuleHandleW(NULL),
                                 .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW), .lpszClassName = L"Tray Volume"};
        RegisterClassW(&tray_volume);
        sndvol = xp_module(L"sndvol32.exe");
    }
    if (sndvol) popup = CreateDialogParamW(sndvol, MAKEINTRESOURCEW(106), NULL, volume_proc, 0);
}

/* XP's Volume Control, the mixer window "Open Volume Control" and a double
 * click opened: sndvol32.exe itself, from a copy in C:\xp\bin whose import
 * of WINMM.dll names xpmix.dll (helpers/xpmix.c), which sets the volume of
 * the Linux sound server where Wine's winmm sets the process's own. */
static BOOL open_volume_control(void)
{
    static const WCHAR source[] = L"C:\\xp\\tray\\sndvol32.exe", target[] = L"C:\\xp\\bin\\sndvol32.exe";
    static const char name[] = "WINMM.dll";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    HANDLE file;
    DWORD size, done = 0, current_size = 0;
    char *image, *current;
    BOOL same, ok;
    if (!exists(L"C:\\xp\\bin\\xpmix.dll")) return FALSE;
    file = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    size = GetFileSize(file, NULL);
    image = size != INVALID_FILE_SIZE ? malloc(size) : NULL;
    ok = image && ReadFile(file, image, size, &done, NULL) && done == size;
    CloseHandle(file);
    if (!ok)
    {
        free(image);
        return FALSE;
    }
    /* Its import table and its bound imports both name the DLL. */
    for (DWORD i = 0; i + sizeof(name) <= size; i++)
        if (!_strnicmp(image + i, name, sizeof(name))) memcpy(image + i, "xpmix.dll", sizeof(name));
    /* Written again only when the user's sndvol32 changed. */
    file = CreateFileW(target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    current = malloc(size);
    same = FALSE;
    if (file != INVALID_HANDLE_VALUE)
    {
        same = current && GetFileSize(file, NULL) == size && ReadFile(file, current, size, &current_size, NULL) &&
               current_size == size && !memcmp(current, image, size);
        CloseHandle(file);
    }
    free(current);
    ok = same;
    if (!same && (file = CreateFileW(target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)) != INVALID_HANDLE_VALUE)
    {
        ok = WriteFile(file, image, size, &done, NULL) && done == size;
        CloseHandle(file);
    }
    free(image);
    if (!ok) return FALSE;
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, 0, NULL, L"C:\\xp\\bin", &startup, &process)) return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

/* Power Meter: batmeter's page inside stobject's frame. */
static HWND power_meter, power_page;

static void power_meter_update(void)
{
    WCHAR text[64];
    if (!power_page) return;
    /* batmeter's own words for its page */
    if (on_ac) xp_text(L"batmeter.dll", 100, L"AC power", text, 64);
    else xp_text(L"batmeter.dll", 109, L"Batteries", text, 64);
    SetDlgItemTextW(power_page, 1001, text);
    ShowWindow(GetDlgItem(power_page, 1001), SW_SHOW);
    if (battery >= 0) xp_text(L"batmeter.dll", 104, L"%1!u!%%", text, 64, battery);
    else xp_text(L"batmeter.dll", 102, L"Unknown", text, 64);
    SetDlgItemTextW(power_page, 1002, text);
    ShowWindow(GetDlgItem(power_page, 1002), SW_SHOW);
    ShowWindow(GetDlgItem(power_page, 1006), charging ? SW_SHOW : SW_HIDE);
    SendDlgItemMessageW(power_page, 1004, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendDlgItemMessageW(power_page, 1004, PBM_SETPOS, max(battery, 0), 0);
    if (minutes_left >= 60 && minutes_left < 60 * 24)
        xp_text(L"batmeter.dll", 105, L"%1!u!:%2!02u! hours", text, 64, minutes_left / 60, minutes_left % 60);
    else if (minutes_left > 0) xp_text(L"batmeter.dll", 106, L"%1!u! min", text, 64, minutes_left);
    else xp_text(L"batmeter.dll", 102, L"Unknown", text, 64);
    SetDlgItemTextW(power_page, 1068, text);
    ShowWindow(GetDlgItem(power_page, 1067), on_ac ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(power_page, 1068), on_ac ? SW_HIDE : SW_SHOW);
    SendDlgItemMessageW(power_page, 1003, STM_SETICON,
                        (WPARAM)LoadImageW(xp_module(xp_icons[on_ac || charging ? 2 : 3].file),
                                           MAKEINTRESOURCEW(xp_icons[on_ac || charging ? 2 : 3].id),
                                           IMAGE_ICON, 32, 32, 0), 0);
}

static INT_PTR CALLBACK power_page_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)dialog; (void)message; (void)wparam; (void)lparam;
    return message == WM_INITDIALOG;
}

static INT_PTR CALLBACK power_meter_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        static HMODULE batmeter;
        RECT area;
        if (!batmeter) batmeter = xp_module(L"batmeter.dll");
        CheckDlgButton(dialog, 1002, BST_CHECKED);
        EnableWindow(GetDlgItem(dialog, 1002), FALSE);
        EnableWindow(GetDlgItem(dialog, 1003), FALSE);
        GetWindowRect(GetDlgItem(dialog, 1000), &area);
        MapWindowPoints(NULL, dialog, (POINT *)&area, 2);
        if (batmeter && (power_page = CreateDialogParamW(batmeter, MAKEINTRESOURCEW(100), dialog, power_page_proc, 0)))
            SetWindowPos(power_page, HWND_TOP, area.left, area.top, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        power_meter_update();
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) DestroyWindow(dialog);
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(dialog);
        return TRUE;
    case WM_DESTROY:
        power_meter = power_page = NULL;
        return TRUE;
    }
    return FALSE;
}

static void show_power_meter(void)
{
    static HMODULE stobject;
    if (power_meter)
    {
        SetForegroundWindow(power_meter);
        return;
    }
    if (!stobject) stobject = xp_module(L"stobject.dll");
    if (stobject && (power_meter = CreateDialogParamW(stobject, MAKEINTRESOURCEW(100), NULL, power_meter_proc, 0)))
        ShowWindow(power_meter, SW_SHOW);
}

/* "<connection> Status": netshell's General and Support pages. */
static HWND net_sheet, net_general, net_support;

static void thousands(unsigned long long value, WCHAR *out, size_t size)
{
    WCHAR digits[32], *p = out;
    int length = swprintf(digits, 32, L"%llu", value);
    for (int i = 0; i < length && (size_t)(p - out) < size - 2; i++)
    {
        if (i && (length - i) % 3 == 0) *p++ = ',';
        *p++ = digits[i];
    }
    *p = 0;
}

/* XP's signal strength bars, drawn for the General page's picture. */
static HBITMAP signal_bitmap(int signal)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 26, 13), old = SelectObject(dc, bitmap);
    RECT all = {0, 0, 26, 13};
    int bars = signal >= 80 ? 5 : signal >= 60 ? 4 : signal >= 40 ? 3 : signal >= 20 ? 2 : signal > 0 ? 1 : 0;
    FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
    for (int i = 0; i < 5; i++)
    {
        RECT bar = {i * 5, 10 - (i + 1) * 2, i * 5 + 4, 13};
        HBRUSH brush = CreateSolidBrush(i < bars ? RGB(0, 170, 0) : RGB(200, 200, 200));
        FillRect(dc, &bar, brush);
        FrameRect(dc, &bar, GetSysColorBrush(COLOR_3DSHADOW));
        DeleteObject(brush);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return bitmap;
}

static void net_status_update(void)
{
    WCHAR text[128], directory[MAX_PATH];
    ULONGLONG seconds, *since = net_time(sheet.name);
    HBITMAP old;
    if (!net_general) return;
    if (sheet.up <= 0) xp_text(L"netshell.dll", 23010, L"Disconnected", text, 128);
    else if (sheet.limited) xp_text(L"xpsp2res.dll", 142, L"Limited or no connectivity", text, 128);
    else xp_text(L"netshell.dll", 23012, L"Connected", text, 128);
    SetDlgItemTextW(net_general, 1007, text);
    seconds = sheet.up > 0 && since && *since ? (GetTickCount64() - *since) / 1000 : 0;
    if (seconds >= 86400)
    {
        WCHAR days[32];
        if (seconds >= 2 * 86400) xp_text(L"netshell.dll", 23070, L" days", days, ARRAYSIZE(days));
        else xp_text(L"netshell.dll", 23069, L" day", days, ARRAYSIZE(days));
        swprintf(text, 128, L"%u%ls %02u:%02u:%02u", (unsigned)(seconds / 86400), days, (unsigned)(seconds / 3600 % 24),
                 (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
    }
    else
        swprintf(text, 128, L"%02u:%02u:%02u", (unsigned)(seconds / 3600), (unsigned)(seconds / 60 % 60),
                 (unsigned)(seconds % 60));
    SetDlgItemTextW(net_general, 1008, text);
    if (sheet.up > 0) speed_text(sheet.speed, text, 128);
    else wcscpy(text, L"0 bps");
    SetDlgItemTextW(net_general, 1009, text);
    ShowWindow(GetDlgItem(net_general, 1043), sheet.wireless ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(net_general, 1044), sheet.wireless ? SW_SHOW : SW_HIDE);
    if (sheet.wireless && (old = (HBITMAP)SendDlgItemMessageW(net_general, 1044, STM_SETIMAGE, IMAGE_BITMAP,
                                                          (LPARAM)signal_bitmap(sheet.up > 0 ? sheet.signal : 0))))
        DeleteObject(old);
    swprintf(directory, MAX_PATH, L"Z:\\sys\\class\\net\\%ls\\statistics", sheet.name);
    thousands(read_number(directory, L"tx_bytes", 0), text, 128);
    SetDlgItemTextW(net_general, 1003, text);
    thousands(read_number(directory, L"rx_bytes", 0), text, 128);
    SetDlgItemTextW(net_general, 1005, text);
    thousands(read_number(directory, L"tx_errors", 0), text, 128);
    SetDlgItemTextW(net_general, 1012, text);
    thousands(read_number(directory, L"rx_errors", 0), text, 128);
    SetDlgItemTextW(net_general, 1013, text);
}

/* The Support page: address type, address, mask and gateway from nmcli. */
static void net_support_update(void)
{
    char out[1024], *line, *address = "", *gateway = "", *method = "";
    WCHAR script[512], wide[128], name[80];
    unsigned prefix_length = 0;
    if (!net_support) return;
    /* Every name goes into the shell quoted. */
    shell_quote_w(sheet.name, name, ARRAYSIZE(name));
    swprintf(script, 512, L"nmcli -t -g IP4.ADDRESS,IP4.GATEWAY device show %ls 2>/dev/null | head -2; "
             L"c=$(nmcli -g GENERAL.CONNECTION device show %ls 2>/dev/null); "
             L"[ -n \"$c\" ] && nmcli -g ipv4.method connection show \"$c\" 2>/dev/null", name, name);
    capture(script, out, sizeof(out));
    line = strtok(out, "\n");
    if (line) { address = line; line = strtok(NULL, "\n"); }
    if (line) { gateway = line; line = strtok(NULL, "\n"); }
    if (line) method = line;
    if (strchr(address, '|')) *strchr(address, '|') = 0;
    if (strchr(address, '/')) { prefix_length = atoi(strchr(address, '/') + 1); *strchr(address, '/') = 0; }
    if (!strcmp(method, "manual")) xp_text(L"netshell.dll", 2573, L"Manually Configured", wide, 128);
    else if (*method) xp_text(L"netshell.dll", 2571, L"Assigned by DHCP", wide, 128);
    else xp_text(L"netshell.dll", 1026, L"Unknown", wide, 128);
    SetDlgItemTextW(net_support, 1053, wide);
    MultiByteToWideChar(CP_UTF8, 0, *address ? address : "0.0.0.0", -1, wide, 128);
    SetDlgItemTextW(net_support, 1050, wide);
    if (prefix_length)
    {
        unsigned mask = prefix_length >= 32 ? 0xffffffff : ~(0xffffffffu >> prefix_length);
        swprintf(wide, 128, L"%u.%u.%u.%u", mask >> 24, mask >> 16 & 255, mask >> 8 & 255, mask & 255);
    }
    else wcscpy(wide, L"0.0.0.0");
    SetDlgItemTextW(net_support, 1051, wide);
    MultiByteToWideChar(CP_UTF8, 0, *gateway ? gateway : "", -1, wide, 128);
    SetDlgItemTextW(net_support, 1052, wide);
}

static INT_PTR CALLBACK net_general_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    WCHAR script[256];
    (void)lparam;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        RECT label, value;
        net_general = dialog;
        if (sheet.up <= 0)
        {
            WCHAR enable[64];
            xp_menu_text(L"netshell.dll", 11010, 16, L"En&able", enable, ARRAYSIZE(enable));
            SetDlgItemTextW(dialog, 1025, enable);
        }
        /* The signal bars go on the Signal Strength row, in the value column;
         * the Errors row is XP's for dial-up connections only. */
        GetWindowRect(GetDlgItem(dialog, 1043), &label);
        GetWindowRect(GetDlgItem(dialog, 1007), &value);
        MapWindowPoints(NULL, dialog, (POINT *)&label, 2);
        MapWindowPoints(NULL, dialog, (POINT *)&value, 2);
        SetWindowPos(GetDlgItem(dialog, 1044), NULL, value.left, label.top, 26, 13, SWP_NOZORDER);
        ShowWindow(GetDlgItem(dialog, 1027), SW_HIDE);
        ShowWindow(GetDlgItem(dialog, 1012), SW_HIDE);
        ShowWindow(GetDlgItem(dialog, 1013), SW_HIDE);
        net_status_update();
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == 1026) run_settings(CMD_NETWORK_SETTINGS);
        else if (LOWORD(wparam) == 1025)
        {
            /* Disable or enable, as XP's button does, through NetworkManager. */
            WCHAR name[80];
            shell_quote_w(sheet.name, name, ARRAYSIZE(name));
            swprintf(script, 256, sheet.up > 0 ? L"nmcli device disconnect %ls" : L"nmcli device connect %ls", name);
            spawn(script, NULL);
            PropSheet_PressButton(net_sheet, PSBTN_CANCEL);
        }
        return TRUE;
    case WM_DESTROY:
        net_general = NULL;
        return TRUE;
    }
    return FALSE;
}

/* netshell's Network Connection Details: the Support page's values, with the
 * adapter's MAC address. */
static INT_PTR CALLBACK net_details_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    static const struct { UINT id; const WCHAR *fallback; int control; } rows[] = {
        { 23143, L"Physical Address", 0 }, { 23145, L"IP Address", 1050 },
        { 23147, L"Subnet Mask", 1051 }, { 23149, L"Default Gateway", 1052 },
    };
    if (message == WM_INITDIALOG)
    {
        HWND list = GetDlgItem(dialog, 1070), support = (HWND)lparam;
        LVCOLUMNW column = {.mask = LVCF_TEXT | LVCF_WIDTH};
        WCHAR text[128], path[MAX_PATH];
        char address[64] = "";
        RECT area;
        GetClientRect(list, &area);
        xp_text(L"netshell.dll", 23159, L"Property", text, ARRAYSIZE(text));
        column.pszText = text;
        column.cx = area.right / 2;
        ListView_InsertColumn(list, 0, &column);
        xp_text(L"netshell.dll", 23160, L"Value", text, ARRAYSIZE(text));
        column.cx = area.right - area.right / 2 - GetSystemMetrics(SM_CXVSCROLL);
        ListView_InsertColumn(list, 1, &column);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        for (int i = 0; i < (int)ARRAYSIZE(rows); i++)
        {
            LVITEMW item = {.mask = LVIF_TEXT, .iItem = i, .pszText = text};
            xp_text(L"netshell.dll", rows[i].id, rows[i].fallback, text, ARRAYSIZE(text));
            ListView_InsertItem(list, &item);
            if (rows[i].control) GetDlgItemTextW(support, rows[i].control, text, ARRAYSIZE(text));
            else
            {
                /* XP writes the MAC address as 00-0C-29-12-34-56. */
                swprintf(path, MAX_PATH, L"Z:\\sys\\class\\net\\%ls\\address", sheet.name);
                read_text(path, address, sizeof(address));
                for (int j = 0; j < (int)sizeof(address); j++)
                    if (!(text[j] = address[j] == ':' ? L'-' : towupper(address[j]))) break;
            }
            ListView_SetItemText(list, i, 1, text);
        }
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL))
    {
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK net_support_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    WCHAR script[256];
    (void)lparam;
    switch (message)
    {
    case WM_INITDIALOG:
        net_support = dialog;
        net_support_update();
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == 1056)
        {
            /* Repair: NetworkManager reapplies the connection. */
            WCHAR name[80];
            shell_quote_w(sheet.name, name, ARRAYSIZE(name));
            swprintf(script, 256, L"nmcli device reapply %ls || nmcli device connect %ls", name, name);
            spawn(script, NULL);
        }
        else if (LOWORD(wparam) == 1062)
            DialogBoxParamW(xp_module(L"netshell.dll"), MAKEINTRESOURCEW(1807), dialog, net_details_proc, (LPARAM)dialog);
        return TRUE;
    case WM_DESTROY:
        net_support = NULL;
        return TRUE;
    }
    return FALSE;
}

/* The status of interface DEVICE, titled with LABEL (a connection's name), or
 * without one with XP's name for a LAN or wireless connection. */
static void show_network_status(const WCHAR *device, const WCHAR *label)
{
    static HMODULE netshell;
    PROPSHEETPAGEW pages[2] = {{0}};
    PROPSHEETHEADERW header = {0};
    WCHAR title[128], name[96], close[32];
    char route[8192];
    if (!device || !device[0]) return;
    if (net_sheet && !wcscmp(sheet.name, device))
    {
        SetForegroundWindow(net_sheet);
        return;
    }
    if (net_sheet)
    {
        DestroyWindow(net_sheet);
        net_sheet = NULL;
    }
    if (!netshell) netshell = xp_module(L"netshell.dll");
    if (!netshell) return;
    lstrcpynW(sheet.name, device, ARRAYSIZE(sheet.name));
    read_routes(route, sizeof(route));
    poll_interface(&sheet, route, TRUE);
    if (sheet.up < 0)
    {
        sheet.name[0] = 0;
        return;
    }
    for (int i = 0; i < 2; i++)
    {
        pages[i].dwSize = sizeof(pages[i]);
        pages[i].hInstance = netshell;
    }
    pages[0].pszTemplate = MAKEINTRESOURCEW(1801);
    pages[0].pfnDlgProc = net_general_proc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(1806);
    pages[1].pfnDlgProc = net_support_proc;
    if (label && label[0]) lstrcpynW(name, label, ARRAYSIZE(name));
    else if (sheet.wireless) xp_text(L"xpsp2res.dll", 16022, L"Wireless Network Connection", name, ARRAYSIZE(name));
    else xp_text(L"netshell.dll", 1811, L"Local Area Connection", name, ARRAYSIZE(name));
    xp_text(L"netshell.dll", 23041, L"%1 Status", title, ARRAYSIZE(title), name);
    header.dwSize = sizeof(header);
    header.dwFlags = PSH_PROPSHEETPAGE | PSH_MODELESS | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
    header.hInstance = netshell;
    header.pszCaption = title;
    header.nPages = 2;
    header.ppsp = pages;
    net_sheet = (HWND)PropertySheetW(&header);
    if (net_sheet)
    {
        /* XP's status sheet has a single Close button. */
        xp_text(L"netshell.dll", 23020, L"&Close", close, ARRAYSIZE(close));
        SetDlgItemTextW(net_sheet, IDCANCEL, close);
        ShowWindow(GetDlgItem(net_sheet, IDOK), SW_HIDE);
    }
}

static void flyouts_update(void)
{
    power_meter_update();
    net_status_update();
}

/* ---- Network Connections: saved profiles and live actions from NetworkManager */

#define CONNECTIONS_CLASS L"XPNetworkConnections"

static HIMAGELIST connection_images;
static BOOL connection_pending;

/* nmcli -t escapes separators and backslashes. Decode a field up to ':' without
 * splitting a profile name that itself contains a colon. */
static void nmcli_field(char **cursor, char *out, size_t size)
{
    char *p = *cursor;
    size_t used = 0;
    while (*p && *p != '\n' && *p != '\r' && *p != ':')
    {
        char c = *p++;
        if (c == '\\' && *p)
        {
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
        }
        if (used + 1 < size) out[used++] = c;
    }
    out[used] = 0;
    if (*p == ':') p++;
    *cursor = p;
}

static void nmcli_wide(const char *source, WCHAR *target, size_t size)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, source, -1, target, (int)size)) target[0] = 0;
}

static BOOL active_uuid(const char *uuid, const char active[][40], unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(uuid, active[i])) return TRUE;
    return FALSE;
}

/* XP's Type column: its virtual private networks, or LAN or High-Speed Internet. */
static void connection_type(const WCHAR *type, WCHAR *out, int size)
{
    if (!wcscmp(type, L"vpn") || !wcscmp(type, L"wireguard"))
        xp_text(L"netshell.dll", 1024, L"Virtual Private Network", out, size);
    else xp_text(L"netshell.dll", 1022, L"LAN or High-Speed Internet", out, size);
}

static const WCHAR *nmcli_missing(void)
{
    return own_text(L"NetworkManager (nmcli) is not available.", L"NetworkManager (nmcli) を利用できません。");
}

static int selected_connection(void)
{
    int row = ListView_GetNextItem(connections_list, -1, LVNI_SELECTED);
    if (row < 0) return -1;
    LVITEMW item = {.mask = LVIF_PARAM, .iItem = row};
    if (!ListView_GetItem(connections_list, &item) || item.lParam < 0 || (unsigned)item.lParam >= connection_count)
        return -1;
    return (int)item.lParam;
}

static void connections_update_buttons(void)
{
    int index = selected_connection();
    BOOL selected = index >= 0;
    WCHAR text[64];
    if (connections_connect)
    {
        /* The connection's menu items in XP's Network Connections folder */
        if (selected && connections[index].active) xp_menu_text(L"netshell.dll", 11022, 3, L"Disc&onnect", text, 64);
        else xp_menu_text(L"netshell.dll", 11020, 2, L"C&onnect", text, 64);
        SetWindowTextW(connections_connect, text);
        EnableWindow(connections_connect, selected);
    }
    EnableWindow(GetDlgItem(GetParent(connections_list), CONN_STATUS), selected && connections[index].active);
}

static void connections_refresh(void)
{
    char output[16384], active_output[4096], active[CONNECTION_LIMIT][40];
    unsigned active_count = 0;
    char *context, *line;
    WCHAR selected[40] = L"";
    int index = selected_connection();
    if (!connections_list) return;
    /* The refresh keeps the selection. */
    if (index >= 0) wcscpy(selected, connections[index].uuid);
    connection_count = 0;
    if (!nmcli_checked)
    {
        char path[256];
        nmcli_available = capture(L"command -v nmcli 2>/dev/null", path, sizeof(path)) && *path;
        nmcli_checked = 1;
    }
    if (!nmcli_available)
    {
        ListView_DeleteAllItems(connections_list);
        SetWindowTextW(connections_hint, nmcli_missing());
        connections_update_buttons();
        return;
    }
    active_output[0] = 0;
    capture(L"nmcli -t -g UUID connection show --active 2>/dev/null", active_output, sizeof(active_output));
    for (line = strtok_s(active_output, "\r\n", &context); line && active_count < CONNECTION_LIMIT;
         line = strtok_s(NULL, "\r\n", &context))
    {
        strncpy(active[active_count], line, sizeof(active[active_count]) - 1);
        active[active_count][sizeof(active[active_count]) - 1] = 0;
        active_count++;
    }
    if (!capture(L"nmcli -t --escape yes -f NAME,UUID,TYPE,DEVICE connection show 2>/dev/null", output, sizeof(output)))
    {
        ListView_DeleteAllItems(connections_list);
        SetWindowTextW(connections_hint, nmcli_missing());
        connections_update_buttons();
        return;
    }
    for (line = strtok_s(output, "\r\n", &context); line && connection_count < CONNECTION_LIMIT;
         line = strtok_s(NULL, "\r\n", &context))
    {
        char *cursor = line, name[512], uuid[128], type[128], device[128];
        NMCONNECTION *connection;
        nmcli_field(&cursor, name, sizeof(name));
        nmcli_field(&cursor, uuid, sizeof(uuid));
        nmcli_field(&cursor, type, sizeof(type));
        nmcli_field(&cursor, device, sizeof(device));
        /* NetworkManager's own loopback profile is no network connection. */
        if (!*uuid || !*name || !strcmp(type, "loopback")) continue;
        connection = &connections[connection_count];
        nmcli_wide(name, connection->name, ARRAYSIZE(connection->name));
        nmcli_wide(uuid, connection->uuid, ARRAYSIZE(connection->uuid));
        nmcli_wide(type, connection->type, ARRAYSIZE(connection->type));
        nmcli_wide(device, connection->device, ARRAYSIZE(connection->device));
        connection->active = active_uuid(uuid, active, active_count);
        connection_count++;
    }
    ListView_DeleteAllItems(connections_list);
    for (unsigned i = 0; i < connection_count; i++)
    {
        NMCONNECTION *connection = &connections[i];
        BOOL wireless = !wcscmp(connection->type, L"802-11-wireless");
        int image = wireless ? (connection->active ? 0 : 1) : (connection->active ? 2 : 3);
        WCHAR text[64];
        LVITEMW item = {.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM, .iItem = (int)i,
                        .iImage = image, .lParam = i, .pszText = connection->name};
        int row = ListView_InsertItem(connections_list, &item);
        if (row < 0) continue;
        connection_type(connection->type, text, ARRAYSIZE(text));
        ListView_SetItemText(connections_list, row, 1, text);
        ListView_SetItemText(connections_list, row, 2, connection->device[0] ? connection->device : L"—");
        if (connection->active) xp_text(L"netshell.dll", 1042, L"Connected", text, ARRAYSIZE(text));
        else xp_text(L"netshell.dll", 1040, L"Disconnected", text, ARRAYSIZE(text));
        ListView_SetItemText(connections_list, row, 3, text);
        if (!wcscmp(connection->uuid, selected))
            ListView_SetItemState(connections_list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (!connection_pending) SetWindowTextW(connections_hint, L"");
    connection_pending = FALSE;
    connections_update_buttons();
}

static void connection_action(void)
{
    int index = selected_connection();
    WCHAR script[256], uuid[96], text[64];
    if (index < 0) return;
    shell_quote_w(connections[index].uuid, uuid, ARRAYSIZE(uuid));
    swprintf(script, ARRAYSIZE(script), L"nmcli --wait 15 connection %ls uuid %ls >/dev/null 2>&1",
             connections[index].active ? L"down" : L"up", uuid);
    if (spawn(script, NULL))
    {
        if (connections[index].active) xp_text(L"netshell.dll", 1043, L"Disconnecting", text, ARRAYSIZE(text));
        else xp_text(L"netshell.dll", 1041, L"Connecting", text, ARRAYSIZE(text));
        SetWindowTextW(connections_hint, text);
        connection_pending = TRUE;
        SetTimer(connections_window, TIMER_CONNECTIONS, 1500, NULL);
    }
    else SetWindowTextW(connections_hint, nmcli_missing());
}

static void connections_layout(HWND hwnd, int width, int height)
{
    int button_y = height - 42;
    MoveWindow(connections_list, 12, 12, width - 24, height - 104, TRUE);
    MoveWindow(connections_hint, 12, height - 84, width - 24, 22, TRUE);
    MoveWindow(connections_connect, width - 396, button_y, 90, 26, TRUE);
    MoveWindow(GetDlgItem(hwnd, CONN_STATUS), width - 300, button_y, 90, 26, TRUE);
    MoveWindow(GetDlgItem(hwnd, CONN_SETTINGS), width - 204, button_y, 90, 26, TRUE);
    MoveWindow(GetDlgItem(hwnd, CONN_CLOSE), width - 108, button_y, 90, 26, TRUE);
}

static LRESULT CALLBACK connections_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        LVCOLUMNW column = {.mask = LVCF_TEXT | LVCF_WIDTH};
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
        HFONT font = GetStockObject(DEFAULT_GUI_FONT);
        WCHAR text[64];
        InitCommonControlsEx(&controls);
        connections_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            12, 12, 540, 240, hwnd, (HMENU)(INT_PTR)1000, GetModuleHandleW(NULL), NULL);
        connections_hint = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 260, 540, 22, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        /* The buttons are the connection's commands in XP's folder menus. */
        connections_connect = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 90, 26, hwnd, (HMENU)(INT_PTR)CONN_CONNECT, GetModuleHandleW(NULL), NULL);
        xp_menu_text(L"netshell.dll", 11010, 4, L"Stat&us", text, ARRAYSIZE(text));
        CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 90, 26, hwnd, (HMENU)(INT_PTR)CONN_STATUS, GetModuleHandleW(NULL), NULL);
        xp_menu_text(L"netshell.dll", 11010, 8, L"P&roperties", text, ARRAYSIZE(text));
        CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 90, 26, hwnd, (HMENU)(INT_PTR)CONN_SETTINGS, GetModuleHandleW(NULL), NULL);
        xp_text(L"netshell.dll", 23020, L"&Close", text, ARRAYSIZE(text));
        CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 90, 26, hwnd, (HMENU)(INT_PTR)CONN_CLOSE, GetModuleHandleW(NULL), NULL);
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, (WPARAM)font, FALSE);
        ListView_SetExtendedListViewStyle(connections_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
        /* XP's folder columns */
        column.pszText = text;
        xp_text(L"netshell.dll", 1000, L"Name", text, ARRAYSIZE(text));
        column.cx = 200; ListView_InsertColumn(connections_list, 0, &column);
        xp_text(L"netshell.dll", 1001, L"Type", text, ARRAYSIZE(text));
        column.cx = 150; ListView_InsertColumn(connections_list, 1, &column);
        xp_text(L"netshell.dll", 1003, L"Device Name", text, ARRAYSIZE(text));
        column.cx = 90; ListView_InsertColumn(connections_list, 2, &column);
        xp_text(L"netshell.dll", 1002, L"Status", text, ARRAYSIZE(text));
        column.cx = 90; ListView_InsertColumn(connections_list, 3, &column);
        connection_images = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 0);
        if (connection_images)
        {
            ImageList_AddIcon(connection_images, xp_icon(10)); /* connected wireless */
            ImageList_AddIcon(connection_images, xp_icon(11)); /* disconnected wireless */
            ImageList_AddIcon(connection_images, xp_icon(4));  /* connected wired */
            ImageList_AddIcon(connection_images, xp_icon(8));  /* disconnected wired */
            ListView_SetImageList(connections_list, connection_images, LVSIL_SMALL);
        }
        connections_layout(hwnd, 580, 340);
        connections_refresh();
        SetTimer(hwnd, TIMER_CONNECTIONS, 4000, NULL);
        return 0;
    }
    case WM_SIZE:
        connections_layout(hwnd, LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_NOTIFY:
    {
        NMHDR *header = (NMHDR *)lparam;
        if (header->hwndFrom == connections_list && header->code == LVN_ITEMCHANGED)
            connections_update_buttons();
        else if (header->hwndFrom == connections_list && header->code == NM_DBLCLK)
        {
            int index = selected_connection();
            if (index >= 0 && connections[index].active)
                show_network_status(connections[index].device, connections[index].name);
            else connection_action();
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_CONNECTIONS)
        {
            connections_refresh();
            SetTimer(hwnd, TIMER_CONNECTIONS, 4000, NULL);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case CONN_CONNECT: connection_action(); return 0;
        case CONN_STATUS:
        {
            int index = selected_connection();
            if (index >= 0) show_network_status(connections[index].device, connections[index].name);
            return 0;
        }
        case CONN_SETTINGS: run_settings(CMD_NETWORK_SETTINGS); return 0;
        case CONN_CLOSE: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CONNECTIONS);
        if (connection_images) ImageList_Destroy(connection_images);
        connection_images = NULL;
        connections_window = connections_list = connections_hint = connections_connect = NULL;
        connection_count = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

/* XP's own Network Connections folder (netshell.dll), where the session has
 * registered it (prefix/extras.py), in the shell's process. */
static BOOL open_network_folder(void)
{
    static const WCHAR folder[] = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}"
                                  L"\\::{7007ACC7-3202-11D1-AAD2-00805FC1270E}";
    WCHAR command[256];
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{BA126AE5-2166-11D1-B1D0-00805FC1270E}\\LocalServer32", 0,
                      KEY_READ, &key))
        return FALSE;
    RegCloseKey(key);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{7007ACC7-3202-11D1-AAD2-00805FC1270E}\\InProcServer32", 0,
                      KEY_READ, &key))
        return FALSE;
    RegCloseKey(key);
    swprintf(command, ARRAYSIZE(command), L"C:\\xp\\xp-explorer.exe %ls", folder);
    if (!CreateProcessW(L"C:\\xp\\xp-explorer.exe", command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
        return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

/* A wireless adapter in the Linux system, whatever carries the traffic. */
static BOOL wireless_adapter(void)
{
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(L"Z:\\sys\\class\\net\\*", &found);
    BOOL wireless = FALSE;
    if (find == INVALID_HANDLE_VALUE) return FALSE;
    do
    {
        WCHAR path[MAX_PATH];
        if (found.cFileName[0] == '.') continue;
        swprintf(path, MAX_PATH, L"Z:\\sys\\class\\net\\%ls\\wireless", found.cFileName);
        wireless = exists(path);
    } while (!wireless && FindNextFileW(find, &found));
    FindClose(find);
    return wireless;
}

static void show_network_connections(void)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {.style = CS_DBLCLKS, .lpfnWndProc = connections_proc, .hInstance = instance,
                     .hCursor = LoadCursorW(NULL, IDC_ARROW), .hIcon = xp_icon(4),
                     .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1), .lpszClassName = CONNECTIONS_CLASS};
    if (connections_window)
    {
        ShowWindow(connections_window, SW_RESTORE);
        SetForegroundWindow(connections_window);
        return;
    }
    if (open_network_folder()) return;
    WCHAR title[64];
    if (!GetClassInfoW(instance, CONNECTIONS_CLASS, &cls) && !RegisterClassW(&cls)) return;
    xp_text(L"netshell.dll", 1060, L"Network Connections", title, ARRAYSIZE(title));
    connections_window = CreateWindowExW(WS_EX_DLGMODALFRAME, CONNECTIONS_CLASS, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 600, 410,
        NULL, NULL, instance, NULL);
    if (connections_window)
    {
        ShowWindow(connections_window, SW_SHOW);
        SetForegroundWindow(connections_window);
    }
}

/* ---- XP-style available wireless networks, backed by NetworkManager ----- */

#define WIRELESS_CLASS L"XPWirelessNetworks"

static void shell_quote_w(const WCHAR *source, WCHAR *target, size_t capacity)
{
    size_t used = 0;
    if (!capacity) return;
    target[used++] = L'\'';
    while (*source && used + 5 < capacity)
    {
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

static void wireless_layout(HWND hwnd, int width, int height)
{
    MoveWindow(GetDlgItem(hwnd, WIFI_TASKS), 0, 0, 155, height, TRUE);
    MoveWindow(GetDlgItem(hwnd, 610), 164, 2, width - 174, 23, TRUE);
    MoveWindow(GetDlgItem(hwnd, 611), 164, 36, width - 174, 27, TRUE);
    MoveWindow(wireless_list, 164, 64, width - 174, height - 136, TRUE);
    MoveWindow(wireless_hint, 164, height - 66, width - 260, 60, TRUE);
    MoveWindow(wireless_connect_button, width - 90, height - 31, 80, 24, TRUE);
}

static void wireless_hint_text(const WCHAR *file, UINT id, const WCHAR *fallback)
{
    WCHAR text[512];
    xp_text(file, id, fallback, text, ARRAYSIZE(text));
    SetWindowTextW(wireless_hint, text);
}

static NMWIRELESS *wireless_selected(void)
{
    int row = (int)SendMessageW(wireless_list, LB_GETCURSEL, 0, 0);
    LRESULT item = row == LB_ERR ? -1 : SendMessageW(wireless_list, LB_GETITEMDATA, row, 0);
    return item >= 0 && (unsigned)item < wireless_count ? &wireless_networks[item] : NULL;
}

static void wireless_update_button(void)
{
    int selected = (int)SendMessageW(wireless_list, LB_GETCURSEL, 0, 0);
    NMWIRELESS *network = wireless_selected();
    WCHAR text[64];
    /* XP's button disconnects the network it is connected to. */
    if (network && network->active) xp_text(L"xpsp2res.dll", 16051, L"&Disconnect", text, ARRAYSIZE(text));
    else xp_dialog_text(L"xpsp2res.dll", 6999, 2010, L"&Connect", text, ARRAYSIZE(text));
    SetWindowTextW(wireless_connect_button, text);
    EnableWindow(wireless_connect_button, network && !wireless_connect_started);
    int count = (int)SendMessageW(wireless_list, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessageW(wireless_list, LB_SETITEMHEIGHT, i, i == selected ? 114 : 48);
    InvalidateRect(wireless_list, NULL, TRUE);
}

static BOOL same_access_point(const NMWIRELESS *network, const WCHAR *ssid, const WCHAR *security)
{
    return !wcscmp(network->ssid, ssid) && !wcscmp(network->security, security);
}

static void wireless_refresh(void)
{
    char output[16384], *context, *line;
    WCHAR previous[128] = L"";
    int previous_row = (int)SendMessageW(wireless_list, LB_GETCURSEL, 0, 0), preferred = -1;
    BOOL listed = wireless_count > 0;
    if (!wireless_list) return;
    if (previous_row != LB_ERR)
    {
        LRESULT item = SendMessageW(wireless_list, LB_GETITEMDATA, previous_row, 0);
        if (item >= 0 && (unsigned)item < wireless_count)
            wcscpy(previous, wireless_networks[item].ssid);
    }
    wireless_count = 0;
    if (!nmcli_checked)
    {
        char path[256];
        nmcli_available = capture(L"command -v nmcli 2>/dev/null", path, sizeof(path)) && *path;
        nmcli_checked = 1;
    }
    if (!nmcli_available ||
        !capture(L"nmcli -t --escape yes -f SSID,BSSID,SIGNAL,SECURITY,IN-USE dev wifi list --rescan no 2>/dev/null",
                 output, sizeof(output)))
    {
        SendMessageW(wireless_list, LB_RESETCONTENT, 0, 0);
        SetWindowTextW(wireless_hint, nmcli_missing());
        wireless_update_button();
        return;
    }
    for (line = strtok_s(output, "\r\n", &context); line && wireless_count < CONNECTION_LIMIT;
         line = strtok_s(NULL, "\r\n", &context))
    {
        char *cursor = line, ssid[512], bssid[128], signal[32], security[128], in_use[16];
        WCHAR wide_ssid[128], wide_bssid[32], wide_security[64];
        int strength, duplicate = -1;
        NMWIRELESS *network;
        nmcli_field(&cursor, ssid, sizeof(ssid));
        nmcli_field(&cursor, bssid, sizeof(bssid));
        nmcli_field(&cursor, signal, sizeof(signal));
        nmcli_field(&cursor, security, sizeof(security));
        nmcli_field(&cursor, in_use, sizeof(in_use));
        if (!*bssid || !*ssid) continue;
        nmcli_wide(ssid, wide_ssid, ARRAYSIZE(wide_ssid));
        nmcli_wide(bssid, wide_bssid, ARRAYSIZE(wide_bssid));
        nmcli_wide(security, wide_security, ARRAYSIZE(wide_security));
        strength = atoi(signal);
        for (unsigned i = 0; i < wireless_count; i++)
            if (same_access_point(&wireless_networks[i], wide_ssid, wide_security))
            {
                duplicate = (int)i;
                break;
            }
        if (duplicate >= 0)
        {
            network = &wireless_networks[duplicate];
            if (strength > network->signal)
            {
                network->signal = strength;
                wcscpy(network->bssid, wide_bssid);
            }
            if (*in_use == '*') network->active = TRUE;
            continue;
        }
        network = &wireless_networks[wireless_count++];
        wcscpy(network->ssid, wide_ssid);
        wcscpy(network->bssid, wide_bssid);
        wcscpy(network->security, wide_security);
        network->signal = strength;
        network->secured = *security && strcmp(security, "--") != 0;
        network->active = *in_use == '*';
    }
    /* Keep the strongest AP first, while putting the currently connected SSID at the top. */
    for (unsigned i = 0; i < wireless_count; i++)
        for (unsigned j = i + 1; j < wireless_count; j++)
            if ((wireless_networks[j].active && !wireless_networks[i].active) ||
                (wireless_networks[j].active == wireless_networks[i].active &&
                 wireless_networks[j].signal > wireless_networks[i].signal))
            {
                NMWIRELESS swap = wireless_networks[i];
                wireless_networks[i] = wireless_networks[j];
                wireless_networks[j] = swap;
            }
    SendMessageW(wireless_list, LB_RESETCONTENT, 0, 0);
    for (unsigned i = 0; i < wireless_count; i++)
    {
        LRESULT row = SendMessageW(wireless_list, LB_ADDSTRING, 0, (LPARAM)wireless_networks[i].ssid);
        if (row == LB_ERR || row == LB_ERRSPACE) continue;
        SendMessageW(wireless_list, LB_SETITEMDATA, row, i);
        if (preferred < 0 && !wcscmp(wireless_networks[i].ssid, previous)) preferred = (int)row;
        if (preferred < 0 && !previous[0] && wireless_networks[i].active) preferred = (int)row;
    }
    if (wireless_count)
    {
        if (preferred < 0) preferred = 0;
        SendMessageW(wireless_list, LB_SETCURSEL, preferred, 0);
        /* The search's words go once there is a list; a connection's stay. */
        if (!listed) SetWindowTextW(wireless_hint, L"");
    }
    else if (GetTickCount64() - wireless_opened > 6000)
        wireless_hint_text(L"xpsp2res.dll", 16102, L"No wireless networks were found in range");
    wireless_update_button();
    InvalidateRect(wireless_list, NULL, TRUE);
}

/* XP's network key dialog: the key, typed twice. */
static INT_PTR CALLBACK network_key_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    WCHAR *key = (WCHAR *)GetWindowLongPtrW(dialog, DWLP_USER), confirm[128], text[512], format[512];
    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        GetDlgItemTextW(dialog, 2005, format, ARRAYSIZE(format));
        insert_text(format, wireless_connect_ssid, text, ARRAYSIZE(text));
        SetDlgItemTextW(dialog, 2005, text);
        SendDlgItemMessageW(dialog, 2007, EM_LIMITTEXT, 127, 0);
        SendDlgItemMessageW(dialog, 2012, EM_LIMITTEXT, 127, 0);
        SetForegroundWindow(dialog);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == 2007 && HIWORD(wparam) == EN_CHANGE)
        {
            /* The confirmation opens once there is a key to confirm. */
            BOOL typed = GetWindowTextLengthW(GetDlgItem(dialog, 2007)) > 0;
            EnableWindow(GetDlgItem(dialog, 2011), typed);
            EnableWindow(GetDlgItem(dialog, 2012), typed);
        }
        else if (LOWORD(wparam) == IDCANCEL) EndDialog(dialog, IDCANCEL);
        else if (LOWORD(wparam) == IDOK)
        {
            GetDlgItemTextW(dialog, 2007, key, 128);
            GetDlgItemTextW(dialog, 2012, confirm, ARRAYSIZE(confirm));
            if (!*key) return TRUE;
            if (wcscmp(key, confirm))
            {
                xp_text(L"xpsp2res.dll", 16031, L"The passwords entered do not match.", text, ARRAYSIZE(text));
                GetWindowTextW(dialog, format, ARRAYSIZE(format));
                MessageBoxW(dialog, text, format, MB_OK | MB_ICONEXCLAMATION);
                SetDlgItemTextW(dialog, 2012, L"");
                SetFocus(GetDlgItem(dialog, 2012));
            }
            else EndDialog(dialog, IDOK);
            SecureZeroMemory(confirm, sizeof(confirm));
        }
        return TRUE;
    }
    return FALSE;
}

/* KEY, for a script to read, in a new file in the user's own runtime
 * directory (/run/user/UID, which only they can enter): Wine gives a Linux
 * program neither its Windows environment nor its standard input, and keeps
 * XDG_RUNTIME_DIR from Windows programs. */
static BOOL write_key_file(const WCHAR *key, WCHAR *file, WCHAR *unix_file)
{
    char text[512], *uid;
    DWORD done;
    HANDLE handle;
    int size;
    handle = CreateFileW(L"Z:\\proc\\self\\status", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(handle, text, sizeof(text) - 1, &done, NULL);
    CloseHandle(handle);
    text[done] = 0;
    if (!(uid = strstr(text, "\nUid:"))) return FALSE;
    swprintf(unix_file, MAX_PATH, L"/run/user/%lu/xptray-key-%lu-%llu", strtoul(uid + 5, NULL, 10),
             GetCurrentProcessId(), GetTickCount64());
    swprintf(file, MAX_PATH, L"Z:%ls", unix_file);
    for (WCHAR *p = file; *p; p++)
        if (*p == '/') *p = '\\';
    size = WideCharToMultiByte(CP_UTF8, 0, key, -1, text, sizeof(text) - 1, NULL, NULL);
    if (size <= 0) return FALSE;
    text[size - 1] = '\n';
    handle = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        SecureZeroMemory(text, sizeof(text));
        return FALSE;
    }
    WriteFile(handle, text, size, &done, NULL);
    CloseHandle(handle);
    SecureZeroMemory(text, sizeof(text));
    return done == (DWORD)size;
}

/* Whether NetworkManager has a saved profile for SSID (with its key). */
static BOOL wireless_known(const WCHAR *ssid)
{
    char output[4096], wanted[512], *context, *line;
    if (!capture(L"for u in $(nmcli -t -g UUID,TYPE connection show 2>/dev/null | sed -n 's/:802-11-wireless$//p'); "
                 L"do nmcli --escape no -g 802-11-wireless.ssid connection show uuid \"$u\" 2>/dev/null; done",
                 output, sizeof(output)))
        return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, ssid, -1, wanted, sizeof(wanted), NULL, NULL);
    for (line = strtok_s(output, "\r\n", &context); line; line = strtok_s(NULL, "\r\n", &context))
        if (!strcmp(line, wanted)) return TRUE;
    return FALSE;
}

static void wireless_connect_selected(void)
{
    NMWIRELESS *network = wireless_selected();
    WCHAR ssid[512], bssid[128], script[1600], key[128], text[512], title[128];
    BOOL started;
    if (!network || wireless_connect_started) return;
    wcscpy(wireless_connect_ssid, network->ssid);
    shell_quote_w(network->ssid, ssid, ARRAYSIZE(ssid));
    shell_quote_w(network->bssid, bssid, ARRAYSIZE(bssid));
    if (network->active)
    {
        /* XP asks before it disconnects. */
        WCHAR format[512];
        HMODULE module = xp_module(L"xpsp2res.dll");
        if (!module || !LoadStringW(module, 16075, format, ARRAYSIZE(format)))
            wcscpy(format, L"Are you sure you wish to disconnect from the network '%s'?");
        insert_text(format, network->ssid, text, ARRAYSIZE(text));
        GetWindowTextW(wireless_window, title, ARRAYSIZE(title));
        if (MessageBoxW(wireless_window, text, title, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        swprintf(script, ARRAYSIZE(script), L"nmcli connection down id %ls >/dev/null 2>&1", ssid);
        spawn(script, NULL);
        SetTimer(wireless_window, TIMER_WIRELESS, 1500, NULL);
        return;
    }
    if (!network->secured || wireless_known(network->ssid))
    {
        swprintf(script, ARRAYSIZE(script), L"nmcli --wait 20 device wifi connect %ls bssid %ls >/dev/null 2>&1",
                 ssid, bssid);
        started = spawn(script, NULL);
    }
    else if (wcsstr(network->security, L"802.1X"))
    {
        /* An enterprise network needs more than a key: NetworkManager's own settings. */
        run_settings(CMD_NETWORK_SETTINGS);
        return;
    }
    else
    {
        /* A new protected network: XP's network key dialog. NetworkManager
         * gets the key on its standard input, never on a command line, keeps
         * it in a new profile when it connects, and the profile goes again
         * when it does not. */
        WCHAR key_file[MAX_PATH], unix_file[MAX_PATH], quoted_file[MAX_PATH + 8];
        const WCHAR *method = wcsstr(network->security, L"WPA1") || wcsstr(network->security, L"WPA2")
                              ? L"wpa-psk" : wcsstr(network->security, L"WPA3") ? L"sae" : L"none";
        const WCHAR *secret = !wcscmp(method, L"none") ? L"802-11-wireless-security.wep-key0"
                                                       : L"802-11-wireless-security.psk";
        if (DialogBoxParamW(xp_module(L"xpsp2res.dll"), MAKEINTRESOURCEW(7200), wireless_window, network_key_proc,
                            (LPARAM)key) != IDOK)
            return;
        started = write_key_file(key, key_file, unix_file);
        SecureZeroMemory(key, sizeof(key));
        if (!started) return;
        shell_quote_w(unix_file, quoted_file, ARRAYSIZE(quoted_file));
        swprintf(script, ARRAYSIZE(script),
                 L"f=%ls; IFS= read -r k < \"$f\"; rm -f \"$f\"; "
                 L"u=$(cat /proc/sys/kernel/random/uuid) && "
                 L"nmcli connection add type wifi con-name %ls ssid %ls connection.uuid \"$u\" "
                 L"connection.autoconnect no wifi-sec.key-mgmt %ls%ls >/dev/null 2>&1 && "
                 L"if printf '%%s:%%s\\n' %ls \"$k\" | "
                 L"nmcli --wait 20 connection up uuid \"$u\" passwd-file /dev/stdin >/dev/null 2>&1; "
                 L"then nmcli connection modify uuid \"$u\" connection.autoconnect yes; "
                 L"else nmcli connection delete uuid \"$u\"; fi >/dev/null 2>&1",
                 quoted_file, ssid, ssid, method, !wcscmp(method, L"none") ? L" wifi-sec.wep-key-type 1" : L"",
                 secret);
        started = spawn(script, NULL);
        if (!started) DeleteFileW(key_file);
    }
    if (!started)
    {
        SetWindowTextW(wireless_hint, nmcli_missing());
        return;
    }
    wireless_connect_started = GetTickCount64();
    wireless_hint_text(L"xpsp2res.dll", 16081, L"Connecting...");
    wireless_update_button();
    SetTimer(wireless_window, TIMER_WIRELESS_CONNECT, 1500, NULL);
}

/* These are XP's wireless UI icons, not the notification-area glyphs. */
static HICON wireless_icon(int id, int size)
{
    static HMODULE module;
    static struct { int id, size; HICON icon; } cache[20];
    static unsigned count;
    for (unsigned i = 0; i < count; i++)
        if (cache[i].id == id && cache[i].size == size) return cache[i].icon;
    if (!module) module = xp_module(L"xpsp2res.dll");
    HICON icon = module ? LoadImageW(module, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, 0) : NULL;
    if (count < ARRAYSIZE(cache))
    {
        cache[count].id = id; cache[count].size = size; cache[count++].icon = icon;
    }
    return icon;
}

static void wireless_gradient(HDC dc, RECT rect, COLORREF start, COLORREF end, BOOL horizontal)
{
    int length = horizontal ? rect.right - rect.left : rect.bottom - rect.top;
    HGDIOBJ old = SelectObject(dc, GetStockObject(DC_PEN));
    for (int i = 0; i < length; i++)
    {
        int n = max(1, length - 1);
        COLORREF color = RGB(GetRValue(start) + ((int)GetRValue(end) - GetRValue(start)) * i / n,
                             GetGValue(start) + ((int)GetGValue(end) - GetGValue(start)) * i / n,
                             GetBValue(start) + ((int)GetBValue(end) - GetBValue(start)) * i / n);
        SetDCPenColor(dc, color);
        if (horizontal)
        {
            MoveToEx(dc, rect.left + i, rect.top, NULL); LineTo(dc, rect.left + i, rect.bottom);
        }
        else
        {
            MoveToEx(dc, rect.left, rect.top + i, NULL); LineTo(dc, rect.right, rect.top + i);
        }
    }
    SelectObject(dc, old);
}

static void wireless_task_card(HDC dc, RECT rect, const WCHAR *title, HFONT heading)
{
    HBRUSH body = CreateSolidBrush(RGB(214, 223, 247));
    FillRect(dc, &rect, body); DeleteObject(body);
    RECT header = {rect.left + 1, rect.top + 1, rect.right - 1, rect.top + 21};
    wireless_gradient(dc, header, RGB(255, 255, 255), RGB(198, 211, 247), TRUE);
    FrameRect(dc, &rect, GetStockObject(WHITE_BRUSH));
    SelectObject(dc, heading);
    SetTextColor(dc, RGB(33, 93, 198));
    header.left += 7;
    DrawTextW(dc, title, -1, &header, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

static void wireless_task_link(HDC dc, int icon_id, int top, int bottom, const WCHAR *text)
{
    HICON icon = wireless_icon(icon_id, 16);
    if (icon) DrawIconEx(dc, 14, top, icon, 16, 16, 0, NULL, DI_NORMAL);
    RECT label = {34, top + 1, 141, bottom};
    DrawTextW(dc, text, -1, &label, DT_WORDBREAK | DT_NOPREFIX);
}

static int wireless_task_at(POINT point)
{
    if (point.x < 7 || point.x >= 146) return 0;
    if (point.y >= 34 && point.y < 62) return WIFI_REFRESH;
    if (point.y >= 68 && point.y < 105) return WIFI_SETTINGS;
    if (point.y >= 141 && point.y < 174) return WIFI_SETTINGS;
    if (point.y >= 176 && point.y < 210) return WIFI_SETTINGS;
    return 0;
}

static void wireless_draw_tasks(const DRAWITEMSTRUCT *draw)
{
    HFONT heading = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    HFONT link = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    HGDIOBJ old_font = SelectObject(draw->hDC, heading);
    SetBkMode(draw->hDC, TRANSPARENT);
    /* The Luna task-pane colors from XP's shellstyle.dll. */
    wireless_gradient(draw->hDC, draw->rcItem, RGB(123, 162, 231), RGB(99, 117, 214), FALSE);
    /* The task pane's words from XP's dialog for this window */
    static const struct { UINT id; const WCHAR *fallback; } texts[] = {
        { 7003, L"Network Tasks" }, { 7010, L"Related Tasks" }, { 7004, L"Refresh network list" },
        { 7019, L"Set up a wireless network for a home or small office" },
        { 7011, L"Change the order of preferred networks" }, { 7017, L"Change advanced settings" },
    };
    WCHAR text[ARRAYSIZE(texts)][128];
    for (unsigned i = 0; i < ARRAYSIZE(texts); i++)
        xp_dialog_text(L"xpsp2res.dll", 6999, texts[i].id, texts[i].fallback, text[i], ARRAYSIZE(text[i]));
    wireless_task_card(draw->hDC, (RECT){7, 8, 146, 105}, text[0], heading);
    wireless_task_card(draw->hDC, (RECT){7, 117, 146, 213}, text[1], heading);
    SelectObject(draw->hDC, link);
    wireless_task_link(draw->hDC, 2020, 36, 63, text[2]);
    wireless_task_link(draw->hDC, 2003, 70, 102, text[3]);
    wireless_task_link(draw->hDC, 2022, 143, 173, text[4]);
    wireless_task_link(draw->hDC, 2023, 178, 209, text[5]);
    SelectObject(draw->hDC, old_font);
    DeleteObject(link); DeleteObject(heading);
}

static void wireless_draw_row(const DRAWITEMSTRUCT *draw)
{
    if (draw->itemID == (UINT)-1) return;
    LRESULT index = SendMessageW(wireless_list, LB_GETITEMDATA, draw->itemID, 0);
    if (index < 0 || (unsigned)index >= wireless_count) return;
    NMWIRELESS *network = &wireless_networks[index];
    RECT rect = draw->rcItem;
    BOOL selected = (draw->itemState & ODS_SELECTED) != 0;
    HBRUSH background = CreateSolidBrush(selected ? RGB(49, 106, 197) : RGB(241, 243, 251));
    FillRect(draw->hDC, &rect, background); DeleteObject(background);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, selected ? RGB(255,255,255) : RGB(0,0,0));
    HICON antenna = wireless_icon(2000, 32);
    if (antenna) DrawIconEx(draw->hDC, rect.left + 6, rect.top + 7, antenna, 32, 32, 0, NULL, DI_NORMAL);
    int bars = max(0, min(5, (network->signal + 19) / 20));
    HICON signal = wireless_icon(2010 + bars, 24);
    if (signal) DrawIconEx(draw->hDC, rect.right - 24, rect.top + (selected ? 20 : 14), signal,
                           24, 24, 0, NULL, DI_NORMAL);
    HFONT title = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    HGDIOBJ old_font = SelectObject(draw->hDC, title);
    RECT text = {rect.left + 50, rect.top + 4, rect.right - 37, rect.top + 20};
    DrawTextW(draw->hDC, network->ssid, -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(draw->hDC, wireless_text_font);
    text.top = rect.top + 29; text.bottom = text.top + 16;
    if (network->secured)
    {
        HICON lock = wireless_icon(2001, 16);
        if (lock) DrawIconEx(draw->hDC, text.left, text.top - 2, lock, 16, 16, 0, NULL, DI_NORMAL);
        text.left += 18;
    }
    WCHAR detail[160], kind[96];
    if (network->secured)
    {
        xp_text(L"xpsp2res.dll", 16060, L"Security-enabled wireless network", kind, ARRAYSIZE(kind));
        swprintf(detail, ARRAYSIZE(detail), L"%ls (%ls)", kind, network->security);
    }
    else xp_text(L"xpsp2res.dll", 16062, L"Unsecured wireless network", detail, ARRAYSIZE(detail));
    DrawTextW(draw->hDC, detail, -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (selected)
    {
        text = (RECT){rect.left + 50, rect.top + 51, rect.right - 30, rect.bottom - 9};
        WCHAR help[512], connect[256];
        if (network->active)
            xp_text(L"xpsp2res.dll", 16090, L"You are currently connected to this network.", help, ARRAYSIZE(help));
        else
        {
            if (network->secured)
                xp_text(L"xpsp2res.dll", 16016, L"This network requires a network key.", help, ARRAYSIZE(help));
            else xp_text(L"xpsp2res.dll", 16015, L"This network is configured for open access.", help, ARRAYSIZE(help));
            xp_text(L"xpsp2res.dll", 16089, L"If you want to connect to this network, click Connect.", connect,
                    ARRAYSIZE(connect));
            wcsncat(help, L" ", ARRAYSIZE(help) - wcslen(help) - 1);
            wcsncat(help, connect, ARRAYSIZE(help) - wcslen(help) - 1);
        }
        DrawTextW(draw->hDC, help, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    }
    SelectObject(draw->hDC, old_font); DeleteObject(title);
    if (draw->itemState & ODS_FOCUS)
    {
        InflateRect(&rect, -1, -1); DrawFocusRect(draw->hDC, &rect);
    }
}

static LRESULT CALLBACK wireless_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        wireless_text_font = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        HFONT font = wireless_text_font ? wireless_text_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        WCHAR title[128], text[512];
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            0, 0, 150, 400, hwnd, (HMENU)(INT_PTR)WIFI_TASKS, GetModuleHandleW(NULL), NULL);
        xp_dialog_text(L"xpsp2res.dll", 6999, 2002, L"Choose a wireless network", title, ARRAYSIZE(title));
        CreateWindowW(L"STATIC", title, WS_CHILD | WS_VISIBLE | SS_LEFT,
            164, 12, 400, 24, hwnd, (HMENU)(INT_PTR)610, GetModuleHandleW(NULL), NULL);
        xp_dialog_text(L"xpsp2res.dll", 6999, 2003, L"Click an item in the list below to connect to a &wireless "
                       L"network in range or to get more information.", text, ARRAYSIZE(text));
        CreateWindowW(L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT, 164, 39, 400, 42, hwnd, (HMENU)(INT_PTR)611, GetModuleHandleW(NULL), NULL);
        wireless_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            160, 88, 400, 240, hwnd, (HMENU)(INT_PTR)WIFI_LIST, GetModuleHandleW(NULL), NULL);
        xp_text(L"xpsp2res.dll", 16101, L"Searching for wireless networks in range...", text, ARRAYSIZE(text));
        wireless_hint = CreateWindowW(L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT, 160, 340, 400, 24, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        wireless_connect_button = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            460, 370, 92, 26, hwnd, (HMENU)(INT_PTR)WIFI_CONNECT, GetModuleHandleW(NULL), NULL);
        for (int id = 610; id <= 611; id++) SendMessageW(GetDlgItem(hwnd, id), WM_SETFONT, (WPARAM)font, TRUE);
        wireless_title_font = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        if (wireless_title_font)
            SendMessageW(GetDlgItem(hwnd, 610), WM_SETFONT, (WPARAM)wireless_title_font, TRUE);
        SendMessageW(wireless_list, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(wireless_hint, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(wireless_connect_button, WM_SETFONT, (WPARAM)font, TRUE);
        RECT client;
        GetClientRect(hwnd, &client);
        wireless_layout(hwnd, client.right, client.bottom);
        wireless_opened = GetTickCount64();
        wireless_refresh();
        spawn(L"nmcli device wifi rescan >/dev/null 2>&1", NULL);
        SetTimer(hwnd, TIMER_WIRELESS, 2000, NULL);
        return 0;
    }
    case WM_SIZE:
        wireless_layout(hwnd, LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_MEASUREITEM:
        if (((MEASUREITEMSTRUCT *)lparam)->CtlID == WIFI_LIST)
        {
            ((MEASUREITEMSTRUCT *)lparam)->itemHeight = 48;
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lparam)->CtlID == WIFI_TASKS) wireless_draw_tasks((DRAWITEMSTRUCT *)lparam);
        else if (((DRAWITEMSTRUCT *)lparam)->CtlID == WIFI_LIST) wireless_draw_row((DRAWITEMSTRUCT *)lparam);
        return TRUE;
    case WM_TIMER:
        if (wparam == TIMER_WIRELESS)
        {
            wireless_refresh();
            SetTimer(hwnd, TIMER_WIRELESS, 10000, NULL);
        }
        else if (wparam == TIMER_WIRELESS_CONNECT)
        {
            BOOL connected = FALSE;
            /* Connected once NetworkManager uses the network (a wired
             * connection may still carry the default route). */
            wireless_refresh();
            for (unsigned i = 0; i < wireless_count; i++)
                if (wireless_networks[i].active && !wcscmp(wireless_networks[i].ssid, wireless_connect_ssid))
                    connected = TRUE;
            if (connected)
            {
                KillTimer(hwnd, TIMER_WIRELESS_CONNECT);
                wireless_connect_started = 0;
                wireless_hint_text(L"xpsp2res.dll", 16083, L"Connected!");
                wireless_update_button();
                poll_network(TRUE);
            }
            else if (GetTickCount64() - wireless_connect_started > 30000)
            {
                KillTimer(hwnd, TIMER_WIRELESS_CONNECT);
                wireless_connect_started = 0;
                wireless_hint_text(L"xpsp2res.dll", 16071, L"Windows is unable to connect to the selected network.");
                wireless_update_button();
            }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case WIFI_TASKS:
        {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(GetDlgItem(hwnd, WIFI_TASKS), &point);
            int command = wireless_task_at(point);
            if (command) PostMessageW(hwnd, WM_COMMAND, command, 0);
            return 0;
        }
        case WIFI_LIST:
            if (HIWORD(wparam) == LBN_DBLCLK) wireless_connect_selected();
            else if (HIWORD(wparam) == LBN_SELCHANGE) wireless_update_button();
            return 0;
        case WIFI_CONNECT: wireless_connect_selected(); return 0;
        case WIFI_REFRESH:
            spawn(L"nmcli device wifi rescan >/dev/null 2>&1", NULL);
            wireless_refresh();
            SetTimer(hwnd, TIMER_WIRELESS, 2000, NULL);
            return 0;
        case WIFI_SETTINGS: run_settings(CMD_NETWORK_SETTINGS); return 0;
        }
        break;
    case WM_SETCURSOR:
        if ((HWND)wparam == GetDlgItem(hwnd, WIFI_TASKS))
        {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient((HWND)wparam, &point);
            if (wireless_task_at(point))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_WIRELESS);
        KillTimer(hwnd, TIMER_WIRELESS_CONNECT);
        if (wireless_title_font) DeleteObject(wireless_title_font);
        wireless_title_font = NULL;
        if (wireless_text_font) DeleteObject(wireless_text_font);
        wireless_text_font = NULL;
        wireless_window = wireless_list = wireless_hint = wireless_connect_button = NULL;
        wireless_count = 0;
        wireless_connect_started = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

/* XP's own "Choose a wireless network" (wzcdlg.dll), where the session has
 * installed it with NetworkManager behind it (prefix/extras.py). */
static BOOL open_wireless_dialog(void)
{
    WCHAR command[] = L"rundll32.exe C:\\windows\\system32\\wzcsapi.dll,ViewAvailableNetworks";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!exists(L"C:\\windows\\system32\\wzcdlg.dll") || !exists(L"C:\\windows\\system32\\netshell.dll"))
        return FALSE;
    if (!CreateProcessW(L"C:\\windows\\rundll32.exe", command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
        return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

static void show_wireless_networks(void)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {.style = CS_DBLCLKS, .lpfnWndProc = wireless_proc, .hInstance = instance,
                     .hCursor = LoadCursorW(NULL, IDC_ARROW), .hIcon = wireless_icon(2021, 16),
                     .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1), .lpszClassName = WIRELESS_CLASS};
    if (wireless_window)
    {
        ShowWindow(wireless_window, SW_RESTORE);
        SetForegroundWindow(wireless_window);
        return;
    }
    if (open_wireless_dialog()) return;
    WCHAR title[128];
    if (!GetClassInfoW(instance, WIRELESS_CLASS, &cls) && !RegisterClassW(&cls)) return;
    xp_dialog_text(L"xpsp2res.dll", 6999, -1, L"Wireless Network Connection", title, ARRAYSIZE(title));
    wireless_window = CreateWindowExW(WS_EX_DLGMODALFRAME, WIRELESS_CLASS, title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 550, 456,
        NULL, NULL, instance, NULL);
    if (wireless_window)
    {
        ShowWindow(wireless_window, SW_SHOW);
        SetForegroundWindow(wireless_window);
    }
}

/* ---- Main window -------------------------------------------------------- */

static void run_settings(UINT command)
{
    switch (command)
    {
    case CMD_MIXER:
        if (open_volume_control()) break;
        /* Without XP's sndvol32: the Linux mixer. */
        spawn(L"command -v pavucontrol >/dev/null && exec pavucontrol; "
              L"kcmshell6 kcm_pulseaudio 2>/dev/null || systemsettings kcm_pulseaudio", NULL);
        break;
    case CMD_SOUND:
        spawn(L"kcmshell6 kcm_pulseaudio 2>/dev/null || systemsettings kcm_pulseaudio 2>/dev/null || pavucontrol", NULL);
        break;
    case CMD_NETWORK_SETTINGS:
        spawn(L"kcmshell6 kcm_networkmanagement 2>/dev/null || systemsettings kcm_networkmanagement 2>/dev/null "
              L"|| nm-connection-editor", NULL);
        break;
    case CMD_POWER:
        spawn(L"kcmshell6 kcm_powerdevilprofilesconfig 2>/dev/null || systemsettings kcm_powerdevilprofilesconfig",
              NULL);
        break;
    }
}

static void context_menu(UINT id)
{
    HMENU menu = CreatePopupMenu();
    POINT cursor;
    UINT command;
    WCHAR text[128];
    /* The items of XP's own menus for these icons */
    if (id == ID_VOLUME)
    {
        xp_text(L"stobject.dll", 256, L"&Open Volume Control", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_MIXER, text);
        xp_text(L"stobject.dll", 255, L"&Adjust Audio Properties", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_SOUND, text);
        SetMenuDefaultItem(menu, CMD_MIXER, FALSE);
    }
    else if (id == ID_NETWORK)
    {
        xp_menu_text(L"netshell.dll", 15103, 3, L"&Status", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_STATUS, text);
        xp_menu_text(L"netshell.dll", 15103, 6, L"&View Available Wireless Networks", text, ARRAYSIZE(text));
        if (wireless_adapter()) AppendMenuW(menu, MF_STRING, CMD_NETWORK, text);
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        xp_menu_text(L"netshell.dll", 15103, 1, L"&Open Network Connections", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_CONNECTIONS, text);
        SetMenuDefaultItem(menu, CMD_STATUS, FALSE);
    }
    else
    {
        xp_text(L"stobject.dll", 153, L"&Open Power Meter", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_METER, text);
        xp_text(L"stobject.dll", 152, L"&Adjust Power Properties", text, ARRAYSIZE(text));
        AppendMenuW(menu, MF_STRING, CMD_POWER, text);
        SetMenuDefaultItem(menu, CMD_METER, FALSE);
    }
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, NULL);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == CMD_STATUS) show_network_status(net.up >= 0 ? net.name : NULL, NULL);
    else if (command == CMD_METER) show_power_meter();
    else if (command == CMD_CONNECTIONS) show_network_connections();
    else if (command == CMD_NETWORK) show_wireless_networks();
    else if (command) run_settings(command);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_OPEN_WIRELESS)
    {
        show_wireless_networks();
        return 0;
    }
    if (message == WM_OPEN_CONNECTIONS)
    {
        show_network_connections();
        return 0;
    }
    if (message == taskbar_created && taskbar_created)
    {
        add_all();
        return 0;
    }
    switch (message)
    {
    case WM_TIMER:
        if (wparam == TIMER_FIND)
        {
            if (find_tray())
            {
                KillTimer(hwnd, TIMER_FIND);
                add_all();
                SetTimer(hwnd, TIMER_POLL, 1000, NULL);
            }
        }
        else if (wparam == TIMER_POLL)
        {
            poll();
            flyouts_update();
        }
        else if (wparam == TIMER_APPLY)
        {
            KillTimer(hwnd, TIMER_APPLY);
            if (wanted_volume >= 0)
            {
                set_volume(wanted_volume);
                if (muted && wanted_volume > 0) set_mute(0);
                wanted_volume = -1;
                show_volume();
            }
        }
        return 0;
    case WM_TRAY:
        switch (LOWORD(lparam))
        {
        case WM_LBUTTONUP:
            if (wparam == ID_BLUETOOTH) break;   /* XP's opens on a double click */
            if (wparam == ID_VOLUME) show_popup();
            else if (wparam == ID_NETWORK) show_network_connections();
            else show_power_meter();
            break;
        case WM_LBUTTONDBLCLK:
            if (wparam == ID_VOLUME) run_settings(CMD_MIXER);
            else if (wparam == ID_BLUETOOTH) bluetooth_command(BT_SHOW);
            break;
        case WM_RBUTTONUP:
            if (wparam == ID_BLUETOOTH) bluetooth_menu();
            else context_menu((UINT)wparam);
            break;
        }
        return 0;
    case WM_DESTROY:
        for (UINT id = 1; id < ID_COUNT; id++)
            if (icons[id].shown) tray_send(NIM_DELETE, id, NULL, NULL, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command_line, int show)
{
    WNDCLASSW main_class = {.lpfnWndProc = window_proc, .hInstance = instance, .lpszClassName = L"XPTrayHelper"};
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES | ICC_PROGRESS_CLASS};
    MSG message;
    BOOL open_connections = command_line && wcsstr(command_line, L"connections") != NULL;
    BOOL open_wireless = command_line && wcsstr(command_line, L"wireless") != NULL;
    (void)previous; (void)show;

    CreateMutexW(NULL, TRUE, L"XPTrayHelperRunning");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (open_connections || open_wireless)
        {
            HWND existing = FindWindowW(L"XPTrayHelper", L"XP notification icons");
            if (existing) SendMessageTimeoutW(existing, open_wireless ? WM_OPEN_WIRELESS : WM_OPEN_CONNECTIONS,
                                             0, 0, SMTO_ABORTIFHUNG, 3000, NULL);
        }
        return 0;
    }
    InitCommonControlsEx(&controls);
    RegisterClassW(&main_class);
    /* stobject's Power Meter template names systray's own dialog class, and
     * sndvol32's Volume template ("sndvol32 /t") its own. */
    {
        WNDCLASSW dialog_class = {.style = CS_GLOBALCLASS | CS_DBLCLKS, .lpfnWndProc = DefDlgProcW,
                                  .cbWndExtra = DLGWINDOWEXTRA, .hInstance = instance,
                                  .hCursor = LoadCursorW(NULL, IDC_ARROW),
                                  .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1), .lpszClassName = L"SystemTray_Main"};
        RegisterClassW(&dialog_class);
        dialog_class.lpszClassName = L"Tray Volume";
        RegisterClassW(&dialog_class);
    }
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    window = CreateWindowW(L"XPTrayHelper", L"XP notification icons", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!window) return 1;
    if (open_connections) PostMessageW(window, WM_OPEN_CONNECTIONS, 0, 0);
    if (open_wireless) PostMessageW(window, WM_OPEN_WIRELESS, 0, 0);
    SetTimer(window, TIMER_FIND, 500, NULL);
    while (GetMessageW(&message, NULL, 0, 0))
    {
        if (net_sheet && PropSheet_IsDialogMessage(net_sheet, &message))
        {
            if (!PropSheet_GetCurrentPageHwnd(net_sheet))
            {
                DestroyWindow(net_sheet);
                net_sheet = NULL;
                sheet.name[0] = 0;
            }
            continue;
        }
        if (popup && IsDialogMessageW(popup, &message)) continue;
        if (power_meter && IsDialogMessageW(power_meter, &message)) continue;
        if (wireless_window && IsDialogMessageW(wireless_window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
