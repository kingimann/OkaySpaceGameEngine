#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the host half of one browser tab, for a program with a window.
// The page engine (okay/Browser/Browser.hpp) does no network and no drawing; this does the rest, once, for every program that
// shows pages: the launcher's Browser page (launcher/BrowserPage.cpp) and the browser program (browser/main.cpp).
//   * Pages and images are fetched through the one HTTP seam (okay/Platform/NativeFetch.hpp) on a worker thread (our own
//     okay::thr threads, CLAUDE.md rule 21a) and handed to the tab; images are read by our own decoders
//     (okay/Core/ImageDecode.hpp) and turned into textures through the host's callbacks (an SDL renderer or a Painter).
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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace okay::browser {

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

private:
    struct Impl;
    void RefreshPageInfo();
    BrowserTab m_tab;
    TextureFns m_textures;
    std::string m_userAgent;
    GameRunner m_runner;
    WebExportConfig m_webConfig;
    const HtmlDocument* m_configFor = nullptr;   // the document m_webConfig was read from
    std::map<std::string, void*> m_images;       // resolved image address -> texture
    std::unique_ptr<Impl> m_impl;
};

} // namespace okay::browser
