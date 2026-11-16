// Our own ZAE reader (.zae: a COLLADA document in a ZIP archive). Written from the public description of the format in the
// COLLADA 1.5 specification (Khronos; the "ZAE" archive: a ZIP file whose root may hold a manifest.xml naming the root
// document in a <dae_root> element, with the document's images and other files beside it) and checked against the three
// .zae files of Assimp's test folder; no code from any other importer. The archive is opened with the engine's own ZIP
// reader (ZipLite.hpp, which caps sizes and entry counts against zip bombs), the document is read by our own COLLADA reader,
// and the images the document names that are inside the archive are written next to the .zae as "<name>_zae_<file>" so the
// renderer can load them (the path version only; the buffer version reports them in `out.notes`).
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "XmlLite.hpp"
#include "ZipLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace okay {

namespace {
namespace zae {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
// The prefix the COLLADA reader puts in front of a relative image path. It cannot start a real path (a control character),
// so after the read a texture that starts with it is known to be a path inside the archive.
const char* const kInArchive = "\x01zae\x01/";

bool Fail(std::string* err, const std::string& m) { if (err) *err = "ZAE: " + m; return false; }

std::string PercentDecode(const std::string& s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else r += s[i];
    }
    return r;
}

// "a/./b/../c.png" -> "a/c.png" inside the archive; false when ".." climbs out of it (the file is then not in the archive).
bool NormalizeInArchive(std::string p, std::string& out) {
    for (char& c : p) if (c == '\\') c = '/';
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i <= p.size()) {
        std::size_t e = p.find('/', i);
        if (e == std::string::npos) e = p.size();
        const std::string seg = p.substr(i, e - i);
        if (seg == "..") { if (parts.empty()) return false; parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        i = e + 1;
    }
    out.clear();
    for (const std::string& s : parts) out += (out.empty() ? "" : "/") + s;
    return !out.empty();
}

std::string DirOf(const std::string& entry) {
    const std::size_t sl = entry.find_last_of('/');
    return sl == std::string::npos ? std::string() : entry.substr(0, sl + 1);
}

bool EndsWithDae(const std::string& n) {
    if (n.size() < 4) return false;
    std::string t = n.substr(n.size() - 4);
    for (char& c : t) c = (char)std::tolower((unsigned char)c);
    return t == ".dae";
}

// The root document's entry: <dae_root> of manifest.xml, else the .dae nearest the top (then the first in name order).
int FindRoot(detail::zip::Archive& z, std::vector<std::string>& notes, std::string& why) {
    const int mf = z.Find("manifest.xml");
    if (mf >= 0) {
        std::string text;
        if (!z.Extract(mf, text)) { why = z.err; return -1; }
        detail::xml::Doc d;
        detail::xml::Limits l;
        l.maxElements = 1000;
        std::string e;
        if (detail::xml::Parse(text, d, e, l)) {
            std::string root;
            if (d.Name(0) == "dae_root") root = d.Text(0);
            else { const int r = d.First(0, "dae_root"); if (r >= 0) root = d.Text(r); }
            while (!root.empty() && detail::txt::IsWs(root.back())) root.pop_back();
            std::size_t a = 0;
            while (a < root.size() && detail::txt::IsWs(root[a])) ++a;
            root = PercentDecode(root.substr(a));
            std::string norm;
            if (!root.empty() && NormalizeInArchive(root, norm)) {
                const int ix = z.Find(norm);
                if (ix >= 0) return ix;
                notes.push_back("ZAE: the manifest names '" + norm.substr(0, 200) + "', which is not in the archive; the first .dae is read instead");
            } else notes.push_back("ZAE: the manifest names no root document; the first .dae is read instead");
        } else notes.push_back("ZAE: manifest.xml is not readable XML (" + e.substr(0, 120) + "); the first .dae is read instead");
    }
    int best = -1;
    std::size_t bestDepth = 0;
    for (std::size_t i = 0; i < z.entries.size(); ++i) {
        const std::string& n = z.entries[i].name;
        if (!EndsWithDae(n)) continue;
        const std::size_t depth = (std::size_t)std::count(n.begin(), n.end(), '/');
        if (best < 0 || depth < bestDepth || (depth == bestDepth && n < z.entries[(std::size_t)best].name)) { best = (int)i; bestDepth = depth; }
    }
    if (best < 0) why = "the archive holds no .dae document";
    return best;
}

// True when a file of `size` bytes is at `path` (a sidecar written by an earlier read of the same archive).
bool SidecarThere(const std::string& path, std::uint64_t size) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (!fs::is_regular_file(p, ec)) return false;
    const std::uintmax_t n = fs::file_size(p, ec);
    return !ec && n == size;
}

// Writes `bytes` to `path` unless a file of the same size is there already. False when it cannot be written.
bool WriteSidecar(const std::string& path, const std::string& bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) == bytes.size() && !ec) return true;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)f;
}

// `zaePath` empty: the buffer version (no files are written). `diskDir` is the folder of the .zae (ends with a slash or empty).
bool ReadImpl(const std::string& bytes, const std::string& zaePath, const std::string& diskDir, ImportedModel& out, std::string* err, bool flatten) {
    if (bytes.size() > kMaxFileBytes) return Fail(err, "the file is too large");
    if (bytes.size() < 4 || bytes.compare(0, 2, "PK") != 0) return Fail(err, "not a ZIP archive (a .zae file is one)");
    detail::zip::Archive z;
    if (!z.Open(bytes)) return Fail(err, z.err);
    std::vector<std::string> notes;
    std::string why;
    const int root = FindRoot(z, notes, why);
    if (root < 0) return Fail(err, why);
    const std::string rootName = z.entries[(std::size_t)root].name;
    std::string text;
    if (!z.Extract(root, text)) return Fail(err, z.err);
    std::string e1;
    if (!ReadColladaBuffer(text, std::string(kInArchive) + DirOf(rootName), out, &e1, flatten)) {
        out = ImportedModel{};
        if (err) *err = "ZAE (" + rootName.substr(0, 200) + "): " + e1;
        return false;
    }
    text.clear(); text.shrink_to_fit();
    // the images: inside the archive -> written next to the .zae (path version), else looked up next to the .zae on disk
    std::string stem;
    if (!zaePath.empty()) {
        const std::size_t sl = zaePath.find_last_of("/\\");
        stem = zaePath.substr(sl == std::string::npos ? 0 : sl + 1);
        const std::size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem.resize(dot);
    }
    std::map<std::string, std::string> done;   // archive entry -> the path it was written to ("" = could not be)
    int inArchive = 0, notWritten = 0, missing = 0;
    const std::size_t pl = std::char_traits<char>::length(kInArchive);
    for (ImportedMaterial& m : out.materials) {
        for (std::string* t : {&m.texture, &m.normalMap, &m.specularMap, &m.aoMap}) {
            if (t->compare(0, pl, kInArchive) != 0) continue;
            const std::string rel = t->substr(pl);
            std::string norm;
            const int ix = NormalizeInArchive(rel, norm) ? z.Find(norm) : -1;
            if (ix < 0) {   // not in the archive: a file next to the .zae (the archive's root is the .zae's folder)
                ++missing;
                std::string r = rel;
                while (r.compare(0, 2, "./") == 0) r.erase(0, 2);
                *t = diskDir + r;
                continue;
            }
            ++inArchive;
            const std::string key = z.entries[(std::size_t)ix].name;
            auto it = done.find(key);
            if (it == done.end()) {
                std::string dest;
                if (!zaePath.empty()) {
                    std::string base = key.substr(key.find_last_of('/') == std::string::npos ? 0 : key.find_last_of('/') + 1);
                    for (char& c : base) if (c == ':' || c == '\\' || (unsigned char)c < 32) c = '_';
                    dest = diskDir + stem + "_zae_" + base;
                    if (!SidecarThere(dest, z.entries[(std::size_t)ix].usize)) {   // already written by an earlier read: not extracted again
                        std::string img;
                        if (!z.Extract(ix, img) || !WriteSidecar(dest, img)) { dest.clear(); z.err.clear(); }
                    }
                }
                it = done.emplace(key, dest).first;
            }
            if (it->second.empty()) ++notWritten;
            *t = it->second;
        }
    }
    out.backend = "own";
    for (const std::string& n : notes) out.notes.push_back(n);
    out.notes.push_back("ZAE: read the document '" + rootName.substr(0, 200) + "' of the archive with the COLLADA reader");
    if (inArchive && !zaePath.empty() && notWritten == 0) out.notes.push_back("ZAE: " + std::to_string(done.size()) + " image(s) of the archive written next to the file as '" + stem + "_zae_<name>'");
    if (notWritten) out.notes.push_back("ZAE: " + std::to_string(notWritten) + " texture reference(s) to images inside the archive left empty (" + (zaePath.empty() ? std::string("read from memory, nothing is written") : std::string("could not extract or write them")) + ")");
    if (missing) out.notes.push_back("ZAE: " + std::to_string(missing) + " texture reference(s) not in the archive, looked up next to the .zae");
    return true;
}

} // namespace zae
} // namespace

bool ReadZaeBuffer(const std::string& bytes, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    const bool ok = zae::ReadImpl(bytes, std::string(), std::string(), out, err, flatten);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadZaeFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, zae::kMaxFileBytes, bytes, err)) return false;
    const std::size_t sl = path.find_last_of("/\\");
    const bool ok = zae::ReadImpl(bytes, path, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
    if (!ok) out = ImportedModel{};
    return ok;
}

} // namespace okay
