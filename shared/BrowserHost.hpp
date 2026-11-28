#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the host half of one browser tab, for a program with a window.
// The page engine (okay/Browser/Browser.hpp) does no network and no drawing; this does the rest, once, for every program that
// shows pages: the launcher's Browser page (launcher/BrowserPage.cpp) and the browser program (browser/main.cpp).
//   * Pages and images are fetched through the one HTTP seam (okay/Platform/NativeFetch.hpp) on a worker thread (our own
//     okay::thr threads, CLAUDE.md rule 21a) and handed to the tab; images are read by our own decoders
//     (okay/Core/ImageDecode.hpp) and turned into textures through the host's callbacks (a Painter).
//   * Games (step 2, docs/platform/browser_plan.md): a page's OkaySpace web game (okay/Browser/GameLaunch.hpp says which
//     boxes) is downloaded into the site's own folder (size-capped), checked and unpacked (okay/Browser/GamePack.hpp) on a
//     worker thread, then run natively by the player as its own process (okaycli::Child, tools/okay-cli/Process.hpp: no
//     shell, its arguments one by one) with --game-dir and --save-dir in that site's folder. Why a separate process and not
//     the player inside the browser: the player's start-up and frame loop own the whole process (one window, one GL context,
//     global engine state, its own exit), so hosting it in-process would mean rewriting player/main.cpp into a library
//     first; a process also keeps a crashing or hanging game from taking the browser down with it.
// Call Pump() once a frame on the thread that draws.
#include "okay/Browser/Browser.hpp"
#include "okay/Browser/GameLaunch.hpp"
#include "okay/Browser/Profile.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace okay::browser {

struct TabFetch;   // one request of a tab on the wire (BrowserHost.cpp)

class TabHost {
public:
    /// How the host makes and frees a texture from RGBA pixels (null = it could not).
    struct TextureFns {
        std::function<void*(const std::uint8_t* rgba, int w, int h)> make;
        std::function<void(void*)> destroy;
    };
    /// Where games run from. Empty `dataRoot` or `playerExe`: this host runs no games (the launcher's page today).
    struct GameRunner {
        std::string dataRoot;    ///< the program's own data folder; each site gets <dataRoot>sites/<site>/
        std::string playerExe;   ///< the player program to start
    };
    /// The state of one game (by its pack address).
    struct GameStatus {
        enum Phase { None, Downloading, Unpacking, Running, Finished, Failed } phase = None;
        std::uint64_t bytes = 0;   ///< Downloading: bytes so far
        std::string message;       ///< Failed: why, in plain words; Finished: how it ended
    };

    static constexpr std::uint64_t kMaxImageBytes = 8u << 20;

    TabHost(TextureFns textures, std::string userAgent);
    ~TabHost();
    TabHost(const TabHost&) = delete;
    TabHost& operator=(const TabHost&) = delete;

    BrowserTab& Tab() { return m_tab; }
    /// A new visit (old answers and images are dropped). False when the tab refused the address.
    bool Open(const std::string& url);
    /// Forget the page and its images (the tab is empty again). A running game keeps running.
    void Reset();
    /// Hand finished fetches to the tab, ask for new ones, move games along. Once a frame.
    void Pump();
    /// The texture for an image of the page (`src` as written), or null while there is none.
    void* ImageFor(const std::string& src) const;

    /// The browser program's own pages ("about:newtab" ..., okay/Browser/Profile.hpp): the host makes their HTML with this
    /// instead of fetching them. Unset (the launcher): such an address is never fetched.
    void SetInternalPages(std::function<std::string(const std::string& url)> make) { m_internal = std::move(make); }
    /// Web fonts (@font-face): the host is handed each font file that arrives with its rule as a TrueType / OpenType file (WOFF and
    /// WOFF2 files are decoded first, okay/Browser/WebFont.hpp; the program makes an OkayUI font of it). Unset: fonts are not
    /// fetched into anything.
    void SetFontLoader(std::function<void(const FontFace& face, const std::vector<std::uint8_t>& bytes)> load) { m_fontLoader = std::move(load); }

    void SetGameRunner(const GameRunner& runner) { m_runner = runner; }
    bool CanRunGames() const { return !m_runner.dataRoot.empty() && !m_runner.playerExe.empty(); }
    /// The pack address an embedded box (`tag`, `src` as written) runs, or "" when it cannot run a game.
    std::string GameUrlFor(const std::string& tag, const std::string& src) const;
    /// The title the page gives its game ("" when it gives none).
    const std::string& GameTitle() const { return m_webConfig.title; }
    GameStatus Game(const std::string& packUrl) const;
    /// Download, check, unpack and start the game. False with `why` when it cannot start now (one game at a time).
    bool Play(const std::string& packUrl, std::string& why);
    /// End the running game.
    void StopGame();

    /// The profile's cookies and localStorage (okay/Browser/Profile.hpp SiteData, one each per profile; not owned, they must
    /// outlive this host). Set on the tab now and again whenever the tab is made anew (Open, Reset). Null: none (the default).
    void SetSiteData(CookieStore* cookies, WebStorage* local);

    /// Site check (tools/sitecheck): one answer the host handed to the tab (after its redirects), for a tool that reports what
    /// failed and why. `body` is valid only during the call. Unset (the default): nothing is reported.
    struct FetchReport {
        std::string url, finalUrl;   ///< what the tab asked for; the last hop's address
        const char* kind = "";       ///< "page", "image", "style", "script", "font", "request" (a page's fetch / XMLHttpRequest)
        bool reached = false;        ///< an answer came (any status)
        long status = 0;
        std::string netError;        ///< the HTTP client's reason when no answer came (DNS, TLS, timeout ...)
        std::string rawHead;         ///< the answer's head
        const std::string* body = nullptr;
        std::string note;            ///< what the host did not use: "redirect refused", "image not decoded", "font not decoded: <why>"
    };
    void SetFetchReporter(std::function<void(const FetchReport&)> report) { m_reporter = std::move(report); }

private:
    std::function<void(const FetchReport&)> m_reporter;
    struct Impl;
    void RefreshPageInfo();
    void Send(std::vector<TabFetch> jobs);
    bool FollowRedirect(TabFetch& job, long status, const std::string& location, const std::vector<std::string>& setCookies,
                        std::vector<TabFetch>& next);
    CookieStore* m_cookies = nullptr;
    WebStorage* m_local = nullptr;
    void DeliverPageAnswerImpl(const std::string& url, bool ok, long status, std::string& body, const std::string& error,
                               const std::string& contentType, const std::string& disposition, const std::string& finalUrl,
                               std::int64_t size, bool tooBig);
    BrowserTab m_tab;
    TextureFns m_textures;
    std::string m_userAgent;
    GameRunner m_runner;
    WebExportConfig m_webConfig;
    const HtmlDocument* m_configFor = nullptr;   // the document m_webConfig was read from
    std::map<std::string, void*> m_images;       // resolved image address -> texture
    std::function<std::string(const std::string&)> m_internal;
    std::function<void(const FontFace&, const std::vector<std::uint8_t>&)> m_fontLoader;
    std::unique_ptr<Impl> m_impl;
};

/// The browser program's downloads: each file is fetched into its place (NativeFetch's FetchToFile: a temporary file renamed
/// when complete, size-capped, cancellable) on a worker thread of its own. Call Pump() once a frame.
class DownloadHost {
public:
    static constexpr std::uint64_t kMaxBytes = 16ull << 30;   ///< a single download is refused beyond 16 GiB
    explicit DownloadHost(std::string userAgent);
    ~DownloadHost();
    DownloadHost(const DownloadHost&) = delete;
    DownloadHost& operator=(const DownloadHost&) = delete;
    /// Start saving `url` to `path` (a full file path that is not taken; UniqueDownloadPath). Returns its index in Items().
    int Start(const std::string& url, const std::string& path, const std::string& contentType, std::int64_t total);
    void Cancel(int index);
    /// Take the workers' progress into Items().
    void Pump();
    const std::vector<DownloadItem>& Items() const { return m_items; }
    /// True once after a download finished (done, failed or cancelled), so the downloads page can be refreshed.
    bool TakeChanged() { const bool c = m_changed; m_changed = false; return c; }

    struct Shared;   // what the workers share (BrowserHost.cpp)

private:
    std::string m_userAgent;
    std::vector<DownloadItem> m_items;
    std::shared_ptr<Shared> m_shared;
    bool m_changed = false;
};

} // namespace okay::browser
