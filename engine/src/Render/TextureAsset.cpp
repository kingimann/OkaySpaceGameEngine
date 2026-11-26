// One texture asset for every renderer. See okay/Render/TextureAsset.hpp and docs/rendering/textures.md.
// Written for this engine from its own AssetManager, MemoryTracker and okay::tex mip filters; no outside code.
#include "okay/Render/TextureAsset.hpp"

#include "okay/Core/AssetManager.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include "okay/Debug/MemoryTracker.hpp"
#include "okay/Render/ProcTexture.hpp"
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Mip.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace okay {

// ---- settings ----------------------------------------------------------------------------------------------------

namespace {
std::string Low(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ParseFlag(const std::string& v, bool& out) {
    const std::string s = Low(v);
    if (s == "1" || s == "true" || s == "on" || s == "yes") { out = true; return true; }
    if (s == "0" || s == "false" || s == "off" || s == "no") { out = false; return true; }
    return false;
}
const char* const kCompressionNames[] = {"auto", "bc1", "bc3", "bc4", "bc5"};
const char* const kMipFilterNames[] = {"box", "kaiser"};

// One texture-only key into `s`. False when the key is not one of ours.
bool ApplyKey(TextureSettings& s, const std::string& keyIn, const std::string& v) {
    const std::string k = Low(keyIn);
    if (k == "anisotropy") {
        const int n = std::atoi(v.c_str());
        s.anisotropy = n < 1 ? 1 : n > 16 ? 16 : n;
    } else if (k == "normalmap") {
        ParseFlag(v, s.normalMap);
    } else if (k == "mipfilter") {
        const std::string m = Low(v);
        s.mipFilter = m == "kaiser" ? TextureSettings::MipKaiser : TextureSettings::MipBox;
    } else if (k == "compression") {
        const std::string c = Low(v);
        s.compression = TextureSettings::CompressAuto;
        for (int i = 0; i < 5; ++i) if (c == kCompressionNames[i]) s.compression = i;
    } else {
        return false;
    }
    return true;
}
} // namespace

bool TextureSettings::operator==(const TextureSettings& o) const {
    return sRGB == o.sRGB && filter == o.filter && wrap == o.wrap && maxSize == o.maxSize && mipmaps == o.mipmaps &&
           compress == o.compress && anisotropy == o.anisotropy && normalMap == o.normalMap && mipFilter == o.mipFilter &&
           compression == o.compression;
}

const char* TextureCompressionName(int c) { return c >= 0 && c < 5 ? kCompressionNames[c] : "auto"; }
const char* TextureMipFilterName(int m) { return m == TextureSettings::MipKaiser ? "kaiser" : "box"; }

TextureSettings ReadTextureSettings(const std::string& path) {
    TextureSettings s;
    if (path.empty() || path[0] == '@' || path.rfind("proc:", 0) == 0) return s;
    std::ifstream in(path + ".okaymeta", std::ios::binary);
    if (!in) return s;
    // The `importer texture` block's `set <key> <value>` lines; later lines win, like ReadImportSetting. Values are
    // unescaped (the asset database writes them through savefmt::Escape), as ImportSidecar.cpp's ParseBlock does.
    std::string line;
    bool texture = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("importer ", 0) == 0) { texture = line.compare(9, 7, "texture") == 0; continue; }
        if (!texture || line.rfind("set ", 0) != 0) continue;
        const std::size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) continue;
        const std::string key = line.substr(4, sp - 4), k = Low(key), v = savefmt::Unescape(line.substr(sp + 1)), lv = Low(v);
        if (k == "srgb") ParseFlag(v, s.sRGB);
        else if (k == "linear") { bool lin = false; if (ParseFlag(v, lin)) s.sRGB = !lin; }   // the v1 layout
        else if (k == "filter") {
            if (lv == "point" || lv == "nearest") s.filter = TextureSettings::Point;
            else if (lv == "bilinear" || lv == "linear") s.filter = TextureSettings::Bilinear;
            else if (lv == "trilinear") s.filter = TextureSettings::Trilinear;
        } else if (k == "wrap") {
            if (lv == "repeat") s.wrap = TextureSettings::Repeat;
            else if (lv == "clamp") s.wrap = TextureSettings::Clamp;
            else if (lv == "mirror") s.wrap = TextureSettings::Mirror;
        } else if (k == "maxsize") {
            const int n = std::atoi(v.c_str());
            s.maxSize = n >= 32 && n <= 16384 ? n : 0;
        } else if (k == "mipmaps" || k == "mips") ParseFlag(v, s.mipmaps);
        else if (k == "compress") ParseFlag(v, s.compress);
        else ApplyKey(s, key, v);
    }
    if (s.normalMap) s.sRGB = false;   // a normal map is never colour data
    return s;
}

std::vector<std::pair<std::string, std::string>> TextureExtraKeys(const TextureSettings& s) {
    return {{"anisotropy", std::to_string(s.anisotropy)},
            {"compression", TextureCompressionName(s.compression)},
            {"mipFilter", TextureMipFilterName(s.mipFilter)},
            {"normalMap", s.normalMap ? "1" : "0"}};
}

void ApplyTextureExtraKeys(TextureSettings& s, const std::vector<std::pair<std::string, std::string>>& keys) {
    for (const auto& kv : keys) ApplyKey(s, kv.first, kv.second);
}

// ---- the asset ---------------------------------------------------------------------------------------------------

namespace {
std::atomic<std::uint64_t> g_serial{0};
std::uint64_t NextSerial() { return ++g_serial; }

// Every asset counted in MemoryTracker, for Report() and GetStats(). Assets can die on AssetManager's worker thread
// (it may hold the last reference while a reload decodes), so the set has a lock.
debugutil::SpinLock g_liveLock;
std::unordered_set<const TextureAsset*>& Live() { static auto* s = new std::unordered_set<const TextureAsset*>(); return *s; }
std::int64_t g_liveBytes = 0;   // guarded by g_liveLock
} // namespace

TextureAsset::~TextureAsset() { Untrack(); }

TextureAsset::TextureAsset(TextureAsset&& o) noexcept { *this = std::move(o); }

TextureAsset& TextureAsset::operator=(TextureAsset&& o) noexcept {
    if (this == &o) return *this;
    const bool count = o.m_tracked >= 0 || m_tracked >= 0;
    Untrack();
    o.Untrack();
    path = std::move(o.path);
    image = std::move(o.image);
    settings = o.settings;
    serial = o.serial;
    registered = o.registered;
    m_mips = std::move(o.m_mips);
    m_mipsSerial = o.m_mipsSerial;
    o.image = Image();
    if (count) Track();
    return *this;
}

std::vector<Image> BuildMipChain(const Image& base, const TextureSettings& settings) {
    std::vector<Image> levels;
    if (!base.Valid()) return levels;
    if (!settings.mipmaps || (base.Width() == 1 && base.Height() == 1)) { levels.push_back(base); return levels; }
    tex::MipOptions o;
    o.filter = settings.mipFilter == TextureSettings::MipKaiser ? tex::MipFilter::Kaiser : tex::MipFilter::Box;
    o.wrap = settings.wrap == TextureSettings::Repeat ? tex::MipWrap::Repeat : tex::MipWrap::Clamp;
    o.normalMap = settings.normalMap;
    o.srgb = settings.IsColour();
    std::vector<std::vector<std::uint8_t>> chain = tex::GenerateMipsRGBA8(base.Data(), base.Width(), base.Height(), o);
    if (chain.empty()) { levels.push_back(base); return levels; }
    int w = base.Width(), h = base.Height();
    levels.reserve(chain.size());
    for (auto& lv : chain) {
        Image im;
        im.Set(w, h, std::move(lv));
        levels.push_back(std::move(im));
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return levels;
}

const std::vector<Image>& TextureAsset::Mips() const {
    if (m_mips && m_mipsSerial == serial) return *m_mips;
    m_mips = std::make_shared<std::vector<Image>>(BuildMipChain(image, settings));
    m_mipsSerial = serial;
    if (m_tracked >= 0) Track();
    return *m_mips;
}

const std::vector<Image>& TextureAsset::UploadLevels(std::vector<Image>& scratch) const {
    if (!settings.mipmaps) { scratch.clear(); scratch.push_back(image); return scratch; }
    if (MipsBuilt()) return *m_mips;
    scratch = BuildMipChain(image, settings);
    return scratch;
}

std::shared_ptr<const std::vector<Image>> TextureAsset::SharedMips() const {
    Mips();
    return m_mips;
}

std::size_t TextureAsset::Bytes() const {
    std::size_t b = image.Pixels().size();
    if (m_mips && m_mipsSerial == serial)
        for (std::size_t i = 1; i < m_mips->size(); ++i) b += (*m_mips)[i].Pixels().size();
    return b;
}

void TextureAsset::Touch() {
    serial = NextSerial();
    m_mips.reset();
    m_mipsSerial = 0;
    if (m_tracked >= 0) Track();
}

void TextureAsset::Track() const {
    const std::int64_t now = static_cast<std::int64_t>(Bytes());
    debugutil::SpinGuard g(g_liveLock);
    MemoryTracker& mem = MemoryTracker::Global();
    if (m_tracked < 0) {
        mem.Add(MemoryTracker::Textures, now, 1);
        Live().insert(this);
    } else if (m_tracked != now) {
        mem.Resize(MemoryTracker::Textures, m_tracked, now);
        g_liveBytes -= m_tracked;
    } else {
        return;
    }
    g_liveBytes += now;
    m_tracked = now;
}

void TextureAsset::Untrack() const {
    debugutil::SpinGuard g(g_liveLock);
    if (m_tracked < 0) return;
    MemoryTracker::Global().Remove(MemoryTracker::Textures, m_tracked, 1);
    Live().erase(this);
    g_liveBytes -= m_tracked;
    m_tracked = -1;
}

// ---- decoding (may run on AssetManager's worker thread) ------------------------------------------------------------

namespace {
bool DecodeInto(const std::string& path, TextureAsset& a, std::string& err) {
    a.path = path;
    a.settings = ReadTextureSettings(path);
    a.registered = false;
    Image img;
    if (path.rfind("proc:", 0) == 0) {
        img = GenerateProcTexture(path);
        if (!img.Valid()) err = "unknown procedural texture '" + path + "'";
    } else if (tex::IsDdsPath(path)) {
        tex::DdsImage d;
        std::vector<std::uint8_t> rgba;
        std::string why;
        if (tex::ReadDdsFile(path, &d, &why) && tex::DdsLevelToRGBA8(d, 0, &rgba)) img.Set(d.width, d.height, std::move(rgba));
        else err = "cannot load '" + path + "': " + (why.empty() ? std::string("decode failed") : why);
    } else {
        std::string why;
        if (!img.Load(path, &why)) err = "cannot load '" + path + "': " + why;
    }
    if (!img.Valid()) { if (err.empty()) err = "cannot load '" + path + "'"; return false; }
    const int ms = a.settings.maxSize;
    if (ms > 0 && (img.Width() > ms || img.Height() > ms)) img = BoxDownscale(img, ms);
    a.image = std::move(img);
    a.serial = NextSerial();
    return true;
}

std::shared_ptr<TextureAsset> Decode(const std::string& path, std::string& err) {
    auto a = std::make_shared<TextureAsset>();
    if (!DecodeInto(path, *a, err)) return nullptr;
    return a;
}

// ---- the library (main thread) -------------------------------------------------------------------------------------

struct Slot {
    std::shared_ptr<TextureAsset> p;   // null: the load failed (remembered so a missing file is not retried every draw)
    std::uint64_t lastUsed = 0;
    AssetHandle<TextureAsset> pending; // a background load (async loading on)
    bool loading = false;
};

struct Library {
    std::unordered_map<std::string, Slot> held;                                  // what the renderers draw with
    std::unordered_map<std::string, std::shared_ptr<TextureAsset>> registered;   // "@..." pictures made in memory
    unsigned generation = 1;
    std::uint64_t frame = 0;
    std::size_t budget = 0;
    bool budgetSet = false;
    int idleFrames = 300;
    bool hot = false;
    bool async = false;
    double hotInterval = 0.5;
    bool loaderReady = false;
    bool warnedOver = false;
    std::uint64_t reloads = 0, trimmed = 0;
    int reloadsThisPump = 0;
};
Library& Lib() { static Library* l = new Library(); return *l; }   // never destroyed: renderers may ask during exit

std::size_t DefaultBudget() {
    if (const char* e = std::getenv("OKAY_TEXTURE_BUDGET_MB")) {
        const long mb = std::atol(e);
        if (mb >= 0) return static_cast<std::size_t>(mb) * 1024u * 1024u;
    }
    // Automatic: the asset streamer's video memory budget (from the renderer's report or the machine).
    return AssetManager::Instance().VramBudget();
}

void EnsureLoader() {
    Library& L = Lib();
    if (L.loaderReady) return;
    L.loaderReady = true;
    if (!L.budgetSet) { L.budget = DefaultBudget(); L.budgetSet = true; }
    AssetManager& am = AssetManager::Instance();
    am.RegisterLoader<TextureAsset>("Texture", &Decode, [](const TextureAsset& t) { return t.Bytes(); });
    am.SetReloadCallback([](const std::string& key, const std::string& type) {
        if (type != "Texture") return;
        Library& lib = Lib();
        ++lib.reloads;
        ++lib.reloadsThisPump;
        Log::Info("[texture] reloaded '", key, "' (the file changed on disk)");
    });
    if (const char* e = std::getenv("OKAY_TEXTURE_HOT_RELOAD")) if (e[0] == '1') { L.hot = true; am.SetHotReload(true, L.hotInterval); }
    if (const char* e = std::getenv("OKAY_TEXTURE_ASYNC")) if (e[0] == '1') L.async = true;
}

std::shared_ptr<TextureAsset> Tracked(std::shared_ptr<TextureAsset> p) {
    if (p) p->Track();   // counted from its first hand-out (a no-op when it already is)
    return p;
}

// A background load that finished: take the asset (or remember the failure).
void Resolve(const std::string& name, Slot& s) {
    if (!s.loading) return;
    if (s.pending.Ready()) {
        s.p = Tracked(s.pending.Shared());
        s.pending.Reset();
        s.loading = false;
    } else if (!s.pending.Loading()) {
        s.pending.Reset();
        s.loading = false;
        Log::Warning("[texture] cannot load '", name, "' (background load failed)");
    }
}
} // namespace

struct TextureLibraryAccess {
    static const std::shared_ptr<TextureAsset>& Ptr(const TextureHandle& h) { return h.m_p; }
};

namespace textures {

TextureHandle Load(const std::string& path, std::string* error) {
    if (path.empty()) { if (error) *error = "no texture name"; return TextureHandle(); }
    EnsureLoader();
    Library& L = Lib();
    if (path[0] == '@') {
        auto it = L.registered.find(path);
        if (it == L.registered.end()) { if (error) *error = "no picture registered as '" + path + "'"; return TextureHandle(); }
        return TextureHandle(it->second);
    }
    std::string err;
    AssetHandle<TextureAsset> h = AssetManager::Instance().Get<TextureAsset>(path, &err);
    if (!h.Ready()) { if (error) *error = err.empty() ? "cannot load '" + path + "'" : err; return TextureHandle(); }
    return TextureHandle(Tracked(h.Shared()));
}

TextureHandle Find(const std::string& path) {
    if (path.empty()) return TextureHandle();
    EnsureLoader();
    Library& L = Lib();
    if (path[0] == '@') {
        auto it = L.registered.find(path);
        return it == L.registered.end() ? TextureHandle() : TextureHandle(it->second);
    }
    return TextureHandle(Tracked(AssetManager::Instance().Find<TextureAsset>(path).Shared()));
}

bool Reload(const std::string& path) {
    TextureHandle h = Find(path);
    if (!h.Get() || h->registered) return false;
    std::string err;
    TextureAsset fresh;
    if (!DecodeInto(path, fresh, err)) {
        Log::Warning("[texture] reload of '", path, "' failed: ", err, "; keeping the old pixels");
        return false;
    }
    TextureAsset& live = *TextureLibraryAccess::Ptr(h);
    live = std::move(fresh);
    ++Lib().reloads;
    return true;
}

namespace {
namespace fs = std::filesystem;
bool NotAFile(const std::string& key) { return key.empty() || key[0] == '@' || key.rfind("proc:", 0) == 0; }
// Absolute and lexically normal, with '/' separators (lower case on Windows, where names are not case sensitive).
std::string CanonFile(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::u8path(p);
    if (a.is_relative()) a = fs::absolute(a, ec);
    std::string out = a.lexically_normal().generic_u8string();
#ifdef _WIN32
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}
std::string LastPart(const std::string& canon) {
    const std::size_t slash = canon.find_last_of('/');
    return slash == std::string::npos ? canon : canon.substr(slash + 1);
}
// Lexically first; another spelling (a link, a short name) is asked of the file system only when the file names agree.
bool SameFile(const std::string& key, const std::string& want, const std::string& wantName, const std::string& file) {
    const std::string k = CanonFile(key);
    if (k == want) return true;
    if (LastPart(k) != wantName) return false;
    std::error_code ec;
    return fs::equivalent(fs::u8path(key), fs::u8path(file), ec) && !ec;
}
} // namespace

int ReloadFile(const std::string& file) {
    if (NotAFile(file)) return 0;
    Library& L = Lib();
    const std::string want = CanonFile(file), wantName = LastPart(want);
    int names = 0;
    std::vector<std::shared_ptr<TextureAsset>> assets;
    auto add = [&](const std::shared_ptr<TextureAsset>& p) {
        if (p && !p->registered && std::find(assets.begin(), assets.end(), p) == assets.end()) assets.push_back(p);
    };
    for (auto it = L.held.begin(); it != L.held.end();) {
        if (NotAFile(it->first) || !SameFile(it->first, want, wantName, file)) { ++it; continue; }
        ++names;
        if (it->second.p) { add(it->second.p); ++it; }
        else it = L.held.erase(it);   // a failed load: look for the file again on the next draw
    }
    // Assets loaded from it that only other code holds (a TextureHandle outside the renderers).
    std::vector<std::string> livePaths;
    {
        debugutil::SpinGuard g(g_liveLock);
        for (const TextureAsset* a : Live()) if (!a->registered && !NotAFile(a->path)) livePaths.push_back(a->path);
    }
    for (const std::string& lp : livePaths)
        if (SameFile(lp, want, wantName, file)) add(TextureLibraryAccess::Ptr(Find(lp)));
    for (const auto& p : assets) {
        std::string err;
        TextureAsset fresh;
        if (!DecodeInto(p->path, fresh, err)) { Log::Warning("[texture] reload of '", p->path, "' failed: ", err, "; keeping the old pixels"); continue; }
        *p = std::move(fresh);
        ++L.reloads;
    }
    if (names > 0 || !assets.empty()) ++L.generation;
    return names;
}

TextureHandle Register(const std::string& name, Image img) {
    EnsureLoader();
    Library& L = Lib();
    std::shared_ptr<TextureAsset>& p = L.registered[name];
    if (!p) { p = std::make_shared<TextureAsset>(); p->path = name; p->registered = true; }
    p->image = std::move(img);
    p->Touch();
    p->Track();
    auto h = L.held.find(name);
    if (h != L.held.end() && h->second.p != p) h->second.p = p;
    return TextureHandle(p);
}

void Unregister(const std::string& name) {
    Library& L = Lib();
    const bool had = L.registered.erase(name) > 0;
    if (L.held.erase(name) > 0 || had) ++L.generation;
}

Image& RegisteredImage(const std::string& name) {
    Library& L = Lib();
    auto it = L.registered.find(name);
    if (it == L.registered.end()) { Register(name, Image()); it = L.registered.find(name); }
    return it->second->image;
}

void TouchRegistered(const std::string& name) {
    Library& L = Lib();
    auto it = L.registered.find(name);
    if (it != L.registered.end()) it->second->Touch();
}

namespace { std::atomic<int> g_readOnly{0}; }
bool ReadOnly() { return g_readOnly.load(std::memory_order_relaxed) > 0; }
ReadOnlyScope::ReadOnlyScope() { g_readOnly.fetch_add(1, std::memory_order_relaxed); }
ReadOnlyScope::~ReadOnlyScope() { g_readOnly.fetch_sub(1, std::memory_order_relaxed); }

const TextureAsset* ForRender(const std::string& name) {
    if (name.empty()) return nullptr;
    Library& L = Lib();
    auto it = L.held.find(name);
    if (ReadOnly()) {   // render threads: a pure lookup (the main thread loaded and resolved everything before)
        if (it == L.held.end() || it->second.loading) return nullptr;
        const TextureAsset* a = it->second.p.get();
        return a && a->Valid() ? a : nullptr;
    }
    if (it == L.held.end()) {
        EnsureLoader();
        Slot s;
        if (name[0] == '@') {
            auto r = L.registered.find(name);
            if (r == L.registered.end()) return nullptr;   // not remembered: it may be registered later
            s.p = r->second;
        } else if (L.async && name.rfind("proc:", 0) != 0) {
            s.pending = AssetManager::Instance().LoadAsync<TextureAsset>(name);
            s.loading = true;
        } else {
            std::string err;
            TextureHandle h = Load(name, &err);
            if (!h.Get()) Log::Warning("[texture] ", err);
            s.p = TextureLibraryAccess::Ptr(h);
        }
        it = L.held.emplace(name, std::move(s)).first;
    }
    Resolve(it->first, it->second);
    it->second.lastUsed = L.frame;
    const TextureAsset* a = it->second.p.get();
    return a && a->Valid() ? a : nullptr;
}

bool Prefetch(const std::string& name, float priority) {
    if (name.empty() || name[0] == '@' || name.rfind("proc:", 0) == 0 || ReadOnly()) return false;
    Library& L = Lib();
    if (L.held.count(name)) return false;
    EnsureLoader();
    Slot s;
    s.pending = AssetManager::Instance().Stream<TextureAsset>(name, priority);
    s.loading = true;
    s.lastUsed = L.frame;
    L.held.emplace(name, std::move(s));
    return true;
}

bool Loading(const std::string& name) {
    Library& L = Lib();
    auto it = L.held.find(name);
    if (it == L.held.end()) return false;
    if (!ReadOnly()) Resolve(it->first, it->second);
    return it->second.loading;
}

void Drop(const std::string& name) {
    Library& L = Lib();
    if (L.held.erase(name) > 0) ++L.generation;
}

void InvalidateAll() {
    Library& L = Lib();
    std::vector<std::string> names;
    for (auto it = L.held.begin(); it != L.held.end();) {
        if (it->second.p && it->second.p->registered) { ++it; continue; }
        names.push_back(it->first);
        it = L.held.erase(it);
    }
    ++L.generation;
    for (const std::string& n : names) Reload(n);   // still held elsewhere: read it again in place
}

unsigned Generation() { return Lib().generation; }

bool IsHeld(const std::string& name) {
    const Library& L = Lib();
    auto it = L.held.find(name);
    return it != L.held.end() && it->second.p && it->second.p->Valid();
}

int Pump() {
    EnsureLoader();
    Library& L = Lib();
    ++L.frame;
    L.reloadsThisPump = 0;
    AssetManager::Instance().Pump();
    for (auto& kv : L.held) Resolve(kv.first, kv.second);
    // With hot reload on, a missing picture is looked for again now and then (it may have been created since).
    if (L.hot && L.frame % 120 == 0)
        for (auto it = L.held.begin(); it != L.held.end();) it = (it->second.p || it->second.loading) ? std::next(it) : L.held.erase(it);
    // The budget: warn once per crossing; while over it, let go of what the renderers have not drawn lately.
    std::size_t bytes = 0;
    { debugutil::SpinGuard g(g_liveLock); bytes = static_cast<std::size_t>(std::max<std::int64_t>(0, g_liveBytes)); }
    L.budget = BudgetBytes();
    L.budgetSet = true;
    if (L.budget > 0 && bytes > L.budget) {
        std::vector<std::pair<std::uint64_t, std::string>> idle;
        for (const auto& kv : L.held)
            if (kv.second.p && !kv.second.p->registered && L.frame - kv.second.lastUsed >= std::uint64_t(L.idleFrames))
                idle.emplace_back(kv.second.lastUsed, kv.first);
        std::sort(idle.begin(), idle.end());
        bool dropped = false;
        for (const auto& c : idle) {
            if (bytes <= L.budget) break;
            auto it = L.held.find(c.second);
            const std::size_t b = it->second.p->Bytes();
            const bool frees = it->second.p.use_count() == 1;   // nobody else holds it: it leaves memory now
            L.held.erase(it);
            if (frees) bytes = bytes > b ? bytes - b : 0;
            ++L.trimmed;
            dropped = true;
        }
        if (dropped) ++L.generation;
        if (bytes > L.budget && !L.warnedOver) {
            char mb[96];
            std::snprintf(mb, sizeof(mb), "%.2f MB of textures, budget %.2f MB", bytes / 1048576.0, L.budget / 1048576.0);
            Log::Warning("[texture] over budget: ", mb, " (OKAY_TEXTURE_BUDGET_MB; textures in use are kept)");
            L.warnedOver = true;
        }
    }
    if (L.budget == 0 || bytes <= L.budget) L.warnedOver = false;
    return L.reloadsThisPump;
}

void SetHotReload(bool enabled, double intervalSeconds) {
    EnsureLoader();
    Library& L = Lib();
    L.hot = enabled;
    L.hotInterval = intervalSeconds;
    AssetManager::Instance().SetHotReload(enabled, intervalSeconds);
}

bool HotReload() { return Lib().hot; }

void SetAsyncLoading(bool enabled) { EnsureLoader(); Lib().async = enabled; }
bool AsyncLoading() { return Lib().async; }

Stats GetStats() {
    const Library& L = Lib();
    Stats s;
    {
        debugutil::SpinGuard g(g_liveLock);
        s.count = Live().size();
        s.bytes = static_cast<std::size_t>(std::max<std::int64_t>(0, g_liveBytes));
    }
    for (const auto& kv : L.held) { if (kv.second.p) ++s.rendererHeld; if (kv.second.loading) ++s.loading; }
    s.budgetBytes = L.budgetSet ? L.budget : DefaultBudget();
    s.overBudget = s.budgetBytes > 0 && s.bytes > s.budgetBytes;
    s.reloads = L.reloads;
    s.trimmed = L.trimmed;
    return s;
}

std::vector<Row> Report() {
    const Library& L = Lib();
    std::unordered_map<const TextureAsset*, long> refs;
    for (const auto& kv : L.held) if (kv.second.p) refs[kv.second.p.get()] = kv.second.p.use_count();
    for (const auto& kv : L.registered) if (kv.second) refs[kv.second.get()] = kv.second.use_count();
    std::vector<Row> rows;
    {
        debugutil::SpinGuard g(g_liveLock);
        for (const TextureAsset* a : Live()) {
            Row r;
            r.path = a->path;
            r.width = a->Width();
            r.height = a->Height();
            r.mipLevels = a->MipLevels();
            r.bytes = a->Bytes();
            auto f = refs.find(a);
            r.refs = f == refs.end() ? 0 : f->second;
            r.registered = a->registered;
            r.sRGB = a->settings.sRGB;
            r.normalMap = a->settings.normalMap;
            r.compress = a->settings.compress;
            rows.push_back(std::move(r));
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.path < b.path; });
    return rows;
}

std::string OverlayLine() {
    const Stats s = GetStats();
    char buf[160];
    if (s.budgetBytes > 0)
        std::snprintf(buf, sizeof(buf), "textures %zu  %.1f MB / %.0f MB%s", s.count, s.bytes / 1048576.0,
                      s.budgetBytes / 1048576.0, s.overBudget ? "  OVER BUDGET" : "");
    else
        std::snprintf(buf, sizeof(buf), "textures %zu  %.1f MB", s.count, s.bytes / 1048576.0);
    std::string out = buf;
    const MemoryTracker& mem = MemoryTracker::Global();
    const int gpu = mem.FindCategory("Textures (GPU)");
    if (gpu >= 0 && mem.Count(gpu) > 0) {
        std::snprintf(buf, sizeof(buf), "  (GPU %lld, %.1f MB)", (long long)mem.Count(gpu), mem.Bytes(gpu) / 1048576.0);
        out += buf;
    }
    return out;
}

void SetBudgetBytes(std::size_t bytes) { Library& L = Lib(); L.budget = bytes; L.budgetSet = true; L.warnedOver = false; }
std::size_t BudgetBytes() { const Library& L = Lib(); return L.budgetSet ? L.budget : DefaultBudget(); }
void SetIdleFrames(int frames) { Lib().idleFrames = frames < 1 ? 1 : frames; }

void ResetForTests() {
    Library& L = Lib();
    L.held.clear();
    L.registered.clear();
    ++L.generation;
    L.reloads = L.trimmed = 0;
    L.warnedOver = false;
    L.budgetSet = false;
    L.idleFrames = 300;
    L.async = false;
    AssetManager::Instance().Purge();
}

} // namespace textures
} // namespace okay
