// The number scanner the text model readers share (engine/src/Render/NumParse.hpp). ScanDecimal reads four digits at a time
// (FourDigits) on little-endian machines and one at a time elsewhere. This test is built twice: as reader_numparse (the build's
// own path) and as reader_numparse_portable (with OKAY_NUMPARSE_NO_FOUR_DIGITS, the path a big-endian machine takes). Both
// check the same contract on the same inputs: for every token, ScanDecimal either gives FastDecimal's value bit for bit and stops
// at the token's end, or declines exactly when FastDecimal declines or the token is longer than the limit. So both builds give
// identical values. Inputs: hand-picked tokens, every four-digit run, a fixed pseudo-random set and, when OKAY_MODEL_CORPUS is
// set, every number-like token of the corpus text files (docs/model_corpus.md). Standalone: no engine library.
#include "test_framework.hpp"
#include "../engine/src/Render/NumParse.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay::detail;

static long g_tokens = 0, g_fast = 0, g_bad = 0;

static bool Same(double a, double b) { std::uint64_t x, y; std::memcpy(&x, &a, 8); std::memcpy(&y, &b, 8); return x == y; }

// one token followed by `tail` (a space, or nothing: the token ends the buffer)
static void Check(const std::string& tok, const char* tail = " ", std::ptrdiff_t maxLen = 64) {
    ++g_tokens;
    const std::string buf = tok + tail;
    const char* b = buf.data();
    const char* e = b + buf.size();
    double fast = 0;
    const bool fastOk = FastDecimal(b, b + tok.size(), fast);
    const char* p = b;
    double v = 0;
    const bool ok = ScanDecimal(p, e, v, maxLen);
    // the token as ScanDecimal sees it: up to the first white space
    std::size_t len = 0;
    while (len < buf.size() && !(buf[len] == ' ' || (unsigned char)(buf[len] - 9) < 5u)) ++len;
    const bool wholeToken = len == tok.size();
    bool good;
    if (ok) good = fastOk && wholeToken && Same(v, fast) && p == b + tok.size() && (std::ptrdiff_t)tok.size() <= maxLen;
    else good = p == b && (!fastOk || !wholeToken || (std::ptrdiff_t)tok.size() > maxLen);
    if (fastOk) ++g_fast;
    if (!good) { if (++g_bad <= 10) std::fprintf(stderr, "  differs: '%s' scan=%d fast=%d\n", tok.c_str(), (int)ok, (int)fastOk); }
}

int main() {
    RUN_SUITE(std::string(OKAY_NUMPARSE_FOUR_DIGITS ? "reader_numparse" : "reader_numparse_portable"));
#ifdef OKAY_NUMPARSE_NO_FOUR_DIGITS
    CHECK(OKAY_NUMPARSE_FOUR_DIGITS == 0);   // the switch really forces the digit-by-digit path
    CHECK(FourDigits("1234") == -1);
#endif
    if (OKAY_NUMPARSE_FOUR_DIGITS) {   // the four-digit trick itself: every run of four digits, and a byte that is not a digit
        int wrong = 0;
        char q[16] = {0};
        for (int n = 0; n < 10000; ++n) { std::snprintf(q, sizeof q, "%04d", n); if (FourDigits(q) != n) ++wrong; }
        for (int pos = 0; pos < 4; ++pos)
            for (int c = 0; c < 256; ++c) {
                if (c >= '0' && c <= '9') continue;
                std::memcpy(q, "5555", 4);
                q[pos] = (char)c;
                if (FourDigits(q) != -1) ++wrong;
            }
        CHECK(wrong == 0);
    }
    std::printf("  path: %s\n", OKAY_NUMPARSE_FOUR_DIGITS ? "four digits at a time (little endian)" : "digit by digit (forced, as on big endian)");

    const char* hand[] = {"0", "-0", "1", "-1", "12", "123", "1234", "12345", "123456789", "1234567890123456789", "12345678901234567890",
                          "0.5", "-0.25", ".5", "5.", "1.0000", "3.14159265358979", "0.0001", "1e5", "1E-5", "-2.5e+10", "1e22", "1e23",
                          "9007199254740992", "9007199254740993", "1.5e", "1.5e+", "+1", "1,5", "1.2.3", "abc", "-", ".", "",
                          "0000000000000000001", "00000000000000000001", "123a", "12345x", "1234.5678", "99999999.99999999",
                          "4294967296", "18446744073709551615", "1e1001", "1e-1001", "-0.0", "0e0", "7.0e-3"};
    for (const char* h : hand) { Check(h); Check(h, ""); Check(h, "\t"); Check(h, "\n"); Check(h, " ", 3); }
    {   // a token at the very end of the buffer with 1 to 3 bytes after a run of four (the reads stay inside)
        for (const char* t : {"1234", "12345", "123456", "1234567", "12345678", "0.1234", "0.12345678"}) Check(t, "");
    }
    char q[16];
    for (int n = 0; n < 10000; ++n) { std::snprintf(q, sizeof q, "%04d", n); Check(q); Check(std::string("-") + q + "." + q); }
    {   // pseudo-random tokens (a fixed sequence): digits with a point, a sign and an exponent here and there
        std::uint64_t s = 0x9E3779B97F4A7C15ull;
        auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
        const char alphabet[] = "0123456789012345678901234567890123456789.-eE+x";
        for (int i = 0; i < 300000; ++i) {
            std::string t;
            const int len = 1 + (int)(rnd() % 24);
            const bool plain = rnd() % 4 != 0;   // most are well-formed numbers
            if (plain) {
                if (rnd() % 3 == 0) t += '-';
                const int a = 1 + (int)(rnd() % 12), b = (int)(rnd() % 12);
                for (int k = 0; k < a; ++k) t += (char)('0' + rnd() % 10);
                if (b) { t += '.'; for (int k = 0; k < b; ++k) t += (char)('0' + rnd() % 10); }
                if (rnd() % 5 == 0) { t += 'e'; if (rnd() % 2) t += '-'; t += std::to_string(rnd() % 30); }
            } else for (int k = 0; k < len; ++k) t += alphabet[rnd() % (sizeof alphabet - 1)];
            Check(t);
        }
    }
    if (const char* corpus = std::getenv("OKAY_MODEL_CORPUS")) {   // every number-like token of the corpus text files
        namespace fs = std::filesystem;
        long files = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(corpus, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->file_size(ec) > 16u * 1024u * 1024u) continue;
            std::ifstream f(it->path(), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::size_t bin = 0;
            for (std::size_t i = 0; i < text.size() && i < 4096; ++i) if (text[i] == 0) ++bin;
            if (bin) continue;   // binary file
            ++files;
            const char* b = text.data();
            const char* e = b + text.size();
            for (const char* p = b; p < e;) {
                while (p < e && (*p == ' ' || (unsigned char)(*p - 9) < 5u)) ++p;
                const char* t = p;
                while (p < e && !(*p == ' ' || (unsigned char)(*p - 9) < 5u)) ++p;
                if (p > t && ((*t >= '0' && *t <= '9') || *t == '-' || *t == '.') && p - t <= 200) {
                    // in place, as the readers see it: ScanDecimal on the file's own bytes, the rest of the file after it
                    ++g_tokens;
                    double fast = 0, v = 0;
                    const bool fastOk = FastDecimal(t, p, fast);
                    const char* s = t;
                    const bool ok = ScanDecimal(s, e, v, 64);
                    const bool good = ok ? (fastOk && s == p && Same(v, fast) && p - t <= 64) : (s == t && (!fastOk || p - t > 64));
                    if (fastOk) ++g_fast;
                    if (!good && ++g_bad <= 10) std::fprintf(stderr, "  differs in %s: '%.*s'\n", it->path().string().c_str(), (int)(p - t), t);
                }
            }
        }
        std::printf("  corpus: %ld text files read\n", files);
        CHECK(files > 100);
    } else std::printf("  corpus: skipped (OKAY_MODEL_CORPUS not set)\n");
    std::printf("  %ld tokens, %ld of them in FastDecimal's case, %ld differ\n", g_tokens, g_fast, g_bad);
    CHECK(g_bad == 0);
    CHECK(g_fast > 100000);
    TEST_MAIN_RESULT();
}
