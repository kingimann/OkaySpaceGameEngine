#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the whole browser, drawn with OkayUI inside a rectangle: the tabs, the
// toolbar and address bar (an address or words to search, Google by default), the bookmarks bar, the main menu and the context
// menu, find on the page, zoom per site, the download shelf and the "Save this file?" prompt, the settings, our own pages
// (new tab, history, bookmarks, downloads) and Play for OkaySpace web games (run by the player, okay/Browser/GameLaunch.hpp).
// ONE implementation for both hosts (owner, 2026-11-26: "can you allow user to launch browser in the launcher"):
//   * the browser program (browser/main.cpp) draws it over its whole window and sets the window's title from it;
//   * the launcher's Browser page (sidebar > Browser, launcher/BrowserPage.cpp) draws it in the page area.
// Each tab is a TabHost (BrowserHost.hpp): pages, images, downloads and game packs are fetched and unpacked on worker threads
// (okay::thr), so the host's frame only hands results over and draws. What lasts is one profile.json in the browser's settings
// folder (okay/Browser/Profile.hpp, ProfileStore), shared by the program and the launcher: each change is kept until it is saved,
// and the file is read again when the other program replaced it.
#include "BrowserHost.hpp"
#include "okay/Browser/Profile.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::browser {

class BrowserApp {
public:
    struct Config {
        TabHost::TextureFns textures;   ///< how the host makes image textures (an SDL renderer or a Painter)
        TabHost::GameRunner runner;     ///< dataRoot: the browser's settings folder (sites/, the Downloads fallback); no player = no games
        std::string profilePath;        ///< the shared profile.json ("" = nothing is kept: tests)
        std::string userAgent = "OkaySpace Browser";
        bool switchedOff = false;       ///< Settings > Experimental > Browser is off: nothing is loaded, a line says why
        bool quitItem = false;          ///< the menu has Quit (Ctrl+Q): the program's own window
        std::string idPrefix = "##browser";   ///< prefix of the OkayUI window names (unique in the host's frame)
    };

    explicit BrowserApp(Config config);
    ~BrowserApp();
    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    /// A new tab for what the person typed: an address, or words to search (DecideAddress). Returns its id; -1 when nothing
    /// could be opened (no search address set). Empty `typed`: the home page.
    int OpenTyped(const std::string& typed, bool activate = true);
    /// A new tab for `url` (http, https or one of our own pages). `fromActive`: opened from the shown tab (it goes after it).
    int OpenUrl(const std::string& url, bool activate = true, bool fromActive = false);

    /// Move every tab's fetches, the downloads, the games and the profile along. Once a frame, also while the browser is not
    /// shown (the launcher on another page): it draws nothing.
    void Pump();
    /// Draw the browser in (x, y, w, h) and act on what the person did (calls Pump). Between OkayUI::BeginFrame and EndFrame.
    void Frame(float x, float y, float w, float h);

    /// Ctrl+Q or Menu > Quit (only with Config::quitItem).
    bool WantsQuit() const;
    /// The shown page's title for a window: "<title> - OkaySpace Browser".
    std::string WindowTitle() const;
    /// Start the first page's first game once it is loaded (okay-browser --play).
    void SetAutoplay(bool on);
    /// Write the profile now if it changed (at exit; otherwise it is written a little after each change).
    void Save();
    /// Stop the games and free the textures (before the host's renderer or painter goes away).
    void CloseAll();

    // ---- what the hosts and the tests read ----
    std::size_t TabCount() const;
    int ActiveId() const;
    const std::vector<int>& TabIds() const;
    BrowserTab* TabById(int id);
    BrowserTab& ActiveTab();
    const Profile& GetProfile() const;
    /// The line at the bottom ("" when none): why a link, a game or a download did not happen.
    const std::string& Notice() const;
    /// The download prompt is open (an answer that is a file, waiting for Save or Cancel).
    bool DownloadPromptOpen() const;
    /// A dialog a page opened (alert / confirm / prompt) is shown over the page; its text ("" when none).
    bool PageDialogOpen() const;
    std::string PageDialogText() const;
    const std::vector<DownloadItem>& Downloads() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace okay::browser
