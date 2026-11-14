#include "okay/Core/ThumbnailCache.hpp"
#include "AssetDatabaseInternal.hpp"
#include "okay/Graphics/Font.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Audio/AudioClip.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace fs = std::filesystem;
using adb::Lower;
using adb::PathFromU8;
using adb::U8;

// --- Image helpers -----------------------------------------------------------------------------

Image BoxDownscale(const Image& src, int maxSide) {
    if (!src.Valid() || maxSide < 1) return Image();
    const int w = src.Width(), h = src.Height();
    const int longest = std::max(w, h);
    if (longest <= maxSide) return src;
    const double scale = static_cast<double>(maxSide) / longest;
    const int nw = std::max(1, static_cast<int>(std::lround(w * scale)));
    const int nh = std::max(1, static_cast<int>(std::lround(h * scale)));
    Image out(nw, nh);
    const double sx = static_cast<double>(w) / nw, sy = static_cast<double>(h) / nh;
    const std::uint8_t* px = src.Data();
    std::uint8_t* dst = out.Data();
    for (int dy = 0; dy < nh; ++dy) {
        const double y0 = dy * sy, y1 = std::min<double>(h, (dy + 1) * sy);
        const int iy0 = static_cast<int>(y0), iy1 = std::min(h - 1, static_cast<int>(std::ceil(y1)) - 1);
        for (int dx = 0; dx < nw; ++dx) {
            const double x0 = dx * sx, x1 = std::min<double>(w, (dx + 1) * sx);
            const int ix0 = static_cast<int>(x0), ix1 = std::min(w - 1, static_cast<int>(std::ceil(x1)) - 1);
            double r = 0, g = 0, b = 0, a = 0, wsum = 0;
            for (int iy = iy0; iy <= iy1; ++iy) {
                const double wy = std::min<double>(y1, iy + 1) - std::max<double>(y0, iy);
                if (wy <= 0) continue;
                for (int ix = ix0; ix <= ix1; ++ix) {
                    const double wx = std::min<double>(x1, ix + 1) - std::max<double>(x0, ix);
                    if (wx <= 0) continue;
                    const double wgt = wx * wy;
                    const std::uint8_t* p = px + (static_cast<std::size_t>(iy) * w + ix) * 4;
                    const double al = p[3] / 255.0;
                    r += p[0] * al * wgt; g += p[1] * al * wgt; b += p[2] * al * wgt;
                    a += al * wgt;
                    wsum += wgt;
                }
            }
            std::uint8_t* o = dst + (static_cast<std::size_t>(dy) * nw + dx) * 4;
            if (wsum <= 0 || a <= 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            auto to8 = [](double v) { return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, std::floor(v + 0.5)))); };
            o[0] = to8(r / a); o[1] = to8(g / a); o[2] = to8(b / a);
            o[3] = to8(a / wsum * 255.0);
        }
    }
    return out;
}

bool WaveformThumbnailGenerator::Handles(const std::string& e) const { return e == "wav" || e == "ogg" || e == "mp3"; }

Image WaveformThumbnailGenerator::Render(const std::string& absolutePath, std::string* error) {
    std::error_code ec;
    const auto bytes = fs::file_size(PathFromU8(absolutePath), ec);
    if (ec || bytes > (64u << 20)) { if (error) *error = "audio file too large or unreadable"; return Image(); }
    AudioClip clip;
    if (!clip.Load(absolutePath, error) || clip.samples.empty()) return Image();
    const int W = 128, H = 128, mid = H / 2;
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) img.SetPixel(x, y, Color(0.10f, 0.12f, 0.14f, 1.0f));
    for (int x = 0; x < W; ++x) img.SetPixel(x, mid, Color(0.25f, 0.30f, 0.34f, 1.0f));
    const std::size_t n = clip.samples.size();
    for (int x = 0; x < W; ++x) {
        const std::size_t a = n * static_cast<std::size_t>(x) / W, b = std::max(a + 1, n * static_cast<std::size_t>(x + 1) / W);
        float lo = 0.0f, hi = 0.0f;
        for (std::size_t i = a; i < b && i < n; ++i) { lo = std::min(lo, clip.samples[i]); hi = std::max(hi, clip.samples[i]); }
        const int y0 = mid - static_cast<int>(std::min(1.0f, hi) * (mid - 4)), y1 = mid - static_cast<int>(std::max(-1.0f, lo) * (mid - 4));
        for (int y = std::min(y0, mid); y <= std::max(y1, mid); ++y) img.SetPixel(x, y, Color(0.35f, 0.85f, 0.55f, 1.0f));
    }
    return img;
}

namespace {
struct Rgb { std::uint8_t r, g, b; };
Rgb KindColour(AssetType k) {
    switch (k) {
        case AssetType::Texture:      return {38, 120, 128};
        case AssetType::Model:        return {176, 104, 40};
        case AssetType::Mesh:         return {160, 88, 56};
        case AssetType::Audio:        return {110, 70, 150};
        case AssetType::Material:     return {160, 60, 120};
        case AssetType::Scene:        return {50, 130, 70};
        case AssetType::Prefab:       return {50, 90, 160};
        case AssetType::Animation:    return {40, 140, 160};
        case AssetType::Dialogue:     return {150, 120, 40};
        case AssetType::Quest:        return {150, 100, 60};
        case AssetType::BehaviorTree: return {90, 140, 60};
        case AssetType::VisualScript: return {80, 90, 170};
        case AssetType::Sequence:     return {130, 70, 70};
        case AssetType::Script:       return {170, 150, 50};
        case AssetType::Data:         return {90, 100, 110};
        case AssetType::Font:         return {100, 100, 140};
        default:                      return {90, 90, 90};
    }
}
} // namespace

Image MakePlaceholderThumbnail(AssetType kind, const std::string& label, int size, bool error) {
    size = std::max(16, size);
    Rgb c = error ? Rgb{150, 40, 40} : KindColour(kind);
    Image img(size, size);
    std::uint8_t* d = img.Data();
    const int border = std::max(1, size / 32);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            std::uint8_t* p = d + (static_cast<std::size_t>(y) * size + x) * 4;
            const bool edge = x < border || y < border || x >= size - border || y >= size - border;
            const double shade = edge ? 0.6 : 1.0 - 0.25 * (static_cast<double>(y) / size);   // a soft top-down gradient
            p[0] = static_cast<std::uint8_t>(c.r * shade);
            p[1] = static_cast<std::uint8_t>(c.g * shade);
            p[2] = static_cast<std::uint8_t>(c.b * shade);
            p[3] = 255;
        }
    std::string text = label;
    for (char& ch : text) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    if (text.size() > 4) text.resize(4);
    if (text.empty()) text = "?";
    // Largest whole-number scale of the 8x8 font that fits the tile.
    int scale = std::max(1, (size - 4 * border) / (8 * static_cast<int>(text.size())));
    scale = std::min(scale, std::max(1, size / 16));
    const int tw = static_cast<int>(text.size()) * 8 * scale, th = 8 * scale;
    const int ox = (size - tw) / 2, oy = (size - th) / 2;
    for (std::size_t i = 0; i < text.size(); ++i)
        for (int gy = 0; gy < 8; ++gy)
            for (int gx = 0; gx < 8; ++gx) {
                if (!Font8x8::Pixel(text[i], gx, gy)) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        const int x = ox + static_cast<int>(i) * 8 * scale + gx * scale + sx;
                        const int y = oy + gy * scale + sy;
                        if (x < 0 || y < 0 || x >= size || y >= size) continue;
                        std::uint8_t* p = d + (static_cast<std::size_t>(y) * size + x) * 4;
                        p[0] = p[1] = p[2] = 245;
                    }
            }
    return img;
}

// --- Image generator ------------------------------------------------------------------------------

bool ImageThumbnailGenerator::Handles(const std::string& e) const {
    return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "tga" || e == "gif" || e == "psd" || e == "hdr";
}

Image ImageThumbnailGenerator::Render(const std::string& absolutePath, std::string* error) {
    Image img;
    if (!img.Load(absolutePath, error)) return Image();
    return img;
}

namespace {
// Adapts a callback to the generator interface.
class FunctionGenerator final : public ThumbnailGenerator {
public:
    FunctionGenerator(std::vector<std::string> exts, std::function<Image(const std::string&)> fn)
        : m_exts(std::move(exts)), m_fn(std::move(fn)) {}
    bool Handles(const std::string& e) const override { return std::find(m_exts.begin(), m_exts.end(), e) != m_exts.end(); }
    Image Render(const std::string& path, std::string*) override { return m_fn(path); }
private:
    std::vector<std::string> m_exts;
    std::function<Image(const std::string&)> m_fn;
};
} // namespace

// --- Cache -------------------------------------------------------------------------------------------

ThumbnailCache::ThumbnailCache(Options options) : m_opt(std::move(options)) {
    m_opt.size = std::max(16, m_opt.size);
    if (m_opt.imageGenerator) m_generators.push_back(std::make_shared<ImageThumbnailGenerator>());
    if (m_opt.audioGenerator) m_generators.push_back(std::make_shared<WaveformThumbnailGenerator>());
}

void ThumbnailCache::AddGenerator(std::shared_ptr<ThumbnailGenerator> generator) {
    if (generator) m_generators.push_back(std::move(generator));
}

void ThumbnailCache::AddGenerator(const std::vector<std::string>& extensions, std::function<Image(const std::string&)> render) {
    std::vector<std::string> exts;
    for (const auto& e : extensions) {
        std::string x = Lower(e);
        if (!x.empty() && x[0] == '.') x.erase(0, 1);
        exts.push_back(x);
    }
    m_generators.push_back(std::make_shared<FunctionGenerator>(std::move(exts), std::move(render)));
}

std::string ThumbnailCache::DiskPathFor(const Guid& guid, std::uint64_t hash) const {
    if (m_opt.diskDir.empty()) return std::string();
    const std::string g = guid.ToString();
    return m_opt.diskDir + "/" + g.substr(0, 2) + "/" + g + "_" + savefmt::Hex16(hash) + "_" +
           std::to_string(m_opt.size) + "_" + m_opt.tag + ".png";
}

bool ThumbnailCache::InMemory(const Guid& guid, std::uint64_t hash) const {
    return m_index.count(Key{guid, hash}) != 0;
}

bool ThumbnailCache::OnDisk(const Guid& guid, std::uint64_t hash) const {
    const std::string p = DiskPathFor(guid, hash);
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(PathFromU8(p), ec);
}

std::shared_ptr<const Image> ThumbnailCache::Placeholder(AssetType kind, const std::string& ext, bool error) {
    const std::string key = std::string(AssetTypeName(kind)) + "/" + ext + (error ? "/e" : "");
    auto it = m_placeholders.find(key);
    if (it != m_placeholders.end()) return it->second;
    auto img = std::make_shared<const Image>(MakePlaceholderThumbnail(kind, ext, m_opt.size, error));
    m_placeholders[key] = img;
    return img;
}

void ThumbnailCache::Evict() {
    while (!m_lru.empty() && (m_lru.size() > std::max<std::size_t>(1, m_opt.maxEntries) ||
                              (m_bytes > m_opt.maxBytes && m_lru.size() > 1))) {
        Entry& e = m_lru.back();
        m_bytes -= e.bytes;
        m_index.erase(e.key);
        m_lru.pop_back();
        ++m_stats.evictions;
    }
}

ThumbnailCache::Thumb ThumbnailCache::Insert(const Key& key, std::shared_ptr<const Image> image, bool placeholder, bool failed, Source source) {
    Entry e;
    e.key = key;
    e.image = image;
    e.placeholder = placeholder;
    e.failed = failed;
    e.bytes = image->Pixels().size();
    m_bytes += e.bytes;
    m_lru.push_front(std::move(e));
    m_index[key] = m_lru.begin();
    Evict();
    Thumb t;
    t.image = std::move(image);
    t.source = source;
    t.placeholder = placeholder;
    t.failed = failed;
    return t;
}

void ThumbnailCache::RemoveStaleOnDisk(const Guid& guid, const std::string& keepFile) {
    const std::string g = guid.ToString();
    std::error_code ec;
    const fs::path shard = PathFromU8(m_opt.diskDir + "/" + g.substr(0, 2));
    if (!fs::is_directory(shard, ec)) return;
    for (fs::directory_iterator it(shard, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = U8(it->path().filename());
        if (name.compare(0, g.size(), g) == 0 && name != keepFile) {
            std::error_code e2;
            fs::remove(it->path(), e2);
        }
    }
}

ThumbnailCache::Thumb ThumbnailCache::Get(const Guid& guid, std::uint64_t hash, const std::string& path, AssetType kind) {
    const Key key{guid, hash};
    // 1. Memory.
    auto hit = m_index.find(key);
    if (hit != m_index.end()) {
        m_lru.splice(m_lru.begin(), m_lru, hit->second);
        ++m_stats.memoryHits;
        const Entry& e = *hit->second;
        Thumb t;
        t.image = e.image;
        t.source = Source::Memory;
        t.placeholder = e.placeholder;
        t.failed = e.failed;
        return t;
    }
    const std::string ext = AssetExtension(path);
    // 2. Disk.
    if (!m_opt.diskDir.empty()) {
        const std::string dp = DiskPathFor(guid, hash);
        std::error_code ec;
        if (fs::is_regular_file(PathFromU8(dp), ec)) {
            Image img;
            if (img.Load(dp) && img.Valid()) {
                ++m_stats.diskHits;
                return Insert(key, std::make_shared<const Image>(std::move(img)), false, false, Source::Disk);
            }
            fs::remove(PathFromU8(dp), ec);   // unreadable: regenerate below
        }
    }
    // 3. A generator.
    for (auto it = m_generators.rbegin(); it != m_generators.rend(); ++it) {
        if (!(*it)->Handles(ext)) continue;
        Image img;
        std::string err;
        try { img = (*it)->Render(path, &err); } catch (...) { img = Image(); }
        if (!img.Valid()) {
            ++m_stats.failures;
            return Insert(key, Placeholder(kind, ext, true), true, true, Source::Placeholder);
        }
        Image small = BoxDownscale(img, m_opt.size);
        ++m_stats.generated;
        if (!m_opt.diskDir.empty()) {
            const std::string dp = DiskPathFor(guid, hash);
            std::error_code ec;
            fs::create_directories(PathFromU8(dp).parent_path(), ec);
            if (small.SavePNG(dp)) {
                ++m_stats.diskWrites;
                RemoveStaleOnDisk(guid, AssetFileName(dp));
            }
        }
        return Insert(key, std::make_shared<const Image>(std::move(small)), false, false, Source::Generated);
    }
    // 4. Nothing renders this type: a placeholder tile (cheap, shared per type).
    ++m_stats.placeholders;
    return Insert(key, Placeholder(kind, ext, false), true, false, Source::Placeholder);
}

ThumbnailCache::Thumb ThumbnailCache::Get(const AssetDatabase& db, const Guid& guid) {
    const AssetRecord* r = db.Find(guid);
    if (!r) {
        Thumb t;
        t.image = Placeholder(AssetType::Unknown, "", true);
        t.placeholder = true;
        t.failed = true;
        ++m_stats.placeholders;
        return t;
    }
    return Get(guid, r->hash, db.AbsolutePath(guid), r->kind);
}

void ThumbnailCache::Invalidate(const Guid& guid) {
    for (auto it = m_lru.begin(); it != m_lru.end();) {
        if (it->key.guid == guid) {
            m_bytes -= it->bytes;
            m_index.erase(it->key);
            it = m_lru.erase(it);
        } else ++it;
    }
    if (!m_opt.diskDir.empty()) RemoveStaleOnDisk(guid, std::string());
}

void ThumbnailCache::Clear() {
    m_lru.clear();
    m_index.clear();
    m_bytes = 0;
}

void ThumbnailCache::ClearDisk() {
    if (m_opt.diskDir.empty()) return;
    std::error_code ec;
    const fs::path dir = PathFromU8(m_opt.diskDir);
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> victims;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) victims.push_back(it->path());
    for (const auto& p : victims) { std::error_code e2; fs::remove_all(p, e2); }
}

} // namespace okay
