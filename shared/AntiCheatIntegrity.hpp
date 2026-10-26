#pragma once
// Anti-cheat, integrity (opt-in; docs/anticheat.md).
//
// File > Build Game writes Data/manifest.json: every shipped file's path, size
// and SHA-256, signed with ed25519 when the owner has set up a signing key.
// Before a launch the launcher, and the player itself, check the install against
// it. Anti-cheat is off unless a build or the launcher turns it on; then it
// reports problems by default and only refuses a launch in "enforce" mode.
//
//   {
//     "format": "okayspace-integrity/1",
//     "game": "My Game", "version": "1.0.0",
//     "files": [{"path": "Data/game.okayscene", "size": 123, "sha256": "..."}, ...],
//     "signature": {"alg": "ed25519", "key": "<public key hex>", "sig": "<signature hex>"}
//   }
//
// The signature covers the manifest text up to (not including) "signature",
// exactly as SignedPayload() writes it; a verifier rebuilds those bytes from the
// parsed fields, so whitespace in the file doesn't matter.
//
// Files are checked with okayofficial::Verify (size + SHA-256), the same check
// the launcher's official-game updater makes, and manifest paths go through the
// same path-safety rules (okayofficial::SafeRelPath / ResolveInside).
//
// Keys (never in the repo; the owner makes them):
//   OKAY_AC_SIGNING_KEY_FILE  path to the private key (hex seed) the editor signs with
//   OKAY_AC_SIGNING_KEY       the same, inline (CI secrets)
//   -DOKAY_AC_TRUSTED_KEYS=<hex>[,<hex>]  public keys the player / launcher trust (CMake)
//   OKAY_AC_TRUSTED_KEY       a public key to trust at run time, only in a build with none built in (dev)
#include <cstdlib>
#include <functional>
#include <map>

#include "AntiCheatCrypto.hpp"
#include "OfficialGame.hpp"   // og::File / Verify / FileSha256 / SafeRelPath / json

#ifndef OKAY_AC_TRUSTED_KEYS
#define OKAY_AC_TRUSTED_KEYS ""
#endif

namespace okayac {

namespace fs = std::filesystem;
namespace og = okayofficial;

// ---- Mode ------------------------------------------------------------------
// Off: nothing runs (the default; builds and launches behave as without anti-cheat).
// Report: checks run and log what they find; nothing is refused.
// Enforce: a failed check refuses the launch / ends the game.
enum class Mode { Off = 0, Report = 1, Enforce = 2 };

inline Mode ParseMode(const std::string& s) {
    const std::string v = og::Lower(og::Trim(s));
    if (v == "report" || v == "1") return Mode::Report;
    if (v == "enforce" || v == "2") return Mode::Enforce;
    return Mode::Off;   // "", "off", "0", and anything unknown
}
inline const char* ModeName(Mode m) {
    return m == Mode::Enforce ? "enforce" : m == Mode::Report ? "report" : "off";
}
inline Mode Stricter(Mode a, Mode b) { return (int)a >= (int)b ? a : b; }

// ---- The manifest ----------------------------------------------------------
constexpr const char* kIntegrityFormat = "okayspace-integrity/1";
constexpr const char* kIntegrityManifest = "Data/manifest.json";

struct Integrity {
    std::string game, version;
    std::vector<og::File> files;    // sorted by path when the build writes it
    Mode mode = Mode::Off;          // the game's anti-cheat mode, inside the signed payload ("anticheat")
    bool hasSignature = false;
    ed25519::PublicKey key{};
    ed25519::Signature sig{};
};

inline std::string KeyId(const ed25519::PublicKey& k) { return ToHex(k.data(), 8); }

// The signed bytes: the manifest without its signature.
inline std::string SignedPayload(const Integrity& m) {
    using og::json::Escape;
    std::ostringstream o;
    o << "{\n  \"format\": \"" << kIntegrityFormat << "\",\n  \"game\": \"" << Escape(m.game)
      << "\",\n  \"version\": \"" << Escape(m.version) << "\",\n";
    if (m.mode != Mode::Off) o << "  \"anticheat\": \"" << ModeName(m.mode) << "\",\n";   // signed with the rest
    o << "  \"files\": [";
    for (std::size_t i = 0; i < m.files.size(); ++i)
        o << (i ? ",\n" : "\n") << "    {\"path\": \"" << Escape(m.files[i].path) << "\", \"size\": " << m.files[i].size
          << ", \"sha256\": \"" << m.files[i].sha256 << "\"}";
    o << "\n  ]";
    return o.str();
}

inline std::string ToJson(const Integrity& m) {
    std::string s = SignedPayload(m);
    if (m.hasSignature)
        s += ",\n  \"signature\": {\"alg\": \"ed25519\", \"key\": \"" + ToHex(m.key.data(), m.key.size()) +
             "\", \"sig\": \"" + ToHex(m.sig.data(), m.sig.size()) + "\"}";
    return s + "\n}\n";
}

namespace detail {
template <std::size_t N> bool HexArray(const og::json::Value* v, std::array<std::uint8_t, N>& out) {
    std::vector<std::uint8_t> b;
    if (!v || v->kind != og::json::Value::String || v->str.size() != N * 2 || !FromHex(v->str, b) || b.size() != N) return false;
    std::copy(b.begin(), b.end(), out.begin());
    return true;
}
inline bool IsManifestPath(const std::string& rel) { return og::Lower(rel) == og::Lower(kIntegrityManifest); }
} // namespace detail

// Parse + validate (untrusted input). False, with a reason, for anything off.
inline bool ParseIntegrity(const std::string& text, Integrity& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    if (text.size() > og::kMaxManifestBytes) return fail("manifest too large");
    og::json::Value root;
    if (!og::json::Reader(text).Parse(root) || root.kind != og::json::Value::Object) return fail("manifest isn't valid JSON");
    auto str = [&](const char* k, std::string& s) {
        const og::json::Value* v = root.get(k);
        if (!v || v->kind != og::json::Value::String || v->str.size() > 200) return false;
        s = v->str;
        return true;
    };
    Integrity m;
    std::string format;
    if (!str("format", format) || format != kIntegrityFormat) return fail("not an integrity manifest (" + std::string(kIntegrityFormat) + ")");
    if (!str("game", m.game) || !str("version", m.version)) return fail("manifest has no game or version");
    if (const og::json::Value* a = root.get("anticheat")) {
        const std::string v = a->kind == og::json::Value::String ? a->str : std::string();
        if (v != "off" && v != "report" && v != "enforce") return fail("bad anticheat mode");
        m.mode = ParseMode(v);
    }
    const og::json::Value* files = root.get("files");
    if (!files || files->kind != og::json::Value::Array || files->arr.empty()) return fail("manifest lists no files");
    if (files->arr.size() > og::kMaxFiles) return fail("manifest lists too many files");
    std::set<std::string> seen;
    for (const og::json::Value& fv : files->arr) {
        if (fv.kind != og::json::Value::Object) return fail("bad file entry");
        og::File f;
        const og::json::Value* p = fv.get("path");
        const og::json::Value* h = fv.get("sha256");
        if (!p || p->kind != og::json::Value::String) return fail("file entry without a path");
        f.path = p->str;
        std::string why;
        if (!og::SafeRelPath(f.path, &why)) return fail("unsafe path \"" + og::CleanText(f.path, 80, false) + "\": " + why);
        if (detail::IsManifestPath(f.path)) return fail("the manifest lists itself");
        if (!og::ParseSize(fv.get("size"), f.size) || f.size > og::kMaxFileBytes) return fail("bad size for " + f.path);
        if (!h || h->kind != og::json::Value::String || h->str.size() != 64 ||
            h->str.find_first_not_of("0123456789abcdef") != std::string::npos)
            return fail("bad sha256 for " + f.path);
        f.sha256 = h->str;
        if (!seen.insert(og::Lower(f.path)).second) return fail("duplicate path " + f.path);
        m.files.push_back(std::move(f));
    }
    if (const og::json::Value* s = root.get("signature"); s && s->kind != og::json::Value::Null) {
        const og::json::Value* alg = s->kind == og::json::Value::Object ? s->get("alg") : nullptr;
        if (!alg || alg->kind != og::json::Value::String || alg->str != "ed25519") return fail("unknown signature type");
        if (!detail::HexArray(s->get("key"), m.key) || !detail::HexArray(s->get("sig"), m.sig)) return fail("malformed signature");
        m.hasSignature = true;
    }
    out = std::move(m);
    return true;
}

// The signature is valid for its embedded key (whether that key is trusted is the caller's call).
inline bool CheckSignature(const Integrity& m) {
    return m.hasSignature && ed25519::Verify(SignedPayload(m), m.sig, m.key);
}

inline void SignIntegrity(Integrity& m, const ed25519::Seed& seed) {
    m.key = ed25519::PublicKeyFromSeed(seed);
    m.sig = ed25519::Sign(SignedPayload(m), seed);
    m.hasSignature = true;
}

// Every file under root (the shipped game) except hidden files / folders (the
// launcher's .okayofficial, .git, ...), links and the manifest itself, sorted.
inline bool ScanForManifest(const fs::path& root, std::vector<og::File>& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return fail("the build folder doesn't exist");
    std::vector<og::File> files;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return fail("couldn't read the build folder");
    for (; it != end; it.increment(ec)) {
        if (ec) return fail("couldn't read every file in the build folder");
        const fs::path& p = it->path();
        if (p.filename().string().rfind('.', 0) == 0) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(ec) || !it->is_regular_file(ec)) continue;
        og::File f;
#if defined(_WIN32)
        f.path = p.lexically_relative(root).generic_u8string();
#else
        f.path = p.lexically_relative(root).generic_string();
#endif
        if (detail::IsManifestPath(f.path)) continue;
        std::string why;
        if (!og::SafeRelPath(f.path, &why)) return fail("\"" + og::CleanText(f.path, 80, false) + "\": " + why);
        f.size = (std::uint64_t)it->file_size(ec);
        if (ec) return fail("couldn't read " + f.path);
        f.sha256 = og::FileSha256(p);
        if (f.sha256.empty()) return fail("couldn't read " + f.path);
        files.push_back(std::move(f));
        if (files.size() > og::kMaxFiles) return fail("too many files");
    }
    std::sort(files.begin(), files.end(), [](const og::File& a, const og::File& b) { return a.path < b.path; });
    out = std::move(files);
    return true;
}

// ---- Keys ------------------------------------------------------------------
using EnvFn = std::function<const char*(const char*)>;

// The editor's signing key. `present` false + empty `error` = none configured
// (the manifest is written unsigned). Error text never contains key material.
struct SigningKey {
    bool present = false;
    ed25519::Seed seed{};
    std::string source;   // which variable it came from
    std::string error;
    void Clear() { Wipe(seed.data(), seed.size()); present = false; }
};

// A private key: 64 hex digits (the 32-byte seed, as docs/anticheat.md makes it
// with openssl), or 128 (libsodium's secret key: seed || public key; the public
// half has to match).
inline bool ParseSeed(const std::string& text, ed25519::Seed& out, std::string* err = nullptr) {
    auto fail = [&](const char* w) { if (err) *err = w; return false; };
    std::vector<std::uint8_t> b;
    if (!FromHex(text, b) || (b.size() != 32 && b.size() != 64)) {
        Wipe(b.data(), b.size());
        return fail("isn't an ed25519 private key (64 or 128 hex digits)");
    }
    ed25519::Seed s{};
    std::copy(b.begin(), b.begin() + 32, s.begin());
    if (b.size() == 64) {
        const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(s);
        if (!std::equal(pk.begin(), pk.end(), b.begin() + 32)) {
            Wipe(b.data(), b.size()); Wipe(s.data(), s.size());
            return fail("is a 64-byte secret key whose public half doesn't match");
        }
    }
    Wipe(b.data(), b.size());
    out = s;
    Wipe(s.data(), s.size());
    return true;
}

inline SigningKey LoadSigningKey(const EnvFn& envIn = nullptr) {
    const EnvFn env = envIn ? envIn : EnvFn([](const char* k) -> const char* { return std::getenv(k); });
    SigningKey k;
    std::string text, why;
    if (const char* file = env("OKAY_AC_SIGNING_KEY_FILE"); file && *file) {
        k.source = "OKAY_AC_SIGNING_KEY_FILE";
        if (!og::ReadText(fs::u8path(file), text, 4096)) { k.error = "couldn't read the signing key file " + std::string(file); return k; }
    } else if (const char* inl = env("OKAY_AC_SIGNING_KEY"); inl && *inl) {
        k.source = "OKAY_AC_SIGNING_KEY";
        text = inl;
    } else {
        return k;   // none set up: unsigned
    }
    if (ParseSeed(text, k.seed, &why)) k.present = true;
    else k.error = "the key in " + k.source + " " + why;
    Wipe(&text[0], text.size());
    return k;
}

// Public keys: exactly 64 hex digits each, separated by commas / spaces /
// semicolons. False if any isn't one, or is a key no signature should verify
// under (non-canonical or small-order, e.g. the identity point).
inline bool ParseKeyList(const std::string& text, std::vector<ed25519::PublicKey>& out) {
    std::vector<ed25519::PublicKey> keys;
    std::string tok;
    auto flush = [&]() {
        if (tok.empty()) return true;
        std::vector<std::uint8_t> b;
        if (tok.size() != 64 || !FromHex(tok, b) || b.size() != 32) return false;
        ed25519::PublicKey k{};
        std::copy(b.begin(), b.end(), k.begin());
        if (ed25519::portable::IsWeakKey(k)) return false;
        keys.push_back(k);
        tok.clear();
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!flush()) return false; }
        else tok += c;
    }
    if (!flush()) return false;
    out = std::move(keys);
    return true;
}

inline std::string BuiltInTrustedKeys() { return OKAY_AC_TRUSTED_KEYS; }

// The built-in list, read exactly as cmake/OkayAntiCheatKeys.cmake allows it:
// keys separated by single commas, nothing else, no empty entries.
inline bool ParseBuiltInKeys(const std::string& text, std::vector<ed25519::PublicKey>& out) {
    if (text.empty() || text.find_first_of(" \t\r\n;") != std::string::npos) return false;
    if (text.front() == ',' || text.back() == ',' || text.find(",,") != std::string::npos) return false;
    return ParseKeyList(text, out) && !out.empty();
}

// The keys a manifest may be signed with, and whether a trusted signature is
// required at all. `required` with no `keys` (a malformed built-in list) means
// nothing verifies: it fails closed.
struct TrustedKeySet {
    std::vector<ed25519::PublicKey> keys;
    bool required = false;   // a key list was built in: unsigned / other-key manifests fail
    std::string error;       // why the built-in list didn't parse ("" = fine)
    TrustedKeySet() = default;
    TrustedKeySet(std::vector<ed25519::PublicKey> k) : keys(std::move(k)), required(!keys.empty()) {}
    TrustedKeySet(std::initializer_list<ed25519::PublicKey> k) : keys(k), required(keys.size() != 0) {}
};

// Keys built in with -DOKAY_AC_TRUSTED_KEYS are the only keys whenever that
// string isn't blank, even if it fails to parse (then no key is trusted and
// every manifest fails). Only a build with nothing built in (a dev build) takes
// OKAY_AC_TRUSTED_KEY from the environment, so a release can never be pointed
// at a cheater's key that way.
inline TrustedKeySet TrustedKeys(const EnvFn& envIn = nullptr, const std::string& builtIn = BuiltInTrustedKeys()) {
    TrustedKeySet t;
    if (!og::Trim(builtIn).empty()) {
        t.required = true;
        if (!ParseBuiltInKeys(builtIn, t.keys)) {
            t.keys.clear();
            t.error = "the trusted keys built into this program are malformed";
        }
        return t;
    }
    const EnvFn env = envIn ? envIn : EnvFn([](const char* k) -> const char* { return std::getenv(k); });
    if (const char* e = env("OKAY_AC_TRUSTED_KEY"); e && !og::Trim(e).empty()) {
        t.required = true;   // (a malformed dev key fails closed too)
        if (!ParseKeyList(e, t.keys) || t.keys.empty()) { t.keys.clear(); t.error = "OKAY_AC_TRUSTED_KEY is malformed"; }
    }
    return t;
}

// The Build Settings > Output > Anti-cheat tooltip (the editor shows it as is).
constexpr const char* kBuildSettingsTooltip =
    "The game checks its files against Data/manifest.json and (Windows) watches for debuggers, injected DLLs and speedhacks.\n"
    "Report logs what it finds; Enforce also refuses to run.\n"
    "Start with Report: the DLL allowlist also flags harmless tools that load into games\n"
    "(antivirus, OBS, RTSS / MSI Afterburner, IME and input-method DLLs, overlays).\n"
    "Client-side checks can be bypassed (docs/anticheat.md).";

// ---- Writing (File > Build Game) ---------------------------------------------
struct BuildManifestResult {
    bool ok = false;
    bool isSigned = false;
    std::size_t files = 0;
    std::string message;   // what happened, for the build log ("" when nothing worth saying)
};

// `mode` goes into the signed payload, so a build with trusted keys can't have
// it lowered by editing game.okayconfig.
inline BuildManifestResult WriteBuildManifest(const fs::path& root, const std::string& game, const std::string& version,
                                              const SigningKey& key, Mode mode = Mode::Off) {
    BuildManifestResult r;
    Integrity m;
    m.game = og::CleanText(game, 200, false);
    m.version = og::CleanText(version, 200, false);
    m.mode = mode;
    std::string err;
    if (!ScanForManifest(root, m.files, &err)) { r.message = "Couldn't write the integrity manifest: " + err + "."; return r; }
    if (m.files.empty()) { r.message = "Couldn't write the integrity manifest: the build has no files."; return r; }
    if (key.present) SignIntegrity(m, key.seed);
    std::error_code ec;
    const fs::path out = root / fs::u8path(kIntegrityManifest);
    fs::create_directories(out.parent_path(), ec);
    const fs::path tmp = out.string() + ".new";
    if (!og::detail::WriteFileSynced(tmp, ToJson(m)) || !og::MoveInto(tmp, out)) {
        fs::remove(tmp, ec);
        r.message = "Couldn't write " + std::string(kIntegrityManifest) + ".";
        return r;
    }
    r.ok = true;
    r.isSigned = m.hasSignature;
    r.files = m.files.size();
    if (r.isSigned) r.message = "integrity manifest signed (key " + KeyId(m.key) + ")";
    else if (!key.error.empty()) r.message = "integrity manifest NOT signed: " + key.error;
    return r;
}

// ---- Verifying ---------------------------------------------------------------
struct VerifyResult {
    bool ran = false;             // false: anti-cheat is off, nothing was looked at
    bool ok = true;
    bool signatureValid = false;
    bool trustedSigner = false;
    int filesChecked = 0;
    std::vector<std::string> problems;   // each one fails the check
    std::vector<std::string> notes;      // worth logging, not a failure
    std::string Summary() const {
        if (!ran) return "anti-cheat is off";
        if (ok) return "integrity OK (" + std::to_string(filesChecked) + " files" +
                       (trustedSigner ? ", trusted signature" : signatureValid ? ", signed" : ", unsigned") + ")";
        std::string s = "integrity check failed: " + (problems.empty() ? std::string("unknown") : problems.front());
        if (problems.size() > 1) s += " (+" + std::to_string(problems.size() - 1) + " more)";
        return s;
    }
};

inline bool LoadIntegrity(const fs::path& root, Integrity& out, std::string* err = nullptr) {
    std::string text;
    if (!og::ReadText(root / fs::u8path(kIntegrityManifest), text)) { if (err) *err = "no integrity manifest (" + std::string(kIntegrityManifest) + ")"; return false; }
    return ParseIntegrity(text, out, err);
}

namespace detail {
inline bool IsCodeFile(const std::string& rel) {
    const std::string e = og::Lower(fs::path(rel).extension().string());
    return e == ".exe" || e == ".dll" || e == ".so" || e == ".dylib" || e == ".asi" || e == ".sys" || e == ".com";
}
} // namespace detail

namespace detail {
// Every program file (by name) and every folder link under root, hidden or not,
// links included: what VerifyInstall holds against the manifest. Only the
// launcher's own top-level .okayofficial folder is skipped.
struct PresentEntry { std::string rel; bool link = false, dir = false; };
inline void ScanPresent(const fs::path& root, std::vector<PresentEntry>& out) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& p = it->path();
#if defined(_WIN32)
        const std::string rel = p.lexically_relative(root).generic_u8string();
#else
        const std::string rel = p.lexically_relative(root).generic_string();
#endif
        if (it.depth() == 0 && og::Lower(p.filename().string()) == og::kMetaDir) { it.disable_recursion_pending(); continue; }
        const bool link = it->is_symlink(ec);
        if (link) {
            // A link to a folder (it isn't followed, so its contents can't be checked).
            if (fs::is_directory(p, ec)) out.push_back({rel, true, true});
            else if (IsCodeFile(rel)) out.push_back({rel, true, false});
            continue;
        }
        if (it->is_regular_file(ec) && IsCodeFile(rel)) out.push_back({rel, false, false});
    }
}
} // namespace detail

// Check the install at root against its manifest. With `trusted.required`, the
// manifest must carry a valid signature by one of `trusted.keys` (none = it
// fails: a malformed built-in key list fails closed). Without, a signature is
// still checked against the key it names, but proves nothing about who made it
// (a note says so). `loaded`, when given, gets the parsed manifest (the runtime
// guard's module allowlist and DecideStart use it).
inline VerifyResult VerifyInstall(const fs::path& root, const TrustedKeySet& trusted, Integrity* loaded = nullptr) {
    VerifyResult r;
    r.ran = true;
    auto problem = [&](const std::string& p) {
        r.ok = false;
        if (r.problems.size() < 20) r.problems.push_back(p);
        else if (r.problems.size() == 20) r.problems.push_back("...and more");
    };
    if (trusted.required && trusted.keys.empty())
        problem(trusted.error.empty() ? std::string("no usable trusted key in this program") : trusted.error);
    Integrity m;
    std::string err;
    if (!LoadIntegrity(root, m, &err)) { problem(err); return r; }

    const auto& keys = trusted.keys;
    if (m.hasSignature) {
        r.signatureValid = CheckSignature(m);
        r.trustedSigner = r.signatureValid && std::find(keys.begin(), keys.end(), m.key) != keys.end();
        if (!r.signatureValid) problem("the manifest's signature doesn't match (the manifest was edited)");
        else if (trusted.required && !r.trustedSigner) problem("the manifest is signed by an untrusted key (" + KeyId(m.key) + ")");
        else if (!trusted.required) r.notes.push_back("signed by key " + KeyId(m.key) + "; no trusted key is built in, so the signer isn't checked");
    } else if (trusted.required) {
        problem("the manifest isn't signed");
    } else {
        r.notes.push_back("the manifest is unsigned (file hashes checked only)");
    }

    std::set<std::string> listed;
    for (const og::File& f : m.files) {
        listed.insert(og::Lower(f.path));
        const fs::path p = og::ResolveInside(root, f.path);
        std::error_code ec;
        if (p.empty()) { problem(f.path + " points outside the game folder"); continue; }
        if (!fs::is_regular_file(p, ec)) problem(f.path + " is missing");
        else if (!og::Verify(p, f)) problem(f.path + " was modified");
        ++r.filesChecked;
    }
    // A program file the build didn't ship (a proxy DLL beside the exe, a
    // swapped-in .asi plugin, hidden-named or a link) is a problem, and so is a
    // link to a folder; other extra files (saves, logs) aren't.
    std::vector<detail::PresentEntry> present;
    detail::ScanPresent(root, present);
    for (const detail::PresentEntry& e : present) {
        if (e.dir) problem("unexpected folder link " + e.rel);
        else if (e.link) problem("unexpected program file link " + e.rel);
        else if (!listed.count(og::Lower(e.rel))) problem("unexpected program file " + e.rel);
    }
    if (loaded) *loaded = std::move(m);
    return r;
}

struct LaunchDecision {
    bool allow = true;
    VerifyResult result;
    std::string message;   // for the player / the log; "" when there's nothing to say
};

namespace detail {
inline std::string RefusalText(const VerifyResult& r) {
    // Short lines: an X11 message box doesn't wrap.
    return "This game's files don't match the ones it shipped with:\n" +
           (r.problems.empty() ? std::string("unknown problem") : r.problems.front()) +
           (r.problems.size() > 1 ? " (+" + std::to_string(r.problems.size() - 1) + " more)" : std::string()) +
           "\n\nReinstall or repair the game, then try again.";
}
} // namespace detail

// What the launcher does before running a game (and the player, in a build
// without trusted keys: see DecideStart).
inline LaunchDecision CheckLaunch(const fs::path& root, Mode mode, const TrustedKeySet& trusted, Integrity* loaded = nullptr) {
    LaunchDecision d;
    if (mode == Mode::Off) return d;
    d.result = VerifyInstall(root, trusted, loaded);
    if (d.result.ok) return d;
    if (mode == Mode::Enforce) {
        d.allow = false;
        d.message = detail::RefusalText(d.result);
    } else {
        d.message = "[anti-cheat] " + d.result.Summary();
    }
    return d;
}

// What the player does at start.
//  * No trusted keys built in: the mode is the stricter of game.okayconfig's
//    anticheat= and OKAY_ANTICHEAT; off checks nothing (anyone who can edit the
//    config can turn it off: that's the unsigned case, see docs/anticheat.md).
//  * Trusted keys built in (a player built for your own signed games): the game
//    must have a valid manifest signed by a trusted key, or it doesn't start, and
//    the mode comes from that signed manifest; the config and the environment can
//    only make it stricter. A scene given on the command line must be one of the
//    signed files.
struct StartDecision {
    bool allow = true;
    Mode mode = Mode::Off;
    VerifyResult result;
    std::string message;
    Integrity manifest;      // what was verified (empty files = none)
};

inline StartDecision DecideStart(const fs::path& root, Mode configMode, Mode envMode, const TrustedKeySet& trusted,
                                 const std::string& sceneArg = std::string()) {
    StartDecision s;
    if (!trusted.required) {
        s.mode = Stricter(configMode, envMode);
        const LaunchDecision d = CheckLaunch(root, s.mode, trusted, &s.manifest);
        s.allow = d.allow; s.result = d.result; s.message = d.message;
        return s;
    }
    s.result = VerifyInstall(root, trusted, &s.manifest);
    if (!s.result.ok) {
        s.allow = false;
        s.message = detail::RefusalText(s.result);
        return s;
    }
    s.mode = Stricter(s.manifest.mode, Stricter(configMode, envMode));
    if (!sceneArg.empty()) {
        std::error_code ec;
        const fs::path base = fs::weakly_canonical(root, ec), scene = fs::weakly_canonical(fs::u8path(sceneArg), ec);
#if defined(_WIN32)
        const std::string rel = scene.lexically_relative(base).generic_u8string();
#else
        const std::string rel = scene.lexically_relative(base).generic_string();
#endif
        bool listed = false;
        for (const og::File& f : s.manifest.files) listed = listed || og::Lower(f.path) == og::Lower(rel);
        if (rel.empty() || rel.rfind("..", 0) == 0 || !listed) {
            s.allow = false;
            s.message = "This game only runs its own signed files.";
        }
    }
    return s;
}

} // namespace okayac
