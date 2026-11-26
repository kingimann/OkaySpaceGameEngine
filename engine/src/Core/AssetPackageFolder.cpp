// Asset packages and a game's data folder: mounting the packages packages.manifest lists, the materialised view for the
// parts of the engine that open files by path, and packing an exported data folder (with each asset's dependencies from
// the asset database's reference scanners). Written for this engine from docs/engine/asset_packages.md.
#include "okay/Core/AssetPackage.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/StringUtil.hpp"   // str::LowerAscii
#include "okay/Core/SaveData.hpp"   // savefmt::AtomicWriteChecked, MoveOver, ReadFileCapped
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace okay {
namespace assetpkg {

namespace fs = std::filesystem;

namespace {

constexpr const char* kManifestFile = "packages.manifest";
constexpr const char* kViewState = ".okaypkg-view";

std::int64_t Stamp(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : (std::int64_t)t.time_since_epoch().count();
}
bool SameBytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    char x[65536], y[65536];
    while (fa && fb) {
        fa.read(x, sizeof(x)); fb.read(y, sizeof(y));
        if (fa.gcount() != fb.gcount() || std::memcmp(x, y, (std::size_t)fa.gcount()) != 0) return false;
    }
    return fa.eof() && fb.eof();
}

// Write bytes to `target` through "<target>.part".
bool WriteThrough(const fs::path& target, const std::vector<std::uint8_t>& bytes, std::string& why) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::path tmp = target;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { why = "cannot create " + tmp.u8string(); return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); why = "cannot write " + target.u8string() + " (is the disk full?)"; return false; }
    }
    if (!savefmt::MoveOver(tmp, target, ec)) { fs::remove(tmp, ec); why = "cannot replace " + target.u8string() + " (" + ec.message() + ")"; return false; }
    return true;
}

struct ViewRow { std::uint64_t hash = 0, size = 0; std::int64_t stamp = 0; std::string name; };

// The view's state file: "okaypkg-view 1" then "<id> <content hash> <size> <time> <name>" per file written. It lives in
// the per-user cache, so it is read carefully: a line that does not fit is skipped (that file is just written again).
std::map<AssetId, ViewRow> ReadViewState(const fs::path& file) {
    std::map<AssetId, ViewRow> rows;
    std::string text;
    if (!savefmt::ReadFileCapped(file.u8string(), 64u << 20, text)) return rows;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "okaypkg-view 1") return rows;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string id, hash, size, stamp;
        if (!(ls >> id >> hash >> size >> stamp)) continue;
        std::string name;
        std::getline(ls, name);
        if (!name.empty() && name[0] == ' ') name.erase(0, 1);
        ViewRow r;
        char* end = nullptr;
        const AssetId i = std::strtoull(id.c_str(), &end, 16);
        if (*end || id.size() != 16) continue;
        r.hash = std::strtoull(hash.c_str(), &end, 16); if (*end) continue;
        r.size = std::strtoull(size.c_str(), &end, 10); if (*end) continue;
        r.stamp = std::strtoll(stamp.c_str(), &end, 10); if (*end) continue;
        if (!IsValidName(name) || IdOf(name) != i) continue;
        r.name = name;
        rows[i] = r;
    }
    return rows;
}

} // namespace

bool MountFolder(const std::string& dataDir, PackageStack& out, std::string& why, const Limits& limits) {
    out = PackageStack();
    const fs::path dir = fs::u8path(dataDir);
    std::error_code ec;
    std::vector<ManifestItem> items;
    const fs::path mf = dir / kManifestFile;
    const bool haveManifest = fs::is_regular_file(mf, ec);
    if (haveManifest) {
        std::string text;
        if (!savefmt::ReadFileCapped(mf.u8string(), 1u << 20, text, &why)) { why = "cannot read " + mf.u8string() + " (" + why + ")"; return false; }
        if (!ParseManifest(text, items, why)) return false;
        if (items.empty()) { why = "packages.manifest lists no package"; return false; }
    } else if (fs::is_regular_file(dir / "game.okaypkg", ec)) {
        items.push_back({"game.okaypkg", 0, 0});
    } else {
        return true;   // loose files: nothing to mount
    }
    for (const ManifestItem& it : items) {
        const fs::path p = dir / fs::u8path(it.file);
        const std::uintmax_t size = fs::file_size(p, ec);
        if (ec) { why = "the game's package " + it.file + " is missing (" + ec.message() + "): reinstall or update the game"; return false; }
        if (haveManifest && size != it.size) {
            why = "the game's package " + it.file + " is " + std::to_string(size) + " bytes, packages.manifest says " + std::to_string(it.size) +
                  " (an unfinished download or a damaged file): reinstall or update the game";
            return false;
        }
        std::string w;
        auto pkg = Package::OpenFile(p.u8string(), w, limits);
        if (!pkg) { why = "the game's package cannot be used: " + w; return false; }
        if (!out.Mount(pkg, why)) return false;
    }
    return true;
}

bool Materialize(const PackageStack& stack, const std::string& dataDir, const std::string& viewDir, MaterializeStats* stats, std::string& why) {
    const auto t0 = std::chrono::steady_clock::now();
    MaterializeStats st;
    const fs::path view = fs::u8path(viewDir);
    std::error_code ec;
    fs::create_directories(view, ec);
    if (!fs::is_directory(view, ec)) { why = "cannot create the folder " + viewDir; return false; }
    const fs::path stateFile = view / kViewState;
    const std::map<AssetId, ViewRow> old = ReadViewState(stateFile);
    std::map<AssetId, ViewRow> now;
    std::set<std::string> packedKeys;   // lower-case names of the visible entries

    const std::vector<PackageStack::Hit> visible = stack.Visible();
    {   // Files of earlier versions that are gone now are deleted first, so a new file may take a removed one's place.
        std::set<AssetId> ids;
        for (const PackageStack::Hit& h : visible) ids.insert(h.entry->id);
        for (const auto& o : old) {
            if (ids.count(o.first)) continue;
            fs::path p;
            if (pathsafety::StaysInside(view, o.second.name, &p) && fs::is_regular_file(p, ec)) { fs::remove(p, ec); ++st.removed; }
        }
    }
    for (const PackageStack::Hit& h : visible) {
        const std::string name = stack.NameOf(h.entry->id);
        if (name.empty()) { why = "the game's packages have no file names (they were stripped), so they cannot be laid out as files"; return false; }
        fs::path target;
        std::string w;
        if (!pathsafety::StaysInside(view, name, &target, &w)) { why = "'" + name + "' cannot be written: " + w; return false; }
        packedKeys.insert(str::LowerAscii(name));
        auto o = old.find(h.entry->id);
        if (o != old.end() && o->second.name == name && o->second.hash == h.entry->contentHash && o->second.size == h.entry->size) {
            const std::uintmax_t sz = fs::file_size(target, ec);
            if (!ec && sz == h.entry->size && Stamp(target) == o->second.stamp) { now[h.entry->id] = o->second; ++st.kept; continue; }
        }
        if (o != old.end() && o->second.name != name) {   // the same asset under another letter case: the old file goes first
            fs::path oldPath;
            if (pathsafety::StaysInside(view, o->second.name, &oldPath)) fs::remove(oldPath, ec);
        }
        std::vector<std::uint8_t> bytes;
        if (!h.package->Read(*h.entry, bytes, why)) return false;
        if (!WriteThrough(target, bytes, why)) return false;
        ViewRow r;
        r.hash = h.entry->contentHash; r.size = h.entry->size; r.stamp = Stamp(target); r.name = name;
        now[h.entry->id] = r;
        ++st.written;
    }
    // Loose files beside the packages (a publisher's content.okayconfig, a small config) are seen in the view too.
    const fs::path data = fs::u8path(dataDir);
    for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string n = it->path().filename().u8string();
        if (str::EndsWithNoCaseAscii(n, ".okaypkg") || str::EndsWithNoCaseAscii(n, ".part") || n == kManifestFile || packedKeys.count(str::LowerAscii(n)) || n == kViewState) continue;
        if (!IsValidName(n)) continue;
        const fs::path target = view / it->path().filename();
        if (fs::exists(target, ec) && SameBytes(it->path(), target)) continue;
        fs::path tmp = target;
        tmp += ".part";
        fs::copy_file(it->path(), tmp, fs::copy_options::overwrite_existing, ec);
        if (ec || !savefmt::MoveOver(tmp, target, ec)) { fs::remove(tmp, ec); why = "cannot copy " + n + " into " + viewDir; return false; }
        ++st.loose;
    }
    std::string text = "okaypkg-view 1\n";
    for (const auto& r : now)
        text += Hex64(r.first) + " " + Hex64(r.second.hash) + " " + std::to_string(r.second.size) + " " + std::to_string(r.second.stamp) + " " + r.second.name + "\n";
    std::string w;
    if (!savefmt::AtomicWriteChecked(stateFile.u8string(), text, &w, {false, false, true})) { why = "cannot write " + stateFile.u8string() + " (" + w + ")"; return false; }
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (stats) *stats = st;
    return true;
}

bool PackFolder(const std::string& dataDir, const std::string& packageFile, const PackFolderOptions& options, PackFolderResult& result,
                std::string& why) {
    result = PackFolderResult();
    const fs::path dir = fs::u8path(dataDir);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { why = dataDir + " is not a folder"; return false; }
    if (!IsValidName(packageFile, &why) || packageFile.find('/') != std::string::npos || !str::EndsWithNoCaseAscii(packageFile, ".okaypkg")) {
        why = "'" + packageFile + "' is not a package file name";
        return false;
    }
    std::set<std::string> loose;
    for (const std::string& k : options.keepLoose) loose.insert(str::LowerAscii(k));
    // 1) The files to pack: everything but the loose names, packages, the manifest and half-written files.
    std::vector<std::pair<std::string, fs::path>> files;   // name, path
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), dir, ec);
        const std::string top = str::LowerAscii(rel.begin() == rel.end() ? std::string() : rel.begin()->u8string());
        if (loose.count(top)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const std::string name = rel.generic_u8string();
        if (str::EndsWithNoCaseAscii(name, ".okaypkg") || str::EndsWithNoCaseAscii(name, ".part") || name == kManifestFile) continue;
        if (!IsValidName(name, &why)) { why = "cannot pack the game: " + why; return false; }
        files.push_back({name, it->path()});
    }
    if (ec) { why = "cannot list " + dataDir + " (" + ec.message() + ")"; return false; }
    std::sort(files.begin(), files.end());

    // 2) What each asset uses, from the asset database's reference scanners (nothing is written into the folder).
    std::unordered_map<std::string, std::vector<std::string>> deps;
    std::unordered_map<std::string, std::uint8_t> kinds;
    if (options.dependencies) {
        AssetDatabase db;
        ScanOptions so;
        so.createMetas = false; so.indexUnmetaed = true; so.detectMoves = false; so.matchByName = false;
        so.verifyHashes = false; so.storeHashInMeta = false; so.upgradeMetas = false; so.logWarnings = false;
        for (const std::string& k : options.keepLoose) so.ignoreDirs.push_back(k);
        so.ignoreExtensions.push_back("okaypkg");
        db.Scan(dataDir, so);
        for (const AssetRecord* r : db.All()) {
            kinds[str::LowerAscii(r->path)] = (std::uint8_t)r->kind;
            for (const Guid& g : db.DependenciesOf(r->guid)) {
                const std::string p = db.GuidToPath(g);
                if (!p.empty()) deps[str::LowerAscii(r->path)].push_back(p);
            }
        }
    }

    auto build = [&](bool trustCache, WriteStats& st) {
        PackageWriter w;
        result.dependencyLinks = 0;
        for (const auto& f : files) {
            const auto d = deps.find(str::LowerAscii(f.first));
            std::vector<std::string> dl = d == deps.end() ? std::vector<std::string>() : d->second;
            result.dependencyLinks += dl.size();
            const auto k = kinds.find(str::LowerAscii(f.first));
            if (!w.AddFile(f.first, f.second.u8string(), std::move(dl), k == kinds.end() ? 0 : k->second, &why)) return false;
        }
        WriteOptions wo = options.write;
        wo.trustCache = trustCache && wo.trustCache;
        wo.baseStackHash = 0;
        return w.Write((dir / fs::u8path(packageFile)).u8string(), wo, &st, &why);
    };
    const std::string pkgPath = (dir / fs::u8path(packageFile)).u8string();
    auto verify = [&]() {
        auto p = Package::OpenFile(pkgPath, why);
        return p && p->VerifyAll(why);
    };
    WriteStats st;
    if (!build(true, st)) { fs::remove(fs::u8path(pkgPath), ec); return false; }
    if (!verify()) {
        // A compressed copy from the cache was bad (or the disk lied): write it again from the sources alone.
        const std::string first = why;
        if (options.write.cacheDir.empty() || !build(false, st) || !verify()) {
            fs::remove(fs::u8path(pkgPath), ec);
            why = "the package did not verify after writing: " + first;
            return false;
        }
    }
    std::uint64_t hash = 0, size = 0;
    if (!HashFile(pkgPath, hash, size, why)) return false;
    const std::string manifest = (dir / kManifestFile).u8string();
    std::string w;
    if (!savefmt::AtomicWriteChecked(manifest, WriteManifest({{packageFile, size, hash}}), &w, {false, false, true})) {
        why = "cannot write " + manifest + " (" + w + ")";
        return false;
    }
    result.stats = st;
    result.packagePath = pkgPath;
    result.manifestPath = manifest;
    for (const auto& f : files) result.packed.push_back(f.first);
    if (options.removeLoose) {
        std::set<fs::path> dirs;
        for (const auto& f : files) {
            fs::remove(f.second, ec);
            for (fs::path p = f.second.parent_path(); !p.empty() && p != dir && p.u8string().size() > dir.u8string().size(); p = p.parent_path())
                dirs.insert(p);
        }
        // Folders left empty, deepest first.
        std::vector<fs::path> order(dirs.begin(), dirs.end());
        std::sort(order.begin(), order.end(), [](const fs::path& a, const fs::path& b) { return a.u8string().size() > b.u8string().size(); });
        for (const fs::path& d : order) if (fs::is_directory(d, ec) && fs::is_empty(d, ec)) fs::remove(d, ec);
    }
    return true;
}

} // namespace assetpkg
} // namespace okay
