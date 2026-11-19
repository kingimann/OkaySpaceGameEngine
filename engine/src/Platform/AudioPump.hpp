#pragma once
// Pure audio helpers for the own backends. No operating system calls, no <mutex> / <thread>
// (CLAUDE.md rule 21a), so the Windows WASAPI device (OwnWin32Audio.cpp) takes its decisions from
// here and tests/test_platform_audio_pump.cpp checks every one of them on Linux.
//
// Written from the public Microsoft documentation of WAVEFORMATEX / WAVEFORMATEXTENSIBLE, the
// speaker position bit values (ksmedia.h, "KSAUDIO_SPEAKER_*" tables in the Windows Driver Kit
// documentation) and REFERENCE_TIME (100 nanosecond units). Nothing here follows another project.
#include "okay/Platform/Platform.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace okay::platform::pump {

// ---- Time: Windows audio durations are REFERENCE_TIME, 100 ns units -------------------------
constexpr int64_t kHnsPerSecond = 10000000;

/// Frames -> 100 ns units, rounded to nearest. 0 for a non-positive rate.
inline int64_t HnsFromFrames(int64_t frames, int freq) {
    if (freq <= 0 || frames < 0) return 0;
    return (frames * kHnsPerSecond + freq / 2) / freq;
}
/// 100 ns units -> frames, rounded to nearest.
inline int64_t FramesFromHns(int64_t hns, int freq) {
    if (freq <= 0 || hns < 0) return 0;
    return (hns * freq + kHnsPerSecond / 2) / kHnsPerSecond;
}
/// The period the caller asked for, kept to a range a driver can honour.
inline int ClampPeriodFrames(int frames) { return frames < 64 ? 64 : (frames > 16384 ? 16384 : frames); }
/// Buffer length asked of the audio engine: three periods, but never under 20 ms (the shared-mode
/// engine period is 10 ms on most machines, so less than two of those underruns).
inline int64_t BufferDurationHns(int periodFrames, int freq) {
    const int64_t three = HnsFromFrames(3ll * ClampPeriodFrames(periodFrames), freq);
    const int64_t floor = 200000;   // 20 ms
    return three < floor ? floor : three;
}
/// How long the pump thread may wait for the engine's event before looking again: two periods, in
/// whole milliseconds, within 5 to 200 ms.
inline unsigned WaitMs(int periodFrames, int freq) {
    const int64_t ms = HnsFromFrames(2ll * ClampPeriodFrames(periodFrames), freq) / 10000;
    return (unsigned)(ms < 5 ? 5 : (ms > 200 ? 200 : ms));
}

// ---- Formats ----------------------------------------------------------------------------------
constexpr uint16_t kTagPcm = 0x0001, kTagFloat = 0x0003, kTagExtensible = 0xFFFE;

// SPEAKER_* position bits.
constexpr uint32_t kSpkFL = 0x1, kSpkFR = 0x2, kSpkFC = 0x4, kSpkLFE = 0x8, kSpkBL = 0x10, kSpkBR = 0x20,
                   kSpkBC = 0x100, kSpkSL = 0x200, kSpkSR = 0x400;

/// The speaker layout Windows uses for `channels` interleaved channels, 0 when there is no standard one.
inline uint32_t DefaultChannelMask(int channels) {
    switch (channels) {
    case 1: return kSpkFC;
    case 2: return kSpkFL | kSpkFR;
    case 3: return kSpkFL | kSpkFR | kSpkFC;
    case 4: return kSpkFL | kSpkFR | kSpkBL | kSpkBR;
    case 5: return kSpkFL | kSpkFR | kSpkFC | kSpkBL | kSpkBR;
    case 6: return kSpkFL | kSpkFR | kSpkFC | kSpkLFE | kSpkBL | kSpkBR;
    case 7: return kSpkFL | kSpkFR | kSpkFC | kSpkLFE | kSpkBC | kSpkSL | kSpkSR;
    case 8: return kSpkFL | kSpkFR | kSpkFC | kSpkLFE | kSpkBL | kSpkBR | kSpkSL | kSpkSR;
    default: return 0;
    }
}

/// The fields of a WAVEFORMATEX(EXTENSIBLE), without the Windows types so they can be tested anywhere.
struct WaveFormat {
    uint16_t tag = kTagPcm;          // kTagPcm, kTagFloat or kTagExtensible
    bool subFloat = false;           // extensible only: the sub-format is IEEE float (else PCM)
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    uint16_t blockAlign = 0;
    uint32_t avgBytesPerSec = 0;
    uint32_t channelMask = 0;        // extensible only
    bool IsFloat() const { return tag == kTagFloat || (tag == kTagExtensible && subFloat); }
};

/// The format to ask the audio engine for. Up to two channels use the plain tags, more use
/// the extensible form (the documentation requires it). Returns false for a spec no device takes.
inline bool MakeWaveFormat(const AudioSpec& s, WaveFormat* out) {
    if (s.channels < 1 || s.channels > 8 || s.freq < 8000 || s.freq > 384000) return false;
    WaveFormat f;
    const bool fl = s.format == SampleFormat::F32;
    f.channels = (uint16_t)s.channels;
    f.rate = (uint32_t)s.freq;
    f.bits = fl ? 32 : 16;
    f.blockAlign = (uint16_t)(f.channels * f.bits / 8);
    f.avgBytesPerSec = f.rate * f.blockAlign;
    if (s.channels <= 2) f.tag = fl ? kTagFloat : kTagPcm;
    else { f.tag = kTagExtensible; f.subFloat = fl; f.channelMask = DefaultChannelMask(s.channels); }
    *out = f;
    return true;
}

/// What the engine's own mix format means for the caller. When the device refuses the exact format
/// and the caller allows a change, the device runs at its mix format: 16-bit PCM or 32-bit float, any
/// rate and channel count in range. False for anything else (24-bit, 8-bit, 32-bit integer, ...).
inline bool SpecFromMixFormat(const WaveFormat& m, const AudioSpec& want, AudioSpec* out) {
    if (m.channels < 1 || m.channels > 8 || m.rate < 8000 || m.rate > 384000) return false;
    SampleFormat sf;
    if (m.IsFloat() && m.bits == 32) sf = SampleFormat::F32;
    else if (!m.IsFloat() && m.bits == 16) sf = SampleFormat::S16;
    else return false;
    AudioSpec g = want;
    g.freq = (int)m.rate;
    g.channels = m.channels;
    g.format = sf;
    *out = g;
    return true;
}

// ---- Byte queue ---------------------------------------------------------------------------------
/// A growing FIFO of bytes shared by the game thread (Queue, Clear) and the audio thread (Pop). The
/// critical sections are a copy of at most one period, so a spin flag is enough and no mutex header is needed.
class ByteRing {
public:
    uint32_t Size() const { Lock l(m_f); return (uint32_t)m_size; }
    /// Append. With `cap` non-zero, the oldest bytes go when the total would pass it (capture queues).
    void Push(const void* data, uint32_t n, uint32_t cap = 0) {
        if (!n) return;
        Lock l(m_f);
        const uint8_t* b = static_cast<const uint8_t*>(data);
        if (cap && n > cap) { b += n - cap; n = cap; }
        if (cap && m_size + n > cap) Drop((size_t)(m_size + n - cap));
        Reserve(m_size + n);
        size_t tail = (m_head + m_size) % m_buf.size();
        const size_t first = std::min<size_t>(n, m_buf.size() - tail);
        std::memcpy(m_buf.data() + tail, b, first);
        if (first < n) std::memcpy(m_buf.data(), b + first, n - first);
        m_size += n;
    }
    /// Take up to `max` bytes from the front; returns how many.
    uint32_t Pop(void* out, uint32_t max) {
        Lock l(m_f);
        const size_t n = std::min<size_t>(max, m_size);
        if (!n) return 0;
        uint8_t* o = static_cast<uint8_t*>(out);
        const size_t first = std::min(n, m_buf.size() - m_head);
        std::memcpy(o, m_buf.data() + m_head, first);
        if (first < n) std::memcpy(o + first, m_buf.data(), n - first);
        Drop(n);
        return (uint32_t)n;
    }
    void Clear() { Lock l(m_f); m_head = 0; m_size = 0; }

private:
    struct Lock {
        explicit Lock(std::atomic<bool>& f) : m_f(f) { while (m_f.exchange(true, std::memory_order_acquire)) {} }
        ~Lock() { m_f.store(false, std::memory_order_release); }
        std::atomic<bool>& m_f;
    };
    void Drop(size_t n) { m_head = (m_head + n) % (m_buf.empty() ? 1 : m_buf.size()); m_size -= n; if (!m_size) m_head = 0; }
    void Reserve(size_t need) {
        if (need <= m_buf.size()) return;
        size_t cap = m_buf.empty() ? 4096 : m_buf.size();
        while (cap < need) cap *= 2;
        std::vector<uint8_t> nb(cap);
        for (size_t i = 0; i < m_size; ++i) nb[i] = m_buf[(m_head + i) % m_buf.size()];
        m_buf.swap(nb);
        m_head = 0;
    }
    mutable std::atomic<bool> m_f{false};
    std::vector<uint8_t> m_buf;
    size_t m_head = 0, m_size = 0;
};

// ---- What the pump thread does with one buffer ---------------------------------------------
/// Fill `bytes` of playback buffer: from the callback when there is one (it gets silence to start
/// from, like SDL), else from the queue; an underrun is padded with silence. Returns the bytes that
/// came from real data (0 for a callback device: it owns the buffer).
inline uint32_t FillPlayback(const AudioSpec& s, ByteRing& q, uint8_t* dst, uint32_t bytes) {
    if (s.callback) {
        std::memset(dst, 0, bytes);
        s.callback(s.user, dst, (int)bytes);
        return 0;
    }
    const uint32_t got = q.Pop(dst, bytes);
    if (got < bytes) std::memset(dst + got, 0, bytes - got);
    return got;
}
/// Hand captured bytes to the callback or the queue (queue capped at `cap`, oldest dropped).
/// `silent` (the engine's "no data, treat as silence" flag) delivers zeros of the same length.
inline void DeliverCapture(const AudioSpec& s, ByteRing& q, const uint8_t* src, uint32_t bytes, bool silent,
                           std::vector<uint8_t>& scratch, uint32_t cap = 1u << 20) {
    const uint8_t* p = src;
    if (silent || !src) { scratch.assign(bytes, 0); p = scratch.data(); }
    if (s.callback) s.callback(s.user, const_cast<uint8_t*>(p), (int)bytes);
    else q.Push(p, bytes, cap);
}
/// How many frames to take from the engine now: what is free in its buffer, and never a fraction of
/// a frame. `padding` is the frames already waiting to be played.
inline uint32_t FreeFrames(uint32_t bufferFrames, uint32_t padding) { return padding >= bufferFrames ? 0 : bufferFrames - padding; }

} // namespace okay::platform::pump
