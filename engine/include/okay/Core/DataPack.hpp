#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace okay {

/// Lightweight obfuscation for shipped game data (scenes, config), so players
/// can't casually open and read or edit your game in a text editor. A packed blob
/// is `OKP1` + nonce + checksum + a keystream-XOR'd body; the player unpacks it
/// transparently at load.
///
/// SECURITY NOTE: this is obfuscation, not strong DRM. The key ships inside the
/// game binary, so a determined attacker can still recover the data — it deters
/// casual reading/tampering (and the checksum detects edits), nothing more.
struct DataPack {
    static const char* Magic() { return "OKP1"; }
    static constexpr std::size_t kHeader = 4 + 8 + 8;   // magic + nonce + checksum

    // splitmix64 — a strong, cheap bit-mixer used as the keystream generator.
    static std::uint64_t Mix(std::uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
    // FNV-1a 64-bit — content checksum (tamper detection).
    static std::uint64_t Fnv(const std::string& s) {
        std::uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
    /// The obfuscation key baked into the engine (editor packs, player unpacks).
    static std::uint64_t DefaultKey() { return 0xA17C0FFEE5EED15Cull; }

    static void XorStream(std::string& data, std::uint64_t key, std::uint64_t nonce) {
        std::uint64_t base = key ^ nonce;
        for (std::size_t i = 0; i < data.size(); ++i) {
            std::uint64_t ks = Mix(base + (std::uint64_t)(i / 8));
            data[i] = (char)((unsigned char)data[i] ^ (unsigned char)(ks >> ((i % 8) * 8)));
        }
    }

    /// Wrap plaintext: [magic 4][nonce 8 LE][fnv(plain) 8 LE][keystream-XOR'd body].
    static std::string Encode(const std::string& plain, std::uint64_t key, std::uint64_t nonce) {
        std::string body = plain;
        std::uint64_t chk = Fnv(plain);
        XorStream(body, key, nonce);
        std::string out; out.reserve(kHeader + body.size());
        out.append(Magic(), 4);
        auto put64 = [&](std::uint64_t v) { for (int i = 0; i < 8; ++i) out += (char)((v >> (i * 8)) & 0xFF); };
        put64(nonce); put64(chk);
        out += body;
        return out;
    }

    static bool IsPacked(const std::string& d) {
        return d.size() >= kHeader && d.compare(0, 4, Magic()) == 0;
    }

    /// Recover the plaintext; returns empty on bad magic or a failed checksum
    /// (corrupt / tampered file).
    static std::string Decode(const std::string& packed, std::uint64_t key) {
        if (!IsPacked(packed)) return std::string();
        auto get64 = [&](std::size_t off) { std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i) v |= (std::uint64_t)(unsigned char)packed[off + i] << (i * 8); return v; };
        std::uint64_t nonce = get64(4), chk = get64(12);
        std::string body = packed.substr(kHeader);
        XorStream(body, key, nonce);
        if (Fnv(body) != chk) return std::string();   // tampered or wrong key
        return body;
    }

    /// Convenience with the default key + a content-derived nonce.
    static std::string Pack(const std::string& plain) {
        return Encode(plain, DefaultKey(), Fnv(plain) ^ 0x5DEECE66Dull);
    }
    /// Decode if packed, else return the input unchanged (plaintext back-compat).
    static std::string Unpack(const std::string& data) {
        return IsPacked(data) ? Decode(data, DefaultKey()) : data;
    }
};

} // namespace okay
