#pragma once
// Microphone capture for the Xbox UWP build, as pure code (no Windows headers, no <thread>/<mutex>; tested on Linux over a fake host by
// tests/test_xbox_uwp_capture.cpp). The Windows side (XboxWasapiCapture.hpp, uncompiled) only opens an IAudioClient on the default capture
// endpoint and calls UwpCaptureDevice::Deliver from its capture thread; everything decided here:
//   * who may capture: the `microphone` capability must be in the package manifest AND the user's consent must not be "denied"
//     (Settings > Privacy; on a console the system asks the first time). EvaluateMicPermission() says Granted / Unknown (ask by trying) /
//     Denied with the reason.
//   * the device list and open / close through a CaptureHost; the engine's capture spec (platform::AudioSpec: S16 or F32, any channel
//     count, any rate) is met by converting whatever the device delivers (16-bit PCM or 32-bit float, any rate and channel count):
//     channel mapping with MapChannels (XboxAudio.hpp), linear-interpolation resampling that keeps its state between chunks.
//   * the queue and the callback are the engine's own capture plumbing, platform::pump::DeliverCapture and pump::ByteRing
//     (engine/src/Platform/AudioPump.hpp, the same code the own Win32 device's ServiceCapture uses): the queue is capped (oldest dropped),
//     a "silent" packet becomes zeros of the same length.
//   * underrun: Dequeue returns what is there (the engine's queue-mode rule); DequeueFilled pads the rest with silence and counts it.
// Written from Microsoft's documentation of WASAPI capture, the `microphone` capability and the C++ standard. UNVERIFIED on Windows and a
// console (no microphone was ever opened by this code).
#include "XboxAudio.hpp"
#include "../../engine/src/Platform/AudioPump.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::xbox {

/// What the system says about the user's microphone consent for this app.
enum class MicConsent { Unknown, Allowed, Denied };
enum class MicPermission { Granted, Unknown, Denied };

struct MicPermissionResult { MicPermission state = MicPermission::Denied; std::string reason; };

/// `capabilityDeclared`: the manifest has <DeviceCapability Name="microphone"/> (XboxManifest.hpp). Without it the system never lets the
/// app record, so asking is pointless. Denied consent is final until the user changes it in Settings. Unknown = nobody has decided yet:
/// opening the device is what makes the system ask.
inline MicPermissionResult EvaluateMicPermission(bool capabilityDeclared, MicConsent consent) {
    if (!capabilityDeclared) return {MicPermission::Denied, "The app's manifest does not declare the microphone capability."};
    if (consent == MicConsent::Denied) return {MicPermission::Denied, "Microphone access is turned off for this app in the system's privacy settings."};
    if (consent == MicConsent::Allowed) return {MicPermission::Granted, std::string()};
    return {MicPermission::Unknown, std::string()};
}

/// The format the device delivers to Deliver().
struct CaptureFormat { int rate = 48000; int channels = 1; bool isFloat = true; int bits = 32; };
inline bool SupportedCaptureFormat(const CaptureFormat& f) {
    if (f.channels < 1 || f.channels > 8 || f.rate < 8000 || f.rate > 384000) return false;
    return f.isFloat ? f.bits == 32 : f.bits == 16;
}

class UwpCaptureDevice;
/// The Windows side. Start must make the capture thread call dev->Deliver; Stop must not return until it is out of Deliver.
struct CaptureHost {
    virtual ~CaptureHost() = default;
    virtual int DeviceCount() = 0;
    virtual std::string DeviceName(int index) = 0;
    virtual bool Format(int index, CaptureFormat& out) = 0;                       // the device's mix format
    virtual bool CapabilityDeclared() = 0;                                        // the manifest asks for `microphone`
    virtual MicConsent Consent() = 0;                                             // the system's answer so far
    virtual bool Start(UwpCaptureDevice* dev, int index, std::string& err) = 0;
    virtual void Stop(UwpCaptureDevice* dev) = 0;
};

class UwpCaptureDevice : public platform::AudioDevice {
public:
    UwpCaptureDevice(const platform::AudioSpec& want, const CaptureFormat& in, CaptureHost* host, int index)
        : spec_(want), in_(in), host_(host), index_(index) {
        if (spec_.channels < 1) spec_.channels = 1;
        if (spec_.freq < 8000) spec_.freq = 8000;
        if (spec_.frames < 64) spec_.frames = 1024;
    }
    ~UwpCaptureDevice() override { if (host_ && started_) host_->Stop(this); }
    bool Begin(std::string& err) { if (!host_) return true; started_ = host_->Start(this, index_, err); return started_; }

    const platform::AudioSpec& Spec() const override { return spec_; }
    void Pause(bool p) override { paused_.store(p, std::memory_order_release); }
    bool Queue(const void*, std::uint32_t) override { return false; }            // a capture device has nothing to play
    std::uint32_t QueuedBytes() const override { return q_.Size(); }
    void ClearQueued() override { q_.Clear(); }
    std::uint32_t Dequeue(void* out, std::uint32_t maxBytes) override { return q_.Pop(out, maxBytes); }
    /// Exactly `bytes` bytes: what is queued, then silence for the rest (counted in UnderrunBytes). For a reader that must never wait.
    std::uint32_t DequeueFilled(void* out, std::uint32_t bytes) {
        const std::uint32_t got = q_.Pop(out, bytes);
        if (got < bytes) { std::memset(static_cast<std::uint8_t*>(out) + got, 0, bytes - got); underrun_ += bytes - got; }
        return got;
    }
    std::uint64_t UnderrunBytes() const { return underrun_; }

    /// Capture thread: `bytes` of the device's format (whole frames; a partial frame at the end is ignored). `silent` = the engine's
    /// "no data, treat as silence" flag. Dropped while paused (a paused microphone records nothing).
    void Deliver(const void* data, std::uint32_t bytes, bool silent) {
        if (paused_.load(std::memory_order_acquire)) return;
        const std::size_t bps = in_.isFloat ? 4u : 2u;
        const std::size_t inFrame = (std::size_t)in_.channels * bps;
        const std::size_t frames = bytes / inFrame;
        if (!frames) return;
        // 1. device samples -> float frames with the spec's channel count
        const int dc = spec_.channels;
        std::vector<float> tmp((std::size_t)in_.channels);
        const std::size_t at = pend_.size(); pend_.resize(at + frames * (std::size_t)dc);
        const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < frames; ++i) {
            for (int c = 0; c < in_.channels; ++c) {
                float v = 0.0f;
                if (!silent && p) {
                    const std::uint8_t* s = p + i * inFrame + (std::size_t)c * bps;
                    if (in_.isFloat) std::memcpy(&v, s, 4); else { std::int16_t t; std::memcpy(&t, s, 2); v = S16ToF32(t); }
                    if (!(v == v)) v = 0.0f;   // a NaN from a broken driver would poison the resampler
                }
                tmp[(std::size_t)c] = v;
            }
            MapChannels(tmp.data(), in_.channels, &pend_[at + i * (std::size_t)dc], dc);
        }
        // 2. resample (linear; position and unread frames carry over to the next call)
        const std::size_t have = pend_.size() / (std::size_t)dc;
        std::vector<float> outF;
        std::size_t produced = 0;
        for (;;) {
            const std::size_t i0 = (std::size_t)(pos_ / (std::uint64_t)spec_.freq);
            if (i0 + 1 >= have) break;
            const float f = (float)(pos_ % (std::uint64_t)spec_.freq) / (float)spec_.freq;
            for (int c = 0; c < dc; ++c) outF.push_back(pend_[i0 * (std::size_t)dc + (std::size_t)c] * (1.0f - f) + pend_[(i0 + 1) * (std::size_t)dc + (std::size_t)c] * f);
            pos_ += (std::uint64_t)in_.rate; ++produced;
        }
        std::size_t drop = (std::size_t)(pos_ / (std::uint64_t)spec_.freq); if (drop > have) drop = have;
        pend_.erase(pend_.begin(), pend_.begin() + (std::ptrdiff_t)(drop * (std::size_t)dc));
        pos_ -= (std::uint64_t)drop * (std::uint64_t)spec_.freq;
        if (!produced) return;
        // 3. float -> the spec's sample format, then the engine's own delivery (callback or capped queue)
        const bool f32 = spec_.format == platform::SampleFormat::F32;
        std::vector<std::uint8_t> bytesOut(outF.size() * (f32 ? 4u : 2u));
        for (std::size_t i = 0; i < outF.size(); ++i) {
            if (f32) std::memcpy(&bytesOut[i * 4], &outF[i], 4);
            else { const std::int16_t s = F32ToS16(outF[i]); std::memcpy(&bytesOut[i * 2], &s, 2); }
        }
        platform::pump::DeliverCapture(spec_, q_, bytesOut.data(), (std::uint32_t)bytesOut.size(), false, scratch_);
    }

private:
    platform::AudioSpec spec_; CaptureFormat in_; CaptureHost* host_; int index_; bool started_ = false;
    platform::pump::ByteRing q_; std::vector<std::uint8_t> scratch_;
    std::atomic<bool> paused_{true};
    std::vector<float> pend_; std::uint64_t pos_ = 0;   // capture thread only; pos_ / freq = the next output frame's source position (exact integer maths: no drift)
    std::uint64_t underrun_ = 0;                              // reader thread only
};

} // namespace okay::xbox
