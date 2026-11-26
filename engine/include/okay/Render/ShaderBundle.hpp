#pragma once
// Pre-compiled shader bundle (pure logic, no Direct3D, no threads). Written from Microsoft's documentation of fxc.exe
// (`/T profile /E entry /Fo file`), the C++ standard, and the FNV-1a hash description (public algorithm).
//
// Why: a Universal Windows Platform app may not call D3DCompile at run time. The Xbox UWP build therefore compiles every
// entry point of the Direct3D 11 HLSL (D3D11Hlsl.hpp) to a .cso file at BUILD time (scripts/xbox-compile-shaders.ps1 with fxc.exe)
// and writes `shaders.manifest` next to them. At run time D3D11Renderer (when OKAY_XBOX_UWP is defined) reads the files through this
// header. The desktop build keeps what D3DCompile made in the same layout (CompileCache, at the end of this file).
//
// Manifest text format (one record per line, '#' starts a comment line):
//   okay-shaders 1
//   source <16 hex digits>        FNV-1a 64 of the HLSL text the .cso files were built from
//   <entry> <profile> <file> <size> <16 hex digits>   FNV-1a 64 of the .cso bytes
// UNVERIFIED on Windows: the .cso files have never been produced here (no fxc.exe on Linux); the logic below is tested on Linux.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace okay { namespace shaderbundle {

// Every entry point D3D11Renderer compiles (8 call sites, 15 entry points) plus the 2 overlay shaders of the Xbox build, in the order Init() asks for them.
// `required` entries make the renderer fail to start when missing; the others only switch a feature off (as on desktop).
struct EntryReq { const char* entry; const char* profile; bool required; };
inline const std::vector<EntryReq>& Entries() {
    static const std::vector<EntryReq> k = {
        {"VSMain", "vs_4_0", true},      {"PSMain", "ps_4_0", true},
        {"VSDepth", "vs_4_0", false},    {"VSSkin", "vs_4_0", false},   {"VSDepthSkin", "vs_4_0", false},
        {"VSQuad", "vs_4_0", false},     {"PSSky", "ps_4_0", false},    {"PSDown", "ps_4_0", false},
        {"PSBlur", "ps_4_0", false},     {"PSUber", "ps_4_0", false},   {"PSEdgeSmooth", "ps_4_0", false},
        {"PSAoDepth", "ps_4_0", false},  {"PSAo", "ps_4_0", false},     {"PSAoMul", "ps_4_0", false},
        {"PSDof", "ps_4_0", false},
        {"VSOverlay", "vs_4_0", false}, {"PSOverlay", "ps_4_0", false},   // the Xbox UI overlay (D3D11OverlayHlsl.hpp); not used by the desktop renderer
    };
    return k;
}

inline std::uint64_t Fnv1a64(const void* data, std::size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = 14695981039346656037ull;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
inline std::string Hex64(std::uint64_t v) {
    char b[17]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b;
}
inline bool ParseHex64(const std::string& s, std::uint64_t& out) {
    if (s.size() != 16) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = (v << 4) | (std::uint64_t)d;
    }
    out = v; return true;
}

// File name of an entry's bytecode. Only letters, digits and '_' are accepted in an entry name (no path tricks).
inline bool ValidEntryName(const std::string& e) {
    if (e.empty() || e.size() > 48) return false;
    for (char c : e) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
inline std::string FileNameFor(const std::string& entry) { return "okay3d_" + entry + ".cso"; }

struct Record { std::string entry, profile, file; std::uint64_t size = 0, hash = 0; };
struct Manifest { std::uint64_t sourceHash = 0; bool hasSource = false; std::vector<Record> records;
    const Record* Find(const std::string& entry) const { for (const Record& r : records) if (r.entry == entry) return &r; return nullptr; } };

inline std::string WriteManifest(const Manifest& m) {
    std::string o = "okay-shaders 1\nsource " + Hex64(m.sourceHash) + "\n";
    for (const Record& r : m.records)
        o += r.entry + " " + r.profile + " " + r.file + " " + std::to_string((unsigned long long)r.size) + " " + Hex64(r.hash) + "\n";
    return o;
}

// Parse and validate. Returns false with `err` set on any problem; never reads outside the text.
inline bool ParseManifest(const std::string& text, Manifest& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    out = Manifest();
    std::size_t pos = 0; int line = 0; bool header = false;
    while (pos <= text.size()) {
        std::size_t e = text.find('\n', pos); if (e == std::string::npos) e = text.size();
        std::string l = text.substr(pos, e - pos); pos = e + 1; ++line;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        if (l.empty() || l[0] == '#') { if (pos > text.size()) break; continue; }
        std::vector<std::string> t; { std::size_t i = 0; while (i < l.size()) { while (i < l.size() && l[i] == ' ') ++i; std::size_t j = i; while (j < l.size() && l[j] != ' ') ++j; if (j > i) t.push_back(l.substr(i, j - i)); i = j; } }
        if (!header) { if (t.size() != 2 || t[0] != "okay-shaders" || t[1] != "1") return fail("line " + std::to_string(line) + ": not an okay-shaders version 1 manifest"); header = true; continue; }
        if (t[0] == "source") { if (t.size() != 2 || !ParseHex64(t[1], out.sourceHash)) return fail("line " + std::to_string(line) + ": bad source hash"); out.hasSource = true; continue; }
        if (t.size() != 5) return fail("line " + std::to_string(line) + ": expected 5 fields");
        Record r; r.entry = t[0]; r.profile = t[1]; r.file = t[2];
        if (!ValidEntryName(r.entry)) return fail("line " + std::to_string(line) + ": bad entry name");
        if (r.file != FileNameFor(r.entry)) return fail("line " + std::to_string(line) + ": file name must be " + FileNameFor(r.entry));
        char* endp = nullptr; unsigned long long sz = std::strtoull(t[3].c_str(), &endp, 10);
        if (t[3].empty() || *endp != 0 || sz == 0 || sz > (64ull << 20)) return fail("line " + std::to_string(line) + ": bad size");
        r.size = sz;
        if (!ParseHex64(t[4], r.hash)) return fail("line " + std::to_string(line) + ": bad hash");
        if (out.Find(r.entry)) return fail("line " + std::to_string(line) + ": duplicate entry " + r.entry);
        out.records.push_back(r);
        if (pos > text.size()) break;
    }
    if (!header) return fail("empty manifest");
    if (!out.hasSource) return fail("no source line");
    return true;
}

// Compare a manifest with the table of entries the renderer needs. Returns the list of problems (empty = fine).
// Missing optional entries are warnings (returned in `warnings`), missing required ones or a wrong profile are errors.
inline std::vector<std::string> CheckCoverage(const Manifest& m, std::vector<std::string>* warnings = nullptr) {
    std::vector<std::string> errs;
    for (const EntryReq& q : Entries()) {
        const Record* r = m.Find(q.entry);
        if (!r) { if (q.required) errs.push_back(std::string("required shader missing: ") + q.entry); else if (warnings) warnings->push_back(std::string("optional shader missing: ") + q.entry); continue; }
        if (r->profile != q.profile) errs.push_back(std::string("wrong profile for ") + q.entry + ": " + r->profile + ", expected " + q.profile);
    }
    return errs;
}

// The loaded bundle: entry name -> bytecode. `readFile(name, bytes)` reads one file from the shader folder (the caller decides how:
// std::ifstream inside the package folder on UWP, a map in tests).
class Bundle {
public:
    using ReadFn = std::function<bool(const std::string& name, std::vector<unsigned char>& bytes)>;
    // sourceText: the HLSL the program was built from now; when given, a manifest built from a different text is refused (stale bytecode
    // paired with a changed constant-buffer layout would draw garbage or crash the driver).
    bool Load(const ReadFn& readFile, const char* sourceText, std::string* err = nullptr) {
        auto fail = [&](const std::string& m) { if (err) *err = m; code_.clear(); return false; };
        code_.clear(); warnings_.clear();
        std::vector<unsigned char> mb;
        if (!readFile("shaders.manifest", mb)) return fail("shaders.manifest not found (run scripts/xbox-compile-shaders.ps1)");
        Manifest m; std::string pe;
        if (!ParseManifest(std::string(mb.begin(), mb.end()), m, &pe)) return fail("shaders.manifest: " + pe);
        if (sourceText && Hex64(Fnv1a64(sourceText, std::strlen(sourceText))) != Hex64(m.sourceHash))
            return fail("shaders.manifest was built from a different HLSL text than this program (rebuild the shaders)");
        std::vector<std::string> errs = CheckCoverage(m, &warnings_);
        if (!errs.empty()) return fail(errs[0]);
        for (const Record& r : m.records) {
            std::vector<unsigned char> b;
            if (!readFile(r.file, b)) { if (isRequired(r.entry)) return fail("cannot read " + r.file); warnings_.push_back("cannot read " + r.file); continue; }
            if (b.size() != r.size || Fnv1a64(b.data(), b.size()) != r.hash) { if (isRequired(r.entry)) return fail(r.file + " does not match the manifest (size or hash)"); warnings_.push_back(r.file + " does not match the manifest"); continue; }
            if (b.size() < 4 || std::memcmp(b.data(), "DXBC", 4) != 0) { if (isRequired(r.entry)) return fail(r.file + " is not DXBC bytecode"); warnings_.push_back(r.file + " is not DXBC bytecode"); continue; }
            code_[r.entry] = std::move(b);
        }
        return true;
    }
    const std::vector<unsigned char>* Find(const std::string& entry) const { auto it = code_.find(entry); return it == code_.end() ? nullptr : &it->second; }
    const std::vector<std::string>& Warnings() const { return warnings_; }
    std::size_t Count() const { return code_.size(); }
private:
    static bool isRequired(const std::string& e) { for (const EntryReq& q : Entries()) if (e == q.entry) return q.required; return false; }
    std::map<std::string, std::vector<unsigned char>> code_;
    std::vector<std::string> warnings_;
};

// Build time: make the manifest from the .cso files fxc wrote. `readFile` reads one file of the folder. Missing optional entries become
// warnings; a missing required one, an empty file or a file that is not DXBC is an error (so a failed fxc run cannot ship).
inline bool BuildManifest(const Bundle::ReadFn& readFile, const char* sourceText, Manifest& out, std::vector<std::string>& warnings, std::string* err = nullptr) {
    out = Manifest(); out.hasSource = true; out.sourceHash = Fnv1a64(sourceText, std::strlen(sourceText));
    for (const EntryReq& q : Entries()) {
        std::vector<unsigned char> b;
        const std::string file = FileNameFor(q.entry);
        if (!readFile(file, b) || b.empty()) {
            if (q.required) { if (err) *err = "required shader " + file + " is missing or empty"; return false; }
            warnings.push_back("optional shader " + file + " is missing"); continue;
        }
        if (b.size() < 4 || std::memcmp(b.data(), "DXBC", 4) != 0) { if (err) *err = file + " is not DXBC bytecode"; return false; }
        Record r; r.entry = q.entry; r.profile = q.profile; r.file = file; r.size = b.size(); r.hash = Fnv1a64(b.data(), b.size());
        out.records.push_back(r);
    }
    return true;
}

// Desktop run-time cache (D3D11Renderer::SetShaderCacheDir): the desktop build compiles its HLSL with D3DCompile when it starts, one
// call per entry point over the whole renderer text, and the owner's okay_editor_perf.log (2026-11-25) shows the first 3D frame at 7-9 s.
// The bytes D3DCompile made are kept in a folder in the same layout as the Xbox bundle (shaders.manifest + okay3d_<entry>.cso, checked
// by Bundle::Load above: the source text's hash, sizes, hashes, DXBC magic), so the next start compiles nothing until the HLSL changes.
// No Direct3D here: `compile` is D3DCompile on Windows and a stand-in in tests. UNVERIFIED on Windows (no D3D on the Linux machine).
class CompileCache {
public:
    using WriteFn = std::function<bool(const std::string& name, const std::vector<unsigned char>& bytes)>;
    using CompileFn = std::function<bool(const char* entry, const char* profile, std::vector<unsigned char>& bytes)>;
    /// `read` / `write` reach one file of the cache folder (either may fail: the cache then only saves nothing). A folder made from a
    /// different HLSL text, a damaged file or no folder at all simply means everything is compiled again (and stored).
    void Open(Bundle::ReadFn read, WriteFn write, const char* sourceText) {
        read_ = std::move(read); write_ = std::move(write); source_ = sourceText ? sourceText : "";
        std::string err;
        if (read_) bundle_.Load(read_, source_.c_str(), &err);   // failing is normal: the first start, or a changed HLSL text
    }
    /// The bytecode of `entry`: from the cache, else compiled now and stored. Null when it does not compile. Valid for this cache's life.
    const std::vector<unsigned char>* Get(const char* entry, const char* profile, const CompileFn& compile) {
        if (const std::vector<unsigned char>* b = bundle_.Find(entry)) { ++hits_; return b; }
        auto it = made_.find(entry);
        if (it != made_.end()) return &it->second;
        std::vector<unsigned char> bytes;
        if (!compile || !compile(entry, profile, bytes) || bytes.empty()) return nullptr;
        ++compiled_;
        const std::vector<unsigned char>& kept = made_[entry] = std::move(bytes);
        Store(entry, profile, kept);
        return &kept;
    }
    int Compiled() const { return compiled_; }   ///< entries compiled by this process
    int Hits() const { return hits_; }           ///< entries taken from the folder

private:
    void Store(const char* entry, const char* profile, const std::vector<unsigned char>& bytes) {
        bool listed = false;   // only the entries the bundle lists have a place in the manifest
        for (const EntryReq& q : Entries()) if (std::strcmp(q.entry, entry) == 0 && std::strcmp(q.profile, profile) == 0) listed = true;
        if (!listed || !write_ || !read_ || bytes.size() < 4 || std::memcmp(bytes.data(), "DXBC", 4) != 0) return;
        if (!write_(FileNameFor(entry), bytes)) return;
        Manifest m; std::vector<std::string> warnings; std::string err;
        if (!BuildManifest(read_, source_.c_str(), m, warnings, &err)) return;   // the required entries are not all there yet
        const std::string text = WriteManifest(m);
        write_("shaders.manifest", std::vector<unsigned char>(text.begin(), text.end()));
    }
    Bundle::ReadFn read_;
    WriteFn write_;
    std::string source_;
    Bundle bundle_;
    std::map<std::string, std::vector<unsigned char>> made_;
    int compiled_ = 0, hits_ = 0;
};

}} // namespace okay::shaderbundle
