/* Starts XP Explorer as the shell of the Wine virtual desktop, as XP's
 * winlogon did, and restarts it if it crashes.
 *
 * - XP Explorer waits up to 60 s for Global\ScmCreatedEvent before it builds
 *   the desktop; Wine's service manager never sets it (win4wine patch 0012).
 *   While Explorer waits, this loads xpcompat.dll into it, then sets the event.
 * - Wine sends no shell hook messages, so XP's taskbar would list no windows.
 *   As win4wine's proxy does (bridge-wm/proxy.c), this follows the top-level
 *   windows and posts SHELLHOOK create, destroy, activate and redraw messages
 *   to the windows that registered for them (Wine patch 0026 marks them).
 * - XP's startup sound plays when the session opens and its logoff sound when
 *   it ends (the sound scheme's SystemStart and WindowsLogoff events).
 * - When the user logs off or turns off the computer (Explorer calls
 *   ExitWindowsEx, which sends WM_QUERYENDSESSION to every window, this
 *   helper's hidden one included), the Wine session ends and the virtual
 *   desktop window closes. Explorer ending in any other way, even with
 *   exit code 0, is restarted like a crash. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

/* ---- Shell hook notifications (after win4wine's bridge-wm/proxy.c) ---- */
#define MAX_TASKS 256
static UINT shell_message;
static HWND previous[MAX_TASKS], current[MAX_TASKS], last_foreground;
static WCHAR previous_titles[MAX_TASKS][256], current_titles[MAX_TASKS][256];
static unsigned previous_count, current_count;

struct notification { WPARAM event; LPARAM window; };

static BOOL CALLBACK deliver(HWND hwnd, LPARAM parameter)
{
    struct notification *n = (struct notification *)parameter;
    if (GetPropW(hwnd, L"Win4WineShellHook")) PostMessageW(hwnd, shell_message, n->event, n->window);
    return TRUE;
}

static BOOL CALLBACK deliver_tree(HWND hwnd, LPARAM parameter)
{
    deliver(hwnd, parameter);
    /* XP registers its child task-switcher window, not the taskbar parent. */
    EnumChildWindows(hwnd, deliver, parameter);
    return TRUE;
}

static void notify(UINT event, HWND window)
{
    struct notification n = {event, (LPARAM)window};
    EnumWindows(deliver_tree, (LPARAM)&n);
}

/* The windows XP shows as tasks: visible, unowned, not tool windows. */
static BOOL CALLBACK collect(HWND hwnd, LPARAM unused)
{
    WCHAR name[80];
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    (void)unused;
    if (!IsWindowVisible(hwnd) || ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW))) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return TRUE;
    GetClassNameW(hwnd, name, 80);
    if (!_wcsicmp(name, L"Progman") || !_wcsicmp(name, L"Shell_TrayWnd") || !_wcsicmp(name, L"WorkerW")
        || !_wcsicmp(name, L"DV2ControlHost")) return TRUE;
    if (current_count < MAX_TASKS)
    {
        current[current_count] = hwnd;
        GetWindowTextW(hwnd, current_titles[current_count], 256);
        current_count++;
    }
    return TRUE;
}

static BOOL contains(HWND *list, unsigned count, HWND hwnd)
{
    for (unsigned i = 0; i < count; i++) if (list[i] == hwnd) return TRUE;
    return FALSE;
}

static void shell_events(void)
{
    HWND foreground;
    current_count = 0;
    EnumWindows(collect, 0);
    for (unsigned i = 0; i < current_count; i++)
    {
        unsigned j;
        for (j = 0; j < previous_count && previous[j] != current[i]; j++) {}
        if (j == previous_count) notify(HSHELL_WINDOWCREATED, current[i]);
        else if (wcscmp(previous_titles[j], current_titles[i])) notify(HSHELL_REDRAW, current[i]);
    }
    for (unsigned i = 0; i < previous_count; i++)
        if (!contains(current, current_count, previous[i])) notify(HSHELL_WINDOWDESTROYED, previous[i]);
    /* Owned dialogs highlight their application's task; a minimized window
     * is no longer the active task, as on XP. */
    foreground = GetForegroundWindow();
    if (foreground && IsIconic(foreground)) foreground = NULL;
    for (unsigned depth = 0; foreground && depth < MAX_TASKS && !contains(current, current_count, foreground); depth++)
        foreground = GetWindow(foreground, GW_OWNER);
    if (foreground != last_foreground)
    {
        notify(HSHELL_WINDOWACTIVATED, foreground);
        last_foreground = foreground;
    }
    memcpy(previous, current, current_count * sizeof(HWND));
    memcpy(previous_titles, current_titles, current_count * sizeof(current_titles[0]));
    previous_count = current_count;
}

static void load_compat(HANDLE process)
{
    static const WCHAR path[] = L"C:\\xp\\bin\\xpcompat.dll";
    HMODULE local = LoadLibraryW(path);
    FARPROC init = local ? GetProcAddress(local, "xpcompat_init") : NULL;
    FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    void *remote_path;
    HANDLE thread;
    DWORD remote_base = 0;
    if (!init) init = local ? GetProcAddress(local, "xpcompat_init") : NULL;
    if (!init || !load) return;
    /* kernel32 is mapped at the same address in every process of the prefix. */
    if (!(remote_path = VirtualAllocEx(process, NULL, sizeof(path), MEM_COMMIT, PAGE_READWRITE))) return;
    WriteProcessMemory(process, remote_path, path, sizeof(path), NULL);
    if ((thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)load, remote_path, 0, NULL)))
    {
        WaitForSingleObject(thread, 10000);
        GetExitCodeThread(thread, &remote_base);
        CloseHandle(thread);
    }
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (!remote_base) return;
    init = (FARPROC)((BYTE *)init - (BYTE *)local + (ULONG_PTR)remote_base);
    if ((thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)init, NULL, 0, NULL)))
    {
        WaitForSingleObject(thread, 10000);
        CloseHandle(thread);
    }
}

/* XP's tools (Display Properties' desktop items, for one) write the icons of
 * My Computer and the other desktop items per user as explorer.exe,N, which
 * here is Wine's explorer, without XP's icons: point them at XP Explorer. */
static void fix_icon_value(HKEY key, const WCHAR *name)
{
    static const WCHAR *const olds[] = {L"%SystemRoot%\\explorer.exe", L"C:\\windows\\explorer.exe", L"explorer.exe"};
    WCHAR value[MAX_PATH], fixed[MAX_PATH];
    DWORD size = sizeof(value), type;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)value, &size) || (type != REG_SZ && type != REG_EXPAND_SZ)) return;
    value[ARRAYSIZE(value) - 1] = 0;
    for (unsigned i = 0; i < ARRAYSIZE(olds); i++)
    {
        size_t length = wcslen(olds[i]);
        if (_wcsnicmp(value, olds[i], length) || (value[length] && value[length] != ',')) continue;
        swprintf(fixed, ARRAYSIZE(fixed), L"C:\\xp\\xp-explorer.exe%ls", value + length);
        RegSetValueExW(key, name, 0, REG_SZ, (BYTE *)fixed, (wcslen(fixed) + 1) * sizeof(WCHAR));
        return;
    }
}

static void fix_desktop_icons(void)
{
    HKEY classes, icon;
    WCHAR name[64], path[128];
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID",
                      0, KEY_READ, &classes)) return;
    for (DWORD i = 0, length = ARRAYSIZE(name); !RegEnumKeyExW(classes, i, name, &length, NULL, NULL, NULL, NULL);
         i++, length = ARRAYSIZE(name))
    {
        swprintf(path, ARRAYSIZE(path), L"%ls\\DefaultIcon", name);
        if (RegOpenKeyExW(classes, path, 0, KEY_READ | KEY_SET_VALUE, &icon)) continue;
        fix_icon_value(icon, NULL);
        fix_icon_value(icon, L"Full");
        fix_icon_value(icon, L"Empty");
        RegCloseKey(icon);
    }
    RegCloseKey(classes);
}

static BOOL ending;

/* C:\xp\xpstart.log says why each session ended (Explorer's exit code, a
 * log off request and who asked for it). */
static void note(const char *format, ...)
{
    FILE *log = fopen("C:\\xp\\xpstart.log", "a");
    SYSTEMTIME now;
    va_list args;
    if (!log) return;
    GetLocalTime(&now);
    fprintf(log, "%02u:%02u:%02u ", now.wHour, now.wMinute, now.wSecond);
    va_start(args, format);
    vfprintf(log, format, args);
    va_end(args);
    fputc('\n', log);
    fclose(log);
}

static LRESULT CALLBACK session_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_QUERYENDSESSION) note("WM_QUERYENDSESSION (lparam %#lx)", (unsigned long)lparam);
    if (message == WM_QUERYENDSESSION) ending = TRUE;
    if (message == WM_ENDSESSION && !wparam) ending = FALSE;
    return message == WM_QUERYENDSESSION ? TRUE : DefWindowProcW(hwnd, message, wparam, lparam);
}

/* Waits for Explorer to end while delivering shell hook notifications and
 * this thread's messages (WM_QUERYENDSESSION). */
static void wait_for(HANDLE process)
{
    static const DWORD redraws[] = {3000, 6000, 10000};
    DWORD next = GetTickCount(), started = next;
    unsigned redrawn = 0;
    for (;;)
    {
        DWORD now = GetTickCount(), timeout = (int)(next - now) > 0 ? next - now : 0;
        DWORD result = MsgWaitForMultipleObjects(1, &process, FALSE, timeout, QS_ALLINPUT);
        MSG msg;
        if (result == WAIT_OBJECT_0) return;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if ((int)(GetTickCount() - next) >= 0)
        {
            shell_events();
            next = GetTickCount() + 250;
        }
        /* The taskbar's Start button sometimes misses its first paint while the
         * desktop window still covers the taskbar on the X side: paint it again. */
        if (redrawn < ARRAYSIZE(redraws) && GetTickCount() - started >= redraws[redrawn])
        {
            HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
            if (tray) RedrawWindow(tray, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            redrawn++;
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command_line, int show)
{
    WNDCLASSW session_class = {.lpfnWndProc = session_proc, .hInstance = instance, .lpszClassName = L"XPStartSession"};
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, L"Global\\ScmCreatedEvent");
    shell_message = RegisterWindowMessageW(L"SHELLHOOK");
    DWORD started[3] = {0};
    WCHAR flag[4];
    (void)previous; (void)command_line; (void)show;
    RegisterClassW(&session_class);
    /* A top-level window (hidden): ExitWindowsEx only asks top-level windows. */
    CreateWindowExW(WS_EX_TOOLWINDOW, L"XPStartSession", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);

    PlaySoundW(L"SystemStart", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
    for (unsigned crashes = 0;; crashes++)
    {
        STARTUPINFOW startup = {.cb = sizeof(startup)};
        PROCESS_INFORMATION process;
        WCHAR line[] = L"C:\\xp\\xp-explorer.exe";

        ResetEvent(ready);
        fix_desktop_icons();
        if (!CreateProcessW(line, line, NULL, NULL, FALSE, 0, NULL, L"C:\\xp", &startup, &process))
        {
            MessageBoxW(NULL, L"C:\\xp\\xp-explorer.exe could not be started.", L"XP Explorer", MB_ICONERROR);
            return 1;
        }
        /* Let Explorer reach its wait for the event, then fix it up. */
        WaitForInputIdle(process.hProcess, 1500);
        Sleep(500);
        /* win4wine's Wine fixes all of this itself (xp.sh sets XPEXPLORER_COMPAT=0). */
        if (!GetEnvironmentVariableW(L"XPEXPLORER_COMPAT", flag, 4) || wcscmp(flag, L"0"))
            load_compat(process.hProcess);
        SetEvent(ready);
        previous_count = 0;
        last_foreground = NULL;
        note("Explorer started (pid %lu)", process.dwProcessId);
        wait_for(process.hProcess);
        {
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            note("Explorer ended with code %#lx%s", code, ending ? " after a log off request" : ", restarting it");
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (ending) break;
        /* Restart as winlogon does, but give up after three crashes in a minute. */
        started[crashes % 3] = GetTickCount();
        if (crashes >= 2 && GetTickCount() - started[(crashes + 1) % 3] < 60000)
        {
            MessageBoxW(NULL, L"XP Explorer keeps crashing. See the terminal for Wine's messages.",
                        L"XP Explorer", MB_ICONERROR);
            return 1;
        }
    }
    PlaySoundW(L"WindowsLogoff", NULL, SND_ALIAS | SND_SYNC | SND_NODEFAULT);
    ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, 0);
    return 0;
}
