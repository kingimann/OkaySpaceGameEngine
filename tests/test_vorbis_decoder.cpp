// Tests for the engine's own Ogg Vorbis decoder (engine/src/Audio/VorbisDecoder.cpp).
//
// Accuracy: tests/data/audio/ogg holds small .ogg files with reference PCM (the
// *.s16 files, 16-bit, scaled by 2^gainLog2 from manifest.txt) that the
// reference decoder libvorbis produced (ov_read_float, written by a throwaway
// program). The decoder output must match it to at least kMinSnrDb and have
// exactly the same length and channel count; the measured SNR of every file is
// printed so a run documents the accuracy. The 16-bit references cap the
// measurable SNR at about 90 dB; against the float output of libvorbis the same
// files match at 129 to 140 dB (float rounding), and against ffmpeg's own
// decoder at about 137 dB.
//   * enc_*: real encoder output (libvorbis through ffmpeg; one file from
//     ffmpeg's native encoder) from a sweep + noise + click signal: mono, stereo
//     and 3 to 8 channels, 8 to 48 kHz, quality -1 to 10, long and short blocks.
//   * syn_*: random but valid streams made by a throwaway generator (random
//     setup headers, random packet bits; complete prefix codes make every bit
//     string decodable) to reach what encoders never emit: floor type 0 (no
//     current encoder writes it; the two floor 0 files are the only coverage of
//     it that has a reference), residue types 0, 1 and 2, codebook lookup types
//     0, 1 and 2, ordered and sparse codebooks,
//     several submaps, channel coupling with 2 to 8 channels, block sizes from
//     64 to 8192 and equal block sizes, packets that end early, and a final
//     granule position that trims the last packet.
// Structure: Ogg framing (continued packets, one packet per page, foreign
// streams, chained streams), granule trimming, hostile headers (huge codebooks,
// bad block sizes, a damaged floor 0 header), truncation at every length, bit flips with and
// without valid page CRCs, noise. Everything here runs under ASan/UBSan in CI's
// sanitizer job.
#include "test_framework.hpp"
#include "okay/Audio/VorbisDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/ogg/";
constexpr double kMinSnrDb = 60.0;

typedef std::vector<unsigned char> Bytes;

bool ReadAll(const std::string& path, Bytes& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Small deterministic generator (the standard <random> engines differ between libraries).
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 1442695040888963407ull) {}
    std::uint32_t Next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<std::uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    std::uint32_t Below(std::uint32_t n) { return n ? Next() % n : 0; }
};

bool AllFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

VorbisAudio Decode(const Bytes& d, bool* ok = nullptr, const VorbisOptions* opt = nullptr, std::string* err = nullptr) {
    VorbisAudio a;
    const bool r = DecodeVorbis(d.data(), d.size(), a, err, opt);
    if (ok) *ok = r;
    return a;
}

// ---- Ogg writing (just enough to build hostile and re-paginated streams) ---------------------------

std::uint32_t Crc(const Bytes& p) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            table[i] = r;
        }
        init = true;
    }
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const unsigned char b = (i >= 22 && i < 26) ? 0 : p[i];
        crc = (crc << 8) ^ table[((crc >> 24) ^ b) & 0xFF];
    }
    return crc;
}

void Put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i))); }

struct Seg { std::vector<unsigned char> lacing, body; };

// Appends one page. `lacing` and `body` must agree.
void AppendPage(Bytes& out, const Seg& s, unsigned flags, std::int64_t granule, std::uint32_t serial, std::uint32_t seq) {
    Bytes p = {'O', 'g', 'g', 'S', 0, static_cast<unsigned char>(flags)};
    for (int i = 0; i < 8; ++i) p.push_back(static_cast<unsigned char>(static_cast<std::uint64_t>(granule) >> (8 * i)));
    Put32(p, serial);
    Put32(p, seq);
    Put32(p, 0);
    p.push_back(static_cast<unsigned char>(s.lacing.size()));
    p.insert(p.end(), s.lacing.begin(), s.lacing.end());
    p.insert(p.end(), s.body.begin(), s.body.end());
    const std::uint32_t c = Crc(p);
    for (int i = 0; i < 4; ++i) p[22 + i] = static_cast<unsigned char>(c >> (8 * i));
    out.insert(out.end(), p.begin(), p.end());
}

// Recomputes every page CRC in `d` (so mutated payloads reach the decoder).
void FixCrcs(Bytes& d) {
    for (std::size_t pos = 0; pos + 27 <= d.size();) {
        if (std::memcmp(&d[pos], "OggS", 4) != 0) { ++pos; continue; }
        const std::size_t nseg = d[pos + 26];
        if (pos + 27 + nseg > d.size()) break;
        std::size_t body = 0;
        for (std::size_t i = 0; i < nseg; ++i) body += d[pos + 27 + i];
        const std::size_t total = 27 + nseg + body;
        if (pos + total > d.size()) break;
        Bytes page(d.begin() + static_cast<std::ptrdiff_t>(pos), d.begin() + static_cast<std::ptrdiff_t>(pos + total));
        const std::uint32_t c = Crc(page);
        for (int i = 0; i < 4; ++i) d[pos + 22 + i] = static_cast<unsigned char>(c >> (8 * i));
        pos += total;
    }
}

struct ParsedStream {
    std::vector<Bytes> packets;
    std::int64_t eosGranule = -1;
    std::uint32_t serial = 0;
};

// Reads the packets of a single-stream, well-formed Ogg file (test files only).
bool ParseOgg(const Bytes& d, ParsedStream& out) {
    out = ParsedStream();
    Bytes cur;
    std::size_t pos = 0;
    while (pos + 27 <= d.size()) {
        if (std::memcmp(&d[pos], "OggS", 4) != 0) return false;
        const unsigned flags = d[pos + 5];
        std::uint64_t g = 0;
        for (int i = 7; i >= 0; --i) g = (g << 8) | d[pos + 6 + i];
        out.serial = static_cast<std::uint32_t>(d[pos + 14] | (d[pos + 15] << 8) | (d[pos + 16] << 16) | (std::uint32_t(d[pos + 17]) << 24));
        const std::size_t nseg = d[pos + 26];
        std::size_t body = pos + 27 + nseg;
        for (std::size_t i = 0; i < nseg; ++i) {
            const std::size_t l = d[pos + 27 + i];
            cur.insert(cur.end(), d.begin() + static_cast<std::ptrdiff_t>(body), d.begin() + static_cast<std::ptrdiff_t>(body + l));
            body += l;
            if (l < 255) { out.packets.push_back(cur); cur.clear(); }
        }
        if (flags & 4) out.eosGranule = static_cast<std::int64_t>(g);
        pos = body;
    }
    return !out.packets.empty();
}

// Packs packets into pages: `maxSegs` lacing values per page at most, the
// header packets (first three) each ending their page, granule positions of
// the audio packets computed from `sizes` by the caller (only the last one
// matters here: `eosGranule`).
Bytes BuildOgg(const std::vector<Bytes>& packets, std::int64_t eosGranule, std::size_t maxSegs, std::uint32_t serial = 0x51a1,
               unsigned onePacketPerPage = 0) {
    Bytes out;
    Seg cur;
    std::uint32_t seq = 0;
    bool cont = false, first = true;
    auto flush = [&](bool last, bool nextCont) {
        AppendPage(out, cur, (cont ? 1u : 0u) | (first ? 2u : 0u) | (last ? 4u : 0u), last ? eosGranule : 0, serial, seq++);
        cur = Seg();
        first = false;
        cont = nextCont;
    };
    for (std::size_t pi = 0; pi < packets.size(); ++pi) {
        const Bytes& p = packets[pi];
        std::size_t pos = 0;
        for (;;) {
            const std::size_t l = std::min<std::size_t>(255, p.size() - pos);
            if (cur.lacing.size() >= maxSegs) flush(false, pos > 0);
            cur.lacing.push_back(static_cast<unsigned char>(l));
            cur.body.insert(cur.body.end(), p.begin() + static_cast<std::ptrdiff_t>(pos), p.begin() + static_cast<std::ptrdiff_t>(pos + l));
            pos += l;
            if (l < 255) break;
            if (pos == p.size()) {
                if (cur.lacing.size() >= maxSegs) flush(false, true);
                cur.lacing.push_back(0);
                break;
            }
        }
        const bool header = pi < 3;
        if (pi + 1 == packets.size()) flush(true, false);
        else if (header || onePacketPerPage) flush(false, false);
    }
    return out;
}

// The same packets with one packet per page (what a streaming encoder writes): damage to a page then costs one packet.
Bytes Repage(const Bytes& src) {
    ParsedStream ps;
    if (!ParseOgg(src, ps)) return Bytes();
    return BuildOgg(ps.packets, ps.eosGranule, 255, ps.serial, 1);
}

// ---- bit writer and setup-header builder for hostile streams --------------------------------------

struct BitWriter {
    Bytes b;
    int nbits = 0;
    void Put(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            if ((nbits & 7) == 0) b.push_back(0);
            if ((v >> i) & 1) b.back() = static_cast<unsigned char>(b.back() | (1u << (nbits & 7)));
            ++nbits;
        }
    }
};

Bytes IdentPacket(int channels, std::uint32_t rate, int b0, int b1, std::uint32_t version = 0, int framing = 1) {
    BitWriter w;
    w.Put(version, 32); w.Put(static_cast<std::uint32_t>(channels), 8); w.Put(rate, 32);
    w.Put(0, 32); w.Put(64000, 32); w.Put(0, 32);
    w.Put(static_cast<std::uint32_t>(b0), 4); w.Put(static_cast<std::uint32_t>(b1), 4); w.Put(static_cast<std::uint32_t>(framing), 1);
    Bytes p = {1, 'v', 'o', 'r', 'b', 'i', 's'};
    p.insert(p.end(), w.b.begin(), w.b.end());
    return p;
}

Bytes CommentPacket() {
    BitWriter w;
    w.Put(4, 32); for (char c : std::string("test")) w.Put(static_cast<unsigned char>(c), 8);
    w.Put(0, 32); w.Put(1, 1);
    Bytes p = {3, 'v', 'o', 'r', 'b', 'i', 's'};
    p.insert(p.end(), w.b.begin(), w.b.end());
    return p;
}

// One codebook with two 1-bit codewords and no lookup.
void SimpleBook(BitWriter& w) {
    w.Put(0x564342, 24); w.Put(1, 16); w.Put(2, 24); w.Put(0, 1); w.Put(0, 1);
    w.Put(0, 5); w.Put(0, 5);                // lengths 1 and 1
    w.Put(0, 4);                             // no lookup
}

enum class Hostile { None, FloorZero, BadFloorType, HugeBookUnordered, HugeBookOrdered, ZeroDims, LookupHuge, BadSync, BadTimeTransform,
                     ResidueHuge, MissingBook, BadMapping, BadMode, NoFraming, CouplingSelf };

// A complete minimal setup header (one book, floor 1 with only its two end points, one empty residue,
// one mapping, one mode), with one deliberate defect when `h` says so.
Bytes SetupPacket(Hostile h, int channels = 1) {
    BitWriter w;
    w.Put(0, 8);                                              // one codebook
    if (h == Hostile::BadSync) { w.Put(0x123456, 24); w.Put(1, 16); w.Put(2, 24); w.Put(0, 1); w.Put(0, 1); w.Put(0, 5); w.Put(0, 5); w.Put(0, 4); }
    else if (h == Hostile::HugeBookUnordered) { w.Put(0x564342, 24); w.Put(1, 16); w.Put(0xFFFFFF, 24); w.Put(0, 1); w.Put(0, 1); for (int i = 0; i < 64; ++i) w.Put(0, 5); }
    else if (h == Hostile::HugeBookOrdered) { w.Put(0x564342, 24); w.Put(1, 16); w.Put(0xFFFFFF, 24); w.Put(1, 1); w.Put(0, 5); w.Put(0xFFFFFF, 24); w.Put(0, 4); }
    else if (h == Hostile::ZeroDims) { w.Put(0x564342, 24); w.Put(0, 16); w.Put(2, 24); w.Put(0, 1); w.Put(0, 1); w.Put(0, 5); w.Put(0, 5); w.Put(0, 4); }
    else if (h == Hostile::LookupHuge) {
        w.Put(0x564342, 24); w.Put(65535, 16); w.Put(0xFFFF, 24); w.Put(0, 1); w.Put(0, 1);
        for (int i = 0; i < 0xFFFF; ++i) w.Put(15, 5);        // 16-bit codewords for 65535 entries: not complete, rejected anyway
        w.Put(2, 4); w.Put(0, 32); w.Put(0, 32); w.Put(15, 4); w.Put(0, 1);
    }
    else SimpleBook(w);
    w.Put(0, 6); w.Put(h == Hostile::BadTimeTransform ? 5 : 0, 16);   // one time domain transform
    w.Put(0, 6);                                              // one floor
    w.Put(h == Hostile::FloorZero ? 0 : (h == Hostile::BadFloorType ? 7 : 1), 16);
    w.Put(0, 5);                                              // no partitions
    w.Put(0, 2); w.Put(7, 4);                                 // multiplier 1, range bits 7
    w.Put(0, 6);                                              // one residue
    w.Put(1, 16); w.Put(h == Hostile::ResidueHuge ? 0xFFFFFF : 0, 24); w.Put(h == Hostile::ResidueHuge ? 0xFFFFFF : 0, 24);
    w.Put(h == Hostile::ResidueHuge ? 0xFFFFFF : 31, 24); w.Put(0, 6); w.Put(h == Hostile::MissingBook ? 5 : 0, 8);
    w.Put(0, 3); w.Put(0, 1);                                 // class 0: no cascade
    w.Put(0, 6);                                              // one mapping
    w.Put(h == Hostile::BadMapping ? 3 : 0, 16);
    w.Put(0, 1);                                              // one submap
    if (channels >= 2) {
        w.Put(1, 1); w.Put(0, 8);                             // one coupling step
        int bits = 0; for (int v = channels - 1; v; v >>= 1) ++bits;
        w.Put(0, bits); w.Put(h == Hostile::CouplingSelf ? 0 : 1, bits);
    } else w.Put(0, 1);
    w.Put(0, 2); w.Put(0, 8);
    w.Put(0, 8); w.Put(0, 8);
    w.Put(0, 6);                                              // one mode
    w.Put(0, 1); w.Put(0, 16); w.Put(0, 16); w.Put(h == Hostile::BadMode ? 9 : 0, 8);
    w.Put(h == Hostile::NoFraming ? 0 : 1, 1);
    Bytes p = {5, 'v', 'o', 'r', 'b', 'i', 's'};
    p.insert(p.end(), w.b.begin(), w.b.end());
    return p;
}

// A packet of the minimal stream: audio, floor in use (flag + two 8-bit points), nothing else.
Bytes SilentAudioPacket(int channels) {
    BitWriter w;
    w.Put(0, 1);                                              // audio packet; one mode, so no mode bits
    for (int c = 0; c < channels; ++c) { w.Put(1, 1); w.Put(100, 8); w.Put(100, 8); }
    return w.b;
}

Bytes MinimalStream(Hostile h, int channels, int packets, std::int64_t eosGranule, int b0 = 8, int b1 = 8) {
    std::vector<Bytes> pk = {IdentPacket(channels, 22050, b0, b1), CommentPacket(), SetupPacket(h, channels)};
    for (int i = 0; i < packets; ++i) pk.push_back(SilentAudioPacket(channels));
    return BuildOgg(pk, eosGranule, 255);
}

struct Entry { std::string name; int rate; int channels; int gainLog2; std::size_t frames; };

std::vector<Entry> LoadManifest() {
    std::vector<Entry> list;
    std::ifstream f(kDir + "manifest.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Entry e;
        if (ss >> e.name >> e.rate >> e.channels >> e.gainLog2 >> e.frames) list.push_back(e);
    }
    return list;
}

double Snr(const std::vector<float>& got, double gain, const Bytes& refBytes) {
    const std::size_t n = std::min(got.size(), refBytes.size() / 2);
    double sig = 0.0, err = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::int16_t r16 = static_cast<std::int16_t>(refBytes[2 * i] | (refBytes[2 * i + 1] << 8));
        const double ref = r16 / 32768.0;
        const double d = got[i] * gain - ref;
        sig += ref * ref;
        err += d * d;
    }
    return err > 0.0 ? 10.0 * std::log10(sig / err) : 200.0;
}

} // namespace

int main() {
    RUN_SUITE("vorbis_decoder");

    // ---- accuracy against the reference decoder ------------------------------------------------
    const std::vector<Entry> manifest = LoadManifest();
    CHECK(manifest.size() >= 28);
    double worst = 1000.0;
    for (const Entry& e : manifest) {
        Bytes ogg, refBytes;
        const bool haveFiles = ReadAll(kDir + e.name + ".ogg", ogg) && ReadAll(kDir + e.name + ".s16", refBytes);
        CHECK(haveFiles);
        if (!haveFiles) continue;
        bool ok = false;
        const VorbisAudio a = Decode(ogg, &ok);
        CHECK(ok);
        CHECK(a.sampleRate == e.rate);
        CHECK(a.channels == e.channels);
        CHECK(a.FrameCount() == e.frames);
        CHECK(a.samples.size() == refBytes.size() / 2);
        CHECK(AllFinite(a.samples));
        CHECK(a.streamsFound == 1 && a.streamsDecoded == 1);
        const double snr = Snr(a.samples, std::ldexp(1.0, e.gainLog2), refBytes);
        worst = std::min(worst, snr);
        std::cout << "  " << e.name << ": " << a.channels << " ch " << a.sampleRate << " Hz, " << a.FrameCount()
                  << " frames, SNR vs libvorbis " << snr << " dB\n";
        CHECK(snr >= kMinSnrDb);
    }
    std::cout << "  worst SNR " << worst << " dB over " << manifest.size() << " files\n";

    Bytes mono, stereo, six, trimmed, tiny, tinyEop;
    CHECK(ReadAll(kDir + "enc_mono_11k_q2.ogg", mono));
    CHECK(ReadAll(kDir + "enc_stereo_22k_q3.ogg", stereo));
    CHECK(ReadAll(kDir + "enc_6ch_22k_q5.ogg", six));
    CHECK(ReadAll(kDir + "syn_stereo_granule_trim.ogg", trimmed));
    CHECK(ReadAll(kDir + "syn_mono_blocks_64_64.ogg", tiny));
    CHECK(ReadAll(kDir + "syn_stereo_truncated_packets.ogg", tinyEop));
    const VorbisAudio monoFull = Decode(mono);
    const VorbisAudio stereoFull = Decode(stereo);

    // ---- Ogg framing: the same packets in every kind of page layout decode identically ---------------
    {
        const char* names[] = {"enc_stereo_44k_q5", "syn_stereo_coupling_res2", "syn_5ch_coupling_res1", "enc_mono_8k_q0"};
        for (const char* name : names) {
            Bytes src;
            CHECK(ReadAll(kDir + name + std::string(".ogg"), src));
            ParsedStream ps;
            CHECK(ParseOgg(src, ps));
            const VorbisAudio ref = Decode(src);
            CHECK(!ref.samples.empty());
            // One packet per page, a handful of segments per page, a single segment per page (every packet is
            // continued over many pages), and 255 segments per page.
            for (std::size_t segs : {255u, 7u, 1u}) {
                for (unsigned onePer : {0u, 1u}) {
                    const Bytes re = BuildOgg(ps.packets, ps.eosGranule, segs, 77, onePer);
                    bool ok = false;
                    const VorbisAudio a = Decode(re, &ok);
                    CHECK(ok && a.samples == ref.samples && a.channels == ref.channels && a.sampleRate == ref.sampleRate);
                }
            }
        }
    }

    // ---- foreign and chained streams ----------------------------------------------------------------
    {
        // A foreign stream (not Vorbis) in front of the Vorbis one and pages of it interleaved with the audio.
        ParsedStream ps;
        CHECK(ParseOgg(stereo, ps));
        const Bytes plain = BuildOgg(ps.packets, ps.eosGranule, 255, 4242, 1);
        Bytes mixed;
        Seg foreignBos;
        foreignBos.lacing = {8};
        foreignBos.body = {'f', 'a', 'k', 'e', 'n', 'o', 't', 'v'};
        AppendPage(mixed, foreignBos, 2, 0, 9999, 0);
        std::size_t pos = 0;
        std::uint32_t fseq = 1;
        while (pos + 27 <= plain.size()) {
            const std::size_t nseg = plain[pos + 26];
            std::size_t body = 0;
            for (std::size_t i = 0; i < nseg; ++i) body += plain[pos + 27 + i];
            const std::size_t total = 27 + nseg + body;
            mixed.insert(mixed.end(), plain.begin() + static_cast<std::ptrdiff_t>(pos), plain.begin() + static_cast<std::ptrdiff_t>(pos + total));
            Seg foreign;
            foreign.lacing = {4};
            foreign.body = {1, 2, 3, 4};
            AppendPage(mixed, foreign, 0, 5, 9999, fseq++);
            pos += total;
        }
        bool ok = false;
        const VorbisAudio a = Decode(mixed, &ok);
        CHECK(ok && a.samples == stereoFull.samples && a.streamsFound == 1);

        // Chained streams: the first one by default, as the previous decoder did.
        Bytes chain = mono;
        chain.insert(chain.end(), stereo.begin(), stereo.end());
        const VorbisAudio first = Decode(chain, &ok);
        CHECK(ok && first.samples == monoFull.samples && first.channels == 1 && first.sampleRate == 11025);
        CHECK(first.streamsFound == 2 && first.streamsDecoded == 1);
        // All streams: a stream with another format cannot be appended.
        VorbisOptions all;
        all.allStreams = true;
        const VorbisAudio mixedFormat = Decode(chain, &ok, &all);
        CHECK(ok && mixedFormat.samples == monoFull.samples && mixedFormat.streamsFound == 2 && mixedFormat.streamsDecoded == 1);
        // Same format: appended end to end.
        Bytes twice = stereo;
        twice.insert(twice.end(), stereo.begin(), stereo.end());
        twice.insert(twice.end(), stereo.begin(), stereo.end());
        const VorbisAudio three = Decode(twice, &ok, &all);
        CHECK(ok && three.streamsDecoded == 3 && three.samples.size() == 3 * stereoFull.samples.size());
        bool same = true;
        for (std::size_t i = 0; i < three.samples.size() && same; ++i) same = three.samples[i] == stereoFull.samples[i % stereoFull.samples.size()];
        CHECK(same);
        // A damaged first stream: the next valid one is used.
        Bytes brokenFirst = mono;
        for (std::size_t i = 30; i < 50 && i < brokenFirst.size(); ++i) brokenFirst[i] ^= 0x5A;
        FixCrcs(brokenFirst);
        brokenFirst.insert(brokenFirst.end(), stereo.begin(), stereo.end());
        const VorbisAudio second = Decode(brokenFirst, &ok);
        CHECK(ok && second.samples == stereoFull.samples);
    }

    // ---- granule positions, trimming, end of stream ----------------------------------------------------
    {
        // The last page's granule position is the length (exercised by the manifest too: syn_stereo_granule_trim).
        ParsedStream ps;
        CHECK(ParseOgg(trimmed, ps));
        const std::size_t trimmedFrames = Decode(trimmed).FrameCount();
        CHECK(ps.eosGranule == static_cast<std::int64_t>(trimmedFrames));
        // A larger granule position than what was decoded does not pad; no end-of-stream flag marks the result truncated.
        const Bytes longer = BuildOgg(ps.packets, ps.eosGranule + 5000, 255);
        const VorbisAudio l = Decode(longer);
        CHECK(l.FrameCount() >= trimmedFrames);
        CHECK(!l.truncated);
        // Zero and negative granule positions.
        for (std::int64_t g : {std::int64_t(0), std::int64_t(-1), std::int64_t(1), std::int64_t(100)}) {
            const VorbisAudio a = Decode(BuildOgg(ps.packets, g, 255));
            if (g == 0) CHECK(a.samples.empty() || a.FrameCount() == 0);
            if (g == 1) CHECK(a.FrameCount() == 1);
            if (g == 100) CHECK(a.FrameCount() == 100);
            if (g == -1) CHECK(a.FrameCount() > 100);
        }
        // Without the end-of-stream page the whole decoded length is returned and `truncated` is set.
        Bytes noEos = Repage(stereo);
        std::size_t lastPage = 0;
        for (std::size_t pos = 0; pos + 27 <= noEos.size();) {
            lastPage = pos;
            const std::size_t nseg = noEos[pos + 26];
            std::size_t body = 0;
            for (std::size_t i = 0; i < nseg; ++i) body += noEos[pos + 27 + i];
            pos += 27 + nseg + body;
        }
        noEos.resize(lastPage);
        bool ok = false;
        const VorbisAudio cut = Decode(noEos, &ok);
        CHECK(ok && cut.truncated && cut.FrameCount() > 0 && cut.FrameCount() < stereoFull.FrameCount());
        CHECK(!stereoFull.truncated);
    }

    // ---- hand-built minimal streams ------------------------------------------------------------------
    {
        bool ok = false;
        std::string err;
        // 3 packets of 256-sample blocks: the first yields nothing, the others 128 frames each.
        const Bytes good = MinimalStream(Hostile::None, 1, 3, 1 << 20);
        const VorbisAudio a = Decode(good, &ok, nullptr, &err);
        CHECK(ok && a.FrameCount() == 256 && a.channels == 1 && a.sampleRate == 22050);
        CHECK(AllFinite(a.samples));
        // Stereo with channel coupling; the floor alone (no residue) gives silence.
        const VorbisAudio st = Decode(MinimalStream(Hostile::None, 2, 5, 400), &ok);
        CHECK(ok && st.FrameCount() == 400 && st.channels == 2);
        bool silent = true;
        for (float x : st.samples) silent = silent && x == 0.0f;
        CHECK(silent);
        // Block sizes of different length work as well.
        const VorbisAudio mixed = Decode(MinimalStream(Hostile::None, 1, 6, 1 << 20, 6, 11), &ok);
        CHECK(ok);

        // Header defects are rejected with a message and no samples.
        const std::pair<Hostile, const char*> defects[] = {
            {Hostile::FloorZero, "floor"}, {Hostile::BadFloorType, "floor"}, {Hostile::HugeBookUnordered, "codebook"},
            {Hostile::HugeBookOrdered, "codebook"}, {Hostile::ZeroDims, "codebook"}, {Hostile::LookupHuge, "codebook"},
            {Hostile::BadSync, "codebook"}, {Hostile::BadTimeTransform, "time"}, {Hostile::ResidueHuge, ""}, {Hostile::MissingBook, "residue"},
            {Hostile::BadMapping, "mapping"}, {Hostile::BadMode, "mode"}, {Hostile::NoFraming, "framing"}};
        for (const auto& d : defects) {
            err.clear();
            const Bytes bad = MinimalStream(d.first, 1, 4, 1 << 20);
            const VorbisAudio r = Decode(bad, &ok, nullptr, &err);
            if (d.first == Hostile::ResidueHuge) {
                // A residue with 24-bit claims is legal; it is clamped to the block. Must not crash or allocate wildly.
                CHECK(AllFinite(r.samples));
                continue;
            }
            CHECK(!ok && r.samples.empty() && r.channels == 0 && !err.empty());
            CHECK(err.find(d.second) != std::string::npos);
        }
        // Coupling a channel with itself.
        err.clear();
        Decode(MinimalStream(Hostile::CouplingSelf, 2, 4, 1 << 20), &ok, nullptr, &err);
        CHECK(!ok && !err.empty());

        // Identification header defects.
        const struct { int ch; std::uint32_t rate; int b0, b1; std::uint32_t ver; int framing; } ids[] = {
            {0, 44100, 8, 11, 0, 1}, {1, 0, 8, 11, 0, 1}, {1, 44100, 5, 11, 0, 1}, {1, 44100, 8, 14, 0, 1}, {1, 44100, 11, 8, 0, 1},
            {1, 44100, 8, 11, 1, 1}, {1, 44100, 8, 11, 0, 0}, {33, 44100, 8, 11, 0, 1}, {255, 44100, 8, 11, 0, 1}, {1, 0xFFFFFFFFu, 8, 11, 0, 1}};
        for (const auto& id : ids) {
            std::vector<Bytes> pk = {IdentPacket(id.ch, id.rate, id.b0, id.b1, id.ver, id.framing), CommentPacket(), SetupPacket(Hostile::None, 1)};
            for (int i = 0; i < 4; ++i) pk.push_back(SilentAudioPacket(1));
            err.clear();
            Decode(BuildOgg(pk, 1 << 20, 255), &ok, nullptr, &err);
            CHECK(!ok && !err.empty());
        }
        // Missing or swapped header packets.
        {
            std::vector<Bytes> pk = {IdentPacket(1, 22050, 8, 8), SetupPacket(Hostile::None, 1), CommentPacket(), SilentAudioPacket(1), SilentAudioPacket(1)};
            Decode(BuildOgg(pk, 1 << 20, 255), &ok);
            CHECK(!ok);
            pk = {IdentPacket(1, 22050, 8, 8), CommentPacket()};
            Decode(BuildOgg(pk, 1 << 20, 255), &ok);
            CHECK(!ok);
        }
        // Headers only (no audio) is not a decodable file either.
        {
            std::vector<Bytes> pk = {IdentPacket(1, 22050, 8, 8), CommentPacket(), SetupPacket(Hostile::None, 1)};
            err.clear();
            Decode(BuildOgg(pk, 0, 255), &ok, nullptr, &err);
            CHECK(!ok && !err.empty());
        }
        // The comment header's contents are not interpreted.
        {
            std::vector<Bytes> pk = {IdentPacket(1, 22050, 8, 8), Bytes{3, 'v', 'o', 'r', 'b', 'i', 's', 0xFF, 0xFF, 0xFF}, SetupPacket(Hostile::None, 1)};
            for (int i = 0; i < 4; ++i) pk.push_back(SilentAudioPacket(1));
            Decode(BuildOgg(pk, 1 << 20, 255), &ok);
            CHECK(ok);
        }
        // Audio packets that are empty, of the wrong type or name a missing mode are skipped.
        {
            std::vector<Bytes> pk = {IdentPacket(1, 22050, 8, 8), CommentPacket(), SetupPacket(Hostile::None, 1)};
            pk.push_back(SilentAudioPacket(1));
            pk.push_back(Bytes{});
            pk.push_back(Bytes{0x01, 0x00});
            pk.push_back(SilentAudioPacket(1));
            pk.push_back(SilentAudioPacket(1));
            const VorbisAudio r = Decode(BuildOgg(pk, 1 << 20, 255), &ok);
            CHECK(ok && r.FrameCount() == 256);
        }
    }

    // ---- limits --------------------------------------------------------------------------------------
    {
        VorbisOptions capped;
        capped.maxSamples = 3000;
        bool ok = false;
        const VorbisAudio a = Decode(stereo, &ok, &capped);
        CHECK(ok && a.truncated && a.samples.size() > 0 && a.samples.size() <= capped.maxSamples);
        // The cap leaves room for a whole packet, so what is returned is a prefix of the full result.
        bool pre = a.samples.size() <= stereoFull.samples.size();
        for (std::size_t i = 0; pre && i < a.samples.size(); ++i) pre = a.samples[i] == stereoFull.samples[i];
        CHECK(pre);
        VorbisOptions zero;
        zero.maxSamples = 0;
        Decode(stereo, &ok, &zero);
        CHECK(!ok);
        // Many channels are refused.
        std::vector<Bytes> pk = {IdentPacket(40, 22050, 8, 8), CommentPacket(), SetupPacket(Hostile::None, 1)};
        for (int i = 0; i < 4; ++i) pk.push_back(SilentAudioPacket(1));
        std::string err;
        Decode(BuildOgg(pk, 1 << 20, 255), &ok, nullptr, &err);
        CHECK(!ok && err.find("32 channels") != std::string::npos);
    }

    // ---- degenerate input ----------------------------------------------------------------------------
    {
        VorbisAudio a;
        std::string err;
        CHECK(!DecodeVorbis(nullptr, 0, a, &err) && !err.empty());
        CHECK(!DecodeVorbis(nullptr, 100, a, nullptr));
        for (std::size_t n = 0; n <= 40; ++n) {
            Bytes tinyIn(n, 0xFF);
            CHECK(!DecodeVorbis(tinyIn.data(), tinyIn.size(), a, nullptr));
            CHECK(a.samples.empty());
            Bytes zeros(n, 0x00);
            CHECK(!DecodeVorbis(zeros.data(), zeros.size(), a, nullptr));
        }
        const char* text = "this is not an Ogg file, just some text that is long enough to hold a page header";
        CHECK(!DecodeVorbis(reinterpret_cast<const unsigned char*>(text), std::strlen(text), a, &err) && !err.empty());
        // A RIFF/WAV header and an MP3-like frame are not Ogg either.
        Bytes wav = {'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        CHECK(!DecodeVorbis(wav.data(), wav.size(), a, nullptr));
        // Leading junk before the first page is skipped (the stream is found by its capture pattern).
        Bytes junk(777);
        Rng rng(3);
        for (auto& b : junk) b = static_cast<unsigned char>(rng.Below(0x40) + 0x10);
        junk.insert(junk.end(), mono.begin(), mono.end());
        bool ok = false;
        const VorbisAudio j = Decode(junk, &ok);
        CHECK(ok && j.samples == monoFull.samples);
        // Trailing junk after the last page changes nothing.
        Bytes tail = mono;
        for (int i = 0; i < 300; ++i) tail.push_back(static_cast<unsigned char>(rng.Next()));
        CHECK(Decode(tail).samples == monoFull.samples);
        // Fake page headers in bulk: bounded work, no result.
        Bytes fake;
        for (int i = 0; i < 20000; ++i) { const unsigned char h[] = {'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255}; fake.insert(fake.end(), h, h + sizeof(h)); }
        CHECK(!DecodeVorbis(fake.data(), fake.size(), a, nullptr));
    }

    // ---- page damage ---------------------------------------------------------------------------------
    {
        // A damaged page in the middle is skipped (CRC): what came before it is unchanged, the rest of the stream
        // that depends on the lost page's packets is bridged by the next valid pages.
        Bytes src;
        CHECK(ReadAll(kDir + "enc_stereo_44k_q5.ogg", src));
        src = Repage(src);
        const VorbisAudio ref = Decode(src);
        Bytes hole = src;
        const std::size_t at = hole.size() / 4 * 3;
        for (std::size_t i = at; i < at + 40; ++i) hole[i] ^= 0xA5;
        bool ok = false;
        const VorbisAudio h = Decode(hole, &ok);
        CHECK(ok && AllFinite(h.samples) && h.FrameCount() < ref.FrameCount() && h.FrameCount() > ref.FrameCount() / 4);
        const std::size_t keep = std::min<std::size_t>(2048, h.samples.size());
        bool prefix = keep > 100;
        for (std::size_t i = 0; i < keep && prefix; ++i) prefix = h.samples[i] == ref.samples[i];
        CHECK(prefix);
        // Damage that keeps the CRC valid reaches the packet decoder instead.
        Bytes valid = src;
        for (std::size_t i = at; i < at + 40; ++i) valid[i] ^= 0xA5;
        FixCrcs(valid);
        const VorbisAudio v = Decode(valid, &ok);
        CHECK(ok && AllFinite(v.samples) && v.FrameCount() <= ref.FrameCount());
    }

    // ---- truncation at every length ------------------------------------------------------------------
    {
        auto sweep = [&](const Bytes& src, std::size_t step) {
            bool fullOk = false;
            const VorbisAudio full = Decode(src, &fullOk);
            CHECK(fullOk);
            int decodedOk = 0;
            std::size_t lastFrames = 0;
            for (std::size_t len = 0; len <= src.size(); len += step) {
                Bytes cut(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
                bool ok = false;
                const VorbisAudio a = Decode(cut, &ok);
                CHECK(AllFinite(a.samples));
                if (!ok) { CHECK(a.samples.empty() && a.channels == 0); continue; }
                ++decodedOk;
                // Decoding is causal: a prefix of the file decodes to a prefix of the full result.
                bool pre = a.samples.size() <= full.samples.size();
                for (std::size_t i = 0; pre && i < a.samples.size(); ++i) pre = a.samples[i] == full.samples[i];
                CHECK(pre);
                CHECK(a.FrameCount() >= lastFrames);
                lastFrames = a.FrameCount();
            }
            CHECK(decodedOk > 5);
        };
        sweep(Repage(tinyEop), 1);
        sweep(Repage(mono), 5);
        sweep(Repage(six), 61);
        Bytes bigBlocks;
        CHECK(ReadAll(kDir + "syn_stereo_blocks_64_8192.ogg", bigBlocks));
        sweep(Repage(bigBlocks), 1);
    }

    // ---- bit flips, bursts, valid-CRC mutations and noise --------------------------------------------
    {
        const char* names[] = {"enc_stereo_32k_q6", "enc_mono_8k_q0", "syn_5ch_coupling_res1", "syn_stereo_submaps_res1", "syn_mono_res0_lookup2",
                               "syn_stereo_ordered_sparse", "syn_stereo_truncated_packets", "enc_ffmpeg_native_stereo_22k", "syn_8ch_res0"};
        Rng rng(2024);
        int decoded = 0, total = 0;
        for (const char* name : names) {
            Bytes src;
            CHECK(ReadAll(kDir + name + std::string(".ogg"), src));
            for (int iter = 0; iter < 60; ++iter) {
                Bytes d = src;
                const int flips = 1 + static_cast<int>(rng.Below(20));
                for (int f = 0; f < flips; ++f) d[rng.Below(static_cast<std::uint32_t>(d.size()))] ^= static_cast<unsigned char>(1u << rng.Below(8));
                if (iter % 5 == 0) {      // a burst of random bytes
                    const std::size_t at2 = rng.Below(static_cast<std::uint32_t>(d.size()));
                    for (std::size_t i = at2; i < std::min(d.size(), at2 + 1 + rng.Below(300)); ++i) d[i] = static_cast<unsigned char>(rng.Next());
                }
                if (iter % 7 == 0) d.resize(rng.Below(static_cast<std::uint32_t>(d.size())) + 1);
                if (iter % 2 == 1) FixCrcs(d);     // let the damage through to the decoder
                bool ok = false;
                const VorbisAudio a = Decode(d, &ok);
                ++total;
                CHECK(AllFinite(a.samples));
                CHECK(a.samples.size() % (ok ? static_cast<std::size_t>(a.channels) : 1u) == 0);
                CHECK(a.samples.size() <= (std::size_t(1) << 28));
                if (ok) ++decoded;
            }
        }
        CHECK(decoded > total / 4);
        // Damage aimed at the header packets (identification, codebooks, floors, residues, mappings).
        Bytes src;
        CHECK(ReadAll(kDir + "syn_stereo_submaps_res1.ogg", src));
        for (std::size_t pos = 28; pos < std::min<std::size_t>(src.size(), 900); ++pos) {
            for (int bit = 0; bit < 8; bit += 3) {
                Bytes d = src;
                d[pos] ^= static_cast<unsigned char>(1u << bit);
                FixCrcs(d);
                bool ok = false;
                const VorbisAudio a = Decode(d, &ok);
                CHECK(AllFinite(a.samples));
            }
        }
        // Noise, constant fills, valid page headers around noise.
        for (int iter = 0; iter < 40; ++iter) {
            Bytes d(20000);
            for (auto& b : d) b = static_cast<unsigned char>(rng.Next());
            if (iter % 2) { for (std::size_t i = 0; i + 4 <= d.size(); i += 301) { d[i] = 'O'; d[i + 1] = 'g'; d[i + 2] = 'g'; d[i + 3] = 'S'; d[i + 4 < d.size() ? i + 4 : i] = 0; } }
            if (iter % 8 == 3) std::fill(d.begin(), d.end(), static_cast<unsigned char>(iter % 2 ? 0xFF : 0x00));
            FixCrcs(d);
            bool ok = false;
            const VorbisAudio a = Decode(d, &ok);
            CHECK(AllFinite(a.samples));
        }
        // Valid headers (from a real file) followed by random audio packets: decodes to something finite and bounded.
        ParsedStream ps;
        CHECK(ParseOgg(src, ps));
        for (int iter = 0; iter < 20; ++iter) {
            std::vector<Bytes> pk(ps.packets.begin(), ps.packets.begin() + 3);
            for (int i = 0; i < 12; ++i) {
                Bytes p(1 + rng.Below(600));
                for (auto& b : p) b = static_cast<unsigned char>(rng.Next());
                if (i % 3) p[0] &= 0xFE;
                pk.push_back(p);
            }
            bool ok = false;
            const VorbisAudio a = Decode(BuildOgg(pk, 1 << 20, 255), &ok);
            CHECK(AllFinite(a.samples));
        }
    }

    // ---- the output is deterministic ---------------------------------------------------------------
    {
        const VorbisAudio a = Decode(six), b = Decode(six);
        CHECK(a.samples == b.samples && a.channels == 6 && a.sampleRate == 22050);
        VorbisOptions all;
        all.allStreams = true;
        CHECK(Decode(six, nullptr, &all).samples == a.samples);
    }

    TEST_MAIN_RESULT();
}
