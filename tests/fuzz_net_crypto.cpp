// Fuzz target for the code that PARSES attacker bytes around okay::crypto (UNREVIEWED): the key-exchange
// step of SecureChannel (a peer public key of any length and value), the sealed-message open path
// (including the replay window), the session-ticket parser (SessionTicket::Verify: base64url, claim
// parsing, signature), and Ed25519 / AEAD acceptance of mutated valid messages. tests/fuzz_crypto.cpp covers the
// primitives themselves; this file covers what the engine does with bytes off the wire.
//
// Two ways to run it (same as fuzz_crypto.cpp):
//   * libFuzzer (clang):  -DOKAY_CRYPTO_LIBFUZZER=ON builds `fuzz_net_crypto` with -fsanitize=fuzzer.
//   * no libFuzzer:       a built-in mutating driver, `fuzz_net_crypto_smoke [iterations] [seed]`; best under
//                         ASan and UBSan (scripts/crypto_fuzz_sanitized.sh).
// Properties (the program aborts on a violation):
//   - Establish refuses every peer key that is not exactly 32 bytes and every small-order point, accepts the rest
//     identically for client and server, and a refused channel stays unusable;
//   - Open of a message the peer sealed succeeds exactly once (sequenced mode: a replay is refused); Open of any
//     OTHER byte string fails; a failed Open never leaves plaintext behind;
//   - a ticket is accepted only if it is byte-for-byte the one that was signed (any other spelling, claim,
//     signature or key is refused), and arbitrary strings never crash the parser;
//   - an Ed25519 signature or an AEAD message is accepted only if every input equals the valid one.
// The whole file is compiled against okay::crypto (OKAY_OWN_CRYPTO), so it needs no libsodium and no engine.
#include <okay/Crypto/Crypto.hpp>
#include <okay/Net/SecureChannel.hpp>
#include <okay/Net/SessionTicket.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace okay::net;
using Bytes = std::vector<std::uint8_t>;

namespace {
[[noreturn]] void Bad(const char* what) { std::fprintf(stderr, "fuzz_net_crypto: property violated: %s\n", what); std::abort(); }

struct Reader {
    const std::uint8_t* p; std::size_t n;
    std::uint8_t Byte() { if (!n) return 0; --n; return *p++; }
    Bytes Take(std::size_t k) { Bytes b(k, 0); const std::size_t c = k < n ? k : n; if (c) std::memcpy(b.data(), p, c); p += c; n -= c; return b; }
    Bytes Rest() { Bytes b(p, p + n); p += n; n = 0; return b; }
};

// Apply up to 3 (position, xor) edits from the reader to `b`; returns whether anything changed.
bool Mutate(Reader& r, Bytes& b) {
    const Bytes orig = b;
    const int edits = r.Byte() % 4;
    for (int i = 0; i < edits && !b.empty(); ++i) {
        const std::size_t pos = (static_cast<std::size_t>(r.Byte()) << 8 | r.Byte()) % b.size();
        b[pos] ^= r.Byte();
    }
    if (r.Byte() % 8 == 0 && !b.empty()) b.pop_back();
    if (r.Byte() % 8 == 0) b.push_back(r.Byte());
    return b != orig;
}

// A fixed pair of established channels and a fixed ticket key pair, built once.
struct World {
    SecureChannel a, b;                 // a = client, b = server
    Bytes ticketPk, ticketSk, ticket;
    SessionTicketClaims claims;
    Bytes edSeed, edPk, edSk, edMsg, edSig;
    Bytes aeadKey, aeadNonce, aeadAad, aeadPlain, aeadSealed;
    World() {
        a.GenerateKeys(); b.GenerateKeys();
        if (!a.Establish(b.PublicKey(), false) || !b.Establish(a.PublicKey(), true)) Bad("could not establish the test channels");
        SessionTicket::GenerateKeyPair(ticketPk, ticketSk);
        claims.userId = "user-1"; claims.audience = "srv"; claims.issuedAt = 1000; claims.expiresAt = 2000;
        claims.keyBinding = SessionTicket::KeyBinding(a.PublicKey());
        ticket = Bytes();
        const std::string t = SessionTicket::Sign(claims, ticketSk);
        ticket.assign(t.begin(), t.end());
        edSeed = Bytes(32, 0); for (int i = 0; i < 32; ++i) edSeed[i] = static_cast<std::uint8_t>(i * 7 + 3);
        edPk.resize(32); edSk.resize(64); edSig.resize(64);
        okay::crypto::Ed25519SeedKeypair(edPk.data(), edSk.data(), edSeed.data());
        edMsg = Bytes(40, 0x61);
        okay::crypto::Ed25519Sign(edSig.data(), edMsg.data(), edMsg.size(), edSk.data());
        aeadKey = Bytes(32, 0x11); aeadNonce = Bytes(24, 0x22); aeadAad = Bytes(9, 0x33); aeadPlain = Bytes(50, 0x44);
        aeadSealed.resize(aeadPlain.size() + 16);
        okay::crypto::XChaCha20Poly1305Seal(aeadSealed.data(), aeadPlain.data(), aeadPlain.size(), aeadAad.data(), aeadAad.size(),
                                            aeadNonce.data(), aeadKey.data());
    }
};
World& W() { static World w; return w; }

// Every small-order point: libsodium's blocklist (see tests/test_crypto.cpp), as raw bytes.
bool IsKnownSmallOrder(const Bytes& k) {
    static const char* const hex[] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0100000000000000000000000000000000000000000000000000000000000000",
        "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
        "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"};
    if (k.size() != 32) return false;
    Bytes t = k; t[31] &= 0x7f;                            // X25519 ignores the top bit
    for (const char* h : hex) {
        Bytes b(32);
        auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (int i = 0; i < 32; ++i) b[i] = static_cast<std::uint8_t>(nib(h[2 * i]) << 4 | nib(h[2 * i + 1]));
        b[31] &= 0x7f;
        if (b == t) return true;
    }
    return false;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Reader r{data, size};
    const std::uint8_t op = r.Byte() % 6;
    World& w = W();
    switch (op) {
    case 0: {   // key exchange with an arbitrary peer public key
        const bool asServer = r.Byte() & 1;
        const std::size_t want = r.Byte() % 4 == 0 ? r.Byte() % 64 : 32;   // mostly 32, sometimes any length
        Bytes peer = r.Take(want);
        SecureChannel c; c.GenerateKeys();
        const bool ok = c.Establish(peer, asServer);
        if (peer.size() != 32 && ok) Bad("a peer key of the wrong length was accepted");
        if (IsKnownSmallOrder(peer) && ok) Bad("a small-order peer key was accepted");
        if (ok != c.Ready()) Bad("Ready() disagrees with Establish()");
        SecureChannel d; d.GenerateKeys();
        const bool ok2 = d.Establish(peer, !asServer);                        // other role, other keys: same acceptance
        if (peer.size() == 32 && ok != ok2) Bad("client and server disagree about a peer key");
        std::vector<std::uint8_t> out;
        const Bytes plain = r.Take(16);
        if (!ok && c.Seal(plain.data(), plain.size(), out)) Bad("sealed on a channel whose key exchange failed");
        if (!ok && c.Open(data, size, out)) Bad("opened on a channel whose key exchange failed");
        break;
    }
    case 1: {   // open of mutated or arbitrary sealed bytes
        const bool sequenced = r.Byte() & 1;
        SecureChannel a, b;
        a.GenerateKeys(); b.GenerateKeys();
        a.Establish(b.PublicKey(), false); b.Establish(a.PublicKey(), true);
        a.SetSequenced(sequenced); b.SetSequenced(sequenced);
        const Bytes plain = r.Take(r.Byte() % 60);
        std::vector<std::uint8_t> blob, out;
        if (!a.Seal(plain.data(), plain.size(), blob)) Bad("seal failed");
        const Bytes original = blob;
        const bool changed = Mutate(r, blob);
        const bool ok = b.Open(blob.data(), blob.size(), out);
        if (changed && ok) Bad("a modified message opened");
        if (!changed) {
            if (!ok || out != plain) Bad("a valid message did not open to its plaintext");
            std::vector<std::uint8_t> again;
            if (sequenced && b.Open(original.data(), original.size(), again)) Bad("a replay opened");
            if (sequenced) for (std::uint8_t x : again) if (x) Bad("a replay left plaintext behind");
        }
        if (!ok && changed) for (std::uint8_t x : out) if (x) Bad("a failed open left plaintext behind");
        // Arbitrary bytes of the remaining input must not open either.
        const Bytes junk = r.Rest();
        std::vector<std::uint8_t> o2;
        if (b.Open(junk.data(), junk.size(), o2)) Bad("arbitrary bytes opened");
        break;
    }
    case 2: {   // ticket: mutated valid ticket, then an arbitrary string
        Bytes t = w.ticket;
        const bool changed = Mutate(r, t);
        SessionTicketClaims c; std::string err;
        const std::string ts(t.begin(), t.end());
        const bool ok = SessionTicket::Verify(ts, w.ticketPk, "srv", 1500, c, &err);
        if (changed && ok) Bad("a modified ticket was accepted");
        if (!changed && (!ok || c.userId != "user-1")) Bad("the valid ticket was refused");
        const Bytes junk = r.Rest();
        const std::string js(junk.begin(), junk.end());
        SessionTicketClaims c2;
        if (SessionTicket::Verify(js, w.ticketPk, "srv", 1500, c2, nullptr)) Bad("an arbitrary string was accepted as a ticket");
        // The parser pieces on their own: decoding then encoding must give back the input (canonical spelling).
        Bytes dec;
        if (SessionTicket::Base64UrlDecode(js, dec) && SessionTicket::Base64UrlEncode(dec) != js) Bad("base64url is not canonical");
        break;
    }
    case 3: {   // Ed25519: mutated signature / message / key
        Bytes sig = w.edSig, msg = w.edMsg, pk = w.edPk;
        bool changed = false;
        changed |= Mutate(r, sig); changed |= Mutate(r, msg); changed |= Mutate(r, pk);
        if (r.Byte() % 4 == 0 && sig.size() == 64) {   // the structured one random edits never find: S + L (a malleated signature)
            static const std::uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
            unsigned carry = 0;
            for (int i = 0; i < 32; ++i) { carry += static_cast<unsigned>(sig[32 + i]) + L[i]; sig[32 + i] = static_cast<std::uint8_t>(carry); carry >>= 8; }
            changed = true;                            // (an overflow past 2^256 just wraps: still a different, invalid S)
        }
        if (sig.size() == 64 && pk.size() == 32) {    // (the API takes fixed-size arrays; a resized input is not a call it allows)
            const bool ok = okay::crypto::Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data());
            const bool ok2 = okay::crypto::Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data());
            if (ok != ok2) Bad("verify is not deterministic");
            if (changed && ok) Bad("a modified signature, message or key verified");
            if (!changed && !ok) Bad("the valid signature was refused");
        }
        break;
    }
    case 4: {   // AEAD: mutated valid message (key, nonce, aad, ciphertext)
        Bytes key = w.aeadKey, nonce = w.aeadNonce, aad = w.aeadAad, ct = w.aeadSealed;
        bool changed = false;
        changed |= Mutate(r, ct);
        Reader r2 = r;
        Bytes k2 = key; if (r2.Byte() % 3 == 0) { k2[r2.Byte() % 32] ^= 1 + r2.Byte() % 255; changed = true; }
        Bytes n2 = nonce; if (r2.Byte() % 3 == 0) { n2[r2.Byte() % 24] ^= 1 + r2.Byte() % 255; changed = true; }
        Bytes a2 = aad; if (r2.Byte() % 3 == 0) { a2.push_back(r2.Byte()); changed = true; }
        Bytes out(ct.size() + 1, 0x5a);
        const bool ok = okay::crypto::XChaCha20Poly1305Open(out.data(), ct.data(), ct.size(), a2.data(), a2.size(), n2.data(), k2.data());
        if (out.back() != 0x5a) Bad("wrote past the plaintext");
        if (changed && ok) Bad("a modified message, key, nonce or aad opened");
        if (!changed && (!ok || Bytes(out.begin(), out.begin() + w.aeadPlain.size()) != w.aeadPlain)) Bad("the valid message did not open");
        break;
    }
    default: {  // the replay window: a handful of sequence numbers in random order, none may open twice
        SecureChannel a, b;
        a.GenerateKeys(); b.GenerateKeys();
        a.Establish(b.PublicKey(), false); b.Establish(a.PublicKey(), true);
        a.SetSequenced(true); b.SetSequenced(true);
        std::vector<Bytes> blobs;
        for (int i = 0; i < 12; ++i) { std::vector<std::uint8_t> bl; const std::uint8_t p[1] = {static_cast<std::uint8_t>(i)}; a.Seal(p, 1, bl); blobs.push_back(bl); }
        bool seen[12] = {false};
        std::vector<std::uint8_t> out;
        for (int step = 0; step < 30; ++step) {
            const int i = r.Byte() % 12;
            const bool ok = b.Open(blobs[i].data(), blobs[i].size(), out);
            if (ok && seen[i]) Bad("a sequenced message opened twice");
            if (ok) { seen[i] = true; if (out.size() != 1 || out[0] != i) Bad("wrong plaintext from the replay window"); }
            if (!ok && !seen[i] && step < 1) Bad("first delivery refused");   // (later ones may be 'too old' only after 64 newer: not possible with 12)
            if (!ok && !seen[i]) Bad("an in-window first delivery was refused");
        }
        break;
    }
    }
    return 0;
}

#ifndef OKAY_CRYPTO_LIBFUZZER
int main(int argc, char** argv) {
    const long iters = argc > 1 ? std::atol(argv[1]) : 3000;
    std::uint64_t s = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 0x9e3779b97f4a7c15ull;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    std::vector<Bytes> corpus(1, Bytes(1, 0));
    for (long i = 0; i < iters; ++i) {
        Bytes in;
        if (corpus.size() > 1 && rnd() % 4 != 0) {
            in = corpus[rnd() % corpus.size()];
            const int muts = 1 + static_cast<int>(rnd() % 4);
            for (int m = 0; m < muts; ++m) {
                switch (rnd() % 4) {
                case 0: if (!in.empty()) in[rnd() % in.size()] ^= static_cast<std::uint8_t>(1u << (rnd() % 8)); break;
                case 1: if (!in.empty()) in[rnd() % in.size()] = static_cast<std::uint8_t>(rnd()); break;
                case 2: in.insert(in.begin() + (in.empty() ? 0 : rnd() % in.size()), static_cast<std::uint8_t>(rnd())); break;
                default: if (!in.empty()) in.erase(in.begin() + rnd() % in.size()); break;
                }
            }
        } else {
            in.resize(rnd() % 120);
            for (auto& b : in) b = static_cast<std::uint8_t>(rnd());
            if (!in.empty()) in[0] = static_cast<std::uint8_t>(rnd() % 6);
        }
        LLVMFuzzerTestOneInput(in.data(), in.size());
        if (corpus.size() < 512) corpus.push_back(in); else corpus[rnd() % corpus.size()] = in;
    }
    std::printf("fuzz_net_crypto: %ld iterations, no property violated\n", iters);
    return 0;
}
#endif
