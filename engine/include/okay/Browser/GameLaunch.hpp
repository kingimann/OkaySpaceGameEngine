#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the rules for running an OkaySpace web game from a page natively,
// with the player (step 2 of docs/platform/browser_plan.md). Pure functions, so they are tested without a network or a
// window; the browser host (shared/BrowserHost.hpp) does the download, the unpack (GamePack.hpp) and starts the player.
//
// Where a game comes from on a page:
//   * an <okay-game src="game.okaypack.gz" width=.. height=..> element: `src` is the game's pack;
//   * a page the web export wrote (platform/web/shell.html with window.OKAY_WEB_CONFIG = {...}): its "packs" name the pack,
//     and its <canvas> is where the game would run, so the canvas gets the Play button.
// Rules (the same limits as an exported web game):
//   * only http and https, and only from the page's own origin (like the page's images);
//   * every site gets its own folder: <data>/sites/<site>/games/<game>/ for the unpacked game (a cache, replaced when the
//     game is downloaded again) and <data>/sites/<site>/saves/<game>/ for its saves, so one site never sees another's files;
//   * the player is started with --game-dir and --save-dir pointing there, nothing else from the page reaches its command
//     line, and no shell is used (okaycli::Child).
// Standard library only.
#include "okay/Browser/Html.hpp"

#include <string>
#include <vector>

namespace okay::browser {

/// The game a page's web-export config names (from window.OKAY_WEB_CONFIG).
struct WebExportConfig {
    bool found = false;
    std::string title;
    std::string gameId;
    std::vector<std::string> packs;   ///< pack files as written ("game.okaypack.gz"), relative to the page
};

/// Look for the web export's config in the page's <script> elements: "window.OKAY_WEB_CONFIG=" followed by a JSON object.
WebExportConfig FindWebExportConfig(const HtmlDocument& doc);

/// The address of the game pack for an embedded element on the page `pageUrl` (absolute), or "" when it may not run:
/// for tag "okay-game" `src` is the pack; for tag "canvas" the page's web-export config must name a pack (the first one).
/// The address must be http(s) on the page's own origin.
std::string GamePackUrl(const std::string& pageUrl, const std::string& tag, const std::string& src, const WebExportConfig& config);

/// A folder name for a site ("https://Example.com:8443" -> "https_example.com_8443"): letters, digits, '.', '-' and '_'
/// only, at most 80 characters (a longer origin is cut and gets a hash so two sites never share a folder).
std::string SiteFolderName(const std::string& origin);
/// A folder name for one game of a site, from its pack address: the file's name without ".okaypack(.gz)" (cleaned like
/// SiteFolderName) and a hash of the whole address, so two games named "game" on one site stay apart.
std::string GameFolderName(const std::string& packUrl);

/// The folders and the player's command line for a game. `dataRoot` is the browser's own data folder (ends in '/').
struct GameLaunchPlan {
    std::string siteDir;        ///< <dataRoot>sites/<site>/
    std::string gameDir;        ///< <siteDir>games/<game>/  (unpacked game, a cache)
    std::string saveDir;        ///< <siteDir>saves/<game>/  (the game's saves; kept when the game is updated)
    std::string downloadPath;   ///< <siteDir>games/<game>.download (the pack while it downloads)
    std::vector<std::string> argv;   ///< player, --game-dir=<gameDir>, --save-dir=<saveDir>
};
GameLaunchPlan MakeGameLaunchPlan(const std::string& dataRoot, const std::string& playerExe, const std::string& packUrl);

} // namespace okay::browser
