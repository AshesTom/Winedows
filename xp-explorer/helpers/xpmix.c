/* XP's Volume Control (sndvol32.exe) on the Linux sound server.
 *
 * Wine's winmm mixer sets the volume of the calling process's own audio
 * session only, so XP's sndvol32 would move sliders that change nothing.
 * xptray.exe runs a copy of sndvol32 whose import of WINMM.dll names this
 * DLL instead; it passes everything to winmm but the mixer's volume and mute
 * controls, which it reads and sets on PipeWire / PulseAudio's default sink
 * (the first playback mixer) and default source (the first recording mixer)
 * with pactl, or wpctl without it. Changes made elsewhere (keyboard keys,
 * the notification area's slider) reach sndvol32 as XP's drivers sent them,
 * MM_MIXM_CONTROL_CHANGE to its callback window.
 *
 * Playback shows the one Volume Control column: Linux has no Wave, SW Synth
 * or CD Player lines, and Wine's only source line changes nothing. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "unixshell.h"

#define CHANNELS 8
#define OPEN_LIMIT 16
/* sndvol32's strings: "Volume Control", "Recording", "Microphone" */
#define IDS_VOLUME_CONTROL 0xcb
#define IDS_RECORDING 0xce
#define IDS_MICROPHONE 0xd9

enum { SINK, SOURCE, TARGETS, NONE = TARGETS };

typedef struct
{
    const WCHAR *pactl, *wpctl;  /* the device in each tool's words */
    const WCHAR *pactl_kind;     /* sink or source */
    DWORD raw[CHANNELS];         /* PulseAudio's volumes, 65536 = 100% */
    unsigned count;
    BOOL muted, loaded;
    /* What sndvol32 set last and the worker has not yet passed on. */
    DWORD wanted_raw[CHANNELS];
    unsigned wanted_count;
    int wanted_mute;             /* -1: none */
    BOOL wanted_volume;
    ULONGLONG set_at;
} TARGET;

static TARGET targets[TARGETS] = {
    {L"@DEFAULT_SINK@", L"@DEFAULT_AUDIO_SINK@", L"sink", .wanted_mute = -1},
    {L"@DEFAULT_SOURCE@", L"@DEFAULT_AUDIO_SOURCE@", L"source", .wanted_mute = -1},
};

typedef struct { HMIXER mixer; HWND callback; unsigned target; } OPENED;

static OPENED opened[OPEN_LIMIT];
static CRITICAL_SECTION lock;
static HANDLE wake;
static HANDLE thread;
static BOOL pactl_checked, have_pactl;

static BOOL use_pactl(void)
{
    char out[64];
    if (!pactl_checked)
    {
        unix_capture(L"command -v pactl >/dev/null && echo pactl", out, sizeof(out));
        have_pactl = strstr(out, "pactl") != NULL;
        pactl_checked = TRUE;
    }
    return have_pactl;
}

/* ---- The sound server's state ----------------------------------------- */

/* Reads a target's volume and mute; FALSE when it did not change. */
static BOOL refresh(unsigned index)
{
    TARGET *t = &targets[index];
    WCHAR script[256];
    char out[1024], *p, *end;
    DWORD raw[CHANNELS];
    unsigned count = 0;
    BOOL muted, changed;
    if (use_pactl())
        swprintf(script, ARRAYSIZE(script), L"pactl get-%ls-volume %ls; pactl get-%ls-mute %ls",
                 t->pactl_kind, t->pactl, t->pactl_kind, t->pactl);
    else
        swprintf(script, ARRAYSIZE(script), L"wpctl get-volume %ls", t->wpctl);
    if (!unix_capture(script, out, sizeof(out)) || !(p = strstr(out, "Volume:"))) return FALSE;
    p += 7;
    if (!(end = strchr(p, '\n'))) end = p + strlen(p);
    if (strstr(out, "Mute:"))
    {
        /* "Volume: front-left: 26214 /  40% / -23.88 dB,   front-right: ..." */
        char *colon;
        while (count < CHANNELS && (colon = strchr(p, ':')) && colon < end)
        {
            char *after;
            long value = strtol(colon + 1, &after, 10);
            while (*after == ' ') after++;
            if (after != colon + 1 && *after == '/') raw[count++] = (DWORD)max(value, 0);
            p = colon + 1;
        }
        muted = strstr(out, "Mute: yes") != NULL;
    }
    else
    {
        /* wpctl: "Volume: 0.40 [MUTED]" */
        raw[count++] = (DWORD)max(atof(p) * 65536 + 0.5, 0);
        muted = strstr(out, "[MUTED]") != NULL;
    }
    if (!count) return FALSE;
    EnterCriticalSection(&lock);
    /* What sndvol32 just set wins over a reading taken before it applied. */
    if (t->wanted_volume || t->wanted_mute >= 0 || GetTickCount64() - t->set_at < 600)
    {
        LeaveCriticalSection(&lock);
        return FALSE;
    }
    changed = !t->loaded || count != t->count || muted != t->muted ||
              memcmp(raw, t->raw, count * sizeof(*raw));
    memcpy(t->raw, raw, sizeof(raw));
    t->count = count;
    t->muted = muted;
    t->loaded = TRUE;
    LeaveCriticalSection(&lock);
    return changed;
}

static void apply(unsigned index)
{
    TARGET *t = &targets[index];
    WCHAR script[512];
    DWORD raw[CHANNELS];
    unsigned count;
    int mute;
    BOOL volume;
    EnterCriticalSection(&lock);
    volume = t->wanted_volume;
    count = t->wanted_count;
    memcpy(raw, t->wanted_raw, sizeof(raw));
    mute = t->wanted_mute;
    t->wanted_volume = FALSE;
    t->wanted_mute = -1;
    t->set_at = GetTickCount64();
    LeaveCriticalSection(&lock);
    if (volume)
    {
        if (use_pactl())
        {
            int length = swprintf(script, ARRAYSIZE(script), L"pactl set-%ls-volume %ls", t->pactl_kind, t->pactl);
            for (unsigned i = 0; i < count; i++)
                length += swprintf(script + length, ARRAYSIZE(script) - length, L" %lu", raw[i]);
        }
        else
        {
            DWORD loudest = 0;
            for (unsigned i = 0; i < count; i++) loudest = max(loudest, raw[i]);
            swprintf(script, ARRAYSIZE(script), L"wpctl set-volume %ls %.4f", t->wpctl, loudest / 65536.0);
        }
        unix_spawn(script);
    }
    if (mute >= 0)
    {
        if (use_pactl())
            swprintf(script, ARRAYSIZE(script), L"pactl set-%ls-mute %ls %d", t->pactl_kind, t->pactl, mute);
        else
            swprintf(script, ARRAYSIZE(script), L"wpctl set-mute %ls %d", t->wpctl, mute);
        unix_spawn(script);
    }
}

/* Tells sndvol32's windows that a target's controls changed. */
static void notify(unsigned index)
{
    EnterCriticalSection(&lock);
    for (unsigned i = 0; i < OPEN_LIMIT; i++)
    {
        if (!opened[i].mixer || opened[i].target != index || !opened[i].callback) continue;
        /* Wine's mixer controls: the destination's volume and mute are 0 and
         * 1, its source line's (a recording mixer's microphone) 2 and 3. */
        for (DWORD control = 0; control < (index == SOURCE ? 4u : 2u); control++)
            PostMessageW(opened[i].callback, MM_MIXM_CONTROL_CHANGE, (WPARAM)opened[i].mixer, control);
    }
    LeaveCriticalSection(&lock);
}

/* Passes sndvol32's changes on as they come and reads the sound server's
 * state every second. */
static DWORD WINAPI worker(void *unused)
{
    (void)unused;
    for (;;)
    {
        WaitForSingleObject(wake, 1000);
        for (unsigned i = 0; i < TARGETS; i++)
        {
            BOOL pending, used = FALSE;
            EnterCriticalSection(&lock);
            pending = targets[i].wanted_volume || targets[i].wanted_mute >= 0;
            for (unsigned j = 0; j < OPEN_LIMIT; j++) used |= opened[j].mixer && opened[j].target == i;
            LeaveCriticalSection(&lock);
            if (pending) apply(i);
            else if (used && refresh(i)) notify(i);
        }
    }
    return 0;
}

static void start(void)
{
    if (thread) return;
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

/* ---- The mixer API ---------------------------------------------------- */

/* The first playback mixer is the default sink, the first recording mixer
 * (after the playback ones) the default source; others stay Wine's. */
static unsigned target_of(HMIXEROBJ object, DWORD flags)
{
    UINT id;
    if (mixerGetID(object, &id, flags & 0xf0000000) != MMSYSERR_NOERROR) return NONE;
    if (id == 0 && waveOutGetNumDevs()) return SINK;
    if (id == waveOutGetNumDevs() && waveInGetNumDevs()) return SOURCE;
    return NONE;
}

static void load(unsigned index)
{
    BOOL loaded;
    EnterCriticalSection(&lock);
    loaded = targets[index].loaded;
    LeaveCriticalSection(&lock);
    if (!loaded) refresh(index);
}

static void string(UINT id, WCHAR *buffer, int size)
{
    WCHAR text[MIXER_LONG_NAME_CHARS];
    if (LoadStringW(GetModuleHandleW(NULL), id, text, ARRAYSIZE(text)))
        lstrcpynW(buffer, text, size);
}

UINT WINAPI xpmix_open(HMIXER *mixer, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    UINT result = mixerOpen(mixer, id, callback, instance, flags);
    unsigned target;
    if (result != MMSYSERR_NOERROR || !mixer) return result;
    target = target_of((HMIXEROBJ)(UINT_PTR)id, MIXER_OBJECTF_MIXER);
    if (target == NONE) return result;
    load(target);
    EnterCriticalSection(&lock);
    for (unsigned i = 0; i < OPEN_LIMIT; i++)
    {
        if (opened[i].mixer) continue;
        opened[i].mixer = *mixer;
        opened[i].target = target;
        opened[i].callback = (flags & CALLBACK_TYPEMASK) == CALLBACK_WINDOW ? (HWND)callback : NULL;
        break;
    }
    LeaveCriticalSection(&lock);
    start();
    return result;
}

UINT WINAPI xpmix_close(HMIXER mixer)
{
    EnterCriticalSection(&lock);
    for (unsigned i = 0; i < OPEN_LIMIT; i++)
        if (opened[i].mixer == mixer) opened[i].mixer = NULL;
    LeaveCriticalSection(&lock);
    return mixerClose(mixer);
}

UINT WINAPI xpmix_line_info(HMIXEROBJ object, MIXERLINEW *line, DWORD flags)
{
    UINT result = mixerGetLineInfoW(object, line, flags);
    unsigned target;
    if (result != MMSYSERR_NOERROR || (target = target_of(object, flags)) == NONE) return result;
    /* Wine names every line "Master Volume"; XP's sndvol32 has its own words. */
    if (line->dwComponentType == MIXERLINE_COMPONENTTYPE_DST_SPEAKERS)
    {
        line->cConnections = 0;
        string(IDS_VOLUME_CONTROL, line->szName, MIXER_LONG_NAME_CHARS);
        string(IDS_VOLUME_CONTROL, line->szShortName, MIXER_SHORT_NAME_CHARS);
    }
    else if (line->dwComponentType == MIXERLINE_COMPONENTTYPE_DST_WAVEIN)
    {
        string(IDS_RECORDING, line->szName, MIXER_LONG_NAME_CHARS);
        string(IDS_RECORDING, line->szShortName, MIXER_SHORT_NAME_CHARS);
    }
    else if (line->dwComponentType == MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE)
    {
        string(IDS_MICROPHONE, line->szName, MIXER_LONG_NAME_CHARS);
        string(IDS_MICROPHONE, line->szShortName, MIXER_SHORT_NAME_CHARS);
    }
    return result;
}

/* Wine's volume controls are 0 and 2, its mute controls 1 and 3. */
static BOOL is_volume(DWORD control) { return control == 0 || control == 2; }
static BOOL is_mute(DWORD control) { return control == 1 || control == 3; }

/* The volume per channel, for sndvol32's Balance slider: Wine's controls
 * are uniform, one value for every channel. */
static void per_channel(MIXERLINECONTROLSW *controls, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
    {
        MIXERCONTROLW *control = (MIXERCONTROLW *)((BYTE *)controls->pamxctrl + i * controls->cbmxctrl);
        if (control->dwControlType == MIXERCONTROL_CONTROLTYPE_VOLUME)
            control->fdwControl &= ~MIXERCONTROL_CONTROLF_UNIFORM;
    }
}

UINT WINAPI xpmix_line_controls(HMIXEROBJ object, MIXERLINECONTROLSW *controls, DWORD flags)
{
    MIXERCONTROLW both[2];
    MIXERLINECONTROLSW all;
    DWORD control;
    UINT result;
    if (!controls || controls->cbStruct < sizeof(*controls) || !controls->pamxctrl ||
        controls->cbmxctrl < sizeof(MIXERCONTROLW) || target_of(object, flags) == NONE)
        return mixerGetLineControlsW(object, controls, flags);
    if ((flags & MIXER_GETLINECONTROLSF_QUERYMASK) != MIXER_GETLINECONTROLSF_ONEBYID)
    {
        result = mixerGetLineControlsW(object, controls, flags);
        if (result == MMSYSERR_NOERROR)
            per_channel(controls, (flags & MIXER_GETLINECONTROLSF_QUERYMASK) == MIXER_GETLINECONTROLSF_ALL ?
                                  controls->cControls : 1);
        return result;
    }
    /* A control by its ID alone, as sndvol32 asks for one: Wine wants the
     * line's ID too and knows only the destination's controls by ID. */
    if ((control = controls->dwControlID) > 3) return MIXERR_INVALCONTROL;
    all = *controls;
    all.dwLineID = control < 2 ? 0xffff0000 : 0;
    all.cControls = 2;
    all.cbmxctrl = sizeof(MIXERCONTROLW);
    all.pamxctrl = both;
    result = mixerGetLineControlsW(object, &all, (flags & ~MIXER_GETLINECONTROLSF_QUERYMASK) | MIXER_GETLINECONTROLSF_ALL);
    if (result != MMSYSERR_NOERROR) return result;
    per_channel(&all, 2);
    controls->dwLineID = all.dwLineID;
    *controls->pamxctrl = both[control % 2];
    return MMSYSERR_NOERROR;
}

UINT WINAPI xpmix_get_details(HMIXEROBJ object, MIXERCONTROLDETAILS *details, DWORD flags)
{
    unsigned target, channels;
    TARGET *t;
    if ((flags & 0xf) != MIXER_GETCONTROLDETAILSF_VALUE || !details || !details->paDetails ||
        (target = target_of(object, flags)) == NONE || !(is_volume(details->dwControlID) || is_mute(details->dwControlID)))
        return mixerGetControlDetailsW(object, details, flags);
    load(target);
    t = &targets[target];
    channels = max(details->cChannels, 1);
    EnterCriticalSection(&lock);
    if (is_volume(details->dwControlID))
    {
        MIXERCONTROLDETAILS_UNSIGNED *values = details->paDetails;
        DWORD loudest = 0;
        if (details->cbDetails < sizeof(*values)) goto invalid;
        for (unsigned i = 0; i < t->count; i++) loudest = max(loudest, t->raw[i]);
        /* Per channel when sndvol32 asks for as many as the device has (its
         * Balance slider), else the loudest, as wpctl reports it. */
        for (unsigned i = 0; i < channels; i++)
            values[i].dwValue = min(channels == t->count ? t->raw[i] : loudest, 0xffff);
    }
    else
    {
        MIXERCONTROLDETAILS_BOOLEAN *values = details->paDetails;
        if (details->cbDetails < sizeof(*values)) goto invalid;
        for (unsigned i = 0; i < channels; i++) values[i].fValue = t->muted;
    }
    LeaveCriticalSection(&lock);
    return MMSYSERR_NOERROR;
invalid:
    LeaveCriticalSection(&lock);
    return MMSYSERR_INVALPARAM;
}

UINT WINAPI xpmix_set_details(HMIXEROBJ object, MIXERCONTROLDETAILS *details, DWORD flags)
{
    unsigned target, channels;
    TARGET *t;
    if ((flags & 0xf) != MIXER_SETCONTROLDETAILSF_VALUE || !details || !details->paDetails ||
        (target = target_of(object, flags)) == NONE || !(is_volume(details->dwControlID) || is_mute(details->dwControlID)))
        return mixerSetControlDetails(object, details, flags);
    t = &targets[target];
    channels = max(details->cChannels, 1);
    EnterCriticalSection(&lock);
    if (is_volume(details->dwControlID))
    {
        MIXERCONTROLDETAILS_UNSIGNED *values = details->paDetails;
        if (details->cbDetails < sizeof(*values)) goto invalid;
        if (channels == t->count && channels <= CHANNELS)
            for (unsigned i = 0; i < channels; i++) t->raw[i] = min(values[i].dwValue, 0xffff);
        else
        {
            DWORD loudest = 0;
            for (unsigned i = 0; i < channels; i++) loudest = max(loudest, values[i].dwValue);
            t->count = max(min(t->count, CHANNELS), 1);
            for (unsigned i = 0; i < t->count; i++) t->raw[i] = min(loudest, 0xffff);
        }
        /* 0xffff, the top of the slider, is PulseAudio's 100%. */
        for (unsigned i = 0; i < t->count; i++) t->wanted_raw[i] = t->raw[i] == 0xffff ? 65536 : t->raw[i];
        t->wanted_count = t->count;
        t->wanted_volume = TRUE;
    }
    else
    {
        MIXERCONTROLDETAILS_BOOLEAN *values = details->paDetails;
        if (details->cbDetails < sizeof(*values)) goto invalid;
        t->muted = values[0].fValue != 0;
        t->wanted_mute = t->muted;
    }
    t->loaded = TRUE;
    LeaveCriticalSection(&lock);
    start();
    SetEvent(wake);
    /* As XP's drivers did: every open handle hears of the change, the
     * setter's included (sndvol32 updates its other views from it). */
    notify(target);
    return MMSYSERR_NOERROR;
invalid:
    LeaveCriticalSection(&lock);
    return MMSYSERR_INVALPARAM;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&lock);
    }
    return TRUE;
}
