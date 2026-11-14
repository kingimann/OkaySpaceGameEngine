// The embedded fonts: our own container (shared/OkfDecode.hpp) must give back EXACTLY the TrueType bytes the old
// Dear ImGui arrays decoded to, so rendering cannot change. The bytes are pinned by size and SHA-256 in
// assets/fonts/embedded_fonts.sha256 (written by scripts/gen_embedded_fonts.py from the original arrays and
// cross-checked there with an independent decoder; the engine's former stb routine produced the same SHA-256 for Roboto).
// Also: the decoder on hand-made streams (every token form), and on damaged input (no crash, no wrong answer).
#include "test_framework.hpp"
#include "OkfDecode.hpp"
#include "RobotoFontOwn.h"
#include "JetBrainsMonoFontOwn.h"
#include <okay/Crypto/Crypto.hpp>
#include <okay/Graphics/TtfFont.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

static std::string Sha256Hex(const Bytes& b) {
    std::uint8_t d[32];
    okay::crypto::Sha256Hash(d, b.data(), b.size());
    static const char* hx = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i) { s += hx[d[i] >> 4]; s += hx[d[i] & 15]; }
    return s;
}

static std::string B64(const Bytes& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const unsigned v = in[i] << 16 | (i + 1 < in.size() ? in[i + 1] << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63];
        o += i + 1 < in.size() ? t[v >> 6 & 63] : '=';
        o += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return o;
}

// An OKF1 blob around a payload.
static Bytes Blob(int method, const Bytes& raw, const Bytes& payload, std::uint32_t sizeOverride = 0xFFFFFFFFu) {
    Bytes b = {'O', 'K', 'F', '1', static_cast<std::uint8_t>(method), 0, 0, 0};
    const std::uint32_t sz = sizeOverride == 0xFFFFFFFFu ? static_cast<std::uint32_t>(raw.size()) : sizeOverride;
    const std::uint32_t crc = okf::Crc32(raw.data(), raw.size());
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(sz >> (8 * i)));
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
static Bytes Str(const std::string& s) { return Bytes(s.begin(), s.end()); }

static std::map<std::string, std::pair<std::string, std::size_t>> ReadPins() {
    std::map<std::string, std::pair<std::string, std::size_t>> m;
    std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/assets/fonts/embedded_fonts.sha256");
    std::string line;
    while (std::getline(f, line)) {
        const std::size_t h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        std::istringstream is(line);
        std::string sha, name; std::size_t size = 0;
        if (is >> sha >> size >> name) m[name] = {sha, size};
    }
    return m;
}

static std::uint64_t g_s = 0x452821e638d01377ull;
static std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return static_cast<std::uint32_t>(g_s >> 16); }

int main() {
    RUN_SUITE("embedded fonts: decoded bytes equal the original TrueType files");
    const auto pins = ReadPins();
    CHECK(pins.size() == 2);
    struct F { const char* name; const char* const* lines; unsigned count; unsigned raw; };
    const F fonts[] = {
        {"Roboto", okf_data::kRobotoMediumOkf, okf_data::kRobotoMediumOkfLines, okf_data::kRobotoMediumOkfRawBytes},
        {"JetBrainsMono", okf_data::kJetBrainsMonoOkf, okf_data::kJetBrainsMonoOkfLines, okf_data::kJetBrainsMonoOkfRawBytes},
    };
    Bytes decoded[2];
    for (int i = 0; i < 2; ++i) {
        const F& f = fonts[i];
        CHECK(okf::DecodeLines(f.lines, f.count, decoded[i]));
        CHECK(decoded[i].size() == f.raw);
        CHECK(pins.count(f.name) == 1);
        if (pins.count(f.name)) {
            CHECK(decoded[i].size() == pins.at(f.name).second);
            CHECK(Sha256Hex(decoded[i]) == pins.at(f.name).first);
        }
        CHECK(decoded[i].size() > 4 && decoded[i][0] == 0 && decoded[i][1] == 1 && decoded[i][2] == 0 && decoded[i][3] == 0);   // TrueType 1.0
    }
    CHECK(decoded[0].size() == 162588);                  // Roboto Medium: the size its old array's header comment gave
    {   // the engine's accessor serves the same bytes
        std::vector<unsigned char> engine;
        CHECK(okay::EmbeddedUIFontData(engine));
        CHECK(engine.size() == decoded[0].size() && std::memcmp(engine.data(), decoded[0].data(), engine.size()) == 0);
    }

    RUN_SUITE("OKF1 decoder: every token form");
    {
        Bytes out;
        // stored
        const Bytes raw = Str("hello, font");
        CHECK(okf::Unpack(Blob(0, raw, raw).data(), Blob(0, raw, raw).size(), out) && out == raw);
        // empty
        const Bytes none;
        CHECK(okf::Unpack(Blob(0, none, none).data(), Blob(0, none, none).size(), out) && out.empty());
        // LZ: literals only (token 0x50 = 5 literals, no match)
        Bytes p = {0x50}; const Bytes l5 = Str("abcde"); p.insert(p.end(), l5.begin(), l5.end());
        Bytes b = Blob(1, l5, p);
        CHECK(okf::Unpack(b.data(), b.size(), out) && out == l5);
        // LZ: 1 literal then an overlapping match (RLE): "a" + 19 more 'a' via offset 1, length 19 (M-4 = 15 -> extended, +0)
        const Bytes rle(20, 'a');
        Bytes p2 = {0x1F, 'a', 0x01, 0x00, 0x00};
        b = Blob(1, rle, p2);
        CHECK(okf::Unpack(b.data(), b.size(), out) && out == rle);
        // LZ: a match of length 4 + 15 + 255 + 3 (extended with a 255 byte) after 2 literals "ab"
        Bytes big = Str("ab"); { std::size_t need = 4 + 15 + 255 + 3; for (std::size_t i = 0; i < need; ++i) big.push_back(big[big.size() - 2]); }
        Bytes p3 = {0x2F, 'a', 'b', 0x02, 0x00, 255, 3};
        b = Blob(1, big, p3);
        CHECK(okf::Unpack(b.data(), b.size(), out) && out == big);
        // LZ: 20 literals (15 + extended 5), then a final token with no match
        Bytes lit20; for (int i = 0; i < 20; ++i) lit20.push_back(static_cast<std::uint8_t>('A' + i));
        Bytes p4 = {0xF0, 5}; p4.insert(p4.end(), lit20.begin(), lit20.end());
        b = Blob(1, lit20, p4);
        CHECK(okf::Unpack(b.data(), b.size(), out) && out == lit20);
        // base64 with all three padding cases
        for (std::size_t n = 0; n < 8; ++n) {
            Bytes x; for (std::size_t i = 0; i < n; ++i) x.push_back(static_cast<std::uint8_t>(0xF0 + i));
            const std::string t = B64(x);
            const char* line = t.c_str();
            Bytes back;
            CHECK(okf::Base64Decode(&line, 1, back) && back == x);
        }
        const char* split[] = {"SGVsbG8s", "IGZvbnQ="};                    // "Hello, font" across two lines
        Bytes hb;
        CHECK(okf::Base64Decode(split, 2, hb) && hb == Str("Hello, font"));
    }

    RUN_SUITE("OKF1 decoder: damaged input is refused, never crashes");
    {
        Bytes out;
        const Bytes raw = Str("hello, font"), good = Blob(0, raw, raw);
        CHECK(okf::Unpack(good.data(), good.size(), out));
        Bytes b = good; b[0] = 'X'; CHECK(!okf::Unpack(b.data(), b.size(), out) && out.empty());       // magic
        b = good; b[4] = 2; CHECK(!okf::Unpack(b.data(), b.size(), out));                              // method
        b = good; b[5] = 1; CHECK(!okf::Unpack(b.data(), b.size(), out));                              // reserved
        b = good; b[12] ^= 1; CHECK(!okf::Unpack(b.data(), b.size(), out));                            // CRC
        b = good; b.back() ^= 1; CHECK(!okf::Unpack(b.data(), b.size(), out));                         // data (CRC catches it)
        b = good; b.pop_back(); CHECK(!okf::Unpack(b.data(), b.size(), out));                          // truncated
        b = good; b.push_back(0); CHECK(!okf::Unpack(b.data(), b.size(), out));                        // trailing
        CHECK(!okf::Unpack(good.data(), 15, out));                                                     // short header
        b = Blob(0, raw, raw, 0x7FFFFFFFu); CHECK(!okf::Unpack(b.data(), b.size(), out));            // absurd size
        b = Blob(1, raw, Bytes{0x0F, 0x01, 0x00}, 11u); CHECK(!okf::Unpack(b.data(), b.size(), out)); // offset before the start
        b = Blob(1, Bytes(20, 'a'), Bytes{0x1F, 'a', 0x01, 0x00, 0x00, 0x00}, 20u); CHECK(!okf::Unpack(b.data(), b.size(), out));   // trailing byte after a good stream
        b = Blob(1, Bytes(20, 'a'), Bytes{0x1F, 'a', 0x01, 0x00}, 20u); CHECK(!okf::Unpack(b.data(), b.size(), out));   // missing length extension
        b = Blob(1, Bytes(8, 'a'), Bytes{0x1F, 'a', 0x01, 0x00, 0x00}, 8u); CHECK(!okf::Unpack(b.data(), b.size(), out)); // match runs past the raw size
        b = Blob(1, Bytes(30, 'a'), Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 30u); CHECK(!okf::Unpack(b.data(), b.size(), out)); // lengths that never end
        const char* bad1[] = {"SGVs*G8s"}; CHECK(!okf::Base64Decode(bad1, 1, out));                    // character outside the alphabet
        const char* bad2[] = {"SGVsbG8"}; CHECK(!okf::Base64Decode(bad2, 1, out));                     // not a multiple of 4
        const char* bad3[] = {"SG=sbG8s"}; CHECK(!okf::Base64Decode(bad3, 1, out));                    // data after padding

        // The real blob (JetBrains Mono): every prefix of a sample is refused; random damage is refused or (never) wrong.
        Bytes blob;
        CHECK(okf::Base64Decode(okf_data::kJetBrainsMonoOkf, okf_data::kJetBrainsMonoOkfLines, blob));
        for (std::size_t cut = 0; cut < blob.size(); cut += 97) CHECK(!okf::Unpack(blob.data(), cut, out) && out.empty());
        int wrong = 0, accepted = 0;
        for (int i = 0; i < 3000; ++i) {
            Bytes m = blob;
            const int edits = 1 + static_cast<int>(Rnd() % 3);
            for (int e = 0; e < edits; ++e) m[Rnd() % m.size()] ^= static_cast<std::uint8_t>(1u << (Rnd() % 8));
            if (okf::Unpack(m.data(), m.size(), out)) { ++accepted; if (out != decoded[1]) ++wrong; }
        }
        CHECK(wrong == 0);
        CHECK(accepted == 0);                             // (a flip that survives the CRC would be a 2^-32 event)
        for (int i = 0; i < 2000; ++i) {                  // wholly random bytes behind a good header
            Bytes junk(16 + Rnd() % 200);
            for (auto& x : junk) x = static_cast<std::uint8_t>(Rnd());
            junk[0] = 'O'; junk[1] = 'K'; junk[2] = 'F'; junk[3] = '1'; junk[4] = 1; junk[5] = junk[6] = junk[7] = 0; junk[11] = 0; junk[10] &= 1;
            if (okf::Unpack(junk.data(), junk.size(), out)) CHECK(out.size() == (static_cast<std::size_t>(junk[8]) | static_cast<std::size_t>(junk[9]) << 8 | static_cast<std::size_t>(junk[10]) << 16));
        }
    }
    TEST_MAIN_RESULT();
}
