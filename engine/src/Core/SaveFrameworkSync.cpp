// Save framework: sync backends, the syncer, and the menu model (no scene code here).
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace okay {

namespace fs = std::filesystem;

// =====================================================================
// Sync backends
// =====================================================================

namespace {
SaveSyncEntry MakeEntry(const std::string& slot, const std::string& bytes, long long modifiedMs) {
    SaveSyncEntry e;
    e.slot = slot;
    e.modifiedMs = modifiedMs;
    e.checksum = SaveStore::Checksum(bytes);
    e.size = (long long)bytes.size();
    return e;
}
bool Offline(bool off, std::string* error) {
    if (off && error) *error = "offline";
    return off;
}
} // namespace

bool MemorySaveSync::List(std::vector<SaveSyncEntry>& out, std::string* error) {
    out.clear();
    if (Offline(m_offline, error)) return false;
    for (const auto& kv : m_slots) out.push_back(MakeEntry(kv.first, kv.second.bytes, kv.second.modifiedMs));
    return true;
}

bool MemorySaveSync::Upload(const std::string& slot, const std::string& bytes, long long modifiedMs, std::string* error) {
    if (Offline(m_offline, error)) return false;
    if (!SaveStore::ValidSlot(slot)) { if (error) *error = "not a valid slot name"; return false; }
    m_slots[slot] = Item{bytes, modifiedMs};
    ++uploads;
    return true;
}

bool MemorySaveSync::Download(const std::string& slot, std::string& bytes, SaveSyncEntry* entry, std::string* error) {
    if (Offline(m_offline, error)) return false;
    auto it = m_slots.find(slot);
    if (it == m_slots.end()) { if (error) *error = "no remote slot \"" + slot + "\""; return false; }
    bytes = it->second.bytes;
    if (entry) *entry = MakeEntry(slot, bytes, it->second.modifiedMs);
    ++downloads;
    return true;
}

bool MemorySaveSync::Remove(const std::string& slot, std::string* error) {
    if (Offline(m_offline, error)) return false;
    return m_slots.erase(slot) > 0;
}

std::string FolderSaveSync::PathFor(const std::string& slot) const {
    std::string d = m_dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d + slot + SaveStore::kExtension;
}

bool FolderSaveSync::List(std::vector<SaveSyncEntry>& out, std::string* error) {
    out.clear();
    std::error_code ec;
    if (!fs::is_directory(m_dir, ec)) return true;   // nothing uploaded yet
    for (fs::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != SaveStore::kExtension) continue;
        const std::string slot = it->path().stem().string();
        if (!SaveStore::ValidSlot(slot)) continue;
        std::string bytes, rerr;
        if (!savefmt::ReadFileCapped(it->path().string(), SaveStore::kDefaultMaxBytes, bytes, &rerr)) continue;
        SaveSlotInfo info;
        SaveStore::ParseHeader(bytes, info, nullptr);   // a damaged file lists with time 0
        out.push_back(MakeEntry(slot, bytes, info.savedAtMs));
    }
    if (ec) { if (error) *error = "can't list " + m_dir + ": " + ec.message(); return false; }
    std::sort(out.begin(), out.end(), [](const SaveSyncEntry& a, const SaveSyncEntry& b) { return a.slot < b.slot; });
    return true;
}

bool FolderSaveSync::Upload(const std::string& slot, const std::string& bytes, long long /*modifiedMs*/, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) { if (error) *error = "not a valid slot name"; return false; }
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    if (!savefmt::AtomicWrite(PathFor(slot), bytes)) { if (error) *error = "couldn't write " + PathFor(slot); return false; }
    return true;
}

bool FolderSaveSync::Download(const std::string& slot, std::string& bytes, SaveSyncEntry* entry, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) { if (error) *error = "not a valid slot name"; return false; }
    if (!savefmt::ReadFileCapped(PathFor(slot), SaveStore::kDefaultMaxBytes, bytes, error)) return false;
    if (entry) {
        SaveSlotInfo info;
        SaveStore::ParseHeader(bytes, info, nullptr);
        *entry = MakeEntry(slot, bytes, info.savedAtMs);
    }
    return true;
}

bool FolderSaveSync::Remove(const std::string& slot, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) { if (error) *error = "not a valid slot name"; return false; }
    std::error_code ec;
    const bool removed = fs::remove(PathFor(slot), ec);
    if (ec && error) *error = ec.message();
    return removed;
}

// =====================================================================
// SaveSyncer
// =====================================================================

std::string SaveSyncer::StatePath() const {
    std::string d = m_local->Directory();
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d + "sync_state.txt";
}

std::map<std::string, std::string> SaveSyncer::LoadBase() const {
    std::map<std::string, std::string> out;
    std::ifstream f(StatePath());
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind("okaysync ", 0) != 0) return out;
    while (std::getline(f, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        out[savefmt::Unescape(line.substr(0, tab))] = line.substr(tab + 1);
    }
    return out;
}

void SaveSyncer::SaveBase(const std::map<std::string, std::string>& base) const {
    std::string text = "okaysync 1\n";
    for (const auto& kv : base) text += savefmt::Escape(kv.first) + "\t" + kv.second + "\n";
    std::error_code ec;
    fs::create_directories(fs::path(StatePath()).parent_path(), ec);
    savefmt::AtomicWrite(StatePath(), text);
}

bool SaveSyncer::Plan(std::vector<Item>& out, std::string* error) {
    out.clear();
    std::vector<SaveSyncEntry> remote;
    if (!m_remote->List(remote, error)) return false;
    std::map<std::string, SaveSyncEntry> rmap, lmap;
    for (const auto& r : remote) rmap[r.slot] = r;
    for (const SaveSlotInfo& i : m_local->ListSlots(SlotSort::Name, false)) {
        if (i.corrupt) continue;   // a damaged local slot is never pushed over a good copy
        std::string bytes;
        SaveSlotInfo info;
        if (!m_local->ExportRaw(i.slot, bytes, &info, nullptr)) continue;
        lmap[i.slot] = MakeEntry(i.slot, bytes, info.savedAtMs);
    }
    const auto base = LoadBase();
    std::set<std::string> names;
    for (const auto& kv : lmap) names.insert(kv.first);
    for (const auto& kv : rmap) names.insert(kv.first);
    for (const std::string& slot : names) {
        Item it;
        it.slot = slot;
        auto li = lmap.find(slot);
        auto ri = rmap.find(slot);
        auto bi = base.find(slot);
        if (bi != base.end()) it.base = bi->second;
        if (li != lmap.end()) it.local = li->second;
        if (ri != rmap.end()) it.remote = ri->second;
        if (li == lmap.end()) it.state = SyncState::RemoteOnly;
        else if (ri == rmap.end()) it.state = SyncState::LocalOnly;
        else if (it.local.checksum == it.remote.checksum) it.state = SyncState::InSync;
        else if (!it.base.empty() && it.local.checksum == it.base) it.state = SyncState::RemoteChanged;
        else if (!it.base.empty() && it.remote.checksum == it.base) it.state = SyncState::LocalChanged;
        else it.state = SyncState::Conflict;
        out.push_back(std::move(it));
    }
    return true;
}

SaveSyncer::Report SaveSyncer::Sync() { return Sync(Options()); }

SaveSyncer::Report SaveSyncer::Sync(const Options& options) {
    Report rep;
    std::vector<Item> plan;
    std::string err;
    if (!Plan(plan, &err)) {
        rep.ok = false;
        rep.messages.push_back("couldn't reach the sync backend: " + err);
        return rep;
    }
    auto base = LoadBase();
    auto fail = [&](const std::string& slot, const std::string& what, const std::string& why) {
        ++rep.failed;
        rep.ok = false;
        rep.messages.push_back(what + " of \"" + slot + "\" failed: " + why);
    };
    auto upload = [&](const std::string& slot) -> bool {
        std::string bytes, e;
        SaveSlotInfo info;
        if (!m_local->ExportRaw(slot, bytes, &info, &e)) { fail(slot, "upload", e); return false; }
        if (!m_remote->Upload(slot, bytes, info.savedAtMs, &e)) { fail(slot, "upload", e); return false; }
        base[slot] = SaveStore::Checksum(bytes);
        ++rep.uploaded;
        return true;
    };
    auto download = [&](const std::string& slot, const std::string& into) -> bool {
        std::string bytes, e;
        if (!m_remote->Download(slot, bytes, nullptr, &e)) { fail(slot, "download", e); return false; }
        if (!m_local->ImportRaw(into, bytes, into == slot, &e)) { fail(slot, "download", e); return false; }
        if (into == slot) base[slot] = SaveStore::Checksum(bytes);
        ++rep.downloaded;
        return true;
    };
    for (const Item& it : plan) {
        switch (it.state) {
            case SyncState::InSync: base[it.slot] = it.local.checksum; break;
            case SyncState::LocalOnly:
            case SyncState::LocalChanged: upload(it.slot); break;
            case SyncState::RemoteOnly:
            case SyncState::RemoteChanged: download(it.slot, it.slot); break;
            case SyncState::Conflict: {
                ++rep.conflicts;
                rep.conflictSlots.push_back(it.slot);
                SaveSyncConflict c;
                c.slot = it.slot; c.local = it.local; c.remote = it.remote; c.baseChecksum = it.base;
                bool decided = true;
                SyncChoice choice = SyncChoice::Skip;
                switch (options.policy) {
                    case ConflictPolicy::PreferLocal: choice = SyncChoice::UseLocal; break;
                    case ConflictPolicy::PreferRemote: choice = SyncChoice::UseRemote; break;
                    case ConflictPolicy::Newest:
                        if (it.local.modifiedMs > it.remote.modifiedMs) choice = SyncChoice::UseLocal;
                        else if (it.local.modifiedMs < it.remote.modifiedMs) choice = SyncChoice::UseRemote;
                        else decided = false;   // same time, different bytes: a person has to say
                        break;
                    case ConflictPolicy::Ask:
                        if (options.resolve) choice = options.resolve(c); else decided = false;
                        break;
                    case ConflictPolicy::Skip: decided = false; break;
                }
                if (decided && choice == SyncChoice::Skip) decided = false;
                if (!decided) {
                    ++rep.unresolved;
                    rep.messages.push_back("conflict on \"" + it.slot + "\" left alone");
                    break;
                }
                if (choice == SyncChoice::UseLocal) upload(it.slot);
                else if (choice == SyncChoice::UseRemote) download(it.slot, it.slot);   // the local copy becomes the .bak
                else {   // KeepBoth: the cloud copy comes down under a new name, then ours goes up
                    std::string alias = it.slot.substr(0, 50) + "_remote";
                    for (int n = 2; m_local->Exists(alias) && n < 100; ++n) alias = it.slot.substr(0, 48) + "_remote" + std::to_string(n);
                    if (download(it.slot, alias) && upload(it.slot)) {
                        ++rep.keptBoth;
                        rep.messages.push_back("kept both versions of \"" + it.slot + "\" (the cloud copy is now \"" + alias + "\")");
                    }
                }
                break;
            }
        }
    }
    SaveBase(base);
    return rep;
}

// =====================================================================
// SaveMenuModel
// =====================================================================

namespace {

void CivilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) ++yy;
    y = (int)yy;
}

std::string Plural(long long n, const char* unit) {
    return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago";
}

} // namespace

std::string SaveMenuModel::FormatPlayTime(float seconds) {
    long long total = seconds > 0.0f ? (long long)(seconds + 0.5f) : 0;
    const long long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    char buf[48];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%lldh %02lldm", h, m);
    else if (m > 0) std::snprintf(buf, sizeof(buf), "%lldm %02llds", m, s);
    else std::snprintf(buf, sizeof(buf), "%llds", s);
    return buf;
}

std::string SaveMenuModel::FormatDateTime(long long unixSeconds, int utcOffsetMinutes) {
    long long t = unixSeconds + (long long)utcOffsetMinutes * 60;
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; --days; }
    int y, mo, d;
    CivilFromDays(days, y, mo, d);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", y, mo, d, (int)(rem / 3600), (int)((rem % 3600) / 60));
    return buf;
}

std::string SaveMenuModel::FormatAgo(long long secondsAgo) {
    if (secondsAgo < 60) return "just now";
    if (secondsAgo < 3600) return Plural(secondsAgo / 60, "minute");
    if (secondsAgo < 86400) return Plural(secondsAgo / 3600, "hour");
    if (secondsAgo < 86400LL * 30) return Plural(secondsAgo / 86400, "day");
    if (secondsAgo < 86400LL * 365) return Plural(secondsAgo / (86400LL * 30), "month");
    return Plural(secondsAgo / (86400LL * 365), "year");
}

void SaveMenuModel::Refresh(long long nowMs) {
    const std::string keepSlot = SelectedRow() ? SelectedRow()->slot : std::string();
    const long long now = nowMs >= 0 ? nowMs : m_store->NowMs();
    const std::string newest = m_store->NewestValidSlot();
    m_continue = newest;
    const std::vector<SaveSlotInfo> infos = m_store->ListSlots(m_options.sort, false);

    auto makeRow = [&](const SaveSlotInfo& i) {
        SaveMenuRow r;
        r.slot = i.slot;
        r.label = i.label; r.scene = i.scene; r.thumbnail = i.thumbnail; r.gameVersion = i.gameVersion;
        r.kind = i.kind.empty() ? std::string(SlotKind::Manual) : i.kind;
        r.savedAt = i.savedAtMs > 0 ? i.savedAtMs / 1000 : i.savedAt;
        r.savedAtMs = i.savedAtMs;
        r.playTime = i.playTime;
        r.fields = i.fields;
        r.corrupt = i.corrupt; r.incompatible = i.incompatible; r.fromBackup = i.fromBackup;
        r.title = !i.label.empty() ? i.label : i.slot;
        if (r.savedAt > 0) {
            r.savedText = FormatDateTime(r.savedAt, m_options.utcOffsetMinutes);
            r.agoText = FormatAgo(now / 1000 - r.savedAt);
            r.playTimeText = FormatPlayTime(i.playTime);
        }
        if (i.corrupt) r.statusText = "Damaged";
        else if (i.incompatible) r.statusText = "Saved by a newer version";
        else if (i.fromBackup) r.statusText = "Restored from backup";
        r.isAuto = r.kind == SlotKind::Auto;
        r.isQuick = r.kind == SlotKind::Quick;
        r.isCheckpoint = r.kind == SlotKind::Checkpoint;
        r.canLoad = !i.corrupt && !i.incompatible;
        r.canSave = r.kind == SlotKind::Manual;
        r.canDelete = true;
        r.isNewest = !newest.empty() && i.slot == newest;
        return r;
    };

    m_rows.clear();
    std::set<std::string> used;
    for (int n = 1; n <= m_options.manualSlots; ++n) {
        const std::string name = m_options.manualPrefix + std::to_string(n);
        const SaveSlotInfo* found = nullptr;
        for (const auto& i : infos) if (i.slot == name) { found = &i; break; }
        used.insert(name);
        if (found && !(found->corrupt && !m_options.showCorrupt)) { m_rows.push_back(makeRow(*found)); continue; }
        SaveMenuRow r;
        r.slot = name;
        r.title = "Empty slot";
        r.kind = SlotKind::Manual;
        r.empty = true;
        r.canSave = true;
        m_rows.push_back(std::move(r));
    }
    for (const auto& i : infos) {
        if (used.count(i.slot)) continue;
        if (i.corrupt && !m_options.showCorrupt) continue;
        const std::string kind = i.kind.empty() ? std::string(SlotKind::Manual) : i.kind;
        if (kind == SlotKind::Auto && !m_options.includeAuto) continue;
        if (kind == SlotKind::Quick && !m_options.includeQuick) continue;
        if (kind == SlotKind::Checkpoint && !m_options.includeCheckpoints) continue;
        m_rows.push_back(makeRow(i));
    }
    m_selected = -1;
    if (!keepSlot.empty()) SelectSlot(keepSlot);
}

const SaveMenuRow* SaveMenuModel::Find(const std::string& slot) const {
    for (const auto& r : m_rows) if (r.slot == slot) return &r;
    return nullptr;
}

const SaveMenuRow* SaveMenuModel::ContinueRow() const {
    return m_continue.empty() ? nullptr : Find(m_continue);
}

const SaveMenuRow* SaveMenuModel::SelectedRow() const {
    return (m_selected >= 0 && m_selected < (int)m_rows.size()) ? &m_rows[(std::size_t)m_selected] : nullptr;
}

bool SaveMenuModel::Select(int index) {
    if (index < 0 || index >= (int)m_rows.size()) return false;
    m_selected = index;
    return true;
}

bool SaveMenuModel::SelectSlot(const std::string& slot) {
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].slot == slot) { m_selected = (int)i; return true; }
    return false;
}

void SaveMenuModel::SelectNext() {
    if (m_rows.empty()) return;
    m_selected = m_selected < 0 ? 0 : std::min((int)m_rows.size() - 1, m_selected + 1);
}

void SaveMenuModel::SelectPrevious() {
    if (m_rows.empty()) return;
    m_selected = m_selected < 0 ? 0 : std::max(0, m_selected - 1);
}

bool SaveMenuModel::DeleteSelected(std::string* error) {
    const SaveMenuRow* r = SelectedRow();
    if (!r || r->empty) { if (error) *error = "nothing selected"; return false; }
    const std::string slot = r->slot;
    const int at = m_selected;
    if (!m_store->Delete(slot)) { if (error) *error = "couldn't delete slot \"" + slot + "\""; return false; }
    Refresh();
    if (!m_rows.empty()) m_selected = std::min(at, (int)m_rows.size() - 1);
    return true;
}

} // namespace okay
