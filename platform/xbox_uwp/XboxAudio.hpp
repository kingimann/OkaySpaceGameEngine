#pragma once
// Audio output for the Xbox UWP build, as pure code (no Windows headers, no <thread>/<mutex>): a lock-free single-producer /
// single-consumer byte ring, sample-format / channel / rate conversion, and a platform::AudioDevice that the WASAPI render thread
// pulls from (XboxWasapi.hpp is the Windows-only part). The mixer (AudioMixer::RenderStereo) is NOT touched: it keeps producing 16-bit
// stereo through platform::AudioDevice exactly as on the desktop; this device converts to whatever the Windows mix format is.
// Written from Microsoft's description of WASAPI shared mode (the engine's mix format is usually 32-bit float, 48 kHz) and the C++ standard.
// Linear interpolation is our own simple resampler. UNVERIFIED on Windows; the conversion is tested on Linux.
#include "okay/Platform/Platform.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::xbox {

/// Lock-free ring for ONE producer thread (Write) and ONE consumer thread (Read). Capacity is rounded up to a power of two.
class ByteRing {
public:
    explicit ByteRing(std::size_t minCapacity = 1 << 16) { std::size_t c = 16; while (c < minCapacity) c <<= 1; buf_.assign(c, 0); mask_ = c - 1; }
    std::size_t Capacity() const { return buf_.size(); }
    std::size_t Size() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }
    std::size_t Free() const { return Capacity() - Size(); }
    /// Writes all `n` bytes or none (returns false when there is no room).
    bool Write(const void* data, std::size_t n) {
        const std::size_t w = w_.load(std::memory_order_relaxed);
        if (n > Capacity() - (w - r_.load(std::memory_order_acquire))) return false;
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) buf_[(w + i) & mask_] = p[i];
        w_.store(w + n, std::memory_order_release);
        return true;
    }
    /// Reads up to `n` bytes; returns how many.
    std::size_t Read(void* out, std::size_t n) {
        const std::size_t r = r_.load(std::memory_order_relaxed);
        const std::size_t avail = w_.load(std::memory_order_acquire) - r;
        if (n > avail) n = avail;
        unsigned char* p = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < n; ++i) p[i] = buf_[(r + i) & mask_];
        r_.store(r + n, std::memory_order_release);
        return n;
    }
    /// Consumer side only: drop everything queued.
    void ConsumerClear() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }
private:
    std::vector<unsigned char> buf_; std::size_t mask_ = 0;
    std::atomic<std::size_t> w_{0}, r_{0};
};

inline float S16ToF32(std::int16_t v) { return v < 0 ? v / 32768.0f : v / 32767.0f; }
inline std::int16_t F32ToS16(float f) { if (!(f > -1.0f)) return f != f ? 0 : -32768; if (f >= 1.0f) return 32767; return (std::int16_t)std::lrintf(f < 0 ? f * 32768.0f : f * 32767.0f); }

/// Map one frame of `srcCh` channels to `dstCh` channels. mono -> every channel (L/R) , stereo -> mono is the average,
/// stereo -> more channels fills front left / right and leaves the rest silent (centre, LFE, surround), anything else copies the
/// common channels and zeroes the rest.
inline void MapChannels(const float* src, int srcCh, float* dst, int dstCh) {
    if (srcCh == dstCh) { for (int c = 0; c < dstCh; ++c) dst[c] = src[c]; return; }
    if (srcCh == 1) { for (int c = 0; c < dstCh; ++c) dst[c] = c < 2 ? src[0] : 0.0f; return; }
    if (dstCh == 1) { float s = 0; for (int c = 0; c < srcCh && c < 2; ++c) s += src[c]; dst[0] = srcCh >= 2 ? s * 0.5f : s; return; }
    for (int c = 0; c < dstCh; ++c) dst[c] = c < srcCh ? src[c] : 0.0f;
}

struct AudioOutputFormat { int rate = 48000; int channels = 2; };

class UwpAudioDevice;
/// The Windows side: starts / stops the render thread that calls UwpAudioDevice::Render. XboxWasapi.hpp implements it.
struct AudioHost {
    virtual ~AudioHost() = default;
    virtual bool Format(AudioOutputFormat& out) = 0;                 // the device's mix format
    virtual bool Start(UwpAudioDevice* dev, std::string& err) = 0;   // begin calling dev->Render from the render thread
    virtual void Stop(UwpAudioDevice* dev) = 0;                      // must not return until the render thread is no longer inside Render
};

class UwpAudioDevice : public platform::AudioDevice {
public:
    UwpAudioDevice(const platform::AudioSpec& want, const AudioOutputFormat& out, AudioHost* host)
        : spec_(want), out_(out), host_(host), ring_(std::size_t(want.freq > 0 ? want.freq : 48000) * (std::size_t)(want.channels > 0 ? want.channels : 2) * 4 * 2) {
        if (spec_.channels < 1) spec_.channels = 1;
        if (spec_.freq < 8000) spec_.freq = 8000;
        if (spec_.frames < 64) spec_.frames = 1024;
        srcStep_ = double(spec_.freq) / double(out_.rate > 0 ? out_.rate : 48000);
    }
    ~UwpAudioDevice() override { if (host_) host_->Stop(this); }
    bool Begin(std::string& err) { return !host_ || host_->Start(this, err); }

    const platform::AudioSpec& Spec() const override { return spec_; }
    void Pause(bool p) override { paused_.store(p, std::memory_order_release); }
    bool Queue(const void* data, std::uint32_t bytes) override {
        if (spec_.callback) return false;                 // callback devices are fed by their callback
        const std::size_t frameBytes = (std::size_t)spec_.channels * platform::BytesPerSample(spec_.format);
        bytes -= bytes % (std::uint32_t)frameBytes;       // whole frames only
        return ring_.Write(data, bytes);
    }
    std::uint32_t QueuedBytes() const override { return clearReq_.load(std::memory_order_acquire) ? 0u : (std::uint32_t)ring_.Size(); }
    void ClearQueued() override { clearReq_.store(true, std::memory_order_release); }
    std::uint32_t Dequeue(void*, std::uint32_t) override { return 0; }   // capture is not supported

    /// Render thread: fill `frames` frames of float32, `out_.channels` interleaved. Always fills the whole buffer (silence when paused or starved).
    void Render(float* dst, int frames) {
        const int dc = out_.channels;
        if (frames <= 0) return;
        if (clearReq_.exchange(false, std::memory_order_acq_rel)) { ring_.ConsumerClear(); pend_.clear(); pos_ = 0; }
        if (paused_.load(std::memory_order_acquire)) { std::memset(dst, 0, sizeof(float) * (std::size_t)frames * (std::size_t)dc); return; }
        const int sc = spec_.channels;
        // source frames needed: the last output frame reads frame floor(pos + (frames-1)*step) and the next one
        const std::size_t need = (std::size_t)std::floor(pos_ + (frames - 1) * srcStep_) + 2;
        while (pend_.size() / (std::size_t)sc < need) {
            if (!Fetch()) break;
        }
        const std::size_t have = pend_.size() / (std::size_t)sc;
        std::vector<float> frame((std::size_t)sc);
        for (int i = 0; i < frames; ++i) {
            const double p = pos_ + i * srcStep_;
            const std::size_t i0 = (std::size_t)p;
            if (i0 >= have) { for (int c = 0; c < dc; ++c) dst[(std::size_t)i * (std::size_t)dc + (std::size_t)c] = 0.0f; continue; }   // starved
            const float f = (float)(p - (double)i0);
            const std::size_t i1 = i0 + 1 < have ? i0 + 1 : i0;
            for (int c = 0; c < sc; ++c) frame[(std::size_t)c] = pend_[i0 * (std::size_t)sc + (std::size_t)c] * (1.0f - f) + pend_[i1 * (std::size_t)sc + (std::size_t)c] * f;
            MapChannels(frame.data(), sc, dst + (std::size_t)i * (std::size_t)dc, dc);
        }
        const double end = pos_ + frames * srcStep_;
        std::size_t drop = (std::size_t)end; if (drop > have) drop = have;
        pend_.erase(pend_.begin(), pend_.begin() + (std::ptrdiff_t)(drop * (std::size_t)sc));
        pos_ = end - (double)drop; if (pos_ < 0) pos_ = 0;
    }
    std::uint64_t UnderrunFrames() const { return underrun_; }

private:
    // Append source frames to pend_: one callback block, or what the queue holds. False = nothing more is available right now.
    bool Fetch() {
        const int sc = spec_.channels; const std::size_t bps = (std::size_t)platform::BytesPerSample(spec_.format);
        const std::size_t frameBytes = (std::size_t)sc * bps;
        std::vector<unsigned char>& raw = raw_;
        if (spec_.callback) {
            raw.assign((std::size_t)spec_.frames * frameBytes, 0);
            spec_.callback(spec_.user, raw.data(), (int)raw.size());
        } else {
            const std::size_t avail = ring_.Size();
            if (avail < frameBytes) { underrun_ += 1; return false; }
            std::size_t n = avail < (std::size_t)spec_.frames * frameBytes ? avail : (std::size_t)spec_.frames * frameBytes;
            n -= n % frameBytes;
            raw.resize(n); ring_.Read(raw.data(), n);
        }
        const std::size_t samples = raw.size() / bps;
        const std::size_t at = pend_.size(); pend_.resize(at + samples);
        if (spec_.format == platform::SampleFormat::F32) std::memcpy(&pend_[at], raw.data(), samples * sizeof(float));
        else for (std::size_t i = 0; i < samples; ++i) { std::int16_t v; std::memcpy(&v, &raw[i * 2], 2); pend_[at + i] = S16ToF32(v); }
        return true;
    }
    platform::AudioSpec spec_; AudioOutputFormat out_; AudioHost* host_;
    ByteRing ring_;
    std::atomic<bool> paused_{true}, clearReq_{false};
    std::vector<float> pend_; std::vector<unsigned char> raw_; double pos_ = 0, srcStep_ = 1.0; std::uint64_t underrun_ = 0;
};

} // namespace okay::xbox
