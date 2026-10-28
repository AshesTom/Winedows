/* wzcsapi.dll for XP's own wireless dialogs (wzcdlg.dll: "Choose a wireless
 * network", the Wireless Networks page) and netshell.dll. XP's is a client of
 * the Wireless Zero Configuration service, which Linux replaces with
 * NetworkManager: this one answers the same calls from nmcli, so XP's dialogs
 * list the networks in range with their signal and security, and connecting
 * to one asks NetworkManager to join it.
 *
 * The structures are XP SP3's, as its RPC stubs and wzcdlg.dll lay them out:
 * INTF_ENTRY (0x60 bytes) and WZC_802_11_CONFIG_LIST of WZC_WLAN_CONFIG (0x2c8
 * bytes, laid out as Windows CE's documented one up to rdUserData). The
 * adapter is the one whose GUID xpnetcon.exe gives the connection (the same
 * hash of the Linux interface name). Every export is stdcall with XP's stack
 * size; the calls Linux has nothing for fail as when the service is stopped. */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "unixshell.h"

typedef struct
{
    DWORD size;
    BYTE *data;
} RAW_DATA;

typedef struct
{
    WCHAR *guid;
    WCHAR *description;
    ULONG media_state;
    ULONG media_type;
    ULONG physical_media_type;
    INT infrastructure_mode;
    INT authentication_mode;
    INT wep_status;
    DWORD reserved1;
    DWORD control_flags;
    DWORD reserved2[2];
    RAW_DATA reserved3;
    RAW_DATA ssid;
    RAW_DATA bssid;
    RAW_DATA bssid_list;
    RAW_DATA preferred_list;
    RAW_DATA control_data;
} INTF_ENTRY;

typedef struct
{
    ULONG length;
    DWORD control_flags;
    BYTE mac[6];
    BYTE reserved[2];
    ULONG ssid_length;
    BYTE ssid[32];
    ULONG privacy;
    LONG rssi;
    ULONG network_type;
    struct
    {
        ULONG length, beacon_period, atim_window, frequency;
        ULONG fh_length, hop_pattern, hop_set, dwell_time;
    } configuration;
    ULONG infrastructure_mode;
    BYTE rates[8];
    ULONG key_index;
    ULONG key_length;
    BYTE key[32];
    ULONG authentication_mode;
    RAW_DATA user_data;
    BYTE extension[0x2c8 - 0xa0];
} WZC_WLAN_CONFIG;

typedef struct
{
    ULONG count;
    ULONG index;
    WZC_WLAN_CONFIG config[];
} WZC_802_11_CONFIG_LIST;

typedef struct
{
    DWORD count;
    WCHAR **guids;
} INTFS_KEY_TABLE;

/* The service's flags (wzcsapi.h) */
#define INTF_ALL_FLAGS 0x0000ffff
#define INTF_DESCR 0x00010000
#define INTF_NDISMEDIA 0x00020000
#define INTF_PREFLIST 0x00040000
#define INTF_SP2_EXTRA 0x00080000
#define INTF_INFRAMODE 0x00200000
#define INTF_AUTHMODE 0x00400000
#define INTF_WEPSTATUS 0x00800000
#define INTF_SSID 0x01000000
#define INTF_BSSID 0x02000000
#define INTF_BSSIDLIST 0x04000000
#define INTF_LIST_SCAN 0x08000000
#define INTFCTL_CM_AUTO 2
#define WZCCTL_ON_DEMAND 0x0200
#define WZCCTL_CONNECTED 0x0c00
#define INTFCTL_OIDSSUPP 0x2000
#define INTFCTL_FALLBACK 0x4000
#define INTFCTL_ENABLED 0x8000
/* NDIS */
enum { AUTH_OPEN = 0, AUTH_WPA = 3, AUTH_WPA_PSK = 4, AUTH_WPA2 = 6, AUTH_WPA2_PSK = 7 };
enum { WEP_ENABLED = 0, WEP_DISABLED = 1, TKIP_ENABLED = 4, AES_ENABLED = 6 };

#define NETWORK_LIMIT 64

struct network
{
    char ssid[33];
    BYTE bssid[6];
    int signal;
    int frequency;
    BOOL active;
    ULONG authentication;
    BOOL privacy;
};

struct adapter
{
    WCHAR ifname[32];
    WCHAR description[256];
    BOOL connected;
    struct network networks[NETWORK_LIMIT];
    unsigned network_count;
    struct network saved[NETWORK_LIMIT];
    unsigned saved_count;
    ULONGLONG read;
};

static struct adapter cache;
static CRITICAL_SECTION lock;

void *__RPC_USER MIDL_user_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void __RPC_USER MIDL_user_free(void *memory)
{
    if (memory) HeapFree(GetProcessHeap(), 0, memory);
}

/* ---- Linux: the adapter and NetworkManager */

/* The GUID xpnetcon.exe gives the connection of IFNAME. */
static GUID link_guid(const WCHAR *ifname)
{
    GUID id = {0x4c696e75, 0x7820, 0x4e65, {0}};
    unsigned long long hash = 1469598103934665603ULL;
    for (const WCHAR *c = ifname; *c; c++) hash = (hash ^ *c) * 1099511628211ULL;
    memcpy(id.Data4, &hash, sizeof(id.Data4));
    return id;
}

static BOOL is_wireless(const WCHAR *ifname)
{
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"Z:\\sys\\class\\net\\%ls\\wireless", ifname);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* The wireless interface whose connection GUID is GUID (or the first one). */
static BOOL find_interface(const WCHAR *guid, WCHAR *ifname, size_t size)
{
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(L"Z:\\sys\\class\\net\\*", &found);
    BOOL ok = FALSE;
    if (find == INVALID_HANDLE_VALUE) return FALSE;
    do
    {
        WCHAR text[40];
        GUID id;
        if (found.cFileName[0] == '.' || !is_wireless(found.cFileName)) continue;
        id = link_guid(found.cFileName);
        StringFromGUID2(&id, text, ARRAYSIZE(text));
        if (!guid || !_wcsicmp(guid, text))
        {
            lstrcpynW(ifname, found.cFileName, (int)size);
            ok = TRUE;
        }
    } while (!ok && FindNextFileW(find, &found));
    FindClose(find);
    return ok;
}

/* The shell through unixshell.c, with UTF-8 scripts (they name SSIDs). */
static WCHAR *wide(const char *text)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    WCHAR *out = length ? malloc(length * sizeof(WCHAR)) : NULL;
    if (out) MultiByteToWideChar(CP_UTF8, 0, text, -1, out, length);
    return out;
}

static BOOL spawn(const char *script)
{
    WCHAR *line = wide(script);
    BOOL ok = line && unix_spawn(line);
    free(line);
    return ok;
}

/* SCRIPT's output without its errors (nmcli's warnings), as a string to free(),
 * or NULL when it did not finish within TIMEOUT milliseconds. */
static char *capture(const char *script, DWORD timeout)
{
    size_t length = strlen(script) + 16;
    char *quiet = malloc(length), *output = NULL;
    WCHAR *line;
    if (!quiet) return NULL;
    snprintf(quiet, length, "(%s) 2>/dev/null", script);
    if ((line = wide(quiet)))
    {
        output = unix_output(line, timeout);
        free(line);
    }
    free(quiet);
    return output;
}

/* One field of nmcli's terse output (":" separated, "\" escaped). */
static void field(char **cursor, char *out, size_t size)
{
    size_t n = 0;
    char *c = *cursor;
    while (*c && *c != ':')
    {
        if (*c == '\\' && c[1]) c++;
        if (n + 1 < size) out[n++] = *c;
        c++;
    }
    out[n] = 0;
    *cursor = *c ? c + 1 : c;
}

/* NetworkManager's SECURITY and key-mgmt words as NDIS's modes. */
static ULONG authentication(const char *security)
{
    if (strstr(security, "802.1X") && strstr(security, "WPA2")) return AUTH_WPA2;
    if (strstr(security, "WPA2") || strstr(security, "WPA3") || strstr(security, "wpa-psk") ||
        strstr(security, "sae"))
        return strstr(security, "wpa-psk") && !strstr(security, "WPA2") ? AUTH_WPA_PSK : AUTH_WPA2_PSK;
    if (strstr(security, "802.1X") || strstr(security, "wpa-eap")) return AUTH_WPA;
    if (strstr(security, "WPA")) return AUTH_WPA_PSK;
    return AUTH_OPEN;
}

static void parse_mac(const char *text, BYTE *mac)
{
    unsigned b[6] = {0};
    sscanf(text, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    for (int i = 0; i < 6; i++) mac[i] = (BYTE)b[i];
}

static void quote(const char *text, char *out, size_t size)
{
    size_t n = 0;
    if (size < 3) return;
    out[n++] = '\'';
    for (; *text && n + 5 < size; text++)
    {
        if (*text == '\'') { memcpy(out + n, "'\\''", 4); n += 4; }
        else out[n++] = *text;
    }
    out[n++] = '\'';
    out[n] = 0;
}

static void description(const WCHAR *ifname, WCHAR *out, size_t size)
{
    char command[256], *text;
    char ifname_utf8[64];
    WideCharToMultiByte(CP_UTF8, 0, ifname, -1, ifname_utf8, sizeof(ifname_utf8), NULL, NULL);
    snprintf(command, sizeof(command), "nmcli -t -g GENERAL.VENDOR,GENERAL.PRODUCT device show %s", ifname_utf8);
    lstrcpynW(out, ifname, (int)size);
    if ((text = capture(command, 5000)))
    {
        char *vendor = strtok(text, "\r\n"), *product = strtok(NULL, "\r\n"), name[256];
        if (vendor && product)
        {
            snprintf(name, sizeof(name), "%s %s", vendor, product);
            MultiByteToWideChar(CP_UTF8, 0, name, -1, out, (int)size);
        }
        free(text);
    }
}

/* The networks in range and the saved ones, read again after 4 seconds.
 * The list is NetworkManager's last scan, read at once; a scan older than 30
 * seconds is made again in the background, as XP's Wireless Zero
 * Configuration scans by itself (nmcli's "--rescan auto" waited for it: about
 * 5 seconds before the dialog showed, past the 8 of capture() at times, and
 * then with no networks). */
static struct adapter *read_adapter(const WCHAR *ifname, BOOL description_only)
{
    static ULONGLONG scanned;
    char command[1024], ifname_utf8[64], *text, *line, *context;
    struct adapter *a = &cache;
    if (!lstrcmpW(a->ifname, ifname) && a->read && GetTickCount64() - a->read < 4000) return a;
    if (lstrcmpW(a->ifname, ifname))
    {
        memset(a, 0, sizeof(*a));
        lstrcpynW(a->ifname, ifname, ARRAYSIZE(a->ifname));
        description(ifname, a->description, ARRAYSIZE(a->description));
    }
    if (description_only) return a;
    WideCharToMultiByte(CP_UTF8, 0, ifname, -1, ifname_utf8, sizeof(ifname_utf8), NULL, NULL);
    a->network_count = a->saved_count = 0;
    a->connected = FALSE;
    if (!scanned || GetTickCount64() - scanned > 30000)
    {
        snprintf(command, sizeof(command), "nmcli device wifi rescan ifname %s >/dev/null 2>&1", ifname_utf8);
        spawn(command);
        scanned = GetTickCount64();
    }
    snprintf(command, sizeof(command),
             "nmcli -t --escape yes -f SSID,BSSID,SIGNAL,SECURITY,IN-USE,FREQ dev wifi list ifname %s --rescan no",
             ifname_utf8);
    if ((text = capture(command, 8000)))
    {
        for (line = strtok_s(text, "\r\n", &context); line && a->network_count < NETWORK_LIMIT;
             line = strtok_s(NULL, "\r\n", &context))
        {
            char ssid[256], bssid[64], signal[16], security[128], in_use[8], frequency[32], *cursor = line;
            struct network *n = &a->networks[a->network_count];
            field(&cursor, ssid, sizeof(ssid));
            field(&cursor, bssid, sizeof(bssid));
            field(&cursor, signal, sizeof(signal));
            field(&cursor, security, sizeof(security));
            field(&cursor, in_use, sizeof(in_use));
            field(&cursor, frequency, sizeof(frequency));
            if (!*ssid || strlen(ssid) > 32) continue;
            memset(n, 0, sizeof(*n));
            strcpy(n->ssid, ssid);
            parse_mac(bssid, n->bssid);
            n->signal = atoi(signal);
            n->frequency = atoi(frequency);
            n->active = *in_use == '*';
            n->privacy = *security && strcmp(security, "--");
            n->authentication = authentication(security);
            if (n->active) a->connected = TRUE;
            a->network_count++;
        }
        free(text);
    }
    /* XP shows a network once, whichever of its access points it reads:
     * all of the one the adapter is on are marked. */
    for (unsigned i = 0; i < a->network_count; i++)
        for (unsigned j = 0; j < a->network_count; j++)
            if (a->networks[j].active && !strcmp(a->networks[i].ssid, a->networks[j].ssid))
                a->networks[i].active = TRUE;
    /* XP's preferred networks: NetworkManager's saved Wi-Fi connections. */
    snprintf(command, sizeof(command),
             "nmcli -t -f UUID,TYPE connection show | while IFS=: read -r uuid type; do "
             "[ \"$type\" = 802-11-wireless ] && printf '%%s\\t%%s\\n' "
             "\"$(nmcli --escape no -g 802-11-wireless.ssid connection show \"$uuid\")\" "
             "\"$(nmcli --escape no -g 802-11-wireless-security.key-mgmt connection show \"$uuid\")\"; done");
    if ((text = capture(command, 8000)))
    {
        for (line = strtok_s(text, "\r\n", &context); line && a->saved_count < NETWORK_LIMIT;
             line = strtok_s(NULL, "\r\n", &context))
        {
            char *tab = strrchr(line, '\t'), ssid[256];
            const char *security = tab ? tab + 1 : "";
            struct network *n = &a->saved[a->saved_count];
            if (tab) *tab = 0;
            lstrcpynA(ssid, line, sizeof(ssid));
            if (!*ssid || strlen(ssid) > 32) continue;
            memset(n, 0, sizeof(*n));
            strcpy(n->ssid, ssid);
            n->privacy = *security != 0;
            n->authentication = authentication(security);
            for (unsigned i = 0; i < a->network_count; i++)
                if (!strcmp(a->networks[i].ssid, ssid) && a->networks[i].signal >= n->signal)
                {
                    memcpy(n->bssid, a->networks[i].bssid, 6);
                    n->signal = a->networks[i].signal;
                    n->frequency = a->networks[i].frequency;
                    n->active |= a->networks[i].active;
                }
            a->saved_count++;
        }
        free(text);
    }
    a->read = GetTickCount64();
    return a;
}

/* ---- XP's structures */

static void fill_config(WZC_WLAN_CONFIG *c, const struct network *n)
{
    memset(c, 0, sizeof(*c));
    c->length = sizeof(*c);
    memcpy(c->mac, n->bssid, 6);
    c->ssid_length = (ULONG)strlen(n->ssid);
    memcpy(c->ssid, n->ssid, c->ssid_length);
    /* NDIS_802_11_WEP_STATUS, which XP keeps there: 1 is unencrypted. */
    c->privacy = !n->privacy ? WEP_DISABLED : n->authentication >= AUTH_WPA2 ? AES_ENABLED :
                 n->authentication >= AUTH_WPA ? TKIP_ENABLED : WEP_ENABLED;
    /* NetworkManager's 0-100 quality as dBm, as XP's bars read them. */
    c->rssi = n->signal ? n->signal / 2 - 100 : -100;
    c->network_type = n->frequency > 4000 ? 2 : 3;  /* Ndis802_11OFDM5, OFDM24 */
    c->configuration.length = 32;
    c->configuration.beacon_period = 100;
    c->configuration.frequency = (ULONG)n->frequency * 1000;
    c->configuration.fh_length = 16;
    c->infrastructure_mode = 1;
    static const BYTE rates[8] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(c->rates, rates, sizeof(rates));
    c->authentication_mode = n->authentication;
    /* The one the adapter is on: the dialog says "Connected". */
    if (n->active) c->control_flags |= WZCCTL_CONNECTED;
}

/* The list's index is the network the adapter is on, as XP's service gives it. */
static RAW_DATA config_list(const struct network *networks, unsigned count)
{
    ULONG index = count;
    for (unsigned i = 0; i < count; i++)
        if (networks[i].active) index = i;
    RAW_DATA raw = {0, NULL};
    DWORD size = sizeof(WZC_802_11_CONFIG_LIST) + count * sizeof(WZC_WLAN_CONFIG);
    WZC_802_11_CONFIG_LIST *list = MIDL_user_allocate(size);
    if (!list) return raw;
    list->count = count;
    list->index = index;
    for (unsigned i = 0; i < count; i++) fill_config(&list->config[i], &networks[i]);
    raw.size = size;
    raw.data = (BYTE *)list;
    return raw;
}

static WCHAR *heap_string(const WCHAR *text)
{
    size_t size = (lstrlenW(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = MIDL_user_allocate(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static RAW_DATA raw_copy(const void *data, DWORD size)
{
    RAW_DATA raw = {0, NULL};
    if ((raw.data = MIDL_user_allocate(size ? size : 1)))
    {
        memcpy(raw.data, data, size);
        raw.size = size;
    }
    return raw;
}

DWORD WINAPI WZCQueryInterface(WCHAR *server, DWORD flags, INTF_ENTRY *entry, DWORD *out_flags)
{
    WCHAR ifname[32];
    struct adapter *a;
    const struct network *current = NULL;
    DWORD given = 0;
    (void)server;
    if (!entry || !entry->guid) return ERROR_INVALID_PARAMETER;
    if (!find_interface(entry->guid, ifname, ARRAYSIZE(ifname))) return ERROR_FILE_NOT_FOUND;
    EnterCriticalSection(&lock);
    a = read_adapter(ifname, !(flags & ~(INTF_ALL_FLAGS | INTF_DESCR)));
    for (unsigned i = 0; i < a->network_count; i++)
        if (a->networks[i].active) current = &a->networks[i];
    if (flags & INTF_ALL_FLAGS)
    {
        entry->control_flags = INTFCTL_ENABLED | INTFCTL_FALLBACK | INTFCTL_OIDSSUPP | INTFCTL_CM_AUTO;
        given |= flags & INTF_ALL_FLAGS;
    }
    if (flags & INTF_DESCR)
    {
        entry->description = heap_string(a->description);
        given |= INTF_DESCR;
    }
    if (flags & INTF_NDISMEDIA)
    {
        entry->media_state = current ? 0 : 1;  /* NdisMediaStateConnected, Disconnected */
        entry->media_type = 0;                 /* NdisMedium802_3 */
        entry->physical_media_type = 1;        /* NdisPhysicalMediumWirelessLan */
        given |= INTF_NDISMEDIA;
    }
    if (flags & INTF_INFRAMODE)
    {
        entry->infrastructure_mode = 1;
        given |= INTF_INFRAMODE;
    }
    if (flags & INTF_AUTHMODE)
    {
        entry->authentication_mode = current ? (INT)current->authentication : AUTH_OPEN;
        given |= INTF_AUTHMODE;
    }
    if (flags & INTF_WEPSTATUS)
    {
        entry->wep_status = !current || !current->privacy ? WEP_DISABLED :
                            current->authentication >= AUTH_WPA2 ? AES_ENABLED :
                            current->authentication >= AUTH_WPA ? TKIP_ENABLED : WEP_ENABLED;
        given |= INTF_WEPSTATUS;
    }
    if ((flags & INTF_SSID) && current)
    {
        entry->ssid = raw_copy(current->ssid, (DWORD)strlen(current->ssid));
        given |= INTF_SSID;
    }
    if ((flags & INTF_BSSID) && current)
    {
        entry->bssid = raw_copy(current->bssid, 6);
        given |= INTF_BSSID;
    }
    if (flags & INTF_BSSIDLIST)
    {
        entry->bssid_list = config_list(a->networks, a->network_count);
        given |= INTF_BSSIDLIST;
    }
    if (flags & INTF_PREFLIST)
    {
        entry->preferred_list = config_list(a->saved, a->saved_count);
        given |= INTF_PREFLIST;
    }
    /* XP SP2's 0x80000 has nothing to fill here; the dialog wants every
     * flag it asked for back. */
    given |= flags & INTF_SP2_EXTRA;
    LeaveCriticalSection(&lock);
    if (out_flags) *out_flags = given;
    return ERROR_SUCCESS;
}

static void hex(const BYTE *data, ULONG size, char *out)
{
    for (ULONG i = 0; i < size; i++) sprintf(out + 2 * i, "%02x", data[i]);
    out[2 * size] = 0;
}

/* XP's dialog connects by putting the network in the preferred list, with
 * the list's index on it: NetworkManager joins it (a new one with the key
 * the dialog asked for, 64 hexadecimal digits for WPA's PSK). It
 * disconnects by making the current one "on demand", with no index. */
DWORD WINAPI WZCSetInterface(WCHAR *server, DWORD flags, INTF_ENTRY *entry, DWORD *out_flags)
{
    WCHAR ifname[32];
    char ifname_utf8[64], ssid[33], quoted[80], key[80], command[512];
    WZC_802_11_CONFIG_LIST *list;
    WZC_WLAN_CONFIG *c;
    (void)server;
    if (out_flags) *out_flags = 0;
    if (!entry || !entry->guid) return ERROR_INVALID_PARAMETER;
    if (!find_interface(entry->guid, ifname, ARRAYSIZE(ifname))) return ERROR_FILE_NOT_FOUND;
    if (out_flags) *out_flags = flags;
    if (!(flags & INTF_PREFLIST) || !entry->preferred_list.data ||
        entry->preferred_list.size < sizeof(WZC_802_11_CONFIG_LIST))
        return ERROR_SUCCESS;
    list = (WZC_802_11_CONFIG_LIST *)entry->preferred_list.data;
    if (entry->preferred_list.size < sizeof(*list) + list->count * sizeof(WZC_WLAN_CONFIG)) return ERROR_SUCCESS;
    WideCharToMultiByte(CP_UTF8, 0, ifname, -1, ifname_utf8, sizeof(ifname_utf8), NULL, NULL);
    if (list->index >= list->count)
    {
        /* "Disconnect": the network the adapter is on, now on demand only. */
        for (ULONG i = 0; i < list->count; i++)
        {
            c = &list->config[i];
            if ((c->control_flags & (WZCCTL_ON_DEMAND | WZCCTL_CONNECTED)) == (WZCCTL_ON_DEMAND | WZCCTL_CONNECTED))
            {
                snprintf(command, sizeof(command), "nmcli device disconnect %s >/dev/null 2>&1", ifname_utf8);
                spawn(command);
                break;
            }
        }
        EnterCriticalSection(&lock);
        cache.read = 0;
        LeaveCriticalSection(&lock);
        return ERROR_SUCCESS;
    }
    c = &list->config[list->index];
    if (!c->ssid_length || c->ssid_length > 32) return ERROR_SUCCESS;
    memcpy(ssid, c->ssid, c->ssid_length);
    ssid[c->ssid_length] = 0;
    quote(ssid, quoted, sizeof(quoted));
    if (c->key_length && c->key_length <= 32)
    {
        hex(c->key, c->key_length, key);
        snprintf(command, sizeof(command),
                 "nmcli --wait 30 device wifi connect %s password %s %s ifname %s >/dev/null 2>&1", quoted, key,
                 c->authentication_mode >= AUTH_WPA ? "" : "wep-key-type key", ifname_utf8);
    }
    else
        snprintf(command, sizeof(command), "nmcli --wait 30 device wifi connect %s ifname %s >/dev/null 2>&1",
                 quoted, ifname_utf8);
    spawn(command);
    EnterCriticalSection(&lock);
    cache.read = 0;
    LeaveCriticalSection(&lock);
    return ERROR_SUCCESS;
}

/* "Refresh network list": NetworkManager scans again. */
DWORD WINAPI WZCRefreshInterface(WCHAR *server, DWORD flags, INTF_ENTRY *entry, DWORD *out_flags)
{
    WCHAR ifname[32];
    char ifname_utf8[64], command[128];
    (void)server;
    if (out_flags) *out_flags = flags;
    if (!entry || !entry->guid || !find_interface(entry->guid, ifname, ARRAYSIZE(ifname)))
        return ERROR_FILE_NOT_FOUND;
    if (flags & INTF_LIST_SCAN)
    {
        WideCharToMultiByte(CP_UTF8, 0, ifname, -1, ifname_utf8, sizeof(ifname_utf8), NULL, NULL);
        snprintf(command, sizeof(command), "nmcli device wifi rescan ifname %s >/dev/null 2>&1", ifname_utf8);
        spawn(command);
    }
    EnterCriticalSection(&lock);
    cache.read = 0;
    LeaveCriticalSection(&lock);
    return ERROR_SUCCESS;
}

/* What WZCQueryInterface allocated in ENTRY. */
VOID WINAPI WZCDeleteIntfObj(INTF_ENTRY *entry)
{
    if (!entry) return;
    MIDL_user_free(entry->guid);
    MIDL_user_free(entry->description);
    MIDL_user_free(entry->reserved3.data);
    MIDL_user_free(entry->ssid.data);
    MIDL_user_free(entry->bssid.data);
    MIDL_user_free(entry->bssid_list.data);
    MIDL_user_free(entry->preferred_list.data);
    MIDL_user_free(entry->control_data.data);
    memset(entry, 0, sizeof(*entry));
}

DWORD WINAPI WZCEnumInterfaces(WCHAR *server, INTFS_KEY_TABLE *table)
{
    WIN32_FIND_DATAW found;
    HANDLE find;
    (void)server;
    if (!table) return ERROR_INVALID_PARAMETER;
    table->count = 0;
    if (!(table->guids = MIDL_user_allocate(16 * sizeof(WCHAR *)))) return ERROR_NOT_ENOUGH_MEMORY;
    if ((find = FindFirstFileW(L"Z:\\sys\\class\\net\\*", &found)) == INVALID_HANDLE_VALUE) return ERROR_SUCCESS;
    do
    {
        WCHAR text[40];
        GUID id;
        if (found.cFileName[0] == '.' || !is_wireless(found.cFileName)) continue;
        id = link_guid(found.cFileName);
        StringFromGUID2(&id, text, ARRAYSIZE(text));
        table->guids[table->count++] = heap_string(text);
    } while (table->count < 16 && FindNextFileW(find, &found));
    FindClose(find);
    return ERROR_SUCCESS;
}

/* XP's resource DLLs of its service packs (1 to 3) and of the dialogs. */
static HMODULE load_module(const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    if (!ExpandEnvironmentStringsW(name, path, MAX_PATH)) return NULL;
    return LoadLibraryExW(path, NULL, 0);
}

HMODULE WINAPI WZCGetSPResModule(DWORD pack)
{
    static HMODULE modules[3];
    static const WCHAR *const names[3] = {L"%SystemRoot%\\system32\\xpsp1res.dll",
                                          L"%SystemRoot%\\system32\\xpsp2res.dll",
                                          L"%SystemRoot%\\system32\\xpsp3res.dll"};
    if (pack < 1 || pack > 3) return NULL;
    if (!modules[pack - 1]) modules[pack - 1] = load_module(names[pack - 1]);
    return modules[pack - 1];
}

HMODULE WINAPI WZCGetDlgResModule(void)
{
    static HMODULE module;
    if (!module) module = load_module(L"%SystemRoot%\\system32\\wzcdlg.dll");
    return module;
}

/* WZCPassword2Key(WZC_WLAN_CONFIG *, const char *): WPA's PSK from the
 * passphrase and the SSID (PBKDF2-HMAC-SHA1, 4096 rounds, 32 bytes). */
static void sha1(const BYTE *data, size_t size, BYTE out[20]);

static void hmac_sha1(const BYTE *key, size_t key_size, const BYTE *data, size_t size, BYTE out[20])
{
    BYTE pad[64], inner[64 + 64 + 4], hash[20], outer[64 + 20];
    memset(pad, 0, sizeof(pad));
    memcpy(pad, key, key_size > 64 ? 64 : key_size);
    for (int i = 0; i < 64; i++) inner[i] = pad[i] ^ 0x36;
    memcpy(inner + 64, data, size);
    sha1(inner, 64 + size, hash);
    for (int i = 0; i < 64; i++) outer[i] = pad[i] ^ 0x5c;
    memcpy(outer + 64, hash, 20);
    sha1(outer, sizeof(outer), out);
}

static void sha1(const BYTE *data, size_t size, BYTE out[20])
{
    DWORD h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    BYTE block[64];
    size_t total = ((size + 8) / 64 + 1) * 64;
    for (size_t offset = 0; offset < total; offset += 64)
    {
        DWORD w[80], a, b, c, d, e;
        for (int i = 0; i < 64; i++)
        {
            size_t at = offset + i;
            block[i] = at < size ? data[at] : at == size ? 0x80 : 0;
        }
        if (offset + 64 == total)
            for (int i = 0; i < 8; i++) block[63 - i] = (BYTE)((unsigned long long)size * 8 >> (8 * i));
        for (int i = 0; i < 16; i++)
            w[i] = (DWORD)block[4 * i] << 24 | (DWORD)block[4 * i + 1] << 16 | (DWORD)block[4 * i + 2] << 8 |
                   block[4 * i + 3];
        for (int i = 16; i < 80; i++)
        {
            DWORD x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = x << 1 | x >> 31;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
        for (int i = 0; i < 80; i++)
        {
            DWORD f, k, t;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            t = (a << 5 | a >> 27) + f + e + k + w[i];
            e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 20; i++) out[i] = (BYTE)(h[i / 4] >> (24 - 8 * (i % 4)));
}

VOID WINAPI WZCPassword2Key(WZC_WLAN_CONFIG *config, const char *password)
{
    BYTE salt[36], u[20], t[20], key[40];
    size_t ssid_length = config->ssid_length > 32 ? 32 : config->ssid_length;
    memcpy(salt, config->ssid, ssid_length);
    for (DWORD block = 1; block <= 2; block++)
    {
        salt[ssid_length] = 0; salt[ssid_length + 1] = 0; salt[ssid_length + 2] = 0;
        salt[ssid_length + 3] = (BYTE)block;
        hmac_sha1((const BYTE *)password, strlen(password), salt, ssid_length + 4, u);
        memcpy(t, u, 20);
        for (int i = 1; i < 4096; i++)
        {
            hmac_sha1((const BYTE *)password, strlen(password), u, 20, u);
            for (int j = 0; j < 20; j++) t[j] ^= u[j];
        }
        memcpy(key + 20 * (block - 1), t, 20);
    }
    memcpy(config->key, key, 32);
    config->key_length = 32;
}

/* ---- What Linux has nothing for: EAP methods and 802.1X state, the
 * service's log and the adapter's OIDs, as when the service is stopped. */

#define STOPPED 1062  /* ERROR_SERVICE_NOT_ACTIVE */

/* An empty DTLLIST of EAP methods {first, last, count, id}. */
void *WINAPI ReadEapcfgList(DWORD flags)
{
    (void)flags;
    return MIDL_user_allocate(16);
}

VOID WINAPI DtlDestroyList(void *list, void *destroy)
{
    (void)destroy;
    MIDL_user_free(list);
}

void *WINAPI EapcfgNodeFromKey(void *list, DWORD key)
{
    (void)list; (void)key;
    return NULL;
}

VOID WINAPI DestroyEapcfgNode(void *node, void *a, void *b) { (void)node; (void)a; (void)b; }
void *WINAPI CreateEapcfgNode(void *a, void *b, void *c) { (void)a; (void)b; (void)c; return NULL; }

#pragma GCC diagnostic ignored "-Wunused-parameter"
#define STUB(name, ...) DWORD WINAPI name(__VA_ARGS__) { return STOPPED; }
STUB(WZCEapolGetCustomAuthData, void *a, void *b, void *c, void *d, void *e, void *f, void *g)
STUB(WZCEapolSetCustomAuthData, void *a, void *b, void *c, void *d, void *e, void *f, void *g)
STUB(WZCEapolQueryState, void *a, void *b, void *c)
STUB(WZCEapolReAuthenticate, void *a, void *b)
STUB(WZCEapolGetPMKCacheInfo, void *a, void *b, void *c, void *d, void *e)
STUB(WZCGetEapUserInfo, void *a, void *b, void *c, void *d, void *e, void *f, void *g)
STUB(WZCProviderCreateConnectionProperties, void *a, void *b, void *c, void *d, void *e)
STUB(WZCProviderCreateUserProperties, void *a, void *b, void *c, void *d, void *e)
STUB(WZCProviderCreateWirelessProfile, void *a, void *b, void *c, void *d, void *e)
STUB(WZCProviderDeleteWirelessProfile, void *a, void *b, void *c, void *d, void *e)
STUB(WZCQueryContext, void *a, void *b, void *c, void *d)
STUB(WZCSetContext, void *a, void *b, void *c, void *d)
STUB(WZCGetAPIVersion, void *a, void *b, void *c)
STUB(GetSSIDForAdapter, void *a, void *b, void *c, void *d, void *e)
STUB(GetEncryptionForAdapter, void *a, void *b, void *c, void *d, void *e)
STUB(GetSignalStrengthForAdapter, void *a, void *b, void *c, void *d, void *e, void *f)
STUB(GetModeForAdapter, void *a, void *b, void *c, void *d, void *e, void *f)
STUB(OpenWZCDbLogSession, void *a, void *b, void *c)
STUB(CloseWZCDbLogSession, void *a)
STUB(EnumWZCDbLogRecords, void *a, void *b, void *c, void *d, void *e, void *f, void *g)
STUB(FlushWZCDbLog, void *a)
STUB(GetSpecificLogRecord, void *a, void *b, void *c, void *d, void *e)

/* A network's 802.1X parameters (server, adapter GUID, SSID length, SSID,
 * EAPOL_INTF_PARAMS): the dialog lists a network only once it has them. The
 * dialog's defaults stay (802.1X off for the networks Linux joins), and a
 * change is accepted: NetworkManager keeps its own. */
DWORD WINAPI WZCEapolGetInterfaceParams(void *server, WCHAR *guid, DWORD ssid_length, BYTE *ssid, void *params)
{
    return ERROR_SUCCESS;
}

DWORD WINAPI WZCEapolSetInterfaceParams(void *server, WCHAR *guid, DWORD ssid_length, BYTE *ssid, void *params)
{
    return ERROR_SUCCESS;
}

/* WZCEapolUIResponse takes an EAPOL_EAP_UI_CONTEXT by value. */
struct eap_ui_context
{
    BYTE data[656];
};

DWORD WINAPI WZCEapolUIResponse(void *server, struct eap_ui_context context, void *response)
{
    (void)server; (void)context; (void)response;
    return STOPPED;
}

VOID WINAPI WZCEapolFreeState(void *state) { MIDL_user_free(state); }

/* netshell's list of connections, which the dialog reads the state of the
 * connection from ("Connected"): the Network Connections folder fills it. */
static void read_connections(void)
{
    IShellFolder *desktop, *folder;
    IEnumIDList *items;
    ITEMIDLIST *pidl;
    WCHAR path[] = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}"
                   L"\\::{7007ACC7-3202-11D1-AAD2-00805FC1270E}";
    if (FAILED(SHGetDesktopFolder(&desktop))) return;
    if (SUCCEEDED(IShellFolder_ParseDisplayName(desktop, NULL, NULL, path, NULL, &pidl, NULL)))
    {
        if (SUCCEEDED(IShellFolder_BindToObject(desktop, pidl, NULL, &IID_IShellFolder, (void **)&folder)))
        {
            if (IShellFolder_EnumObjects(folder, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items) == S_OK)
            {
                ITEMIDLIST *item;
                while (IEnumIDList_Next(items, 1, &item, NULL) == S_OK) CoTaskMemFree(item);
                IEnumIDList_Release(items);
            }
            IShellFolder_Release(folder);
        }
        CoTaskMemFree(pidl);
    }
    IShellFolder_Release(desktop);
}

/* rundll32 wzcsapi.dll,ViewAvailableNetworks: XP's "Choose a wireless
 * network" for the first wireless adapter, as netshell.dll opens it
 * (WZCOnBalloonClick with its "view available networks" request), from the
 * notification area's icon and the Start menu (xptray.exe). */
void CALLBACK ViewAvailableNetworks(HWND window, HINSTANCE instance, char *arguments, int show)
{
    HRESULT (WINAPI *open)(const GUID *, const WCHAR *, DWORD *);
    HRESULT (WINAPI *running)(const GUID *, HWND *);
    DWORD request[2] = {0x11001, 1};
    WCHAR ifname[32], name[128];
    HMODULE dialogs, texts;
    HWND found;
    GUID id;
    MSG message;
    (void)window; (void)instance; (void)arguments; (void)show;
    if (!find_interface(NULL, ifname, ARRAYSIZE(ifname))) return;
    id = link_guid(ifname);
    if (!(dialogs = WZCGetDlgResModule())) return;
    open = (void *)GetProcAddress(dialogs, "WZCOnBalloonClick");
    running = (void *)GetProcAddress(dialogs, "WZCIsVanRunning");
    if (!open || !running) return;
    if (running(&id, &found) == S_OK)
    {
        SetForegroundWindow(found);
        return;
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    read_connections();
    texts = WZCGetSPResModule(2);
    if (!texts || !LoadStringW(texts, 16022, name, ARRAYSIZE(name))) lstrcpyW(name, L"Wireless Network Connection");
    if (FAILED(open(&id, name, request))) return;
    /* The dialog lives in this process: until it closes. */
    for (;;)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (running(&id, NULL) != S_OK) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 500, QS_ALLINPUT);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)instance; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) InitializeCriticalSection(&lock);
    return TRUE;
}
