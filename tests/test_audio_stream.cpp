// Streaming audio (AudioStream): streamed decode matches whole-file decode sample for
// sample for WAV, MP3 and OGG; seek is exact; loops have no gap; memory stays small for a
// ten minute file; a starved ring plays silence; damaged files and files that change under
// the stream never crash it. Also the AudioSource "Stream" option and the Music player.
#include "test_framework.hpp"
#include <Okay.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

// ---- heap accounting (counts every C++ allocation, to bound a stream's memory) --------------------
namespace {
std::atomic<std::size_t> g_live{0}, g_peak{0};
}
void* operator new(std::size_t n) {
    void* p = std::malloc(n + 16);
    if (!p) throw std::bad_alloc();
    *static_cast<std::size_t*>(p) = n;
    const std::size_t now = g_live += n;
    std::size_t pk = g_peak.load();
    while (now > pk && !g_peak.compare_exchange_weak(pk, now)) {}
    return static_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    char* base = static_cast<char*>(p) - 16;
    g_live -= *reinterpret_cast<std::size_t*>(base);
    std::free(base);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

using namespace okay;

namespace {

const std::string kData = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/";

// ---- file helpers ------------------------------------------------------------------------------
std::vector<unsigned char> Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void Spit(const std::string& path, const std::vector<unsigned char>& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
}
void Put32(std::vector<unsigned char>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((unsigned char)(x >> (8 * i))); }
void Put16(std::vector<unsigned char>& v, std::uint32_t x) { for (int i = 0; i < 2; ++i) v.push_back((unsigned char)(x >> (8 * i))); }

// A WAV file image: `fmt` code, channels, rate, bits and raw sample bytes.
std::vector<unsigned char> WavBytes(int fmt, int ch, int rate, int bits, const std::vector<unsigned char>& data,
                                    std::uint32_t declaredData = 0xFFFFFFFEu) {
    std::vector<unsigned char> v;
    v.insert(v.end(), {'R', 'I', 'F', 'F'});
    Put32(v, (std::uint32_t)(36 + data.size()));
    v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(v, 16); Put16(v, (std::uint32_t)fmt); Put16(v, (std::uint32_t)ch); Put32(v, (std::uint32_t)rate);
    Put32(v, (std::uint32_t)(rate * ch * bits / 8)); Put16(v, (std::uint32_t)(ch * bits / 8)); Put16(v, (std::uint32_t)bits);
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    Put32(v, declaredData == 0xFFFFFFFEu ? (std::uint32_t)data.size() : declaredData);
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

std::vector<unsigned char> Pcm16(int ch, int frames, std::uint32_t seed) {
    std::vector<unsigned char> d;
    std::uint32_t x = seed;
    for (int i = 0; i < frames * ch; ++i) {
        x = x * 1664525u + 1013904223u;
        Put16(d, (x >> 16) & 0xFFFF);
    }
    return d;
}

// Reads a whole stream in chunks of `chunk` frames.
std::vector<float> StreamAll(AudioStream& s, std::size_t chunk, std::size_t limit = 50000000) {
    std::vector<float> out, buf(chunk * 2);
    for (;;) {
        const std::size_t n = s.Read(buf.data(), chunk);
        out.insert(out.end(), buf.begin(), buf.begin() + (std::ptrdiff_t)(n * (std::size_t)s.Channels()));
        if (n < chunk || out.size() > limit) break;
    }
    return out;
}

struct Fixture { std::string name, path; };

std::vector<Fixture> Fixtures(const std::string& tmpWavPrefix) {
    std::vector<Fixture> v;
    std::ifstream man(kData + "mp3/manifest.txt");
    std::string line;
    while (std::getline(man, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::string name = line.substr(0, line.find(' '));
        v.push_back({"mp3 " + name, kData + "mp3/" + name + ".mp3"});
    }
    v.push_back({"mp3 tone_mono", kData + "tone_mono.mp3"});
    v.push_back({"ogg tone_stereo", kData + "tone_stereo.ogg"});
    {   // every Ogg fixture of the decoder's own test set (mono to 8 channels, odd block sizes, floor 0, truncated packets)
        std::vector<std::string> names;
        for (const auto& e : std::filesystem::directory_iterator(kData + "ogg"))
            if (e.path().extension() == ".ogg") names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) v.push_back({"ogg " + n, kData + "ogg/" + n});
    }
    struct W { const char* name; int fmt, ch, rate, bits; };
    const W ws[] = {{"wav16 stereo", 1, 2, 22050, 16}, {"wav16 mono", 1, 1, 11025, 16}, {"wav8 stereo", 1, 2, 8000, 8},
                    {"wav24 stereo", 1, 2, 44100, 24}, {"wav32f stereo", 3, 2, 32000, 32}, {"wav16 6ch", 1, 6, 16000, 16}};
    int k = 0;
    for (const W& w : ws) {
        std::vector<unsigned char> d;
        std::uint32_t x = 12345u + (std::uint32_t)k;
        const int frames = 9000 + 37 * k;
        for (int i = 0; i < frames * w.ch; ++i) {
            x = x * 1664525u + 1013904223u;
            const std::uint32_t r = x >> 8;
            if (w.bits == 8) d.push_back((unsigned char)r);
            else if (w.bits == 16) Put16(d, r & 0xFFFF);
            else if (w.bits == 24) { d.push_back((unsigned char)r); d.push_back((unsigned char)(r >> 8)); d.push_back((unsigned char)(r >> 16)); }
            else if (w.fmt == 3) { const float f = ((r & 0xFFFF) / 32768.0f) - 1.0f; unsigned char b[4]; std::memcpy(b, &f, 4); d.insert(d.end(), b, b + 4); }
            else Put32(d, x);
        }
        const std::string p = tmpWavPrefix + std::to_string(k++) + ".wav";
        Spit(p, WavBytes(w.fmt, w.ch, w.rate, w.bits, d));
        v.push_back({w.name, p});
    }
    return v;
}

void ResetMixer(int rate) {
    AudioMixer::sampleRate = rate;
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::muted = false;
    AudioMixer::focusMuted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) { AudioMixer::groupVolume[i] = 1.0f; AudioMixer::groupMuted[i] = false; }
    AudioMixer::duckMusicUnderVoice = false;
    AudioMixer::SetListener(Vec3::Zero);
}

// ---- Ogg repackaging: a long, many-page file made from a fixture's own packets -------------------
std::uint32_t OggCrc(const std::vector<unsigned char>& p) {
    static std::uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t r = i << 24; for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1); t[i] = r; }
        init = true;
    }
    std::uint32_t crc = 0;
    for (unsigned char b : p) crc = (crc << 8) ^ t[((crc >> 24) ^ b) & 0xFF];
    return crc;
}

// The packets of a well-formed single-stream Ogg file.
std::vector<std::vector<unsigned char>> OggPackets(const std::vector<unsigned char>& f) {
    std::vector<std::vector<unsigned char>> out;
    std::vector<unsigned char> cur;
    std::size_t pos = 0;
    while (pos + 27 <= f.size() && std::memcmp(&f[pos], "OggS", 4) == 0) {
        const unsigned nseg = f[pos + 26];
        std::size_t body = pos + 27 + nseg;
        for (unsigned i = 0; i < nseg; ++i) {
            const unsigned l = f[pos + 27 + i];
            cur.insert(cur.end(), f.begin() + (std::ptrdiff_t)body, f.begin() + (std::ptrdiff_t)(body + l));
            body += l;
            if (l < 255) { out.push_back(cur); cur.clear(); }
        }
        pos = body;
    }
    return out;
}

// One packet per page, `repeats` copies of the audio packets; the last page ends the stream.
std::vector<unsigned char> OggRepack(const std::vector<std::vector<unsigned char>>& pk, int repeats) {
    std::vector<std::vector<unsigned char>> all(pk.begin(), pk.begin() + 3);
    for (int r = 0; r < repeats; ++r) all.insert(all.end(), pk.begin() + 3, pk.end());
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        std::vector<unsigned char> pg = {'O', 'g', 'g', 'S', 0, (unsigned char)(i == 0 ? 2 : (i + 1 == all.size() ? 4 : 0))};
        const std::uint64_t granule = i + 1 == all.size() ? 0x7FFFFFFFFFull : 0;
        for (int b = 0; b < 8; ++b) pg.push_back((unsigned char)(granule >> (8 * b)));
        Put32(pg, 0x1234); Put32(pg, (std::uint32_t)i); Put32(pg, 0);
        std::vector<unsigned char> lacing;
        std::size_t len = all[i].size();
        while (len >= 255) { lacing.push_back(255); len -= 255; }
        lacing.push_back((unsigned char)len);
        pg.push_back((unsigned char)lacing.size());
        pg.insert(pg.end(), lacing.begin(), lacing.end());
        pg.insert(pg.end(), all[i].begin(), all[i].end());
        const std::uint32_t crc = OggCrc(pg);
        for (int b = 0; b < 4; ++b) pg[22 + b] = (unsigned char)(crc >> (8 * b));
        out.insert(out.end(), pg.begin(), pg.end());
    }
    return out;
}

bool Same(const float* a, const float* b, std::size_t n) { return std::memcmp(a, b, n * sizeof(float)) == 0; }

} // namespace

int main() {
    RUN_SUITE("audio_stream");
    const std::string tmp = "test_audio_stream_tmp_";
    const auto fixtures = Fixtures(tmp);

    // ---- streamed decode == whole-file decode, sample for sample ------------------------------------
    for (const Fixture& fx : fixtures) {
        AudioClip whole;
        std::string err;
        const bool wholeOk = whole.Load(fx.path, &err);
        std::string serr;
        auto st = AudioStream::OpenFile(fx.path, &serr);
        if (!wholeOk || !st) { std::cerr << "  fixture failed to open: " << fx.name << " " << err << serr << "\n"; CHECK(false); continue; }
        CHECK(st->Channels() == whole.channels);
        CHECK(st->SampleRate() == whole.sampleRate);
        if (st->TotalFrames() != (std::int64_t)whole.FrameCount())
            std::cerr << "  length differs for " << fx.name << ": stream " << st->TotalFrames() << " whole " << whole.FrameCount() << "\n";
        CHECK(st->TotalFrames() == (std::int64_t)whole.FrameCount());

        for (std::size_t chunk : {std::size_t(1), std::size_t(777), std::size_t(4096)}) {
            if (chunk == 1 && whole.FrameCount() > 3000) continue;   // one at a time only for the short ones
            st->Seek(0);
            const auto got = StreamAll(*st, chunk);
            const bool eq = got.size() == whole.samples.size() && Same(got.data(), whole.samples.data(), got.size());
            if (!eq) std::cerr << "  streamed != whole for " << fx.name << " chunk " << chunk << " (" << got.size() << " vs " << whole.samples.size() << ")\n";
            CHECK(eq);
        }
        // From memory too.
        auto bytes = std::make_shared<std::vector<unsigned char>>(Slurp(fx.path));
        auto ms = AudioStream::OpenMemory(bytes);
        CHECK(ms != nullptr);
        if (ms) {
            const auto got = StreamAll(*ms, 1000);
            CHECK(got.size() == whole.samples.size() && Same(got.data(), whole.samples.data(), got.size()));
        }
        // Ends at the end: nothing more after the last frame.
        float one[2];
        CHECK(st->Read(one, 1) == 0);
        CHECK(st->Ended());
    }

    // ---- seek accuracy ---------------------------------------------------------------------------
    for (const Fixture& fx : fixtures) {
        AudioClip whole; whole.Load(fx.path);
        auto st = AudioStream::OpenFile(fx.path);
        if (!st || whole.FrameCount() < 600) continue;
        const std::size_t ch = (std::size_t)whole.channels, n = whole.FrameCount();
        std::vector<std::int64_t> targets = {0, 1, 575, 576, 1151, 1152, 1153, (std::int64_t)n / 3, (std::int64_t)n / 2 + 7,
                                             (std::int64_t)n - 300, (std::int64_t)n - 1};
        std::uint32_t x = 99;
        for (int i = 0; i < 6; ++i) { x = x * 1664525u + 1013904223u; targets.push_back((std::int64_t)((x >> 8) % n)); }
        bool allOk = true;
        for (std::int64_t t : targets) {
            if (t < 0 || t >= (std::int64_t)n) continue;
            CHECK(st->Seek(t));
            const std::size_t want = std::min<std::size_t>(300, n - (std::size_t)t);
            std::vector<float> buf(want * ch);
            const std::size_t got = st->Read(buf.data(), want);
            const bool ok = got == want && Same(buf.data(), whole.samples.data() + (std::size_t)t * ch, want * ch);
            if (!ok) { allOk = false; std::cerr << "  seek to " << t << " mismatch in " << fx.name << " (got " << got << "/" << want << ")\n"; }
            // Reading on from the seek point continues the same audio to the very end.
            if (t == (std::int64_t)n / 3) {
                const auto rest = StreamAll(*st, 1500);
                const std::size_t from = ((std::size_t)t + want) * ch;
                CHECK(rest.size() == whole.samples.size() - from && Same(rest.data(), whole.samples.data() + from, rest.size()));
            }
        }
        CHECK(allOk);
        // Past the end clamps: nothing to read, no crash.
        st->Seek((std::int64_t)n + 100000);
        float tail[4];
        CHECK(st->Read(tail, 2) == 0);
        // And back to the start.
        st->Seek(0);
        std::vector<float> head(ch * 50);
        CHECK(st->Read(head.data(), 50) == 50 && Same(head.data(), whole.samples.data(), 50 * ch));
    }

    // ---- loop continuity: the join has no gap and no click ------------------------------------------
    {
        // A sine of whole periods: the looped stream must be the sine continued without a beat missing.
        const int rate = 22050, period = 50, frames = period * 120;   // 441 Hz, 6000 frames
        std::vector<unsigned char> d;
        std::vector<float> expect;
        for (int i = 0; i < frames; ++i) {
            const double v = std::sin(2.0 * 3.14159265358979323846 * (double)(i % period) / period);
            const int s = (int)std::lround(v * 20000.0);
            Put16(d, (std::uint32_t)(s & 0xFFFF));
            expect.push_back((float)s / 32768.0f);
        }
        const std::string p = tmp + "loop.wav";
        Spit(p, WavBytes(1, 1, rate, 16, d));
        auto st = AudioStream::OpenFile(p);
        CHECK(st != nullptr);
        if (st) {
            st->loop = true;
            const std::size_t total = 3 * (std::size_t)frames + 123;   // three joins
            std::vector<float> out(total);
            std::size_t got = 0;
            while (got < total) { const std::size_t n = st->Read(out.data() + got, std::min<std::size_t>(997, total - got)); if (!n) break; got += n; }
            CHECK(got == total);
            bool exact = true;
            float maxStep = 0.0f;
            for (std::size_t i = 0; i < total; ++i) {
                if (out[i] != expect[i % (std::size_t)frames]) exact = false;
                if (i) maxStep = std::max(maxStep, std::fabs(out[i] - out[i - 1]));
            }
            CHECK(exact);
            CHECK(maxStep < 0.4f);   // the sine's own largest step; a click or a dropped frame would exceed it
            CHECK(!st->Ended());
            CHECK(st->Position() == (std::int64_t)(total % (std::size_t)frames));
        }
        std::remove(p.c_str());
    }
    // Looped MP3, OGG and stereo WAV: the second pass is the first pass, and nothing is skipped or silent at the join.
    for (const Fixture& fx : fixtures) {
        if (fx.name != "mp3 stereo_44k_cbr128" && fx.name != "mp3 mono_16k_mpeg2" && fx.name != "ogg tone_stereo" && fx.name != "ogg enc_stereo_22k_q3" && fx.name != "ogg syn_stereo_blocks_64_8192" &&
            fx.name != "wav16 stereo" && fx.name != "mp3 noxing_44k")
            continue;
        AudioClip whole; whole.Load(fx.path);
        auto st = AudioStream::OpenFile(fx.path);
        CHECK(st != nullptr);
        if (!st) continue;
        st->loop = true;
        const std::size_t n = whole.FrameCount(), ch = (std::size_t)whole.channels;
        std::vector<float> out(2 * n * ch + 500 * ch);
        std::size_t got = 0;
        const std::size_t want = out.size() / ch;
        while (got < want) { const std::size_t k = st->Read(out.data() + got * ch, std::min<std::size_t>(701, want - got)); if (!k) break; got += k; }
        CHECK(got == want);
        bool same = true;
        for (std::size_t i = 0; i < want; ++i)
            for (std::size_t c = 0; c < ch; ++c)
                if (out[i * ch + c] != whole.samples[(i % n) * ch + c]) same = false;
        if (!same) std::cerr << "  loop pass differs for " << fx.name << "\n";
        CHECK(same);
        // The join itself: the frames either side of it are the end and the start of the file.
        CHECK(!st->Ended());
    }

    // ---- a long Ogg with many pages: the seek index, exact seeks and a seamless loop -----------------
    for (const char* name : {"ogg/enc_stereo_22k_q3.ogg", "ogg/syn_stereo_blocks_64_8192.ogg", "ogg/enc_mono_16k_q4.ogg"}) {
        const auto pk = OggPackets(Slurp(kData + name));
        CHECK(pk.size() > 4);
        if (pk.size() <= 4) continue;
        const int repeats = std::max(1, 600 / (int)(pk.size() - 3));
        const std::string lp = tmp + "long.ogg";
        Spit(lp, OggRepack(pk, repeats));
        AudioClip whole;
        CHECK(whole.Load(lp));
        auto st = AudioStream::OpenFile(lp);
        CHECK(st != nullptr);
        if (!st) continue;
        const std::size_t n = whole.FrameCount(), ch = (std::size_t)whole.channels;
        std::cout << "  " << name << " x" << repeats << ": " << n << " frames\n";
        CHECK(st->TotalFrames() == (std::int64_t)n);
        const auto all = StreamAll(*st, 1234);
        CHECK(all.size() == whole.samples.size() && Same(all.data(), whole.samples.data(), all.size()));
        bool allOk = true;
        std::uint32_t x = 7;
        for (int i = 0; i < 60; ++i) {
            x = x * 1664525u + 1013904223u;
            const std::size_t t = i == 0 ? 0 : i == 1 ? n - 1 : (x >> 8) % n;
            st->Seek((std::int64_t)t);
            const std::size_t want = std::min<std::size_t>(700, n - t);
            std::vector<float> buf(want * ch);
            const std::size_t got = st->Read(buf.data(), want);
            if (got != want || !Same(buf.data(), whole.samples.data() + t * ch, want * ch)) { allOk = false; std::cerr << "  seek " << t << " wrong in " << name << "\n"; }
        }
        CHECK(allOk);
        st->Seek(0);
        st->loop = true;
        std::vector<float> two(2 * n * ch);
        std::size_t got = 0;
        while (got < 2 * n) { const std::size_t k = st->Read(two.data() + got * ch, std::min<std::size_t>(999, 2 * n - got)); if (!k) break; got += k; }
        CHECK(got == 2 * n);
        bool same = true;
        for (std::size_t i = 0; i < 2 * n * ch; ++i) if (two[i] != whole.samples[i % (n * ch)]) { same = false; break; }
        CHECK(same);
        std::remove(lp.c_str());
    }

    // ---- memory bound: a ten minute file ------------------------------------------------------------
    {
        // 10 minutes of 8 kHz stereo 16-bit: 19.2 MB on disk, 38 MB as decoded floats.
        const int rate = 8000;
        const std::int64_t frames = 600LL * rate;
        const std::string p = tmp + "ten_minutes.wav";
        {
            std::ofstream f(p, std::ios::binary);
            std::vector<unsigned char> hdr = WavBytes(1, 2, rate, 16, {}, (std::uint32_t)(frames * 4));
            f.write(reinterpret_cast<const char*>(hdr.data()), (std::streamsize)hdr.size());
            std::vector<unsigned char> blk;
            for (std::int64_t k = 0; k < frames; ++k) {
                Put16(blk, (std::uint32_t)((k * 37) & 0x7FFF));
                Put16(blk, (std::uint32_t)(k % 1000));
                if (blk.size() >= 65536) { f.write(reinterpret_cast<const char*>(blk.data()), (std::streamsize)blk.size()); blk.clear(); }
            }
            f.write(reinterpret_cast<const char*>(blk.data()), (std::streamsize)blk.size());
        }
        const std::size_t base = g_live.load();
        g_peak = base;
        {
            auto st = AudioStream::OpenFile(p);
            CHECK(st != nullptr);
            if (st) {
                CHECK(st->TotalFrames() == frames);
                CHECK_NEAR(st->Duration(), 600.0, 0.01);
                bool exact = true;
                std::int64_t pos = 0;
                std::vector<float> buf(4096 * 2);
                for (;;) {
                    const std::size_t n = st->Read(buf.data(), 4096);
                    for (std::size_t i = 0; i < n; ++i, ++pos) {
                        if (buf[2 * i] != (float)(std::int16_t)((pos * 37) & 0x7FFF) / 32768.0f ||
                            buf[2 * i + 1] != (float)(pos % 1000) / 32768.0f) { exact = false; break; }
                    }
                    if (n < 4096 || !exact) break;
                }
                CHECK(exact && pos == frames);
                CHECK(st->MemoryBytes() < 2u * 1024 * 1024);
                // Seek far into it and read: still exact.
                st->Seek(frames - 5000);
                float two[4];
                CHECK(st->Read(two, 2) == 2);
                CHECK(two[1] == (float)((frames - 5000) % 1000) / 32768.0f);
            }
        }
        const std::size_t peak = g_peak.load() - base;
        std::cout << "  ten-minute WAV: peak heap above baseline " << peak / 1024 << " KB\n";
        CHECK(peak < 2u * 1024 * 1024);
        std::remove(p.c_str());

        // Ten minutes of MP3 (the stereo fixture repeated back to back) and the OGG: bounded too.
        const auto one = Slurp(kData + "mp3/noxing_44k.mp3");   // no Xing tag, so no gapless trim: the copies just add up
        const std::string mp = tmp + "ten_minutes.mp3";
        {
            std::ofstream f(mp, std::ios::binary);
            for (int i = 0; i < 1300; ++i) f.write(reinterpret_cast<const char*>(one.data()), (std::streamsize)one.size());
        }
        const std::size_t base2 = g_live.load();
        g_peak = base2;
        {
            auto st = AudioStream::OpenFile(mp);
            CHECK(st != nullptr);
            if (st) {
                {   // each copy is about 0.58 s of audio 
                    AudioClip one1; one1.Load(kData + "mp3/noxing_44k.mp3");
                    std::cout << "  mp3 stream duration " << st->Duration() << " s (one copy " << one1.Duration() << " s)\n";
                    CHECK(std::fabs(st->Duration() - 1300.0f * one1.Duration()) < 0.5f);
                    CHECK(st->Duration() > 590.0f);   // ten minutes and more
                }
                CHECK(st->Channels() == 2);
                std::int64_t n = 0;
                std::vector<float> buf(4096 * 2);
                for (;;) { const std::size_t k = st->Read(buf.data(), 4096); n += (std::int64_t)k; if (k < 4096) break; }
                CHECK(n == st->TotalFrames());
                CHECK(st->MemoryBytes() < 2u * 1024 * 1024);
                CHECK(st->Seek(st->TotalFrames() / 2));
                CHECK(st->Read(buf.data(), 100) == 100);
            }
        }
        const std::size_t peak2 = g_peak.load() - base2;
        std::cout << "  ten-minute MP3: peak heap above baseline " << peak2 / 1024 << " KB\n";
        CHECK(peak2 < 2u * 1024 * 1024);
        std::remove(mp.c_str());

        for (const char* n : {"tone_stereo.ogg", "ogg/enc_stereo_48k_q10.ogg", "ogg/enc_8ch_16k_q3.ogg", "ogg/syn_stereo_blocks_64_8192.ogg"}) {
            auto og = AudioStream::OpenFile(kData + n);
            CHECK(og && og->MemoryBytes() < 2u * 1024 * 1024);
            if (og) std::cout << "  " << n << " stream memory " << og->MemoryBytes() / 1024 << " KB\n";
        }
    }

    // ---- underrun: a starved ring plays silence, never skips ahead, never crashes -------------------
    {
        ResetMixer(1000);
        const int total = 20000;
        std::vector<unsigned char> d;
        for (int i = 0; i < total; ++i) Put16(d, (std::uint32_t)(i + 1));   // never zero
        const std::string p = tmp + "ramp.wav";
        Spit(p, WavBytes(1, 1, 1000, 16, d));
        Scene s("Under"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clipPath = p; a->stream = true;
        CHECK(LoadSceneAudio(s, "", 1000) == 1);
        CHECK(a->IsStreaming() && a->HasAudio());
        s.Start();
        a->Play();
        auto st = a->Stream();
        CHECK(st != nullptr);
        st->decodeBudget = 100;   // a slow disk: 100 frames per Pump
        int next = 0;             // next ramp value expected (1-based)
        bool inOrder = true, finite = true;
        std::vector<float> out(2 * 1024);
        for (int r = 0; r < 10 && next < total; ++r) {
            AudioMixer::RenderStereo(s, out.data(), 1024);
            int played = 0;
            for (int i = 0; i < 1024; ++i) {
                const float v = out[2 * i];
                if (!std::isfinite(v) || !std::isfinite(out[2 * i + 1])) finite = false;
                if (v == 0.0f) { for (int j = i; j < 1024; ++j) if (out[2 * j] != 0.0f) inOrder = false; break; }   // silence only at the end
                if (v != (float)(next + 1) / 32768.0f) inOrder = false;
                ++next; ++played;
            }
            if (r == 0) CHECK(played > 0 && played < 1024);
        }
        CHECK(inOrder && finite);
        CHECK(st->Underruns() > 0);
        CHECK(a->IsPlaying());
        // The disk catches up: it carries on from exactly where it waited, and finishes.
        st->decodeBudget = 0;
        for (int r = 0; r < 40 && a->IsPlaying(); ++r) {
            AudioMixer::RenderStereo(s, out.data(), 1024);
            for (int i = 0; i < 1024; ++i) {
                const float v = out[2 * i];
                if (v == 0.0f) break;
                if (v != (float)(next + 1) / 32768.0f) inOrder = false;
                ++next;
            }
        }
        CHECK(inOrder);
        CHECK(next == total);
        CHECK(!a->IsPlaying());
        std::remove(p.c_str());
    }

    // ---- hostile files while streaming ---------------------------------------------------------------
    {
        std::uint32_t rng = 0xC0FFEE;
        auto next = [&]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
        auto exercise = [&](const std::vector<unsigned char>& bytes, const std::string& path, bool loop) {
            Spit(path, bytes);
            std::shared_ptr<AudioStream> files[2] = {AudioStream::OpenFile(path),
                                                     AudioStream::OpenMemory(std::make_shared<std::vector<unsigned char>>(bytes))};
            for (auto& st : files) {
                if (!st) continue;
                st->loop = loop;
                std::vector<float> buf(2 * 600);
                std::size_t total = 0;
                for (int i = 0; i < 80; ++i) {
                    if (i % 7 == 3 && st->TotalFrames() > 0) st->Seek((std::int64_t)(next() % (std::uint32_t)st->TotalFrames()));
                    if (i % 11 == 5) st->Seek((std::int64_t)(next() % 100000));
                    const std::size_t n = st->Read(buf.data(), 600);
                    total += n;
                    if (n == 0) { st->Seek(0); }
                }
                (void)total;
            }
        };
        int opened = 0;
        const std::string hp = tmp + "hostile.bin";
        for (const Fixture& fx : fixtures) {
            if (fx.name.find("synth") != std::string::npos && fx.name.find("mpeg1_dual") == std::string::npos) continue;   // keep the run short
            const auto good = Slurp(fx.path);
            if (good.empty()) continue;
            // Truncated at assorted lengths, including mid-header and mid-frame.
            for (std::size_t len : {std::size_t(0), std::size_t(3), std::size_t(11), std::size_t(44), std::size_t(45), good.size() / 7,
                                    good.size() / 2, good.size() - 1, good.size() - 2}) {
                if (len > good.size()) continue;
                std::vector<unsigned char> cut(good.begin(), good.begin() + (std::ptrdiff_t)len);
                exercise(cut, hp, false);
                exercise(cut, hp, true);
            }
            // Random byte flips and zeroed runs.
            for (int round = 0; round < 12; ++round) {
                std::vector<unsigned char> bad = good;
                const int flips = 1 + (int)(next() % 24);
                for (int k = 0; k < flips; ++k) bad[next() % bad.size()] = (unsigned char)next();
                if (round % 3 == 0) { const std::size_t at = next() % bad.size(); for (std::size_t k = at; k < std::min(bad.size(), at + 300); ++k) bad[k] = 0; }
                exercise(bad, hp, round % 2 == 0);
                ++opened;
            }
            // Pure noise behind the right magic.
            std::vector<unsigned char> noise(good.begin(), good.begin() + (std::ptrdiff_t)std::min<std::size_t>(good.size(), 64));
            for (int k = 0; k < 4000; ++k) noise.push_back((unsigned char)next());
            exercise(noise, hp, true);
        }
        // WAV headers that lie.
        exercise(WavBytes(1, 2, 8000, 16, Pcm16(2, 100, 1), 0xFFFFFFF0u), hp, true);               // data chunk longer than the file
        exercise(WavBytes(1, 0, 8000, 16, Pcm16(2, 100, 1)), hp, false);                            // zero channels
        exercise(WavBytes(1, 2, 0, 16, Pcm16(2, 100, 1)), hp, false);                               // zero rate
        exercise(WavBytes(1, 2, 8000, 0, Pcm16(2, 100, 1)), hp, false);                             // zero bits
        exercise(WavBytes(1, 2, 0xFFFFFFF0u, 16, Pcm16(2, 100, 1)), hp, false);                     // absurd rate
        exercise(WavBytes(1, 65535, 8000, 16, Pcm16(2, 100, 1)), hp, true);                         // absurd channel count
        exercise(WavBytes(1, 2, 8000, 12, Pcm16(2, 100, 1)), hp, true);                             // odd bit depth
        {
            auto odd = WavBytes(1, 2, 8000, 16, Pcm16(2, 100, 1));
            odd.push_back(7); odd.push_back(9); odd.push_back(11);                                    // a partial frame at the end
            auto wst = [&]() { Spit(hp, odd); return AudioStream::OpenFile(hp); }();
            CHECK(wst != nullptr);
            if (wst) CHECK(wst->TotalFrames() == 100 + 0);
        }
        // Unusable files are refused with a message, not a crash.
        {
            std::string err;
            CHECK(AudioStream::OpenFile("does/not/exist.wav", &err) == nullptr && !err.empty());
            Spit(hp, std::vector<unsigned char>());
            CHECK(AudioStream::OpenFile(hp, &err) == nullptr);
            Spit(hp, std::vector<unsigned char>(5000, 0x41));
            err.clear();
            CHECK(AudioStream::OpenFile(hp, &err) == nullptr && !err.empty());
            CHECK(AudioStream::OpenMemory(nullptr, &err) == nullptr);
        }
        // The file shrinks while it is being streamed: the stream just ends.
        {
            const std::string sp = tmp + "shrink.wav";
            Spit(sp, WavBytes(1, 2, 8000, 16, Pcm16(2, 50000, 5)));
            auto st = AudioStream::OpenFile(sp);
            CHECK(st != nullptr);
            if (st) {
                float b[400];
                CHECK(st->Read(b, 100) == 100);
                Spit(sp, std::vector<unsigned char>(100, 0));
                st->Seek(30000);
                std::size_t n = 0, k;
                while ((k = st->Read(b, 200)) > 0) { n += k; if (n > 100000) break; }
                CHECK(n < 100000);
                CHECK(st->Ended());
            }
            std::remove(sp.c_str());
        }
        std::remove(hp.c_str());
        CHECK(opened > 0);
    }

    // ---- the AudioSource "Stream" option ------------------------------------------------------------
    {
        // A stereo file: left a ramp, right silent.
        const int rate = 22050, frames = 30000;
        std::vector<unsigned char> d;
        for (int i = 0; i < frames; ++i) { Put16(d, (std::uint32_t)((i % 20000) + 1)); Put16(d, 0); }
        const std::string wav = tmp + "music.wav";
        Spit(wav, WavBytes(1, 2, rate, 16, d));

        ResetMixer(rate);
        Scene whole("Whole"); whole.physicsEnabled = false;
        Scene strm("Stream"); strm.physicsEnabled = false;
        auto* a = whole.CreateGameObject("A")->AddComponent<AudioSource>();
        auto* b = strm.CreateGameObject("A")->AddComponent<AudioSource>();
        for (AudioSource* x : {a, b}) { x->clipPath = wav; x->loop = true; x->pitch = 1.25f; x->volume = 0.8f; x->pan = 0.3f; }
        b->stream = true;
        CHECK(LoadSceneAudio(whole, "", rate) == 1);
        CHECK(LoadSceneAudio(strm, "", rate) == 1);
        CHECK(!a->IsStreaming() && b->IsStreaming() && b->clip.samples.empty());
        CHECK(a->clip.channels == 2);
        whole.Start(); strm.Start();
        a->Play(); b->Play();
        CHECK_NEAR(b->Duration(), a->Duration(), 1e-5);
        bool match = true, leftOnly = true, nonzero = false;
        std::vector<float> o1(2 * 700), o2(2 * 700);
        for (int r = 0; r < 120; ++r) {     // well past one loop: 84000 output frames over a 30000 frame file
            AudioMixer::RenderStereo(whole, o1.data(), 700);
            AudioMixer::RenderStereo(strm, o2.data(), 700);
            for (int i = 0; i < 1400; ++i) if (std::fabs(o1[i] - o2[i]) > 1e-5f) match = false;
            for (int i = 0; i < 700; ++i) { if (o2[2 * i + 1] != 0.0f) leftOnly = false; if (o2[2 * i] != 0.0f) nonzero = true; }
        }
        CHECK(match);
        CHECK(leftOnly && nonzero);   // stereo placement survives streaming: left file, right silent
        CHECK(b->IsPlaying() && a->IsPlaying());

        // Seek: the streamed source jumps and the very next buffer is the new position.
        ResetMixer(rate);
        Scene s2("Seek"); s2.physicsEnabled = false;
        auto* c = s2.CreateGameObject("C")->AddComponent<AudioSource>();
        c->clipPath = wav; c->stream = true;
        LoadSceneAudio(s2, "", rate);
        s2.Start();
        c->Play();
        std::vector<float> o(2 * 64);
        AudioMixer::RenderStereo(s2, o.data(), 64);
        c->Seek(0.5f);
        CHECK_NEAR(c->Time(), 0.5f, 1e-4);
        AudioMixer::RenderStereo(s2, o.data(), 64);
        CHECK_NEAR(o[0], (float)(rate / 2 + 1) / 32768.0f, 1e-6);
        CHECK_NEAR(o[2 * 63], (float)(rate / 2 + 64) / 32768.0f, 1e-6);
        CHECK(c->Time() > 0.5f);
        // Stop and Play again start over.
        c->Stop(); c->Play();
        AudioMixer::RenderStereo(s2, o.data(), 64);
        CHECK_NEAR(o[0], 1.0f / 32768.0f, 1e-6);
        // Remaining time counts down on a stream as it does on a clip.
        CHECK_NEAR(c->RemainingSeconds(), c->Duration() - 64.0f / rate, 1e-3);
        // A file that can't be streamed is reported, and the source stays silent rather than broken.
        Scene s3("Bad"); s3.physicsEnabled = false;
        auto* e = s3.CreateGameObject("E")->AddComponent<AudioSource>();
        e->clipPath = "nope.mp3"; e->stream = true;
        std::vector<std::string> failed;
        CHECK(LoadSceneAudio(s3, "", rate, &failed) == 0 && failed.size() == 1);
        e->Play();
        s3.Start();
        AudioMixer::RenderStereo(s3, o.data(), 64);
        CHECK(!e->IsPlaying());

        // The setting is saved and loaded with the scene (and old scene files, without it, still load).
        Scene src("Save"); src.physicsEnabled = false;
        auto* sa = src.CreateGameObject("S")->AddComponent<AudioSource>();
        sa->clipPath = "bgm.ogg"; sa->stream = true; sa->lowPass = 8000.0f;
        auto* sb = src.CreateGameObject("T")->AddComponent<AudioSource>();
        sb->clipPath = "hit.wav";
        auto* mpo = src.CreateGameObject("M")->AddComponent<MusicPlayer>();
        mpo->tracks = {"a.mp3", "b.ogg"}; mpo->stream = false;
        const std::string text = SceneSerializer::Serialize(src);
        Scene back("Back"); back.physicsEnabled = false;
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        bool sawStream = false, sawPlain = false, musicOk = false;
        for (AudioSource* x : back.FindObjectsOfType<AudioSource>()) {
            if (x->clipPath == "bgm.ogg") { sawStream = x->stream && x->lowPass == 8000.0f; }
            if (x->clipPath == "hit.wav") { sawPlain = !x->stream; }
        }
        for (MusicPlayer* m : back.FindObjectsOfType<MusicPlayer>()) musicOk = !m->stream && m->tracks.size() == 2;
        CHECK(sawStream && sawPlain && musicOk);
        std::string oldText = text;      // strip the stream flags the way an older file lacks them
        const std::size_t at = oldText.find("audio \"bgm.ogg\"");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            const std::size_t eol = oldText.find('\n', at);
            std::string line = oldText.substr(at, eol - at);
            line = line.substr(0, line.find_last_of(' '));
            oldText = oldText.substr(0, at) + line + oldText.substr(eol);
            Scene older("Older"); older.physicsEnabled = false;
            CHECK(SceneSerializer::Deserialize(older, oldText, &err));
            bool ok = false;
            for (AudioSource* x : older.FindObjectsOfType<AudioSource>()) if (x->clipPath == "bgm.ogg") ok = !x->stream && x->lowPass == 8000.0f;
            CHECK(ok);
        }

        // ---- the Music player streams its tracks --------------------------------------------------
        for (int streamed = 0; streamed < 2; ++streamed) {
            ResetMixer(rate);
            Scene ms("Music"); ms.physicsEnabled = false;
            auto* go = ms.CreateGameObject("Music");
            auto* mp = go->AddComponent<MusicPlayer>();
            mp->tracks = {wav};
            mp->stream = streamed == 1;
            mp->crossfade = 0.0f;
            ms.Start();
            CHECK(mp->IsPlaying());
            std::vector<float> om(2 * 256);
            AudioMixer::RenderStereo(ms, om.data(), 256);
            bool anyLeft = false, rightSilent = true;
            for (int i = 0; i < 256; ++i) { if (om[2 * i] > 0.0f) anyLeft = true; if (om[2 * i + 1] != 0.0f) rightSilent = false; }
            CHECK(anyLeft && rightSilent);       // stereo music keeps its sides
            bool sawStreaming = false;
            for (AudioSource* x : ms.FindObjectsOfType<AudioSource>()) if (x->IsStreaming()) sawStreaming = true;
            CHECK(sawStreaming == (streamed == 1));
            // Pause and resume work on a streamed track.
            mp->Pause();
            AudioMixer::RenderStereo(ms, om.data(), 256);
            CHECK(om[0] == 0.0f && om[2 * 255] == 0.0f);
            mp->Resume();
            AudioMixer::RenderStereo(ms, om.data(), 256);
            CHECK(om[2 * 255] != 0.0f);
            // The track's remaining time is known, so the playlist advances / loops on cue.
            for (AudioSource* x : ms.FindObjectsOfType<AudioSource>()) if (x->IsPlaying()) CHECK(x->RemainingSeconds() > 0.0f);
        }
        std::remove(wav.c_str());
    }

    for (const Fixture& fx : fixtures) if (fx.path.find(tmp) == 0) std::remove(fx.path.c_str());
    TEST_MAIN_RESULT();
}
