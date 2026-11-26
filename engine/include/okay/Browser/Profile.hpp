#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): what the browser program keeps for the person, apart from the
// pages themselves (round 1 of "a regular browser", docs/platform/browser_plan.md):
//   * the address bar's rule: a typed address or a search (DecideAddress), with the search address as a setting;
//   * the order of the tabs and which one is shown (TabOrder), with the closed tabs to reopen;
//   * the history of every visit (History), the bookmarks (Bookmarks), the zoom of each site (SiteZoom), the settings;
//   * the rules for downloads: which answers are shown as a page and which are saved, and the saved file's name;
//   * our own pages ("about:newtab", "about:history", "about:bookmarks", "about:downloads"), made here as HTML.
// Profile keeps the lasting parts and turns them into JSON and back (okay/Core/Json.hpp), so the program saves them in the
// person's own settings folder. No network, no drawing, no files except SaveProfile / LoadProfile. Standard library only.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay::browser {

// ---- the address bar ---------------------------------------------------------------------------------------------------

/// The search address the browser starts with (owner, 2026-11-26: "use google for search"): Google's documented query form.
/// "%s" is replaced by the terms, encoded with FormUrlEncode (Url.hpp). The person can change it in the settings (an empty
/// setting means no search: the bar then only takes addresses).
inline constexpr const char* kDefaultSearchUrl = "https://www.google.com/search?q=%s";

struct AddressDecision {
    enum Kind {
        Empty,      ///< nothing typed
        Address,    ///< `url` is the address to open
        Search,     ///< `url` is the search address for the typed words
        NoSearch    ///< it looked like words, but no search address is set
    } kind = Empty;
    std::string url;
};

/// What the address bar does with `typed`: an address when it has a scheme ("https://...", "about:..."), is "localhost" or an IP
/// address (opened with http://), or is one word that looks like a host name ("example.com", "example.com/a", "host:8080")
/// (opened with https://); otherwise a search with `searchTemplate` ("%s" = the terms).
AddressDecision DecideAddress(const std::string& typed, const std::string& searchTemplate);
/// The search address for `terms`: "%s" in `searchTemplate` replaced by FormUrlEncode(terms), or the encoded terms added at
/// the end when the template has no "%s". "" when the template is empty.
std::string SearchUrl(const std::string& searchTemplate, const std::string& terms);
/// The placeholder shown in an empty address bar: "Search Google or type an address" for Google's search address, "Search or
/// type an address" for another one, "Type an address" when there is no search.
std::string AddressPlaceholder(const std::string& searchTemplate);

// ---- tabs --------------------------------------------------------------------------------------------------------------

/// The order of the tabs and the active one, by id (the program keeps each tab's page under its id). Like common browsers: a
/// new tab from Ctrl+T goes last, a link opened in a new tab goes right after the tab it came from (after the others already
/// opened from it), closing the active tab shows the one to its right (else the one to its left).
class TabOrder {
public:
    /// A new tab. `fromActive`: opened from the active tab (a link), so it goes after it; else last. `activate`: show it.
    int Open(bool fromActive, bool activate);
    /// Close tab `id`; false when there is no such tab. The closed tab's address is kept to reopen (`url`).
    bool Close(int id, const std::string& url = std::string());
    void Activate(int id);
    void Next();       ///< Ctrl+Tab
    void Previous();   ///< Ctrl+Shift+Tab
    /// Ctrl+1 .. Ctrl+8 (the n-th tab) and Ctrl+9 (the last).
    void ActivateNumber(int n);
    /// Move tab `id` to position `index`.
    void Move(int id, int index);
    /// The address of the last closed tab (Ctrl+Shift+T), removed from the list; "" when none.
    std::string TakeClosed();
    int Active() const { return m_active; }
    int IndexOf(int id) const;
    const std::vector<int>& Ids() const { return m_ids; }
    std::size_t Count() const { return m_ids.size(); }

    static constexpr std::size_t kMaxClosed = 25;

private:
    std::vector<int> m_ids;
    int m_active = -1;
    int m_next = 1;
    int m_openedFrom = -1;     // the tab the last "fromActive" tabs were opened from
    int m_lastOpened = -1;     // the last tab opened from it
    std::vector<std::string> m_closed;
};

// ---- history, bookmarks, zoom ------------------------------------------------------------------------------------------

struct HistoryEntry {
    std::string url, title;
    std::int64_t time = 0;   ///< seconds since 1970 (the host's clock)
};

/// Every page visited, oldest first. A visit to the same address as the newest entry only refreshes it.
class History {
public:
    static constexpr std::size_t kMax = 5000;
    void Add(const std::string& url, const std::string& title, std::int64_t time);
    /// The newest entry for `url` gets `title` (the title is known once the page is read).
    void SetTitle(const std::string& url, const std::string& title);
    void Remove(const std::string& url);
    void Clear() { m_items.clear(); }
    const std::vector<HistoryEntry>& Items() const { return m_items; }
    std::vector<HistoryEntry>& MutableItems() { return m_items; }

private:
    std::vector<HistoryEntry> m_items;
};

struct Bookmark {
    std::string title, url;
};

class Bookmarks {
public:
    static constexpr std::size_t kMax = 2000;
    /// False when `url` is already bookmarked (or the list is full).
    bool Add(const std::string& url, const std::string& title);
    bool Remove(const std::string& url);
    bool Has(const std::string& url) const;
    /// Add when missing, remove when there: the star button. True when it is bookmarked afterwards.
    bool Toggle(const std::string& url, const std::string& title);
    const std::vector<Bookmark>& Items() const { return m_items; }
    std::vector<Bookmark>& MutableItems() { return m_items; }

private:
    std::vector<Bookmark> m_items;
};

/// The zoom of each site (by origin, as common browsers keep it), in the steps they use.
class SiteZoom {
public:
    static const std::vector<float>& Steps();   ///< 0.25 .. 5, 1 included
    float Get(const std::string& origin) const;
    float In(const std::string& origin);      ///< the next bigger step; returns it
    float Out(const std::string& origin);     ///< the next smaller step
    void Reset(const std::string& origin);    ///< back to 1 (100%)
    void Set(const std::string& origin, float zoom);
    const std::map<std::string, float>& All() const { return m_zoom; }

private:
    std::map<std::string, float> m_zoom;   // only the sites that are not at 100%
};

struct BrowserSettings {
    std::string searchUrl = kDefaultSearchUrl;   ///< "%s" = the terms; "" = no search
    std::string homePage;                        ///< "" = the new-tab page
    std::string downloadDir;                     ///< "" = the program's own Downloads folder
    bool showBookmarksBar = true;
    bool askBeforeDownload = true;
};

/// Everything that lasts between runs.
struct Profile {
    History history;
    Bookmarks bookmarks;
    SiteZoom zoom;
    BrowserSettings settings;

    std::string ToJson() const;
    /// Read what ToJson wrote. Unknown keys are skipped; a damaged file gives false and leaves the profile as it was.
    bool FromJson(const std::string& text, std::string* why = nullptr);
};
/// Write the profile to `path` (a temporary file of this writer's own first, renamed over the old one, so a crash never leaves
/// half a file and two programs writing at once never write into the same temporary file).
bool SaveProfile(const Profile& p, const std::string& path, std::string* why = nullptr);
/// Read it. False (and `p` unchanged) when there is no file or it cannot be read.
bool LoadProfile(Profile& p, const std::string& path, std::string* why = nullptr);

/// The profile file as one program sees it, when more than one program uses the same file: the browser program and the
/// launcher's Browser page (owner, 2026-11-26) keep one profile.json in the browser's settings folder. Every change goes through
/// Change(), which applies it at once and keeps it until it is saved. Refresh() reads the file again when another program
/// replaced it (its size or time changed and its text differs) and applies this program's unsaved changes on top, so a change
/// made in one program is never lost to the other's write; Save() does that first, then writes (SaveProfile). Two saves in the
/// same few milliseconds can still race (the later rename wins); the next Refresh shows the result to both.
class ProfileStore {
public:
    using ChangeFn = std::function<void(Profile&)>;
    explicit ProfileStore(std::string path) : m_path(std::move(path)) {}
    /// Read the file now. False (the profile unchanged) when there is none or it is damaged.
    bool Load(std::string* why = nullptr);
    const Profile& Get() const { return m_profile; }
    /// Apply `change` now and remember it until the next Save.
    void Change(ChangeFn change);
    /// Unsaved changes are waiting.
    bool Dirty() const { return !m_pending.empty(); }
    /// The file was replaced by another program since this one last read or wrote it: read it and apply the unsaved changes on
    /// top. True when the profile was replaced by the file's.
    bool Refresh();
    /// Refresh, then write the profile. With no path (a program that keeps nothing) it only forgets the changes.
    bool Save(std::string* why = nullptr);
    const std::string& Path() const { return m_path; }

private:
    struct Stamp { std::int64_t size = -1; std::int64_t time = 0; bool operator==(const Stamp& o) const { return size == o.size && time == o.time; } };
    Stamp FileStamp() const;
    std::string m_path;
    Profile m_profile;
    std::vector<ChangeFn> m_pending;
    Stamp m_seen;              // the file as this program last read or wrote it
    std::uint64_t m_seenHash = 0;
};

// ---- downloads ---------------------------------------------------------------------------------------------------------

/// A header's value from a raw HTTP head ("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n..."), "" when it is missing. The
/// name is matched without regard to case; the last answer's headers count when the head holds several (redirects).
std::string HeaderValue(const std::string& rawHead, const std::string& name);
/// The media type of a Content-Type value, lower-cased, without its parameters ("text/html; charset=utf-8" -> "text/html").
std::string MediaType(const std::string& contentType);

/// What the browser does with an answer.
enum class AnswerUse { Page, PlainText, Image, Download };
/// From the answer's Content-Type and Content-Disposition (and the address when the type is missing): HTML and XHTML are pages,
/// text/* is shown as plain text, image/png, jpeg, gif, bmp, webp and svg are shown as an image, "attachment" and everything
/// else (archives, programs, PDFs, videos ...) is a download.
AnswerUse UseOfAnswer(const std::string& contentType, const std::string& contentDisposition, const std::string& url);

/// The file name to save a download under: the filename (RFC 6266, also the filename*=UTF-8'' form) of Content-Disposition, else
/// the last part of the address's path (percent-decoded), else "download". Cleaned so it is a plain file name on every system:
/// no folders, no "..", no control characters or <>:"/\|?*, no trailing dot or space, not a Windows device name, at most 120
/// bytes; an extension is added from the media type when the name has none and the type is known.
std::string DownloadFileName(const std::string& url, const std::string& contentDisposition, const std::string& contentType);
/// A name in `folder` that is not taken yet: "name.ext", else "name (1).ext", "name (2).ext" ... `exists` says whether a path
/// is taken.
std::string UniqueDownloadPath(const std::string& folder, const std::string& fileName,
                               bool (*exists)(const std::string& path, void* user), void* user);
/// "1.5 MB", "12 KB", "300 bytes" ("unknown size" for a negative size).
std::string SizeText(std::int64_t bytes);

/// One download, as the downloads page shows it.
struct DownloadItem {
    enum State { Active, Done, Failed, Cancelled } state = Active;
    std::string url, path, fileName, contentType, message;
    std::int64_t bytes = 0, total = -1;   ///< total < 0: unknown
};

// ---- our own pages -----------------------------------------------------------------------------------------------------

/// True for the addresses of our own pages ("about:newtab", "about:history", "about:bookmarks", "about:downloads",
/// "about:blank").
bool IsInternalPage(const std::string& url);
/// The HTML of one of our own pages (text escaped; every link is an http(s) address or another of our pages).
std::string InternalPage(const std::string& url, const Profile& p, const std::vector<DownloadItem>& downloads);
/// Text with <, >, & and " escaped for HTML.
std::string HtmlEscape(const std::string& text);

} // namespace okay::browser
