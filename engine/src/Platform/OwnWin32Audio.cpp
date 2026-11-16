// Own Windows backend, audio part: waveOut (playback) and waveIn (capture) with a worker thread and four
// buffers per device. Queue mode and callback mode behave like the ALSA device (OwnLinuxAudio.cpp): devices
// start paused, a paused device keeps what is queued, an underrun plays silence, a capture queue drops the
// oldest bytes past 1 MiB.
//
// STATUS: cross-compiled with MinGW. Under Wine on the build machine there is no sound card (waveOutGetNumDevs
// is 0), so Open reports "no device" there and the data path has NOT run against a real driver. The pure
// parts (ring queue, period fill) are unit-tested natively (tests/test_platform_pure.cpp).
#ifdef _WIN32
#include "Win32Audio.hpp"
#include "AudioRing.hpp"
#include "PlatformText.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace okay::platform::win32 {
namespace {

constexpr int kBuffers = 4;
constexpr size_t kCaptureCap = (size_t)1 << 20;

std::string MmError(bool capture, MMRESULT r) {
    wchar_t buf[MAXERRORLENGTH] = {0};
    if (capture ? waveInGetErrorTextW(r, buf, MAXERRORLENGTH) : waveOutGetErrorTextW(r, buf, MAXERRORLENGTH)) return "mmsystem error " + std::to_string((int)r);
    return text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(buf), wcslen(buf));
}

class WaveDevice final : public AudioDevice {
public:
    WaveDevice(const AudioSpec& spec, bool capture) : m_spec(spec), m_capture(capture), m_ring(capture ? kCaptureCap : 0) {
        m_frameBytes = (size_t)BytesPerSample(spec.format) * (size_t)spec.channels;
        m_bufBytes = m_frameBytes * (size_t)(spec.frames > 0 ? spec.frames : 1024);
    }
    ~WaveDevice() override {
        m_stop = true;
        if (m_event) SetEvent(m_event);
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_capture) { if (m_in) waveInReset(m_in); } else if (m_out) waveOutReset(m_out);   // returns every buffer as done
        for (int i = 0; i < kBuffers; ++i) {
            if (!m_hdr[i].lpData) continue;
            if (m_capture) waveInUnprepareHeader(m_in, &m_hdr[i], sizeof(WAVEHDR)); else waveOutUnprepareHeader(m_out, &m_hdr[i], sizeof(WAVEHDR));
        }
        if (m_in) waveInClose(m_in);
        if (m_out) waveOutClose(m_out);
        if (m_event) CloseHandle(m_event);
    }

    bool Open(UINT deviceId, std::string& err) {
        WAVEFORMATEX fx; std::memset(&fx, 0, sizeof(fx));
        fx.wFormatTag = m_spec.format == SampleFormat::F32 ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        fx.nChannels = (WORD)m_spec.channels;
        fx.nSamplesPerSec = (DWORD)m_spec.freq;
        fx.wBitsPerSample = (WORD)(BytesPerSample(m_spec.format) * 8);
        fx.nBlockAlign = (WORD)m_frameBytes;
        fx.nAvgBytesPerSec = fx.nSamplesPerSec * fx.nBlockAlign;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // auto-reset: signalled by the driver when a buffer completes
        if (!m_event) { err = "CreateEvent failed"; return false; }
        MMRESULT r = m_capture ? waveInOpen(&m_in, deviceId, &fx, (DWORD_PTR)m_event, 0, CALLBACK_EVENT)
                               : waveOutOpen(&m_out, deviceId, &fx, (DWORD_PTR)m_event, 0, CALLBACK_EVENT);
        if (r != MMSYSERR_NOERROR) { err = std::string(m_capture ? "waveInOpen: " : "waveOutOpen: ") + MmError(m_capture, r); return false; }
        for (int i = 0; i < kBuffers; ++i) {
            m_data[i].assign(m_bufBytes, 0);
            std::memset(&m_hdr[i], 0, sizeof(WAVEHDR));
            m_hdr[i].lpData = reinterpret_cast<LPSTR>(m_data[i].data());
            m_hdr[i].dwBufferLength = (DWORD)m_bufBytes;
            r = m_capture ? waveInPrepareHeader(m_in, &m_hdr[i], sizeof(WAVEHDR)) : waveOutPrepareHeader(m_out, &m_hdr[i], sizeof(WAVEHDR));
            if (r != MMSYSERR_NOERROR) { m_hdr[i].lpData = nullptr; err = "PrepareHeader: " + MmError(m_capture, r); return false; }
            m_hdr[i].dwFlags |= WHDR_DONE;   // idle: the worker queues it when the device is first un-paused
        }
        if (!m_capture) waveOutPause(m_out);   // a device starts paused
        m_thread = std::thread([this] { Run(); });
        return true;
    }

    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override {
        std::lock_guard<std::mutex> lk(m_mu);
        if (p == m_paused) return;
        m_paused = p;
        if (!p) {
            if (!m_started) { m_started = true; Service(true); }   // first un-pause: queue every buffer
            if (m_capture) waveInStart(m_in); else waveOutRestart(m_out);
        } else {
            if (m_capture) waveInStop(m_in); else waveOutPause(m_out);   // keeps (not drops) what is in flight
        }
    }
    bool Queue(const void* d, uint32_t n) override {
        if (m_capture || m_spec.callback) return false;
        std::lock_guard<std::mutex> lk(m_mu);
        m_ring.Push(d, n);
        return true;
    }
    uint32_t QueuedBytes() const override {
        std::lock_guard<std::mutex> lk(m_mu);
        size_t n = m_ring.Size();
        if (!m_capture && !m_spec.callback) for (int i = 0; i < kBuffers; ++i) if (m_started && !(m_hdr[i].dwFlags & WHDR_DONE)) n += m_valid[i];
        return (uint32_t)n;
    }
    void ClearQueued() override { std::lock_guard<std::mutex> lk(m_mu); m_ring.Clear(); }
    uint32_t Dequeue(void* out, uint32_t max) override {
        std::lock_guard<std::mutex> lk(m_mu);
        return (uint32_t)m_ring.Pop(out, max);
    }

private:
    void Run() {
        while (!m_stop) {
            WaitForSingleObject(m_event, 20);   // the timeout also covers a lost wake-up
            if (m_stop) break;
            std::lock_guard<std::mutex> lk(m_mu);
            if (m_started && !m_paused) Service(false);
        }
    }
    // Refill and re-queue every buffer the driver has finished with (or every buffer, at start). m_mu is held.
    void Service(bool all) {
        for (int i = 0; i < kBuffers; ++i) {
            WAVEHDR& h = m_hdr[i];
            if (!h.lpData || !(h.dwFlags & WHDR_DONE)) continue;
            (void)all;
            if (m_capture) {
                if (!(h.dwFlags & WHDR_INQUEUE) && m_filled[i]) {   // a completed recording: deliver, then give the buffer back
                    const size_t bytes = h.dwBytesRecorded;
                    if (bytes) Deliver(m_data[i].data(), bytes);
                }
                m_filled[i] = true;
                h.dwBytesRecorded = 0;
                h.dwFlags &= ~(DWORD)WHDR_DONE;
                waveInAddBuffer(m_in, &h, sizeof(WAVEHDR));
            } else {
                m_valid[i] = FillPlaybackPeriod(m_data[i].data(), m_bufBytes, m_spec.callback != nullptr,
                    [this](uint8_t* b, size_t n) { m_mu.unlock(); m_spec.callback(m_spec.user, b, (int)n); m_mu.lock(); }, m_ring);
                h.dwFlags &= ~(DWORD)WHDR_DONE;
                waveOutWrite(m_out, &h, sizeof(WAVEHDR));
            }
        }
    }
    void Deliver(uint8_t* data, size_t bytes) {
        if (m_spec.callback) { m_mu.unlock(); m_spec.callback(m_spec.user, data, (int)bytes); m_mu.lock(); }
        else m_ring.Push(data, bytes);   // capped at kCaptureCap: oldest dropped
    }

    AudioSpec m_spec;
    bool m_capture;
    size_t m_frameBytes = 2, m_bufBytes = 0;
    HWAVEOUT m_out = nullptr;
    HWAVEIN m_in = nullptr;
    HANDLE m_event = nullptr;
    WAVEHDR m_hdr[kBuffers];
    std::vector<uint8_t> m_data[kBuffers];
    size_t m_valid[kBuffers] = {0, 0, 0, 0};      // playback: real queued bytes in each buffer (the rest is silence)
    bool m_filled[kBuffers] = {false, false, false, false};
    bool m_started = false, m_paused = true;
    std::atomic<bool> m_stop{false};
    mutable std::mutex m_mu;
    AudioRing m_ring;
    std::thread m_thread;
};

} // namespace

std::unique_ptr<AudioDevice> OpenWaveDevice(const AudioSpec& want, bool capture, const char* deviceName, AudioSpec* got, std::string& err) {
    if (want.channels < 1 || want.channels > 8 || want.freq < 4000 || want.freq > 192000) { err = "unsupported audio format"; return nullptr; }
    if ((capture ? waveInGetNumDevs() : waveOutGetNumDevs()) == 0) { err = "no audio device"; return nullptr; }
    UINT id = WAVE_MAPPER;   // the user's default device
    if (deviceName && *deviceName) {
        const int n = WaveDeviceCount(capture);
        for (int i = 0; i < n; ++i) if (WaveDeviceName(i, capture) == deviceName) { id = (UINT)i; break; }
    }
    AudioSpec spec = want;
    if (spec.frames <= 0) spec.frames = 1024;
    auto dev = std::unique_ptr<WaveDevice>(new WaveDevice(spec, capture));
    if (!dev->Open(id, err)) return nullptr;
    if (got) *got = spec;   // the mapper converts, so the device runs at exactly what was asked
    return dev;
}

int WaveDeviceCount(bool capture) { return (int)(capture ? waveInGetNumDevs() : waveOutGetNumDevs()); }

std::string WaveDeviceName(int index, bool capture) {
    if (index < 0 || index >= WaveDeviceCount(capture)) return std::string();
    wchar_t name[MAXPNAMELEN] = {0};
    if (capture) { WAVEINCAPSW c; if (waveInGetDevCapsW((UINT)index, &c, sizeof(c)) != MMSYSERR_NOERROR) return std::string(); wcsncpy(name, c.szPname, MAXPNAMELEN - 1); }
    else { WAVEOUTCAPSW c; if (waveOutGetDevCapsW((UINT)index, &c, sizeof(c)) != MMSYSERR_NOERROR) return std::string(); wcsncpy(name, c.szPname, MAXPNAMELEN - 1); }
    return text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(name), wcslen(name));
}

} // namespace okay::platform::win32
#endif
