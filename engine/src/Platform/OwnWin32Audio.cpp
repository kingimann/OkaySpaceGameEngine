// Own Windows backend, audio part: WASAPI shared mode, event driven, through the COM interfaces
// in the system headers (mmdeviceapi.h, audioclient.h). No link to anything but ole32.
//
// Written from the public Microsoft documentation of IMMDeviceEnumerator, IAudioClient (Initialize
// with AUDCLNT_STREAMFLAGS_EVENTCALLBACK, AUTOCONVERTPCM and SRC_DEFAULT_QUALITY, SetEventHandle,
// GetCurrentPadding), IAudioRenderClient and IAudioCaptureClient. All decisions that need no Windows
// (formats, buffer sizes, the queue, what to do with a buffer) are in AudioPump.hpp and are tested on Linux.
//
// STATUS: COMPILES (MinGW -fsyntax-only), NEVER RUN. No Windows machine has run this. One thread per
// device (CreateThread, not std::thread: CLAUDE.md rule 21a) owns every COM object of its device.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propidl.h>
#include <functiondiscoverykeys_devpkey.h>
#include "Internal.hpp"
#include "AudioPump.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace okay::platform::own {
namespace {

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { Reset(); }
    void Reset() { if (p) { p->Release(); p = nullptr; } }
    T** Out() { Reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string Utf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string s((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
    return s;
}

std::string Hex(HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lX", (unsigned long)hr);
    return b;
}

/// COM for the calling thread for as long as the object lives. A thread that already chose another
/// apartment model is fine: its COM keeps working and we must not undo it.
struct ComInit {
    bool undo = false;
    ComInit() { const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); undo = SUCCEEDED(hr); }
    ~ComInit() { if (undo) CoUninitialize(); }
};

EDataFlow Flow(bool capture) { return capture ? eCapture : eRender; }

std::string FriendlyName(IMMDevice* d) {
    Com<IPropertyStore> ps;
    if (FAILED(d->OpenPropertyStore(STGM_READ, ps.Out()))) return std::string();
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name;
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = Utf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

HRESULT Enumerator(Com<IMMDeviceEnumerator>& e) {
    return CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)e.Out());
}

/// Active endpoint names in the order Windows lists them.
std::vector<std::string> ActiveNames(bool capture) {
    std::vector<std::string> out;
    ComInit com;
    Com<IMMDeviceEnumerator> en;
    if (FAILED(Enumerator(en))) return out;
    Com<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(Flow(capture), DEVICE_STATE_ACTIVE, col.Out()))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d;
        if (SUCCEEDED(col->Item(i, d.Out()))) { std::string s = FriendlyName(d.p); if (!s.empty()) out.push_back(s); }
    }
    return out;
}

class WasapiDevice final : public AudioDevice {
public:
    WasapiDevice(const AudioSpec& want, bool capture, std::string device)
        : m_spec(want), m_capture(capture), m_device(std::move(device)) {}

    /// Starts the pump thread and waits until it has either opened the device or given up.
    bool Start(AudioSpec* got, std::string& err) {
        m_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_ready) { err = "CreateEvent failed"; return false; }
        m_thread = CreateThread(nullptr, 0, &WasapiDevice::ThreadMain, this, 0, nullptr);
        if (!m_thread) { err = "CreateThread failed"; CloseHandle(m_ready); m_ready = nullptr; return false; }
        if (WaitForSingleObject(m_ready, 10000) != WAIT_OBJECT_0) m_openErr = "WASAPI did not open the device within 10 s";
        if (!m_openOk) { err = m_openErr.empty() ? "WASAPI open failed" : m_openErr; Stop(); return false; }
        if (got) *got = m_spec;
        return true;
    }
    ~WasapiDevice() override { Stop(); }

    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override { m_paused = p; }
    bool Queue(const void* d, uint32_t n) override {
        if (m_capture || m_spec.callback) return false;
        m_ring.Push(d, n);
        return true;
    }
    uint32_t QueuedBytes() const override { return m_ring.Size(); }
    void ClearQueued() override { m_ring.Clear(); }
    uint32_t Dequeue(void* out, uint32_t max) override { return m_capture ? m_ring.Pop(out, max) : 0; }

private:
    void Stop() {
        m_stop = true;
        if (m_thread) { WaitForSingleObject(m_thread, 5000); CloseHandle(m_thread); m_thread = nullptr; }
        if (m_ready) { CloseHandle(m_ready); m_ready = nullptr; }
    }
    static DWORD WINAPI ThreadMain(LPVOID p) { static_cast<WasapiDevice*>(p)->Run(); return 0; }

    // Open the endpoint (by name, or the default one) and an IAudioClient on it.
    bool OpenClient(Com<IMMDeviceEnumerator>& en, Com<IAudioClient>& cl, std::string& err) {
        Com<IMMDevice> dev;
        const bool byName = !m_device.empty() && m_device != "default";
        if (byName) {
            Com<IMMDeviceCollection> col;
            if (SUCCEEDED(en->EnumAudioEndpoints(Flow(m_capture), DEVICE_STATE_ACTIVE, col.Out()))) {
                UINT n = 0;
                col->GetCount(&n);
                for (UINT i = 0; i < n && !dev; ++i) {
                    Com<IMMDevice> d;
                    if (SUCCEEDED(col->Item(i, d.Out())) && FriendlyName(d.p) == m_device) { dev.p = d.p; d.p = nullptr; }
                }
            }
            if (!dev) { err = "no audio device named \"" + m_device + "\""; return false; }
        } else {
            const HRESULT hr = en->GetDefaultAudioEndpoint(Flow(m_capture), eConsole, dev.Out());
            if (FAILED(hr)) { err = "no default audio device (" + Hex(hr) + ")"; return false; }
        }
        const HRESULT hr = dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)cl.Out());
        if (FAILED(hr)) { err = "IMMDevice::Activate failed (" + Hex(hr) + ")"; return false; }
        return true;
    }

    static void ToWindows(const pump::WaveFormat& f, WAVEFORMATEXTENSIBLE* w) {
        std::memset(w, 0, sizeof *w);
        w->Format.wFormatTag = f.tag;
        w->Format.nChannels = f.channels;
        w->Format.nSamplesPerSec = f.rate;
        w->Format.wBitsPerSample = f.bits;
        w->Format.nBlockAlign = f.blockAlign;
        w->Format.nAvgBytesPerSec = f.avgBytesPerSec;
        if (f.tag == pump::kTagExtensible) {
            w->Format.cbSize = 22;
            w->Samples.wValidBitsPerSample = f.bits;
            w->dwChannelMask = f.channelMask;
            w->SubFormat = f.subFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
        }
    }
    static pump::WaveFormat FromWindows(const WAVEFORMATEX* w) {
        pump::WaveFormat f;
        f.tag = w->wFormatTag;
        f.channels = w->nChannels;
        f.rate = w->nSamplesPerSec;
        f.bits = w->wBitsPerSample;
        f.blockAlign = w->nBlockAlign;
        f.avgBytesPerSec = w->nAvgBytesPerSec;
        if (f.tag == pump::kTagExtensible && w->cbSize >= 22) {
            const WAVEFORMATEXTENSIBLE* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(w);
            f.subFloat = IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
            f.channelMask = x->dwChannelMask;
        }
        return f;
    }

    bool Setup(std::string& err) {
        Com<IMMDeviceEnumerator> en;
        HRESULT hr = Enumerator(en);
        if (FAILED(hr)) { err = "cannot create the device enumerator (" + Hex(hr) + ")"; return false; }
        if (!OpenClient(en, m_client, err)) return false;

        m_period = pump::ClampPeriodFrames(m_spec.frames > 0 ? m_spec.frames : 1024);
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        pump::WaveFormat wf;
        if (!pump::MakeWaveFormat(m_spec, &wf)) { err = "unsupported audio format (channels or rate out of range)"; return false; }
        WAVEFORMATEXTENSIBLE w;
        ToWindows(wf, &w);
        hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, pump::BufferDurationHns(m_period, m_spec.freq), 0,
                                  reinterpret_cast<WAVEFORMATEX*>(&w), nullptr);
        if (FAILED(hr) && m_spec.allowChange) {
            // The engine would not convert to the exact format: run at its own mix format and say so.
            m_client.Reset();
            if (!OpenClient(en, m_client, err)) return false;
            WAVEFORMATEX* mix = nullptr;
            hr = m_client->GetMixFormat(&mix);
            AudioSpec g;
            if (SUCCEEDED(hr) && mix && pump::SpecFromMixFormat(FromWindows(mix), m_spec, &g)) {
                g.frames = m_period;
                hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                          pump::BufferDurationHns(m_period, g.freq), 0, mix, nullptr);
                if (SUCCEEDED(hr)) m_spec = g;
            } else if (SUCCEEDED(hr)) {
                hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
            if (mix) CoTaskMemFree(mix);
        }
        if (FAILED(hr)) { err = "IAudioClient::Initialize failed (" + Hex(hr) + ")"; return false; }
        m_spec.frames = m_period;
        m_frameBytes = (uint32_t)(BytesPerSample(m_spec.format) * m_spec.channels);

        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_event) { err = "CreateEvent failed"; return false; }
        hr = m_client->SetEventHandle(m_event);
        if (FAILED(hr)) { err = "SetEventHandle failed (" + Hex(hr) + ")"; return false; }
        hr = m_client->GetBufferSize(&m_bufferFrames);
        if (FAILED(hr)) { err = "GetBufferSize failed (" + Hex(hr) + ")"; return false; }
        hr = m_capture ? m_client->GetService(IID_IAudioCaptureClient, (void**)m_cap.Out())
                       : m_client->GetService(IID_IAudioRenderClient, (void**)m_render.Out());
        if (FAILED(hr)) { err = "GetService failed (" + Hex(hr) + ")"; return false; }
        return true;
    }

    // Playback: fill the free part of the engine's buffer, in chunks of one period.
    bool ServiceRender() {
        UINT32 pad = 0;
        if (FAILED(m_client->GetCurrentPadding(&pad))) return false;
        const uint32_t freeFrames = pump::FreeFrames(m_bufferFrames, pad);
        if (!freeFrames) return true;
        BYTE* data = nullptr;
        if (FAILED(m_render->GetBuffer(freeFrames, &data)) || !data) return false;
        uint32_t done = 0;
        while (done < freeFrames) {
            const uint32_t n = std::min<uint32_t>((uint32_t)m_period, freeFrames - done);
            pump::FillPlayback(m_spec, m_ring, data + (size_t)done * m_frameBytes, n * m_frameBytes);
            done += n;
        }
        return SUCCEEDED(m_render->ReleaseBuffer(freeFrames, 0));
    }

    // Capture: take every packet the engine has.
    bool ServiceCapture() {
        for (;;) {
            UINT32 next = 0;
            if (FAILED(m_cap->GetNextPacketSize(&next))) return false;
            if (!next) return true;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD fl = 0;
            if (FAILED(m_cap->GetBuffer(&data, &frames, &fl, nullptr, nullptr))) return false;
            pump::DeliverCapture(m_spec, m_ring, data, frames * m_frameBytes, (fl & AUDCLNT_BUFFERFLAGS_SILENT) != 0, m_scratch);
            if (FAILED(m_cap->ReleaseBuffer(frames))) return false;
        }
    }

    void Run() {
        ComInit com;
        std::string err;
        if (!Setup(err)) { m_openErr = err; m_openOk = false; SetEvent(m_ready); Teardown(); return; }
        m_openOk = true;
        SetEvent(m_ready);

        const DWORD waitMs = pump::WaitMs(m_period, m_spec.freq);
        bool running = false, dead = false;
        while (!m_stop) {
            const bool want = !m_paused && !dead;
            if (want && !running) {
                if (!m_capture) ServiceRender();   // pre-fill, so the first period is not silence
                running = SUCCEEDED(m_client->Start());
                if (!running) dead = true;
            } else if (!want && running) {
                m_client->Stop();
                m_client->Reset();
                running = false;
            }
            if (!running) { Sleep(2); continue; }
            WaitForSingleObject(m_event, waitMs);   // a timeout is fine: look again
            if (m_stop) break;
            if (!(m_capture ? ServiceCapture() : ServiceRender())) dead = true;   // device removed or reset
        }
        if (running) m_client->Stop();
        Teardown();
    }

    void Teardown() {
        m_render.Reset();
        m_cap.Reset();
        m_client.Reset();
        if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    }

    AudioSpec m_spec;
    bool m_capture;
    std::string m_device;
    int m_period = 1024;
    uint32_t m_frameBytes = 4;
    UINT32 m_bufferFrames = 0;
    HANDLE m_thread = nullptr, m_ready = nullptr, m_event = nullptr;
    Com<IAudioClient> m_client;
    Com<IAudioRenderClient> m_render;
    Com<IAudioCaptureClient> m_cap;
    pump::ByteRing m_ring;
    std::vector<uint8_t> m_scratch;
    std::atomic<bool> m_stop{false}, m_paused{true};
    bool m_openOk = false;       // written by the pump thread before m_ready is set
    std::string m_openErr;
};

} // namespace

std::unique_ptr<AudioDevice> OpenWasapi(const AudioSpec& want, bool capture, const char* device,
                                        AudioSpec* got, std::string& err) {
    pump::WaveFormat probe;
    if (!pump::MakeWaveFormat(want, &probe)) { err = "unsupported audio format (channels or rate out of range)"; return nullptr; }
    std::unique_ptr<WasapiDevice> d(new WasapiDevice(want, capture, device ? device : ""));
    if (!d->Start(got, err)) return nullptr;
    return d;
}

// Index 0 is "default" (the system default endpoint), then the active endpoints by friendly name,
// the same shape as the Linux list.
int WasapiDeviceCount(bool capture) { return 1 + (int)ActiveNames(capture).size(); }
std::string WasapiDeviceName(int index, bool capture) {
    if (index == 0) return "default";
    const auto v = ActiveNames(capture);
    return index > 0 && index <= (int)v.size() ? v[(size_t)index - 1] : std::string();
}

} // namespace okay::platform::own
#endif
