// The loaders' view of an asset's `.okaymeta` (ImportSidecar.hpp). Written for this engine from the meta layout
// the asset database writes (AssetDatabase.cpp, WriteMeta: `importer <kind> <version>` then `set <key> <escaped
// value>` lines); the typed reading, migration and clamping are ImportSettings::FromPairs, shared with the editor.
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Debug/DebugUtil.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace okay {

namespace {

namespace fs = std::filesystem;

using str::LowerAscii;

// The block of `kind` in the meta text: its version and its `set` lines (values unescaped). A file may hold the
// block more than once (hand edits): later lines win, as they always did.
bool ParseBlock(std::istream& in, const std::string& kindName, int& version, ImportSettings::Pairs& pairs) {
    std::string line;
    bool right = false, found = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("importer ", 0) == 0) {
            std::istringstream is(line.substr(9));
            std::string kind;
            is >> kind;
            right = LowerAscii(kind) == kindName;
            if (right) {
                found = true;
                int v = 1;
                if (is >> v) version = v;
            }
            continue;
        }
        if (!right || line.rfind("set ", 0) != 0) continue;
        std::string rest = line.substr(4);
        const std::size_t sp = rest.find_first_of(" \t");
        const std::string key = sp == std::string::npos ? rest : rest.substr(0, sp);
        std::string val = sp == std::string::npos ? std::string() : rest.substr(sp + 1);
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
        if (key.empty()) continue;
        const std::string unescaped = savefmt::Unescape(val);
        bool replaced = false;
        for (auto& kv : pairs) if (kv.first == key) { kv.second = unescaped; replaced = true; }
        if (!replaced) pairs.emplace_back(key, unescaped);
    }
    return found;
}

ImportSidecar ReadUncached(const std::string& metaPath, AssetType kind) {
    ImportSidecar sc;
    sc.settings = ImportSettings::Defaults(kind);
    std::ifstream in(metaPath, std::ios::binary);
    if (!in) return sc;
    int version = 1;
    ImportSettings::Pairs raw;
    if (!ParseBlock(in, AssetTypeName(kind), version, raw)) return sc;
    sc.found = true;
    sc.settings = ImportSettings::FromPairs(kind, version, raw);
    const int current = ImportSettings::CurrentVersion(kind);
    sc.pairs = version < current ? ImportSettings::Migrate(kind, version, raw)
             : version == current ? ImportSettings::AcceptOlderKeys(kind, raw) : raw;
    return sc;
}

struct Stamp {
    bool exists = false;
    long long time = 0;
    std::uintmax_t size = 0;
    bool operator==(const Stamp& o) const { return exists == o.exists && time == o.time && size == o.size; }
};

Stamp StampOf(const std::string& metaPath) {
    Stamp st;
    std::error_code ec;
    const fs::path p(metaPath);
    const auto t = fs::last_write_time(p, ec);
    if (ec) return st;
    const auto n = fs::file_size(p, ec);
    if (ec) return st;
    st.exists = true;
    st.time = static_cast<long long>(t.time_since_epoch().count());
    st.size = n;
    return st;
}

struct Entry {
    Stamp stamp;
    ImportSidecar data;
    bool settled = false;   // the file was older than kSettle when it was read
};

// A file changed within this long before it was read may change again with the same time and size: file times tick
// coarsely (Windows: a rewrite a few milliseconds later often keeps the same time), and "set compress 0" rewritten as
// "set compress 1" keeps the same size. Such an entry is read again until the file is older than this ("racy" times).
constexpr std::chrono::seconds kSettle{2};

bool Settled(const Stamp& st) {
    using clock = fs::file_time_type::clock;
    const long long nowTicks = static_cast<long long>(clock::now().time_since_epoch().count());
    const long long settle = static_cast<long long>(std::chrono::duration_cast<clock::duration>(kSettle).count());
    return st.time < nowTicks - settle;
}

debugutil::SpinLock g_lock;
std::unordered_map<std::string, Entry>& Cache() {
    static std::unordered_map<std::string, Entry> m;
    return m;
}
constexpr std::size_t kMaxEntries = 8192;   // a runaway caller can't grow it without end

} // namespace

ImportSidecar ReadImportSidecar(const std::string& assetPath, AssetType kind) {
    const std::string metaPath = assetPath + ".okaymeta";
    const Stamp now = StampOf(metaPath);
    if (!now.exists) {
        ImportSidecar none;
        none.settings = ImportSettings::Defaults(kind);
        return none;
    }
    const std::string key = std::string(AssetTypeName(kind)) + '|' + metaPath;
    {
        debugutil::SpinGuard g(g_lock);
        auto it = Cache().find(key);
        if (it != Cache().end() && it->second.stamp == now && it->second.settled) return it->second.data;
    }
    Entry e;
    e.stamp = now;
    e.settled = Settled(now);   // decided before the read: a change made during the read is seen next time
    e.data = ReadUncached(metaPath, kind);   // parsed outside the lock
    debugutil::SpinGuard g(g_lock);
    if (Cache().size() >= kMaxEntries) Cache().clear();
    Cache()[key] = e;
    return e.data;
}

void ClearImportSidecarCache() {
    debugutil::SpinGuard g(g_lock);
    Cache().clear();
}

std::string ReadImportSetting(const std::string& assetPath, const char* importer, const char* key) {
    const AssetType kind = AssetTypeFromName(importer ? importer : "");
    if (!ImportSettings::HasSettings(kind) || !key) return std::string();
    const ImportSidecar sc = ReadImportSidecar(assetPath, kind);
    if (!sc.found) return std::string();
    const std::string want = LowerAscii(key);
    std::string found;
    for (const auto& kv : sc.pairs) if (LowerAscii(kv.first) == want) found = kv.second;
    return found;
}

} // namespace okay
