/* The Windows Audio services (AudioSrv, AudioEndpointBuilder) as Windows 7's
 * notification area expects them: SndVolSSO.dll and SndVol.exe show the
 * speaker crossed out ("The Audio Service is not running") unless the
 * service manager says they run. Wine's mmdevapi does their work in the
 * programs themselves, so these only run.
 * Usage: audiosrv.exe (as a service of either name). */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static SERVICE_STATUS_HANDLE handle;
static SERVICE_STATUS status = { SERVICE_WIN32_SHARE_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP, 0, 0, 0, 0 };
static HANDLE stop_event;
static LONG running;

static DWORD WINAPI control(DWORD code, DWORD type, void *data, void *context)
{
    (void)type;
    (void)data;
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN)
    {
        status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus((SERVICE_STATUS_HANDLE)context, &status);
        if (!InterlockedDecrement(&running)) SetEvent(stop_event);
        return NO_ERROR;
    }
    return code == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, WCHAR **argv)
{
    SERVICE_STATUS_HANDLE service;
    (void)argc;
    InterlockedIncrement(&running);
    service = RegisterServiceCtrlHandlerExW(argv[0], control, NULL);
    /* the handler needs its own handle back */
    RegisterServiceCtrlHandlerExW(argv[0], control, (void *)service);
    handle = service;
    SetServiceStatus(service, &status);
}

int wmain(void)
{
    SERVICE_TABLE_ENTRYW table[] = {
        { (WCHAR *)L"AudioSrv", service_main },
        { (WCHAR *)L"AudioEndpointBuilder", service_main },
        { NULL, NULL },
    };
    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    return !StartServiceCtrlDispatcherW(table);
}
