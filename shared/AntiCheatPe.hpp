#pragma once
// Anti-cheat: code integrity (opt-in; docs/anticheat.md, "Hardening: code and import integrity").
//
// A build-time tool (tools/okay_ac_baseline.cpp) hashes the code sections of the
// game's own exe / dll files and writes Data/codebaseline.json, signed with the
// same ed25519 key as the integrity manifest. At run time the player hashes the
// same sections as they sit in memory and compares. A patched instruction (an
// inline hook in the game's own code, a NOP-ed check, a changed constant) changes
// the hash.
//
// Relocations: when Windows loads an image at a different base than it was linked
// for, it rewrites the relocated fields in the code. Those fields are zeroed
// before hashing, on disk and in memory alike, so the same hash comes out
// whatever the load address (ASLR).
//
// This file is portable: it parses PE images from byte buffers, so everything
// but "where is my own image" is built and tested on Linux with synthetic images.
// The Windows glue is in AntiCheatHardening.hpp.
//
// What it is not: a cheater who patches the exe can patch this check too, or
// hook the hash function, or restore the bytes just before it looks. It catches
// off-the-shelf trainers and casual patches, and a mismatch is a signal for the
// backend, not proof.
#include <cstring>

#include "AntiCheatChecks.hpp"   // Finding, Sha256, ToHex, signing

namespace okayac {
namespace pe {

constexpr std::uint32_t kScnExec = 0x20000000u, kScnWrite = 0x80000000u;

struct Section {
    std::string name;
    std::uint32_t rva = 0, vsize = 0, rawOff = 0, rawSize = 0, chars = 0;
    // The bytes that exist both on disk and in memory (what is hashed).
    std::uint32_t Len() const { return rawSize == 0 ? vsize : (vsize == 0 ? rawSize : std::min(vsize, rawSize)); }
    bool IsCode() const { return (chars & kScnExec) != 0 && (chars & kScnWrite) == 0; }
};

struct Image {
    bool is64 = false;
    std::uint64_t imageBase = 0;
    std::uint32_t sizeOfImage = 0, sizeOfHeaders = 0;
    std::uint32_t importRva = 0, importSize = 0, relocRva = 0, relocSize = 0;
    std::vector<Section> sections;
};

enum class Layout { File, Mapped };   // a file read from disk, or an image as the loader maps it

namespace detail {
inline bool Rd(const std::uint8_t* d, std::size_t n, std::size_t off, std::size_t len, std::uint64_t& v) {
    if (len > 8 || off > n || len > n - off) return false;
    v = 0;
    for (std::size_t i = 0; i < len; ++i) v |= (std::uint64_t)d[off + i] << (8 * i);
    return true;
}
} // namespace detail

inline bool Parse(const std::uint8_t* d, std::size_t n, Image& out, std::string* err = nullptr) {
    auto fail = [&](const char* w) { if (err) *err = w; return false; };
    std::uint64_t v = 0;
    if (!detail::Rd(d, n, 0, 2, v) || v != 0x5A4D) return fail("not a PE file (no MZ)");
    if (!detail::Rd(d, n, 0x3C, 4, v) || v > n) return fail("bad PE offset");
    const std::size_t pe = (std::size_t)v;
    if (!detail::Rd(d, n, pe, 4, v) || v != 0x00004550) return fail("not a PE file (no PE signature)");
    Image im;
    std::uint64_t nsec = 0, optSize = 0;
    if (!detail::Rd(d, n, pe + 6, 2, nsec) || !detail::Rd(d, n, pe + 20, 2, optSize)) return fail("truncated header");
    if (nsec == 0 || nsec > 96) return fail("bad section count");
    const std::size_t opt = pe + 24;
    std::uint64_t magic = 0, ndirs = 0, t = 0;
    if (!detail::Rd(d, n, opt, 2, magic)) return fail("truncated header");
    std::size_t dirs = 0;
    if (magic == 0x20B) {
        im.is64 = true;
        if (!detail::Rd(d, n, opt + 24, 8, im.imageBase) || !detail::Rd(d, n, opt + 108, 4, ndirs)) return fail("truncated header");
        dirs = opt + 112;
    } else if (magic == 0x10B) {
        if (!detail::Rd(d, n, opt + 28, 4, im.imageBase) || !detail::Rd(d, n, opt + 92, 4, ndirs)) return fail("truncated header");
        dirs = opt + 96;
    } else {
        return fail("unknown optional header");
    }
    if (!detail::Rd(d, n, opt + 56, 4, t)) return fail("truncated header");
    im.sizeOfImage = (std::uint32_t)t;
    if (!detail::Rd(d, n, opt + 60, 4, t)) return fail("truncated header");
    im.sizeOfHeaders = (std::uint32_t)t;
    auto dir = [&](int idx, std::uint32_t& rva, std::uint32_t& size) {
        std::uint64_t a = 0, b = 0;
        if ((std::uint64_t)idx < ndirs && detail::Rd(d, n, dirs + idx * 8, 4, a) && detail::Rd(d, n, dirs + idx * 8 + 4, 4, b)) {
            rva = (std::uint32_t)a;
            size = (std::uint32_t)b;
        }
    };
    dir(1, im.importRva, im.importSize);
    dir(5, im.relocRva, im.relocSize);
    const std::size_t tab = opt + (std::size_t)optSize;
    for (std::uint64_t i = 0; i < nsec; ++i) {
        const std::size_t s = tab + (std::size_t)i * 40;
        Section sec;
        char nm[9] = {};
        if (s + 40 > n) return fail("truncated section table");
        std::memcpy(nm, d + s, 8);
        sec.name = nm;
        std::uint64_t a = 0;
        detail::Rd(d, n, s + 8, 4, a);  sec.vsize = (std::uint32_t)a;
        detail::Rd(d, n, s + 12, 4, a); sec.rva = (std::uint32_t)a;
        detail::Rd(d, n, s + 16, 4, a); sec.rawSize = (std::uint32_t)a;
        detail::Rd(d, n, s + 20, 4, a); sec.rawOff = (std::uint32_t)a;
        detail::Rd(d, n, s + 36, 4, a); sec.chars = (std::uint32_t)a;
        im.sections.push_back(sec);
    }
    out = std::move(im);
    return true;
}

// A section's bytes inside the buffer, or nullptr if they don't fit.
inline const std::uint8_t* SectionData(const std::uint8_t* d, std::size_t n, const Section& s, Layout l) {
    const std::uint64_t off = l == Layout::File ? s.rawOff : s.rva;
    if (off > n || (std::uint64_t)s.Len() > n - off) return nullptr;
    return d + off;
}

// A byte range of the image given by RVA (for the import / reloc tables).
inline const std::uint8_t* RvaData(const std::uint8_t* d, std::size_t n, const Image& im, Layout l, std::uint32_t rva,
                                   std::uint32_t len) {
    if (l == Layout::Mapped) return (rva <= n && len <= n - rva) ? d + rva : nullptr;
    if (rva < im.sizeOfHeaders) return (rva <= n && len <= n - rva) ? d + rva : nullptr;
    for (const Section& s : im.sections) {
        const std::uint32_t span = s.rawSize;
        if (rva >= s.rva && (std::uint64_t)rva - s.rva + len <= span) {
            const std::uint64_t off = (std::uint64_t)s.rawOff + (rva - s.rva);
            return off <= n && len <= n - off ? d + off : nullptr;
        }
    }
    return nullptr;
}

struct Mask { std::uint32_t off, len; };   // offsets inside one section's hashed bytes

// The relocated fields that fall inside section `s`, sorted and non-overlapping.
inline std::vector<Mask> RelocMasks(const std::uint8_t* d, std::size_t n, const Image& im, Layout l, const Section& s) {
    std::vector<Mask> out;
    if (im.relocRva == 0 || im.relocSize < 8) return out;
    const std::uint8_t* r = RvaData(d, n, im, l, im.relocRva, im.relocSize);
    if (!r) return out;
    std::size_t p = 0;
    const std::uint32_t secLen = s.Len();
    while (p + 8 <= im.relocSize) {
        std::uint64_t page = 0, size = 0;
        detail::Rd(r, im.relocSize, p, 4, page);
        detail::Rd(r, im.relocSize, p + 4, 4, size);
        if (size < 8 || p + size > im.relocSize) break;
        for (std::size_t e = p + 8; e + 2 <= p + size; e += 2) {
            std::uint64_t ent = 0;
            detail::Rd(r, im.relocSize, e, 2, ent);
            const unsigned type = (unsigned)(ent >> 12);
            const std::uint32_t rva = (std::uint32_t)page + (std::uint32_t)(ent & 0xFFF);
            std::uint32_t len = type == 3 ? 4 : type == 10 ? 8 : type == 1 || type == 2 ? 2 : 0;
            if (!len || rva < s.rva) continue;
            const std::uint32_t off = rva - s.rva;
            if (off >= secLen) continue;
            if ((std::uint64_t)off + len > secLen) len = secLen - off;
            out.push_back({off, len});
        }
        p += (std::size_t)size;
    }
    std::sort(out.begin(), out.end(), [](const Mask& a, const Mask& b) { return a.off < b.off; });
    // Merge overlaps so the hasher can walk the list once.
    std::vector<Mask> merged;
    for (const Mask& m : out) {
        if (!merged.empty() && m.off <= merged.back().off + merged.back().len) {
            const std::uint32_t end = std::max(merged.back().off + merged.back().len, m.off + m.len);
            merged.back().len = end - merged.back().off;
        } else {
            merged.push_back(m);
        }
    }
    return merged;
}

// SHA-256 of one section's bytes with the relocated fields zeroed, in steps so a
// frame never pays for a whole multi-megabyte section. `data` must stay valid.
class SectionHasher {
public:
    SectionHasher() = default;
    SectionHasher(const std::uint8_t* data, std::uint32_t len, std::vector<Mask> masks)
        : data_(data), len_(len), masks_(std::move(masks)) {}

    // Hash up to `budget` more bytes. True once the whole section has been hashed.
    bool Step(std::size_t budget) {
        static const std::uint8_t zeros[256] = {};
        while (pos_ < len_ && budget > 0) {
            while (mi_ < masks_.size() && masks_[mi_].off + masks_[mi_].len <= pos_) ++mi_;
            std::size_t n;
            const std::uint8_t* src;
            if (mi_ < masks_.size() && masks_[mi_].off <= pos_) {   // inside a relocated field: zeros
                n = std::min<std::size_t>(masks_[mi_].off + masks_[mi_].len - pos_, sizeof(zeros));
                src = zeros;
            } else {
                n = len_ - pos_;
                if (mi_ < masks_.size()) n = std::min<std::size_t>(n, masks_[mi_].off - pos_);
                src = data_ + pos_;
            }
            n = std::min(n, budget);
            h_.Update(src, n);
            pos_ += (std::uint32_t)n;
            budget -= n;
        }
        return pos_ >= len_;
    }
    bool Done() const { return pos_ >= len_; }
    std::uint32_t Position() const { return pos_; }
    std::uint32_t Length() const { return len_; }
    std::string Hex() { const auto d = h_.Digest(); return ToHex(d.data(), d.size()); }   // only once Done()

private:
    const std::uint8_t* data_ = nullptr;
    std::uint32_t len_ = 0, pos_ = 0;
    std::vector<Mask> masks_;
    std::size_t mi_ = 0;
    okay::account::detail::Sha256 h_;
};

// ---- The baseline ---------------------------------------------------------------
struct SectionDigest {
    std::string name;
    std::uint32_t size = 0;
    std::string sha256;
};
struct ModuleBaseline {
    std::string name;   // file name, compared lower-case ("game.exe")
    std::vector<SectionDigest> sections;
};

// Every code section of an image (file or mapped) hashed in one go.
inline bool HashCodeSections(const std::uint8_t* d, std::size_t n, Layout l, std::vector<SectionDigest>& out, std::string* err = nullptr) {
    Image im;
    if (!Parse(d, n, im, err)) return false;
    out.clear();
    for (const Section& s : im.sections) {
        if (!s.IsCode() || s.Len() == 0) continue;
        const std::uint8_t* p = SectionData(d, n, s, l);
        if (!p) { if (err) *err = "section " + s.name + " runs past the end of the image"; return false; }
        SectionHasher h(p, s.Len(), RelocMasks(d, n, im, l, s));
        h.Step((std::size_t)-1);
        out.push_back({s.name, s.Len(), h.Hex()});
    }
    if (out.empty()) { if (err) *err = "the image has no code section"; return false; }
    return true;
}

constexpr const char* kCodeBaselineFormat = "okayspace-code/1";
constexpr const char* kCodeBaselineFile = "Data/codebaseline.json";

struct CodeBaseline {
    std::string build;   // the build id (also what the heartbeat attestation names)
    std::vector<ModuleBaseline> modules;
    bool hasSignature = false;
    ed25519::PublicKey key{};
    ed25519::Signature sig{};
};

inline std::string SignedPayload(const CodeBaseline& b) {
    using og::json::Escape;
    std::ostringstream o;
    o << "{\n  \"format\": \"" << kCodeBaselineFormat << "\",\n  \"build\": \"" << Escape(b.build) << "\",\n  \"modules\": [";
    for (std::size_t i = 0; i < b.modules.size(); ++i) {
        o << (i ? ",\n" : "\n") << "    {\"name\": \"" << Escape(b.modules[i].name) << "\", \"sections\": [";
        for (std::size_t j = 0; j < b.modules[i].sections.size(); ++j) {
            const SectionDigest& s = b.modules[i].sections[j];
            o << (j ? ", " : "") << "{\"name\": \"" << Escape(s.name) << "\", \"size\": " << s.size << ", \"sha256\": \"" << s.sha256 << "\"}";
        }
        o << "]}";
    }
    o << "\n  ]";
    return o.str();
}
inline std::string ToJson(const CodeBaseline& b) {
    std::string s = SignedPayload(b);
    if (b.hasSignature)
        s += ",\n  \"signature\": {\"alg\": \"ed25519\", \"key\": \"" + ToHex(b.key.data(), b.key.size()) + "\", \"sig\": \"" +
             ToHex(b.sig.data(), b.sig.size()) + "\"}";
    return s + "\n}\n";
}
inline bool CheckSignature(const CodeBaseline& b) { return b.hasSignature && ed25519::Verify(SignedPayload(b), b.sig, b.key); }
inline void Sign(CodeBaseline& b, const ed25519::Seed& seed) {
    b.key = ed25519::PublicKeyFromSeed(seed);
    b.sig = ed25519::Sign(SignedPayload(b), seed);
    b.hasSignature = true;
}

inline bool ParseCodeBaseline(const std::string& text, CodeBaseline& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    if (text.size() > og::kMaxManifestBytes) return fail("baseline too large");
    og::json::Value root;
    if (!og::json::Reader(text).Parse(root) || root.kind != og::json::Value::Object) return fail("baseline isn't valid JSON");
    auto str = [](const og::json::Value* v, std::size_t cap, std::string& s) {
        if (!v || v->kind != og::json::Value::String || v->str.size() > cap) return false;
        s = v->str;
        return true;
    };
    CodeBaseline b;
    std::string format;
    if (!str(root.get("format"), 64, format) || format != kCodeBaselineFormat) return fail(std::string("not a code baseline (") + kCodeBaselineFormat + ")");
    if (!str(root.get("build"), 200, b.build)) return fail("baseline has no build id");
    const og::json::Value* mods = root.get("modules");
    if (!mods || mods->kind != og::json::Value::Array || mods->arr.empty() || mods->arr.size() > 64) return fail("baseline lists no modules");
    for (const og::json::Value& mv : mods->arr) {
        ModuleBaseline m;
        if (mv.kind != og::json::Value::Object || !str(mv.get("name"), 200, m.name) || m.name.empty() ||
            m.name.find_first_of("/\\") != std::string::npos)
            return fail("bad module entry");
        m.name = og::Lower(m.name);
        const og::json::Value* secs = mv.get("sections");
        if (!secs || secs->kind != og::json::Value::Array || secs->arr.empty() || secs->arr.size() > 96) return fail("module without sections");
        for (const og::json::Value& sv : secs->arr) {
            SectionDigest s;
            std::uint64_t size = 0;
            if (sv.kind != og::json::Value::Object || !str(sv.get("name"), 8, s.name) || !og::ParseSize(sv.get("size"), size) ||
                size > 0xFFFFFFFFull || !str(sv.get("sha256"), 64, s.sha256) || s.sha256.size() != 64 ||
                s.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
                return fail("bad section entry");
            s.size = (std::uint32_t)size;
            m.sections.push_back(s);
        }
        b.modules.push_back(std::move(m));
    }
    if (const og::json::Value* sig = root.get("signature")) {
        if (sig->kind != og::json::Value::Object) return fail("bad signature");
        const og::json::Value* alg = sig->get("alg");
        if (!alg || alg->kind != og::json::Value::String || alg->str != "ed25519" || !okayac::detail::HexArray(sig->get("key"), b.key) ||
            !okayac::detail::HexArray(sig->get("sig"), b.sig))
            return fail("bad signature");
        b.hasSignature = true;
    }
    out = std::move(b);
    return true;
}

// What the post-build tool does for one file: hash its code sections.
inline bool BaselineForFile(const fs::path& file, ModuleBaseline& out, std::string* err = nullptr) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { if (err) *err = "can't read " + file.string(); return false; }
    std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = ModuleBaseline{};
    out.name = og::Lower(file.filename().string());
    return HashCodeSections(buf.data(), buf.size(), Layout::File, out.sections, err);
}

// A baseline is trusted when its signature verifies under one of the trusted keys
// (same rule as the integrity manifest); an unsigned or untrusted-key baseline is
// only usable where nothing is required, and says so.
struct BaselineTrust { bool usable = false; std::string note; };
inline BaselineTrust CheckBaselineTrust(const CodeBaseline& b, const TrustedKeySet& trusted) {
    BaselineTrust t;
    if (trusted.required) {
        if (!b.hasSignature || !CheckSignature(b)) { t.note = "the code baseline isn't validly signed"; return t; }
        for (const ed25519::PublicKey& k : trusted.keys)
            if (k == b.key) { t.usable = true; return t; }
        t.note = "the code baseline is signed with a key this player doesn't trust";
        return t;
    }
    t.usable = true;
    t.note = b.hasSignature && CheckSignature(b) ? "the code baseline is signed, but this build has no trusted keys: it is telemetry, not proof"
                                                  : "the code baseline is unsigned: it is telemetry, not proof";
    return t;
}

// ---- The scanner -------------------------------------------------------------------
// Walks the module's code sections in memory a slice at a time; each full pass
// compares every section with the baseline and then waits `interval`.
// `image` is the module as the loader mapped it (on Windows: its base address).
class CodeScanner {
public:
    CodeScanner() = default;
    CodeScanner(const std::uint8_t* image, std::size_t imageSize, ModuleBaseline expected)
        : img_(image), size_(imageSize), want_(std::move(expected)) {}

    bool Ready() const { return img_ != nullptr; }
    int Passes() const { return passes_; }

    // Do up to `budget` bytes of hashing. Returns the findings a finished section produced.
    std::vector<Finding> Step(std::size_t budget) {
        std::vector<Finding> out;
        if (!img_) return out;
        if (!parsed_) {
            parsed_ = true;
            std::string err;
            if (!Parse(img_, size_, im_, &err)) { out.push_back({"code", "own image can't be parsed in memory: " + err, 4}); img_ = nullptr; return out; }
            for (std::size_t i = 0; i < im_.sections.size(); ++i)
                if (im_.sections[i].IsCode() && im_.sections[i].Len() > 0) order_.push_back(i);
        }
        const int startPass = passes_;
        while (budget > 0 && !order_.empty() && passes_ == startPass) {   // a call never runs past the end of a pass
            const Section& s = im_.sections[order_[cur_]];
            if (!active_) {
                const std::uint8_t* p = SectionData(img_, size_, s, Layout::Mapped);
                if (!p) { out.push_back({"code", "section " + s.name + " is not readable in memory", 4}); Advance(); continue; }
                hasher_ = SectionHasher(p, s.Len(), RelocMasks(img_, size_, im_, Layout::Mapped, s));
                active_ = true;
            }
            const std::uint32_t before = hasher_.Position();
            const bool done = hasher_.Step(budget);
            const std::size_t used = hasher_.Position() - before;
            budget = used >= budget ? 0 : budget - used;
            if (done) {
                Compare(s, hasher_.Hex(), out);
                active_ = false;
                Advance();
            }
        }
        return out;
    }

    // A whole pass at once (tests, a loading screen).
    std::vector<Finding> ScanAll() {
        std::vector<Finding> all;
        const int start = passes_;
        while (passes_ == start && img_) {
            auto f = Step((std::size_t)-1 / 2);
            all.insert(all.end(), f.begin(), f.end());
            if (order_.empty() && parsed_) break;
        }
        return all;
    }

private:
    void Advance() {
        if (++cur_ >= order_.size()) { cur_ = 0; ++passes_; }
    }
    void Compare(const Section& s, const std::string& got, std::vector<Finding>& out) {
        for (const SectionDigest& w : want_.sections) {
            if (w.name != s.name) continue;
            if (w.size != s.Len())
                out.push_back({"code", "section " + s.name + " of " + want_.name + " has a different size in memory", 6});
            else if (w.sha256 != got)
                out.push_back({"code", "section " + s.name + " of " + want_.name + " was modified in memory (expected " + w.sha256.substr(0, 12) +
                                           ", found " + got.substr(0, 12) + ")", 6});
            return;
        }
        out.push_back({"code", "section " + s.name + " of " + want_.name + " isn't in the baseline", 3});
    }

    const std::uint8_t* img_ = nullptr;
    std::size_t size_ = 0;
    ModuleBaseline want_;
    Image im_;
    bool parsed_ = false, active_ = false;
    std::vector<std::size_t> order_;
    std::size_t cur_ = 0;
    SectionHasher hasher_;
    int passes_ = 0;
};

} // namespace pe
} // namespace okayac
