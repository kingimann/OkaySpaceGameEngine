// okay::platform backend "own" for Windows, audio part: playback and capture through the waveform audio API
// (waveOut* / waveIn*, winmm.dll loaded at run time with LoadLibrary, nothing linked).
//
// Written from Microsoft's documentation of the waveform audio functions (waveOutOpen, waveOutPrepareHeader, waveOutWrite,
// waveInOpen, waveInAddBuffer, waveInStart, the WAVEHDR / WAVEFORMATEX structures and the CALLBACK_EVENT notification).
// It follows the model of the own ALSA device (OwnLinuxAudio.cpp): one worker thread per device keeps a ring of buffers
// with the driver, and either calls the user's callback or moves bytes to / from a queue (SDL's audio device model).
//
// STATUS: COMPILES, UNTESTED. Only syntax-checked with MinGW on Linux; never run on Windows.
// Threads and locks are Win32 ones (CreateThread, CRITICAL_SECTION, events): the Windows build has no C++ thread library.
#ifdef _WIN32
#include "Internal.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>   // types and constants only (WAVEHDR, WAVEFORMATEX, WAVE_MAPPER, ...): winmm.dll is loaded at run time
#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace okay::platform {
namespace {

struct WinmmApi {
    HMODULE lib = nullptr;
    UINT (WINAPI* waveOutGetNumDevs)(void) = nullptr;
    MMRESULT (WINAPI* waveOutGetDevCapsW)(UINT_PTR, LPWAVEOUTCAPSW, UINT) = nullptr;
    MMRESULT (WINAPI* waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD) = nullptr;
    MMRESULT (WINAPI* waveOutClose)(HWAVEOUT) = nullptr;
    MMRESULT (WINAPI* waveOutPrepareHeader)(HWAVEOUT, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveOutUnprepareHeader)(HWAVEOUT, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveOutReset)(HWAVEOUT) = nullptr;
    UINT (WINAPI* waveInGetNumDevs)(void) = nullptr;
    MMRESULT (WINAPI* waveInGetDevCapsW)(UINT_PTR, LPWAVEINCAPSW, UINT) = nullptr;
    MMRESULT (WINAPI* waveInOpen)(LPHWAVEIN, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD) = nullptr;
    MMRESULT (WINAPI* waveInClose)(HWAVEIN) = nullptr;
    MMRESULT (WINAPI* waveInPrepareHeader)(HWAVEIN, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveInUnprepareHeader)(HWAVEIN, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveInAddBuffer)(HWAVEIN, LPWAVEHDR, UINT) = nullptr;
    MMRESULT (WINAPI* waveInStart)(HWAVEIN) = nullptr;
    MMRESULT (WINAPI* waveInReset)(HWAVEIN) = nullptr;
};

// Loaded once, on the first audio call; the library stays loaded for the process.
const WinmmApi* Api(std::string& err) {
    static WinmmApi api;
    static int state = 0;   // 0 untried, 1 ok, -1 failed (the first call happens on the main thread: the launcher opens devices there)
    static std::string why;
    if (state == 0) {
        state = -1;
        api.lib = LoadLibraryW(L"winmm.dll");
        if (!api.lib) why = "winmm.dll not found";
        else {
            bool ok = true;
#define OKAY_SYM(n) api.n = reinterpret_cast<decltype(api.n)>((void*)GetProcAddress(api.lib, #n)); \
            if (!api.n) { ok = false; why = "winmm.dll lacks " #n; }
            OKAY_SYM(waveOutGetNumDevs) OKAY_SYM(waveOutGetDevCapsW) OKAY_SYM(waveOutOpen) OKAY_SYM(waveOutClose)
            OKAY_SYM(waveOutPrepareHeader) OKAY_SYM(waveOutUnprepareHeader) OKAY_SYM(waveOutWrite) OKAY_SYM(waveOutReset)
            OKAY_SYM(waveInGetNumDevs) OKAY_SYM(waveInGetDevCapsW) OKAY_SYM(waveInOpen) OKAY_SYM(waveInClose)
            OKAY_SYM(waveInPrepareHeader) OKAY_SYM(waveInUnprepareHeader) OKAY_SYM(waveInAddBuffer) OKAY_SYM(waveInStart)
            OKAY_SYM(waveInReset)
#undef OKAY_SYM
            if (ok) state = 1;
        }
    }
    if (state != 1) { err = why; return nullptr; }
    return &api;
}

std::string Utf8Of(const wchar_t* w) {
    const int n = (int)wcslen(w);
    if (n <= 0) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(len > 0 ? len : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], len, nullptr, nullptr);
    return s;
}

// Device names as the driver reports them (WAVEOUTCAPSW / WAVEINCAPSW szPname). Index i is the waveOut / waveIn device id.
std::vector<std::string> ListDevices(const WinmmApi* a, bool capture) {
    std::vector<std::string> out;
    const UINT n = capture ? a->waveInGetNumDevs() : a->waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        if (capture) {
            WAVEINCAPSW c; std::memset(&c, 0, sizeof(c));
            out.push_back(a->waveInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR ? Utf8Of(c.szPname) : std::string());
        } else {
            WAVEOUTCAPSW c; std::memset(&c, 0, sizeof(c));
            out.push_back(a->waveOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR ? Utf8Of(c.szPname) : std::string());
        }
    }
    return out;
}

constexpr int kBuffers = 4;   // buffers handed to the driver in turn: about 4 periods of latency

class WinmmDevice final : public AudioDevice {
public:
    WinmmDevice(const WinmmApi* api, const AudioSpec& spec, bool capture) : m_api(api), m_spec(spec), m_capture(capture) {
        InitializeCriticalSection(&m_cs);
        m_frameBytes = BytesPerSample(spec.format) * spec.channels;
    }
    ~WinmmDevice() override {
        if (m_thread) {
            InterlockedExchange(&m_stop, 1);
            SetEvent(m_event);
            WaitForSingleObject(m_thread, INFINITE);
            CloseHandle(m_thread);
        }
        // Take every buffer back from the driver before it is freed (Reset marks them all done).
        if (m_capture && m_in) { m_api->waveInReset(m_in); for (WAVEHDR& h : m_hdr) if (h.dwFlags & WHDR_PREPARED) m_api->waveInUnprepareHeader(m_in, &h, sizeof(h)); m_api->waveInClose(m_in); }
        if (!m_capture && m_out) { m_api->waveOutReset(m_out); for (WAVEHDR& h : m_hdr) if (h.dwFlags & WHDR_PREPARED) m_api->waveOutUnprepareHeader(m_out, &h, sizeof(h)); m_api->waveOutClose(m_out); }
        if (m_event) CloseHandle(m_event);
        DeleteCriticalSection(&m_cs);
    }

    bool Open(UINT deviceId, std::string& err) {
        WAVEFORMATEX f; std::memset(&f, 0, sizeof(f));
        f.wFormatTag = m_spec.format == SampleFormat::F32 ? 3 /* WAVE_FORMAT_IEEE_FLOAT */ : WAVE_FORMAT_PCM;
        f.nChannels = (WORD)m_spec.channels;
        f.nSamplesPerSec = (DWORD)m_spec.freq;
        f.wBitsPerSample = (WORD)(BytesPerSample(m_spec.format) * 8);
        f.nBlockAlign = (WORD)m_frameBytes;
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // auto-reset: the driver sets it when a buffer is done
        if (!m_event) { err = "CreateEvent failed"; return false; }
        // A named device goes through the mapper too (WAVE_MAPPED), so it converts the format as WAVE_MAPPER does.
        const DWORD flags = CALLBACK_EVENT | (deviceId != WAVE_MAPPER ? WAVE_MAPPED : 0);
        const MMRESULT r = m_capture
            ? m_api->waveInOpen(&m_in, deviceId, &f, (DWORD_PTR)m_event, 0, flags)
            : m_api->waveOutOpen(&m_out, deviceId, &f, (DWORD_PTR)m_event, 0, flags);
        if (r != MMSYSERR_NOERROR) {
            err = std::string(m_capture ? "waveInOpen" : "waveOutOpen") + " failed (MMRESULT " + std::to_string((unsigned)r) + ")";
            return false;
        }
        const size_t bytes = (size_t)std::max(64, m_spec.frames) * (size_t)m_frameBytes;
        for (int i = 0; i < kBuffers; ++i) {
            m_buf[i].assign(bytes, 0);
            std::memset(&m_hdr[i], 0, sizeof(WAVEHDR));
            m_hdr[i].lpData = (LPSTR)m_buf[i].data();
            m_hdr[i].dwBufferLength = (DWORD)bytes;
            const MMRESULT p = m_capture ? m_api->waveInPrepareHeader(m_in, &m_hdr[i], sizeof(WAVEHDR))
                                         : m_api->waveOutPrepareHeader(m_out, &m_hdr[i], sizeof(WAVEHDR));
            if (p != MMSYSERR_NOERROR) { err = "waveform header preparation failed"; return false; }
        }
        // Capture: every buffer goes to the driver now and recording starts; what arrives while paused is dropped.
        // Playback: the worker fills and writes the buffers (silence while paused).
        if (m_capture) {
            for (WAVEHDR& h : m_hdr) m_api->waveInAddBuffer(m_in, &h, sizeof(WAVEHDR));
            if (m_api->waveInStart(m_in) != MMSYSERR_NOERROR) { err = "waveInStart failed"; return false; }
        } else {
            for (WAVEHDR& h : m_hdr) h.dwFlags |= WHDR_DONE;   // free for the worker's first pass
        }
        m_thread = CreateThread(nullptr, 0, &WinmmDevice::ThreadMain, this, 0, nullptr);
        if (!m_thread) { err = "CreateThread failed"; return false; }
        return true;
    }

    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override { InterlockedExchange(&m_paused, p ? 1 : 0); }
    bool Queue(const void* d, uint32_t n) override {
        if (m_capture || m_spec.callback) return false;
        const uint8_t* b = static_cast<const uint8_t*>(d);
        EnterCriticalSection(&m_cs);
        m_queue.insert(m_queue.end(), b, b + n);
        LeaveCriticalSection(&m_cs);
        return true;
    }
    uint32_t QueuedBytes() const override {
        EnterCriticalSection(&m_cs);
        const uint32_t n = (uint32_t)m_queue.size();
        LeaveCriticalSection(&m_cs);
        return n;
    }
    void ClearQueued() override { EnterCriticalSection(&m_cs); m_queue.clear(); LeaveCriticalSection(&m_cs); }
    uint32_t Dequeue(void* out, uint32_t max) override {
        EnterCriticalSection(&m_cs);
        const uint32_t n = (uint32_t)std::min<size_t>(max, m_queue.size());
        uint8_t* o = static_cast<uint8_t*>(out);
        for (uint32_t i = 0; i < n; ++i) { o[i] = m_queue.front(); m_queue.pop_front(); }
        LeaveCriticalSection(&m_cs);
        return n;
    }

private:
    static DWORD WINAPI ThreadMain(LPVOID self) { static_cast<WinmmDevice*>(self)->Run(); return 0; }
    void Run() {
        while (InterlockedCompareExchange(&m_stop, 0, 0) == 0) {
            for (WAVEHDR& h : m_hdr) {
                if (!(h.dwFlags & WHDR_DONE)) continue;   // still with the driver
                if (m_capture) Captured(h); else Fill(h);
            }
            WaitForSingleObject(m_event, 50);   // a buffer came back, or 50 ms passed
        }
    }
    // Playback: fill one free buffer from the callback or the queue (silence on underrun or while paused) and hand it back.
    void Fill(WAVEHDR& h) {
        uint8_t* b = (uint8_t*)h.lpData;
        const size_t bytes = h.dwBufferLength;
        const bool paused = InterlockedCompareExchange(&m_paused, 0, 0) != 0;
        std::memset(b, 0, bytes);
        if (!paused) {
            if (m_spec.callback) m_spec.callback(m_spec.user, b, (int)bytes);
            else {
                EnterCriticalSection(&m_cs);
                const size_t take = std::min(bytes, m_queue.size());
                for (size_t i = 0; i < take; ++i) { b[i] = m_queue.front(); m_queue.pop_front(); }
                LeaveCriticalSection(&m_cs);
            }
        }
        h.dwFlags &= ~(DWORD)WHDR_DONE;
        m_api->waveOutWrite(m_out, &h, sizeof(WAVEHDR));
    }
    // Capture: pass what one buffer recorded to the callback or the queue (dropped while paused), then give it back.
    void Captured(WAVEHDR& h) {
        const size_t got = h.dwBytesRecorded;
        if (got > 0 && InterlockedCompareExchange(&m_paused, 0, 0) == 0) {
            uint8_t* b = (uint8_t*)h.lpData;
            if (m_spec.callback) m_spec.callback(m_spec.user, b, (int)got);
            else {
                EnterCriticalSection(&m_cs);
                m_queue.insert(m_queue.end(), b, b + got);
                while (m_queue.size() > (size_t)1 << 20) m_queue.pop_front();   // cap: drop the oldest, as the ALSA device does
                LeaveCriticalSection(&m_cs);
            }
        }
        h.dwFlags &= ~(DWORD)WHDR_DONE;
        h.dwBytesRecorded = 0;
        m_api->waveInAddBuffer(m_in, &h, sizeof(WAVEHDR));
    }

    const WinmmApi* m_api;
    AudioSpec m_spec;
    bool m_capture;
    int m_frameBytes = 2;
    HWAVEOUT m_out = nullptr;
    HWAVEIN m_in = nullptr;
    HANDLE m_event = nullptr, m_thread = nullptr;
    volatile LONG m_stop = 0, m_paused = 1;   // devices start paused
    WAVEHDR m_hdr[kBuffers];
    std::vector<uint8_t> m_buf[kBuffers];
    mutable CRITICAL_SECTION m_cs;
    std::deque<uint8_t> m_queue;
};

} // namespace

std::unique_ptr<AudioDevice> OpenWinmmAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got, std::string& err) {
    const WinmmApi* a = Api(err);
    if (!a) return nullptr;
    UINT id = WAVE_MAPPER;   // the system default device
    if (name && *name) {
        const std::vector<std::string> names = ListDevices(a, capture);
        auto it = std::find(names.begin(), names.end(), std::string(name));
        if (it == names.end()) { err = std::string("no audio device named ") + name; return nullptr; }
        id = (UINT)(it - names.begin());
    }
    AudioSpec spec = want;
    if (spec.frames <= 0) spec.frames = 1024;
    if (spec.channels <= 0) spec.channels = 1;
    std::unique_ptr<WinmmDevice> d(new WinmmDevice(a, spec, capture));
    // WAVE_MAPPER converts rates and formats for us, so the device runs at exactly what was asked (allowChange is not needed).
    if (!d->Open(id, err)) return nullptr;
    if (got) *got = spec;
    return std::unique_ptr<AudioDevice>(d.release());
}

int WinmmDeviceCount(bool capture) {
    std::string err;
    const WinmmApi* a = Api(err);
    return a ? (int)ListDevices(a, capture).size() : 0;
}

std::string WinmmDeviceName(int index, bool capture) {
    std::string err;
    const WinmmApi* a = Api(err);
    if (!a) return std::string();
    const std::vector<std::string> v = ListDevices(a, capture);
    return index >= 0 && index < (int)v.size() ? v[(size_t)index] : std::string();
}

} // namespace okay::platform
#endif // _WIN32
