#pragma once
// Signature check for the release index (versions.json). Docs: docs/updater.md ("Signed releases").
//
// The publisher signs the EXACT bytes of versions.json with an ed25519 key that never leaves their machine and publishes the
// signature next to it as versions.json.sig. A launcher or editor built with -DOKAY_UPDATE_TRUSTED_KEYS=<hex>[,<hex>] (public keys:
// the current one and, during a rotation, the next one) refuses an index whose signature is missing or does not verify under any
// of them. Built without keys it behaves as before (no signature needed): that is the transition for feeds nobody has signed yet,
// and it is the only case where an unsigned index is accepted.
//
//   signature file:  one line, "okay-sig1 <128 hex digits>\n"
//   signed message:  "okayspace-versions-json-v1\n" + <versions.json bytes>   (a domain prefix, so a signature made for this can
//                    never be a valid signature for another use of the same key, such as the anti-cheat manifest)
//
// Header only; the verify side needs no libsodium (shared/AntiCheatCrypto.hpp falls back to its portable ed25519).
#include <string>
#include <vector>

#include "AntiCheatCrypto.hpp"

#ifndef OKAY_UPDATE_TRUSTED_KEYS
#define OKAY_UPDATE_TRUSTED_KEYS ""
#endif

namespace okayupdate {

inline constexpr const char* kSigName = "versions.json.sig";
inline constexpr const char* kSigTag = "okay-sig1";
inline constexpr const char* kDomain = "okayspace-versions-json-v1\n";
inline constexpr std::size_t kMaxKeys = 4;
inline constexpr std::size_t kMaxSigBytes = 1024;

/// The public keys built into this program (comma-separated hex); "" = none: unsigned indexes are accepted.
inline std::string BuiltInTrustedKeys() { return OKAY_UPDATE_TRUSTED_KEYS; }

/// Parse "<64 hex>[,<64 hex>...]". False (with `why`) for any malformed, weak (non-canonical / small-order) or surplus key: a bad list
/// must never quietly become "trust nothing" or "trust no signature".
inline bool ParseKeys(const std::string& list, std::vector<okayac::ed25519::PublicKey>& out, std::string* why = nullptr) {
    out.clear();
    std::size_t pos = 0;
    while (pos <= list.size()) {
        const std::size_t comma = list.find(',', pos);
        const std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        std::vector<std::uint8_t> raw;
        okayac::ed25519::PublicKey pk{};
        if (item.size() != 64 || !okayac::FromHex(item, raw) || raw.size() != 32) {
            if (why) *why = "the update key list is not a list of 64-digit hex public keys";
            out.clear();
            return false;
        }
        std::copy(raw.begin(), raw.end(), pk.begin());
        if (okayac::ed25519::IsWeakKey(pk)) {
            if (why) *why = "the update key list holds a weak public key";
            out.clear();
            return false;
        }
        out.push_back(pk);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (out.empty() || out.size() > kMaxKeys) {
        if (why) *why = "the update key list must hold one to " + std::to_string(kMaxKeys) + " keys";
        out.clear();
        return false;
    }
    return true;
}

inline std::string SignedMessage(const std::string& body) { return std::string(kDomain) + body; }

/// "okay-sig1 <128 hex>" (surrounding whitespace ignored; nothing else allowed).
inline bool ParseSignature(const std::string& text, okayac::ed25519::Signature& out) {
    if (text.size() > kMaxSigBytes) return false;
    const std::size_t a = text.find_first_not_of(" \t\r\n");
    const std::size_t b = text.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    const std::string t = text.substr(a, b - a + 1);
    const std::string tag = std::string(kSigTag) + " ";
    if (t.size() != tag.size() + 128 || t.compare(0, tag.size(), tag) != 0) return false;
    std::vector<std::uint8_t> raw;
    if (!okayac::FromHex(t.substr(tag.size()), raw) || raw.size() != 64) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}

/// The text of a signature file (for the publisher's tools and tests; the real publish step is scripts/sign_index.py).
inline std::string MakeSignatureFile(const std::string& body, const okayac::ed25519::Seed& seed) {
    const okayac::ed25519::Signature s = okayac::ed25519::Sign(SignedMessage(body), seed);
    return std::string(kSigTag) + " " + okayac::ToHex(s.data(), s.size()) + "\n";
}

enum class Kind {
    NotRequired,   ///< no key built in: an unsigned index is accepted (the transition)
    Valid,         ///< signed by one of the trusted keys
    Missing,       ///< a key is built in but there is no signature file
    Bad,           ///< a signature is there and does not verify under any trusted key (or is malformed)
    KeysInvalid,   ///< the baked key list is unusable (refuse everything; never fall back to unsigned)
};
struct Verdict {
    Kind kind = Kind::NotRequired;
    int keyIndex = -1;           ///< which trusted key verified (0 = first)
    std::string message;         ///< plain words for the person, "" when fine
    bool Accepts() const { return kind == Kind::NotRequired || kind == Kind::Valid; }
};

/// `trusted` = the key list ("" = none), `body` = the versions.json bytes as downloaded, `sigText` = versions.json.sig ("" = absent).
inline Verdict Check(const std::string& trusted, const std::string& body, const std::string& sigText) {
    Verdict v;
    if (trusted.empty()) return v;
    std::vector<okayac::ed25519::PublicKey> keys;
    std::string why;
    if (!ParseKeys(trusted, keys, &why)) {
        v.kind = Kind::KeysInvalid;
        v.message = "Update refused: this program was built with an unusable update key (" + why + "). Download a fresh installer.";
        return v;
    }
    static const char* kTail = " Nothing was installed and your current version is unchanged. The release server may be mid-publish: "
                               "try again in a minute. If it keeps happening, do not install: download the full installer from the official release page.";
    if (sigText.empty()) {
        v.kind = Kind::Missing;
        v.message = std::string("Update refused: the release list (versions.json) is not signed.") + kTail;
        return v;
    }
    okayac::ed25519::Signature sig{};
    if (!ParseSignature(sigText, sig)) {
        v.kind = Kind::Bad;
        v.message = std::string("Update refused: the signature of the release list (versions.json.sig) is damaged.") + kTail;
        return v;
    }
    const std::string msg = SignedMessage(body);
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (okayac::ed25519::Verify(msg, sig, keys[i])) { v.kind = Kind::Valid; v.keyIndex = (int)i; return v; }
    v.kind = Kind::Bad;
    v.message = std::string("Update refused: the release list (versions.json) was not signed by the publisher's key.") + kTail;
    return v;
}

}  // namespace okayupdate
