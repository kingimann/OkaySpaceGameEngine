#pragma once
#include <functional>
#include <string>

namespace okay {

/// Where a game keeps its files, in one place: the per-user data / config / cache /
/// log folders for each platform, and the folder the game's assets are loaded from.
///
///   Windows   data   %APPDATA%/App          config %APPDATA%/App/config
///             cache  %LOCALAPPDATA%/App/cache   logs %LOCALAPPDATA%/App/logs
///   Linux     data   $XDG_DATA_HOME/App (~/.local/share)   config $XDG_CONFIG_HOME/App (~/.config)
///             cache  $XDG_CACHE_HOME/App (~/.cache)        logs   $XDG_STATE_HOME/App/logs (~/.local/state)
///   macOS     data   ~/Library/Application Support/App    config  .../Application Support/App/config
///             cache  ~/Library/Caches/App                  logs    ~/Library/Logs/App
///
/// The folders are only computed here; EnsureDir() creates one. Separators are
/// always '/', which every platform accepts.
class Paths {
public:
    enum class Platform { Windows, Linux, MacOS };

    /// The environment the per-user folders are derived from. Current() reads the real one;
    /// tests (or a tool laying out another platform's folders) can fill their own.
    struct Env {
        std::string home, appData, localAppData;
        std::string xdgData, xdgConfig, xdgCache, xdgState;
        static Env Current();
    };

    static Platform CurrentPlatform();

    /// The name the per-user folders are filed under (default "OkaySpace"). Set it once
    /// at startup, before asking for a folder.
    static void SetAppName(const std::string& name);
    static const std::string& AppName();

    // ---- per-user folders (current platform, current app name) ----
    static std::string UserDataDir();
    static std::string ConfigDir();
    static std::string CacheDir();
    static std::string LogDir();

    // ---- the same for any platform / environment / app name (pure; for tests and tools) ----
    static std::string UserDataDirFor(Platform p, const Env& env, const std::string& app);
    static std::string ConfigDirFor(Platform p, const Env& env, const std::string& app);
    static std::string CacheDirFor(Platform p, const Env& env, const std::string& app);
    static std::string LogDirFor(Platform p, const Env& env, const std::string& app);

    /// Create `dir` and its parents. True if it exists afterwards.
    static bool EnsureDir(const std::string& dir);

    /// "a" + "b" -> "a/b" (no doubled or missing separator; an empty side is ignored).
    static std::string Join(const std::string& a, const std::string& b);

    // ---- the assets folder ----
    /// Where game data (scenes, data assets, saves-as-assets) is loaded from. One setting
    /// instead of a SetBaseDir per asset type: set it once, and anything that loads
    /// files can call Resolve(). Listeners (register the per-class SetBaseDir calls here)
    /// are told whenever it changes.
    static void SetAssetBaseDir(const std::string& dir);
    static const std::string& AssetBaseDir();
    /// `relative` under the asset folder; an absolute path (or one with a drive letter) is returned as is.
    static std::string Resolve(const std::string& relative);
    static bool IsAbsolute(const std::string& path);
    /// The StreamingAssets folder of a built game: files the game opens by name at run time, copied from the
    /// project's StreamingAssets/ folder as they are (<Name>_Data/StreamingAssets/). Empty when the game has none.
    /// The player sets it at startup; it ends in '/'.
    static void SetStreamingAssetsDir(const std::string& dir);
    static const std::string& StreamingAssetsDir();
    /// `relative` under StreamingAssets (the file may not exist); "" when the game has no StreamingAssets folder.
    static std::string StreamingPath(const std::string& relative);
    /// Call `fn(newDir)` now and on every later SetAssetBaseDir. Returns an id for RemoveAssetBaseDirListener.
    static int  AddAssetBaseDirListener(std::function<void(const std::string&)> fn);
    static void RemoveAssetBaseDirListener(int id);
};

} // namespace okay
