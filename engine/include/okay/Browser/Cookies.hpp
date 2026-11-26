#pragma once
// Browser (page engine), round 3: the browser's cookies. One CookieStore belongs to the person's browser profile: every tab
// sends its cookies on page, image, style, script and fetch requests and stores the Set-Cookie answers, and pages read and
// write it through document.cookie. Written from RFC 6265 (section 5.1.1 dates, 5.1.3 domain matching, 5.1.4 paths, 5.2 the
// Set-Cookie attributes, 5.3 the storage model, 5.4 the Cookie header) with two later rules from the RFC 6265bis draft: the
// SameSite attribute (Strict / Lax / None; a cookie without it counts as Lax, a None cookie must be Secure) and "a page that
// is not https may not set or overwrite a Secure cookie".
//
// Our choices where the RFC leaves room (safe defaults):
//   * there is no public suffix list yet: a Domain attribute must have at least two labels and may not be one of a short
//     built-in list of shared suffixes ("co.uk", "com.au" ...); otherwise the cookie is refused. An IP address host only
//     takes host-only cookies;
//   * at most kMaxCookieBytes per cookie (name + value), kMaxPerDomain per host, kMaxCookies in all (the least recently
//     used goes first), a Cookie header of at most kMaxHeaderBytes;
//   * Save writes only persistent cookies (Expires / Max-Age) that have not expired; session cookies live as long as the store.
// Standard library and okay/Core/Json.hpp only; no files (the host saves ToJson() in the profile folder).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::browser {

struct Cookie {
    enum SameSite { Unset, None, Lax, Strict };
    std::string name, value;
    std::string domain;            ///< lower-cased host (host-only) or Domain attribute without the leading dot
    std::string path = "/";
    bool hostOnly = true;
    bool secure = false;
    bool httpOnly = false;
    bool persistent = false;       ///< has an expiry time (else: a session cookie)
    SameSite sameSite = Unset;     ///< Unset is treated as Lax
    std::int64_t expires = 0;      ///< seconds since 1970 (persistent cookies)
    std::int64_t created = 0, lastAccess = 0;
};

/// Who is asking (RFC 6265bis 5.2 "same-site" and "top-level navigation"; RFC 6265 5.2 "non-HTTP API").
struct CookieContext {
    bool fromScript = false;     ///< document.cookie: HttpOnly cookies are neither read nor written
    bool sameSite = true;        ///< the request's site is the site of the page that made it (or the person typed it)
    bool topLevel = false;       ///< a page load in a tab
    bool safeMethod = true;      ///< GET / HEAD (a cross-site top-level POST does not get Lax cookies)
};

class CookieStore {
public:
    static constexpr std::size_t kMaxCookieBytes = 4096;
    static constexpr std::size_t kMaxPerDomain = 50;
    static constexpr std::size_t kMaxCookies = 3000;
    static constexpr std::size_t kMaxHeaderBytes = 8190;

    using Context = CookieContext;

    /// Store one Set-Cookie header value (or a document.cookie write) that came for `url` at time `now` (seconds since
    /// 1970). False when the cookie was refused or the header was not readable; an expired cookie removes the stored one.
    bool Set(const std::string& url, const std::string& setCookie, std::int64_t now, const Context& ctx = Context());
    /// The Cookie header value for a request to `url` ("a=1; b=2", longer paths first, then older first); "" when none.
    /// document.cookie reads the same text with ctx.fromScript.
    std::string Header(const std::string& url, std::int64_t now, const Context& ctx = Context());

    std::size_t Count() const { return m_items.size(); }
    const std::vector<Cookie>& All() const { return m_items; }
    void Clear() { if (!m_items.empty()) m_changed = true; m_items.clear(); }
    /// Remove the cookies of `host` and its subdomains (the settings' "clear this site").
    void ClearSite(const std::string& host);
    /// Drop cookies whose time is up.
    void Expire(std::int64_t now);
    /// True once after a persistent cookie was added, changed or removed (the host then saves ToJson()).
    bool TakeChanged() { const bool c = m_changed; m_changed = false; return c; }

    /// The persistent cookies that are still valid at `now`, as JSON.
    std::string ToJson(std::int64_t now) const;
    /// Read what ToJson wrote (expired ones are dropped); false when the text is not ours (the store is left empty).
    bool FromJson(const std::string& text, std::int64_t now);
    /// Another program on the same profile saved its cookies (`saved`, read with FromJson): with `replace`, this store's
    /// persistent cookies become the saved ones (its session cookies stay, and win over a saved cookie of the same name,
    /// domain and path); without, only saved cookies this store does not have are added (a store with unsaved changes keeps
    /// its own). Does not set TakeChanged.
    void MergeSaved(const CookieStore& saved, bool replace);

private:
    std::vector<Cookie> m_items;
    bool m_changed = false;
    void Evict(const std::string& domain);
};

/// RFC 6265 5.1.1: a cookie date ("Wed, 21 Oct 2015 07:28:00 GMT" and the older forms) as seconds since 1970; false when
/// it is not one.
bool ParseCookieDate(const std::string& text, std::int64_t& out);
/// RFC 6265 5.1.3: does `host` domain-match `domain`?
bool DomainMatches(const std::string& host, const std::string& domain);
/// The "site" of a host for SameSite and for the Domain check: its last two labels (three under a shared suffix such as
/// "co.uk"); an IP address or a one-label host is its own site.
std::string SiteOf(const std::string& host);

} // namespace okay::browser
