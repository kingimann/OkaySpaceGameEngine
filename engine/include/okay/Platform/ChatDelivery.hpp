#pragma once
// Chat delivery rules (Community system, feature map 17.4): the pure decisions under the
// launcher's outbox and the friends window's chat tabs. No network, no UI, no threads, so
// everything here is unit tested (tests/test_chat_invites.cpp).
//
//   * Client ids: each message gets a random id on the sender's PC once. The server stores
//     (sender, client_id) once (docs/sql/chat_invites.sql), so a resend after a lost answer
//     or on reconnect never makes a duplicate.
//   * Retry rules: what is worth trying again (no connection, 5xx, 429, 408, 401, the rate
//     limit) and how long to wait (2 s doubling up to 60 s). A privacy rule is final.
//   * The offline queue file: messages written while offline, kept across a restart and sent
//     when the connection is back (given up after a week).
//   * History: merge an older page or a resent copy into a log (one row per id, oldest first).
//   * Unread counts per conversation and in total, from the rows the chat already fetches.
//   * Links: which ones are safe to open, the real host to show, a confirm text, lookalikes.
//   * Limits: 4000 characters and 30 messages a minute (the same numbers the server enforces),
//     with a plain sentence when one is hit.
//   * Events: what arrived (new message, invite, answer), for the one place that posts
//     desktop notifications (this file never shows anything itself).
//
// Written from RFC 4122 (version 4 ids), RFC 3986 (URL parts) and our own SQL rules.
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Privacy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace okay {
namespace chat {

constexpr int kMaxBodyChars = 4000;   // docs/sql/chat_invites.sql okay_private.chat_limits
constexpr int kMaxPerMinute = 30;
constexpr long kGiveUpSeconds = 7L * 86400L;

// ---- client ids --------------------------------------------------------------------------------

/// A random version-4 id: "xxxxxxxx-xxxx-4xxx-Yxxx-xxxxxxxxxxxx" (Y is 8, 9, a or b). Only for telling
/// messages apart, not a secret.
inline std::string NewClientId() {
    static std::mt19937_64 gen([] {
        std::random_device rd;
        std::uint64_t s = (std::uint64_t)rd() << 32 ^ rd();
        s ^= (std::uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return s;
    }());
    static const char* hex = "0123456789abcdef";
    std::uint64_t a = gen(), b = gen();
    unsigned char bytes[16];
    for (int i = 0; i < 8; ++i) { bytes[i] = (unsigned char)(a >> (i * 8)); bytes[8 + i] = (unsigned char)(b >> (i * 8)); }
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hex[bytes[i] >> 4]; out += hex[bytes[i] & 15];
    }
    return out;
}
/// True for 8-4-4-4-12 hex digits (any case): the only thing that may go into a URL or JSON as a client id.
inline bool IsClientId(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; continue; }
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// ---- retry rules -------------------------------------------------------------------------------

/// Worth sending again later? A refusal by a server rule is final, except the rate limit.
inline bool IsRetryable(const okay::account::ApiResponse& r) {
    if (r.ok) return false;
    if (!r.reached) return true;
    const privacy::Rule rule = privacy::ParseRule(r.body);
    if (rule == privacy::Rule::RateLimited) return true;
    if (rule != privacy::Rule::None) return false;
    return r.status == 401 || r.status == 408 || r.status == 425 || r.status == 429 || r.status >= 500;
}
/// Seconds to wait before attempt number `attempts` + 1: 2, 4, 8, ... at most 60.
inline int RetryDelaySeconds(int attempts) {
    if (attempts < 0) attempts = 0;
    if (attempts >= 5) return 60;
    return std::min(60, 2 << attempts);
}

// ---- the offline queue file --------------------------------------------------------------------

struct QueuedMessage {
    std::string clientId;
    std::string toUid;      ///< a DM to this user, or
    long toGroup = 0;       ///< a group chat, or
    long toChannel = 0;     ///< a community server channel
    std::string body;
    long replyTo = 0;
    long created = 0;       ///< unix seconds when written
    int attempts = 0;       ///< failed sends so far
};

namespace detail {
inline std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\"; else if (c == '\t') o += "\\t"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}
inline std::string Unesc(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        const char e = s[++i];
        o += e == 't' ? '\t' : e == 'n' ? '\n' : e == 'r' ? '\r' : e;
    }
    return o;
}
inline std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> f; std::size_t a = 0;
    while (true) { const std::size_t b = line.find('\t', a); f.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
    return f;
}
inline long ToLong(const std::string& s) { char* e = nullptr; const long v = std::strtol(s.c_str(), &e, 10); return (e && *e == 0) ? v : 0; }
}  // namespace detail

/// The queue as text: a "okaychatq1" header line, then one tab-separated line per message.
inline std::string EncodeQueue(const std::vector<QueuedMessage>& q) {
    std::string out = "okaychatq1\n";
    for (const QueuedMessage& m : q) {
        if (m.body.empty()) continue;
        out += detail::Esc(m.clientId) + "\t" + detail::Esc(m.toUid) + "\t" + std::to_string(m.toGroup) + "\t" +
               std::to_string(m.toChannel) + "\t" + std::to_string(m.replyTo) + "\t" + std::to_string(m.created) + "\t" +
               std::to_string(m.attempts) + "\t" + detail::Esc(m.body) + "\n";
    }
    return out;
}
/// Reads EncodeQueue's text; damaged lines and lines with a bad client id are skipped.
inline std::vector<QueuedMessage> DecodeQueue(const std::string& text) {
    std::vector<QueuedMessage> out;
    if (text.compare(0, 11, "okaychatq1\n") != 0) return out;
    std::size_t p = 11;
    while (p < text.size()) {
        std::size_t nl = text.find('\n', p); if (nl == std::string::npos) nl = text.size();
        const std::vector<std::string> f = detail::SplitTabs(text.substr(p, nl - p));
        p = nl + 1;
        if (f.size() != 8) continue;
        QueuedMessage m;
        m.clientId = detail::Unesc(f[0]); m.toUid = detail::Unesc(f[1]);
        m.toGroup = detail::ToLong(f[2]); m.toChannel = detail::ToLong(f[3]); m.replyTo = detail::ToLong(f[4]);
        m.created = detail::ToLong(f[5]); m.attempts = (int)detail::ToLong(f[6]); m.body = detail::Unesc(f[7]);
        if (!IsClientId(m.clientId) || m.body.empty()) continue;
        if (m.toUid.empty() && m.toGroup <= 0 && m.toChannel <= 0) continue;
        out.push_back(m);
    }
    return out;
}
/// A queued message older than a week is dropped rather than posted late.
inline bool ShouldGiveUp(const QueuedMessage& m, long now) { return m.created > 0 && now - m.created > kGiveUpSeconds; }

// ---- history -----------------------------------------------------------------------------------

/// Merge `incoming` (an older page, a re-poll, a resent copy) into `log`: one row per `id`, the incoming
/// copy wins (it may be an edit), oldest first. Works with any row type that has a `long id`.
template <class Row>
inline void MergeById(std::vector<Row>& log, const std::vector<Row>& incoming) {
    std::map<long, Row> byId;
    for (const Row& r : log) byId[r.id] = r;
    for (const Row& r : incoming) byId[r.id] = r;
    log.clear();
    for (auto& kv : byId) log.push_back(kv.second);
}
/// The cursor for "load older messages": the smallest id held, 0 when empty.
template <class Row>
inline long OldestId(const std::vector<Row>& log) {
    long m = 0;
    for (const Row& r : log) if (m == 0 || r.id < m) m = r.id;
    return m;
}

// ---- unread counts -----------------------------------------------------------------------------

struct UnreadRow { long id = 0; std::string sender, recipient; };
struct UnreadSummary {
    std::map<std::string, int> perPeer;   ///< unread messages from each person (only people with some)
    int total = 0;                        ///< all of them
    int conversations = 0;                ///< how many conversations have any
};
/// Count messages sent TO me with an id above what I have seen from that person (`seen`, 0 / missing = nothing
/// seen). Blocked people never count.
inline UnreadSummary CountUnread(const std::vector<UnreadRow>& rows, const std::string& myId,
                                 const std::map<std::string, long>& seen, const std::set<std::string>& blocked) {
    UnreadSummary u;
    for (const UnreadRow& r : rows) {
        if (r.recipient != myId || r.sender == myId || r.sender.empty() || blocked.count(r.sender)) continue;
        const auto it = seen.find(r.sender);
        if (r.id <= (it == seen.end() ? 0 : it->second)) continue;
        ++u.perPeer[r.sender]; ++u.total;
    }
    u.conversations = (int)u.perPeer.size();
    return u;
}
/// The badge on an avatar or a tab: "" for none, the number, "99+" above 99.
inline std::string BadgeText(int n) { return n <= 0 ? std::string() : n > 99 ? std::string("99+") : std::to_string(n); }

// ---- links -------------------------------------------------------------------------------------

enum class LinkKind { Web, Join, Unsafe };

namespace detail {
inline std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }
}  // namespace detail

/// The host of an http(s) URL, lowercased, without user info or port ("" when there is none).
inline std::string LinkHost(const std::string& url) {
    const std::size_t s = url.find("://");
    if (s == std::string::npos) return {};
    std::size_t a = s + 3, e = url.find_first_of("/?#", a);
    std::string auth = url.substr(a, e == std::string::npos ? std::string::npos : e - a);
    const std::size_t at = auth.rfind('@');
    if (at != std::string::npos) auth = auth.substr(at + 1);
    if (!auth.empty() && auth[0] == '[') { const std::size_t rb = auth.find(']'); return detail::Lower(auth.substr(0, rb == std::string::npos ? auth.size() : rb + 1)); }
    const std::size_t colon = auth.find(':');
    if (colon != std::string::npos) auth = auth.substr(0, colon);
    return detail::Lower(auth);
}
/// Web: an http(s) link made only of plain URL characters, with a host; Join: an okay://join link (opened
/// inside the launcher, never by the shell); Unsafe: everything else (javascript:, data:, file:, vbscript:,
/// other schemes, network paths, quotes or spaces that could break out of a command line).
inline LinkKind ClassifyLink(const std::string& raw) {
    std::string u = raw;
    while (!u.empty() && (u.front() == ' ' || u.front() == '\t')) u.erase(u.begin());
    while (!u.empty() && (u.back() == ' ' || u.back() == '\t' || u.back() == '\n' || u.back() == '\r')) u.pop_back();
    const std::string low = detail::Lower(u);
    if (low.compare(0, 12, "okay://join?") == 0) return LinkKind::Join;
    if (low.compare(0, 7, "http://") != 0 && low.compare(0, 8, "https://") != 0) return LinkKind::Unsafe;
    if (u.size() > 4096) return LinkKind::Unsafe;
    static const char* kOk = "-._~:/?#[]@!*+,;=&()%";
    for (unsigned char c : u) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!alnum && !std::strchr(kOk, (int)c)) return LinkKind::Unsafe;
    }
    return LinkHost(u).empty() ? LinkKind::Unsafe : LinkKind::Web;
}
/// True when the link may not go where it seems to: a punycode ("xn--") host that can imitate another name,
/// or a "name@" part in front of the host ("https://bank.com@evil.example/").
inline bool LooksDeceptive(const std::string& url) {
    const std::string h = LinkHost(url);
    if (h.compare(0, 4, "xn--") == 0 || h.find(".xn--") != std::string::npos) return true;
    const std::size_t s = url.find("://");
    if (s == std::string::npos) return false;
    const std::size_t e = url.find_first_of("/?#", s + 3);
    return url.substr(s + 3, e == std::string::npos ? std::string::npos : e - s - 3).find('@') != std::string::npos;
}
/// What the confirm box says before an outside link opens.
inline std::string ConfirmText(const std::string& url) {
    const LinkKind k = ClassifyLink(url);
    if (k == LinkKind::Unsafe) return "This link can't be opened safely, so it was not opened.";
    if (k == LinkKind::Join) return "This is a join link. Join the game?";
    std::string t = "This link opens " + LinkHost(url) + " in your browser. Open it?";
    if (LooksDeceptive(url)) t += " Careful: this link may not be the site it looks like.";
    return t;
}

// ---- limits ------------------------------------------------------------------------------------

/// Characters (code points) in UTF-8 text, as the server's char_length counts them.
inline int Utf8Length(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}
/// "" when `body` may be sent now, else a plain sentence. `recentSends` are the unix times of my own
/// recent sends (any order; older ones are ignored).
inline std::string CheckOutgoing(const std::string& body, const std::vector<long>& recentSends, long now) {
    if (body.find_first_not_of(" \t\r\n") == std::string::npos) return "Write a message first.";
    if (Utf8Length(body) > kMaxBodyChars) return privacy::RuleText(privacy::Rule::TooLong);
    int n = 0;
    for (long t : recentSends) if (now - t < 60) ++n;
    if (n >= kMaxPerMinute) return privacy::RuleText(privacy::Rule::RateLimited);
    return {};
}

// ---- events for notifications ------------------------------------------------------------------

/// Something the person may want to hear about. The chat and invite code only produce these; the one
/// notification module decides whether and how to show them (focus, Do not disturb, bursts).
struct Event {
    enum class Kind { Message, GroupMessage, Invite, InviteAnswered, JoinRequest } kind = Kind::Message;
    std::string fromUid, fromName;   ///< who (a public name, never an email)
    std::string text;                ///< a short preview / sentence
    std::string peerUid;             ///< the DM conversation to open (Message, JoinRequest)
    long groupId = 0;                ///< the group to open (GroupMessage)
    long messageId = 0;              ///< the message id
    long inviteId = 0;               ///< the game_invites id (Invite, InviteAnswered; 0 for a chat-only invite)
};

}  // namespace chat
}  // namespace okay
