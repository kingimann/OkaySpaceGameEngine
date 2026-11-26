#pragma once
// Site check (okay-sitecheck, docs/feature_map/17-launcher-platform.md 17.19): what one site's visit recorded, the causes it is
// filed under, and the summary that ranks those causes by how many sites each one hits (so fixes can be ordered). No network,
// no drawing: main.cpp loads the sites through the browser's own tab host and fills a SiteResult; this file reads it.
// Written for this tool from the reports the browser's hooks give (TabHost::FetchReport, the "not implemented: X" console
// lines of PageScripts, the CSS reporter of Css.hpp) and from the image and font file signatures in their specifications
// (PNG, JPEG, GIF, WebP "RIFF....WEBP", AVIF/HEIF "ftyp", ICO, SVG text, WOFF "wOFF", WOFF2 "wOF2"). JSON written with the
// engine's one escaper (okay/Core/Json.hpp). Standard library only.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sitecheck {

/// The area of the browser a cause belongs to (the order fixes are planned in).
enum class Area { Network, JavaScript, DomApi, Css, Layout, Images, Fonts, Encoding };
const char* AreaName(Area a);

/// One sub-request (or the page itself) as the tab host reported it.
struct Fetch {
    std::string url, finalUrl, kind;   ///< kind: page, image, style, script, font, request
    bool reached = false;
    long status = 0;
    std::string netError, contentType, contentEncoding, note;
    std::string head;                  ///< the first bytes of the body (for the file signature), at most 64
};

struct SiteResult {
    std::string site;                  ///< the address from sites.txt
    std::string category;              ///< the sites.txt section ("news", "search" ...)
    std::string finalUrl, title, pageError, charset;
    int status = 0;
    bool loaded = false;               ///< the tab reached Ready
    bool timedOut = false;             ///< the visit hit the time limit with requests still open
    double loadMs = 0.0;               ///< from Open to Ready
    double totalMs = 0.0;              ///< the whole visit, settle time included
    int nodes = 0, elements = 0, textBytes = 0;
    int drawCmds = 0;                  ///< what the layout drew
    double pageHeight = 0.0;
    double blankShare = 0.0;           ///< share of the screenshot's page area in its most common colour (1 = one flat colour)
    std::vector<Fetch> fetches;
    std::vector<std::string> console;  ///< the page's console, warnings and errors only, in order
    std::map<std::string, int> cssProperties, cssSelectors;   ///< not used / not read (name -> times)
    std::string screenshot;            ///< the PNG's path ("" = none)
};

/// One cause found on one site: its area, a short stable key (the ranking groups by it), and the detail seen there.
struct Cause {
    Area area = Area::Network;
    std::string key, detail;
};

/// The causes a visit shows (each key at most once per site).
std::vector<Cause> CausesOf(const SiteResult& r);

/// Helpers the causes are made with (tested on their own).
std::string ImageFormatOf(const std::string& head, const std::string& contentType);   ///< "PNG", "WebP", "SVG", "AVIF" ... or ""
std::string FontFormatOf(const std::string& head);                                    ///< "TrueType", "OpenType", "WOFF", "WOFF2" or ""
std::string NetReason(const std::string& netError);                                   ///< "DNS", "TLS", "timeout", "connect" ...
std::string JsErrorKey(const std::string& consoleLine);                               ///< "" when the line is not a script error
std::string SelectorKey(const std::string& selector);                                 ///< the pseudo-classes in a dropped selector
std::string CharsetOf(const std::string& contentType, const std::string& pageHead);   ///< lower-case, "" when none is named

/// The JSON report of one site (causes included).
std::string SiteJson(const SiteResult& r);

/// The ranking over all sites: each cause key with its area, the number of sites, and up to three example sites.
struct Ranked {
    Area area = Area::Network;
    std::string key;
    int sites = 0;
    std::vector<std::string> examples;
    std::string detail;   ///< the first site's detail
};
std::vector<Ranked> Rank(const std::vector<SiteResult>& all);
/// The summary as a plain-text table (Markdown) and as JSON.
std::string SummaryMarkdown(const std::vector<SiteResult>& all, const std::vector<Ranked>& ranked);
std::string SummaryJson(const std::vector<SiteResult>& all, const std::vector<Ranked>& ranked);

/// sites.txt: one address per line; "# name" lines start a category; blank lines and other "#" lines are skipped.
struct SiteLine { std::string url, category; };
std::vector<SiteLine> ReadSiteList(const std::string& text);

} // namespace sitecheck
