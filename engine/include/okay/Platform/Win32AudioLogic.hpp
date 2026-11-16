#pragma once
// okay::platform::wl: the plain-logic half of the own Windows audio device (waveOut / waveIn in
// OwnWin32.cpp): the byte queue, the buffer plan, the format check and the device-name match. No
// Windows header, no thread, no lock: the device wraps the queue in a Win32 CRITICAL_SECTION itself,
// and these pieces are checked by tests/test_platform_win32_logic.cpp on Linux.
//
// Written from Microsoft's documentation of WAVEFORMATEX (wFormatTag, nChannels, nSamplesPerSec,
// nAvgBytesPerSec, nBlockAlign, wBitsPerSample), of WAVEHDR buffer cycling and of WAVEOUTCAPS
// (the 31-character product name), and from general knowledge of ring buffers. Nothing copied from
// SDL, PortAudio, miniaudio or any other project.
#include "okay/Platform/Platform.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::platform::wl {

/// A growable byte FIFO on a ring buffer (queue mode playback, queue mode capture). With a `cap`, a
/// Push that would pass it drops the OLDEST bytes (a capture that nobody reads keeps the newest
/// audio); cap 0 = unbounded (playback: the caller decides how much to queue).
class ByteFifo {
public:
    explicit ByteFifo(std::size_t cap = 0) : m_cap(cap) {}
    /// Appends; returns how many old bytes were dropped to stay within the cap.
    std::size_t Push(const void* data, std::size_t n) {
        const std::uint8_t* b = static_cast<const std::uint8_t*>(data);
        std::size_t dropped = 0;
        if (m_cap && n >= m_cap) {   // the new data alone fills the cap: keep its newest `cap` bytes
            dropped = m_size + (n - m_cap);
            Clear();
            b += n - m_cap;
            n = m_cap;
        } else if (m_cap && m_size + n > m_cap) {
            dropped = m_size + n - m_cap;
            Discard(dropped);
        }
        if (n == 0) return dropped;
        Reserve(m_size + n);
        const std::size_t tail = (m_head + m_size) % m_buf.size();
        const std::size_t first = std::min(n, m_buf.size() - tail);
        std::memcpy(m_buf.data() + tail, b, first);
        if (n > first) std::memcpy(m_buf.data(), b + first, n - first);
        m_size += n;
        return dropped;
    }
    /// Removes up to `n` bytes into `out` (may be null to just discard); returns how many.
    std::size_t Pop(void* out, std::size_t n) {
        n = std::min(n, m_size);
        if (out && n) {
            std::uint8_t* o = static_cast<std::uint8_t*>(out);
            const std::size_t first = std::min(n, m_buf.size() - m_head);
            std::memcpy(o, m_buf.data() + m_head, first);
            if (n > first) std::memcpy(o + first, m_buf.data(), n - first);
        }
        Discard(n);
        return n;
    }
    std::size_t Size() const { return m_size; }
    void Clear() { m_head = 0; m_size = 0; }
private:
    void Discard(std::size_t n) {
        n = std::min(n, m_size);
        m_head = m_buf.empty() ? 0 : (m_head + n) % m_buf.size();
        m_size -= n;
        if (m_size == 0) m_head = 0;
    }
    void Reserve(std::size_t need) {
        if (need <= m_buf.size()) return;
        std::size_t cap = m_buf.empty() ? 4096 : m_buf.size();
        while (cap < need) cap *= 2;
        std::vector<std::uint8_t> nb(cap);
        // Unroll the ring into the new buffer.
        const std::size_t first = std::min(m_size, m_buf.size() - m_head);
        if (m_size) {
            std::memcpy(nb.data(), m_buf.data() + m_head, first);
            if (m_size > first) std::memcpy(nb.data() + first, m_buf.data(), m_size - first);
        }
        m_buf.swap(nb);
        m_head = 0;
    }
    std::size_t m_cap;
    std::size_t m_head = 0, m_size = 0;
    std::vector<std::uint8_t> m_buf;
};

/// Fills one playback buffer of `bytes` from the queue: the queued bytes first, then silence (an
/// underrun plays silence, never old data). Returns how many real bytes were used.
inline std::size_t FillFromFifo(ByteFifo& q, std::uint8_t* buf, std::size_t bytes) {
    const std::size_t took = q.Pop(buf, bytes);
    if (took < bytes) std::memset(buf + took, 0, bytes - took);   // 0 is silence for 16-bit integer and for float
    return took;
}

inline int FrameBytes(const AudioSpec& s) { return BytesPerSample(s.format) * s.channels; }

/// How a device's audio is cut into buffers handed to waveOutWrite / waveInAddBuffer.
struct BufferPlan {
    int frames = 0;        // frames per buffer
    int bytes = 0;         // bytes per buffer
    int count = 0;         // number of buffers cycling through the device
    int latencyMs = 0;     // all buffers together, in milliseconds
};
/// `want.frames` is a hint (0 = 1024). Per-buffer frames are rounded up to a multiple of 16 and kept
/// within 64 .. 16384; the count is chosen so all buffers hold at least 60 ms, between 3 and 8, so one
/// late wake-up of the feeding thread does not cause an underrun.
inline BufferPlan PlanBuffers(const AudioSpec& want) {
    BufferPlan p;
    int frames = want.frames > 0 ? want.frames : 1024;
    frames = (frames + 15) / 16 * 16;
    frames = std::min(16384, std::max(64, frames));
    const int freq = want.freq > 0 ? want.freq : 48000;
    long long count = ((long long)freq * 60 / 1000 + frames - 1) / frames;
    count = std::min<long long>(8, std::max<long long>(3, count));
    p.frames = frames;
    p.count = (int)count;
    p.bytes = frames * std::max(1, FrameBytes(want));
    p.latencyMs = (int)((long long)frames * p.count * 1000 / freq);
    return p;
}

/// The fields of a WAVEFORMATEX for a spec: 16-bit integer PCM (tag 1) or 32-bit float (tag 3).
struct WaveFormatFields {
    unsigned short tag = 1;
    unsigned short channels = 0;
    unsigned long samplesPerSec = 0;
    unsigned long avgBytesPerSec = 0;
    unsigned short blockAlign = 0;
    unsigned short bitsPerSample = 0;
};
constexpr unsigned short kWaveFormatPcm = 1, kWaveFormatIeeeFloat = 3;

inline WaveFormatFields WaveFormatFor(const AudioSpec& s) {
    WaveFormatFields f;
    f.tag = s.format == SampleFormat::F32 ? kWaveFormatIeeeFloat : kWaveFormatPcm;
    f.channels = (unsigned short)s.channels;
    f.samplesPerSec = (unsigned long)s.freq;
    f.bitsPerSample = (unsigned short)(BytesPerSample(s.format) * 8);
    f.blockAlign = (unsigned short)(f.channels * (f.bitsPerSample / 8));
    f.avgBytesPerSec = f.samplesPerSec * f.blockAlign;
    return f;
}

/// What the device can really do, given what was asked. waveOut's default device (the wave mapper)
/// converts sample rates and channel counts itself, so any rate from 8 kHz to 192 kHz is accepted;
/// the channel count must be 1 or 2 (more needs WAVEFORMATEXTENSIBLE, which is not written). With
/// `want.allowChange` an out-of-range value is replaced by the nearest supported one; without it the
/// spec is refused and `why` says which field. Returns false on refusal.
inline bool NegotiateSpec(const AudioSpec& want, AudioSpec& got, std::string& why) {
    got = want;
    if (want.freq < 8000 || want.freq > 192000) {
        if (!want.allowChange) { why = "sample rate " + std::to_string(want.freq) + " is outside 8000 .. 192000"; return false; }
        got.freq = want.freq < 8000 ? 8000 : 192000;
    }
    if (want.channels < 1 || want.channels > 2) {
        if (!want.allowChange) { why = "channel count " + std::to_string(want.channels) + " is not 1 or 2"; return false; }
        got.channels = want.channels < 1 ? 1 : 2;
    }
    got.frames = PlanBuffers(want).frames;
    return true;
}

/// waveOut and waveIn keep a product name of at most 31 characters (WAVEOUTCAPS::szPname). A requested
/// name matches a device when it equals the device's name, or when the device's name is a full-length
/// (31 character) cut of the requested one.
inline bool WaveNameMatches(const std::string& requested, const std::string& deviceName) {
    if (requested == deviceName) return true;
    return deviceName.size() >= 31 && requested.size() > deviceName.size() &&
           requested.compare(0, deviceName.size(), deviceName) == 0;
}

/// Device index to open for `name`: -1 (the system default, WAVE_MAPPER) for null, "" or "default";
/// otherwise the index into `devices` of the first match; -2 when there is no such device.
inline int ResolveWaveDevice(const char* name, const std::vector<std::string>& devices) {
    if (!name || !*name || std::string(name) == "default") return -1;
    for (std::size_t i = 0; i < devices.size(); ++i)
        if (WaveNameMatches(name, devices[i])) return (int)i;
    return -2;
}

} // namespace okay::platform::wl
