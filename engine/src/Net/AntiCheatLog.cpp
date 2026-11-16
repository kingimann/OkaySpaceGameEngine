#include "okay/Net/AntiCheatLog.hpp"
#include "okay/Crypto/Sha256.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace okay {

namespace fs = std::filesystem;

std::string UtcTimeText(std::int64_t unixTime) {
    std::time_t t = (std::time_t)unixTime;
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// ---------------------------------------------------------------------------
// BanList
// ---------------------------------------------------------------------------
std::string BanList::CleanText(const std::string& s, std::size_t maxBytes) {
    std::string o;
    o.reserve(std::min(s.size(), maxBytes));
    for (unsigned char c : s) {
        if (o.size() >= maxBytes) break;
        o.push_back(c < 0x20 || c == 0x7F ? ' ' : (char)c);   // tabs and newlines would break the file's lines
    }
    return o;
}

bool BanList::ValidIdent(const std::string& id) {
    if (id.size() < 3 || id.size() > 96) return false;
    if (id[1] != ':' || (id[0] != 'u' && id[0] != 'a')) return false;
    for (std::size_t i = 2; i < id.size(); ++i) {
        const unsigned char c = (unsigned char)id[i];
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':')) return false;
    }
    return true;
}

namespace {
bool ParseI64(const std::string& s, std::int64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    std::int64_t v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
    out = v;
    return true;
}
}

bool BanList::Load(const std::string& path, std::string* error) {
    m_path = path;
    m_entries.clear();
    m_skipped = 0;
    if (path.empty()) return true;
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    if (fs::is_directory(path, ec)) { if (error) *error = path + " is a folder, not a file"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "can't read " + path; return false; }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > kMaxLineBytes || m_entries.size() >= kMaxEntries) { ++m_skipped; continue; }
        std::vector<std::string> cols;
        std::size_t start = 0;
        for (int i = 0; i < 3; ++i) {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) break;
            cols.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        cols.push_back(line.substr(start));
        BanEntry e;
        // ident <tab> when <tab> until <tab> reason   (reason may be missing)
        if (cols.size() < 3 || !ValidIdent(cols[0]) || !ParseI64(cols[1], e.when) || !ParseI64(cols[2], e.until)) { ++m_skipped; continue; }
        e.ident = cols[0];
        e.reason = cols.size() > 3 ? CleanText(cols[3], kMaxReasonBytes) : std::string();
        m_entries[e.ident] = e;
    }
    return true;
}

bool BanList::IsBanned(const std::string& ident, std::int64_t now, BanEntry* out) const {
    auto it = m_entries.find(ident);
    if (it == m_entries.end()) return false;
    if (it->second.until != 0 && now >= it->second.until) return false;   // ended
    if (out) *out = it->second;
    return true;
}

bool BanList::Add(const std::string& ident, std::int64_t now, double hours, const std::string& reason) {
    if (!ValidIdent(ident)) return false;
    for (auto it = m_entries.begin(); it != m_entries.end();)
        it = (it->second.until != 0 && now >= it->second.until) ? m_entries.erase(it) : std::next(it);
    if (m_entries.size() >= kMaxEntries && !m_entries.count(ident)) return false;
    BanEntry e;
    e.ident = ident; e.when = now;
    if (hours > 0.0 && std::isfinite(hours)) e.until = now + (std::int64_t)std::min(hours, 24.0 * 365.0 * 50.0) * 3600;
    e.reason = CleanText(reason, kMaxReasonBytes);
    auto old = m_entries.find(ident);
    if (old != m_entries.end() && old->second.until == 0 && e.until != 0) return true;   // a longer ban stays
    if (old != m_entries.end() && old->second.until != 0 && e.until != 0 && old->second.until > e.until) return true;
    m_entries[ident] = e;
    return true;
}

bool BanList::Remove(const std::string& ident) { return m_entries.erase(ident) != 0; }

bool BanList::Save(std::string* error) const {
    if (m_path.empty()) return true;
    const std::string tmp = m_path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (error) *error = "can't write " + tmp; return false; }
        f << "# OkaySpace server ban list. One ban per line: identity <tab> made (unix) <tab> ends (unix, 0 = never) <tab> reason.\n";
        f << "# Identities: u:<account id> or a:<address number>. Delete a line to lift that ban.\n";
        std::vector<const BanEntry*> sorted;
        for (const auto& [k, e] : m_entries) sorted.push_back(&e);
        std::sort(sorted.begin(), sorted.end(), [](const BanEntry* a, const BanEntry* b) { return a->ident < b->ident; });
        for (const BanEntry* e : sorted) f << e->ident << '\t' << e->when << '\t' << e->until << '\t' << e->reason << '\n';
        f.flush();
        if (!f) { if (error) *error = "can't write " + tmp; return false; }
    }
    std::error_code ec;
    fs::rename(tmp, m_path, ec);
    if (ec) {   // some systems won't rename over an existing file
        fs::remove(m_path, ec);
        ec.clear();
        fs::rename(tmp, m_path, ec);
        if (ec) { if (error) *error = "can't replace " + m_path; return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// AuditLog
// ---------------------------------------------------------------------------
void AuditLog::Open(const std::string& path, std::uint64_t maxBytes) {
    m_path = path;
    m_maxBytes = maxBytes ? maxBytes : kDefaultMaxBytes;
    m_size = 0; m_lines = 0; m_salt.clear();
    if (path.empty()) return;
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (!ec) m_size = (std::uint64_t)sz;
    const std::string saltPath = path + ".salt";
    {
        std::ifstream sf(saltPath, std::ios::binary);
        std::string s;
        if (sf && std::getline(sf, s) && s.size() >= 16 && s.size() <= 64) m_salt = s;
    }
    if (m_salt.empty()) {
        std::random_device rd;
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%08x%08x", (unsigned)rd(), (unsigned)rd());
        m_salt = buf;
        std::ofstream sf(saltPath, std::ios::binary | std::ios::trunc);
        if (sf) sf << m_salt << "\n";
    }
}

std::string AuditLog::Tag(const std::string& ident) const {
    const std::string in = "okayaudit1|" + m_salt + "|" + ident;
    std::uint8_t h[32];
    crypto::Sha256Hash(h, reinterpret_cast<const std::uint8_t*>(in.data()), in.size());
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 5; ++i) { out.push_back(hex[h[i] >> 4]); out.push_back(hex[h[i] & 15]); }
    return out;
}

void AuditLog::Write(std::int64_t unixTime, const std::string& event, const std::string& ident, const std::string& detail) {
    if (m_path.empty()) return;
    std::string line = UtcTimeText(unixTime) + " " + BanList::CleanText(event, 40);
    if (!ident.empty()) line += " who=" + Tag(ident);
    const std::string d = BanList::CleanText(detail, 160);
    if (!d.empty()) line += " " + d;
    line += "\n";
    if (m_size + line.size() > m_maxBytes) {   // rotate: one older file is kept
        std::error_code ec;
        fs::remove(m_path + ".1", ec);
        ec.clear();
        fs::rename(m_path, m_path + ".1", ec);
        m_size = 0;
    }
    std::ofstream f(m_path, std::ios::binary | std::ios::app);
    if (!f) return;
    f << line;
    m_size += line.size();
    ++m_lines;
}

} // namespace okay
