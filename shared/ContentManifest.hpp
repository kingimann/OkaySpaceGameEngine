#pragma once
// Content manifest: the signed list of files a game downloads after it is installed (models, textures, animations, scenes, audio,
// dialogue, scripts and data). Docs: docs/release/content_delivery.md. Header only, no threads, no engine code (so the launcher, the player,
// the okay-content tool and tests can all include it). Written for this project from the plain description in the docs; the only
// outside format knowledge is the SHA-256 and ed25519 standards, which come from the existing code (okay::crypto Sha256,
// shared/UpdateSignature.hpp key and signature parsing). NO new cryptography is written here.
//
// Text format (UTF-8, '\n' line ends, TAB between fields, no blank lines, no comments; every other shape is refused):
//
//   okay-content 1
//   game <id>                       the game this content is for ([a-z0-9._-], 1..64); a client only accepts its own game's id
//   version <n>                     content version, a whole number 1..2^53; a client never goes to a LOWER number (replay floor)
//   min-app <x.y.z>                 optional: the whole manifest needs at least this app version
//   file <TAB> <sha256 hex> <TAB> <size> <TAB> <kind> <TAB> <group or -> <TAB> <min-app or -> <TAB> <path>
//
// kind: model texture animation scene audio dialogue script data. "script" is Visual Script / OkayScript text and "data" is plain data
// (.okaydata, strings, items). There is NO kind for programs or libraries: content is data and scripts only, never native code.
//
// Signature: content.manifest.sig, "okay-sig1 <128 hex>" (the same line format as versions.json.sig), over the message
// "okayspace-content-manifest-v1\n" + the manifest bytes. The domain differs from the release index's, so a signature made for one can
// never be valid for the other even when the same key signs both.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ProjectVersion.hpp"
#include "UpdateSignature.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Crypto/Sha256.hpp"

namespace okaycontent {

inline constexpr const char* kManifestName = "content.manifest";
inline constexpr const char* kSigName = "content.manifest.sig";
inline constexpr const char* kSigDomain = "okayspace-content-manifest-v1\n";
inline constexpr const char* kMagicLine = "okay-content 1";

enum class Kind { Model, Texture, Animation, Scene, Audio, Dialogue, Script, Data };

inline const char* KindName(Kind k) {
    switch (k) {
        case Kind::Model: return "model";
        case Kind::Texture: return "texture";
        case Kind::Animation: return "animation";
        case Kind::Scene: return "scene";
        case Kind::Audio: return "audio";
        case Kind::Dialogue: return "dialogue";
        case Kind::Script: return "script";
        case Kind::Data: return "data";
    }
    return "data";
}
inline bool KindFromName(const std::string& s, Kind& out) {
    static const Kind all[] = {Kind::Model, Kind::Texture, Kind::Animation, Kind::Scene, Kind::Audio, Kind::Dialogue, Kind::Script, Kind::Data};
    for (Kind k : all) if (s == KindName(k)) { out = k; return true; }
    return false;
}

/// Hard limits. A client may pass tighter ones; a manifest that breaks any of them is refused whole.
struct Limits {
    std::size_t maxManifestBytes = 4u << 20;                  ///< the manifest text itself
    std::size_t maxEntries = 20000;
    std::uint64_t maxFileBytes = 2ull << 30;                  ///< one file
    std::uint64_t maxTotalBytes = 8ull << 30;                 ///< all files of one version
    std::size_t maxGroupBytes = 64;
};

struct Entry {
    std::string path;          ///< relative, '/' separators, passes okay::pathsafety (Portable mode)
    std::string sha256;        ///< 64 lowercase hex
    std::uint64_t size = 0;
    Kind kind = Kind::Data;
    std::string group;         ///< "" = always downloaded
    std::string minApp;        ///< "" = any app version; else the entry is skipped by an older app
};

struct Manifest {
    std::string game;
    std::uint64_t version = 0;
    std::string minApp;
    std::vector<Entry> entries;     ///< sorted by path (Serialize and Parse both enforce it)
    std::uint64_t TotalBytes() const { std::uint64_t t = 0; for (const Entry& e : entries) t += e.size; return t; }
};

namespace detail {
inline char Low(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline std::string Lower(std::string s) { for (char& c : s) c = Low(c); return s; }
inline bool IsLowerHex(const std::string& s, std::size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool IdOk(const std::string& s, std::size_t maxLen) {
    if (s.empty() || s.size() > maxLen) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
inline bool Digits(const std::string& s, std::uint64_t& out, std::uint64_t maxVal) {
    if (s.empty() || s.size() > 19) return false;
    if (s.size() > 1 && s[0] == '0') return false;   // no leading zeros: one spelling per number
    std::uint64_t v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + static_cast<std::uint64_t>(c - '0'); }
    if (v > maxVal) return false;
    out = v;
    return true;
}
// A strict "x.y" or "x.y.z" of plain numbers (the shape the app version has); "dev" or words are not a requirement.
inline bool VersionOk(const std::string& s) {
    int dots = 0;
    if (s.empty() || s.size() > 24) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') { if (i == 0 || s[i - 1] == '.' || i + 1 == s.size()) return false; ++dots; }
        else if (c < '0' || c > '9') return false;
    }
    return dots >= 1 && dots <= 2;
}
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t e = s.find(sep, pos);
        out.push_back(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return out;
}
}  // namespace detail

/// Why a path is refused (empty = fine). Stricter than a script path: Portable rules plus no backslash (one spelling), no "." part,
/// no empty part, a dot-leading first part is refused (".okay-content" is the client's own bookkeeping folder).
inline std::string PathProblem(const std::string& p) {
    const okay::pathsafety::Why w = okay::pathsafety::Check(p, okay::pathsafety::Mode::Portable);
    if (w != okay::pathsafety::Why::Ok) return okay::pathsafety::Describe(w);
    if (p.find('\\') != std::string::npos) return "uses a backslash (write paths with '/')";
    if (p.front() == '/' || p.back() == '/') return "starts or ends with '/'";
    for (const std::string& part : detail::Split(p, '/')) {
        if (part.empty()) return "has an empty part ('//')";
        if (part == "." || part == "..") return "has a '.' or '..' part";
    }
    if (p[0] == '.') return "starts with '.' (reserved for the client's own files)";
    return std::string();
}

/// Extensions of native code and of things the system runs by itself. Content is data and scripts only; a manifest that lists one of
/// these is refused even when it is correctly signed (defence in depth: a UWP / Store app must never download and run code).
inline bool IsExecutableExtension(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = detail::Lower(path.substr(dot + 1));
    static const char* bad[] = {"exe", "dll", "sys", "drv", "ocx", "so", "dylib", "a", "lib", "o", "bat", "cmd", "com", "scr", "pif", "msi", "msix", "appx", "appxbundle",
                                "ps1", "psm1", "vbs", "vbe", "wsf", "hta", "jar", "apk", "dex", "lnk", "cpl", "sh", "dmg", "pkg", "wasm"};
    for (const char* b : bad) if (ext == b) return true;
    return false;
}

/// Validate one entry (paths, hash shape, size limits, names). Empty = fine.
inline std::string EntryProblem(const Entry& e, const Limits& lim) {
    if (std::string w = PathProblem(e.path); !w.empty()) return "path '" + e.path + "': " + w;
    if (IsExecutableExtension(e.path)) return "'" + e.path + "': programs and libraries are not content (data and scripts only)";
    if (!detail::IsLowerHex(e.sha256, 64)) return "'" + e.path + "': sha256 is not 64 lowercase hex digits";
    if (e.size > lim.maxFileBytes) return "'" + e.path + "': larger than the per-file limit";
    if (!e.group.empty() && !detail::IdOk(e.group, lim.maxGroupBytes)) return "'" + e.path + "': group name is not [a-z0-9._-]";
    if (!e.minApp.empty() && !detail::VersionOk(e.minApp)) return "'" + e.path + "': min-app is not a version like 2.886.0";
    return std::string();
}

/// Check a whole manifest (also what Parse runs after reading). Empty = fine.
inline std::string Problem(const Manifest& m, const Limits& lim = Limits()) {
    if (!detail::IdOk(m.game, 64)) return "the game id is missing or not [a-z0-9._-] (at most 64)";
    if (m.version == 0 || m.version > (1ull << 53)) return "the content version must be 1 to 2^53";
    if (!m.minApp.empty() && !detail::VersionOk(m.minApp)) return "min-app is not a version like 2.886.0";
    if (m.entries.size() > lim.maxEntries) return "too many files (limit " + std::to_string(lim.maxEntries) + ")";
    std::set<std::string> seen;          // lower-cased paths: two names that differ only by case would collide on Windows
    std::set<std::string> dirs, files;   // a name cannot be both a file and a folder ("a" and "a/b")
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < m.entries.size(); ++i) {
        const Entry& e = m.entries[i];
        if (std::string w = EntryProblem(e, lim); !w.empty()) return w;
        if (i > 0 && !(m.entries[i - 1].path < e.path)) return "files are not in strict byte order by path (or a path repeats): '" + e.path + "'";
        const std::string low = detail::Lower(e.path);
        if (!seen.insert(low).second) return "two files differ only by letter case: '" + e.path + "'";
        files.insert(low);
        for (std::size_t s = low.find('/'); s != std::string::npos; s = low.find('/', s + 1)) dirs.insert(low.substr(0, s));
        if (e.size > lim.maxTotalBytes || total > lim.maxTotalBytes - e.size) return "the files add up to more than the total limit";
        total += e.size;
    }
    for (const std::string& f : files) if (dirs.count(f)) return "'" + f + "' is a file and also a folder";
    return std::string();
}

/// The exact bytes to sign and publish. Entries are written in path order (sorts a copy).
inline std::string Serialize(Manifest m) {
    std::sort(m.entries.begin(), m.entries.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
    std::string s = std::string(kMagicLine) + "\n";
    s += "game " + m.game + "\n";
    s += "version " + std::to_string(m.version) + "\n";
    if (!m.minApp.empty()) s += "min-app " + m.minApp + "\n";
    for (const Entry& e : m.entries) {
        s += "file\t" + e.sha256 + "\t" + std::to_string(e.size) + "\t" + KindName(e.kind) + "\t" + (e.group.empty() ? "-" : e.group) + "\t" +
             (e.minApp.empty() ? "-" : e.minApp) + "\t" + e.path + "\n";
    }
    return s;
}

/// Strict parse. False with a plain `why` (and `out` cleared) for anything outside the format or over a limit.
inline bool Parse(const std::string& text, Manifest& out, std::string* why = nullptr, const Limits& lim = Limits()) {
    out = Manifest();
    auto fail = [&](const std::string& m) { if (why) *why = m; out = Manifest(); return false; };
    if (text.size() > lim.maxManifestBytes) return fail("the manifest is larger than " + std::to_string(lim.maxManifestBytes) + " bytes");
    if (text.empty() || text.back() != '\n') return fail("the manifest must end with a newline");
    if (text.find('\r') != std::string::npos) return fail("the manifest must use '\\n' line ends");
    if (text.find('\0') != std::string::npos) return fail("the manifest contains a NUL byte");
    for (unsigned char c : text) if ((c < 32 && c != '\n' && c != '\t') || c == 127) return fail("the manifest contains a control character");
    std::vector<std::string> lines = detail::Split(text.substr(0, text.size() - 1), '\n');
    if (lines.empty() || lines[0] != kMagicLine) return fail("not an okay-content 1 manifest");
    std::size_t i = 1;
    bool haveGame = false, haveVersion = false, haveMin = false;
    for (; i < lines.size() && lines[i].compare(0, 5, "file\t") != 0; ++i) {
        const std::string& l = lines[i];
        const std::size_t sp = l.find(' ');
        if (sp == std::string::npos) return fail("line " + std::to_string(i + 1) + ": expected 'key value'");
        const std::string key = l.substr(0, sp), val = l.substr(sp + 1);
        if (key == "game" && !haveGame) { out.game = val; haveGame = true; }
        else if (key == "version" && !haveVersion) {
            if (!detail::Digits(val, out.version, 1ull << 53)) return fail("line " + std::to_string(i + 1) + ": bad version number");
            haveVersion = true;
        } else if (key == "min-app" && !haveMin) { out.minApp = val; haveMin = true; }
        else return fail("line " + std::to_string(i + 1) + ": unknown or repeated header '" + key + "'");
    }
    if (!haveGame || !haveVersion) return fail("the manifest needs 'game' and 'version' lines");
    for (; i < lines.size(); ++i) {
        const std::vector<std::string> f = detail::Split(lines[i], '\t');
        const std::string at = "line " + std::to_string(i + 1) + ": ";
        if (f.size() != 7 || f[0] != "file") return fail(at + "a file line has 7 tab-separated fields");
        Entry e;
        e.sha256 = f[1];
        if (!detail::Digits(f[2], e.size, ~0ull >> 1)) return fail(at + "bad size");
        if (!KindFromName(f[3], e.kind)) return fail(at + "unknown kind '" + f[3] + "'");
        e.group = f[4] == "-" ? std::string() : f[4];
        e.minApp = f[5] == "-" ? std::string() : f[5];
        e.path = f[6];
        if (out.entries.size() >= lim.maxEntries) return fail("too many files (limit " + std::to_string(lim.maxEntries) + ")");
        out.entries.push_back(std::move(e));
    }
    if (std::string w = Problem(out, lim); !w.empty()) return fail(w);
    return true;
}

// ---- Signature (reuses shared/UpdateSignature.hpp: key list and signature line parsing; ed25519 from AntiCheatCrypto.hpp) ----

inline std::string SignedMessage(const std::string& manifestBytes) { return std::string(kSigDomain) + manifestBytes; }

/// The text of content.manifest.sig. For the publisher's tool and for tests with throw-away seeds; the real seed never lives in the repo.
inline std::string MakeSignatureFile(const std::string& manifestBytes, const okayac::ed25519::Seed& seed) {
    const okayac::ed25519::Signature s = okayac::ed25519::Sign(SignedMessage(manifestBytes), seed);
    return std::string(okayupdate::kSigTag) + " " + okayac::ToHex(s.data(), s.size()) + "\n";
}

enum class SigKind { Valid, NoKeys, KeysInvalid, Missing, Bad, UnsignedAllowed };
struct SigVerdict {
    SigKind kind = SigKind::NoKeys;
    int keyIndex = -1;
    std::string message;
    bool Accepts() const { return kind == SigKind::Valid || kind == SigKind::UnsignedAllowed; }
};

/// `trustedKeys` = "<64 hex>[,<64 hex>...]" (compiled in OKAY_CONTENT_TRUSTED_KEYS, or the game's config). Unlike the release index there is
/// NO transition mode: no key means content is REFUSED, unless `allowUnsigned` is set (a local test; the player only sets it from an
/// explicit developer switch and says so in its log).
inline SigVerdict CheckSignature(const std::string& trustedKeys, const std::string& manifestBytes, const std::string& sigText, bool allowUnsigned = false) {
    SigVerdict v;
    if (allowUnsigned) { v.kind = SigKind::UnsignedAllowed; v.message = "content signature check is OFF (developer switch)"; return v; }
    if (trustedKeys.empty()) { v.kind = SigKind::NoKeys; v.message = "Content refused: this game has no content signing key configured."; return v; }
    std::vector<okayac::ed25519::PublicKey> keys;
    std::string why;
    if (!okayupdate::ParseKeys(trustedKeys, keys, &why)) { v.kind = SigKind::KeysInvalid; v.message = "Content refused: unusable content key (" + why + ")."; return v; }
    if (sigText.empty()) { v.kind = SigKind::Missing; v.message = "Content refused: the manifest is not signed."; return v; }
    okayac::ed25519::Signature sig{};
    if (!okayupdate::ParseSignature(sigText, sig)) { v.kind = SigKind::Bad; v.message = "Content refused: the manifest signature is damaged."; return v; }
    const std::string msg = SignedMessage(manifestBytes);
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (okayac::ed25519::Verify(msg, sig, keys[i])) { v.kind = SigKind::Valid; v.keyIndex = static_cast<int>(i); return v; }
    v.kind = SigKind::Bad;
    v.message = "Content refused: the manifest was not signed by this game's publisher key.";
    return v;
}

/// SHA-256 of bytes as 64 lowercase hex (the engine's okay::crypto::Sha256).
inline std::string Sha256Hex(const std::string& bytes) {
    okay::crypto::Sha256 h;
    h.Update(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    std::uint8_t d[32];
    h.Final(d);
    return okayac::ToHex(d, 32);
}

}  // namespace okaycontent
