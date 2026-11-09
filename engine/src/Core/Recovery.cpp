#include "okay/Core/Recovery.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <set>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <signal.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace okay {

namespace fs = std::filesystem;

namespace {

constexpr const char* kMagic = "OKAYREC";
constexpr int kVersion = 1;
constexpr std::size_t kMaxHeader = 64 * 1024;
constexpr const char* kAutosaveExt = ".autosave";
constexpr const char* kLockExt = ".lock";
constexpr const char* kTmpExt = ".tmp";

long CurrentPid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

bool OsProcessAlive(long pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // exists, we may just not look at it
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
#else
    if (kill((pid_t)pid, 0) == 0) return true;
    return errno == EPERM;
#endif
}

std::string Sanitize(const std::string& s) {
    std::string o;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
        o += ok ? c : '_';
    }
    return o;
}

std::string HexNum(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%llx", (unsigned long long)v);
    return b;
}

bool EndsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool ParseU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (std::uint64_t)(c - '0');
    }
    out = v;
    return true;
}

bool ParseI64(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    const bool neg = s[0] == '-';
    std::uint64_t u = 0;
    if (!ParseU64(neg ? s.substr(1) : s, u)) return false;
    out = neg ? -(std::int64_t)u : (std::int64_t)u;
    return true;
}

bool ParseHex64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 16) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (std::uint64_t)d;
    }
    out = v;
    return true;
}

std::string MakeSessionId(std::int64_t now) {
    static unsigned counter = 0;
    unsigned r = 0;
    try { std::random_device rd; r = rd(); } catch (...) { r = 0x9e3779b9u; }
    r ^= (++counter) * 2654435761u;
    return HexNum((std::uint64_t)now) + "p" + HexNum((std::uint64_t)CurrentPid()) + "r" + HexNum(r);
}

/// Value of "key=" in a small text file made of key=value lines.
bool FindKey(const std::string& text, const std::string& key, std::string& out) {
    const std::string needle = key + "=";
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        if (text.compare(pos, needle.size(), needle) == 0) {
            out = text.substr(pos + needle.size(), end - pos - needle.size());
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return true;
        }
        pos = end + 1;
    }
    return false;
}

} // namespace

RecoveryManager::RecoveryManager() : RecoveryManager(Config{}) {}

RecoveryManager::RecoveryManager(Config cfg) : m_cfg(std::move(cfg)) {
    if (m_cfg.slots < 1) m_cfg.slots = 1;
    if (m_cfg.baseName.empty()) m_cfg.baseName = "autosave";
    m_cfg.baseName = Sanitize(m_cfg.baseName);
}

std::int64_t RecoveryManager::Now() const {
    if (m_clock) return m_clock();
    return (std::int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool RecoveryManager::ProcessAlive(long pid) const {
    return m_probe ? m_probe(pid) : OsProcessAlive(pid);
}

std::string RecoveryManager::PathFor(const std::string& session, int slot) const {
    return (fs::path(m_cfg.directory) / (m_cfg.baseName + "-" + session + "-" + std::to_string(slot) + kAutosaveExt)).string();
}

std::string RecoveryManager::LockPath(const std::string& session) const {
    return (fs::path(m_cfg.directory) / (m_cfg.baseName + "-" + session + kLockExt)).string();
}

std::string RecoveryManager::NextAutosavePath() const {
    return PathFor(m_session, (int)(m_seq % (std::uint64_t)m_cfg.slots));
}

std::vector<RecoveryManager::FileEntry> RecoveryManager::ListFiles(const char* suffix) const {
    std::vector<FileEntry> v;
    std::error_code ec;
    const std::string prefix = m_cfg.baseName + "-";
    const std::string suf = suffix;
    fs::directory_iterator it(m_cfg.directory, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string name = it->path().filename().string();
        if (name.compare(0, prefix.size(), prefix) != 0 || !EndsWith(name, suf)) continue;
        v.push_back({it->path().string(), name});
    }
    return v;
}

namespace {

/// "<base>-<session>-<slot>.autosave" -> session; "<base>-<session>.lock" -> session
/// (with the optional ".tmp" a write in progress adds).
std::string SessionOfName(const std::string& base, std::string name, bool hasSlot) {
    if (EndsWith(name, kTmpExt)) name.resize(name.size() - std::strlen(kTmpExt));
    const std::string ext = hasSlot ? kAutosaveExt : kLockExt;
    if (!EndsWith(name, ext)) return "";
    name.resize(name.size() - ext.size());
    const std::string prefix = base + "-";
    if (name.compare(0, prefix.size(), prefix) != 0) return "";
    name.erase(0, prefix.size());
    if (hasSlot) {
        const std::size_t dash = name.rfind('-');
        if (dash == std::string::npos) return "";
        name.resize(dash);
    }
    return name;
}

} // namespace

bool RecoveryManager::SessionLive(const std::string& session, bool* hasLock) const {
    if (hasLock) *hasLock = false;
    if (!m_session.empty() && session == m_session) return true;
    std::string text;
    if (!savefmt::ReadFileCapped(LockPath(session), 4096, text)) return false;
    if (hasLock) *hasLock = true;
    std::string pidText;
    std::int64_t pid = 0;
    if (!FindKey(text, "pid", pidText) || !ParseI64(pidText, pid)) return false;   // unreadable lock: dead
    return ProcessAlive((long)pid);
}

// ---- Session ---------------------------------------------------------------------

void RecoveryManager::ScanStaleLocks() {
    for (const auto& f : ListFiles(kLockExt)) {
        const std::string session = SessionOfName(m_cfg.baseName, f.name, false);
        if (session.empty() || session == m_session) continue;
        if (SessionLive(session)) continue;
        m_crashed.push_back(session);
        std::error_code ec;
        fs::remove(f.path, ec);
    }
    // Half-written files a crash left behind (`x.autosave.tmp`, `x.lock.tmp`).
    for (const char* ext : {kAutosaveExt, kLockExt}) {
        const std::string tmpSuffix = std::string(ext) + kTmpExt;
        for (const auto& f : ListFiles(tmpSuffix.c_str())) {
            const std::string session = SessionOfName(m_cfg.baseName, f.name, std::string(ext) == kAutosaveExt);
            if (!session.empty() && session != m_session && SessionLive(session)) continue;
            std::error_code ec;
            fs::remove(f.path, ec);
        }
    }
}

void RecoveryManager::TrimOldSessions() {
    if (m_cfg.maxOtherSessions <= 0) return;
    const std::vector<Candidate> all = FindRecoverable(false);   // newest first
    std::vector<std::string> order;
    for (const auto& c : all) if (std::find(order.begin(), order.end(), c.sessionId) == order.end()) order.push_back(c.sessionId);
    if ((int)order.size() <= m_cfg.maxOtherSessions) return;
    std::set<std::string> drop(order.begin() + m_cfg.maxOtherSessions, order.end());
    for (const auto& c : all) {
        if (!drop.count(c.sessionId)) continue;
        std::error_code ec;
        fs::remove(c.path, ec);
    }
}

bool RecoveryManager::StartSession() {
    if (m_active) return true;
    m_error.clear();
    m_crashed.clear();
    if (m_cfg.directory.empty()) { m_error = "no recovery directory set"; return false; }
    std::error_code ec;
    fs::create_directories(m_cfg.directory, ec);
    if (ec || !fs::is_directory(m_cfg.directory, ec)) {
        m_error = "can't create recovery directory " + m_cfg.directory;
        return false;
    }
    m_session = m_cfg.sessionId.empty() ? MakeSessionId(Now()) : Sanitize(m_cfg.sessionId);

    // Who died, and what did they leave? Do this before our own lock exists.
    ScanStaleLocks();
    if (m_cfg.removeCorruptOnStart) RemoveCorrupt();
    TrimOldSessions();

    std::string lock = "OKAYLOCK 1\n";
    lock += "session=" + m_session + "\n";
    lock += "pid=" + std::to_string(CurrentPid()) + "\n";
    lock += "start=" + std::to_string(Now()) + "\n";
    if (!savefmt::AtomicWrite(LockPath(m_session), lock)) {
        m_error = "can't write session lock in " + m_cfg.directory;
        return false;
    }
    m_active = true;
    m_seq = 0;
    m_count = 0;
    m_elapsed = 0.0f;
    m_haveLast = false;
    m_lastTime = 0;
    return true;
}

void RecoveryManager::RemoveOwnAutosaves() {
    std::error_code ec;
    for (const auto& f : ListFiles(kAutosaveExt)) {
        if (SessionOfName(m_cfg.baseName, f.name, true) == m_session) fs::remove(f.path, ec);
    }
    for (int i = 0; i < m_cfg.slots; ++i) fs::remove(PathFor(m_session, i) + kTmpExt, ec);
}

void RecoveryManager::EndSession(bool removeAutosaves) {
    if (!m_active) return;
    if (removeAutosaves) RemoveOwnAutosaves();
    std::error_code ec;
    fs::remove(LockPath(m_session), ec);
    fs::remove(LockPath(m_session) + kTmpExt, ec);
    m_active = false;
}

// ---- Autosave -------------------------------------------------------------------------

std::string RecoveryManager::Encode(const std::string& session, std::uint64_t seq, std::int64_t time,
                                    const std::string& label, const std::string& payload) {
    std::string h;
    h += std::string(kMagic) + " " + std::to_string(kVersion) + "\n";
    h += "session=" + savefmt::Escape(session) + "\n";
    h += "seq=" + std::to_string(seq) + "\n";
    h += "time=" + std::to_string(time) + "\n";
    h += "label=" + savefmt::Escape(label) + "\n";
    h += "bytes=" + std::to_string(payload.size()) + "\n";
    h += "fnv=" + savefmt::Hex16(savefmt::Fnv1a(payload.data(), payload.size())) + "\n";
    h += "\n";
    return h + payload;
}

bool RecoveryManager::Decode(const std::string& file, Candidate& meta, std::string& payload, std::string* reason) {
    auto fail = [&](const char* why) { if (reason) *reason = why; return false; };
    const std::size_t split = file.find("\n\n");
    if (split == std::string::npos || split > kMaxHeader) return fail(file.empty() ? "empty file" : "bad header");
    const std::string head = file.substr(0, split + 1);
    const std::size_t bodyStart = split + 2;

    const std::size_t eol = head.find('\n');
    const std::string first = head.substr(0, eol);
    const std::string magic = std::string(kMagic) + " ";
    if (first.compare(0, magic.size(), magic) != 0) return fail("bad header");
    std::int64_t version = 0;
    if (!ParseI64(first.substr(magic.size()), version) || version < 1) return fail("bad header");
    if (version > kVersion) return fail("newer format");

    std::string session, seqT, timeT, label, bytesT, fnvT;
    if (!FindKey(head, "session", session) || !FindKey(head, "seq", seqT) || !FindKey(head, "time", timeT) ||
        !FindKey(head, "bytes", bytesT) || !FindKey(head, "fnv", fnvT)) return fail("bad header");
    FindKey(head, "label", label);
    std::uint64_t seq = 0, bytes = 0, fnv = 0;
    std::int64_t time = 0;
    if (!ParseU64(seqT, seq) || !ParseI64(timeT, time) || !ParseU64(bytesT, bytes) || !ParseHex64(fnvT, fnv))
        return fail("bad header");

    const std::uint64_t have = file.size() - bodyStart;
    if (have < bytes) return fail("truncated");
    if (have > bytes) return fail("size mismatch");
    if (savefmt::Fnv1a(file.data() + bodyStart, (std::size_t)bytes) != fnv) return fail("checksum mismatch");

    meta.sessionId = savefmt::Unescape(session);
    meta.seq = seq;
    meta.timestamp = time;
    meta.size = bytes;
    meta.label = savefmt::Unescape(label);
    payload.assign(file, bodyStart, (std::size_t)bytes);
    return true;
}

RecoveryManager::AutosaveResult RecoveryManager::WritePayload(const std::string& payload, const std::string& label) {
    if (!m_active) { m_error = "autosave session not started"; return AutosaveResult::Failed; }
    if (payload.size() > m_cfg.maxPayloadBytes) {
        m_error = "document too large to autosave (" + std::to_string(payload.size()) + " bytes)";
        return AutosaveResult::Failed;
    }
    const std::uint64_t hash = savefmt::Fnv1a(payload.data(), payload.size());
    if (m_cfg.skipUnchanged && m_haveLast && hash == m_lastHash && payload.size() == m_lastSize)
        return AutosaveResult::Skipped;

    std::error_code ec;
    fs::create_directories(m_cfg.directory, ec);   // someone may have cleared it meanwhile
    const std::int64_t now = Now();
    const std::string path = NextAutosavePath();
    const std::string content = Encode(m_session, m_seq + 1, now, label, payload);
    if (!savefmt::AtomicWrite(path, content)) {
        m_error = "can't write " + path;
        return AutosaveResult::Failed;
    }
    ++m_seq;
    ++m_count;
    m_lastTime = now;
    m_haveLast = true;
    m_lastHash = hash;
    m_lastSize = payload.size();
    m_error.clear();
    return AutosaveResult::Written;
}

RecoveryManager::AutosaveResult RecoveryManager::AutosavePayload(const std::string& payload, const std::string& label) {
    return WritePayload(payload, label);
}

RecoveryManager::AutosaveResult RecoveryManager::Autosave() {
    if (!m_provider) return AutosaveResult::Skipped;
    std::string payload, label;
    if (!m_provider(payload, label)) return AutosaveResult::Skipped;
    return WritePayload(payload, label);
}

bool RecoveryManager::Tick(float dt) {
    if (!m_active || !m_enabled || m_cfg.intervalSeconds <= 0.0f) return false;
    if (dt > 0.0f) m_elapsed += dt;
    if (m_elapsed < m_cfg.intervalSeconds) return false;
    m_elapsed = 0.0f;
    const AutosaveResult r = Autosave();
    if (r == AutosaveResult::Failed) {
        const float wait = m_cfg.intervalSeconds - m_cfg.retrySeconds;
        m_elapsed = wait > 0.0f ? wait : 0.0f;   // try again soon, not a whole interval later
    }
    return r == AutosaveResult::Written;
}

void RecoveryManager::OnSaved() {
    if (!m_active) return;
    RemoveOwnAutosaves();
    m_haveLast = false;
    m_elapsed = 0.0f;
}

// ---- Recovery ---------------------------------------------------------------------------

std::vector<RecoveryManager::Candidate> RecoveryManager::FindRecoverable(bool includeOwnSession) {
    m_skipped.clear();
    std::vector<Candidate> out;
    std::map<std::string, bool> liveCache;
    for (const auto& f : ListFiles(kAutosaveExt)) {
        const std::string session = SessionOfName(m_cfg.baseName, f.name, true);
        const bool own = !m_session.empty() && session == m_session;
        if (own && !includeOwnSession) continue;
        if (!own) {
            auto it = liveCache.find(session);
            if (it == liveCache.end()) it = liveCache.emplace(session, SessionLive(session)).first;
            if (it->second) continue;   // a running editor's autosave isn't a crash leftover
        }
        std::string text, why;
        if (!savefmt::ReadFileCapped(f.path, m_cfg.maxPayloadBytes + kMaxHeader, text, &why)) {
            m_skipped.push_back({f.path, why});
            continue;
        }
        Candidate c;
        std::string payload;
        if (!Decode(text, c, payload, &why)) {
            m_skipped.push_back({f.path, why});
            continue;
        }
        c.path = f.path;
        c.unclean = std::find(m_crashed.begin(), m_crashed.end(), c.sessionId) != m_crashed.end();
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        if (a.seq != b.seq) return a.seq > b.seq;
        return a.path < b.path;
    });
    return out;
}

bool RecoveryManager::Restore(const Candidate& c, std::string& payload, std::string* error) const {
    payload.clear();
    std::string text, why;
    if (!savefmt::ReadFileCapped(c.path, m_cfg.maxPayloadBytes + kMaxHeader, text, &why)) {
        if (error) *error = why;
        return false;
    }
    Candidate meta;
    if (!Decode(text, meta, payload, &why)) {
        payload.clear();
        if (error) *error = c.path + " is damaged (" + why + ")";
        return false;
    }
    return true;
}

bool RecoveryManager::RestoreToFile(const Candidate& c, const std::string& destPath, std::string* error) const {
    std::string payload;
    if (!Restore(c, payload, error)) return false;
    if (!savefmt::AtomicWrite(destPath, payload)) {
        if (error) *error = "can't write " + destPath;
        return false;
    }
    return true;
}

bool RecoveryManager::Discard(const Candidate& c) {
    std::error_code ec;
    const bool removed = fs::remove(c.path, ec);
    return removed && !ec;
}

int RecoveryManager::DiscardAll() {
    int n = 0;
    std::map<std::string, bool> liveCache;
    for (const char* ext : {kAutosaveExt, ".autosave.tmp"}) {
        for (const auto& f : ListFiles(ext)) {
            const std::string session = SessionOfName(m_cfg.baseName, f.name, true);
            auto it = liveCache.find(session);
            if (it == liveCache.end()) it = liveCache.emplace(session, SessionLive(session)).first;
            if (it->second) continue;
            std::error_code ec;
            if (fs::remove(f.path, ec) && !ec) ++n;
        }
    }
    return n;
}

int RecoveryManager::RemoveCorrupt() {
    int n = 0;
    std::map<std::string, bool> liveCache;
    for (const auto& f : ListFiles(kAutosaveExt)) {
        const std::string session = SessionOfName(m_cfg.baseName, f.name, true);
        auto it = liveCache.find(session);
        if (it == liveCache.end()) it = liveCache.emplace(session, SessionLive(session)).first;
        if (it->second) continue;
        std::string text, why;
        Candidate meta;
        std::string payload;
        const bool readable = savefmt::ReadFileCapped(f.path, m_cfg.maxPayloadBytes + kMaxHeader, text, &why);
        if (readable && Decode(text, meta, payload, &why)) continue;
        if (readable && why == "newer format") continue;   // not damaged, just from a newer build
        std::error_code ec;
        if (fs::remove(f.path, ec) && !ec) ++n;
    }
    return n;
}

} // namespace okay
