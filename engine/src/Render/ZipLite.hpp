#pragma once
// A small ZIP reader for the own 3MF / AMF readers. Internal: not installed.
//
// Reads the central directory and extracts one entry at a time, stored or deflated (okay::Inflate, the engine's own
// decoder). Hard caps against zip bombs and hostile directories: file size, entry count, per-entry and total
// uncompressed bytes, and the inflated size must match the size the directory declares. Encrypted entries, ZIP64,
// multi-disk archives and other compression methods are refused with a message. Entry names are only compared,
// never used as file system paths, so there is no path traversal.
#include "okay/Core/ImageDecode.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace detail {
namespace zip {

struct Limits {
    std::size_t maxArchiveBytes = 512u * 1024u * 1024u;
    std::size_t maxEntries = 20000;
    std::size_t maxEntryBytes = 256u * 1024u * 1024u;    // one entry, uncompressed
    std::size_t maxTotalBytes = 384u * 1024u * 1024u;    // everything extracted from one archive
};

struct Entry {
    std::string name;
    std::uint16_t method = 0, flags = 0;
    std::uint32_t crc = 0, csize = 0, usize = 0, localOffset = 0;
};

class Archive {
public:
    Limits lim;
    std::vector<Entry> entries;
    std::string err;

    bool Open(const std::string& bytes) {
        data_ = &bytes; entries.clear(); err.clear(); total_ = 0;
        const std::size_t n = bytes.size();
        if (n > lim.maxArchiveBytes) return Fail("ZIP: archive too large");
        if (n < 22) return Fail("ZIP: not a zip archive");
        // End of central directory record: the last 22 bytes, or up to 64 KiB earlier (comment).
        std::size_t eocd = std::string::npos;
        const std::size_t lo = n > 22 + 65535 ? n - 22 - 65535 : 0;
        for (std::size_t i = n - 22 + 1; i-- > lo;)
            if (U32(i) == 0x06054b50u) { eocd = i; break; }
        if (eocd == std::string::npos) return Fail("ZIP: no end-of-directory record");
        const unsigned disk = U16(eocd + 4), cdDisk = U16(eocd + 6);
        const std::uint32_t total = U16(eocd + 10), cdSize = U32(eocd + 12), cdOff = U32(eocd + 16);
        if (disk != 0 || cdDisk != 0) return Fail("ZIP: multi-disk archives are not supported");
        if (total == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return Fail("ZIP: ZIP64 archives are not supported");
        if (total > lim.maxEntries) return Fail("ZIP: too many entries");
        if ((std::size_t)cdOff > n || (std::size_t)cdSize > n - cdOff) return Fail("ZIP: central directory outside the file");
        std::size_t p = cdOff;
        const std::size_t end = (std::size_t)cdOff + cdSize;
        for (std::uint32_t i = 0; i < total; ++i) {
            if (end - p < 46 || end < p) return Fail("ZIP: truncated central directory");
            if (U32(p) != 0x02014b50u) return Fail("ZIP: bad central directory entry");
            Entry e;
            e.flags = U16(p + 8); e.method = U16(p + 10); e.crc = U32(p + 16);
            e.csize = U32(p + 20); e.usize = U32(p + 24);
            const std::size_t nl = U16(p + 28), xl = U16(p + 30), cl = U16(p + 32);
            e.localOffset = U32(p + 42);
            if (end - p - 46 < nl + xl + cl) return Fail("ZIP: truncated central directory entry");
            e.name.assign(bytes, p + 46, nl);
            entries.push_back(std::move(e));
            p += 46 + nl + xl + cl;
        }
        return true;
    }

    // Index of the entry with this name (exact first, then ignoring case), or -1.
    int Find(const std::string& name) const {
        for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].name == name) return (int)i;
        auto low = [](std::string s) { for (char& c : s) { if (c >= 'A' && c <= 'Z') c = (char)(c + 32); else if (c == '\\') c = '/'; } return s; };   // some writers use backslashes
        const std::string l = low(name);
        for (std::size_t i = 0; i < entries.size(); ++i) if (low(entries[i].name) == l) return (int)i;
        return -1;
    }

    bool Extract(int index, std::string& out) {
        out.clear();
        if (index < 0 || (std::size_t)index >= entries.size()) return Fail("ZIP: no such entry");
        const Entry& e = entries[(std::size_t)index];
        const std::string& b = *data_;
        if (e.flags & 1u) return Fail("ZIP: encrypted entries are not supported");
        if (e.method != 0 && e.method != 8) return Fail("ZIP: unsupported compression method (only stored and deflate)");
        if (e.usize > lim.maxEntryBytes) return Fail("ZIP: entry too large (zip bomb guard)");
        if (total_ + e.usize > lim.maxTotalBytes) return Fail("ZIP: too much data in one archive (zip bomb guard)");
        const std::size_t lo = e.localOffset;
        if (lo > b.size() || b.size() - lo < 30 || U32(lo) != 0x04034b50u) return Fail("ZIP: bad local header");
        const std::size_t start = lo + 30 + (std::size_t)U16(lo + 26) + U16(lo + 28);
        if (start > b.size() || b.size() - start < e.csize) return Fail("ZIP: entry data outside the file");
        if (e.method == 0) {
            if (e.csize != e.usize) return Fail("ZIP: stored entry size mismatch");
            out.assign(b, start, e.csize);
        } else {
            if (e.usize > 0 && e.csize == 0) return Fail("ZIP: entry size mismatch");
            std::vector<std::uint8_t> v;
            if (!okay::Inflate(reinterpret_cast<const std::uint8_t*>(b.data()) + start, e.csize, false, v, (std::size_t)e.usize))
                return Fail("ZIP: corrupt deflate data, or more data than the directory declares");
            if (v.size() != e.usize) return Fail("ZIP: inflated size differs from the directory");
            out.assign(reinterpret_cast<const char*>(v.data()), v.size());
        }
        if (Crc32(out) != e.crc) return Fail("ZIP: checksum mismatch");
        total_ += out.size();
        return true;
    }

    static std::uint32_t Crc32(const std::string& s) {
        static const std::vector<std::uint32_t> table = [] {
            std::vector<std::uint32_t> t(256);
            for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
            return t;
        }();
        std::uint32_t c = 0xFFFFFFFFu;
        for (unsigned char ch : s) c = table[(c ^ ch) & 255] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

private:
    const std::string* data_ = nullptr;
    std::size_t total_ = 0;
    bool Fail(const char* m) { err = m; return false; }
    std::uint32_t U16(std::size_t i) const { const std::string& b = *data_; return (unsigned char)b[i] | ((std::uint32_t)(unsigned char)b[i + 1] << 8); }
    std::uint32_t U32(std::size_t i) const { return U16(i) | (U16(i + 2) << 16); }
};

} // namespace zip
} // namespace detail
} // namespace okay
