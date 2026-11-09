#include "okay/Core/Backup.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

namespace okay {

namespace fs = std::filesystem;

namespace {

constexpr const char* kBakExt = ".bak";

// Days since 1970-01-01 <-> civil date (proleptic Gregorian), after H. Hinnant.
void CivilFromDays(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t yy = (std::int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = (int)(yy + (m <= 2 ? 1 : 0));
}

std::int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (std::int64_t)doe - 719468;
}

std::int64_t FloorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

bool EndsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool AllDigits(const std::string& s, std::size_t from, std::size_t n) {
    if (from + n > s.size()) return false;
    for (std::size_t i = from; i < from + n; ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

int Num(const std::string& s, std::size_t from, std::size_t n) {
    int v = 0;
    for (std::size_t i = from; i < from + n; ++i) v = v * 10 + (s[i] - '0');
    return v;
}

std::string Hash8(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    if (ec) p = fs::path(path);
    const std::string norm = p.lexically_normal().generic_string();
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", (unsigned)(savefmt::Fnv1a(norm.data(), norm.size()) & 0xffffffffu));
    return b;
}

} // namespace

BackupManager::BackupManager() : BackupManager(Config{}) {}
BackupManager::BackupManager(Config cfg) : m_cfg(std::move(cfg)) {}

std::int64_t BackupManager::Now() const {
    if (m_clock) return m_clock();
    return (std::int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---- Time helpers ------------------------------------------------------------------

std::string BackupManager::FormatStamp(std::int64_t t) {
    if (t < 0) t = 0;
    const std::int64_t days = FloorDiv(t, 86400);
    const std::int64_t rem = t - days * 86400;
    int y; unsigned m, d;
    CivilFromDays(days, y, m, d);
    char b[32];
    std::snprintf(b, sizeof(b), "%04d%02u%02u-%02d%02d%02d", y, m, d,
                  (int)(rem / 3600), (int)((rem % 3600) / 60), (int)(rem % 60));
    return b;
}

bool BackupManager::ParseStamp(const std::string& s, std::int64_t& out) {
    if (s.size() != 15 || s[8] != '-' || !AllDigits(s, 0, 8) || !AllDigits(s, 9, 6)) return false;
    const int y = Num(s, 0, 4), mo = Num(s, 4, 2), d = Num(s, 6, 2);
    const int hh = Num(s, 9, 2), mi = Num(s, 11, 2), ss = Num(s, 13, 2);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mi > 59 || ss > 59) return false;
    out = DaysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + hh * 3600 + mi * 60 + ss;
    return true;
}

std::int64_t BackupManager::DayIndex(std::int64_t t, int dayOffsetSeconds) {
    return FloorDiv(t + dayOffsetSeconds, 86400);
}

// ---- Names ---------------------------------------------------------------------------

std::string BackupManager::KeyPrefix(const std::string& file) const {
    return fs::path(file).filename().string() + "." + Hash8(file);
}

bool BackupManager::ParseName(const std::string& name, Info& out, std::string& key) {
    if (!EndsWith(name, kBakExt)) return false;
    std::string n = name.substr(0, name.size() - 4);
    const std::size_t dot1 = n.rfind('.');
    if (dot1 == std::string::npos) return false;
    std::string tail = n.substr(dot1 + 1);           // YYYYMMDD-HHMMSS[-n]
    const std::string rest = n.substr(0, dot1);       // <file>.<hash8>
    const std::size_t dot2 = rest.rfind('.');
    if (dot2 == std::string::npos || dot2 == 0 || rest.size() - dot2 - 1 != 8) return false;
    int seq = 0;
    if (tail.size() > 15) {
        if (tail[15] != '-' || tail.size() == 16 || !AllDigits(tail, 16, tail.size() - 16) || tail.size() > 22) return false;
        seq = Num(tail, 16, tail.size() - 16);
        tail.resize(15);
    }
    std::int64_t t = 0;
    if (!ParseStamp(tail, t)) return false;
    out.source = rest.substr(0, dot2);
    out.timestamp = t;
    out.seq = seq;
    key = rest;
    return true;
}

std::vector<BackupManager::Info> BackupManager::Scan(const std::string* keyPrefix) const {
    std::vector<Info> v;
    std::error_code ec;
    fs::directory_iterator it(m_cfg.directory, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        Info info;
        std::string key;
        if (!ParseName(it->path().filename().string(), info, key)) continue;
        if (keyPrefix && key != *keyPrefix) continue;
        info.path = it->path().string();
        const auto sz = it->file_size(e2);
        info.size = e2 ? 0 : (std::uint64_t)sz;
        v.push_back(std::move(info));
    }
    std::sort(v.begin(), v.end(), [](const Info& a, const Info& b) {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        if (a.seq != b.seq) return a.seq > b.seq;
        return a.path < b.path;
    });
    return v;
}

std::vector<BackupManager::Info> BackupManager::List(const std::string& file) const {
    const std::string key = KeyPrefix(file);
    return Scan(&key);
}

std::vector<BackupManager::Info> BackupManager::ListAll() const { return Scan(nullptr); }

// ---- Backup / restore ----------------------------------------------------------------

bool BackupManager::Backup(const std::string& file, std::string* outBackup, bool* created) {
    if (outBackup) outBackup->clear();
    if (created) *created = false;
    m_error.clear();
    if (m_cfg.directory.empty()) { m_error = "no backup directory set"; return false; }
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) { m_error = "nothing to back up: " + file; return false; }

    std::string content;
    std::string why;
    if (!savefmt::ReadFileCapped(file, (std::size_t)m_cfg.maxFileBytes, content, &why)) { m_error = why; return false; }
    fs::create_directories(m_cfg.directory, ec);
    if (ec) { m_error = "can't create backup directory " + m_cfg.directory; return false; }

    const std::string key = KeyPrefix(file);
    const std::vector<Info> existing = Scan(&key);
    if (m_cfg.skipIdentical && !existing.empty() && existing.front().size == content.size()) {
        std::string prev;
        if (savefmt::ReadFileCapped(existing.front().path, (std::size_t)m_cfg.maxFileBytes, prev) && prev == content) {
            if (outBackup) *outBackup = existing.front().path;
            return true;
        }
    }

    const std::string stamp = FormatStamp(Now());
    std::string dest;
    for (int n = 0; n < 100000; ++n) {
        const std::string name = key + "." + stamp + (n ? "-" + std::to_string(n) : std::string()) + kBakExt;
        dest = (fs::path(m_cfg.directory) / name).string();
        if (!fs::exists(dest, ec)) break;
    }
    if (!savefmt::AtomicWrite(dest, content)) { m_error = "can't write backup " + dest; return false; }
    if (outBackup) *outBackup = dest;
    if (created) *created = true;
    if (m_cfg.autoPrune) Prune(file);
    return true;
}

bool BackupManager::SaveWithBackup(const std::string& file, const std::string& content) {
    std::error_code ec;
    std::string backupError;
    if (fs::is_regular_file(file, ec)) {
        if (!Backup(file)) {
            backupError = m_error;
            if (m_cfg.requireBackup) return false;
        }
    }
    m_error = backupError;   // a failed backup is reported, but the save still goes ahead
    if (!savefmt::AtomicWrite(file, content)) { m_error = "can't write " + file; return false; }
    return true;
}

bool BackupManager::Read(const Info& b, std::string& content) const {
    return savefmt::ReadFileCapped(b.path, (std::size_t)m_cfg.maxFileBytes, content);
}

bool BackupManager::Restore(const Info& b, const std::string& file) {
    m_error.clear();
    std::string content, why;
    if (!savefmt::ReadFileCapped(b.path, (std::size_t)m_cfg.maxFileBytes, content, &why)) { m_error = why; return false; }
    std::error_code ec;
    if (m_cfg.backupBeforeRestore && fs::is_regular_file(file, ec)) {
        if (!Backup(file)) return false;   // don't overwrite what we couldn't keep
    }
    if (!savefmt::AtomicWrite(file, content)) { m_error = "can't write " + file; return false; }
    return true;
}

bool BackupManager::Delete(const Info& b) {
    std::error_code ec;
    return fs::remove(b.path, ec) && !ec;
}

// ---- Pruning ----------------------------------------------------------------------------

std::vector<std::size_t> BackupManager::SelectForRemoval(const std::vector<Info>& list, const BackupPolicy& policy,
                                                         std::int64_t now, int dayOffset) {
    const std::size_t n = list.size();
    std::vector<bool> keep(n, false);
    std::set<std::int64_t> coveredDays;
    const std::size_t last = (std::size_t)(policy.keepLast < 1 ? 1 : policy.keepLast);
    for (std::size_t i = 0; i < n && i < last; ++i) {
        keep[i] = true;
        coveredDays.insert(DayIndex(list[i].timestamp, dayOffset));
    }
    if (policy.keepDailyDays > 0) {
        const std::int64_t today = DayIndex(now, dayOffset);
        for (std::size_t i = (last < n ? last : n); i < n; ++i) {   // newest first: the first seen of a day is its newest
            const std::int64_t day = DayIndex(list[i].timestamp, dayOffset);
            if (today - day >= policy.keepDailyDays) continue;
            if (coveredDays.insert(day).second) keep[i] = true;
        }
    }
    if (policy.maxTotalBytes > 0) {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < n; ++i) if (keep[i]) total += list[i].size;
        for (std::size_t i = n; i-- > 1 && total > policy.maxTotalBytes;) {   // oldest first; index 0 stays
            if (!keep[i]) continue;
            keep[i] = false;
            total -= list[i].size;
        }
    }
    std::vector<std::size_t> remove;
    for (std::size_t i = 0; i < n; ++i) if (!keep[i]) remove.push_back(i);
    return remove;
}

int BackupManager::PruneList(std::vector<Info> list) {
    int removed = 0;
    for (std::size_t i : SelectForRemoval(list, m_cfg.policy, Now(), m_cfg.dayOffsetSeconds)) {
        if (Delete(list[i])) ++removed;
    }
    return removed;
}

int BackupManager::Prune(const std::string& file) {
    return PruneList(List(file));
}

int BackupManager::PruneAll() {
    std::map<std::string, std::vector<Info>> groups;
    for (auto& info : ListAll()) {
        Info probe;
        std::string key;
        if (!ParseName(fs::path(info.path).filename().string(), probe, key)) continue;
        groups[key].push_back(std::move(info));   // ListAll is newest first, so each group is too
    }
    int removed = 0;
    for (auto& g : groups) removed += PruneList(std::move(g.second));
    return removed;
}

} // namespace okay
