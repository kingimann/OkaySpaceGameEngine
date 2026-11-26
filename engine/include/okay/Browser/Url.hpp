#pragma once
// Browser (page engine): web addresses. Splitting an address into its parts and resolving a link against the page it is on, written
// from RFC 3986 (section 3 for the parts, Appendix B for the split, section 5.2 for "relative resolution" and its
// "remove_dot_segments", section 6.2.3 for the default ports when two addresses are compared by origin).
// Standard library only.
#include <string>
#include <utility>
#include <vector>

namespace okay::browser {

/// The five parts of RFC 3986 section 3. The `has*` flags tell an empty part ("http://a/?") from a missing one ("http://a/").
struct Url {
    std::string scheme;      ///< lower-cased, without the ':'
    std::string authority;   ///< userinfo@host:port as written (host lower-cased)
    std::string path;
    std::string query;       ///< without the '?'
    std::string fragment;    ///< without the '#'
    bool hasScheme = false, hasAuthority = false, hasQuery = false, hasFragment = false;
};

/// Split `text` (leading and trailing spaces removed) into its parts. Never fails: anything is some kind of reference.
Url ParseUrl(const std::string& text);
/// Put the parts back together (RFC 3986 section 5.3).
std::string ToString(const Url& u);
/// RFC 3986 section 5.2.4.
std::string RemoveDotSegments(const std::string& path);
/// Resolve `ref` against the absolute address `base` (RFC 3986 section 5.2.2, strict). `ref` that has its own scheme is
/// returned with its dot segments removed.
std::string ResolveUrl(const std::string& base, const std::string& ref);
/// "scheme://host:port" with the scheme and host lower-cased and the default port (80 for http, 443 for https) left out;
/// empty for an address with no host.
std::string OriginOf(const std::string& url);
/// Both addresses have the same, non-empty origin.
bool SameOrigin(const std::string& a, const std::string& b);
/// The address without its "#fragment".
std::string WithoutFragment(const std::string& url);
/// The fragment, without the '#' ("" when there is none).
std::string FragmentOf(const std::string& url);
/// The last path segment's extension, lower-cased, without the dot ("" when there is none): "/a/notes.TXT?x" -> "txt".
std::string ExtensionOf(const std::string& url);
/// The host part (no userinfo, no port), lower-cased.
std::string HostOf(const std::string& url);
/// The application/x-www-form-urlencoded byte serializer of the WHATWG URL standard (section 5.2): ASCII letters, digits and
/// "*-._" stay, a space becomes '+', every other byte of the UTF-8 text becomes %XX (upper-case hex). Used for searches from
/// the address bar and for form submission (Forms.hpp).
std::string FormUrlEncode(const std::string& text);
/// name=value pairs, each encoded with FormUrlEncode, joined with '&' (the standard's urlencoded serializer).
std::string FormUrlEncodePairs(const std::vector<std::pair<std::string, std::string>>& pairs);

} // namespace okay::browser
