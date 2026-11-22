#include "test_framework.hpp"
#include "okay/Core/Base64.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// One Base64 (okay/Core/Base64.hpp, #422 audit item 24). The copies it replaces are kept below exactly as they were;
// on many random inputs, junk included, the shared code must give the same bytes and the same yes/no answers.
using namespace okay;
namespace {

// ---- the old copies ---------------------------------------------------------------------------------------------------
bool OldGltf(const std::string& in, std::vector<std::uint8_t>& out) {          // Render/Gltf.hpp
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62; if (c == '/') return 63;
        return -1;
    };
    int bits = 0; unsigned acc = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c); if (v < 0) continue;
        acc = ((acc << 6) | (unsigned)v) & 0xFFFFFFu; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((std::uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}
void OldFbx(const std::string& in, std::vector<std::uint8_t>& out) {           // Render/FbxScene.cpp (fast path included)
    signed char tab[256];
    for (int c = 0; c < 256; ++c) tab[c] = -1;
    const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) tab[(unsigned char)a[i]] = (signed char)i;
    const unsigned char* p = (const unsigned char*)in.data();
    const std::size_t n = in.size();
    out.resize(n / 4 * 3 + 3);
    std::uint8_t* o = out.data();
    std::size_t w = 0;
    unsigned acc = 0; int bits = 0;
    std::size_t i = 0;
    while (i < n) {
        if (bits == 0) {
            while (n - i >= 4) {
                const int A = tab[p[i]], B = tab[p[i + 1]], C = tab[p[i + 2]], D = tab[p[i + 3]];
                if ((A | B | C | D) < 0) break;
                const std::uint32_t v = ((std::uint32_t)A << 18) | ((std::uint32_t)B << 12) | ((std::uint32_t)C << 6) | (std::uint32_t)D;
                o[w] = (std::uint8_t)(v >> 16); o[w + 1] = (std::uint8_t)(v >> 8); o[w + 2] = (std::uint8_t)v;
                w += 3; i += 4;
            }
            if (i >= n) break;
        }
        const int t = tab[p[i++]];
        if (t < 0) continue;
        acc = (acc << 6) | (unsigned)t; bits += 6;
        if (bits >= 8) { bits -= 8; o[w++] = (std::uint8_t)((acc >> bits) & 0xFF); }
    }
    out.resize(w);
}
std::string OldVoxelEncode(const std::vector<unsigned char>& bytes) {          // VoxelTerrain Encode{Density,Materials}
    const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        unsigned b0 = bytes[i], b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0, b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        unsigned n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kB64[(n >> 18) & 63]); out.push_back(kB64[(n >> 12) & 63]);
        out.push_back((i + 1 < bytes.size()) ? kB64[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < bytes.size()) ? kB64[n & 63] : '=');
    }
    return out;
}
std::vector<unsigned char> OldVoxelDecode(const std::string& s) {             // VoxelTerrain Decode{Density,Materials}
    std::vector<unsigned char> bytes; unsigned acc = 0; int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '+') v = 62; else if (c == '/') v = 63; else continue;
        acc = ((acc << 6) | (unsigned)v) & 0xFFFFFFu; bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back((unsigned char)((acc >> bits) & 0xFF)); }
    }
    return bytes;
}
bool OldOkf(const char* const* lines, std::size_t lineCount, std::vector<std::uint8_t>& out) {   // shared/OkfDecode.hpp
    out.clear();
    std::uint32_t acc = 0; int have = 0, pad = 0; std::size_t chars = 0;
    for (std::size_t l = 0; l < lineCount; ++l) {
        for (const char* s = lines[l]; *s; ++s) {
            const char c = *s; int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') { ++pad; v = 0; }
            else return false;
            if (pad && c != '=') return false;
            if (pad > 2) return false;
            ++chars;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            if (++have == 4) {
                out.push_back(static_cast<std::uint8_t>(acc >> 16));
                if (pad < 2) out.push_back(static_cast<std::uint8_t>(acc >> 8));
                if (pad < 1) out.push_back(static_cast<std::uint8_t>(acc));
                have = 0; acc = 0;
            }
        }
    }
    return have == 0 && chars % 4 == 0;
}
const char* kUrl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string OldTicketEncode(const std::vector<std::uint8_t>& b) {             // Net/SessionTicket.cpp
    std::string o; std::size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        const unsigned v = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
        o += kUrl[(v >> 18) & 63]; o += kUrl[(v >> 12) & 63]; o += kUrl[(v >> 6) & 63]; o += kUrl[v & 63];
    }
    if (b.size() - i == 1) { const unsigned v = b[i] << 16; o += kUrl[(v >> 18) & 63]; o += kUrl[(v >> 12) & 63]; }
    else if (b.size() - i == 2) { const unsigned v = (b[i] << 16) | (b[i + 1] << 8); o += kUrl[(v >> 18) & 63]; o += kUrl[(v >> 12) & 63]; o += kUrl[(v >> 6) & 63]; }
    return o;
}
bool OldTicketDecode(const std::string& t, std::vector<std::uint8_t>& out) {
    out.clear();
    if (t.size() % 4 == 1) return false;
    unsigned acc = 0; int bits = 0;
    for (char c : t) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '-') v = 62; else if (c == '_') v = 63;
        else return false;
        acc = (acc << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF)); }
    }
    return (acc & ((1u << bits) - 1)) == 0;
}

struct Lcg {
    std::uint32_t s;
    std::uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};
std::vector<std::uint8_t> Bytes(Lcg& r, std::size_t n) { std::vector<std::uint8_t> v(n); for (auto& b : v) b = (std::uint8_t)r.Next(); return v; }
// Some text to decode: valid Base64 of either alphabet, with junk, padding and line breaks sprinkled in, or pure noise.
std::string Messy(Lcg& r, const std::string& clean) {
    static const char junk[] = " \n\r\t=*-_+/.%#";
    std::string s;
    for (char c : clean) {
        if (r.Next() % 9 == 0) s += junk[r.Next() % (sizeof(junk) - 1)];
        s += c;
    }
    if (r.Next() % 3 == 0) s += "==";
    return s;
}

} // namespace

int main() {
    RUN_SUITE("base64");

    // ---- RFC 4648 section 10 test vectors ----------------------------------------------------------------------------
    const char* vec[][2] = {{"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
    for (auto& v : vec) {
        const std::string in = v[0];
        CHECK(base64::Encode((const std::uint8_t*)in.data(), in.size()) == v[1]);
        std::vector<std::uint8_t> back;
        CHECK(base64::DecodePadded(v[1], back) && std::string(back.begin(), back.end()) == in);
        back.clear(); base64::DecodeLenient(std::string(v[1]), back);
        CHECK(std::string(back.begin(), back.end()) == in);
    }
    {   // the URL alphabet, unpadded (RFC 4648 section 5 and 3.2)
        const std::vector<std::uint8_t> b = {0xFB, 0xFF, 0xBF};
        CHECK(base64::Encode(b, base64::Alphabet::Url, false) == "-_-_");
        CHECK(base64::Encode(b) == "+/+/");
    }

    // ---- same bytes and answers as every old copy -------------------------------------------------------------------
    Lcg r{2026u};
    int n = 0, gltf = 0, fbx = 0, voxEnc = 0, voxDec = 0, okf = 0, tEnc = 0, tDec = 0, roundTrip = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::vector<std::uint8_t> raw = Bytes(r, r.Next() % 70);
        ++n;
        // encoders
        const std::string std64 = base64::Encode(raw);
        voxEnc += std64 == OldVoxelEncode(std::vector<unsigned char>(raw.begin(), raw.end()));
        const std::string url64 = base64::Encode(raw, base64::Alphabet::Url, false);
        tEnc += url64 == OldTicketEncode(raw);
        // round trips
        std::vector<std::uint8_t> a, b;
        roundTrip += base64::DecodePadded(std64, a) && a == raw && base64::DecodeUnpadded(url64, b) && b == raw;
        // lenient readers on messy text (and on noise every few rounds)
        std::string messy = Messy(r, i % 5 == 0 ? url64 : std64);
        if (i % 7 == 0) { messy.clear(); for (int k = 0; k < (int)(r.Next() % 40); ++k) messy += (char)(1 + r.Next() % 127); }   // no NUL: the .okf reader takes C strings
        { std::vector<std::uint8_t> o1 = {9, 9}, o2 = {9, 9}; OldGltf(messy, o1); base64::DecodeLenient(messy, o2); gltf += o1 == o2; }   // appends
        { std::vector<std::uint8_t> o1, o2 = {7}; OldFbx(messy, o1); o2.clear(); base64::DecodeLenient(messy, o2); fbx += o1 == o2; }
        { std::vector<std::uint8_t> o2; base64::DecodeLenient(messy, o2); const auto o1 = OldVoxelDecode(messy); voxDec += std::vector<std::uint8_t>(o1.begin(), o1.end()) == o2; }
        // strict readers: same yes/no and same bytes, on clean, messy and cut text
        for (const std::string& t : {std64, messy, std64.substr(0, std64.size() / 2), url64}) {
            const char* lines[] = {t.c_str()};
            std::vector<std::uint8_t> o1, o2;
            const bool r1 = OldOkf(lines, 1, o1), r2 = base64::DecodePadded(t, o2);
            okf += r1 == r2 && (!r1 || o1 == o2);
        }
        for (const std::string& t : {url64, messy, url64 + "A", std64}) {
            std::vector<std::uint8_t> o1, o2;
            const bool r1 = OldTicketDecode(t, o1), r2 = base64::DecodeUnpadded(t, o2);
            tDec += r1 == r2 && (!r1 || o1 == o2);
        }
    }
    std::printf("  of %d: voxel encode %d, ticket encode %d, round trips %d, glTF %d, FBX %d, voxel decode %d, okf %d/%d, ticket decode %d/%d\n",
                n, voxEnc, tEnc, roundTrip, gltf, fbx, voxDec, okf, 4 * n, tDec, 4 * n);
    CHECK(voxEnc == n); CHECK(tEnc == n); CHECK(roundTrip == n);
    CHECK(gltf == n); CHECK(fbx == n); CHECK(voxDec == n);
    CHECK(okf == 4 * n); CHECK(tDec == 4 * n);

    // ---- the .okf reader splits its blob over lines; the shared decoder sees them joined -------------------------------
    {
        const char* lines[] = {"Zm9v", "YmFy", "Zg=="};
        std::vector<std::uint8_t> o1, o2;
        CHECK(OldOkf(lines, 3, o1) && base64::DecodePadded(std::string("Zm9vYmFyZg=="), o2) && o1 == o2);
    }
    TEST_MAIN_RESULT();
}
