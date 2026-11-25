// Drives the engine's own engine/src/Audio/WavDecoder.cpp, compiled unchanged by okay-cc (round 21:
// std::nearbyint, std::string, std::vector of strings, enum class). Builds small WAV files in memory
// (PCM 8/16/24-bit, float, A-law, mu-law, a cut data chunk, broken headers) and prints what the
// decoder makes of each: format, frame count, warnings, and a checksum of the samples' bits.
#include "okay/Audio/WavDecoder.hpp"

#include <cstring>

extern "C" int printf(const char *fmt, ...);

static void put16(std::vector<unsigned char> &b, unsigned v) {
    b.push_back((unsigned char)(v & 255));
    b.push_back((unsigned char)((v >> 8) & 255));
}
static void put32(std::vector<unsigned char> &b, unsigned v) {
    put16(b, v & 65535);
    put16(b, v >> 16);
}
static void tag(std::vector<unsigned char> &b, const char *t) {
    for (int i = 0; i < 4; ++i) b.push_back((unsigned char)t[i]);
}

// A RIFF WAVE file: fmt chunk (format code, channels, rate, bits) and a data chunk of `data`.
static std::vector<unsigned char> wav(unsigned fmt, unsigned ch, unsigned rate, unsigned bits,
                                      const std::vector<unsigned char> &data, unsigned dataLenField) {
    std::vector<unsigned char> b;
    tag(b, "RIFF");
    put32(b, (unsigned)(4 + 8 + 16 + 8 + data.size()));
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, fmt);
    put16(b, ch);
    put32(b, rate);
    put32(b, rate * ch * (bits / 8));
    put16(b, ch * (bits / 8));
    put16(b, bits);
    tag(b, "data");
    put32(b, dataLenField);
    for (unsigned char c : data) b.push_back(c);
    return b;
}

static unsigned checksum(const std::vector<float> &s) {
    unsigned h = 2166136261u;
    for (unsigned long i = 0; i < s.size(); ++i) {
        unsigned bitsOf;
        std::memcpy(&bitsOf, &s[i], 4);
        h = (h ^ bitsOf) * 16777619u;
    }
    return h;
}

static void run(const char *name, const std::vector<unsigned char> &file) {
    okay::wav::WavAudio a;
    std::string err;
    bool ok = okay::wav::DecodeWav(file.data(), file.size(), a, &err);
    printf("%s: ok %d", name, ok ? 1 : 0);
    if (!ok) {
        printf(" error \"%s\"\n", err.c_str());
        return;
    }
    printf(" rate %d ch %d enc %d bits %d frames %d sum %08x", a.sampleRate, a.channels, (int)a.encoding,
           a.bitsPerSample, (int)a.FrameCount(), checksum(a.samples));
    if (!a.samples.empty()) printf(" first %.6f last %.6f", a.samples[0], a.samples[a.samples.size() - 1]);
    for (const std::string &w : a.warnings) printf(" [%s]", w.c_str());
    std::vector<std::int16_t> s16 = okay::wav::ToS16(a);
    unsigned long t = 0;
    for (std::int16_t v : s16) t = t * 3 + (unsigned long)(long)v;
    printf(" s16 %lu\n", t);
}

int main() {
    std::vector<unsigned char> pcm16;
    for (int i = 0; i < 40; ++i) put16(pcm16, (unsigned)((i * 1637 - 30000) & 65535));
    run("pcm16 stereo", wav(1, 2, 44100, 16, pcm16, (unsigned)pcm16.size()));
    std::vector<unsigned char> pcm8;
    for (int i = 0; i < 33; ++i) pcm8.push_back((unsigned char)(i * 7 + 3));
    run("pcm8 mono", wav(1, 1, 8000, 8, pcm8, (unsigned)pcm8.size()));
    std::vector<unsigned char> pcm24;
    for (int i = 0; i < 30; ++i) {
        unsigned v = (unsigned)(i * 300007) & 0xFFFFFF;
        pcm24.push_back((unsigned char)(v & 255));
        pcm24.push_back((unsigned char)((v >> 8) & 255));
        pcm24.push_back((unsigned char)((v >> 16) & 255));
    }
    run("pcm24 mono", wav(1, 1, 48000, 24, pcm24, (unsigned)pcm24.size()));
    std::vector<unsigned char> flt;
    for (int i = 0; i < 12; ++i) {
        float f = (float)i / 7.0f - 0.8f;
        unsigned u;
        std::memcpy(&u, &f, 4);
        put32(flt, u);
    }
    run("float stereo", wav(3, 2, 22050, 32, flt, (unsigned)flt.size()));
    std::vector<unsigned char> law;
    for (int i = 0; i < 64; ++i) law.push_back((unsigned char)(i * 4 + 1));
    run("alaw", wav(6, 1, 8000, 8, law, (unsigned)law.size()));
    run("mulaw", wav(7, 1, 8000, 8, law, (unsigned)law.size()));
    run("cut data", wav(1, 1, 8000, 16, pcm16, 0xFFFFFFFFu));
    std::vector<unsigned char> odd = pcm16;
    odd.push_back(9);
    run("half frame", wav(1, 2, 8000, 16, odd, (unsigned)odd.size()));
    std::vector<unsigned char> bad = wav(1, 1, 8000, 16, pcm16, (unsigned)pcm16.size());
    bad[0] = 'X';
    run("not riff", bad);
    run("zero channels", wav(1, 0, 8000, 16, pcm16, (unsigned)pcm16.size()));
    run("too short", std::vector<unsigned char>(10, 0));
    return 0;
}
