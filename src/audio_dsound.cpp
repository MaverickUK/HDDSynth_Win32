// DirectSound backend: opportunistic alternative to audio_waveout.cpp's
// waveOut/MME stream, used when DirectX is actually present and working
// on this machine. dsound.dll is loaded dynamically via LoadLibraryA +
// GetProcAddress -- never linked at build time (no -ldsound anywhere in
// the Makefile) -- so this file compiles and runs identically whether or
// not DirectX is installed; DSoundInit simply returns false if it isn't,
// and the caller (audio.cpp's dispatcher) falls back to the waveOut
// backend in that case. This is deliberate: DirectX was never part of a
// stock Windows 95 install, so a hard link against dsound.dll would add
// exactly the kind of new required DLL dependency CLAUDE.md's static
// verification step (objdump -p | grep "DLL Name") is there to catch.
//
// Uses the classic IDirectSound/IDirectSoundBuffer interface (via
// DirectSoundCreate, DirectX 3+) rather than IDirectSound8/DX8, since the
// latter would arbitrarily exclude older Win9x machines with an earlier
// DirectX runtime for no benefit -- nothing here needs anything DX8-only.
//
// #define INITGUID before including <dsound.h> is required: without it,
// DEFINE_GUID expands to an `extern const GUID` declaration rather than a
// definition, and referencing IID_IDirectSoundNotify would need the
// symbol to come from dsound.lib at link time -- which we deliberately
// never link. With INITGUID, the GUID bytes are defined locally in this
// object file instead (a weak/selectany definition, just data, not a DLL
// entry point) -- no dsound.lib needed. CLSID_DirectSound is never
// referenced at all: DirectSoundCreate(NULL, ...) uses the default
// device, avoiding that symbol too.
#include "audio_backend.h"
#include "mixer.h"
#include <mmsystem.h>
#define INITGUID
// dsound.h's objbase.h include drags in combaseapi.h, which
// unconditionally #includes <stdlib.h> -- whose libstdc++ C++ wrapper
// references quick_exit/at_quick_exit, UCRT-only symbols this project's
// -mcrtdll=msvcrt-os CRT target doesn't declare at all (a hard compile
// error, not just a link error -- see CLAUDE.md rule 2). Nothing in this
// file needs anything from stdlib.h (no malloc/rand/exit -- HeapAlloc
// and a hand-rolled PRNG are used elsewhere in this codebase instead),
// so faking its include guards makes the preprocessor skip its body
// entirely, the same category of fix as WIN32_LEAN_AND_MEAN preventing
// windows.h from pulling in the same poisoned OLE/COM headers.
#define _GLIBCXX_STDLIB_H 1
#define _GLIBCXX_CSTDLIB 1
#include <dsound.h>
#include <process.h>

// Secondary buffer is split into this many equal chunks, each with its
// own IDirectSoundNotify position at its start. When the play cursor
// crosses into chunk i, chunk (i-1) has just finished playing and is
// safe to refill -- the DirectSound equivalent of waveOut's WHDR_DONE
// per-buffer completion flag, just with a fixed chunk count instead of a
// user-configurable one (audioBufferMs still controls total buffer
// duration, split evenly across these chunks).
#define DS_NUM_CHUNKS 4

typedef HRESULT(WINAPI *DirectSoundCreateFunc)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);

static HMODULE g_hDSoundDll;
static LPDIRECTSOUND g_pDS;
static LPDIRECTSOUNDBUFFER g_pDSBuffer;
static LPDIRECTSOUNDNOTIFY g_pDSNotify;
static HANDLE g_notifyEvents[DS_NUM_CHUNKS];
static HANDLE g_stopEvent;
static HANDLE g_thread;
static volatile LONG g_stopRequested = 0;
static DWORD g_chunkBytes;
static unsigned long g_currentSampleRate = 0;
static int g_currentBufferMs = 0;

static void FillChunk(int chunkIndex) {
    void *ptr1;
    DWORD bytes1;
    void *ptr2;
    DWORD bytes2;
    if (FAILED(g_pDSBuffer->Lock(chunkIndex * g_chunkBytes, g_chunkBytes, &ptr1, &bytes1,
                                   &ptr2, &bytes2, 0))) {
        return;
    }
    MixerFillBuffer((short *)ptr1, bytes1 / sizeof(short));
    if (ptr2 && bytes2) {
        MixerFillBuffer((short *)ptr2, bytes2 / sizeof(short));
    }
    g_pDSBuffer->Unlock(ptr1, bytes1, ptr2, bytes2);
}

static unsigned __stdcall DSoundThreadProc(void *) {
    HANDLE waitHandles[DS_NUM_CHUNKS + 1];
    for (int i = 0; i < DS_NUM_CHUNKS; i++) {
        waitHandles[i] = g_notifyEvents[i];
    }
    waitHandles[DS_NUM_CHUNKS] = g_stopEvent;

    while (!InterlockedExchangeAdd(&g_stopRequested, 0)) {
        DWORD result = WaitForMultipleObjects(DS_NUM_CHUNKS + 1, waitHandles, FALSE, INFINITE);
        if (InterlockedExchangeAdd(&g_stopRequested, 0)) {
            break;
        }
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + DS_NUM_CHUNKS) {
            int notifiedChunk = (int)(result - WAIT_OBJECT_0);
            int chunkToFill = (notifiedChunk - 1 + DS_NUM_CHUNKS) % DS_NUM_CHUNKS;
            FillChunk(chunkToFill);
        }
    }
    return 0;
}

static void StartDSoundThread() {
    g_stopRequested = 0;
    unsigned tid;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, DSoundThreadProc, NULL, 0, &tid);
}

static void StopDSoundThread() {
    if (g_thread) {
        InterlockedExchange(&g_stopRequested, 1);
        SetEvent(g_stopEvent);
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

static void ReleaseSecondaryBuffer() {
    if (g_pDSNotify) {
        g_pDSNotify->Release();
        g_pDSNotify = NULL;
    }
    if (g_pDSBuffer) {
        g_pDSBuffer->Stop();
        g_pDSBuffer->Release();
        g_pDSBuffer = NULL;
    }
}

static void ReleaseAllEvents() {
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
    }
    for (int i = 0; i < DS_NUM_CHUNKS; i++) {
        if (g_notifyEvents[i]) {
            CloseHandle(g_notifyEvents[i]);
            g_notifyEvents[i] = NULL;
        }
    }
}

// Creates the secondary buffer sized from bufferMs, registers a notify
// position at the start of each chunk, fills the whole thing with the
// mixer's initial output, and starts it looping. Assumes g_pDS and the
// event handles already exist; on any failure, unwinds whatever it
// itself created (buffer/notify) and returns false -- the caller decides
// whether that means falling back to waveOut entirely (DSoundInit) or
// just failing this one reconfiguration (DSoundSetBufferMs/
// DSoundSwitchSamplePack).
static bool CreateSecondaryBuffer(int bufferMs) {
    unsigned long sampleRate = MixerGetSampleRate();

    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    long chunkSamples = (long)((unsigned long long)bufferMs * sampleRate / 1000 / DS_NUM_CHUNKS);
    if (chunkSamples < 64) chunkSamples = 64; // defensive floor; real Settings values never get here
    DWORD chunkBytes = (DWORD)chunkSamples * sizeof(short);
    DWORD totalBytes = chunkBytes * DS_NUM_CHUNKS;

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(dsbd));
    dsbd.dwSize = sizeof(dsbd);
    dsbd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS;
    dsbd.dwBufferBytes = totalBytes;
    dsbd.lpwfxFormat = &wfx;

    if (FAILED(g_pDS->CreateSoundBuffer(&dsbd, &g_pDSBuffer, NULL))) {
        g_pDSBuffer = NULL;
        return false;
    }

    if (FAILED(g_pDSBuffer->QueryInterface(IID_IDirectSoundNotify, (void **)&g_pDSNotify))) {
        g_pDSBuffer->Release();
        g_pDSBuffer = NULL;
        return false;
    }

    DSBPOSITIONNOTIFY notifies[DS_NUM_CHUNKS];
    for (int i = 0; i < DS_NUM_CHUNKS; i++) {
        notifies[i].dwOffset = i * chunkBytes;
        notifies[i].hEventNotify = g_notifyEvents[i];
    }
    if (FAILED(g_pDSNotify->SetNotificationPositions(DS_NUM_CHUNKS, notifies))) {
        g_pDSNotify->Release();
        g_pDSNotify = NULL;
        g_pDSBuffer->Release();
        g_pDSBuffer = NULL;
        return false;
    }

    void *ptr1;
    DWORD bytes1;
    void *ptr2;
    DWORD bytes2;
    if (FAILED(g_pDSBuffer->Lock(0, totalBytes, &ptr1, &bytes1, &ptr2, &bytes2, 0))) {
        g_pDSNotify->Release();
        g_pDSNotify = NULL;
        g_pDSBuffer->Release();
        g_pDSBuffer = NULL;
        return false;
    }
    MixerFillBuffer((short *)ptr1, bytes1 / sizeof(short));
    if (ptr2 && bytes2) {
        MixerFillBuffer((short *)ptr2, bytes2 / sizeof(short));
    }
    g_pDSBuffer->Unlock(ptr1, bytes1, ptr2, bytes2);

    g_pDSBuffer->Play(0, 0, DSBPLAY_LOOPING);

    g_chunkBytes = chunkBytes;
    g_currentSampleRate = sampleRate;
    g_currentBufferMs = bufferMs;
    return true;
}

bool DSoundInit(HWND hwnd, int bufferMs) {
    // NULL here (dsound.dll simply isn't on this machine) is the
    // expected, common case this whole backend exists to handle
    // gracefully -- not a real error.
    g_hDSoundDll = LoadLibraryA("DSOUND.DLL");
    if (!g_hDSoundDll) {
        return false;
    }

    DirectSoundCreateFunc pDirectSoundCreate =
        (DirectSoundCreateFunc)GetProcAddress(g_hDSoundDll, "DirectSoundCreate");
    if (!pDirectSoundCreate) {
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
        return false;
    }

    if (FAILED(pDirectSoundCreate(NULL, &g_pDS, NULL))) {
        g_pDS = NULL;
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
        return false;
    }

    if (FAILED(g_pDS->SetCooperativeLevel(hwnd, DSSCL_PRIORITY))) {
        g_pDS->Release();
        g_pDS = NULL;
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
        return false;
    }

    g_stopEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    bool eventsOk = (g_stopEvent != NULL);
    for (int i = 0; i < DS_NUM_CHUNKS && eventsOk; i++) {
        g_notifyEvents[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!g_notifyEvents[i]) eventsOk = false;
    }
    if (!eventsOk) {
        ReleaseAllEvents();
        g_pDS->Release();
        g_pDS = NULL;
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
        return false;
    }

    if (!CreateSecondaryBuffer(bufferMs)) {
        ReleaseAllEvents();
        g_pDS->Release();
        g_pDS = NULL;
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
        return false;
    }

    StartDSoundThread();
    return true;
}

void DSoundSetBufferMs(int ms) {
    if (ms == g_currentBufferMs) {
        return;
    }
    StopDSoundThread();
    ReleaseSecondaryBuffer();
    if (CreateSecondaryBuffer(ms)) {
        StartDSoundThread();
    }
}

bool DSoundSwitchSamplePack(unsigned long newSampleRate) {
    if (newSampleRate == g_currentSampleRate) {
        return true;
    }

    // Format is fixed at buffer-creation time, same as waveOut's device
    // handle -- but unlike waveOut, only the secondary buffer needs
    // recreating here, not the whole IDirectSound device object.
    StopDSoundThread();
    ReleaseSecondaryBuffer();
    bool ok = CreateSecondaryBuffer(g_currentBufferMs);
    if (ok) {
        StartDSoundThread();
    }
    return ok;
}

void DSoundShutdown() {
    StopDSoundThread();
    ReleaseSecondaryBuffer();
    ReleaseAllEvents();
    if (g_pDS) {
        g_pDS->Release();
        g_pDS = NULL;
    }
    if (g_hDSoundDll) {
        FreeLibrary(g_hDSoundDll);
        g_hDSoundDll = NULL;
    }
}
