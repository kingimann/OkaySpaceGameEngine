// Asset packages: the hash, ids, the writer, the reader with its checks, the package stack, patches and the manifest.
// Written for this engine from the format description in okay/Core/AssetPackage.hpp and docs/engine/asset_packages.md
// (designed from first principles; no other engine's package format or code was used). Compression is the engine's
// own DEFLATE pair: okay::ImageWrite::Deflate to pack, okay::inflate::Raw to unpack.
#include "okay/Core/AssetPackage.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/Inflate.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/SaveData.hpp"   // savefmt::MoveOver
#include "okay/Core/StringUtil.hpp"   // str::LowerAscii
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include "CoreThreads.hpp"   // last: the private threading shim (never <mutex> in a header, CLAUDE.md 21a)

namespace okay {
namespace assetpkg {

namespace fs = std::filesystem;

// ---- Hash64 ------------------------------------------------------------------------------------------------
// Odd 64-bit constants: the fractional parts of the golden ratio and of sqrt(2) (made odd), sqrt(3), sqrt(5), sqrt(7),
// times 2^64. Worked out with exact decimal arithmetic; the pinned values in tests/test_asset_package.cpp hold them.
namespace {
constexpr std::uint64_t K0 = 0x9E3779B97F4A7C15ull;
constexpr std::uint64_t K1 = 0x6A09E667F3BCC909ull;
constexpr std::uint64_t K2 = 0xBB67AE8584CAA73Bull;
constexpr std::uint64_t K3 = 0x3C6EF372FE94F82Bull;
constexpr std::uint64_t K4 = 0xA54FF53A5F1D36F1ull;

inline std::uint64_t Rotl(std::uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }
inline std::uint64_t Load64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
// One word into one lane: xor, multiply (low bits move up), xorshift (high bits move down).
inline std::uint64_t LaneStep(std::uint64_t acc, std::uint64_t w) {
    acc = (acc ^ w) * K1;
    return acc ^ (acc >> 29);
}
inline std::uint64_t Avalanche(std::uint64_t h) {
    h ^= h >> 31; h *= K3;
    h ^= h >> 29; h *= K1;
    h ^= h >> 32;
    return h;
}
} // namespace

Hasher::Hasher(std::uint64_t seed) : m_seed(seed) {
    for (int i = 0; i < 4; ++i) m_lane[i] = seed + K0 * (std::uint64_t)(i + 1);
}

void Hasher::Update(const void* data, std::size_t size) {
    if (size == 0) return;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    m_total += size;
    if (m_used) {
        const std::size_t take = std::min(size, 32 - m_used);
        std::memcpy(m_buf + m_used, p, take);
        m_used += take; p += take; size -= take;
        if (m_used < 32) return;
        for (int i = 0; i < 4; ++i) m_lane[i] = LaneStep(m_lane[i], Load64(m_buf + 8 * i));
        m_used = 0;
    }
    while (size >= 32) {
        m_lane[0] = LaneStep(m_lane[0], Load64(p));
        m_lane[1] = LaneStep(m_lane[1], Load64(p + 8));
        m_lane[2] = LaneStep(m_lane[2], Load64(p + 16));
        m_lane[3] = LaneStep(m_lane[3], Load64(p + 24));
        p += 32; size -= 32;
    }
    if (size) { std::memcpy(m_buf, p, size); m_used = size; }
}

std::uint64_t Hasher::Final() const {
    std::uint64_t h;
    if (m_total >= 32) {
        h = K4;
        for (int i = 0; i < 4; ++i) h = (h ^ Avalanche(m_lane[i])) * K0;
    } else {
        h = m_seed ^ K4;
    }
    h ^= m_total * K2;
    std::size_t i = 0;
    for (; i + 8 <= m_used; i += 8) h = Rotl(h ^ (Load64(m_buf + i) * K3), 27) * K1;
    if (i < m_used) {
        std::uint64_t w = 0;
        for (std::size_t k = m_used; k > i; --k) w = (w << 8) | m_buf[k - 1];
        h = Rotl(h ^ (w * K4), 23) * K2;
    }
    return Avalanche(h);
}

std::uint64_t Hash64(const std::uint8_t* data, std::size_t size, std::uint64_t seed) {
    Hasher h(seed);
    h.Update(data, size);
    return h.Final();
}

std::string NormalizeName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    std::size_t i = 0;
    while (i < name.size()) {
        std::size_t j = i;
        while (j < name.size() && name[j] != '/' && name[j] != '\\') ++j;
        const std::string_view part = name.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (!out.empty()) out += '/';
            out.append(part.data(), part.size());
        }
        i = j + 1;
    }
    return out;
}

AssetId IdOf(std::string_view name) {
    const std::string key = str::LowerAscii(NormalizeName(name));
    const AssetId id = Hash64(std::string_view(key), kIdSeed);
    return id == 0 ? 1 : id;
}

std::string Hex64(std::uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) { s[(std::size_t)i] = d[v & 15]; v >>= 4; }
    return s;
}

bool IsValidName(std::string_view name, std::string* why) {
    auto no = [&](const std::string& w) { if (why) *why = "'" + std::string(name.substr(0, 200)) + "' " + w; return false; };
    if (name.empty()) return no("is empty");
    if (name.size() > 1024) return no("is longer than 1024 bytes");
    if (NormalizeName(name) != name) return no("is not a plain '/'-separated name (no '\\', '//', './' or leading '/')");
    const pathsafety::Why w = pathsafety::Check(name, pathsafety::Mode::Portable);
    if (w != pathsafety::Why::Ok) return no(std::string("is not a safe file name: ") + pathsafety::Describe(w));
    return true;
}

// ---- small byte helpers -------------------------------------------------------------------------------------
namespace {

void Put16(unsigned char* p, std::uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
void Put32(unsigned char* p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i)); }
void Put64(unsigned char* p, std::uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i)); }
std::uint16_t Get16(const unsigned char* p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }
std::uint32_t Get32(const unsigned char* p) { return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); }
std::uint64_t Get64(const unsigned char* p) { return Load64(p); }

const char kMagic[8] = {'O', 'K', 'A', 'Y', 'P', 'K', 'G', 0};
const char kTail[8] = {'O', 'K', 'P', 'K', 'E', 'N', 'D', 0};
// The compressed copies in a cache depend on the compressor: bump this when ImageWrite::Deflate's output changes, so
// an old cache is not mixed with new output (the same input must always give the same package).
constexpr unsigned kCompressorVersion = 1;
constexpr std::size_t kCacheHeader = 32;

bool IsPow2(std::uint64_t v) { return v && !(v & (v - 1)); }

// a + b <= limit, without overflow; the sum in `out`.
bool AddWithin(std::uint64_t a, std::uint64_t b, std::uint64_t limit, std::uint64_t& out) {
    if (a > limit || b > limit - a) return false;
    out = a + b;
    return true;
}

// A file opened for binary reading or writing with UTF-8 names on Windows and 64-bit offsets everywhere.
struct File {
    std::FILE* f = nullptr;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { Close(); }
    bool Open(const fs::path& p, bool write) {
#if defined(_WIN32)
        f = _wfopen(p.c_str(), write ? L"wb" : L"rb");
#else
        f = std::fopen(p.c_str(), write ? "wb" : "rb");
#endif
        return f != nullptr;
    }
    bool Seek(std::uint64_t at) {
#if defined(_WIN32)
        return _fseeki64(f, (long long)at, SEEK_SET) == 0;
#else
        return fseeko(f, (off_t)at, SEEK_SET) == 0;
#endif
    }
    bool Read(void* dst, std::size_t n) { return n == 0 || std::fread(dst, 1, n, f) == n; }
    bool Write(const void* src, std::size_t n) { return n == 0 || std::fwrite(src, 1, n, f) == n; }
    bool Close() {
        if (!f) return true;
        const bool ok = std::fclose(f) == 0;
        f = nullptr;
        return ok;
    }
};

std::string PathText(const std::string& p) { return p; }

bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& out, std::uint64_t maxBytes, std::string& why) {
    std::error_code ec;
    const fs::path fp = fs::u8path(path);
    const std::uintmax_t size = fs::file_size(fp, ec);
    if (ec) { why = "cannot read " + PathText(path) + " (" + ec.message() + ")"; return false; }
    if (size > maxBytes) { why = PathText(path) + " is too big for one package entry"; return false; }
    File f;
    if (!f.Open(fp, false)) { why = "cannot open " + PathText(path); return false; }
    out.resize((std::size_t)size);
    if (!f.Read(out.data(), out.size())) { why = "cannot read " + PathText(path); return false; }
    return true;
}

// Extensions whose files are already compressed: DEFLATE is not tried on them (it would only cost time).
bool AlreadyCompressed(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = str::LowerAscii(name.substr(dot + 1));
    static const char* list[] = {"png", "jpg", "jpeg", "gif", "webp", "ogg", "mp3", "opus", "flac", "zip", "gz",
                                 "br", "7z", "mp4", "webm", "okaypkg", "ktx2"};
    for (const char* e : list) if (ext == e) return true;
    return false;
}

} // namespace

// ---- Writer ---------------------------------------------------------------------------------------------------

bool PackageWriter::AddItem(Item item, std::string* why) {
    if (!IsValidName(item.name, why)) return false;
    item.id = IdOf(item.name);
    std::vector<std::string> deps;
    std::vector<AssetId> depIds;
    for (const std::string& d : item.deps) {
        const std::string n = NormalizeName(d);
        if (!IsValidName(n, why)) { if (why) *why = "a dependency of " + item.name + ": " + *why; return false; }
        const AssetId did = IdOf(n);
        if (did == item.id || std::find(depIds.begin(), depIds.end(), did) != depIds.end()) continue;
        depIds.push_back(did);
        deps.push_back(n);
    }
    item.deps.swap(deps);
    auto it = std::lower_bound(m_items.begin(), m_items.end(), item.id, [](const Item& a, AssetId id) { return a.id < id; });
    if (it != m_items.end() && it->id == item.id) {
        if (why) {
            if (it->name == item.name) *why = "'" + item.name + "' is in the package twice";
            else {
                const std::string a = str::LowerAscii(it->name), b = str::LowerAscii(item.name);
                *why = "'" + it->name + "' and '" + item.name + "' have the same asset id " + Hex64(item.id) +
                       (a == b ? ": the names differ only in letter case, which is one file on Windows. Rename one of them."
                               : ": a 64-bit id collision. Rename one of them.");
            }
        }
        return false;
    }
    m_items.insert(it, std::move(item));
    return true;
}

bool PackageWriter::Add(std::string_view name, std::vector<std::uint8_t> bytes, std::vector<std::string> dependencies,
                        std::uint8_t kind, std::string* why) {
    Item i;
    i.name = std::string(name); i.bytes = std::move(bytes); i.deps = std::move(dependencies); i.kind = kind;
    return AddItem(std::move(i), why);
}

bool PackageWriter::AddFile(std::string_view name, std::string file, std::vector<std::string> dependencies,
                            std::uint8_t kind, std::string* why) {
    Item i;
    i.name = std::string(name); i.file = std::move(file); i.deps = std::move(dependencies); i.kind = kind;
    if (i.file.empty()) { if (why) *why = "no source file for " + i.name; return false; }
    return AddItem(std::move(i), why);
}

bool PackageWriter::AddRemoval(std::string_view name, std::string* why) {
    Item i;
    i.name = std::string(name); i.removal = true;
    return AddItem(std::move(i), why);
}

bool PackageWriter::Has(std::string_view name) const {
    const AssetId id = IdOf(name);
    auto it = std::lower_bound(m_items.begin(), m_items.end(), id, [](const Item& a, AssetId v) { return a.id < v; });
    return it != m_items.end() && it->id == id;
}

namespace {

// Where the package goes: a file (written in order, the header filled in at the end) or memory.
struct Sink {
    File* file = nullptr;
    std::vector<std::uint8_t>* mem = nullptr;
    std::uint64_t pos = 0;
    bool Write(const void* p, std::size_t n) {
        if (mem) { const auto* b = static_cast<const std::uint8_t*>(p); mem->insert(mem->end(), b, b + n); }
        else if (!file->Write(p, n)) return false;
        pos += n;
        return true;
    }
    bool Zeros(std::size_t n) {
        static const unsigned char z[256] = {};
        while (n) { const std::size_t k = std::min(n, sizeof(z)); if (!Write(z, k)) return false; n -= k; }
        return true;
    }
    bool PatchHeader(const unsigned char* h) {
        if (mem) { std::memcpy(mem->data(), h, kHeaderBytes); return true; }
        return file->Seek(0) && file->Write(h, kHeaderBytes);
    }
};

// What one entry becomes: its content hash and the bytes that go into the package.
struct Packed {
    std::uint64_t size = 0, hash = 0;
    Method method = Method::Stored;
    std::vector<std::uint8_t> owned;          // compressed bytes, or the file's bytes
    const std::vector<std::uint8_t>* stored = nullptr;   // what is written (points at owned or at the item's bytes)
    bool fromCache = false;
    std::string error;
};

std::string CachePath(const std::string& dir, std::uint64_t hash, std::uint64_t size) {
    return (fs::u8path(dir) / (Hex64(hash) + "-" + Hex64(size) + "-" + std::to_string(kCompressorVersion) + ".okpc")).u8string();
}

// A cache record: "OKPC", version, method, 2 zero bytes, content hash, size, stored bytes; then the compressed bytes.
bool CacheLoad(const std::string& dir, std::uint64_t hash, std::uint64_t size, Packed& p) {
    const fs::path fp = fs::u8path(CachePath(dir, hash, size));
    std::error_code ec;
    const std::uintmax_t fsz = fs::file_size(fp, ec);
    if (ec || fsz < kCacheHeader || fsz - kCacheHeader >= size) return false;
    File f;
    unsigned char h[kCacheHeader];
    if (!f.Open(fp, false) || !f.Read(h, sizeof(h))) return false;
    if (std::memcmp(h, "OKPC", 4) != 0 || h[4] != 1 || Get64(h + 8) != hash || Get64(h + 16) != size) return false;
    const std::uint64_t stored = Get64(h + 24);
    if (h[5] == (unsigned char)Method::Stored) {
        if (stored != 0 || fsz != kCacheHeader) return false;
        p.method = Method::Stored;
        p.fromCache = true;
        return true;
    }
    if (h[5] != (unsigned char)Method::Deflate || stored != fsz - kCacheHeader) return false;
    std::vector<std::uint8_t> b((std::size_t)stored);
    if (!f.Read(b.data(), b.size())) return false;
    p.method = Method::Deflate;
    p.owned.swap(b);
    p.fromCache = true;
    return true;
}

void CacheStore(const std::string& dir, std::uint64_t hash, std::uint64_t size, Method m, const std::vector<std::uint8_t>* bytes,
                std::size_t uniq) {
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    const fs::path target = fs::u8path(CachePath(dir, hash, size));
    fs::path tmp = target;
    tmp += "." + std::to_string(uniq) + ".part";
    unsigned char h[kCacheHeader] = {};
    std::memcpy(h, "OKPC", 4);
    h[4] = 1; h[5] = (unsigned char)m;
    Put64(h + 8, hash); Put64(h + 16, size); Put64(h + 24, bytes ? bytes->size() : 0);
    File f;
    if (!f.Open(tmp, true)) return;
    bool ok = f.Write(h, sizeof(h)) && (!bytes || f.Write(bytes->data(), bytes->size()));
    ok = f.Close() && ok;
    std::error_code e2;
    if (!ok || !savefmt::MoveOver(tmp, target, e2)) fs::remove(tmp, e2);   // a cache that cannot be written only costs time
}

void PackOne(const PackageWriter::Item& it, const WriteOptions& o, std::size_t index, Packed& p) {
    const std::vector<std::uint8_t>* src = &it.bytes;
    if (!it.file.empty()) {
        if (!ReadWholeFile(it.file, p.owned, ~0ull, p.error)) return;
        src = &p.owned;
    }
    p.size = src->size();
    p.hash = Hash64(src->data(), src->size());
    p.stored = src;
    p.method = Method::Stored;
    if (!o.compress || p.size < 64 || AlreadyCompressed(it.name)) return;
    if (!o.cacheDir.empty() && o.trustCache) {
        Packed c;
        if (CacheLoad(o.cacheDir, p.hash, p.size, c)) {
            p.fromCache = true;
            if (c.method == Method::Deflate) {
                p.method = Method::Deflate;
                p.owned.swap(c.owned);   // the source bytes (if they were the file's) are no longer needed
                p.stored = &p.owned;
            }
            return;
        }
    }
    std::vector<std::uint8_t> z = ImageWrite::Deflate(src->data(), src->size());
    const bool pays = !z.empty() && z.size() <= p.size - p.size / 32 && z.size() < p.size;
    if (pays) {
        p.method = Method::Deflate;
        p.owned.swap(z);
        p.stored = &p.owned;
    }
    if (!o.cacheDir.empty()) CacheStore(o.cacheDir, p.hash, p.size, p.method, pays ? &p.owned : nullptr, index);
}

bool WriteImpl(const std::vector<PackageWriter::Item>& items, Sink& out, const WriteOptions& o, WriteStats* stats, std::string* why) {
    auto fail = [&](const std::string& w) { if (why) *why = w; return false; };
    const auto t0 = std::chrono::steady_clock::now();
    if (!IsPow2(o.alignment) || o.alignment > 65536) return fail("the alignment must be a power of two from 1 to 65536");
    const bool patch = o.baseStackHash != 0;
    if (items.size() > (std::size_t)Limits().maxEntries) return fail("too many entries for one package");
    for (const auto& it : items)
        if (it.removal && !patch) return fail("'" + it.name + "' is a removal, and only a patch package may remove entries");

    WriteStats st;
    std::vector<unsigned char> toc(items.size() * kEntryBytes, 0);
    std::string names;
    std::vector<std::uint8_t> deps;
    Hasher dataHash;
    unsigned char header[kHeaderBytes] = {};
    if (!out.Write(header, sizeof(header))) return fail("cannot write the package (is the disk full?)");
    const std::uint64_t dataOffset = kHeaderBytes;

    // A batch at a time: read and compress (on workers when given), then write in id order.
    std::size_t next = 0;
    while (next < items.size()) {
        std::size_t end = next;
        std::uint64_t batchBytes = 0;
        while (end < items.size() && end - next < 256 && batchBytes < (64ull << 20)) {
            const auto& it = items[end];
            if (!it.file.empty()) { std::error_code ec; const auto s = fs::file_size(fs::u8path(it.file), ec); batchBytes += ec ? 0 : s; }
            else batchBytes += it.bytes.size();
            ++end;
        }
        std::vector<Packed> packed(end - next);
        auto work = [&](std::size_t i) { if (!items[next + i].removal) PackOne(items[next + i], o, next + i, packed[i]); };
        if (o.jobs && o.jobs->WorkerCount() > 0) o.jobs->ParallelFor(0, packed.size(), 1, work);
        else for (std::size_t i = 0; i < packed.size(); ++i) work(i);
        for (std::size_t i = 0; i < packed.size(); ++i) {
            const auto& it = items[next + i];
            Packed& p = packed[i];
            if (!p.error.empty()) return fail(p.error);
            unsigned char* rec = toc.data() + (next + i) * kEntryBytes;
            Put64(rec, it.id);
            rec[57] = it.kind;
            if (it.removal) {
                rec[56] = (unsigned char)Method::Removed;
                ++st.removed;
            } else {
                const std::uint64_t at = (out.pos + o.alignment - 1) / o.alignment * o.alignment;
                const std::size_t pad = (std::size_t)(at - out.pos);
                static const unsigned char zeros[256] = {};
                for (std::size_t k = pad; k;) { const std::size_t n = std::min(k, sizeof(zeros)); dataHash.Update(zeros, n); k -= n; }
                if (!out.Zeros(pad)) return fail("cannot write the package (is the disk full?)");
                const std::vector<std::uint8_t>& b = *p.stored;
                dataHash.Update(b.data(), b.size());
                if (!out.Write(b.data(), b.size())) return fail("cannot write the package (is the disk full?)");
                Put64(rec + 8, at);
                Put64(rec + 16, b.size());
                Put64(rec + 24, p.size);
                Put64(rec + 32, p.hash);
                rec[56] = (unsigned char)p.method;
                st.bytesIn += p.size;
                if (p.method == Method::Deflate) ++st.compressed;
                if (p.fromCache) ++st.fromCache;
                ++st.entries;
            }
            if (o.keepNames) {
                if (names.size() + it.name.size() > 0xFFFFFFFFull) return fail("the names are too long for one package");
                Put32(rec + 40, (std::uint32_t)names.size());
                Put32(rec + 44, (std::uint32_t)it.name.size());
                names += it.name;
            }
            std::vector<AssetId> ids;
            for (const std::string& d : it.deps) ids.push_back(IdOf(d));
            std::sort(ids.begin(), ids.end());
            if (deps.size() / 8 + ids.size() > 0xFFFFFFFFull) return fail("too many dependencies for one package");
            Put32(rec + 48, (std::uint32_t)(deps.size() / 8));
            Put32(rec + 52, (std::uint32_t)ids.size());
            for (AssetId d : ids) { unsigned char b8[8]; Put64(b8, d); deps.insert(deps.end(), b8, b8 + 8); }
        }
        next = end;
    }

    const std::uint64_t dataBytes = out.pos - dataOffset;
    const std::uint64_t tocOffset = out.pos;
    const std::uint64_t namesOffset = tocOffset + toc.size();
    const std::uint64_t depsOffset = namesOffset + names.size();
    if (!out.Write(toc.data(), toc.size()) || !out.Write(names.data(), names.size()) || !out.Write(deps.data(), deps.size()))
        return fail("cannot write the package (is the disk full?)");
    std::memcpy(header, kMagic, 8);
    Put16(header + 8, kVersion);
    Put16(header + 10, (std::uint16_t)kHeaderBytes);
    Put32(header + 12, (o.keepNames ? kHasNames : 0u) | (patch ? kIsPatch : 0u));
    Put32(header + 16, (std::uint32_t)items.size());
    Put32(header + 20, (std::uint32_t)kEntryBytes);
    Put32(header + 24, o.alignment);
    Put32(header + 28, 0);
    Put64(header + 32, dataOffset);
    Put64(header + 40, dataBytes);
    Put64(header + 48, tocOffset);
    Put64(header + 56, namesOffset);
    Put64(header + 64, names.size());
    Put64(header + 72, depsOffset);
    Put64(header + 80, deps.size());
    Put64(header + 88, o.baseStackHash);
    Hasher meta;
    meta.Update(header, kHeaderBytes);
    meta.Update(toc.data(), toc.size());
    meta.Update(names.data(), names.size());
    meta.Update(deps.data(), deps.size());
    unsigned char tail[kTrailerBytes];
    std::memcpy(tail, kTail, 8);
    Put64(tail + 8, out.pos + kTrailerBytes);
    Put64(tail + 16, meta.Final());
    Put64(tail + 24, dataHash.Final());
    if (!out.Write(tail, sizeof(tail)) || !out.PatchHeader(header)) return fail("cannot write the package (is the disk full?)");
    st.bytesOut = out.pos;
    st.metaHash = Get64(tail + 16);
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (stats) *stats = st;
    return true;
}

} // namespace

bool PackageWriter::WriteToMemory(std::vector<std::uint8_t>& out, const WriteOptions& options, WriteStats* stats,
                                  std::string* why) const {
    out.clear();
    Sink s;
    s.mem = &out;
    if (!WriteImpl(m_items, s, options, stats, why)) { out.clear(); return false; }
    return true;
}

bool PackageWriter::Write(const std::string& path, const WriteOptions& options, WriteStats* stats, std::string* why) const {
    const fs::path target = fs::u8path(path);
    fs::path tmp = target;
    tmp += ".part";
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    File f;
    if (!f.Open(tmp, true)) { if (why) *why = "cannot create " + tmp.u8string(); return false; }
    Sink s;
    s.file = &f;
    bool ok = WriteImpl(m_items, s, options, stats, why);
    if (!f.Close() && ok) { ok = false; if (why) *why = "cannot finish writing " + tmp.u8string() + " (is the disk full?)"; }
    if (ok && !savefmt::MoveOver(tmp, target, ec)) { ok = false; if (why) *why = "cannot replace " + path + " (" + ec.message() + ")"; }
    if (!ok) fs::remove(tmp, ec);
    return ok;
}

// ---- Reader -----------------------------------------------------------------------------------------------------

struct Package::Source {
    std::vector<std::uint8_t> mem;
    bool inMemory = false;
    File file;
    std::uint64_t size = 0;
    mutable corethreads::Mutex lock;   // one read at a time on the shared file handle
};

Package::~Package() = default;

std::shared_ptr<Package> Package::OpenFile(const std::string& path, std::string& why, const Limits& limits) {
    std::shared_ptr<Package> p(new Package());
    p->m_src = std::make_unique<Source>();
    p->m_path = path;
    const fs::path fp = fs::u8path(path);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(fp, ec);
    if (ec) { why = "cannot open the package " + path + " (" + ec.message() + ")"; return nullptr; }
    if (!p->m_src->file.Open(fp, false)) { why = "cannot open the package " + path; return nullptr; }
    p->m_src->size = size;
    if (!p->Load(why, limits)) { why = path + ": " + why; return nullptr; }
    return p;
}

std::shared_ptr<Package> Package::OpenMemory(std::vector<std::uint8_t> bytes, std::string& why, const Limits& limits) {
    std::shared_ptr<Package> p(new Package());
    p->m_src = std::make_unique<Source>();
    p->m_src->inMemory = true;
    p->m_src->size = bytes.size();
    p->m_src->mem = std::move(bytes);
    if (!p->Load(why, limits)) return nullptr;
    return p;
}

bool Package::ReadAt(std::uint64_t offset, void* dst, std::size_t size, std::string& why) const {
    std::uint64_t end = 0;
    if (!AddWithin(offset, size, m_src->size, end)) { why = "a read past the end of the package"; return false; }
    if (size == 0) return true;
    if (m_src->inMemory) { std::memcpy(dst, m_src->mem.data() + offset, size); return true; }
    corethreads::LockGuard g(m_src->lock);
    if (!m_src->file.Seek(offset) || !m_src->file.Read(dst, size)) { why = "cannot read the package file (was it changed or removed while the game runs?)"; return false; }
    return true;
}

bool Package::Load(std::string& why, const Limits& limits) {
    m_limits = limits;
    const std::uint64_t size = m_src->size;
    if (size < kHeaderBytes + kTrailerBytes) { why = "not an asset package (only " + std::to_string(size) + " bytes)"; return false; }
    unsigned char h[kHeaderBytes], t[kTrailerBytes];
    if (!ReadAt(0, h, sizeof(h), why) || !ReadAt(size - kTrailerBytes, t, sizeof(t), why)) return false;
    if (std::memcmp(h, kMagic, 8) != 0) { why = "not an asset package (it does not start with OKAYPKG)"; return false; }
    Header& H = m_header;
    H.version = Get16(h + 8);
    if (H.version > kVersion) { why = "made by a newer engine (package format " + std::to_string(H.version) + "; this one reads " + std::to_string(kVersion) + "): update the game"; return false; }
    if (H.version == 0) { why = "damaged: the format version is 0"; return false; }
    auto bad = [&](const std::string& what) { why = "damaged: " + what; return false; };
    if (Get16(h + 10) != kHeaderBytes) return bad("the header size is wrong");
    H.flags = Get32(h + 12);
    H.entryCount = Get32(h + 16);
    if (Get32(h + 20) != kEntryBytes) return bad("the table record size is wrong");
    H.alignment = Get32(h + 24);
    if (Get32(h + 28) != 0) return bad("a reserved header field is not zero");
    H.dataOffset = Get64(h + 32); H.dataBytes = Get64(h + 40); H.tocOffset = Get64(h + 48);
    H.namesOffset = Get64(h + 56); H.namesBytes = Get64(h + 64); H.depsOffset = Get64(h + 72); H.depsBytes = Get64(h + 80);
    H.baseStackHash = Get64(h + 88);
    if (std::memcmp(t, kTail, 8) != 0) return bad("the end marker is missing (the file is cut short or has extra bytes)");
    H.fileSize = Get64(t + 8); H.metaHash = Get64(t + 16); H.dataHash = Get64(t + 24);
    if (H.fileSize != size) return bad("the file is " + std::to_string(size) + " bytes, the package says " + std::to_string(H.fileSize));
    if (H.flags & ~(std::uint32_t)(kHasNames | kIsPatch)) return bad("unknown flags are set");
    if (!IsPow2(H.alignment) || H.alignment > 65536) return bad("the alignment is not a power of two up to 65536");
    if (H.entryCount > limits.maxEntries) return bad("too many entries (" + std::to_string(H.entryCount) + ")");
    if (((H.flags & kIsPatch) != 0) != (H.baseStackHash != 0)) return bad("the patch flag and the base hash disagree");
    if ((H.flags & kHasNames) == 0 && H.namesBytes != 0) return bad("a name table without the names flag");
    if (H.namesBytes > limits.maxNamesBytes) return bad("the name table is too big");
    if (H.depsBytes > limits.maxDepsBytes || H.depsBytes % 8 != 0) return bad("the dependency table has a bad size");
    // The regions follow each other exactly: header, data, table, names, dependencies, trailer.
    const std::uint64_t body = size - kTrailerBytes;
    std::uint64_t at = 0;
    if (H.dataOffset != kHeaderBytes) return bad("the data does not start after the header");
    if (!AddWithin(H.dataOffset, H.dataBytes, body, at) || at != H.tocOffset) return bad("the data region does not fit the file");
    const std::uint64_t tocBytes = (std::uint64_t)H.entryCount * kEntryBytes;
    if (!AddWithin(H.tocOffset, tocBytes, body, at) || at != H.namesOffset) return bad("the table of contents does not fit the file");
    if (!AddWithin(H.namesOffset, H.namesBytes, body, at) || at != H.depsOffset) return bad("the name table does not fit the file");
    if (!AddWithin(H.depsOffset, H.depsBytes, body, at) || at != body) return bad("the dependency table does not fit the file");

    // Everything is inside the file and under the limits: read the tables and check their hash.
    std::vector<unsigned char> toc((std::size_t)tocBytes);
    m_names.assign((std::size_t)H.namesBytes, '\0');
    std::vector<unsigned char> deps((std::size_t)H.depsBytes);
    if (!ReadAt(H.tocOffset, toc.data(), toc.size(), why) || !ReadAt(H.namesOffset, m_names.data(), m_names.size(), why) ||
        !ReadAt(H.depsOffset, deps.data(), deps.size(), why)) return false;
    Hasher meta;
    meta.Update(h, kHeaderBytes);
    meta.Update(toc.data(), toc.size());
    meta.Update(m_names.data(), m_names.size());
    meta.Update(deps.data(), deps.size());
    if (meta.Final() != H.metaHash) return bad("the table of contents does not match its checksum");
    m_deps.resize(deps.size() / 8);
    for (std::size_t i = 0; i < m_deps.size(); ++i) m_deps[i] = Get64(deps.data() + i * 8);

    const bool names = (H.flags & kHasNames) != 0, patch = (H.flags & kIsPatch) != 0;
    m_entries.resize(H.entryCount);
    for (std::uint32_t i = 0; i < H.entryCount; ++i) {
        const unsigned char* r = toc.data() + (std::size_t)i * kEntryBytes;
        Entry& e = m_entries[i];
        e.id = Get64(r); e.offset = Get64(r + 8); e.storedBytes = Get64(r + 16); e.size = Get64(r + 24); e.contentHash = Get64(r + 32);
        e.nameOffset = Get32(r + 40); e.nameBytes = Get32(r + 44); e.depsFirst = Get32(r + 48); e.depsCount = Get32(r + 52);
        const unsigned m = r[56];
        e.kind = r[57];
        const std::string which = "entry " + std::to_string(i) + " (" + Hex64(e.id) + ")";
        if (Get16(r + 58) != 0 || Get32(r + 60) != 0) return bad(which + " has unknown flags");
        if (e.id == 0) return bad(which + " has the id 0");
        if (i > 0 && m_entries[i - 1].id >= e.id) return bad(which + " is out of order or repeats an id");
        if (m > (unsigned)Method::Removed) return bad(which + " has an unknown compression method " + std::to_string(m));
        e.method = (Method)m;
        if (e.method == Method::Removed) {
            if (!patch) return bad(which + " is a removal in a base package");
            if (e.offset || e.storedBytes || e.size || e.contentHash) return bad(which + " is a removal with data");
        } else {
            std::uint64_t endAt = 0;
            if (e.offset < H.dataOffset || !AddWithin(e.offset, e.storedBytes, H.dataOffset + H.dataBytes, endAt))
                return bad(which + " lies outside the data region");
            if (e.offset % H.alignment != 0) return bad(which + " is not aligned");
            if (e.size > limits.maxEntryBytes) return bad(which + " is bigger than allowed (" + std::to_string(e.size) + " bytes)");
            if (e.method == Method::Stored && e.storedBytes != e.size) return bad(which + " is stored with the wrong size");
            if (e.method == Method::Deflate) {
                if (e.storedBytes >= e.size) return bad(which + " is compressed but not smaller");
                // DEFLATE's densest code is a 258-byte copy in about 2 bits, so one stored byte gives at most about
                // 1032 bytes. A claim beyond that (plus slack) is refused before anything is allocated for it.
                if (e.size / 1032 > e.storedBytes + 64) return bad(which + " claims to unpack to more than DEFLATE can produce");
            }
        }
        if (names) {
            std::uint64_t nameEnd = 0;
            if (e.nameBytes == 0 || e.nameBytes > 1024 || !AddWithin(e.nameOffset, e.nameBytes, H.namesBytes, nameEnd))
                return bad(which + " has its name outside the name table");
            const std::string_view nm(m_names.data() + e.nameOffset, e.nameBytes);
            std::string w;
            if (!IsValidName(nm, &w)) return bad(which + ": " + w);
            if (IdOf(nm) != e.id) return bad(which + ": its name '" + std::string(nm) + "' does not give its id (damaged, or two names collide)");
        } else if (e.nameOffset || e.nameBytes) {
            return bad(which + " has a name in a package without names");
        }
        std::uint64_t depEnd = 0;
        if (e.depsCount > limits.maxDepsPerEntry || !AddWithin(e.depsFirst, e.depsCount, m_deps.size(), depEnd))
            return bad(which + " has its dependencies outside the dependency table");
    }
    return true;
}

const Entry* Package::Find(AssetId id) const {
    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), id, [](const Entry& e, AssetId v) { return e.id < v; });
    return it != m_entries.end() && it->id == id ? &*it : nullptr;
}

std::string_view Package::NameOf(const Entry& e) const {
    if (!HasNames() || (std::uint64_t)e.nameOffset + e.nameBytes > m_names.size()) return std::string_view();
    return std::string_view(m_names.data() + e.nameOffset, e.nameBytes);
}

std::vector<AssetId> Package::DependenciesOf(const Entry& e) const {
    if ((std::uint64_t)e.depsFirst + e.depsCount > m_deps.size()) return {};
    return std::vector<AssetId>(m_deps.begin() + e.depsFirst, m_deps.begin() + e.depsFirst + e.depsCount);
}

bool Package::Read(const Entry& e, std::vector<std::uint8_t>& out, std::string& why) const {
    out.clear();
    const std::string label = HasNames() ? std::string(NameOf(e)) : Hex64(e.id);
    if (e.method == Method::Removed) { why = label + " was removed by a patch"; return false; }
    if (e.method == Method::Stored) {
        out.resize((std::size_t)e.size);
        if (!ReadAt(e.offset, out.data(), out.size(), why)) { out.clear(); return false; }
    } else {
        std::vector<std::uint8_t> in((std::size_t)e.storedBytes);
        if (!ReadAt(e.offset, in.data(), in.size(), why)) return false;
        inflate::Options op;
        op.maxOutput = (std::size_t)e.size;
        op.hint = (std::size_t)e.size;
        std::size_t used = 0;
        std::string w;
        if (!inflate::Raw(in.data(), in.size(), out, op, used, w) || out.size() != e.size || used != in.size()) {
            out.clear();
            why = "the package is damaged: " + label + " does not unpack" + (w.empty() ? std::string() : " (" + w + ")");
            return false;
        }
    }
    if (Hash64(out.data(), out.size()) != e.contentHash) {
        out.clear();
        why = "the package is damaged: " + label + " does not match its checksum";
        return false;
    }
    return true;
}

bool Package::VerifyAll(std::string& why) const {
    Hasher h;
    std::vector<unsigned char> buf(1 << 20);
    for (std::uint64_t at = 0; at < m_header.dataBytes;) {
        const std::size_t n = (std::size_t)std::min<std::uint64_t>(buf.size(), m_header.dataBytes - at);
        if (!ReadAt(m_header.dataOffset + at, buf.data(), n, why)) return false;
        h.Update(buf.data(), n);
        at += n;
    }
    if (h.Final() != m_header.dataHash) { why = "the package is damaged: its data does not match its checksum"; return false; }
    std::vector<std::uint8_t> out;
    for (const Entry& e : m_entries)
        if (e.method != Method::Removed && !Read(e, out, why)) return false;
    return true;
}

// ---- PackageStack --------------------------------------------------------------------------------------------------

namespace {
std::uint64_t ChainHash(std::uint64_t so, std::uint64_t meta) {
    unsigned char b[16];
    Put64(b, so); Put64(b + 8, meta);
    return Hash64(b, sizeof(b));
}
std::string Label(const Package& p) { return p.Path().empty() ? std::string("the package") : fs::u8path(p.Path()).filename().u8string(); }
} // namespace

bool PackageStack::Mount(std::shared_ptr<const Package> package, std::string& why) {
    if (!package) { why = "no package"; return false; }
    if (m_layers.empty()) {
        if (package->IsPatch()) { why = Label(*package) + " is a patch: mount the game's base package first"; return false; }
        m_stackHash = package->Info().metaHash;
    } else {
        if (!package->IsPatch()) { why = Label(*package) + " is a base package: only patches go on top of the base"; return false; }
        if (package->Info().baseStackHash != m_stackHash) {
            why = Label(*package) + " was made for other versions of the game's packages (it expects " + Hex64(package->Info().baseStackHash) +
                  ", these are " + Hex64(m_stackHash) + ")";
            return false;
        }
        m_stackHash = ChainHash(m_stackHash, package->Info().metaHash);
    }
    m_layers.push_back(std::move(package));
    return true;
}

bool PackageStack::Find(AssetId id, Hit& out) const {
    for (std::size_t i = m_layers.size(); i-- > 0;) {
        const Entry* e = m_layers[i]->Find(id);
        if (!e) continue;
        if (e->method == Method::Removed) return false;
        out.package = m_layers[i].get(); out.entry = e; out.layer = i;
        return true;
    }
    return false;
}

bool PackageStack::Read(AssetId id, std::vector<std::uint8_t>& out, std::string& why) const {
    Hit h;
    if (!Find(id, h)) { out.clear(); why = "no asset " + Hex64(id) + " in the game's packages"; return false; }
    return h.package->Read(*h.entry, out, why);
}

std::string PackageStack::NameOf(AssetId id) const {
    Hit h;
    if (!Find(id, h)) return std::string();
    for (std::size_t i = m_layers.size(); i-- > 0;) {
        const Entry* e = m_layers[i]->Find(id);
        if (e && m_layers[i]->HasNames()) return std::string(m_layers[i]->NameOf(*e));
    }
    return std::string();
}

std::vector<PackageStack::Hit> PackageStack::Visible() const {
    struct Row { AssetId id; std::size_t layer; const Entry* e; };
    std::vector<Row> rows;
    for (std::size_t l = 0; l < m_layers.size(); ++l)
        for (const Entry& e : m_layers[l]->Entries()) rows.push_back({e.id, l, &e});
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id != b.id ? a.id < b.id : a.layer > b.layer; });
    std::vector<Hit> out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i > 0 && rows[i - 1].id == rows[i].id) continue;   // a lower layer's copy
        if (rows[i].e->method == Method::Removed) continue;
        out.push_back({m_layers[rows[i].layer].get(), rows[i].e, rows[i].layer});
    }
    return out;
}

std::vector<AssetId> PackageStack::Closure(AssetId root) const {
    std::vector<AssetId> out;
    Hit h;
    if (!Find(root, h)) return out;
    std::unordered_set<AssetId> seen;
    struct Frame { AssetId id; std::vector<AssetId> deps; std::size_t next = 0; };
    std::vector<Frame> stack;   // depth-first without recursion, so a long chain cannot overflow the C++ stack
    seen.insert(root);
    stack.push_back({root, h.package->DependenciesOf(*h.entry), 0});
    while (!stack.empty()) {
        Frame& f = stack.back();
        if (f.next < f.deps.size()) {
            const AssetId d = f.deps[f.next++];
            Hit dh;
            if (seen.count(d) || !Find(d, dh)) continue;
            seen.insert(d);
            stack.push_back({d, dh.package->DependenciesOf(*dh.entry), 0});
        } else {
            out.push_back(f.id);
            stack.pop_back();
        }
    }
    return out;
}

void PackageStack::ReadAsync(AssetId id, std::function<void(std::vector<std::uint8_t>&&, const std::string&)> done, JobSystem* jobs) const {
    JobSystem& js = jobs ? *jobs : EngineJobs();
    Hit h;
    std::shared_ptr<const Package> keep;
    if (Find(id, h)) keep = m_layers[h.layer];
    const Entry* e = h.entry;
    js.Submit([keep, e, id, done]() {
        std::vector<std::uint8_t> out;
        std::string why;
        if (!keep) why = "no asset " + Hex64(id) + " in the game's packages";
        else keep->Read(*e, out, why);
        if (done) done(std::move(out), why);
    });
}

// ---- Manifest ---------------------------------------------------------------------------------------------------

std::string WriteManifest(const std::vector<ManifestItem>& items) {
    std::string s = "okaypkg-manifest 1\n";
    for (const ManifestItem& i : items) s += i.file + " " + std::to_string(i.size) + " " + Hex64(i.hash) + "\n";
    return s;
}

bool ParseManifest(std::string_view text, std::vector<ManifestItem>& out, std::string& why) {
    out.clear();
    std::vector<std::string_view> lines;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t j = text.find('\n', i);
        if (j == std::string_view::npos) j = text.size();
        lines.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    if (lines.empty() || lines[0] != "okaypkg-manifest 1") { why = "packages.manifest: not a version 1 package list"; return false; }
    for (std::size_t li = 1; li < lines.size(); ++li) {
        const std::string_view l = lines[li];
        const std::string where = "packages.manifest line " + std::to_string(li + 1) + ": ";
        const std::size_t a = l.find(' '), b = a == std::string_view::npos ? a : l.find(' ', a + 1);
        if (a == std::string_view::npos || b == std::string_view::npos || l.find(' ', b + 1) != std::string_view::npos) { why = where + "needs a file, a size and a hash"; out.clear(); return false; }
        ManifestItem it;
        it.file = std::string(l.substr(0, a));
        const std::string_view sz = l.substr(a + 1, b - a - 1), hx = l.substr(b + 1);
        if (it.file.find('/') != std::string::npos || !IsValidName(it.file) || it.file.size() < 9 ||
            it.file.compare(it.file.size() - 8, 8, ".okaypkg") != 0) { why = where + "'" + it.file + "' is not a package file name"; out.clear(); return false; }
        if (sz.empty() || sz.size() > 19) { why = where + "bad size"; out.clear(); return false; }
        for (char c : sz) {
            if (c < '0' || c > '9') { why = where + "bad size"; out.clear(); return false; }
            it.size = it.size * 10 + (std::uint64_t)(c - '0');
        }
        if (hx.size() != 16) { why = where + "bad hash"; out.clear(); return false; }
        for (char c : hx) {
            const int v = str::HexDigitValue(c);
            if (v < 0) { why = where + "bad hash"; out.clear(); return false; }
            it.hash = (it.hash << 4) | (std::uint64_t)v;
        }
        for (const ManifestItem& o : out)
            if (IdOf(o.file) == IdOf(it.file)) { why = where + "'" + it.file + "' is listed twice"; out.clear(); return false; }
        out.push_back(std::move(it));
    }
    return true;
}

std::vector<ManifestItem> ChangedPackages(const std::vector<ManifestItem>& have, const std::vector<ManifestItem>& wanted) {
    std::vector<ManifestItem> out;
    for (const ManifestItem& w : wanted) {
        bool same = false;
        for (const ManifestItem& h : have) if (h.file == w.file && h.size == w.size && h.hash == w.hash) { same = true; break; }
        if (!same) out.push_back(w);
    }
    return out;
}

bool HashFile(const std::string& path, std::uint64_t& hash, std::uint64_t& size, std::string& why) {
    File f;
    if (!f.Open(fs::u8path(path), false)) { why = "cannot open " + path; return false; }
    Hasher h;
    std::vector<unsigned char> buf(1 << 20);
    size = 0;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), f.f);
        if (n) { h.Update(buf.data(), n); size += n; }
        if (n < buf.size()) {
            if (std::ferror(f.f)) { why = "cannot read " + path; return false; }
            break;
        }
    }
    hash = h.Final();
    return true;
}

// ---- Patches --------------------------------------------------------------------------------------------------------

bool MakePatch(const PackageStack& current, const std::vector<PatchSource>& wanted, PackageWriter& writer, std::size_t* changed,
               std::string& why) {
    std::size_t n = 0;
    std::unordered_set<AssetId> keep;
    for (const PatchSource& s : wanted) {
        const std::string name = NormalizeName(s.name);
        if (!IsValidName(name, &why)) return false;
        std::vector<std::uint8_t> bytes;
        if (!ReadWholeFile(s.file, bytes, Limits().maxEntryBytes, why)) return false;
        const AssetId id = IdOf(name);
        keep.insert(id);
        std::vector<AssetId> deps;
        for (const std::string& d : s.dependencies) { const AssetId di = IdOf(d); if (di != id) deps.push_back(di); }
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
        PackageStack::Hit h;
        if (current.Find(id, h) && h.entry->size == bytes.size() && h.entry->contentHash == Hash64(bytes.data(), bytes.size()) &&
            h.entry->kind == s.kind && h.package->DependenciesOf(*h.entry) == deps)
            continue;   // unchanged
        if (!writer.AddFile(name, s.file, s.dependencies, s.kind, &why)) return false;
        ++n;
    }
    for (const PackageStack::Hit& h : current.Visible()) {
        if (keep.count(h.entry->id)) continue;
        const std::string name = current.NameOf(h.entry->id);
        if (name.empty()) { why = "the current packages have no names, so a removal cannot be written"; return false; }
        if (!writer.AddRemoval(name, &why)) return false;
    }
    if (changed) *changed = n;
    return true;
}

} // namespace assetpkg
} // namespace okay
