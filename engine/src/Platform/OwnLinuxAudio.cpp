// Own Linux backend, audio part: ALSA through dlopen (libasound.so.2), no link
// dependency and no ALSA headers (the handful of calls are declared here).
// One thread per device does the blocking read / write and drives either the user's
// callback or the queue, so the interface matches SDL's audio device model.
#include "OwnLinux.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <vector>

namespace okay::platform::own {
namespace {

struct snd_pcm_t;
using snd_pcm_sframes_t = long;
using snd_pcm_uframes_t = unsigned long;

struct AlsaApi {
    void* lib = nullptr;
    int (*snd_pcm_open)(snd_pcm_t**, const char*, int, int) = nullptr;
    int (*snd_pcm_close)(snd_pcm_t*) = nullptr;
    int (*snd_pcm_set_params)(snd_pcm_t*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
    snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t*, const void*, snd_pcm_uframes_t) = nullptr;
    snd_pcm_sframes_t (*snd_pcm_readi)(snd_pcm_t*, void*, snd_pcm_uframes_t) = nullptr;
    int (*snd_pcm_recover)(snd_pcm_t*, int, int) = nullptr;
    int (*snd_pcm_prepare)(snd_pcm_t*) = nullptr;
    int (*snd_pcm_start)(snd_pcm_t*) = nullptr;
    int (*snd_pcm_drop)(snd_pcm_t*) = nullptr;
    const char* (*snd_strerror)(int) = nullptr;
    int (*snd_device_name_hint)(int, const char*, void***) = nullptr;
    char* (*snd_device_name_get_hint)(const void*, const char*) = nullptr;
    int (*snd_device_name_free_hint)(void**) = nullptr;
};

constexpr int kPlayback = 0, kCapture = 1;           // snd_pcm_stream_t
constexpr int kAccessRwInterleaved = 3;               // SND_PCM_ACCESS_RW_INTERLEAVED
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr int kFmtS16 = 3, kFmtF32 = 15;              // S16_BE, FLOAT_BE
#else
constexpr int kFmtS16 = 2, kFmtF32 = 14;              // S16_LE, FLOAT_LE
#endif

const AlsaApi* Api(std::string& err) {
    static AlsaApi api;
    static bool tried = false, ok = false;
    static std::string why;
    if (!tried) {
        tried = true;
        api.lib = dlopen("libasound.so.2", RTLD_NOW);
        if (!api.lib) { why = "libasound.so.2 not found"; }
        else {
#define OKAY_SYM(n) api.n = reinterpret_cast<decltype(api.n)>(dlsym(api.lib, #n)); if (!api.n) { why = "libasound lacks " #n; }
            OKAY_SYM(snd_pcm_open) OKAY_SYM(snd_pcm_close) OKAY_SYM(snd_pcm_set_params)
            OKAY_SYM(snd_pcm_writei) OKAY_SYM(snd_pcm_readi) OKAY_SYM(snd_pcm_recover)
            OKAY_SYM(snd_pcm_prepare) OKAY_SYM(snd_pcm_start) OKAY_SYM(snd_pcm_drop) OKAY_SYM(snd_strerror)
#undef OKAY_SYM
            // The device-list calls are optional: without them only "default" is offered.
            api.snd_device_name_hint = reinterpret_cast<decltype(api.snd_device_name_hint)>(dlsym(api.lib, "snd_device_name_hint"));
            api.snd_device_name_get_hint = reinterpret_cast<decltype(api.snd_device_name_get_hint)>(dlsym(api.lib, "snd_device_name_get_hint"));
            api.snd_device_name_free_hint = reinterpret_cast<decltype(api.snd_device_name_free_hint)>(dlsym(api.lib, "snd_device_name_free_hint"));
            ok = why.empty();
        }
    }
    if (!ok) { err = why; return nullptr; }
    return &api;
}

class AlsaDevice final : public AudioDevice {
public:
    AlsaDevice(const AlsaApi* api, snd_pcm_t* pcm, const AudioSpec& spec, bool capture)
        : m_api(api), m_pcm(pcm), m_spec(spec), m_capture(capture) {
        m_frameBytes = BytesPerSample(spec.format) * spec.channels;
        m_thread = std::thread([this] { Run(); });
    }
    ~AlsaDevice() override {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
        m_api->snd_pcm_close(m_pcm);
    }
    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override { m_paused = p; }
    bool Queue(const void* d, uint32_t n) override {
        if (m_capture || m_spec.callback) return false;
        std::lock_guard<std::mutex> lk(m_mu);
        const uint8_t* b = static_cast<const uint8_t*>(d);
        m_queue.insert(m_queue.end(), b, b + n);
        return true;
    }
    uint32_t QueuedBytes() const override {
        std::lock_guard<std::mutex> lk(m_mu);
        return (uint32_t)m_queue.size();
    }
    void ClearQueued() override { std::lock_guard<std::mutex> lk(m_mu); m_queue.clear(); }
    uint32_t Dequeue(void* out, uint32_t max) override {
        std::lock_guard<std::mutex> lk(m_mu);
        const uint32_t n = (uint32_t)std::min<size_t>(max, m_queue.size());
        uint8_t* o = static_cast<uint8_t*>(out);
        for (uint32_t i = 0; i < n; ++i) { o[i] = m_queue.front(); m_queue.pop_front(); }
        return n;
    }

private:
    void Run() {
        using Clock = std::chrono::steady_clock;
        const int frames = std::max(64, m_spec.frames);
        std::vector<uint8_t> buf((size_t)frames * m_frameBytes);
        const auto period = std::chrono::nanoseconds((long long)frames * 1000000000ll / std::max(1, m_spec.freq));
        Clock::time_point t0 = Clock::now();
        long long done = 0;   // frames moved since t0, for pacing devices that never block (ALSA "null")
        bool wasPaused = true;
        while (!m_stop) {
            if (m_paused) { wasPaused = true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
            if (wasPaused) { wasPaused = false; t0 = Clock::now(); done = 0; m_api->snd_pcm_prepare(m_pcm); if (m_capture) m_api->snd_pcm_start(m_pcm); }
            if (m_capture) {
                snd_pcm_sframes_t n = m_api->snd_pcm_readi(m_pcm, buf.data(), (snd_pcm_uframes_t)frames);
                if (n < 0) { if (m_api->snd_pcm_recover(m_pcm, (int)n, 1) < 0) std::this_thread::sleep_for(period); continue; }
                if (n > 0) {
                    const size_t bytes = (size_t)n * m_frameBytes;
                    if (m_spec.callback) m_spec.callback(m_spec.user, buf.data(), (int)bytes);
                    else {
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_queue.insert(m_queue.end(), buf.begin(), buf.begin() + (long)bytes);
                        while (m_queue.size() > (size_t)1 << 20) m_queue.pop_front();   // cap: drop the oldest
                    }
                    done += n;
                }
            } else {
                const size_t bytes = buf.size();
                if (m_spec.callback) {
                    std::memset(buf.data(), 0, bytes);
                    m_spec.callback(m_spec.user, buf.data(), (int)bytes);
                } else {
                    std::lock_guard<std::mutex> lk(m_mu);
                    const size_t take = std::min(bytes, m_queue.size());
                    for (size_t i = 0; i < take; ++i) { buf[i] = m_queue.front(); m_queue.pop_front(); }
                    if (take < bytes) std::memset(buf.data() + take, 0, bytes - take);   // underrun: silence, like SDL
                }
                snd_pcm_sframes_t n = m_api->snd_pcm_writei(m_pcm, buf.data(), (snd_pcm_uframes_t)frames);
                if (n < 0) { if (m_api->snd_pcm_recover(m_pcm, (int)n, 1) < 0) std::this_thread::sleep_for(period); continue; }
                done += n;
            }
            // Never run ahead of real time by more than two periods.
            const auto due = t0 + std::chrono::nanoseconds(done * 1000000000ll / std::max(1, m_spec.freq));
            const auto now = Clock::now();
            if (due > now + 2 * period) std::this_thread::sleep_until(due - 2 * period);
        }
    }

    const AlsaApi* m_api;
    snd_pcm_t* m_pcm;
    AudioSpec m_spec;
    bool m_capture;
    int m_frameBytes = 2;
    std::atomic<bool> m_stop{false}, m_paused{true};
    mutable std::mutex m_mu;
    std::deque<uint8_t> m_queue;
    std::thread m_thread;
};

std::vector<std::string> ListDevices(bool capture) {
    std::vector<std::string> out;
    out.push_back("default");
    std::string err;
    const AlsaApi* a = Api(err);
    if (!a || !a->snd_device_name_hint || !a->snd_device_name_get_hint || !a->snd_device_name_free_hint) return out;
    void** hints = nullptr;
    if (a->snd_device_name_hint(-1, "pcm", &hints) < 0 || !hints) return out;
    for (void** h = hints; *h; ++h) {
        char* name = a->snd_device_name_get_hint(*h, "NAME");
        char* ioid = a->snd_device_name_get_hint(*h, "IOID");   // null = both directions
        const bool dirOk = !ioid || std::strcmp(ioid, capture ? "Input" : "Output") == 0;
        if (name && dirOk && std::strcmp(name, "null") != 0 && std::strcmp(name, "default") != 0) out.push_back(name);
        std::free(name);
        std::free(ioid);
    }
    a->snd_device_name_free_hint(hints);
    return out;
}

} // namespace

std::unique_ptr<AudioDevice> OpenAlsa(const AudioSpec& want, bool capture, const char* device,
                                      AudioSpec* got, std::string& err) {
    const AlsaApi* a = Api(err);
    if (!a) return nullptr;
    std::string name = device ? device : "";
    if (name.empty()) { const char* e = std::getenv("OKAY_ALSA_DEVICE"); name = e && *e ? e : "default"; }
    snd_pcm_t* pcm = nullptr;
    int rc = a->snd_pcm_open(&pcm, name.c_str(), capture ? kCapture : kPlayback, 0);
    if (rc < 0) { err = "snd_pcm_open(" + name + "): " + a->snd_strerror(rc); return nullptr; }
    const int fmt = want.format == SampleFormat::F32 ? kFmtF32 : kFmtS16;
    const int frames = want.frames > 0 ? want.frames : 1024;
    const unsigned latencyUs = (unsigned)((long long)frames * 3 * 1000000ll / std::max(1, want.freq));   // 3 periods
    rc = a->snd_pcm_set_params(pcm, fmt, kAccessRwInterleaved, (unsigned)want.channels, (unsigned)want.freq, 1, latencyUs);
    if (rc < 0) { err = "snd_pcm_set_params: " + std::string(a->snd_strerror(rc)); a->snd_pcm_close(pcm); return nullptr; }
    AudioSpec spec = want;   // soft_resample = 1: the "plug" layer converts, so we get exactly what we asked for
    spec.frames = frames;
    if (got) *got = spec;
    return std::unique_ptr<AudioDevice>(new AlsaDevice(a, pcm, spec, capture));
}

int AlsaDeviceCount(bool capture) { return (int)ListDevices(capture).size(); }
std::string AlsaDeviceName(int index, bool capture) {
    const auto v = ListDevices(capture);
    return index >= 0 && index < (int)v.size() ? v[(size_t)index] : std::string();
}

} // namespace okay::platform::own
