#pragma once
// ---------------------------------------------------------------------------
// EXPERIMENTAL Xbox One Developer Mode export: our own APPX writer and checker (no MakeAppx).
//
// Writes an UNSIGNED .appx: a ZIP container (Open Packaging Conventions) holding the payload files,
// AppxManifest.xml, AppxBlockMap.xml (SHA-256 of every 64 KiB block of every payload file) and
// [Content_Types].xml. Written from Microsoft's public documentation: the "App package structure",
// "App package block map" and "App package manifest" pages, ECMA-376 part 2 (Open Packaging
// Conventions: part names, [Content_Types].xml) and the ZIP application note (PKWARE APPNOTE:
// local file header, central directory, end of central directory). SHA-256 is our own okay::crypto
// code; the ZIP CRC-32 table is generated here; base64 is RFC 4648. No other project's code.
//
// Choices that are ours, and UNVERIFIED against a real Windows install (docs/platform/xbox_uwp.md, "Packer rules"):
//   * every file is STORED (no compression), so a block entry has no Size attribute;
//   * entry order: AppxManifest.xml, the other files in byte order of their names, AppxBlockMap.xml,
//     [Content_Types].xml;
//   * names inside the ZIP are percent-encoded; names in the block map use '\' and no encoding;
//   * fixed time stamp 1980-01-01, so the same input always gives the same bytes.
// The package is NOT signed: Windows refuses to install an unsigned package. Signing is done on the PC with
// SignTool (see docs/platform/xbox_uwp.md "Signing"); we write no signature code on purpose.
// ---------------------------------------------------------------------------
#include "okay/Crypto/Sha256.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace okay::xbox::appx {

constexpr std::size_t kBlockSize = 65536;   // the block map hashes the file in 64 KiB pieces

struct PackItem {
    std::string name;                // path inside the package, '/' separators, e.g. "Assets/StoreLogo.png"
    std::string data;                // the bytes (when !fromFile)
    std::string srcPath;             // a file to read at pack time (when fromFile)
    bool fromFile = false;
};
struct PackResult {
    bool ok = false;
    std::string error;
    std::string bytes;               // the finished .appx
    int files = 0;                   // payload files (manifest included, block map and content types not)
    std::uint64_t blocks = 0;
};

namespace detail {

inline const std::uint32_t* CrcTable() {
    static std::uint32_t t[256];
    static bool made = false;
    if (!made) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        made = true;
    }
    return t;
}
inline std::uint32_t Crc32(const std::uint8_t* p, std::size_t n) {
    const std::uint32_t* t = CrcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

inline std::string Base64(const std::uint8_t* p, std::size_t n) {   // RFC 4648, with padding
    static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    std::size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const std::uint32_t v = (std::uint32_t(p[i]) << 16) | (std::uint32_t(p[i + 1]) << 8) | p[i + 2];
        o += a[v >> 18]; o += a[(v >> 12) & 63]; o += a[(v >> 6) & 63]; o += a[v & 63];
    }
    if (n - i == 1) { const std::uint32_t v = std::uint32_t(p[i]) << 16; o += a[v >> 18]; o += a[(v >> 12) & 63]; o += "=="; }
    else if (n - i == 2) { const std::uint32_t v = (std::uint32_t(p[i]) << 16) | (std::uint32_t(p[i + 1]) << 8); o += a[v >> 18]; o += a[(v >> 12) & 63]; o += a[(v >> 6) & 63]; o += '='; }
    return o;
}
inline std::string BlockHashB64(const std::uint8_t* p, std::size_t n) {
    std::uint8_t h[32];
    okay::crypto::Sha256Hash(h, p, n);
    return Base64(h, 32);
}

inline void Put16(std::string& s, std::uint32_t v) { s += char(v & 255); s += char((v >> 8) & 255); }
inline void Put32(std::string& s, std::uint32_t v) { Put16(s, v & 0xFFFF); Put16(s, v >> 16); }
inline std::uint32_t Get16(const std::string& s, std::size_t at) { return std::uint8_t(s[at]) | (std::uint32_t(std::uint8_t(s[at + 1])) << 8); }
inline std::uint32_t Get32(const std::string& s, std::size_t at) { return Get16(s, at) | (Get16(s, at + 2) << 16); }

inline std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; default: o += c; }
    return o;
}
inline std::string Unesc(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0) { o += '&'; i += 4; continue; }
            if (s.compare(i, 4, "&lt;") == 0) { o += '<'; i += 3; continue; }
            if (s.compare(i, 4, "&gt;") == 0) { o += '>'; i += 3; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { o += '"'; i += 5; continue; }
        }
        o += s[i];
    }
    return o;
}

// Part name in the ZIP: percent-encode everything but unreserved characters and '/'.
inline std::string EncodePart(const std::string& name) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : name) {
        if (std::isalnum(c) && c < 128) o += char(c);
        else if (c == '-' || c == '.' || c == '_' || c == '~' || c == '/') o += char(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
inline bool DecodePart(const std::string& in, std::string& out) {
    out.clear();
    auto h = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') { out += in[i]; continue; }
        if (i + 2 >= in.size()) return false;
        const int x = h(in[i + 1]), y = h(in[i + 2]);
        if (x < 0 || y < 0) return false;
        out += char(x * 16 + y);
        i += 2;
    }
    return true;
}
inline std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }

inline const char* TypeForExt(const std::string& ext) {
    static const std::map<std::string, const char*> m = {
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"bmp", "image/bmp"},
        {"exe", "application/x-msdownload"}, {"dll", "application/x-msdownload"}, {"xml", "application/xml"},
        {"json", "application/json"}, {"txt", "text/plain"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"}};
    auto it = m.find(ext);
    return it == m.end() ? "application/octet-stream" : it->second;
}

inline bool BadName(const std::string& n, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = std::string(w) + ": \"" + n + "\""; return true; };
    if (n.empty()) return no("empty file name");
    if (n.size() > 240) return no("file name too long (240 at most)");
    if (n[0] == '/' || n.back() == '/') return no("file name must not start or end with '/'");
    std::size_t start = 0;
    while (start <= n.size()) {
        std::size_t e = n.find('/', start);
        if (e == std::string::npos) e = n.size();
        const std::string part = n.substr(start, e - start);
        if (part.empty()) return no("empty folder name");
        if (part == "." || part == "..") return no("'.' and '..' are not allowed");
        if (part.back() == '.' || part.back() == ' ') return no("a name must not end in '.' or a space");
        {   // Windows device names (CON.txt is as bad as CON): the package folder could not even be unpacked on a PC
            std::string base = Lower(part.substr(0, part.find('.')));
            while (!base.empty() && base.back() == ' ') base.pop_back();
            static const char* dev[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
            for (const char* d : dev) if (base == d) return no("a Windows device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9) cannot be a file name");
        }
        start = e + 1;
    }
    for (unsigned char c : n) if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return no("a character Windows does not allow in a file name");
    const std::string l = Lower(n);
    if (l == "appxblockmap.xml" || l == "[content_types].xml" || l == "appxsignature.p7x") return no("this name is written by the packer");
    return false;
}

} // namespace detail

// Pack `items` into an unsigned .appx. One item must be named AppxManifest.xml.
inline PackResult Pack(std::vector<PackItem> items) {
    using namespace detail;
    PackResult r;
    std::set<std::string> seen;
    bool haveManifest = false;
    for (const PackItem& it : items) {
        std::string why;
        if (BadName(it.name, &why)) { r.error = why; return r; }
        if (!seen.insert(Lower(it.name)).second) { r.error = "two files with the same name (names ignore case): \"" + it.name + "\""; return r; }
        if (it.name == "AppxManifest.xml") haveManifest = true;
    }
    if (!haveManifest) { r.error = "AppxManifest.xml is missing"; return r; }
    // A plain ZIP counts its entries in 16 bits (65535); the block map and [Content_Types].xml take two of them.
    // Without this check the directory would silently hold the wrong count and Windows would reject the package.
    if (items.size() > 65533) { r.error = "too many files for a plain ZIP package (65533 at most, got " + std::to_string(items.size()) + ")"; return r; }
    std::stable_sort(items.begin(), items.end(), [](const PackItem& a, const PackItem& b) {
        const bool am = a.name == "AppxManifest.xml", bm = b.name == "AppxManifest.xml";
        if (am != bm) return am;
        return a.name < b.name;
    });

    struct Central { std::string encName; std::uint32_t crc, size, offset; };
    std::vector<Central> central;
    std::string out;
    std::ostringstream blockMap;
    blockMap << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\">";
    std::set<std::string> exts, overrides;
    auto writeEntry = [&](const std::string& encName, const std::string& data) {
        if (data.size() >= 0xFFFFFFFFull || out.size() >= 0xF0000000ull) { r.error = "package too large for plain ZIP (4 GB)"; return false; }
        Central c; c.encName = encName; c.size = std::uint32_t(data.size()); c.offset = std::uint32_t(out.size());
        c.crc = Crc32(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        Put32(out, 0x04034b50); Put16(out, 20); Put16(out, 0); Put16(out, 0);     // signature, version, flags, method = stored
        Put16(out, 0); Put16(out, 0x0021);                                         // time 00:00:00, date 1980-01-01
        Put32(out, c.crc); Put32(out, c.size); Put32(out, c.size);
        Put16(out, std::uint32_t(encName.size())); Put16(out, 0);
        out += encName; out += data;
        central.push_back(c);
        return true;
    };
    for (PackItem& it : items) {
        std::string data = it.data;
        if (it.fromFile) {
            std::ifstream f(std::filesystem::u8path(it.srcPath), std::ios::binary);
            if (!f) { r.error = "cannot read " + it.srcPath; return r; }
            std::ostringstream ss; ss << f.rdbuf(); data = ss.str();
        }
        const std::string enc = EncodePart(it.name);
        std::string win = it.name; for (char& c : win) if (c == '/') c = '\\';
        blockMap << "\n<File Name=\"" << Esc(win) << "\" Size=\"" << data.size() << "\" LfhSize=\"" << (30 + enc.size()) << "\">";
        for (std::size_t off = 0; off < data.size(); off += kBlockSize) {
            const std::size_t n = std::min(kBlockSize, data.size() - off);
            blockMap << "<Block Hash=\"" << BlockHashB64(reinterpret_cast<const std::uint8_t*>(data.data()) + off, n) << "\"/>";
            ++r.blocks;
        }
        blockMap << "</File>";
        if (!writeEntry(enc, data)) return r;
        ++r.files;
        if (it.name != "AppxManifest.xml") {
            const std::size_t dot = it.name.find_last_of('.'), slash = it.name.find_last_of('/');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) overrides.insert(it.name);
            else exts.insert(Lower(it.name.substr(dot + 1)));
        }
    }
    blockMap << "\n</BlockMap>\n";
    if (!writeEntry("AppxBlockMap.xml", blockMap.str())) return r;

    std::ostringstream ct;
    ct << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">";
    for (const std::string& e : exts) ct << "<Default Extension=\"" << Esc(e) << "\" ContentType=\"" << TypeForExt(e) << "\"/>";
    for (const std::string& o : overrides) ct << "<Override PartName=\"/" << Esc(EncodePart(o)) << "\" ContentType=\"application/octet-stream\"/>";
    ct << "<Override PartName=\"/AppxManifest.xml\" ContentType=\"application/vnd.ms-appx.manifest+xml\"/>"
          "<Override PartName=\"/AppxBlockMap.xml\" ContentType=\"application/vnd.ms-appx.blockmap+xml\"/></Types>\n";
    if (!writeEntry("[Content_Types].xml", ct.str())) return r;

    const std::uint32_t cdStart = std::uint32_t(out.size());
    for (const Central& c : central) {
        Put32(out, 0x02014b50); Put16(out, 20); Put16(out, 20); Put16(out, 0); Put16(out, 0);
        Put16(out, 0); Put16(out, 0x0021);
        Put32(out, c.crc); Put32(out, c.size); Put32(out, c.size);
        Put16(out, std::uint32_t(c.encName.size())); Put16(out, 0); Put16(out, 0); Put16(out, 0); Put16(out, 0); Put32(out, 0);
        Put32(out, c.offset);
        out += c.encName;
    }
    const std::uint32_t cdSize = std::uint32_t(out.size()) - cdStart;
    Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0);
    Put16(out, std::uint32_t(central.size())); Put16(out, std::uint32_t(central.size()));
    Put32(out, cdSize); Put32(out, cdStart); Put16(out, 0);
    r.bytes = std::move(out);
    r.ok = true;
    return r;
}

// Every file under `folder` (not the folder name itself), names with '/' separators.
inline PackResult PackFolder(const std::string& folder) {
    namespace fs = std::filesystem;
    PackResult r;
    std::error_code ec;
    const fs::path root = fs::u8path(folder);
    if (!fs::is_directory(root, ec)) { r.error = "not a folder: " + folder; return r; }
    std::vector<PackItem> items;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        PackItem p;
        p.name = it->path().lexically_relative(root).generic_u8string();
        p.srcPath = it->path().u8string(); p.fromFile = true;
        const std::string l = detail::Lower(p.name);
        if (l == "appxblockmap.xml" || l == "[content_types].xml" || l == "appxsignature.p7x") continue;   // stale ones from an earlier pack
        items.push_back(std::move(p));
    }
    return Pack(std::move(items));
}

struct VerifyResult {
    bool ok = false;
    std::vector<std::string> problems;
    int files = 0;
    std::uint64_t blocks = 0;
    bool signedPackage = false;   // an AppxSignature.p7x part is present (we do not check it)
};

// Read a package back with our own reader and check what we can without Windows. Understands STORED entries only
// (what Pack writes). A deflated entry is reported as a problem, not skipped.
inline VerifyResult Verify(const std::string& z) {
    using namespace detail;
    VerifyResult v;
    auto bad = [&](const std::string& s) { v.problems.push_back(s); };
    if (z.size() < 22) { bad("too small to be a ZIP file"); return v; }
    std::size_t eocd = std::string::npos;
    for (std::size_t i = z.size() - 22 + 1; i-- > 0;) { if (Get32(z, i) == 0x06054b50) { eocd = i; break; } if (z.size() - i > 22 + 65535) break; }
    if (eocd == std::string::npos) { bad("no end-of-central-directory record"); return v; }
    const std::uint32_t count = Get16(z, eocd + 10), cdSize = Get32(z, eocd + 12), cdOff = Get32(z, eocd + 16);
    if (Get16(z, eocd + 4) != 0 || Get16(z, eocd + 6) != 0) bad("multi-disk ZIP");
    if (std::uint64_t(cdOff) + cdSize > eocd) { bad("central directory is out of range"); return v; }
    struct E { std::string name; std::string data; std::size_t lfh; };
    std::vector<E> es;
    std::size_t p = cdOff;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 46 > z.size() || Get32(z, p) != 0x02014b50) { bad("bad central directory entry " + std::to_string(i)); return v; }
        const std::uint32_t flags = Get16(z, p + 8), method = Get16(z, p + 10), crc = Get32(z, p + 16), cs = Get32(z, p + 20), us = Get32(z, p + 24);
        const std::uint32_t nl = Get16(z, p + 28), xl = Get16(z, p + 30), cl = Get16(z, p + 32), off = Get32(z, p + 42);
        if (p + 46 + nl + xl + cl > z.size()) { bad("central directory entry runs past the end"); return v; }
        E e; e.name = z.substr(p + 46, nl); e.lfh = off;
        p += 46 + nl + xl + cl;
        if (std::uint64_t(off) + 30 > z.size() || Get32(z, off) != 0x04034b50) { bad("bad local header for " + e.name); continue; }
        const std::uint32_t lnl = Get16(z, off + 26), lxl = Get16(z, off + 28);
        if (z.compare(off + 30, lnl, e.name) != 0) bad("local header name differs from the directory for " + e.name);
        if (flags & 8) bad("data descriptor used (not allowed here): " + e.name);
        if (method != 0) { bad("entry is not stored (this checker reads stored entries only): " + e.name); continue; }
        if (cs != us) bad("stored entry with different sizes: " + e.name);
        const std::uint64_t ds = std::uint64_t(off) + 30 + lnl + lxl;
        if (ds + us > cdOff) { bad("data of " + e.name + " is out of range"); continue; }
        e.data = z.substr(ds, us);
        if (Crc32(reinterpret_cast<const std::uint8_t*>(e.data.data()), e.data.size()) != crc) bad("CRC-32 mismatch: " + e.name);
        es.push_back(std::move(e));
    }
    std::map<std::string, const E*> byName;   // decoded, lower-case
    std::set<std::string> dup;
    for (const E& e : es) {
        std::string dec;
        if (e.name == "[Content_Types].xml") dec = e.name; else if (!DecodePart(e.name, dec)) { bad("bad percent-encoding in " + e.name); continue; }
        if (!byName.emplace(Lower(dec), &e).second) bad("duplicate part (names ignore case): " + dec);
    }
    auto get = [&](const std::string& n) -> const E* { auto it = byName.find(Lower(n)); return it == byName.end() ? nullptr : it->second; };
    const E *ct = get("[Content_Types].xml"), *bm = get("AppxBlockMap.xml"), *mf = get("AppxManifest.xml");
    if (!ct) bad("[Content_Types].xml is missing");
    if (!bm) bad("AppxBlockMap.xml is missing");
    if (!mf) bad("AppxManifest.xml is missing");
    v.signedPackage = get("AppxSignature.p7x") != nullptr;
    if (!ct || !bm) return v;

    // --- content types: every part is covered by a Default (extension) or an Override (part name) ---
    std::map<std::string, std::string> defaults, overrides;
    auto attr = [](const std::string& tag, const char* key, std::string& val) {
        const std::string k = std::string(key) + "=\"";
        const std::size_t a = tag.find(k);
        if (a == std::string::npos) return false;
        const std::size_t b = tag.find('"', a + k.size());
        if (b == std::string::npos) return false;
        val = Unesc(tag.substr(a + k.size(), b - a - k.size()));
        return true;
    };
    for (std::size_t i = 0; (i = ct->data.find('<', i)) != std::string::npos;) {
        const std::size_t e = ct->data.find('>', i); if (e == std::string::npos) break;
        const std::string tag = ct->data.substr(i, e - i + 1);
        std::string a, b;
        if (tag.compare(0, 8, "<Default") == 0 && attr(tag, "Extension", a) && attr(tag, "ContentType", b)) defaults[Lower(a)] = b;
        if (tag.compare(0, 9, "<Override") == 0 && attr(tag, "PartName", a) && attr(tag, "ContentType", b)) overrides[Lower(a)] = b;
        i = e + 1;
    }
    for (const E& e : es) {
        if (e.name == "[Content_Types].xml") continue;
        const std::string enc = "/" + e.name;
        if (overrides.count(Lower(enc))) continue;
        const std::size_t dot = e.name.find_last_of('.'), slash = e.name.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && defaults.count(Lower(e.name.substr(dot + 1)))) continue;
        bad("no content type for " + e.name);
    }
    if (overrides["/appxmanifest.xml"] != "application/vnd.ms-appx.manifest+xml") bad("manifest content type is wrong");
    if (overrides["/appxblockmap.xml"] != "application/vnd.ms-appx.blockmap+xml") bad("block map content type is wrong");

    // --- block map: every payload file once, size, header size and every block hash ---
    std::set<std::string> listed;
    const std::string& m = bm->data;
    for (std::size_t i = 0; (i = m.find("<File ", i)) != std::string::npos;) {
        const std::size_t e = m.find('>', i), close = m.find("</File>", i);
        if (e == std::string::npos || close == std::string::npos) { bad("block map: unfinished File element"); break; }
        const std::string tag = m.substr(i, e - i + 1);
        std::string name, size, lfh;
        if (!attr(tag, "Name", name) || !attr(tag, "Size", size) || !attr(tag, "LfhSize", lfh)) { bad("block map: File without Name, Size or LfhSize"); i = close + 7; continue; }
        std::string unix = name; for (char& c : unix) if (c == '\\') c = '/';
        const E* f = get(unix);
        if (!f) { bad("block map lists a file that is not in the package: " + name); i = close + 7; continue; }
        if (!listed.insert(Lower(unix)).second) bad("block map lists a file twice: " + name);
        if (std::to_string(f->data.size()) != size) bad("block map size is wrong for " + name);
        if (std::to_string(30 + f->name.size()) != lfh) bad("block map LfhSize is wrong for " + name);
        std::vector<std::string> hashes;
        for (std::size_t b = e; (b = m.find("<Block ", b)) != std::string::npos && b < close;) {
            const std::size_t be = m.find('>', b);
            std::string h; if (!attr(m.substr(b, be - b + 1), "Hash", h)) bad("block without Hash in " + name); else hashes.push_back(h);
            b = be + 1;
        }
        const std::size_t want = (f->data.size() + kBlockSize - 1) / kBlockSize;
        if (hashes.size() != want) bad("wrong number of blocks for " + name + " (" + std::to_string(hashes.size()) + ", expected " + std::to_string(want) + ")");
        for (std::size_t b = 0; b < hashes.size() && b < want; ++b) {
            const std::size_t off = b * kBlockSize, n = std::min(kBlockSize, f->data.size() - off);
            if (hashes[b] != BlockHashB64(reinterpret_cast<const std::uint8_t*>(f->data.data()) + off, n)) bad("block " + std::to_string(b) + " hash mismatch in " + name);
            ++v.blocks;
        }
        ++v.files;
        i = close + 7;
    }
    for (const E& e : es) {
        std::string dec = e.name == "[Content_Types].xml" ? e.name : std::string();
        if (dec.empty()) DecodePart(e.name, dec);
        const std::string l = Lower(dec);
        if (l == "appxblockmap.xml" || l == "[content_types].xml" || l == "appxsignature.p7x") continue;
        if (!listed.count(l)) bad("file is not in the block map: " + dec);
    }
    v.ok = v.problems.empty();
    return v;
}

} // namespace okay::xbox::appx
