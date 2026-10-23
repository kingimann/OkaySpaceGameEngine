#pragma once
// ---------------------------------------------------------------------------
// A join link: everything needed to get into someone's multiplayer session,
// as one string the launcher, the game, friend presence and chat invites all
// share (Steam's "+connect" / rich-presence "connect" string):
//
//   okay://join?game=Arena&addr=203.0.113.7:45000&room=match-2&name=EU%201&pw=1
//   okay://join?game=Arena&relay=relay.okayspace.net:45100&code=K7Q-29X
//
// `game` is the game's title (the launcher finds it in your library by name);
// a session is reached either directly (addr) or through a relay (relay+code).
// A password is only ever put in a link sent privately (an invite).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdlib>
#include <string>

namespace okay {

struct JoinLink {
    std::string game;          ///< game title
    std::string addr;          ///< "host:port" for a direct session
    std::string relay;         ///< "host:port" of the relay for a relayed session
    std::string code;          ///< relay session code
    std::string room;          ///< lobby room ("" = default)
    std::string name;          ///< server / session name to show
    std::string password;      ///< only in private invites
    bool        needsPassword = false;
    std::string host;          ///< who's hosting / who sent it (display only)

    bool Relayed() const { return !relay.empty() && !code.empty(); }
    bool Valid() const { return !game.empty() && (!addr.empty() || Relayed()); }
    /// Where it goes, for display: "203.0.113.7:45000" or "relay · K7Q-29X".
    std::string Where() const { return Relayed() ? "relay \xC2\xB7 " + code : addr; }
    /// The same session (ignores who's hosting / the password).
    bool SameSession(const JoinLink& o) const {
        return game == o.game && addr == o.addr && relay == o.relay && code == o.code && room == o.room;
    }

    std::string Format() const {
        std::string s = "okay://join?game=" + Enc(game);
        if (!addr.empty())     s += "&addr=" + Enc(addr);
        if (!relay.empty())    s += "&relay=" + Enc(relay);
        if (!code.empty())     s += "&code=" + Enc(code);
        if (!room.empty())     s += "&room=" + Enc(room);
        if (!name.empty())     s += "&name=" + Enc(name);
        if (!host.empty())     s += "&host=" + Enc(host);
        if (!password.empty()) s += "&pass=" + Enc(password);
        else if (needsPassword) s += "&pw=1";
        return s;
    }

    /// Parse a link (also accepts a bare "host:port" with no game). False if it
    /// isn't a join link.
    static bool Parse(const std::string& text, JoinLink& out) {
        out = JoinLink{};
        static const char kPrefix[] = "okay://join?";
        std::string t = text;
        while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r')) t.pop_back();
        if (t.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
        std::size_t p = sizeof(kPrefix) - 1;
        while (p < t.size()) {
            std::size_t amp = t.find('&', p);
            if (amp == std::string::npos) amp = t.size();
            const std::string kv = t.substr(p, amp - p);
            const std::size_t eq = kv.find('=');
            const std::string k = kv.substr(0, eq), v = eq == std::string::npos ? std::string() : Dec(kv.substr(eq + 1));
            if (k == "game") out.game = v;
            else if (k == "addr") out.addr = v;
            else if (k == "relay") out.relay = v;
            else if (k == "code") out.code = v;
            else if (k == "room") out.room = v;
            else if (k == "name") out.name = v;
            else if (k == "host") out.host = v;
            else if (k == "pass") { out.password = v; out.needsPassword = !v.empty(); }
            else if (k == "pw") out.needsPassword = (v == "1");
            p = amp + 1;
        }
        return !out.addr.empty() || out.Relayed();
    }

    static std::string Enc(const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string o;
        for (unsigned char c : s) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~' || c == ':')
                o += (char)c;
            else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
        }
        return o;
    }
    static std::string Dec(const std::string& s) {
        std::string o;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                const char h[3] = {s[i + 1], s[i + 2], 0};
                o += (char)std::strtol(h, nullptr, 16); i += 2;
            } else if (s[i] == '+') o += ' ';
            else o += s[i];
        }
        return o;
    }
};

} // namespace okay
