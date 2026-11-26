// Browser (page engine): web addresses, written from RFC 3986 (see Url.hpp).
#include "okay/Browser/Url.hpp"

#include "okay/Browser/Encoding.hpp"
#include "okay/Core/StringUtil.hpp"

#include <cctype>
#include <cstdlib>

namespace okay::browser {

namespace {

using okay::str::LowerAscii;

// The authority with its host case-folded (hosts are case-blind, RFC 3986 3.2.2); userinfo is kept as written.
std::string FoldHostCase(const std::string& authority) {
    const std::size_t at = authority.rfind('@');
    const std::size_t hostStart = at == std::string::npos ? 0 : at + 1;
    return authority.substr(0, hostStart) + LowerAscii(authority.substr(hostStart));
}

// RFC 3986 5.2.3: merge a relative path with the base's.
std::string Merge(const Url& base, const std::string& refPath) {
    if (base.hasAuthority && base.path.empty()) return "/" + refPath;
    const std::size_t slash = base.path.rfind('/');
    if (slash == std::string::npos) return refPath;
    return base.path.substr(0, slash + 1) + refPath;
}

} // namespace

Url ParseUrl(const std::string& textIn) {
    // Appendix B: ^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?
    const std::string text = okay::str::Trim(textIn);
    Url u;
    std::size_t i = 0;
    const std::size_t n = text.size();
    {   // scheme: up to the first ':' if no '/', '?' or '#' comes before it, and it is a valid scheme
        const std::size_t stop = text.find_first_of(":/?#");
        if (stop != std::string::npos && stop > 0 && text[stop] == ':') {
            bool ok = std::isalpha((unsigned char)text[0]) != 0;
            for (std::size_t k = 1; k < stop && ok; ++k) {
                const char c = text[k];
                ok = std::isalnum((unsigned char)c) || c == '+' || c == '-' || c == '.';
            }
            if (ok) { u.scheme = LowerAscii(text.substr(0, stop)); u.hasScheme = true; i = stop + 1; }
        }
    }
    if (text.compare(i, 2, "//") == 0) {
        i += 2;
        std::size_t e = text.find_first_of("/?#", i);
        if (e == std::string::npos) e = n;
        u.authority = FoldHostCase(text.substr(i, e - i));
        u.hasAuthority = true;
        i = e;
    }
    {
        std::size_t e = text.find_first_of("?#", i);
        if (e == std::string::npos) e = n;
        u.path = text.substr(i, e - i);
        i = e;
    }
    if (i < n && text[i] == '?') {
        std::size_t e = text.find('#', i);
        if (e == std::string::npos) e = n;
        u.query = text.substr(i + 1, e - i - 1);
        u.hasQuery = true;
        i = e;
    }
    if (i < n && text[i] == '#') { u.fragment = text.substr(i + 1); u.hasFragment = true; }
    return u;
}

std::string ToString(const Url& u) {
    std::string s;
    if (u.hasScheme) s += u.scheme + ":";
    if (u.hasAuthority) s += "//" + u.authority;
    s += u.path;
    if (u.hasQuery) s += "?" + u.query;
    if (u.hasFragment) s += "#" + u.fragment;
    return s;
}

std::string RemoveDotSegments(const std::string& path) {
    // RFC 3986 5.2.4, with the input buffer walked by index and the output as a string.
    std::string in = path, out;
    std::size_t i = 0;
    auto startsWith = [&](const char* p) { return in.compare(i, std::char_traits<char>::length(p), p) == 0; };
    auto restIs = [&](const char* p) { return in.size() - i == std::char_traits<char>::length(p) && startsWith(p); };
    auto dropLastSegment = [&]() {
        const std::size_t slash = out.rfind('/');
        out.erase(slash == std::string::npos ? 0 : slash);
    };
    while (i < in.size()) {
        if (startsWith("../")) i += 3;                                    // A
        else if (startsWith("./")) i += 2;
        else if (startsWith("/./")) i += 2;                               // B: "/./" -> "/"
        else if (restIs("/.")) { in.replace(i, 2, "/"); }
        else if (startsWith("/../")) { i += 3; dropLastSegment(); }       // C: "/../" -> "/"
        else if (restIs("/..")) { in.replace(i, 3, "/"); dropLastSegment(); }
        else if (restIs(".") || restIs("..")) i = in.size();              // D
        else {                                                            // E: move the first segment
            std::size_t e = in.find('/', i + (in[i] == '/' ? 1 : 0));
            if (e == std::string::npos) e = in.size();
            out.append(in, i, e - i);
            i = e;
        }
    }
    return out;
}

std::string ResolveUrl(const std::string& baseText, const std::string& refText) {
    const Url base = ParseUrl(baseText);
    const Url r = ParseUrl(refText);
    Url t;
    if (r.hasScheme) {
        t = r;
        t.path = RemoveDotSegments(r.path);
    } else {
        t.scheme = base.scheme; t.hasScheme = base.hasScheme;
        if (r.hasAuthority) {
            t.authority = r.authority; t.hasAuthority = true;
            t.path = RemoveDotSegments(r.path);
            t.query = r.query; t.hasQuery = r.hasQuery;
        } else {
            t.authority = base.authority; t.hasAuthority = base.hasAuthority;
            if (r.path.empty()) {
                t.path = base.path;
                if (r.hasQuery) { t.query = r.query; t.hasQuery = true; }
                else { t.query = base.query; t.hasQuery = base.hasQuery; }
            } else {
                t.path = r.path[0] == '/' ? RemoveDotSegments(r.path) : RemoveDotSegments(Merge(base, r.path));
                t.query = r.query; t.hasQuery = r.hasQuery;
            }
        }
    }
    t.fragment = r.fragment; t.hasFragment = r.hasFragment;
    return ToString(t);
}

std::string HostOf(const std::string& url) {
    const Url u = ParseUrl(url);
    if (!u.hasAuthority) return std::string();
    std::string a = u.authority;
    const std::size_t at = a.rfind('@');
    if (at != std::string::npos) a = a.substr(at + 1);
    if (!a.empty() && a[0] == '[') {   // an IPv6 literal keeps its brackets
        const std::size_t close = a.find(']');
        return LowerAscii(close == std::string::npos ? a : a.substr(0, close + 1));
    }
    const std::size_t colon = a.find(':');
    return LowerAscii(colon == std::string::npos ? a : a.substr(0, colon));
}

std::string OriginOf(const std::string& url) {
    const Url u = ParseUrl(url);
    if (!u.hasScheme || !u.hasAuthority) return std::string();
    const std::string host = HostOf(url);
    if (host.empty()) return std::string();
    std::string a = u.authority;
    const std::size_t at = a.rfind('@');
    if (at != std::string::npos) a = a.substr(at + 1);
    std::string port;
    const std::size_t close = a.rfind(']');
    const std::size_t colon = a.find(':', close == std::string::npos ? 0 : close);
    if (colon != std::string::npos) port = a.substr(colon + 1);
    if ((u.scheme == "http" && port == "80") || (u.scheme == "https" && port == "443")) port.clear();
    return u.scheme + "://" + host + (port.empty() ? std::string() : ":" + port);
}

bool SameOrigin(const std::string& a, const std::string& b) {
    const std::string oa = OriginOf(a);
    return !oa.empty() && oa == OriginOf(b);
}

std::string ExtensionOf(const std::string& url) {
    const Url u = ParseUrl(url);
    const std::size_t slash = u.path.rfind('/');
    const std::string last = u.path.substr(slash == std::string::npos ? 0 : slash + 1);
    const std::size_t dot = last.rfind('.');
    return dot == std::string::npos ? std::string() : LowerAscii(last.substr(dot + 1));
}

std::string WithoutFragment(const std::string& url) {
    const std::size_t h = url.find('#');
    return h == std::string::npos ? url : url.substr(0, h);
}

std::string FragmentOf(const std::string& url) {
    const std::size_t h = url.find('#');
    return h == std::string::npos ? std::string() : url.substr(h + 1);
}

std::string FormUrlEncode(const std::string& text) {
    static const char* const hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3 / 2);
    for (const char ch : text) {
        const unsigned char c = (unsigned char)ch;
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '*' || c == '-' ||
                          c == '.' || c == '_';
        if (keep) out += ch;
        else if (c == ' ') out += '+';
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string FormUrlEncodePairs(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    for (const auto& p : pairs) {
        if (!out.empty()) out += '&';
        out += FormUrlEncode(p.first);
        out += '=';
        out += FormUrlEncode(p.second);
    }
    return out;
}

std::string FormUrlDecode(const std::string& text) {
    auto hexv = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string bytes;
    bytes.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') { bytes += ' '; continue; }
        if (c == '%' && i + 2 < text.size()) {
            const int hi = hexv(text[i + 1]), lo = hexv(text[i + 2]);
            if (hi >= 0 && lo >= 0) { bytes += (char)(hi * 16 + lo); i += 2; continue; }
        }
        bytes += c;
    }
    return DecodeToUtf8(bytes, TextEncoding::Utf8);
}

// ---- the WHATWG URL standard's view of an address (see Url.hpp ParseWebUrl)
namespace {

bool IsSpecialScheme(const std::string& s) {
    return s == "http" || s == "https" || s == "ws" || s == "wss" || s == "ftp" || s == "file";
}
const char* DefaultPort(const std::string& s) {
    if (s == "http" || s == "ws") return "80";
    if (s == "https" || s == "wss") return "443";
    if (s == "ftp") return "21";
    return "";
}
// Percent-encode the bytes `set` names (and C0 controls, DEL and every non-ASCII byte), keeping '%' as it is (section 1.3).
std::string Encode(const std::string& in, const char* set) {
    static const char* const hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (const char ch : in) {
        const unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c >= 0x7F || std::char_traits<char>::find(set, std::char_traits<char>::length(set), ch)) {
            out += '%'; out += hex[c >> 4]; out += hex[c & 15];
        } else out += ch;
    }
    return out;
}
// The scheme at the start of `s` ("" when there is none): ASCII alpha, then alphanumerics, '+', '-' and '.', then ':'.
std::string SchemeOf(const std::string& s) {
    if (s.empty() || !std::isalpha((unsigned char)s[0])) return std::string();
    for (std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ':') return LowerAscii(s.substr(0, i));
        if (!(std::isalnum((unsigned char)c) || c == '+' || c == '-' || c == '.')) return std::string();
    }
    return std::string();
}

} // namespace

std::string WebUrl::Href() const {
    std::string s = protocol;
    if (hasAuthority) {
        s += "//";
        if (!username.empty() || !password.empty()) s += username + (password.empty() ? std::string() : ":" + password) + "@";
        s += hostname;
        if (!port.empty()) s += ":" + port;
    }
    return s + pathname + search + hash;
}

std::string WebUrl::Origin() const {
    if (!special || protocol == "file:" || hostname.empty()) return "null";
    return protocol + "//" + hostname + (port.empty() ? std::string() : ":" + port);
}

bool ParseWebUrl(const std::string& inputIn, const std::string* base, WebUrl& out) {
    if (inputIn.size() > (2u << 20)) return false;
    // leading / trailing C0 controls and spaces go, and every ASCII tab or newline (4.4, the first steps)
    std::size_t b = 0, e = inputIn.size();
    while (b < e && (unsigned char)inputIn[b] <= 0x20) ++b;
    while (e > b && (unsigned char)inputIn[e - 1] <= 0x20) --e;
    std::string input;
    input.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) if (inputIn[i] != '\t' && inputIn[i] != '\n' && inputIn[i] != '\r') input += inputIn[i];
    std::string scheme = SchemeOf(input);
    WebUrl baseUrl;
    const bool haveBase = base && ParseWebUrl(*base, nullptr, baseUrl);
    if (base && !haveBase) return false;   // an invalid base makes the URL invalid (6.1, the constructor's steps)
    // "http:foo" with a base of the same special scheme is relative (4.4, the "special relative or authority" state)
    if (!scheme.empty() && haveBase && IsSpecialScheme(scheme) && baseUrl.protocol == scheme + ":" &&
        input.compare(scheme.size() + 1, 1, "/") != 0 && input.compare(scheme.size() + 1, 1, "\\") != 0)
        { input = input.substr(scheme.size() + 1); scheme.clear(); }
    std::string abs;
    if (!scheme.empty()) {
        abs = input;
        if (IsSpecialScheme(scheme)) {
            // '\' is '/' before the query, and a special address's slashes after the scheme are always "//" (4.4)
            const std::size_t q = abs.find_first_of("?#");
            for (std::size_t i = 0; i < abs.size() && i < q; ++i) if (abs[i] == '\\') abs[i] = '/';
            std::size_t k = scheme.size() + 1;
            while (k < abs.size() && abs[k] == '/') ++k;
            if (scheme != "file") abs = scheme + "://" + abs.substr(k);
        }
    } else {
        if (!haveBase) return false;
        if (!baseUrl.hasAuthority && !baseUrl.special && (input.empty() || input[0] != '#')) return false;   // an opaque base
        std::string ref = input;
        if (baseUrl.special) {
            const std::size_t q = ref.find_first_of("?#");
            for (std::size_t i = 0; i < ref.size() && i < q; ++i) if (ref[i] == '\\') ref[i] = '/';
        }
        abs = ResolveUrl(baseUrl.Href(), ref);
        scheme = SchemeOf(abs);
    }
    const Url u = ParseUrl(abs);
    if (!u.hasScheme) return false;
    WebUrl w;
    w.protocol = u.scheme + ":";
    w.special = IsSpecialScheme(u.scheme);
    w.hasAuthority = u.hasAuthority;
    if (u.hasAuthority) {
        std::string a = u.authority;
        const std::size_t at = a.rfind('@');
        if (at != std::string::npos) {
            const std::string info = a.substr(0, at);
            a = a.substr(at + 1);
            const std::size_t colon = info.find(':');
            static const char* const kUserinfo = " \"#<>?`{}/:;=@[\\]^|";
            w.username = Encode(info.substr(0, colon), kUserinfo);
            if (colon != std::string::npos) w.password = Encode(info.substr(colon + 1), kUserinfo);
        }
        std::string host = a, port;
        if (!a.empty() && a[0] == '[') {
            const std::size_t close = a.find(']');
            if (close == std::string::npos) return false;
            host = a.substr(0, close + 1);
            for (std::size_t i = 1; i < close; ++i) if (!(std::isxdigit((unsigned char)a[i]) || a[i] == ':' || a[i] == '.')) return false;
            if (close + 1 < a.size()) { if (a[close + 1] != ':') return false; port = a.substr(close + 2); }
        } else {
            const std::size_t colon = a.rfind(':');
            if (colon != std::string::npos) { host = a.substr(0, colon); port = a.substr(colon + 1); }
            // the forbidden host code points (3.5); '%' too for a special host (it is not decoded here)
            if (host.find_first_of(std::string(" #/:<>?@[\\]^|\0", 15)) != std::string::npos) return false;
            if (w.special && host.find('%') != std::string::npos) return false;
            for (const char c : host) if ((unsigned char)c < 0x20 || c == 0x7F) return false;
        }
        host = LowerAscii(host);
        if (w.special && u.scheme != "file" && host.empty()) return false;
        if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return false;
        if (!port.empty()) {
            const long pv = std::strtol(port.c_str(), nullptr, 10);
            if (pv > 65535) return false;
            port = std::to_string(pv);   // "0080" -> "80"
            if (port == DefaultPort(u.scheme)) port.clear();
        }
        w.hostname = host;
        w.port = port;
    } else if (w.special && u.scheme != "file") {
        return false;
    }
    std::string path = u.path;
    if (w.special || u.hasAuthority) path = RemoveDotSegments(path);
    if (w.special && path.empty()) path = "/";
    w.pathname = Encode(path, (w.special || u.hasAuthority) ? " \"#<>?`{}" : "");
    if (u.hasQuery && !u.query.empty()) w.search = "?" + Encode(u.query, w.special ? " \"#<>'" : " \"#<>");
    if (u.hasFragment && !u.fragment.empty()) w.hash = "#" + Encode(u.fragment, " \"<>`");
    out = std::move(w);
    return true;
}

} // namespace okay::browser
