// The null audio device: a sound device that needs no sound card, for every backend (Linux, Windows, the web).
// OKAY_AUDIO_DEVICE=null in the environment makes every OpenAudio of the default backend open this instead of the real
// device (CI runners and servers have no sound card; before SDL2 left, SDL_AUDIODRIVER=dummy did this job).
//
// It behaves like a real device that plays into nowhere and records silence, at the real rate:
//   * callback mode: the callback is called from the device's own timer thread, one period (spec.frames frames) every
//     frames / freq seconds, playback with a silent buffer to fill, capture with a silent buffer to read;
//   * queue mode: queued playback bytes drain at freq frames a second; capture fills the queue with silence at that rate;
//   * devices start paused; Pause(true) stops the clock; closing stops and joins the thread.
// Where no thread can be started (the web build without pthreads) the device advances its clock whenever the program
// calls it (Queue, QueuedBytes, Dequeue, Pause), so queue mode still drains at the real rate.
//
// Written for this repo from the AudioDevice contract in okay/Platform/Platform.hpp; the buffer helpers are AudioPump.hpp's.
// CLAUDE.md rule 21a: no <mutex> / <thread> here; the thread, lock and wake-up are the Core shim's (CoreThreads.hpp).
#include "Internal.hpp"
#include "AudioPump.hpp"
#include "../Core/CoreThreads.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace okay::platform {
namespace {

using Clock = std::chrono::steady_clock;
namespace ct = okay::corethreads;

class NullDevice final : public AudioDevice {
public:
    NullDevice(const AudioSpec& s, bool capture)
        : m_spec(s), m_capture(capture),
          m_periodBytes((uint32_t)s.frames * (uint32_t)(BytesPerSample(s.format) * s.channels)),
          m_period(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>((double)s.frames / (double)s.freq))) {
        m_buf.resize(m_periodBytes);
        m_threaded = m_thread.Start(&NullDevice::ThreadMain, this);
    }
    ~NullDevice() override {
        {
            ct::LockGuard g(m_mu);
            m_quit = true;
            m_wake.NotifyAll();
        }
        m_thread.Join();
    }

    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool paused) override {
        StepIfNoThread();
        ct::LockGuard g(m_mu);
        if (m_paused && !paused) m_next = Clock::now() + m_period;   // the first period ends one period from now
        m_paused = paused;
        m_wake.NotifyAll();
    }
    bool Queue(const void* data, uint32_t bytes) override {
        if (m_capture) return false;
        if (!data || !bytes) return bytes == 0;
        StepIfNoThread();
        m_q.Push(data, bytes);
        return true;
    }
    uint32_t QueuedBytes() const override { const_cast<NullDevice*>(this)->StepIfNoThread(); return m_q.Size(); }
    void ClearQueued() override { m_q.Clear(); }
    uint32_t Dequeue(void* out, uint32_t maxBytes) override {
        if (!m_capture || !out) return 0;
        StepIfNoThread();
        return m_q.Pop(out, maxBytes);
    }

private:
    static void ThreadMain(void* p) { static_cast<NullDevice*>(p)->Run(); }

    /// One period: playback takes its bytes from the callback or the queue (and throws them away), capture hands over silence.
    void OnePeriod() {
        if (m_capture) pump::DeliverCapture(m_spec, m_q, nullptr, m_periodBytes, true, m_scratch);
        else pump::FillPlayback(m_spec, m_q, m_buf.data(), m_periodBytes);
    }

    void Run() {
        m_mu.Lock();
        while (!m_quit) {
            if (m_paused) { m_wake.WaitFor(m_mu, 1.0); continue; }
            const Clock::time_point now = Clock::now();
            if (now < m_next) {
                m_wake.WaitFor(m_mu, std::chrono::duration<double>(m_next - now).count());
                continue;
            }
            // Never run more than a few periods behind (the machine was suspended, the thread starved): skip ahead instead.
            if (now - m_next > 4 * m_period) m_next = now;
            m_next += m_period;
            m_mu.Unlock();   // the callback runs without our lock, so it may call Pause or QueuedBytes
            OnePeriod();
            m_mu.Lock();
        }
        m_mu.Unlock();
    }

    /// No thread (web without pthreads): do the periods that are due by now, on the caller's thread.
    void StepIfNoThread() {
        if (m_threaded || m_paused) return;
        const Clock::time_point now = Clock::now();
        if (now - m_next > 4 * m_period) m_next = now;
        while (m_next <= now) { m_next += m_period; OnePeriod(); }
    }

    const AudioSpec m_spec;
    const bool m_capture;
    const uint32_t m_periodBytes;
    const Clock::duration m_period;
    pump::ByteRing m_q;
    std::vector<uint8_t> m_buf, m_scratch;   // used by the thread only (or the caller when there is none)
    ct::Mutex m_mu;
    ct::CondVar m_wake;
    bool m_paused = true;                    // devices start paused; m_mu
    bool m_quit = false;                     // m_mu
    Clock::time_point m_next;                // when the next period is due; m_mu
    bool m_threaded = false;
    ct::Thread m_thread;                     // last: started after every member it reads exists, joined in the destructor
};

} // namespace

bool NullAudioRequested() {
    const char* e = std::getenv("OKAY_AUDIO_DEVICE");
    return e && std::strcmp(e, "null") == 0;
}

std::unique_ptr<AudioDevice> OpenNullAudio(const AudioSpec& want, bool capture, AudioSpec* got, std::string& err) {
    if (want.freq < 8000 || want.freq > 384000 || want.channels < 1 || want.channels > 8) {
        err = "unsupported audio format (channels or rate out of range)";
        return nullptr;
    }
    AudioSpec s = want;
    s.frames = pump::ClampPeriodFrames(want.frames > 0 ? want.frames : 1024);
    if (got) *got = s;
    return std::unique_ptr<AudioDevice>(new NullDevice(s, capture));
}

} // namespace okay::platform
