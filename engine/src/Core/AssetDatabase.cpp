#include "okay/Core/AssetDatabase.hpp"
#include "AssetDatabaseInternal.hpp"
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveData.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <random>
#include <set>
#include <sstream>

namespace okay {

namespace fs = std::filesystem;
using adb::EndsWith;
using adb::Lower;
using adb::PathFromU8;
using adb::StartsWith;
using adb::U8;

namespace {

constexpr std::size_t kMaxMetaBytes = 1u << 20;

std::uint64_t Splitmix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::int64_t EpochSeconds(fs::file_time_type t) {
    using namespace std::chrono;
    const auto d = duration_cast<seconds>(t - fs::file_time_type::clock::now());
    return static_cast<std::int64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count()) + d.count();
}

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// --- Meta text ---------------------------------------------------------------------

struct ParsedMeta {
    bool ok = false;
    std::string error;
    int version = 0;
    bool newer = false;
    Guid guid;
    std::string type;
    std::uint64_t hash = 0;
    bool hasHash = false;
    std::int64_t size = -1;
    std::vector<std::string> labels;
    bool hasImporter = false;
    std::string importerKind;
    int importerVersion = 1;
    ImportSettings::Pairs pairs;
    std::vector<std::string> unknown;
};

ParsedMeta ParseMeta(const std::string& text) {
    ParsedMeta m;
    std::istringstream in(text);
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (Trim(line).empty()) continue;
        if (!header) {
            std::istringstream h(line);
            std::string magic;
            int v = 0;
            h >> magic >> v;
            if (magic != "okaymeta" || v < 1) { m.error = "not a meta file (expected 'okaymeta 1')"; return m; }
            m.version = v;
            m.newer = v > AssetDatabase::kMetaVersion;
            header = true;
            continue;
        }
        if (line[0] == '#') { m.unknown.push_back(line); continue; }
        const std::size_t sp = line.find_first_of(" \t");
        const std::string head = sp == std::string::npos ? line : line.substr(0, sp);
        std::string rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.erase(0, 1);
        if (head == "guid") {
            if (!Guid::Parse(Trim(rest), m.guid)) { m.error = "bad guid '" + rest + "'"; return m; }
        } else if (head == "type") {
            m.type = Trim(rest);
        } else if (head == "hash") {
            const std::string h = Trim(rest);
            if (!h.empty() && h.size() <= 16) { m.hash = std::strtoull(h.c_str(), nullptr, 16); m.hasHash = true; }
        } else if (head == "size") {
            m.size = std::strtoll(Trim(rest).c_str(), nullptr, 10);
        } else if (head == "label") {
            const std::string l = savefmt::Unescape(rest);
            if (!l.empty()) m.labels.push_back(l);
        } else if (head == "importer") {
            std::istringstream is(rest);
            is >> m.importerKind;
            int v = 1;
            if (is >> v) m.importerVersion = v;
            m.hasImporter = true;
        } else if (head == "set") {
            const std::size_t sp2 = rest.find_first_of(" \t");
            const std::string key = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
            std::string val = sp2 == std::string::npos ? std::string() : rest.substr(sp2 + 1);
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
            if (!key.empty()) m.pairs.emplace_back(key, savefmt::Unescape(val));
        } else {
            m.unknown.push_back(line);
        }
    }
    if (!header) { m.error = "empty meta file"; return m; }
    if (m.guid.IsNull()) { m.error = "no guid"; return m; }
    m.ok = true;
    return m;
}

// Settings for `kind` from a parsed meta; `rewrite` is set when the meta should be rewritten
// (older layout, or a type that no longer matches the file).
ImportSettings SettingsFrom(AssetType kind, const ParsedMeta& pm, std::vector<std::string>& notes, bool& rewrite) {
    if (!ImportSettings::HasSettings(kind)) return ImportSettings::Defaults(kind);
    const bool typeMatches = pm.type.empty() || pm.type == AssetTypeName(kind);
    if (!typeMatches) {
        notes.push_back("the meta says '" + pm.type + "' but the file is a " + AssetTypeName(kind) + ": import settings reset");
        rewrite = true;
        return ImportSettings::Defaults(kind);
    }
    if (pm.hasImporter && AssetTypeFromName(pm.importerKind) == kind) {
        ImportSettings s = ImportSettings::FromPairs(kind, pm.importerVersion, pm.pairs, &notes);
        if (pm.importerVersion < ImportSettings::CurrentVersion(kind)) rewrite = true;
        return s;
    }
    return ImportSettings::Defaults(kind);
}

struct FileEntry {
    std::string rel;
    std::int64_t size = 0, stamp = 0, mtime = 0;
};

} // namespace

// --- Guid ----------------------------------------------------------------------------------

std::string Guid::ToString() const {
    char b[33];
    std::snprintf(b, sizeof(b), "%016llx%016llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
    return b;
}

bool Guid::Parse(const std::string& text, Guid& out) {
    std::string s = Trim(text);
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') s = s.substr(1, s.size() - 2);
    if (s.size() == 36) {
        if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return false;
        s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
    }
    if (s.size() != 32) return false;
    std::uint64_t v[2] = {0, 0};
    for (int i = 0; i < 32; ++i) {
        const int h = HexVal(s[static_cast<std::size_t>(i)]);
        if (h < 0) return false;
        v[i / 16] = (v[i / 16] << 4) | static_cast<std::uint64_t>(h);
    }
    out.hi = v[0];
    out.lo = v[1];
    return true;
}

Guid Guid::Generate() {
    static std::atomic<std::uint64_t> counter{0};
    std::uint64_t a = 0, b = 0;
    try {
        std::random_device rd;
        a = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
        b = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    } catch (...) {}
    const auto t = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto w = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    const std::uint64_t c = ++counter;
    int local = 0;
    const auto addr = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&local));
    Guid g;
    g.hi = Splitmix(a ^ Splitmix(t) ^ (c * 0x9E3779B97F4A7C15ull));
    g.lo = Splitmix(b ^ Splitmix(w ^ addr) ^ Splitmix(c + 0x632BE59BD9B4E019ull));
    if (g.IsNull()) g.lo = 1;
    return g;
}

bool AssetRecord::HasLabel(const std::string& label) const {
    const std::string l = Lower(label);
    for (const auto& x : labels) if (Lower(x) == l) return true;
    return false;
}

const char* ReimportEvent::ReasonName() const {
    switch (reason) {
        case Reason::Added: return "added";
        case Reason::SourceChanged: return "source changed";
        case Reason::SettingsChanged: return "settings changed";
    }
    return "changed";
}

std::string ScanReport::ToString() const {
    std::ostringstream o;
    o << scanned << " assets, " << created << " new, " << moved.size() << " moved, " << duplicates.size()
      << " duplicate guids, " << orphanMetas.size() << " orphan metas, " << removed.size() << " removed, "
      << reimports.size() << " reimports";
    if (upgraded) o << ", " << upgraded << " metas upgraded";
    if (unmetaed) o << ", " << unmetaed << " without a meta";
    for (const auto& m : moved) o << "\n  moved " << m.from << " -> " << m.to << " (" << m.how << ")";
    for (const auto& d : duplicates) o << "\n  duplicate guid on " << d.path;
    for (const auto& w : warnings) o << "\n  warning: " << w;
    return o.str();
}

// --- Construction / paths ------------------------------------------------------------------

AssetDatabase::AssetDatabase() { RegisterDefaultExtractors(); }
AssetDatabase::~AssetDatabase() = default;

void AssetDatabase::Clear() {
    m_root.clear();
    m_slots.clear();
    m_pathIndex.clear();
    m_redirects.clear();
    m_vanished.clear();
    m_orphans.clear();
    m_pending.clear();
    m_fresh.clear();
    m_reverse.clear();
    m_reverseValid = false;
}

std::string AssetDatabase::KeyOf(const std::string& projectPath) const { return AssetManager::Normalize(projectPath); }

Guid AssetDatabase::NewGuid() const {
    for (int i = 0; i < 64; ++i) {
        Guid g = m_guidGen ? m_guidGen() : Guid::Generate();
        if (g.IsNull()) continue;
        if (m_slots.count(g) || m_vanished.count(g) || m_orphans.count(g)) continue;
        return g;
    }
    return Guid::Generate();
}

std::string AssetDatabase::ToProjectPath(const std::string& path) const {
    if (path.empty()) return std::string();
    const std::string p = AssetManager::Normalize(path, false);
    const bool absolute = p[0] == '/' || (p.size() >= 2 && p[1] == ':');
    if (absolute) {
        if (m_root.empty()) return std::string();
        const std::string kp = AssetManager::Normalize(path), kr = AssetManager::Normalize(m_root);
        if (kp.size() > kr.size() && kp.compare(0, kr.size(), kr) == 0 && kp[kr.size()] == '/')
            return p.substr(m_root.size() + 1);
        return std::string();
    }
    if (p == "." || p == ".." || StartsWith(p, "../")) return std::string();
    return p;
}

std::string AssetDatabase::GuidToPath(const Guid& guid) const {
    const Slot* s = SlotOf(guid);
    return s ? s->rec.path : std::string();
}

Guid AssetDatabase::PathToGuid(const std::string& path) const {
    const std::string p = ToProjectPath(path);
    if (p.empty()) return Guid();
    auto it = m_pathIndex.find(KeyOf(p));
    return it == m_pathIndex.end() ? Guid() : it->second;
}

AssetResolution AssetDatabase::Resolve(const std::string& guidOrPath) const {
    AssetResolution r;
    Guid g;
    if (Guid::Parse(guidOrPath, g)) {
        if (const Slot* s = SlotOf(g)) { r.guid = g; r.path = s->rec.path; }
        return r;
    }
    auto lookup = [&](const std::string& p) {
        if (p.empty()) return false;
        const std::string key = KeyOf(p);
        auto it = m_pathIndex.find(key);
        if (it != m_pathIndex.end()) { r.guid = it->second; r.path = SlotOf(it->second)->rec.path; return true; }
        auto rd = m_redirects.find(key);
        if (rd != m_redirects.end()) {
            if (const Slot* s = SlotOf(rd->second)) { r.guid = rd->second; r.path = s->rec.path; r.viaRedirect = true; return true; }
        }
        return false;
    };
    if (lookup(ToProjectPath(guidOrPath))) return r;
    // A relative path may also be meant relative to the working directory.
    if (!guidOrPath.empty()) {
        std::error_code ec;
        const fs::path abs = fs::absolute(PathFromU8(guidOrPath), ec);
        if (!ec && lookup(ToProjectPath(U8(abs)))) return r;
    }
    return r;
}

AssetDatabase::Slot* AssetDatabase::SlotOf(const Guid& guid) {
    auto it = m_slots.find(guid);
    return it == m_slots.end() ? nullptr : &it->second;
}
const AssetDatabase::Slot* AssetDatabase::SlotOf(const Guid& guid) const {
    auto it = m_slots.find(guid);
    return it == m_slots.end() ? nullptr : &it->second;
}

const AssetRecord* AssetDatabase::Find(const Guid& guid) const {
    const Slot* s = SlotOf(guid);
    return s ? &s->rec : nullptr;
}

const AssetRecord* AssetDatabase::Find(const std::string& guidOrPath) const {
    const AssetResolution r = Resolve(guidOrPath);
    return r ? Find(r.guid) : nullptr;
}

std::vector<const AssetRecord*> AssetDatabase::All() const {
    std::vector<const AssetRecord*> out;
    out.reserve(m_slots.size());
    for (const auto& kv : m_slots) out.push_back(&kv.second.rec);
    std::sort(out.begin(), out.end(), [](const AssetRecord* a, const AssetRecord* b) { return a->path < b->path; });
    return out;
}

std::string AssetDatabase::AbsolutePath(const Guid& guid) const {
    const Slot* s = SlotOf(guid);
    return s ? m_root + "/" + s->rec.path : std::string();
}

std::vector<Guid> AssetDatabase::OrphanGuids() const {
    std::vector<Guid> out;
    for (const auto& kv : m_orphans) out.push_back(kv.first);
    return out;
}

void AssetDatabase::RebuildIndexes() {
    m_pathIndex.clear();
    m_pathIndex.reserve(m_slots.size());
    for (const auto& kv : m_slots) m_pathIndex[KeyOf(kv.second.rec.path)] = kv.first;
}

// --- Hashing -------------------------------------------------------------------------------

std::uint64_t AssetDatabase::HashFile(const std::string& absolutePath, std::int64_t* size, std::size_t fullHashLimit) {
    if (size) *size = 0;
    std::ifstream f(PathFromU8(absolutePath), std::ios::binary);
    if (!f) return 0;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return 0;
    f.seekg(0);
    if (size) *size = static_cast<std::int64_t>(n);
    std::uint64_t h = 1469598103934665603ull;
    std::vector<char> buf(64 * 1024);
    auto feed = [&](std::streamoff count) {
        while (count > 0 && f) {
            const std::streamsize want = static_cast<std::streamsize>(std::min<std::streamoff>(count, static_cast<std::streamoff>(buf.size())));
            f.read(buf.data(), want);
            const std::streamsize got = f.gcount();
            if (got <= 0) break;
            h = savefmt::Fnv1a(buf.data(), static_cast<std::size_t>(got), h);
            count -= got;
        }
    };
    if (static_cast<unsigned long long>(n) <= fullHashLimit) {
        feed(n);
    } else {
        const std::streamoff chunk = 4 << 20;
        feed(chunk);
        f.clear();
        f.seekg(n - chunk);
        feed(chunk);
        const std::uint64_t sz = static_cast<std::uint64_t>(n);
        h = savefmt::Fnv1a(reinterpret_cast<const char*>(&sz), sizeof(sz), h);
    }
    return h == 0 ? 1 : h;
}

// --- Meta files ----------------------------------------------------------------------------

bool AssetDatabase::WriteMeta(Slot& slot, std::string* error) {
    if (slot.rec.metaReadOnly) { if (error) *error = "the meta was written by a newer layout and is left as is"; return false; }
    std::ostringstream o;
    const AssetRecord& r = slot.rec;
    o << "okaymeta " << kMetaVersion << "\n";
    o << "guid " << r.guid.ToString() << "\n";
    o << "type " << AssetTypeName(r.kind) << "\n";
    if (r.hash != 0) o << "hash " << savefmt::Hex16(r.hash) << "\n";
    o << "size " << r.size << "\n";
    for (const auto& l : r.labels) o << "label " << savefmt::Escape(l) << "\n";
    if (ImportSettings::HasSettings(r.kind) || !r.settings.extra.empty()) {
        o << "importer " << AssetTypeName(r.kind) << " " << ImportSettings::CurrentVersion(r.kind) << "\n";
        for (const auto& kv : r.settings.ToPairs()) o << "set " << kv.first << " " << savefmt::Escape(kv.second) << "\n";
    }
    for (const auto& u : slot.unknownMetaLines) o << u << "\n";
    const std::string path = m_root + "/" + MetaPathFor(r.path);
    if (!savefmt::AtomicWrite(path, o.str())) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    slot.rec.metaVersion = kMetaVersion;
    return true;
}

void AssetDatabase::Raise(const ReimportEvent& ev, std::vector<ReimportEvent>* report) {
    m_pending.push_back(ev);
    if (m_pending.size() > 100000) m_pending.erase(m_pending.begin(), m_pending.begin() + 50000);
    m_fresh.push_back(ev);
    if (report) report->push_back(ev);
}

void AssetDatabase::Deliver() {
    if (m_fresh.empty()) return;
    std::vector<ReimportEvent> events;
    events.swap(m_fresh);
    if (m_onReimport) for (const auto& e : events) m_onReimport(e);
}

std::vector<ReimportEvent> AssetDatabase::TakeReimports() {
    std::vector<ReimportEvent> out;
    out.swap(m_pending);
    return out;
}

// --- Scan ----------------------------------------------------------------------------------

ScanReport AssetDatabase::Rescan() {
    if (m_root.empty()) return ScanReport();   // nothing was scanned yet
    const std::string root = m_root;
    const ScanOptions opt = m_options;
    return Scan(root, opt);
}

ScanReport AssetDatabase::Scan(const std::string& rootIn, const ScanOptions& opt) {
    ScanReport rep;
    auto warn = [&](const std::string& w) {
        rep.warnings.push_back(w);
        if (opt.logWarnings) OKAY_WARN("[assets] ", w);
    };

    std::error_code ec;
    const fs::path rootPath = fs::absolute(PathFromU8(rootIn), ec);
    const std::string newRoot = AssetManager::Normalize(U8(rootPath), false);
    if (newRoot != m_root) { Clear(); m_root = newRoot; }
    m_options = opt;
    if (!fs::is_directory(rootPath, ec)) { warn("scan root is not a folder: " + m_root); return rep; }

    // 1. Walk the tree: asset files and metas.
    std::vector<FileEntry> files, metaFiles;
    {
        std::set<std::string> skipDirs(opt.ignoreDirs.begin(), opt.ignoreDirs.end());
        std::set<std::string> skipExts;
        for (const auto& e : opt.ignoreExtensions) skipExts.insert(Lower(e));
        fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry& e = *it;
            const std::string name = U8(e.path().filename());
            std::error_code e2;
            if (e.is_directory(e2)) {
                if (name.empty() || name[0] == '.' || skipDirs.count(name)) it.disable_recursion_pending();
                continue;
            }
            if (name.empty() || name[0] == '.' || !e.is_regular_file(e2)) continue;
            FileEntry fe;
            fe.rel = U8(e.path().lexically_relative(rootPath));
            const auto sz = e.file_size(e2);
            fe.size = e2 ? 0 : static_cast<std::int64_t>(sz);
            const auto ft = e.last_write_time(e2);
            if (!e2) { fe.stamp = static_cast<std::int64_t>(ft.time_since_epoch().count()); fe.mtime = EpochSeconds(ft); }
            const std::string ext = AssetExtension(name);
            if (ext == "okaymeta") metaFiles.push_back(std::move(fe));
            else if (!skipExts.count(ext)) files.push_back(std::move(fe));
        }
        if (ec) warn("the scan stopped early: " + ec.message());
    }
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) { return a.rel < b.rel; });
    std::sort(metaFiles.begin(), metaFiles.end(), [](const FileEntry& a, const FileEntry& b) { return a.rel < b.rel; });
    rep.scanned = static_cast<int>(files.size());

    struct Cand {
        FileEntry fe;
        bool hasMeta = false;
        ParsedMeta pm;
        std::string metaRel;
        std::int64_t metaStamp = 0;
        bool dupLoser = false;
        Guid dupOf;
        Guid guid;
        bool needsWrite = false;       // a meta must be (re)written
        bool created = false;          // brand new asset: raises Added
        std::string movedFrom, movedHow;
        bool adoptedFromDisk = false;  // the orphan's meta file was renamed into place
        bool upgradeWanted = false;    // that meta is from an older layout
        // What a new or adopted meta starts from.
        std::vector<std::string> labels;
        ImportSettings settings;
        bool settingsSet = false;
        std::vector<std::string> unknown;
        std::uint64_t baseHash = 0;    // content hash the meta recorded (the baseline for "source changed")
        bool hashed = false;
        std::uint64_t hash = 0;
        int metaVersion = 0;
        bool readOnly = false;
        bool skip = false;             // left unindexed (createMetas off)
    };
    std::vector<Cand> cands(files.size());
    std::unordered_map<std::string, std::size_t> byKey;
    for (std::size_t i = 0; i < files.size(); ++i) {
        cands[i].fe = files[i];
        byKey[KeyOf(files[i].rel)] = i;
    }

    // 2. Read the metas: pair them with assets, set corrupt ones aside, collect orphans.
    struct Orphan { std::string assetRel, metaRel; ParsedMeta pm; bool used = false; bool usable = true; };
    std::vector<Orphan> orphans;
    for (const FileEntry& mf : metaFiles) {
        const std::string assetRel = mf.rel.substr(0, mf.rel.size() - std::string(MetaExtension()).size());
        std::string text;
        ParsedMeta pm;
        if (adb::ReadAll(PathFromU8(m_root + "/" + mf.rel), kMaxMetaBytes, text)) pm = ParseMeta(text);
        else pm.error = "cannot read the file";
        auto hit = byKey.find(KeyOf(assetRel));
        if (hit != byKey.end()) {
            Cand& c = cands[hit->second];
            if (!pm.ok) {
                warn("unreadable meta " + mf.rel + " (" + pm.error + "): kept as " + mf.rel + ".corrupt, a new meta is created");
                std::error_code e2;
                const fs::path from = PathFromU8(m_root + "/" + mf.rel);
                const fs::path to = PathFromU8(m_root + "/" + mf.rel + ".corrupt");
                fs::remove(to, e2);
                fs::rename(from, to, e2);
                continue;
            }
            c.hasMeta = true;
            c.pm = std::move(pm);
            c.metaRel = mf.rel;
            c.metaStamp = mf.stamp;
            if (c.pm.newer)
                warn(mf.rel + " was written by a newer layout (v" + std::to_string(c.pm.version) + "): it is read but not rewritten");
        } else {
            std::error_code e2;
            if (fs::exists(PathFromU8(m_root + "/" + assetRel), e2)) continue;   // asset exists but is not indexed (ignored type)
            if (!pm.ok) { warn("unreadable orphan meta " + mf.rel + " (" + pm.error + "): left alone"); continue; }
            Orphan o;
            o.assetRel = assetRel;
            o.metaRel = mf.rel;
            o.pm = std::move(pm);
            orphans.push_back(std::move(o));
        }
    }

    // 3. Duplicate GUIDs: a copied meta. The original keeps the GUID, the copy gets a new one.
    {
        std::map<Guid, std::vector<std::size_t>> groups;
        for (std::size_t i = 0; i < cands.size(); ++i) if (cands[i].hasMeta) groups[cands[i].pm.guid].push_back(i);
        for (auto& kv : groups) {
            if (kv.second.size() < 2) continue;
            std::size_t keeper = kv.second[0];
            bool decided = false;
            if (const Slot* prev = SlotOf(kv.first)) {
                const std::string pk = KeyOf(prev->rec.path);
                for (std::size_t i : kv.second) if (KeyOf(cands[i].fe.rel) == pk) { keeper = i; decided = true; break; }
            }
            if (!decided) {
                for (std::size_t i : kv.second) {
                    const Cand& a = cands[i];
                    const Cand& b = cands[keeper];
                    if (a.metaStamp < b.metaStamp || (a.metaStamp == b.metaStamp && a.fe.rel < b.fe.rel)) keeper = i;
                }
            }
            for (std::size_t i : kv.second) {
                if (i == keeper) continue;
                Cand& c = cands[i];
                c.dupLoser = true;
                c.dupOf = kv.first;
                c.hasMeta = false;   // c.pm keeps the labels and settings the copy brought along
            }
        }
    }
    std::set<Guid> present;
    for (const Cand& c : cands) if (c.hasMeta) present.insert(c.pm.guid);

    // 4. Hashes: reuse what is known, compute what is needed.
    auto ensureHash = [&](Cand& c) {
        if (c.hashed) return;
        std::int64_t sz = 0;
        c.hash = HashFile(m_root + "/" + c.fe.rel, &sz, opt.fullHashLimit);
        c.hashed = true;
        if (c.hash != 0) c.fe.size = sz;
    };
    for (Cand& c : cands) {
        if (!c.hasMeta) continue;
        const Slot* prev = SlotOf(c.pm.guid);
        if (prev && prev->stamp == c.fe.stamp && prev->rec.size == c.fe.size && prev->rec.hash != 0) {
            c.hash = prev->rec.hash;
            c.hashed = true;
        } else if (!prev && !opt.verifyHashes && c.pm.hasHash && c.pm.size == c.fe.size) {
            c.hash = c.pm.hash;
            c.hashed = true;
        } else {
            ensureHash(c);
        }
    }

    // 5. Moves and renames. First the assets whose meta travelled with them.
    for (Cand& c : cands) if (c.hasMeta) c.guid = c.pm.guid;
    for (Cand& c : cands) {
        if (!c.hasMeta || !opt.detectMoves) continue;
        const Slot* prev = SlotOf(c.guid);
        if (prev && KeyOf(prev->rec.path) != KeyOf(c.fe.rel)) {
            c.movedFrom = prev->rec.path;
            c.movedHow = "guid";
        } else if (!prev) {
            auto v = m_vanished.find(c.guid);
            if (v != m_vanished.end() && KeyOf(v->second.rec.path) != KeyOf(c.fe.rel)) {
                c.movedFrom = v->second.rec.path;
                c.movedHow = "restored";
            }
        }
    }

    // Sources a new file can take its GUID from: orphan metas on disk, and assets that were
    // indexed and whose file is gone (the meta was lost with them).
    struct Source {
        Guid guid;
        std::string oldRel;
        std::uint64_t hash = 0;
        std::int64_t size = -1;
        AssetType kind = AssetType::Other;
        int type = 0;                  // 0 orphan meta on disk, 1 gone this scan, 2 gone earlier
        std::size_t orphan = 0;
        bool used = false;
    };
    std::vector<Source> sources;
    std::set<Guid> sourceGuids;
    if (opt.detectMoves) {
        for (std::size_t i = 0; i < orphans.size(); ++i) {
            Orphan& o = orphans[i];
            if (present.count(o.pm.guid) || sourceGuids.count(o.pm.guid)) { o.usable = false; continue; }
            Source s;
            s.guid = o.pm.guid; s.oldRel = o.assetRel; s.hash = o.pm.hasHash ? o.pm.hash : 0; s.size = o.pm.size;
            s.kind = AssetTypeFromPath(o.assetRel); s.type = 0; s.orphan = i;
            if (s.hash == 0) {   // the meta stores no hash: what the database remembers will do
                auto known = m_slots.find(s.guid);
                if (known != m_slots.end()) { s.hash = known->second.rec.hash; s.size = known->second.rec.size; }
                else { auto gone = m_vanished.find(s.guid); if (gone != m_vanished.end()) { s.hash = gone->second.rec.hash; s.size = gone->second.rec.size; } }
            }
            sources.push_back(s);
            sourceGuids.insert(s.guid);
        }
        for (const auto& kv : m_slots) {
            if (present.count(kv.first) || sourceGuids.count(kv.first)) continue;
            if (byKey.count(KeyOf(kv.second.rec.path))) continue;   // that path still holds some other asset
            Source s;
            s.guid = kv.first; s.oldRel = kv.second.rec.path; s.hash = kv.second.rec.hash; s.size = kv.second.rec.size;
            s.kind = kv.second.rec.kind; s.type = 1;
            sources.push_back(s);
            sourceGuids.insert(s.guid);
        }
        for (const auto& kv : m_vanished) {
            if (present.count(kv.first) || sourceGuids.count(kv.first)) continue;
            Source s;
            s.guid = kv.first; s.oldRel = kv.second.rec.path; s.hash = kv.second.rec.hash; s.size = kv.second.rec.size;
            s.kind = kv.second.rec.kind; s.type = 2;
            sources.push_back(s);
            sourceGuids.insert(s.guid);
        }
    }

    std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) {
        return a.type != b.type ? a.type < b.type : a.oldRel < b.oldRel;
    });

    auto adopt = [&](Cand& c, Source& s, const char* how) {
        s.used = true;
        c.guid = s.guid;
        c.movedFrom = s.oldRel;
        c.movedHow = how;
        c.needsWrite = true;   // a meta must be written, unless an orphan's own file is moved into place below
        if (s.type == 0) {
            const Orphan& o = orphans[s.orphan];
            c.labels = o.pm.labels;
            c.unknown = o.pm.unknown;
            c.baseHash = o.pm.hasHash ? o.pm.hash : 0;
            c.metaVersion = o.pm.version;
            c.readOnly = o.pm.newer;
            std::vector<std::string> notes;
            bool rewrite = false;
            c.settings = SettingsFrom(AssetTypeFromPath(c.fe.rel), o.pm, notes, rewrite);
            c.settingsSet = true;
            c.upgradeWanted = rewrite || o.pm.version < kMetaVersion;
            // Move the meta file next to its asset again.
            std::error_code e2;
            const fs::path from = PathFromU8(m_root + "/" + o.metaRel);
            const fs::path to = PathFromU8(m_root + "/" + MetaPathFor(c.fe.rel));
            fs::create_directories(to.parent_path(), e2);
            fs::rename(from, to, e2);
            if (!e2) { c.adoptedFromDisk = true; c.needsWrite = false; }
        } else if (s.type == 1) {
            const Slot& old = m_slots.at(s.guid);
            c.labels = old.rec.labels;
            c.settings = old.rec.settings;
            c.settingsSet = true;
            c.unknown = old.unknownMetaLines;
            c.baseHash = old.rec.hash;
            c.metaVersion = old.rec.metaVersion;
        } else {
            const Vanished& v = m_vanished.at(s.guid);
            c.labels = v.rec.labels;
            c.settings = v.rec.settings;
            c.settingsSet = true;
            c.unknown = v.unknownMetaLines;
            c.baseHash = v.rec.hash;
            c.metaVersion = v.rec.metaVersion;
        }
    };

    // Assets without a meta (and copied metas, which need a new GUID).
    std::vector<std::size_t> unmetaed;
    for (std::size_t i = 0; i < cands.size(); ++i) if (!cands[i].hasMeta) unmetaed.push_back(i);

    // 5a. The meta was deleted but the asset stayed where it was: the database still knows its GUID.
    if (opt.detectMoves) {
        for (std::size_t i : unmetaed) {
            Cand& c = cands[i];
            if (c.dupLoser) continue;
            auto pi = m_pathIndex.find(KeyOf(c.fe.rel));
            if (pi == m_pathIndex.end() || present.count(pi->second)) continue;
            auto sl = m_slots.find(pi->second);
            if (sl == m_slots.end()) continue;
            c.guid = pi->second;
            c.needsWrite = true;
            c.labels = sl->second.rec.labels;
            c.settings = sl->second.rec.settings;
            c.settingsSet = true;
            c.unknown = sl->second.unknownMetaLines;
            c.baseHash = sl->second.rec.hash;
            c.metaVersion = sl->second.rec.metaVersion;
            present.insert(c.guid);
        }
    }

    if (opt.detectMoves && !sources.empty()) {
        // 5b. Same content (and size): the file was moved or renamed without its meta.
        for (std::size_t i : unmetaed) {
            Cand& c = cands[i];
            if (c.dupLoser || !c.guid.IsNull()) continue;
            ensureHash(c);
            if (c.hash == 0) continue;
            int bestScore = -1;
            Source* best = nullptr;
            for (Source& s : sources) {
                if (s.used || s.hash == 0 || s.hash != c.hash) continue;
                if (s.size >= 0 && s.size != c.fe.size) continue;
                int score = 0;
                if (Lower(AssetFileName(s.oldRel)) == Lower(AssetFileName(c.fe.rel))) score += 2;
                if (AssetExtension(s.oldRel) == AssetExtension(c.fe.rel)) score += 1;
                if (score > bestScore) { bestScore = score; best = &s; }
            }
            if (best) adopt(c, *best, "hash");
        }
        // 5c. Last resort: exactly one orphan and one new file share a name and type.
        if (opt.matchByName) {
            std::map<std::string, std::vector<std::size_t>> newByName;
            for (std::size_t i : unmetaed) {
                const Cand& c = cands[i];
                if (c.dupLoser || !c.guid.IsNull()) continue;
                newByName[Lower(AssetFileName(c.fe.rel))].push_back(i);
            }
            std::map<std::string, std::vector<std::size_t>> srcByName;
            for (std::size_t k = 0; k < sources.size(); ++k) {
                if (sources[k].used || sources[k].type == 2) continue;   // only recent losses and orphan metas
                srcByName[Lower(AssetFileName(sources[k].oldRel))].push_back(k);
            }
            for (auto& kv : newByName) {
                auto sIt = srcByName.find(kv.first);
                if (sIt == srcByName.end() || kv.second.size() != 1 || sIt->second.size() != 1) continue;
                Cand& c = cands[kv.second[0]];
                Source& s = sources[sIt->second[0]];
                if (s.kind != AssetTypeFromPath(c.fe.rel)) continue;
                adopt(c, s, "name");
                warn(c.fe.rel + " took the GUID of " + s.oldRel + " (same name, content changed)");
            }
        }
    }

    // 5d. Whatever is left: new assets (and copies) get fresh GUIDs and metas.
    for (std::size_t i : unmetaed) {
        Cand& c = cands[i];
        if (!c.guid.IsNull()) continue;
        if (!opt.createMetas) { c.skip = true; ++rep.unmetaed; continue; }
        c.guid = NewGuid();
        c.created = true;
        c.needsWrite = true;
        if (c.dupLoser) {
            c.labels = c.pm.labels;
            c.unknown = c.pm.unknown;
            std::vector<std::string> notes;
            bool rewrite = false;
            c.settings = SettingsFrom(AssetTypeFromPath(c.fe.rel), c.pm, notes, rewrite);
            c.settingsSet = true;
            rep.duplicates.push_back({c.fe.rel, c.dupOf, c.guid});
            warn("duplicate guid " + c.dupOf.ToString() + " on " + c.fe.rel + ": it got a new guid " + c.guid.ToString());
        }
    }

    // 6. Build the new slots.
    std::map<Guid, Slot> next;
    std::vector<ReimportEvent> events;
    for (Cand& c : cands) {
        if (c.skip || c.guid.IsNull()) continue;
        ensureHash(c);
        const AssetType kind = AssetTypeFromPath(c.fe.rel);
        auto old = m_slots.find(c.guid);
        const bool existed = old != m_slots.end();
        Slot s;
        if (existed) s = std::move(old->second);
        s.rec.guid = c.guid;
        s.rec.path = c.fe.rel;
        s.rec.kind = kind;
        s.rec.size = c.fe.size;
        s.rec.modified = c.fe.mtime;
        s.stamp = c.fe.stamp;

        std::uint64_t baseline = 0;
        std::string newPrint;
        bool rewrite = false;
        std::vector<std::string> notes;
        if (c.hasMeta) {
            s.rec.labels = c.pm.labels;
            s.rec.settings = SettingsFrom(kind, c.pm, notes, rewrite);
            s.unknownMetaLines = c.pm.unknown;
            s.rec.metaVersion = c.pm.version;
            s.rec.metaReadOnly = c.pm.newer;
            baseline = existed ? s.rec.hash : (c.pm.hasHash ? c.pm.hash : 0);
            if (c.pm.version < kMetaVersion) rewrite = true;
        } else {
            s.rec.labels = c.labels;
            if (c.settingsSet) {
                s.rec.settings = c.settings;
                if (s.rec.settings.kind != kind) s.rec.settings = ImportSettings::Defaults(kind);
            } else {
                s.rec.settings = ImportSettings::Defaults(kind);
            }
            s.unknownMetaLines = c.unknown;
            s.rec.metaVersion = c.metaVersion;
            s.rec.metaReadOnly = c.readOnly;
            baseline = existed ? s.rec.hash : c.baseHash;
        }
        for (const auto& n : notes) if (n.find("migrated") == std::string::npos) warn(c.fe.rel + ": " + n);
        s.rec.settings.Clamp();
        newPrint = s.rec.settings.Fingerprint();

        // Signals.
        const bool settingsChanged = existed && s.settingsPrint != newPrint;
        const bool sourceChanged = baseline != 0 && c.hash != 0 && baseline != c.hash;
        s.rec.hash = c.hash;
        s.settingsPrint = newPrint;
        if (c.created) Raise({c.guid, c.fe.rel, ReimportEvent::Reason::Added}, &events);
        else if (sourceChanged) Raise({c.guid, c.fe.rel, ReimportEvent::Reason::SourceChanged}, &events);
        if (!c.created && settingsChanged) Raise({c.guid, c.fe.rel, ReimportEvent::Reason::SettingsChanged}, &events);

        // Does the meta on disk need to change?
        bool write = c.needsWrite;
        if (c.hasMeta) {
            if (rewrite && opt.upgradeMetas) { write = true; ++rep.upgraded; }
            if (opt.storeHashInMeta && c.hash != 0 && (!c.pm.hasHash || c.pm.hash != c.hash || c.pm.size != c.fe.size)) write = true;
            if (rewrite && !opt.upgradeMetas) write = false;   // an old-layout meta is left exactly as it is
        } else if (c.adoptedFromDisk) {
            if (opt.storeHashInMeta && c.hash != 0 && c.baseHash != c.hash) write = true;
            if (c.upgradeWanted && opt.upgradeMetas) { write = true; ++rep.upgraded; }
        }
        if (c.created) ++rep.created;
        if (write && !s.rec.metaReadOnly) {
            std::string err;
            if (!opt.storeHashInMeta) {
                const std::uint64_t keep = s.rec.hash;
                s.rec.hash = c.hasMeta && c.pm.hasHash ? c.pm.hash : 0;
                if (!WriteMeta(s, &err)) warn(err);
                s.rec.hash = keep;
            } else if (!WriteMeta(s, &err)) {
                warn(err);
            }
        }
        if (!c.movedFrom.empty()) {
            ScanReport::Move mv;
            mv.guid = c.guid; mv.from = c.movedFrom; mv.to = c.fe.rel; mv.how = c.movedHow;
            rep.moved.push_back(mv);
            m_redirects[KeyOf(c.movedFrom)] = c.guid;
        }
        next[c.guid] = std::move(s);
    }

    // 7. Assets that are gone; orphans that were not matched.
    for (auto& kv : m_slots) {
        if (next.count(kv.first)) continue;
        // `kv.second` may have been moved from above only when it is in `next`, so this one is intact.
        rep.removed.push_back(kv.second.rec.path);
        Vanished v;
        v.rec = kv.second.rec;
        v.settingsPrint = kv.second.settingsPrint;
        v.unknownMetaLines = kv.second.unknownMetaLines;
        m_vanished[kv.first] = std::move(v);
    }
    for (const auto& kv : next) m_vanished.erase(kv.first);
    while (m_vanished.size() > 4096) m_vanished.erase(m_vanished.begin());
    m_orphans.clear();
    for (std::size_t i = 0; i < orphans.size(); ++i) {
        Orphan& o = orphans[i];
        bool taken = false;
        for (const Source& s : sources) if (s.type == 0 && s.orphan == i && s.used) taken = true;
        if (taken) continue;
        rep.orphanMetas.push_back(o.assetRel);
        if (opt.deleteOrphanMetas) {
            std::error_code e2;
            fs::remove(PathFromU8(m_root + "/" + o.metaRel), e2);
            continue;
        }
        if (o.usable) m_orphans[o.pm.guid] = o.assetRel;
    }

    std::sort(rep.removed.begin(), rep.removed.end());
    std::sort(rep.orphanMetas.begin(), rep.orphanMetas.end());
    m_slots.swap(next);
    RebuildIndexes();
    // Redirects: drop those that point at live paths or at assets we no longer know.
    for (auto it = m_redirects.begin(); it != m_redirects.end();) {
        if (m_pathIndex.count(it->first) || (!m_slots.count(it->second) && !m_vanished.count(it->second))) it = m_redirects.erase(it);
        else ++it;
    }

    // 8. Dependencies.
    if (opt.buildDependencies) for (auto& kv : m_slots) ExtractDependencies(kv.second, false);
    ResolveAllEdges();

    rep.reimports = std::move(events);
    Deliver();
    return rep;
}

// --- Changes -------------------------------------------------------------------------------

Guid AssetDatabase::RefreshFile(const std::string& path) {
    AssetResolution r = Resolve(path);
    if (!r) {
        // A new file: index it (and everything else that changed) with a normal scan.
        const std::string p = ToProjectPath(path);
        if (p.empty() || m_root.empty()) return Guid();
        std::error_code ec;
        if (!fs::is_regular_file(PathFromU8(m_root + "/" + p), ec)) return Guid();
        Rescan();
        return PathToGuid(p);
    }
    Slot* s = SlotOf(r.guid);
    if (!s) return Guid();
    const std::string abs = m_root + "/" + s->rec.path;
    std::error_code ec;
    if (!fs::is_regular_file(PathFromU8(abs), ec)) return Guid();
    std::int64_t size = 0;
    const std::uint64_t h = HashFile(abs, &size, m_options.fullHashLimit);
    const auto ft = fs::last_write_time(PathFromU8(abs), ec);
    if (!ec) { s->stamp = static_cast<std::int64_t>(ft.time_since_epoch().count()); s->rec.modified = EpochSeconds(ft); }
    s->rec.size = size;
    if (h != 0 && h != s->rec.hash) {
        s->rec.hash = h;
        if (m_options.storeHashInMeta && !s->rec.metaReadOnly) { std::string err; WriteMeta(*s, &err); }
        ExtractDependencies(*s, false);
        ResolveEdgesOf(*s);
        Raise({r.guid, s->rec.path, ReimportEvent::Reason::SourceChanged});
        Deliver();
    }
    return r.guid;
}

bool AssetDatabase::MoveAsset(const std::string& guidOrPath, const std::string& newPath, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    const AssetResolution r = Resolve(guidOrPath);
    if (!r) return fail("unknown asset: " + guidOrPath);
    Slot* s = SlotOf(r.guid);
    const std::string to = ToProjectPath(newPath);
    if (to.empty()) return fail("the new path is outside the project: " + newPath);
    if (AssetTypeFromPath(to) != s->rec.kind) return fail("the new extension would change the asset type");
    const std::string from = s->rec.path;
    if (KeyOf(from) == KeyOf(to)) {
        if (from == to) return true;
    } else if (m_pathIndex.count(KeyOf(to))) {
        return fail("another asset is already at " + to);
    }
    std::error_code ec;
    const fs::path fromAbs = PathFromU8(m_root + "/" + from), toAbs = PathFromU8(m_root + "/" + to);
    const fs::path fromMeta = PathFromU8(m_root + "/" + MetaPathFor(from)), toMeta = PathFromU8(m_root + "/" + MetaPathFor(to));
    if (KeyOf(from) != KeyOf(to) && (fs::exists(toAbs, ec) || fs::exists(toMeta, ec))) return fail("a file already exists at " + to);
    fs::create_directories(toAbs.parent_path(), ec);
    ec.clear();
    fs::rename(fromAbs, toAbs, ec);
    if (ec) return fail("cannot move " + from + ": " + ec.message());
    if (fs::exists(fromMeta, ec)) {
        ec.clear();
        fs::rename(fromMeta, toMeta, ec);
        if (ec) {
            std::error_code undo;
            fs::rename(toAbs, fromAbs, undo);
            return fail("cannot move the meta of " + from + ": " + ec.message());
        }
    }
    s->rec.path = to;
    m_redirects[KeyOf(from)] = r.guid;
    RebuildIndexes();
    m_redirects.erase(KeyOf(to));
    ResolveAllEdges();
    if (!s->rec.metaReadOnly && !fs::exists(toMeta, ec)) { std::string err; WriteMeta(*s, &err); }
    return true;
}

int AssetDatabase::RemoveOrphanMetas() {
    int n = 0;
    for (const auto& kv : m_orphans) {
        std::error_code ec;
        if (fs::remove(PathFromU8(m_root + "/" + MetaPathFor(kv.second)), ec)) ++n;
    }
    m_orphans.clear();
    return n;
}

namespace {
std::vector<std::string> CleanLabels(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (std::string l : in) {
        for (char& c : l) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        l = Trim(l);
        if (l.empty()) continue;
        if (seen.insert(Lower(l)).second) out.push_back(l);
    }
    return out;
}
} // namespace

bool AssetDatabase::SetLabels(const Guid& guid, const std::vector<std::string>& labels, std::string* error) {
    Slot* s = SlotOf(guid);
    if (!s) { if (error) *error = "unknown asset"; return false; }
    std::vector<std::string> clean = CleanLabels(labels);
    if (clean == s->rec.labels) return true;
    if (s->rec.metaReadOnly) { if (error) *error = "the meta was written by a newer layout and is left as is"; return false; }
    std::vector<std::string> old = std::move(s->rec.labels);
    s->rec.labels = std::move(clean);
    if (!WriteMeta(*s, error)) { s->rec.labels = std::move(old); return false; }
    return true;
}

bool AssetDatabase::AddLabel(const Guid& guid, const std::string& label, std::string* error) {
    const AssetRecord* r = Find(guid);
    if (!r) { if (error) *error = "unknown asset"; return false; }
    std::vector<std::string> l = r->labels;
    l.push_back(label);
    return SetLabels(guid, l, error);
}

bool AssetDatabase::RemoveLabel(const Guid& guid, const std::string& label, std::string* error) {
    const AssetRecord* r = Find(guid);
    if (!r) { if (error) *error = "unknown asset"; return false; }
    std::vector<std::string> l;
    const std::string k = Lower(Trim(label));
    for (const auto& x : r->labels) if (Lower(x) != k) l.push_back(x);
    return SetLabels(guid, l, error);
}

std::vector<std::pair<std::string, std::size_t>> AssetDatabase::AllLabels() const {
    std::map<std::string, std::pair<std::string, std::size_t>> counts;   // lower -> (first spelling, count)
    for (const AssetRecord* r : All())   // by path, so the spelling shown is the same every run
        for (const auto& l : r->labels) {
            auto& e = counts[Lower(l)];
            if (e.second == 0) e.first = l;
            ++e.second;
        }
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const auto& kv : counts) out.emplace_back(kv.second.first, kv.second.second);
    return out;
}

const ImportSettings* AssetDatabase::GetSettings(const Guid& guid) const {
    const Slot* s = SlotOf(guid);
    return s ? &s->rec.settings : nullptr;
}

bool AssetDatabase::SetSettings(const Guid& guid, const ImportSettings& settings, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    Slot* s = SlotOf(guid);
    if (!s) return fail("unknown asset");
    if (!ImportSettings::HasSettings(s->rec.kind)) return fail(std::string("a ") + AssetTypeName(s->rec.kind) + " has no import settings");
    if (settings.kind != s->rec.kind) return fail(std::string("these settings are for a ") + AssetTypeName(settings.kind) + ", the asset is a " + AssetTypeName(s->rec.kind));
    ImportSettings n = settings;
    n.Clamp();
    n.version = ImportSettings::CurrentVersion(n.kind);
    n.fromNewerVersion = false;
    if (n.SameAs(s->rec.settings)) return true;
    if (s->rec.metaReadOnly) return fail("the meta was written by a newer layout and is left as is");
    ImportSettings old = s->rec.settings;
    const std::string oldPrint = s->settingsPrint;
    s->rec.settings = n;
    s->settingsPrint = n.Fingerprint();
    if (!WriteMeta(*s, error)) { s->rec.settings = old; s->settingsPrint = oldPrint; return false; }
    Raise({guid, s->rec.path, ReimportEvent::Reason::SettingsChanged});
    Deliver();
    return true;
}

std::function<void(const std::string&, const std::string&)> AssetDatabase::ReloadHook() {
    return [this](const std::string& key, const std::string&) {
        AssetResolution r = Resolve(key);
        if (r) RefreshFile(r.path);
    };
}

void AssetDatabase::AttachHotReload(AssetManager& manager,
                                    std::function<void(const std::string&, const std::string&)> chain) {
    auto hook = ReloadHook();
    manager.SetReloadCallback([hook, chain](const std::string& key, const std::string& type) {
        hook(key, type);
        if (chain) chain(key, type);
    });
}

} // namespace okay
