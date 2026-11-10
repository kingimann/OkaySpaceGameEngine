#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay {

namespace fs = std::filesystem;

namespace {

using savefmt::Escape;
using savefmt::Unescape;

const std::size_t kMaxDocLines = 8000000u;

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i)
        if (i == line.size() || line[i] == '\t') { out.push_back(line.substr(start, i - start)); start = i + 1; }
    return out;
}

bool AllDigits(const std::string& s) {
    if (s.empty() || s.size() > 18) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

bool IsHex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ',') { out.push_back(s.substr(start, i - start)); start = i + 1; }
    return out;
}

} // namespace

// =====================================================================
// SaveBlock
// =====================================================================

bool SaveBlock::Payload(const std::string& key, char& type, std::string& payload) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return false;
    const std::string& s = it->second;
    if (s.size() < 2 || s[1] != ':') return false;
    type = s[0];
    payload = s.substr(2);
    return true;
}

void SaveBlock::SetInt(const std::string& k, int v) { m_values[k] = "i:" + std::to_string(v); }
void SaveBlock::SetInt64(const std::string& k, long long v) { m_values[k] = "l:" + std::to_string(v); }
void SaveBlock::SetFloat(const std::string& k, float v) { m_values[k] = "f:" + savefmt::FormatFloat(v); }
void SaveBlock::SetBool(const std::string& k, bool v) { m_values[k] = v ? "b:1" : "b:0"; }
void SaveBlock::SetString(const std::string& k, const std::string& v) { m_values[k] = "s:" + v; }
void SaveBlock::SetVec3(const std::string& k, const Vec3& v) {
    m_values[k] = "v:" + savefmt::FormatFloat(v.x) + "," + savefmt::FormatFloat(v.y) + "," + savefmt::FormatFloat(v.z);
}
void SaveBlock::SetQuat(const std::string& k, const Quat& q) {
    m_values[k] = "q:" + savefmt::FormatFloat(q.x) + "," + savefmt::FormatFloat(q.y) + "," +
                  savefmt::FormatFloat(q.z) + "," + savefmt::FormatFloat(q.w);
}
void SaveBlock::SetStrings(const std::string& k, const std::vector<std::string>& v) {
    SetInt(k + ".n", (int)v.size());
    for (std::size_t i = 0; i < v.size(); ++i) SetString(k + "." + std::to_string(i), v[i]);
}

long long SaveBlock::GetInt64(const std::string& k, long long def) const {
    char t; std::string p;
    if (!Payload(k, t, p)) return def;
    if (t == 'i' || t == 'l') return std::strtoll(p.c_str(), nullptr, 10);
    if (t == 'f') return (long long)savefmt::ParseFloat(p, (float)def);
    if (t == 'b') return p == "1" ? 1 : 0;
    return def;
}
int SaveBlock::GetInt(const std::string& k, int def) const {
    const long long v = GetInt64(k, def);
    if (v > 2147483647LL) return 2147483647;
    if (v < -2147483647LL - 1) return (-2147483647 - 1);
    return (int)v;
}
float SaveBlock::GetFloat(const std::string& k, float def) const {
    char t; std::string p;
    if (!Payload(k, t, p)) return def;
    if (t == 'f') return savefmt::ParseFloat(p, def);
    if (t == 'i' || t == 'l') return (float)std::strtoll(p.c_str(), nullptr, 10);
    return def;
}
bool SaveBlock::GetBool(const std::string& k, bool def) const {
    char t; std::string p;
    if (!Payload(k, t, p)) return def;
    if (t == 'b') return p == "1";
    if (t == 'i' || t == 'l') return std::strtoll(p.c_str(), nullptr, 10) != 0;
    return def;
}
std::string SaveBlock::GetString(const std::string& k, const std::string& def) const {
    char t; std::string p;
    if (!Payload(k, t, p)) return def;
    return p;
}
Vec3 SaveBlock::GetVec3(const std::string& k, const Vec3& def) const {
    char t; std::string p;
    if (!Payload(k, t, p) || t != 'v') return def;
    const auto parts = SplitComma(p);
    if (parts.size() != 3) return def;
    return Vec3{savefmt::ParseFloat(parts[0]), savefmt::ParseFloat(parts[1]), savefmt::ParseFloat(parts[2])};
}
Quat SaveBlock::GetQuat(const std::string& k, const Quat& def) const {
    char t; std::string p;
    if (!Payload(k, t, p) || t != 'q') return def;
    const auto parts = SplitComma(p);
    if (parts.size() != 4) return def;
    return Quat{savefmt::ParseFloat(parts[0]), savefmt::ParseFloat(parts[1]), savefmt::ParseFloat(parts[2]), savefmt::ParseFloat(parts[3])};
}
std::vector<std::string> SaveBlock::GetStrings(const std::string& k) const {
    std::vector<std::string> out;
    const int n = GetInt(k + ".n", 0);
    for (int i = 0; i < n && i < 1000000; ++i) out.push_back(GetString(k + "." + std::to_string(i)));
    return out;
}
bool SaveBlock::HasPrefix(const std::string& prefix) const {
    auto it = m_values.lower_bound(prefix);
    return it != m_values.end() && it->first.compare(0, prefix.size(), prefix) == 0;
}
std::vector<std::string> SaveBlock::Keys() const {
    std::vector<std::string> out;
    out.reserve(m_values.size());
    for (const auto& kv : m_values) out.push_back(kv.first);
    return out;
}

// =====================================================================
// SaveDocument
// =====================================================================

std::vector<std::string> SaveDocument::Names() const {
    std::vector<std::string> out;
    out.reserve(m_blocks.size());
    for (const auto& kv : m_blocks) out.push_back(kv.first);
    return out;
}

SaveReader SaveDocument::Reader(const std::string& name) const {
    static const SaveBlock kEmpty;
    const SaveBlock* b = Find(name);
    return SaveReader(b ? *b : kEmpty);
}

void SaveDocument::Merge(const SaveDocument& other) {
    for (const auto& kv : other.m_blocks) m_blocks[kv.first] = kv.second;
}

std::string SaveDocument::Serialize() const {
    std::string out;
    for (const auto& b : m_blocks) {
        out += "@block\t" + Escape(b.first) + "\t" + std::to_string(b.second.Count()) + "\n";
        for (const auto& kv : b.second.Raw()) out += Escape(kv.first, true) + "=" + Escape(kv.second) + "\n";
    }
    return out;
}

bool SaveDocument::Parse(const std::string& text, SaveDocument& out, std::string* error) {
    SaveDocument doc;
    std::size_t pos = 0, lines = 0;
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    auto next = [&](std::string& line) -> bool {
        if (pos >= text.size()) return false;
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        line = text.substr(pos, e - pos);
        pos = e + 1;
        return true;
    };
    std::string line;
    while (next(line)) {
        if (++lines > kMaxDocLines) return fail("too many lines");
        const auto cols = SplitTabs(line);
        if (cols.size() != 3 || cols[0] != "@block") return fail("bad block header at line " + std::to_string(lines));
        const std::string name = Unescape(cols[1]);
        if (name.empty() || !AllDigits(cols[2])) return fail("bad block header at line " + std::to_string(lines));
        if (doc.Has(name)) return fail("block \"" + name + "\" appears twice");
        const std::size_t n = (std::size_t)std::strtoull(cols[2].c_str(), nullptr, 10);
        SaveBlock& blk = doc.Block(name);
        for (std::size_t i = 0; i < n; ++i) {
            if (!next(line)) return fail("block \"" + name + "\" is cut short");
            if (++lines > kMaxDocLines) return fail("too many lines");
            const std::size_t eq = savefmt::FindSeparator(line);
            if (eq == std::string::npos) return fail("bad field line " + std::to_string(lines));
            blk.SetRaw(Unescape(line.substr(0, eq)), Unescape(line.substr(eq + 1)));
        }
    }
    out = std::move(doc);
    return true;
}

bool SaveDocument::RoundTrips(std::string* why) const {
    const std::string a = Serialize();
    SaveDocument back;
    std::string err;
    if (!Parse(a, back, &err)) { if (why) *why = "serialised text doesn't parse: " + err; return false; }
    if (!(back == *this)) { if (why) *why = "parsed blocks differ from the original"; return false; }
    if (back.Serialize() != a) { if (why) *why = "re-serialised text differs"; return false; }
    return true;
}

// =====================================================================
// SaveStore: the slot file format
// =====================================================================

namespace {

enum class FileStatus { Ok, Missing, Damaged, Newer };

struct Parsed {
    SaveSlotInfo info;
    std::size_t headerLen = 0;
    std::size_t payloadBytes = 0;
    std::string payloadSum;
};

std::string BuildContainer(const SaveSlotInfo& info, const std::string& payload) {
    std::string h = "okayslot " + std::to_string(SaveStore::kContainerVersion) + "\n";
    h += "dataVersion\t" + std::to_string(info.version) + "\n";
    h += "gameVersion\t" + Escape(info.gameVersion) + "\n";
    h += "savedAtMs\t" + std::to_string(info.savedAtMs) + "\n";
    h += "playTime\t" + savefmt::FormatFloat(info.playTime) + "\n";
    h += "scene\t" + Escape(info.scene) + "\n";
    h += "label\t" + Escape(info.label) + "\n";
    h += "kind\t" + Escape(info.kind) + "\n";
    h += "thumb\t" + Escape(info.thumbnail) + "\n";
    h += "payloadBytes\t" + std::to_string(payload.size()) + "\n";
    h += "payloadSum\t" + savefmt::Hex16(savefmt::Fnv1a(payload.data(), payload.size())) + "\n";
    for (const auto& kv : info.fields) h += "field\t" + Escape(kv.first) + "\t" + Escape(kv.second) + "\n";
    h += "headerSum\t" + savefmt::Hex16(savefmt::Fnv1a(h.data(), h.size())) + "\n";
    return h + payload;
}

// `bytes` holds at least the whole header; `totalSize` is the file's size.
FileStatus ParseContainer(const std::string& bytes, std::size_t totalSize, Parsed& out, std::string* why) {
    auto bad = [&](const std::string& m) { if (why) *why = m; return FileStatus::Damaged; };
    const std::size_t nl = bytes.find('\n');
    if (nl == std::string::npos || bytes.compare(0, 9, "okayslot ") != 0) {
        if (why) *why = "isn't a save slot";
        return FileStatus::Damaged;
    }
    const int cv = std::atoi(bytes.c_str() + 9);
    if (cv < 1) return bad("isn't a save slot");
    if (cv > SaveStore::kContainerVersion) {
        if (why) *why = "uses a newer save file format (" + std::to_string(cv) + ")";
        return FileStatus::Newer;
    }
    Parsed p;
    bool found = false;
    bool haveVersion = false;
    std::size_t pos = nl + 1;
    while (pos < bytes.size()) {
        const std::size_t e = bytes.find('\n', pos);
        if (e == std::string::npos) break;
        const std::string line = bytes.substr(pos, e - pos);
        if (line.compare(0, 10, "headerSum\t") == 0) {
            const std::string sum = line.substr(10);
            if (!IsHex16(sum) || savefmt::Hex16(savefmt::Fnv1a(bytes.data(), pos)) != sum)
                return bad("header doesn't match its checksum (damaged or edited)");
            p.headerLen = e + 1;
            found = true;
            break;
        }
        const auto cols = SplitTabs(line);
        if (cols.size() >= 2) {
            const std::string& k = cols[0];
            if (k == "dataVersion") { p.info.version = std::atoi(cols[1].c_str()); haveVersion = true; }
            else if (k == "gameVersion") p.info.gameVersion = Unescape(cols[1]);
            else if (k == "savedAtMs") p.info.savedAtMs = std::atoll(cols[1].c_str());
            else if (k == "playTime") p.info.playTime = savefmt::ParseFloat(cols[1]);
            else if (k == "scene") p.info.scene = Unescape(cols[1]);
            else if (k == "label") p.info.label = Unescape(cols[1]);
            else if (k == "kind") p.info.kind = Unescape(cols[1]);
            else if (k == "thumb") p.info.thumbnail = Unescape(cols[1]);
            else if (k == "payloadBytes") { if (!AllDigits(cols[1])) return bad("has a bad payload size"); p.payloadBytes = (std::size_t)std::strtoull(cols[1].c_str(), nullptr, 10); }
            else if (k == "payloadSum") p.payloadSum = cols[1];
            else if (k == "field" && cols.size() >= 3) p.info.fields[Unescape(cols[1])] = Unescape(cols[2]);
        }
        pos = e + 1;
    }
    if (!found) return bad(bytes.size() >= SaveStore::kMaxHeaderBytes ? "header is too large" : "is cut short (no complete header)");
    if (!haveVersion || p.info.version < 1) return bad("has no data version");
    if (!IsHex16(p.payloadSum)) return bad("has a bad payload checksum field");
    if (totalSize < p.headerLen + p.payloadBytes) return bad("is cut short (payload is smaller than its header says)");
    if (totalSize > p.headerLen + p.payloadBytes) return bad("has extra data after its payload");
    p.info.savedAt = p.info.savedAtMs / 1000;
    p.info.exists = true;
    out = std::move(p);
    return FileStatus::Ok;
}

bool PayloadMatches(const std::string& bytes, const Parsed& p) {
    return savefmt::Hex16(savefmt::Fnv1a(bytes.data() + p.headerLen, bytes.size() - p.headerLen)) == p.payloadSum;
}

// The slot file `path`: only its header (prefix) or the whole file with its checksum checked.
FileStatus LoadContainerFile(const std::string& path, bool whole, std::size_t maxBytes, Parsed& out,
                             std::string* payload, std::string* why) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) { if (why) *why = "is missing"; return FileStatus::Missing; }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) { if (why) *why = "can't be read"; return FileStatus::Damaged; }
    if (whole || payload) {
        std::string bytes, rerr;
        if (!savefmt::ReadFileCapped(path, maxBytes, bytes, &rerr)) { if (why) *why = "can't be read: " + rerr; return FileStatus::Damaged; }
        const FileStatus st = ParseContainer(bytes, bytes.size(), out, why);
        if (st != FileStatus::Ok) return st;
        if (!PayloadMatches(bytes, out)) { if (why) *why = "doesn't match its checksum (damaged or edited)"; return FileStatus::Damaged; }
        if (payload) *payload = bytes.substr(out.headerLen);
        return FileStatus::Ok;
    }
    std::string prefix;
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) { if (why) *why = "can't be opened"; return FileStatus::Damaged; }
        const std::size_t n = (std::size_t)std::min<std::uintmax_t>(size, SaveStore::kMaxHeaderBytes);
        prefix.resize(n);
        if (n > 0) f.read(&prefix[0], (std::streamsize)n);
        prefix.resize((std::size_t)f.gcount());
    }
    return ParseContainer(prefix, (std::size_t)size, out, why);
}

bool PlainFileName(const std::string& s) {
    if (s.empty() || s.size() > 128 || s == "." || s == "..") return false;
    return s.find_first_of("/\\:") == std::string::npos && s.find('\0') == std::string::npos;
}

// The thumbnail file that belongs to `slot`: a bare file name beside the slot files,
// named <slot>.<ext>. Anything else is the game's own path and is never touched.
bool ManagedThumbnail(const std::string& slot, const std::string& thumb, std::string& ext) {
    if (!PlainFileName(thumb)) return false;
    const fs::path p(thumb);
    if (p.stem().string() != slot || !p.has_extension()) return false;
    ext = p.extension().string();
    return true;
}

} // namespace

// =====================================================================
// SaveStore
// =====================================================================

SaveStore::SaveStore(std::string directory) : m_dir(std::move(directory)) {}

SaveStore& SaveStore::Default() {
    static SaveStore s;
    return s;
}

std::string SaveStore::Directory() const {
    return m_dir.empty() ? okay::Save::Resolve("saves") : m_dir;
}

std::string SaveStore::SlotPath(const std::string& slot) const {
    std::string d = Directory();
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d + slot + kExtension;
}

bool SaveStore::ValidSlot(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) return false;
    return !savefmt::IsReservedDeviceName(slot);   // "con", "nul", "com1"... are devices on Windows
}

long long SaveStore::NowMs() const {
    if (m_clock) return m_clock();
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void SaveStore::RegisterMigration(int fromVersion, Migration fn) {
    if (fn) m_migrations[fromVersion] = std::move(fn);
    else m_migrations.erase(fromVersion);
}

std::vector<int> SaveStore::MigrationPath(int fromVersion) const {
    std::vector<int> out;
    for (int v = fromVersion; v < m_dataVersion; ++v) out.push_back(v);
    return out;
}

bool SaveStore::Migrate(SaveDocument& doc, int from, SaveLoadReport* report, std::string* error) const {
    if (report) { report->fromVersion = from; report->toVersion = m_dataVersion; }
    for (int v = from; v < m_dataVersion; ++v) {
        auto it = m_migrations.find(v);
        if (it == m_migrations.end() || !it->second) continue;
        std::string merr;
        if (!it->second(doc, &merr)) {
            if (error) *error = "couldn't be upgraded from data version " + std::to_string(v) + " to " + std::to_string(v + 1) +
                                (merr.empty() ? "" : ": " + merr);
            return false;
        }
        if (report) { report->migrated = true; report->steps.push_back(v); }
    }
    return true;
}

bool SaveStore::Install(const std::string& slot, const std::string& bytes, std::string* error) {
    const std::string path = SlotPath(slot);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (fs::is_regular_file(path, ec)) {
        std::string old, why;
        if (savefmt::ReadFileCapped(path, m_maxBytes, old, &why)) {
            Parsed p;
            const FileStatus st = ParseContainer(old, old.size(), p, &why);
            const bool newer = st == FileStatus::Newer || (st == FileStatus::Ok && p.info.version > m_dataVersion);
            if (newer) {
                // A save from a newer build is about to be replaced: keep it, so going back
                // to the newer build still finds it.
                std::string kept;
                const std::string backup = path + ".newer.bak";
                if (!(savefmt::ReadFileCapped(backup, m_maxBytes, kept, nullptr) && kept == old))
                    savefmt::AtomicWrite(backup, old);
            } else if (st == FileStatus::Ok && PayloadMatches(old, p)) {
                savefmt::AtomicWrite(path + ".bak", old);   // only a good file becomes the backup
            }
        }
    }
    savefmt::WriteOptions wopt;
    wopt.sync = true;
    std::string werr;
    if (!savefmt::AtomicWriteChecked(path, bytes, &werr, wopt)) {
        if (error) *error = "Couldn't write " + path + ": " + werr + " (the slot was not changed)";
        return false;
    }
    return true;
}

bool SaveStore::Write(const std::string& slot, const SaveDocument& doc, SaveSlotInfo meta, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name (letters, digits, - and _)"; return false; }
    meta.slot = slot;
    meta.version = m_dataVersion;
    if (meta.gameVersion.empty()) meta.gameVersion = m_gameVersion;
    meta.savedAtMs = NowMs();
    meta.savedAt = meta.savedAtMs / 1000;
    const std::string payload = doc.Serialize();
    const std::string bytes = BuildContainer(meta, payload);
    if (bytes.size() > m_maxBytes) { if (error) *error = "Slot \"" + slot + "\" would be too large (" + std::to_string(bytes.size()) + " bytes)"; return false; }
    if (!Install(slot, bytes, error)) return false;
    if (m_verifyAfterWrite) {
        SaveDocument back;
        std::string rerr;
        if (!Read(slot, back, nullptr, &rerr)) { if (error) *error = "Verification failed after writing \"" + slot + "\": " + rerr; return false; }
        if (!(back == doc)) { if (error) *error = "Verification failed: slot \"" + slot + "\" reads back different from what was saved"; return false; }
    }
    return true;
}

namespace {

// One slot file read into a document, with the data-version check.
FileStatus ReadSlotFile(const std::string& path, const std::string& slot, std::size_t maxBytes, int dataVersion,
                        SaveDocument& doc, SaveSlotInfo& info, std::string& err) {
    Parsed p;
    std::string payload, why;
    FileStatus st = LoadContainerFile(path, true, maxBytes, p, &payload, &why);
    if (st == FileStatus::Missing) { err = "No save in slot \"" + slot + "\""; return st; }
    if (st == FileStatus::Newer) {
        err = "Slot \"" + slot + "\" " + why + "; this build can't read it. The file was left untouched.";
        return st;
    }
    if (st != FileStatus::Ok) { err = "Slot \"" + slot + "\" " + why; return st; }
    if (p.info.version > dataVersion) {
        err = "Slot \"" + slot + "\" was saved by a newer version of the game (save data version " + std::to_string(p.info.version) +
              ", this build reads up to " + std::to_string(dataVersion) + "). It was left untouched; update the game to load it.";
        info = p.info; info.slot = slot; info.incompatible = true;
        return FileStatus::Newer;
    }
    SaveDocument d;
    std::string perr;
    if (!SaveDocument::Parse(payload, d, &perr)) { err = "Slot \"" + slot + "\" is damaged: " + perr; return FileStatus::Damaged; }
    doc = std::move(d);
    info = p.info;
    info.slot = slot;
    return FileStatus::Ok;
}

} // namespace

bool SaveStore::Read(const std::string& slot, SaveDocument& out, SaveSlotInfo* info, std::string* error, SaveLoadReport* report) const {
    if (report) *report = SaveLoadReport();
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    const std::string path = SlotPath(slot);
    SaveDocument doc;
    SaveSlotInfo si;
    std::string err;
    FileStatus st = ReadSlotFile(path, slot, m_maxBytes, m_dataVersion, doc, si, err);
    bool usedBackup = false;
    std::string note;
    if (st == FileStatus::Damaged) {
        SaveDocument bdoc;
        SaveSlotInfo bsi;
        std::string berr;
        if (ReadSlotFile(path + ".bak", slot, m_maxBytes, m_dataVersion, bdoc, bsi, berr) == FileStatus::Ok) {
            doc = std::move(bdoc); si = bsi; usedBackup = true;
            note = err + "; loaded the backup of the previous save instead";
            Log::Warning("Save slot ", slot, " was damaged; loaded its backup instead");
            st = FileStatus::Ok;
        }
    }
    if (st != FileStatus::Ok) { if (error) *error = err; if (info) *info = si; return false; }
    if (report) { report->usedBackup = usedBackup; report->fromVersion = report->toVersion = si.version; }
    std::string merr;
    if (!Migrate(doc, si.version, report, &merr)) {
        if (error) *error = "Slot \"" + slot + "\" " + merr;
        return false;
    }
    si.fromBackup = usedBackup;
    if (info) *info = si;
    if (error && usedBackup) *error = note;
    out = std::move(doc);
    return true;
}

SaveSlotInfo SaveStore::Info(const std::string& slot, bool deep) const {
    SaveSlotInfo i;
    i.slot = slot;
    if (!ValidSlot(slot)) return i;
    const std::string path = SlotPath(slot);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return i;
    const std::uintmax_t size = fs::file_size(path, ec);
    Parsed p;
    std::string why;
    FileStatus st = LoadContainerFile(path, false, m_maxBytes, p, nullptr, &why);
    bool headerKnown = st == FileStatus::Ok;   // the header is fine even if the rest turns out not to be
    if (st == FileStatus::Ok && deep) {
        Parsed whole;
        if (LoadContainerFile(path, true, m_maxBytes, whole, nullptr, &why) != FileStatus::Ok) st = FileStatus::Damaged;
    }
    if (st == FileStatus::Ok) {
        i = p.info;
        i.slot = slot;
        i.sizeBytes = ec ? 0 : (long long)size;
        i.incompatible = p.info.version > m_dataVersion;
        if (i.incompatible) i.error = "saved by a newer version of the game (save data version " + std::to_string(p.info.version) + ")";
        return i;
    }
    if (st == FileStatus::Damaged) {
        Parsed bp;
        std::string bwhy;
        if (LoadContainerFile(path + ".bak", deep, m_maxBytes, bp, nullptr, &bwhy) == FileStatus::Ok) {
            SaveSlotInfo b = bp.info;
            b.slot = slot;
            b.sizeBytes = ec ? 0 : (long long)size;
            b.fromBackup = true;
            b.incompatible = bp.info.version > m_dataVersion;
            b.error = "the slot file " + why + "; showing its backup";
            return b;
        }
    }
    if (headerKnown) { i = p.info; i.slot = slot; }   // a damaged slot still shows what its header says
    i.exists = true;
    i.sizeBytes = ec ? 0 : (long long)size;
    if (st == FileStatus::Newer) i.incompatible = true;
    else i.corrupt = true;
    i.error = why;
    return i;
}

bool SaveStore::Exists(const std::string& slot) const {
    std::error_code ec;
    return ValidSlot(slot) && fs::is_regular_file(SlotPath(slot), ec);
}

std::vector<SaveSlotInfo> SaveStore::ListSlots(SlotSort sort, bool deep) const {
    std::vector<SaveSlotInfo> out;
    std::error_code ec;
    const fs::path dir = fs::path(SlotPath("x")).parent_path();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != kExtension) continue;
        const std::string slot = it->path().stem().string();
        if (!ValidSlot(slot)) continue;
        SaveSlotInfo i = Info(slot, deep);
        if (i.exists) out.push_back(std::move(i));
    }
    auto known = [](const SaveSlotInfo& s) { return s.savedAtMs > 0 ? 0 : 1; };   // unreadable ones go last
    std::sort(out.begin(), out.end(), [&](const SaveSlotInfo& a, const SaveSlotInfo& b) {
        switch (sort) {
            case SlotSort::Oldest:
                if (known(a) != known(b)) return known(a) < known(b);
                if (a.savedAtMs != b.savedAtMs) return a.savedAtMs < b.savedAtMs;
                break;
            case SlotSort::Name: break;
            case SlotSort::PlayTime:
                if (known(a) != known(b)) return known(a) < known(b);
                if (a.playTime != b.playTime) return a.playTime > b.playTime;
                break;
            case SlotSort::Newest:
            default:
                if (known(a) != known(b)) return known(a) < known(b);
                if (a.savedAtMs != b.savedAtMs) return a.savedAtMs > b.savedAtMs;
                break;
        }
        return a.slot < b.slot;
    });
    return out;
}

std::string SaveStore::NewestValidSlot() const {
    for (const SaveSlotInfo& i : ListSlots(SlotSort::Newest, false)) {
        if (i.corrupt || i.incompatible) continue;
        const SaveSlotInfo d = Info(i.slot, true);   // reads the whole file: checksum
        if (d.exists && !d.corrupt && !d.incompatible) return i.slot;
    }
    return std::string();
}

bool SaveStore::Delete(const std::string& slot) {
    if (!ValidSlot(slot)) return false;
    std::error_code ec;
    const std::string path = SlotPath(slot);
    std::string ext;
    const SaveSlotInfo info = Info(slot);
    if (ManagedThumbnail(slot, info.thumbnail, ext)) fs::remove(fs::path(path).parent_path() / info.thumbnail, ec);
    fs::remove(path + ".bak", ec);
    fs::remove(path + ".tmp", ec);
    fs::remove(path + ".newer.bak", ec);
    return fs::remove(path, ec);
}

namespace {

// The slot file's header rewritten by `fn`, the payload bytes left exactly as they were.
bool RewriteHeader(const std::string& bytes, const std::function<void(SaveSlotInfo&)>& fn, std::string& out, std::string* error) {
    Parsed p;
    std::string why;
    if (ParseContainer(bytes, bytes.size(), p, &why) != FileStatus::Ok || !PayloadMatches(bytes, p)) {
        if (error) *error = "the slot " + (why.empty() ? std::string("is damaged") : why);
        return false;
    }
    SaveSlotInfo info = p.info;
    fn(info);
    out = BuildContainer(info, bytes.substr(p.headerLen));
    return true;
}

} // namespace

bool SaveStore::UpdateInfo(const std::string& slot, const std::function<void(SaveSlotInfo&)>& fn, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    const std::string path = SlotPath(slot);
    std::string bytes, rerr;
    if (!savefmt::ReadFileCapped(path, m_maxBytes, bytes, &rerr)) { if (error) *error = "No save in slot \"" + slot + "\""; return false; }
    std::string out;
    std::string why;
    const int dataVersion = m_dataVersion;
    bool newer = false;
    auto edit = [&](SaveSlotInfo& info) {
        if (info.version > dataVersion) { newer = true; return; }
        SaveSlotInfo copy = info;
        fn(copy);
        info.label = copy.label;
        info.thumbnail = copy.thumbnail;
        info.kind = copy.kind;
        info.fields = copy.fields;
        info.gameVersion = copy.gameVersion;
    };
    if (!RewriteHeader(bytes, edit, out, &why)) { if (error) *error = "Slot \"" + slot + "\": " + why; return false; }
    if (newer) { if (error) *error = "Slot \"" + slot + "\" was saved by a newer version of the game and can't be edited here"; return false; }
    std::string werr;
    if (!savefmt::AtomicWriteChecked(path, out, &werr)) { if (error) *error = "Couldn't write " + path + ": " + werr; return false; }
    return true;
}

bool SaveStore::SetLabel(const std::string& slot, const std::string& label, std::string* error) {
    return UpdateInfo(slot, [&](SaveSlotInfo& i) { i.label = label; }, error);
}

bool SaveStore::ExportRaw(const std::string& slot, std::string& bytes, SaveSlotInfo* info, std::string* error) const {
    bytes.clear();
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    std::string raw, rerr;
    if (!savefmt::ReadFileCapped(SlotPath(slot), m_maxBytes, raw, &rerr)) { if (error) *error = "No save in slot \"" + slot + "\""; return false; }
    Parsed p;
    std::string why;
    const FileStatus st = ParseContainer(raw, raw.size(), p, &why);
    if (st == FileStatus::Damaged || (st == FileStatus::Ok && !PayloadMatches(raw, p))) {
        if (error) *error = "Slot \"" + slot + "\" " + (why.empty() ? "doesn't match its checksum (damaged or edited)" : why);
        return false;
    }
    if (st == FileStatus::Newer) { if (error) *error = "Slot \"" + slot + "\" " + why; return false; }
    if (info) { *info = p.info; info->slot = slot; info->incompatible = p.info.version > m_dataVersion; }
    bytes = std::move(raw);
    return true;
}

bool SaveStore::ParseHeader(const std::string& bytes, SaveSlotInfo& info, std::string* error) {
    Parsed p;
    std::string why;
    if (ParseContainer(bytes, bytes.size(), p, &why) != FileStatus::Ok) { if (error) *error = why; return false; }
    if (!PayloadMatches(bytes, p)) { if (error) *error = "doesn't match its checksum (damaged or edited)"; return false; }
    info = p.info;
    return true;
}

std::string SaveStore::Checksum(const std::string& bytes) {
    return savefmt::Hex16(savefmt::Fnv1a(bytes.data(), bytes.size()));
}

bool SaveStore::ImportRaw(const std::string& slot, const std::string& bytes, bool overwrite, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    SaveSlotInfo info;
    std::string why;
    if (!ParseHeader(bytes, info, &why)) { if (error) *error = "The data for slot \"" + slot + "\" " + why; return false; }
    SaveDocument probe;
    Parsed p;
    ParseContainer(bytes, bytes.size(), p, nullptr);
    if (!SaveDocument::Parse(bytes.substr(p.headerLen), probe, &why)) { if (error) *error = "The data for slot \"" + slot + "\" is damaged: " + why; return false; }
    if (!overwrite && Exists(slot)) { if (error) *error = "Slot \"" + slot + "\" already exists"; return false; }
    return Install(slot, bytes, error);
}

bool SaveStore::Copy(const std::string& from, const std::string& to, bool overwrite, std::string* error) {
    if (!ValidSlot(from) || !ValidSlot(to)) { if (error) *error = "not a valid slot name"; return false; }
    if (from == to) { if (error) *error = "source and destination are the same slot"; return false; }
    std::string bytes;
    SaveSlotInfo info;
    if (!ExportRaw(from, bytes, &info, error)) return false;
    if (!overwrite && Exists(to)) { if (error) *error = "Slot \"" + to + "\" already exists"; return false; }
    std::string ext;
    const fs::path dir = fs::path(SlotPath(from)).parent_path();
    if (ManagedThumbnail(from, info.thumbnail, ext)) {
        std::error_code ec;
        const std::string newName = to + ext;
        fs::copy_file(dir / info.thumbnail, dir / newName, fs::copy_options::overwrite_existing, ec);
        std::string out;
        if (RewriteHeader(bytes, [&](SaveSlotInfo& i) { i.thumbnail = ec ? std::string() : newName; }, out, nullptr)) bytes = std::move(out);
    }
    return Install(to, bytes, error);
}

bool SaveStore::Rename(const std::string& from, const std::string& to, bool overwrite, std::string* error) {
    if (!ValidSlot(from) || !ValidSlot(to)) { if (error) *error = "not a valid slot name"; return false; }
    if (from == to) return true;
    if (!Exists(from)) { if (error) *error = "No save in slot \"" + from + "\""; return false; }
    if (Exists(to)) {
        if (!overwrite) { if (error) *error = "Slot \"" + to + "\" already exists"; return false; }
        Delete(to);
    }
    const SaveSlotInfo info = Info(from);
    const std::string fp = SlotPath(from), tp = SlotPath(to);
    std::error_code ec;
    fs::rename(fp, tp, ec);
    if (ec) { if (error) *error = "Couldn't rename " + fp + ": " + ec.message(); return false; }
    for (const char* suffix : {".bak", ".newer.bak"}) {
        std::error_code e2;
        if (fs::is_regular_file(fp + suffix, e2)) { fs::remove(tp + suffix, e2); fs::rename(fp + suffix, tp + suffix, e2); }
    }
    std::string ext;
    if (ManagedThumbnail(from, info.thumbnail, ext)) {
        const fs::path dir = fs::path(fp).parent_path();
        std::error_code e3;
        fs::rename(dir / info.thumbnail, dir / (to + ext), e3);
        const std::string newName = to + ext;
        UpdateInfo(to, [&](SaveSlotInfo& i) { i.thumbnail = e3 ? std::string() : newName; }, nullptr);
    }
    return true;
}

bool SaveStore::Verify(const std::string& slot, std::string* report) const {
    auto say = [&](const std::string& m) { if (report) *report = m; };
    if (!ValidSlot(slot)) { say("not a valid slot name"); return false; }
    Parsed p;
    std::string payload, why;
    const FileStatus st = LoadContainerFile(SlotPath(slot), true, m_maxBytes, p, &payload, &why);
    if (st == FileStatus::Missing) { say("slot \"" + slot + "\" does not exist"); return false; }
    if (st != FileStatus::Ok) { say("slot \"" + slot + "\" " + why); return false; }
    if (p.info.version > m_dataVersion) {
        say("slot \"" + slot + "\" was saved by a newer version (save data version " + std::to_string(p.info.version) + ", this build reads up to " + std::to_string(m_dataVersion) + ")");
        return false;
    }
    SaveDocument doc;
    if (!SaveDocument::Parse(payload, doc, &why)) { say("payload doesn't parse: " + why); return false; }
    if (!doc.RoundTrips(&why)) { say("payload doesn't round-trip: " + why); return false; }
    SaveDocument up = doc;
    SaveLoadReport lr;
    if (!Migrate(up, p.info.version, &lr, &why)) { say("migration failed: " + why); return false; }
    if (!up.RoundTrips(&why)) { say("payload doesn't round-trip after migration: " + why); return false; }
    say("ok: data version " + std::to_string(p.info.version) + (lr.migrated ? " (migrated by " + std::to_string(lr.steps.size()) + " step(s) to " + std::to_string(m_dataVersion) + ")" : "") +
        ", " + std::to_string(doc.Count()) + " block(s), " + std::to_string(payload.size()) + " payload bytes");
    return true;
}

} // namespace okay
