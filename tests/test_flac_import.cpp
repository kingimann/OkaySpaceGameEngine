// FLAC decodes to the samples that were encoded, and each audio import setting
// that the loader can honour changes what a load produces. Quality is stored
// for older files and is not applied: two files that differ only in that number
// load as the same clip.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/flac/";

bool ReadAll(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

void WriteMeta(const std::string& audioPath, const std::string& sets) {
    std::ofstream out(audioPath + ".okaymeta", std::ios::binary);
    out << "okaymeta 1\n"
        << "guid 11111111111111111111111111111111\n"
        << "type audio\n"
        << "importer audio 2\n"
        << sets;
}

float MaxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    float m = 0.0f;
    for (std::size_t i = 0; i < n; ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

// Compare a decoded clip to little-endian integer samples. `bps` is 16 or 24.
bool MatchesIntegers(const AudioClip& clip, const std::vector<unsigned char>& raw, int bps, float* err) {
    const int width = bps == 16 ? 2 : 4;
    if (raw.size() % static_cast<std::size_t>(width) != 0) return false;
    const std::size_t n = raw.size() / static_cast<std::size_t>(width);
    if (clip.samples.size() != n) return false;
    const float scale = bps == 16 ? 32768.0f : 8388608.0f;
    float worst = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        std::int32_t s = 0;
        if (bps == 16) {
            s = static_cast<std::int16_t>(raw[2 * i] | (raw[2 * i + 1] << 8));
        } else {
            std::uint32_t u = raw[4 * i] | (raw[4 * i + 1] << 8) | (raw[4 * i + 2] << 16) | (raw[4 * i + 3] << 24);
            s = static_cast<std::int32_t>(u);
        }
        const float d = std::fabs(clip.samples[i] - static_cast<float>(s) / scale);
        worst = std::max(worst, d);
    }
    if (err) *err = worst;
    return worst < 1.0e-6f;
}

std::vector<float> ReadStream(const std::string& path) {
    auto st = AudioStream::OpenFile(path);
    if (!st) return {};
    std::vector<float> out;
    const int ch = st->Channels();
    std::vector<float> buf(static_cast<std::size_t>(ch) * 1024);
    for (;;) {
        const std::size_t n = st->Read(buf.data(), 1024);
        if (n == 0) break;
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * static_cast<std::size_t>(ch)));
    }
    return out;
}


// The fixtures are ours: written by scripts/gen_flac_fixtures.py (our own encoder, from the FLAC format description),
// whose VORBIS_COMMENT vendor string says so. Also lists the header and first-subframe forms the frames use, so a
// regenerated set cannot silently stop covering one. Frame headers are found by their sync code and checked with
// the header CRC-8 (polynomial 0x07).
struct FlacForms {
    std::string vendor;
    std::set<int> blockCodes, rateCodes, chanCodes, bpsCodes, strategies, subTypes;   // subTypes: 0 constant, 1 verbatim, 8+n fixed n, 32 LPC
    int wastedFrames = 0, frames = 0;
};

unsigned char FlacCrc8(const unsigned char* p, std::size_t n) {
    unsigned c = 0;
    for (std::size_t i = 0; i < n; ++i) { c ^= p[i]; for (int b = 0; b < 8; ++b) c = (c & 0x80) ? ((c << 1) ^ 0x07) & 0xFF : (c << 1) & 0xFF; }
    return static_cast<unsigned char>(c);
}

FlacForms ScanFlac(const std::vector<unsigned char>& d) {
    FlacForms f;
    std::size_t pos = 4;
    for (bool last = false; !last && pos + 4 <= d.size();) {
        last = (d[pos] & 0x80) != 0;
        const int type = d[pos] & 0x7F;
        const std::size_t len = (std::size_t(d[pos + 1]) << 16) | (std::size_t(d[pos + 2]) << 8) | d[pos + 3];
        if (type == 4 && pos + 8 <= d.size()) {
            const std::size_t vl = d[pos + 4] | (d[pos + 5] << 8) | (d[pos + 6] << 16) | (std::size_t(d[pos + 7]) << 24);
            if (pos + 8 + vl <= d.size()) f.vendor.assign(d.begin() + static_cast<std::ptrdiff_t>(pos + 8), d.begin() + static_cast<std::ptrdiff_t>(pos + 8 + vl));
        }
        pos += 4 + len;
    }
    for (std::size_t i = pos; i + 6 < d.size(); ++i) {
        if (d[i] != 0xFF || (d[i + 1] & 0xFE) != 0xF8) continue;
        std::size_t L = 0;
        for (std::size_t n = 5; n <= 16 && i + n < d.size(); ++n) if (FlacCrc8(&d[i], n - 1) == d[i + n - 1]) { L = n; break; }
        if (!L || i + L >= d.size()) continue;
        ++f.frames;
        f.strategies.insert(d[i + 1] & 1);
        f.blockCodes.insert(d[i + 2] >> 4);
        f.rateCodes.insert(d[i + 2] & 15);
        f.chanCodes.insert(d[i + 3] >> 4);
        f.bpsCodes.insert((d[i + 3] >> 1) & 7);
        const int sub = (d[i + L] >> 1) & 0x3F;
        f.subTypes.insert(sub >= 32 ? 32 : sub);
        if (d[i + L] & 1) ++f.wastedFrames;
        i += L - 1;
    }
    return f;
}

} // namespace

int main() {
    okaytest::Sandbox sandbox("flac_import", OKAY_SOURCE_DIR);
    RUN_SUITE("flac_import");

    struct Fx {
        const char* name;
        const char* pcm;
        int rate;
        int channels;
        int bps;
    };
    const Fx files[] = {
        {"sine_mono_44k.flac", "sine_mono_44k.s16", 44100, 1, 16},
        {"tones_stereo_22k.flac", "tones_stereo_22k.s16", 22050, 2, 16},
        {"noise_mono_8k_fixed.flac", "noise_mono_8k.s16", 8000, 1, 16},
        {"noise_mono_8k_lpc.flac", "noise_mono_8k.s16", 8000, 1, 16},
        {"pcm24_mono_16k.flac", "pcm24_mono_16k.s32", 16000, 1, 24},
    };

    for (const Fx& fx : files) {
        AudioClip clip;
        std::string err;
        CHECK(clip.Load(kDir + fx.name, &err));
        CHECK(err.empty());
        CHECK(clip.sampleRate == fx.rate);
        CHECK(clip.channels == fx.channels);
        std::vector<unsigned char> raw;
        CHECK(ReadAll(kDir + fx.pcm, raw));
        float worst = 1.0f;
        CHECK(MatchesIntegers(clip, raw, fx.bps, &worst));
        std::cout << "  " << fx.name << " worst sample error " << worst << "\n";

        // A stream of the same file is the same samples, including a seek.
        const std::vector<float> streamed = ReadStream(kDir + fx.name);
        CHECK(streamed.size() == clip.samples.size());
        CHECK(MaxAbsDiff(streamed, clip.samples) < 1.0e-6f);
        auto st = AudioStream::OpenFile(kDir + fx.name);
        CHECK(st != nullptr);
        if (st && clip.FrameCount() > 1000) {
            CHECK(st->Seek(1000));
            std::vector<float> tail(clip.samples.size() - 1000 * static_cast<std::size_t>(clip.channels));
            const std::size_t n = st->Read(tail.data(), clip.FrameCount() - 1000);
            CHECK(n == clip.FrameCount() - 1000);
            const std::size_t off = 1000 * static_cast<std::size_t>(clip.channels);
            float seekErr = 0.0f;
            for (std::size_t i = 0; i < n * static_cast<std::size_t>(clip.channels); ++i)
                seekErr = std::max(seekErr, std::fabs(tail[i] - clip.samples[off + i]));
            CHECK(seekErr < 1.0e-6f);
        }
    }

    // The fixtures are our own encoder's output and, between them, use every header form and subframe type.
    {
        FlacForms all;
        for (const Fx& fx : files) {
            std::vector<unsigned char> b;
            CHECK(ReadAll(kDir + fx.name, b));
            const FlacForms f = ScanFlac(b);
            CHECK(f.vendor == "OkaySpace scripts/gen_flac_fixtures.py");
            if (f.vendor != "OkaySpace scripts/gen_flac_fixtures.py") std::cout << "  " << fx.name << " was written by: " << f.vendor << "\n";
            CHECK(f.frames >= 2);
            all.blockCodes.insert(f.blockCodes.begin(), f.blockCodes.end());
            all.rateCodes.insert(f.rateCodes.begin(), f.rateCodes.end());
            all.chanCodes.insert(f.chanCodes.begin(), f.chanCodes.end());
            all.bpsCodes.insert(f.bpsCodes.begin(), f.bpsCodes.end());
            all.strategies.insert(f.strategies.begin(), f.strategies.end());
            all.subTypes.insert(f.subTypes.begin(), f.subTypes.end());
            all.wastedFrames += f.wastedFrames;
        }
        CHECK((all.blockCodes == std::set<int>{1, 2, 3, 4, 5, 6, 7, 10}));
        CHECK((all.rateCodes == std::set<int>{0, 4, 5, 6, 9, 12, 13, 14}));
        CHECK((all.chanCodes == std::set<int>{0, 1, 8, 9, 10}));
        CHECK((all.bpsCodes == std::set<int>{0, 4, 6}));
        CHECK((all.strategies == std::set<int>{0, 1}));                       // fixed and variable blocking
        CHECK((all.subTypes == std::set<int>{0, 1, 8, 9, 10, 11, 12, 32}));    // constant, verbatim, fixed 0-4, LPC
        CHECK(all.wastedFrames >= 1);
    }

    CHECK(AudioClip::IsAudioFile("song.FLAC"));
    CHECK(!AudioClip::IsAudioFile("song.txt"));
    {
        AudioClip bad;
        std::string err;
        CHECK(!bad.Load(kDir + "does-not-exist.flac", &err));
        CHECK(!err.empty());
        const std::string junk = "not_flac.flac";
        { std::ofstream f(junk, std::ios::binary); f << "fLaC this is not a real frame"; }
        CHECK(!bad.Load(junk, &err));
        CHECK(bad.samples.empty());
    }

    // Mix down to one channel changes a stereo clip into the average of its channels.
    {
        const std::string stereo = "import_stereo.wav";
        const std::string mono = "import_mono.wav";
        AudioClip src = AudioClip::FromChannels({0.5f, -0.25f, 0.0f}, {-0.5f, 0.25f, 1.0f}, 8000);
        CHECK(src.SaveWAV(stereo));
        CHECK(src.SaveWAV(mono));
        AudioClip plain;
        CHECK(plain.Load(stereo));
        CHECK(plain.channels == 2);
        CHECK(plain.FrameCount() == 3);
        WriteMeta(mono, "set loadMode decode\nset forceMono 1\nset quality 0.2\n");
        AudioClip mixed;
        CHECK(mixed.Load(mono));
        CHECK(mixed.channels == 1);
        CHECK(mixed.FrameCount() == plain.FrameCount());
        for (std::size_t i = 0; i < plain.FrameCount(); ++i) {
            const float avg = 0.5f * (plain.samples[2 * i] + plain.samples[2 * i + 1]);
            CHECK_NEAR(mixed.samples[i], avg, 1.0e-6);
        }
        CHECK(MaxAbsDiff(plain.samples, mixed.samples) > 0.1f);

        // The same mix on a stream.
        auto st = AudioStream::OpenFile(mono);
        CHECK(st && st->Channels() == 1);
        std::vector<float> got(8, 99.0f);
        CHECK(st->Read(got.data(), 3) == 3);
        for (std::size_t i = 0; i < 3; ++i) CHECK_NEAR(got[i], mixed.samples[i], 1.0e-6);
    }

    // Read while it plays leaves the in-memory clip empty and a stream in its place.
    // Load the whole file fills the clip. The samples that come out are the same.
    {
        const std::string whole = "import_decode.wav";
        const std::string streamed = "import_stream.wav";
        CHECK(AudioClip::Sine(440.0f, 0.05f, 0.4f, 8000).SaveWAV(whole));
        CHECK(AudioClip::Sine(440.0f, 0.05f, 0.4f, 8000).SaveWAV(streamed));
        WriteMeta(whole, "set loadMode decode\nset forceMono 0\nset quality 0.1\n");
        WriteMeta(streamed, "set loadMode stream\nset forceMono 0\nset quality 0.9\n");

        Scene decoded("Decoded");
        decoded.physicsEnabled = false;
        auto* a = decoded.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clipPath = whole;
        a->stream = false;
        CHECK(LoadSceneAudio(decoded, "", 8000) == 1);
        CHECK(!a->IsStreaming());
        CHECK(a->Clip().FrameCount() == 400);
        CHECK(a->Clip().channels == 1);

        Scene streaming("Streaming");
        streaming.physicsEnabled = false;
        auto* b = streaming.CreateGameObject("B")->AddComponent<AudioSource>();
        b->clipPath = streamed;
        b->stream = false;
        CHECK(LoadSceneAudio(streaming, "", 8000) == 1);
        CHECK(b->IsStreaming());
        CHECK(b->Clip().samples.empty());
        CHECK(b->Stream() && b->Stream()->Channels() == 1);
        std::vector<float> fromStream(400);
        CHECK(b->Stream()->Read(fromStream.data(), 400) == 400);
        CHECK(MaxAbsDiff(fromStream, a->Clip().samples) < 1.0e-5f);

        // An older sidecar (stream / mono) still takes effect.
        const std::string legacy = "import_legacy.wav";
        AudioClip wide = AudioClip::FromChannels({0.4f, 0.2f}, {0.0f, 0.2f}, 8000);
        CHECK(wide.SaveWAV(legacy));
        {
            std::ofstream out(legacy + ".okaymeta", std::ios::binary);
            out << "okaymeta 1\nimporter audio 1\nset stream 1\nset mono 1\nset quality 40\n";
        }
        Scene old("Legacy");
        old.physicsEnabled = false;
        auto* c = old.CreateGameObject("C")->AddComponent<AudioSource>();
        c->clipPath = legacy;
        c->stream = false;
        CHECK(LoadSceneAudio(old, "", 8000) == 1);
        CHECK(c->IsStreaming());
        CHECK(c->Stream() && c->Stream()->Channels() == 1);

        // Quality is kept in the file and does not change the samples.
        const std::string q0 = "import_q0.wav";
        const std::string q1 = "import_q1.wav";
        CHECK(AudioClip::Sine(220.0f, 0.02f, 0.3f, 8000).SaveWAV(q0));
        CHECK(AudioClip::Sine(220.0f, 0.02f, 0.3f, 8000).SaveWAV(q1));
        WriteMeta(q0, "set loadMode decode\nset forceMono 0\nset quality 0\n");
        WriteMeta(q1, "set loadMode decode\nset forceMono 0\nset quality 1\n");
        AudioClip low, high;
        CHECK(low.Load(q0) && high.Load(q1));
        CHECK(low.samples.size() == high.samples.size());
        CHECK(MaxAbsDiff(low.samples, high.samples) == 0.0f);
    }

    // Music Player with Stream off still reads a file whose import setting says to.
    {
        const std::string track = "import_music.wav";
        CHECK(AudioClip::Sine(330.0f, 0.05f, 0.2f, 8000).SaveWAV(track));
        WriteMeta(track, "set loadMode stream\nset forceMono 0\nset quality 0.7\n");
        Scene s("Music");
        s.physicsEnabled = false;
        auto* mp = s.CreateGameObject("Player")->AddComponent<MusicPlayer>();
        mp->tracks.push_back(track);
        mp->stream = false;
        mp->playOnStart = false;
        mp->crossfade = 0.0f;
        s.Start();
        CHECK(mp->Play(0));
        AudioSource* voice = nullptr;
        for (AudioSource* au : s.FindObjectsOfType<AudioSource>()) {
            if (au->IsStreaming()) voice = au;
        }
        CHECK(voice != nullptr);
        CHECK(voice->Stream() && voice->Stream()->SampleRate() == 8000);
    }

    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
