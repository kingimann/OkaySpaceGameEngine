// Browser (page engine): web addresses, written from RFC 3986 (see Url.hpp).
#include "okay/Browser/Url.hpp"

#include "okay/Core/StringUtil.hpp"

#include <cctype>

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

} // namespace okay::browser
