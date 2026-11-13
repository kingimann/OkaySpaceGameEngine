// Project Wycheproof known-answer and edge-case vectors against okay::crypto.
//
// Runs EVERY vector of these files (tests/wycheproof/, copied unmodified from the public Project Wycheproof
// repository, Apache-2.0, see tests/wycheproof/README.md for the commit):
//   x25519_test.json              X25519 (RFC 7748), incl. low-order, non-canonical and twist points
//   ed25519_test.json             Ed25519 verification, incl. malleability, non-canonical and small-order cases
//   chacha20_poly1305_test.json   ChaCha20-Poly1305 (RFC 8439)
//   xchacha20_poly1305_test.json  XChaCha20-Poly1305
//   hmac_sha256/512_test.json     HMAC, built here from OUR Sha256/Sha512 (okay::crypto has no HMAC of its own; this
//                                 exercises the two hashes over many key and message lengths)
//   hkdf_sha256/512_test.json     HKDF (RFC 5869) on that HMAC
//   chacha20_counter_test.json    raw (X)ChaCha20 counter carry and exhaustion boundaries
// The project has no BLAKE2b or SHA-2 hash vectors; those are covered by the RFC vectors in test_crypto.cpp.
//
// Where a file says "acceptable" the result is recorded, not required: it is printed, compared with libsodium when it
// is linked (OKAY_WY_SODIUM), and pinned below so a change of behaviour is noticed. A difference from libsodium on any
// vector is reported with its tcId; Ed25519 and X25519 must agree with libsodium on every vector (network compatibility).
//
// Directory: $OKAY_WYCHEPROOF_DIR if set (e.g. a fresh clone's testvectors_v1/), else the copy in the repository
// (OKAY_WYCHEPROOF_DEFAULT_DIR). When neither can be read the test skips and passes (like test_model_corpus).
#include <okay/Crypto/Crypto.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef OKAY_WY_SODIUM
#include <sodium.h>
#endif

namespace oc = okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(c, ...) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---------------------------------------------------------------- minimal JSON (objects, arrays, strings, numbers, true/false/null)
struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* Get(const char* k) const {
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    std::string S(const char* k) const { const Json* j = Get(k); return j && j->kind == Str ? j->str : std::string(); }
    long N(const char* k, long dflt = 0) const { const Json* j = Get(k); return j && j->kind == Num ? static_cast<long>(j->num) : dflt; }
};

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    bool ok = true;
    explicit Parser(const std::string& t) : s(t) {}
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    static void Utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
        else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    }
    std::string String() {
        std::string o;
        ++i;   // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char c = s[i + 1];
                i += 2;
                switch (c) {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) { ok = false; return o; }
                        unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                        i += 4;
                        Utf8(o, cp);
                        break;
                    }
                    default: o += c;
                }
            } else o += s[i++];
        }
        if (i >= s.size()) ok = false; else ++i;
        return o;
    }
    Json Value() {
        Json j;
        Ws();
        if (i >= s.size()) { ok = false; return j; }
        const char c = s[i];
        if (c == '{') {
            j.kind = Json::Obj; ++i; Ws();
            if (i < s.size() && s[i] == '}') { ++i; return j; }
            for (;;) {
                Ws();
                if (i >= s.size() || s[i] != '"') { ok = false; return j; }
                std::string k = String();
                Ws();
                if (i >= s.size() || s[i] != ':') { ok = false; return j; }
                ++i;
                Json v = Value();
                j.obj.emplace_back(std::move(k), std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return j; }
                ok = false; return j;
            }
        }
        if (c == '[') {
            j.kind = Json::Arr; ++i; Ws();
            if (i < s.size() && s[i] == ']') { ++i; return j; }
            for (;;) {
                j.arr.push_back(Value());
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return j; }
                ok = false; return j;
            }
        }
        if (c == '"') { j.kind = Json::Str; j.str = String(); return j; }
        if (s.compare(i, 4, "true") == 0) { j.kind = Json::Bool; j.b = true; i += 4; return j; }
        if (s.compare(i, 5, "false") == 0) { j.kind = Json::Bool; j.b = false; i += 5; return j; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return j; }
        char* end = nullptr;
        j.num = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) { ok = false; return j; }
        j.kind = Json::Num;
        i = static_cast<std::size_t>(end - s.c_str());
        return j;
    }
};

static bool LoadJson(const std::string& path, Json& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    Parser p(text);
    out = p.Value();
    return p.ok && out.kind == Json::Obj;
}

static Bytes Hex(const std::string& h) {
    Bytes b;
    auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(static_cast<std::uint8_t>(nib(h[i]) << 4 | nib(h[i + 1])));
    return b;
}
static const std::uint8_t* P(const Bytes& b) { static const std::uint8_t z = 0; return b.empty() ? &z : b.data(); }

// ---------------------------------------------------------------- bookkeeping
struct Tally {
    int valid = 0, invalid = 0, acceptable = 0, skipped = 0;
    int acceptAccepted = 0, acceptRejected = 0;
    std::map<std::string, std::pair<int, int>> byFlag;   // flag -> {accepted, rejected} among "acceptable" vectors
};

static std::string g_dir;

static bool Open(const char* file, Json& j) {
    const std::string path = g_dir + "/" + file;
    if (!LoadJson(path, j)) { CHECK(false, "cannot read or parse %s", path.c_str()); return false; }
    return true;
}

static void Report(const char* name, const Json& j, const Tally& t) {
    std::printf("[wycheproof] %-28s file declares %ld tests: valid %d, invalid %d, acceptable %d (accepted %d, rejected %d), not expressible in our API %d\n",
                name, j.N("numberOfTests"), t.valid, t.invalid, t.acceptable, t.acceptAccepted, t.acceptRejected, t.skipped);
    for (const auto& f : t.byFlag) std::printf("[wycheproof]     acceptable flag %-24s accepted %d rejected %d\n", f.first.c_str(), f.second.first, f.second.second);
    const int total = t.valid + t.invalid + t.acceptable + t.skipped;
    CHECK(total == j.N("numberOfTests"), "%s: ran %d of %ld declared vectors", name, total, j.N("numberOfTests"));
}

static std::string Flags(const Json& t) {
    std::string s;
    if (const Json* f = t.Get("flags")) for (const Json& x : f->arr) { if (!s.empty()) s += ","; s += x.str; }
    return s;
}

// Pinned decisions for "acceptable" vectors (all of them equal libsodium's; the test compares with libsodium when linked).
// key = file + "/" + flag (Ed25519 only; the X25519 decisions follow a rule, see RunX25519), value = true when we accept (verify true / shared secret computed).
static std::map<std::string, bool> g_pinned = {
    {"ed25519/SmallOrderPublicKey", false},    // libsodium refuses a small-order public key, and so do we
    {"ed25519/MixedOrderPublicKey", true},     // cofactorless check like libsodium and RFC 8032 section 5.1.7 (either equation)
};

static void NotePinned(const char* file, const Json& test, bool accepted, int tcId, const std::string& what) {
    std::string flags = Flags(test);
    std::set<std::string> done;
    std::stringstream fs(flags);
    std::string f;
    bool any = false;
    while (std::getline(fs, f, ',')) {
        auto it = g_pinned.find(std::string(file) + "/" + f);
        if (it == g_pinned.end()) continue;
        any = true;
        CHECK(it->second == accepted, "%s tc %d (%s): %s %s, pinned decision for flag %s is %s", file, tcId, test.S("comment").c_str(), what.c_str(),
              accepted ? "accepted" : "rejected", f.c_str(), it->second ? "accept" : "reject");
    }
    CHECK(any, "%s tc %d: acceptable vector with no pinned flag (%s)", file, tcId, flags.c_str());
}

// ---------------------------------------------------------------- X25519
static void RunX25519() {
    Json j;
    if (!Open("x25519_test.json", j)) return;
    Tally t;
    int sodiumDiff = 0;
    for (const Json& g : j.Get("testGroups")->arr) {
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const Bytes pub = Hex(tc.S("public")), priv = Hex(tc.S("private")), shared = Hex(tc.S("shared"));
            const std::string res = tc.S("result");
            if (pub.size() != 32 || priv.size() != 32 || shared.size() != 32) {
                // (the file has no wrong-length vectors today; this keeps the count honest if one appears)
                ++t.skipped; continue;
            }
            std::uint8_t out[32];
            const bool ok = oc::X25519(out, P(priv), P(pub));
            const bool match = ok && std::memcmp(out, shared.data(), 32) == 0;
            if (res == "valid") {
                ++t.valid;
                CHECK(ok && match, "x25519 tc %d (%s): valid vector %s", id, tc.S("comment").c_str(), !ok ? "refused" : "wrong shared secret");
            } else if (res == "invalid") {
                ++t.invalid;
                CHECK(!ok || !match, "x25519 tc %d: invalid vector reproduced the listed secret", id);
            } else {
                ++t.acceptable;
                // Acceptable: either refused, or computed correctly (never a wrong value).
                CHECK(!ok || match, "x25519 tc %d (%s): acceptable vector computed a WRONG secret", id, tc.S("comment").c_str());
                if (ok) { ++t.acceptAccepted; } else { ++t.acceptRejected; }
                std::stringstream fs(Flags(tc)); std::string f;
                while (std::getline(fs, f, ',')) (ok ? t.byFlag[f].first : t.byFlag[f].second)++;
                // Pinned rule (equal to libsodium's crypto_scalarmult): refuse exactly when the shared secret is all zero.
                bool zero = true;
                for (std::uint8_t b : shared) zero = zero && b == 0;
                CHECK(ok == !zero, "x25519 tc %d (%s) [%s]: %s, expected %s (rule: refuse iff the secret is all zero)", id, tc.S("comment").c_str(),
                      Flags(tc).c_str(), ok ? "computed" : "refused", zero ? "refused" : "computed");
            }
#ifdef OKAY_WY_SODIUM
            {
                std::uint8_t so[32];
                const bool sok = crypto_scalarmult_curve25519(so, P(priv), P(pub)) == 0;
                const bool same = sok == ok && (!ok || std::memcmp(so, out, 32) == 0);
                if (!same) { ++sodiumDiff; std::printf("[wycheproof] DIFFERENCE x25519 tc %d (%s): libsodium %s, ours %s\n", id, Flags(tc).c_str(), sok ? "computes" : "refuses", ok ? "computes" : "refuses"); }
            }
#endif
        }
    }
    Report("x25519", j, t);
#ifdef OKAY_WY_SODIUM
    CHECK(sodiumDiff == 0, "x25519: %d vectors where libsodium and okay::crypto differ", sodiumDiff);
#else
    (void)sodiumDiff;
#endif
}

// ---------------------------------------------------------------- Ed25519
static void RunEd25519() {
    Json j;
    if (!Open("ed25519_test.json", j)) return;
    Tally t;
    int sodiumDiff = 0;
    for (const Json& g : j.Get("testGroups")->arr) {
        const Bytes pk = Hex(g.Get("publicKey")->S("pk"));
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const Bytes msg = Hex(tc.S("msg")), sig = Hex(tc.S("sig"));
            const std::string res = tc.S("result");
            // A signature that is not exactly 64 bytes (truncated, with garbage) cannot be handed to our fixed-size API; it is
            // rejected by construction there (and checked below with the length the API takes). Count it as run, with the
            // decision "reject", and also exercise the 64-byte prefix/zero-padded variants for robustness.
            bool ok = false;
            if (sig.size() == oc::kEd25519SignatureBytes && pk.size() == 32) {
                ok = oc::Ed25519Verify(P(sig), P(msg), msg.size(), P(pk));
            } else {
                ok = false;
                Bytes pad(64, 0);
                for (std::size_t i = 0; i < sig.size() && i < 64; ++i) pad[i] = sig[i];
                if (pk.size() == 32) oc::Ed25519Verify(P(pad), P(msg), msg.size(), P(pk));   // must not crash; result irrelevant
            }
            if (res == "valid") {
                ++t.valid;
                CHECK(ok, "ed25519 tc %d (%s): valid signature rejected", id, tc.S("comment").c_str());
            } else if (res == "invalid") {
                ++t.invalid;
                CHECK(!ok, "ed25519 tc %d (%s) [%s]: INVALID signature accepted", id, tc.S("comment").c_str(), Flags(tc).c_str());
            } else {
                ++t.acceptable;
                if (ok) ++t.acceptAccepted; else ++t.acceptRejected;
                std::stringstream fs(Flags(tc)); std::string f;
                while (std::getline(fs, f, ',')) (ok ? t.byFlag[f].first : t.byFlag[f].second)++;
                NotePinned("ed25519", tc, ok, id, "ed25519");
            }
#ifdef OKAY_WY_SODIUM
            {
                bool sok = false;
                if (sig.size() == 64 && pk.size() == 32) sok = crypto_sign_verify_detached(P(sig), P(msg), msg.size(), P(pk)) == 0;
                if (sok != ok) { ++sodiumDiff; std::printf("[wycheproof] DIFFERENCE ed25519 tc %d (%s) [%s]: libsodium %s, ours %s\n", id, tc.S("comment").c_str(), Flags(tc).c_str(), sok ? "accepts" : "rejects", ok ? "accepts" : "rejects"); }
            }
#endif
        }
    }
    Report("ed25519", j, t);
#ifdef OKAY_WY_SODIUM
    CHECK(sodiumDiff == 0, "ed25519: %d vectors where libsodium and okay::crypto differ", sodiumDiff);
#else
    (void)sodiumDiff;
#endif
}

// ---------------------------------------------------------------- AEAD
static void RunAead(const char* file, bool x) {
    Json j;
    if (!Open(file, j)) return;
    Tally t;
    const std::size_t nonceLen = x ? oc::kXChaChaNonceBytes : oc::kChaChaNonceBytes;
    for (const Json& g : j.Get("testGroups")->arr) {
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const Bytes key = Hex(tc.S("key")), iv = Hex(tc.S("iv")), aad = Hex(tc.S("aad")), msg = Hex(tc.S("msg")), ct = Hex(tc.S("ct")), tag = Hex(tc.S("tag"));
            const std::string res = tc.S("result");
            if (key.size() != 32 || iv.size() != nonceLen) {
                // The API fixes the key and nonce sizes (a wrong size cannot be expressed), so these vectors are "rejected by the type".
                CHECK(res != "valid", "%s tc %d: valid vector with an unsupported key/nonce size", file, id);
                ++t.skipped;
                continue;
            }
            Bytes sealed(msg.size() + 16), opened(ct.size() + tag.size() + 1);
            if (x) oc::XChaCha20Poly1305Seal(sealed.data(), P(msg), msg.size(), P(aad), aad.size(), P(iv), P(key));
            else oc::ChaCha20Poly1305Seal(sealed.data(), P(msg), msg.size(), P(aad), aad.size(), P(iv), P(key));
            Bytes wire = ct;
            wire.insert(wire.end(), tag.begin(), tag.end());
            const bool open = x ? oc::XChaCha20Poly1305Open(opened.data(), P(wire), wire.size(), P(aad), aad.size(), P(iv), P(key))
                                : oc::ChaCha20Poly1305Open(opened.data(), P(wire), wire.size(), P(aad), aad.size(), P(iv), P(key));
            if (res == "valid") {
                ++t.valid;
                CHECK(sealed == wire, "%s tc %d (%s): seal differs from the listed ciphertext+tag", file, id, tc.S("comment").c_str());
                CHECK(open && std::memcmp(opened.data(), P(msg), msg.size()) == 0, "%s tc %d: open failed or wrong plaintext", file, id);
            } else if (res == "invalid") {
                ++t.invalid;
                CHECK(!open, "%s tc %d (%s) [%s]: INVALID message opened", file, id, tc.S("comment").c_str(), Flags(tc).c_str());
            } else {
                ++t.acceptable;
                CHECK(false, "%s tc %d: unexpected acceptable result in an AEAD file", file, id);
            }
        }
    }
    Report(file, j, t);
}

// ---------------------------------------------------------------- HMAC and HKDF (on our hashes)
struct HashFn {
    std::size_t len, block;
    void (*hash)(std::uint8_t*, const std::uint8_t*, std::size_t);
};
static void H256(std::uint8_t* o, const std::uint8_t* m, std::size_t n) { oc::Sha256Hash(o, m, n); }
static void H512(std::uint8_t* o, const std::uint8_t* m, std::size_t n) { oc::Sha512Hash(o, m, n); }

static Bytes Hmac(const HashFn& h, const Bytes& key, const Bytes& msg) {
    Bytes k(h.block, 0);
    if (key.size() > h.block) h.hash(k.data(), P(key), key.size());
    else for (std::size_t i = 0; i < key.size(); ++i) k[i] = key[i];
    Bytes in(h.block + msg.size()), inner(h.len), outer(h.block + h.len), mac(h.len);
    for (std::size_t i = 0; i < h.block; ++i) in[i] = k[i] ^ 0x36;
    for (std::size_t i = 0; i < msg.size(); ++i) in[h.block + i] = msg[i];
    h.hash(inner.data(), in.data(), in.size());
    for (std::size_t i = 0; i < h.block; ++i) outer[i] = k[i] ^ 0x5c;
    for (std::size_t i = 0; i < h.len; ++i) outer[h.block + i] = inner[i];
    h.hash(mac.data(), outer.data(), outer.size());
    return mac;
}

static void RunHmac(const char* file, const HashFn& h) {
    Json j;
    if (!Open(file, j)) return;
    Tally t;
    for (const Json& g : j.Get("testGroups")->arr) {
        const std::size_t tagBytes = static_cast<std::size_t>(g.N("tagSize")) / 8;
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const Bytes key = Hex(tc.S("key")), msg = Hex(tc.S("msg")), tag = Hex(tc.S("tag"));
            const Bytes mac = Hmac(h, key, msg);
            // "accept" = the tag has the group's (possibly truncated) length and equals that prefix of the MAC
            const bool ok = tag.size() == tagBytes && tagBytes <= mac.size() && oc::ConstantTimeEqual(mac.data(), P(tag), tagBytes);
            const std::string res = tc.S("result");
            if (res == "valid") { ++t.valid; CHECK(ok, "%s tc %d (%s): valid tag does not match", file, id, tc.S("comment").c_str()); }
            else if (res == "invalid") { ++t.invalid; CHECK(!ok, "%s tc %d (%s): invalid tag matches", file, id, tc.S("comment").c_str()); }
            else { ++t.acceptable; CHECK(false, "%s tc %d: unexpected acceptable", file, id); }
        }
    }
    Report(file, j, t);
}

static bool Hkdf(const HashFn& h, Bytes& okm, const Bytes& ikm, const Bytes& salt, const Bytes& info, std::size_t size) {
    if (size > 255 * h.len) return false;
    Bytes s = salt;
    if (s.empty()) s.assign(h.len, 0);
    const Bytes prk = Hmac(h, s, ikm);
    okm.clear();
    Bytes prev;
    for (std::uint8_t c = 1; okm.size() < size; ++c) {
        Bytes in = prev;
        in.insert(in.end(), info.begin(), info.end());
        in.push_back(c);
        prev = Hmac(h, prk, in);
        okm.insert(okm.end(), prev.begin(), prev.end());
    }
    okm.resize(size);
    return true;
}

static void RunHkdf(const char* file, const HashFn& h) {
    Json j;
    if (!Open(file, j)) return;
    Tally t;
    for (const Json& g : j.Get("testGroups")->arr) {
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const Bytes ikm = Hex(tc.S("ikm")), salt = Hex(tc.S("salt")), info = Hex(tc.S("info")), want = Hex(tc.S("okm"));
            Bytes got;
            const bool ok = Hkdf(h, got, ikm, salt, info, static_cast<std::size_t>(tc.N("size")));
            const std::string res = tc.S("result");
            if (res == "valid") { ++t.valid; CHECK(ok && got == want, "%s tc %d (%s): wrong OKM", file, id, tc.S("comment").c_str()); }
            else if (res == "invalid") { ++t.invalid; CHECK(!ok, "%s tc %d (%s): oversize output not refused", file, id, tc.S("comment").c_str()); }
            else { ++t.acceptable; CHECK(false, "%s tc %d: unexpected acceptable", file, id); }
        }
    }
    Report(file, j, t);
}

// ---------------------------------------------------------------- raw (X)ChaCha20 counter boundaries
// The file is about the 64-bit-counter ("original") layout that libsodium's stream functions expose: state words 12 and 13
// are a 64-bit little-endian counter, the 8 bytes after the 16-byte HChaCha20 input are the nonce. okay::crypto exposes the
// IETF layout (32-bit counter in word 12, 12-byte nonce in words 13..15) and the AEADs only. The 64-bit layout is the IETF
// layout with nonce = counter_high(4 bytes LE) || iv[16..24], so this test drives ChaCha20Xor one 64-byte block at a time and
// carries the counter itself. It checks the block function on every boundary; what it cannot test is a library-level carry
// (we have none) or the "counter exhausted" refusal of the 64-bit layout (those invalid vectors are counted as not expressible:
// nothing in our API can reach 2^64 blocks). The ChaCha20-IETF group IS our API: its "invalid" vectors (a message that would
// wrap the 32-bit counter and reuse keystream) found that ChaCha20Xor used to wrap silently; it now refuses (returns false).
static void RunChaChaCounter() {
    Json j;
    if (!Open("chacha20_counter_test.json", j)) return;
    Tally t;
    for (const Json& g : j.Get("testGroups")->arr) {
        const bool ietf = g.S("algorithm") == "ChaCha20-IETF";
        for (const Json& tc : g.Get("tests")->arr) {
            const int id = static_cast<int>(tc.N("tcId"));
            const std::string res = tc.S("result");
            const Bytes key = Hex(tc.S("key")), iv = Hex(tc.S("iv")), msg = Hex(tc.S("msg")), want = Hex(tc.S("ct")), ic = Hex(tc.S("initialCounter"));
            if (ietf) {
                // ChaCha20-IETF: 32-bit counter, 12-byte nonce: exactly our ChaCha20Xor. "invalid" = would wrap the counter.
                if (key.size() != 32 || iv.size() != 12 || ic.size() != 4) { CHECK(false, "chacha counter tc %d: unexpected sizes", id); continue; }
                const std::uint32_t ctr = static_cast<std::uint32_t>(ic[0]) | static_cast<std::uint32_t>(ic[1]) << 8 | static_cast<std::uint32_t>(ic[2]) << 16 | static_cast<std::uint32_t>(ic[3]) << 24;
                Bytes out(msg.size(), 0xAA);
                const bool ok = oc::ChaCha20Xor(out.data(), P(msg), msg.size(), P(key), P(iv), ctr);
                if (res == "valid") { ++t.valid; CHECK(ok && out == want, "chacha counter tc %d (%s): keystream differs", id, tc.S("comment").c_str()); }
                else { ++t.invalid; CHECK(!ok, "chacha counter tc %d (%s): counter wrap not refused", id, tc.S("comment").c_str());
                       CHECK(out == Bytes(msg.size(), 0xAA), "chacha counter tc %d: refused call wrote output", id); }
                continue;
            }
            if (res != "valid") { ++t.skipped; continue; }   // 64-bit counter exhaustion: unreachable through our API
            if (key.size() != 32 || iv.size() != 24 || ic.size() != 8) { CHECK(false, "chacha counter tc %d: unexpected sizes", id); continue; }
            ++t.valid;
            std::uint8_t sub[32];
            oc::HChaCha20(sub, P(key), P(iv));
            std::uint64_t ctr = 0;
            for (int i = 7; i >= 0; --i) ctr = ctr << 8 | ic[static_cast<std::size_t>(i)];
            Bytes out(msg.size());
            for (std::size_t off = 0; off < msg.size(); off += 64) {
                std::uint8_t nonce[12];
                const std::uint32_t hi = static_cast<std::uint32_t>(ctr >> 32);
                nonce[0] = static_cast<std::uint8_t>(hi); nonce[1] = static_cast<std::uint8_t>(hi >> 8);
                nonce[2] = static_cast<std::uint8_t>(hi >> 16); nonce[3] = static_cast<std::uint8_t>(hi >> 24);
                for (int i = 0; i < 8; ++i) nonce[4 + i] = iv[16 + static_cast<std::size_t>(i)];
                const std::size_t take = msg.size() - off < 64 ? msg.size() - off : 64;
                CHECK(oc::ChaCha20Xor(out.data() + off, msg.data() + off, take, sub, nonce, static_cast<std::uint32_t>(ctr)), "chacha counter tc %d: refused a legal block", id);
                ++ctr;
            }
            CHECK(out == want, "chacha counter tc %d (%s): keystream differs", id, tc.S("comment").c_str());
        }
    }
    Report("chacha20_counter", j, t);
}

int main() {
    const char* env = std::getenv("OKAY_WYCHEPROOF_DIR");
    g_dir = env && *env ? env : OKAY_WYCHEPROOF_DEFAULT_DIR;
    {
        std::ifstream probe(g_dir + "/x25519_test.json");
        if (!probe) { std::printf("[wycheproof] no vectors at %s (set OKAY_WYCHEPROOF_DIR): skipped\n", g_dir.c_str()); return 0; }
    }
    std::printf("[wycheproof] vectors from %s\n", g_dir.c_str());
#ifdef OKAY_WY_SODIUM
    if (sodium_init() < 0) { std::printf("sodium_init failed\n"); return 1; }
    std::printf("[wycheproof] comparing every X25519 and Ed25519 vector with libsodium %s\n", sodium_version_string());
#endif
    const HashFn h256{32, 64, H256}, h512{64, 128, H512};
    RunX25519();
    RunEd25519();
    RunAead("chacha20_poly1305_test.json", false);
    RunAead("xchacha20_poly1305_test.json", true);
    RunHmac("hmac_sha256_test.json", h256);
    RunHmac("hmac_sha512_test.json", h512);
    RunHkdf("hkdf_sha256_test.json", h256);
    RunHkdf("hkdf_sha512_test.json", h512);
    RunChaChaCounter();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
