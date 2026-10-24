#pragma once
// The official OkaySpace game: what the launcher's Play page installs, keeps up to
// date and uninstalls. scripts/publish-official-game.sh puts it in the public
// "releases" bucket:
//   official/VERSION.txt              newest version (uploaded last)
//   official/NOTES.txt                "what's new" lines
//   official/v<ver>/manifest.json     {name, version, exe, notes, image, size, files: [{path, size, sha256}]}
//   official/v<ver>/<path>            every file of the built game
//
// This header holds the parts that don't need the network or the UI, so the
// tests can cover them: reading the manifest (untrusted input: every path is
// checked before it's joined to the install folder), comparing versions, working
// out what to download / delete, and swapping a verified update into place with
// a journal so a crash or failure never leaves a half-updated install.
//
// The install folder (<launcher>/Games/Official) holds the game's files as
// published, plus a ".okayofficial" folder of our own:
//   .okayofficial/manifest.json   the installed manifest
//   .okayofficial/staging/        new files, downloaded and verified before the swap
//   .okayofficial/old/            the files a swap replaced, until it finishes
//   .okayofficial/apply.txt       the swap's journal (present only mid-swap)
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "okay/Platform/Account/AccountService.hpp"   // the bundled SHA-256

namespace okayofficial {

namespace fs = std::filesystem;

constexpr const char* kMetaDir = ".okayofficial";   // ours: a manifest path may not start with it
constexpr std::size_t kMaxManifestBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxFiles = 20000;
constexpr std::uint64_t kMaxFileBytes = 4ull * 1024ull * 1024ull * 1024ull;

struct File {
    std::string path;          // relative, forward slashes ("Data/game.okayscene")
    std::uint64_t size = 0;
    std::string sha256;        // 64 lowercase hex digits
};

struct Manifest {
    std::string name, version;
    std::string exe;           // relative path of the game's exe (one of files)
    std::string notes;
    std::string image;         // optional cover image (one of files), "" = none
    std::uint64_t size = 0;    // total bytes of files
    std::vector<File> files;
};

// What an update needs: files to fetch (new or changed) and files to delete.
struct Plan {
    std::vector<File> download;
    std::vector<std::string> remove;
    std::uint64_t downloadBytes = 0;
    bool empty() const { return download.empty() && remove.empty(); }
};

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---- Versions --------------------------------------------------------------
// Dotted numbers only ("1.2.0", "2026.10.24.1"), as the publish script writes.
inline bool LooksLikeVersion(const std::string& v) {
    if (v.empty() || v.size() > 32 || v.front() == '.' || v.back() == '.') return false;
    bool dot = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (c == '.') { if (v[i - 1] == '.') return false; dot = true; }
        else if (c < '0' || c > '9') return false;
    }
    return dot;
}

// -1 / 0 / 1 for a < b / a == b / a > b ("1.10" > "1.9", "1.2" == "1.2.0").
inline int CompareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<unsigned long long> v;
        unsigned long long n = 0; bool any = false;
        for (char c : s) {
            if (c >= '0' && c <= '9') { if (n < 100000000000ull) n = n * 10 + (unsigned)(c - '0'); any = true; }
            else if (c == '.') { v.push_back(n); n = 0; any = false; }
            else break;
        }
        if (any || !v.empty()) v.push_back(n);
        return v;
    };
    const auto va = parts(a), vb = parts(b);
    for (std::size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
        const unsigned long long x = i < va.size() ? va[i] : 0, y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// ---- Path safety -----------------------------------------------------------
// A manifest path must stay inside the install folder on every OS: relative,
// forward slashes, no "." / ".." / empty parts, nothing Windows treats specially
// (drive letters and streams via ':', reserved device names, trailing dots or
// spaces that Windows strips), and not our own .okayofficial folder.
inline bool SafeRelPath(const std::string& p, std::string* why = nullptr) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (p.empty()) return no("empty path");
    if (p.size() > 400) return no("path too long");
    if (p.front() == '/') return no("absolute path");
    for (unsigned char c : p) {
        if (c < 0x20 || c == 0x7f) return no("control character");
        if (c == '\\') return no("backslash");
        if (c == ':') return no("drive or stream (':')");
        if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return no("character Windows can't store");
    }
    static const char* kDevices[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
                                     "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                     "lpt7", "lpt8", "lpt9", "conin$", "conout$"};
    std::size_t start = 0;
    bool first = true;
    while (start <= p.size()) {
        std::size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        const std::string part = p.substr(start, end - start);
        if (part.empty()) return no("empty path part");
        if (part == "." || part == "..") return no("'.' or '..' in path");
        if (part.back() == '.' || part.back() == ' ') return no("part ends in a dot or space");
        const std::string low = Lower(part.substr(0, part.find('.')));
        for (const char* d : kDevices) if (low == d) return no("reserved device name");
        if (first && Lower(part) == kMetaDir) return no("reserved folder");
        first = false;
        start = end + 1;
        if (end == p.size()) break;
    }
    return true;
}

// root / rel, or "" if rel isn't safe or (through an existing link) resolves
// outside root.
inline fs::path ResolveInside(const fs::path& root, const std::string& rel) {
    if (!SafeRelPath(rel)) return {};
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root, ec);
    if (ec || base.empty()) return {};
    const fs::path full = fs::weakly_canonical(base / fs::u8path(rel), ec);
    if (ec) return {};
    auto b = base.begin(), f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty() && std::next(b) == base.end()) break;   // a trailing separator
        if (f == full.end() || *f != *b) return {};
    }
    if (f == full.end()) return {};   // root itself
    return full;
}

// A manifest path made safe for a URL: each part percent-encoded.
inline std::string UrlEncodePath(const std::string& rel) {
    static const char* hx = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : rel) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') out += (char)c;
        else { out += '%'; out += hx[c >> 4]; out += hx[c & 15]; }
    }
    return out;
}

// ---- JSON ------------------------------------------------------------------
// A small strict reader for the manifest: objects, arrays, strings, numbers,
// true / false / null, nesting limited. Anything malformed fails the parse.
namespace json {
struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    std::string str;          // String, or a Number's text
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;
    const Value* get(const std::string& k) const {
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

class Reader {
public:
    explicit Reader(const std::string& s) : s_(s) {}
    bool Parse(Value& out) {
        if (!ParseValue(out, 0)) return false;
        Ws();
        return i_ == s_.size();
    }
private:
    const std::string& s_;
    std::size_t i_ = 0;
    void Ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    bool Lit(const char* w) {
        const std::size_t n = std::char_traits<char>::length(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n; return true;
    }
    static void Utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
               o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool Str(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            const unsigned char c = (unsigned char)s_[i_++];
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { o += (char)c; continue; }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case '"': o += '"'; break;   case '\\': o += '\\'; break;
                case '/': o += '/'; break;   case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;  case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;  case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {   // surrogate pair
                        unsigned lo = 0;
                        if (!Lit("\\u") || !Hex4(lo) || lo < 0xDC00 || lo >= 0xE000) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp < 0xE000) return false;
                    Utf8(o, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool ParseValue(Value& v, int depth) {
        if (depth > 16) return false;
        Ws();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            v.kind = Value::Object; ++i_; Ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                Ws();
                std::string k;
                if (!Str(k)) return false;
                Ws();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                Value child;
                if (!ParseValue(child, depth + 1)) return false;
                v.obj.emplace_back(std::move(k), std::move(child));
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.kind = Value::Array; ++i_; Ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                Value child;
                if (!ParseValue(child, depth + 1)) return false;
                v.arr.push_back(std::move(child));
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v.kind = Value::String; return Str(v.str); }
        if (Lit("true")) { v.kind = Value::Bool; v.b = true; return true; }
        if (Lit("false")) { v.kind = Value::Bool; v.b = false; return true; }
        if (Lit("null")) { v.kind = Value::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t st = i_;
            if (s_[i_] == '-') ++i_;
            while (i_ < s_.size() && (std::isdigit((unsigned char)s_[i_]) || s_[i_] == '.' || s_[i_] == 'e' ||
                                      s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) ++i_;
            v.kind = Value::Number; v.str = s_.substr(st, i_ - st);
            return v.str != "-";
        }
        return false;
    }
};

inline std::string Escape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
} // namespace json

// A whole non-negative integer ("123"; no sign, fraction or exponent).
inline bool ParseSize(const json::Value* v, std::uint64_t& out) {
    if (!v || v->kind != json::Value::Number || v->str.empty() || v->str.size() > 15) return false;
    std::uint64_t n = 0;
    for (char c : v->str) { if (c < '0' || c > '9') return false; n = n * 10 + (std::uint64_t)(c - '0'); }
    out = n;
    return true;
}

// Text for the UI: control characters dropped (newlines kept when asked), capped.
inline std::string CleanText(const std::string& s, std::size_t cap, bool keepNewlines) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '\n' && keepNewlines) o += '\n';
        else if (c == '\t') o += ' ';
        else if (c >= 0x20 && c != 0x7f) o += (char)c;
        if (o.size() >= cap) {   // cut: don't leave half a UTF-8 character
            while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back();
            if (!o.empty() && ((unsigned char)o.back() & 0x80)) o.pop_back();
            break;
        }
    }
    return o;
}

// Parse + validate a manifest. False (with a reason) for anything malformed or
// unsafe; nothing from a failed parse is used.
inline bool ParseManifest(const std::string& text, Manifest& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    if (text.size() > kMaxManifestBytes) return fail("manifest too large");
    json::Value root;
    if (!json::Reader(text).Parse(root) || root.kind != json::Value::Object) return fail("manifest isn't valid JSON");
    auto str = [&](const char* k) -> std::string {
        const json::Value* v = root.get(k);
        return v && v->kind == json::Value::String ? v->str : std::string();
    };
    Manifest m;
    m.name = CleanText(str("name"), 80, false);
    m.version = str("version");
    m.exe = str("exe");
    m.notes = CleanText(str("notes"), 4000, true);
    m.image = str("image");
    if (m.name.empty()) return fail("manifest has no name");
    if (!LooksLikeVersion(m.version)) return fail("manifest has no valid version");
    const json::Value* files = root.get("files");
    if (!files || files->kind != json::Value::Array || files->arr.empty()) return fail("manifest lists no files");
    if (files->arr.size() > kMaxFiles) return fail("manifest lists too many files");
    std::set<std::string> seen;   // lower-cased: Windows paths are case-insensitive
    for (const json::Value& fv : files->arr) {
        if (fv.kind != json::Value::Object) return fail("bad file entry");
        File f;
        const json::Value* p = fv.get("path");
        const json::Value* h = fv.get("sha256");
        if (!p || p->kind != json::Value::String) return fail("file entry without a path");
        f.path = p->str;
        std::string why;
        if (!SafeRelPath(f.path, &why)) return fail("unsafe path \"" + CleanText(f.path, 80, false) + "\": " + why);
        if (!ParseSize(fv.get("size"), f.size) || f.size > kMaxFileBytes) return fail("bad size for " + f.path);
        if (!h || h->kind != json::Value::String || h->str.size() != 64) return fail("bad sha256 for " + f.path);
        f.sha256 = Lower(h->str);
        if (f.sha256.find_first_not_of("0123456789abcdef") != std::string::npos) return fail("bad sha256 for " + f.path);
        if (!seen.insert(Lower(f.path)).second) return fail("duplicate path " + f.path);
        m.size += f.size;
        m.files.push_back(std::move(f));
    }
    // A file can't also be a folder another file lives in ("Data" and "Data/x").
    for (const File& f : m.files)
        for (std::size_t s = f.path.find('/'); s != std::string::npos; s = f.path.find('/', s + 1))
            if (seen.count(Lower(f.path.substr(0, s)))) return fail("path " + f.path + " is inside a file");
    auto listed = [&](const std::string& p) { return !p.empty() && seen.count(Lower(p)) != 0; };
    if (!listed(m.exe) || Lower(fs::path(m.exe).extension().string()) != ".exe") return fail("manifest's exe isn't one of its files");
    if (!m.image.empty()) {   // a bad cover just isn't shown
        const std::string ext = Lower(fs::path(m.image).extension().string());
        if (!listed(m.image) || (ext != ".png" && ext != ".jpg" && ext != ".jpeg")) m.image.clear();
    }
    out = std::move(m);
    return true;
}

inline std::string ToJson(const Manifest& m) {
    using json::Escape;
    std::ostringstream o;
    o << "{\n  \"name\": \"" << Escape(m.name) << "\",\n  \"version\": \"" << Escape(m.version)
      << "\",\n  \"exe\": \"" << Escape(m.exe) << "\",\n  \"notes\": \"" << Escape(m.notes) << "\",\n";
    if (!m.image.empty()) o << "  \"image\": \"" << Escape(m.image) << "\",\n";
    o << "  \"size\": " << m.size << ",\n  \"files\": [";
    for (std::size_t i = 0; i < m.files.size(); ++i)
        o << (i ? ",\n" : "\n") << "    {\"path\": \"" << Escape(m.files[i].path) << "\", \"size\": " << m.files[i].size
          << ", \"sha256\": \"" << m.files[i].sha256 << "\"}";
    o << "\n  ]\n}\n";
    return o.str();
}

// ---- Diff ------------------------------------------------------------------
// Files of `next` that are new, changed (sha256 / size) or not intact on disk
// (`intact` says whether the installed copy is there with the right size), and
// files of `installed` that `next` no longer has. Paths match case-insensitively
// (one file on Windows), so a renamed-case file is updated, never deleted.
inline Plan Diff(const Manifest* installed, const Manifest& next,
                 const std::function<bool(const File&)>& intact = nullptr) {
    Plan plan;
    std::map<std::string, const File*> have;
    if (installed) for (const File& f : installed->files) have[Lower(f.path)] = &f;
    std::set<std::string> keep;
    for (const File& f : next.files) {
        const std::string key = Lower(f.path);
        keep.insert(key);
        auto it = have.find(key);
        const bool same = it != have.end() && it->second->sha256 == f.sha256 && it->second->size == f.size &&
                          it->second->path == f.path && (!intact || intact(f));
        if (!same) { plan.download.push_back(f); plan.downloadBytes += f.size; }
    }
    if (installed)
        for (const File& f : installed->files)
            if (!keep.count(Lower(f.path))) plan.remove.push_back(f.path);
    return plan;
}

// ---- Files -----------------------------------------------------------------
inline std::string FileSha256(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    okay::account::detail::Sha256 h;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        const std::streamsize n = in.gcount();
        if (n > 0) h.Update(reinterpret_cast<const std::uint8_t*>(buf.data()), (std::size_t)n);
    }
    if (in.bad()) return {};
    const auto d = h.Digest();
    return okay::account::detail::ToHex(d.data(), d.size());
}

// The file exists with the size and sha256 the manifest says.
inline bool Verify(const fs::path& p, const File& f) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != f.size || ec) return false;
    return FileSha256(p) == f.sha256;
}

inline fs::path MetaDir(const fs::path& root)      { return root / kMetaDir; }
inline fs::path StagingDir(const fs::path& root)   { return MetaDir(root) / "staging"; }
inline fs::path ManifestPath(const fs::path& root) { return MetaDir(root) / "manifest.json"; }

inline bool ReadText(const fs::path& p, std::string& out, std::size_t cap = kMaxManifestBytes) {
    std::error_code ec;
    const auto sz = fs::file_size(p, ec);
    if (ec || sz > cap) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss; ss << in.rdbuf();
    out = ss.str();
    return true;
}

// The installed manifest (false = not installed, or unreadable).
inline bool LoadInstalled(const fs::path& root, Manifest& out) {
    std::string text;
    return ReadText(ManifestPath(root), text) && ParseManifest(text, out);
}

// Move a file to `to` (making its folder), replacing whatever is there.
inline bool MoveInto(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    return !ec;
}

namespace detail {
inline bool WriteFileSynced(const fs::path& p, const std::string& text) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << text;
    o.flush();
    return (bool)o;
}

// Undo a swap from its journal. Each "put" moved the old file to old/ (if there
// was one) and then the staged file into place; each "del" moved the file to
// old/. A staged file still in staging/ means its put never happened.
inline void Rollback(const fs::path& root, const std::vector<std::string>& lines) {
    const fs::path old = MetaDir(root) / "old", stage = StagingDir(root);
    std::error_code ec;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const std::string& ln = *it;
        if (ln.size() < 5) continue;
        const std::string op = ln.substr(0, 4), rel = ln.substr(4);
        if (op != "put " && op != "del ") continue;
        const fs::path target = ResolveInside(root, rel);
        if (target.empty()) continue;
        const fs::path saved = old / fs::u8path(rel);
        const bool hadOld = fs::exists(saved, ec);
        if (op == "put " && !fs::exists(stage / fs::u8path(rel), ec)) {
            if (hadOld) MoveInto(saved, target);   // the new file goes, the old one returns
            else fs::remove(target, ec);           // the file was new in this update
        } else if (hadOld) {
            MoveInto(saved, target);
        }
    }
}
} // namespace detail

// Swap a verified update into place: every file of plan.download must already be
// in StagingDir(root) and verified. Moves are journaled; on any failure what was
// moved is put back and the old install (and its manifest) stays. `failAt` is
// for tests: fail as if step N of the swap couldn't be done.
inline bool Apply(const fs::path& root, const Manifest& next, const Plan& plan, std::string* err = nullptr,
                  int failAt = -1) {
    auto fail = [&](const std::string& w) { if (err) *err = w; return false; };
    std::error_code ec;
    const fs::path meta = MetaDir(root), stage = StagingDir(root), old = meta / "old";
    const fs::path journal = meta / "apply.txt";
    for (const File& f : plan.download)   // (the rollback relies on every staged file being there)
        if (!fs::is_regular_file(stage / fs::u8path(f.path), ec)) return fail(f.path + " wasn't downloaded");
    fs::create_directories(meta, ec);
    fs::remove_all(old, ec);
    std::vector<std::string> lines;
    for (const File& f : plan.download) lines.push_back("put " + f.path);
    for (const std::string& p : plan.remove) lines.push_back("del " + p);
    {
        std::string j = "version " + next.version + "\n";
        for (const std::string& l : lines) j += l + "\n";
        if (!detail::WriteFileSynced(journal, j)) return fail("couldn't write to the install folder");
    }
    int step = 0;
    bool ok = true;
    std::string why;
    for (const std::string& ln : lines) {
        const std::string rel = ln.substr(4);
        const fs::path target = ResolveInside(root, rel);
        if (target.empty() || step++ == failAt) { ok = false; why = "couldn't place " + rel; break; }
        const fs::path saved = old / fs::u8path(rel);
        if (fs::exists(target, ec) && !MoveInto(target, saved)) { ok = false; why = rel + " is in use"; break; }
        if (ln[0] == 'p' && !MoveInto(stage / fs::u8path(rel), target)) { ok = false; why = "couldn't place " + rel; break; }
    }
    if (ok) {
        const fs::path tmp = ManifestPath(root).string() + ".new";
        ok = detail::WriteFileSynced(tmp, ToJson(next)) && step != failAt && MoveInto(tmp, ManifestPath(root));
        if (!ok) why = "couldn't save the manifest";
    }
    if (!ok) {
        detail::Rollback(root, lines);
        fs::remove(ManifestPath(root).string() + ".new", ec);
        fs::remove(journal, ec);
        fs::remove_all(old, ec);
        return fail(why);
    }
    fs::remove(journal, ec);
    fs::remove_all(old, ec);
    fs::remove_all(stage, ec);
    // Folders the deleted files leave empty go too (never the install root).
    for (const std::string& p : plan.remove) {
        fs::path d = ResolveInside(root, p).parent_path();
        const fs::path base = fs::weakly_canonical(root, ec);
        while (!d.empty() && d != base && d.string().size() > base.string().size() && fs::is_empty(d, ec) && !ec) {
            fs::remove(d, ec);
            d = d.parent_path();
        }
    }
    return true;
}

// After a crash mid-swap: finish it if the new manifest was written, otherwise
// put the old files back. Call before reading the installed manifest.
inline void Recover(const fs::path& root) {
    std::error_code ec;
    const fs::path meta = MetaDir(root), journal = meta / "apply.txt";
    const fs::path tmp = ManifestPath(root).string() + ".new";
    if (!fs::exists(journal, ec)) { fs::remove(tmp, ec); return; }
    std::vector<std::string> lines;
    std::string ver;
    {
        std::ifstream in(journal);
        std::string ln;
        while (std::getline(in, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.rfind("version ", 0) == 0) ver = ln.substr(8); else lines.push_back(ln);
        }
    }
    Manifest done;
    std::string text;
    if (ReadText(tmp, text) && ParseManifest(text, done) && done.version == ver) {
        MoveInto(tmp, ManifestPath(root));   // every file was in place; only the rename was left
    } else if (LoadInstalled(root, done) && done.version == ver) {
        // finished; only the clean-up was left
    } else {
        detail::Rollback(root, lines);
        fs::remove(tmp, ec);
    }
    fs::remove(journal, ec);
    fs::remove_all(meta / "old", ec);
}

} // namespace okayofficial
