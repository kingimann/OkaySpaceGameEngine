// The self-updater and runtime fetcher (Updater.hpp). Moved out of main.cpp unchanged (docs/editor/main_split_plan.md,
// move 2): only the includes, the namespace lines and one default argument (now in the header) differ.
#include "EditorCommon.hpp"
#include "Updater.hpp"
#include "MainShared.hpp"            // SysNoWindow (main.cpp)
#include "AppRestart.hpp"            // detached self-update: wait, swap, relaunch once
#include "UpdaterCore.hpp"           // the updater rules shared with the launcher: the checksum list is REQUIRED (docs/release/updater.md)
#include "okay/Platform/NativeFetch.hpp"   // the HTTP seam for the update download: WinHTTP / our own client

namespace oked {

// ---- Self-updater + runtime fetcher -----------------------------------
// The editor can pull newer published builds from the releases bucket and, when exporting a
// game, fetch the player runtime if it isn't sitting beside the editor — so a
// fresh editor "generates everything it needs" without a manual file shuffle.
namespace updater {
namespace fs = std::filesystem;

// Published builds live only in the public Supabase "releases" bucket. The editor
// never needs GitHub at run time (owner decision 2026-11-20). RawUrl() below
// builds the per-file URL.

std::atomic<bool> pendingQuit{false}; // set true after a successful self-update relaunch

// Absolute path of the running executable.
std::string SelfPath() {
#if defined(_WIN32)
    // The whole path, however deep the folder: a MAX_PATH buffer cut it. Still the ANSI
    // form, because the callers turn it into a path with fs::path(std::string).
    std::string buf(512, '\0');
    for (;;) {
        const DWORD n = GetModuleFileNameA(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); return buf; }
        if (buf.size() >= 32768) return {};   // the longest path Windows has
        buf.assign(buf.size() * 2, '\0');
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
    return {};
#endif
}

// Append a unique cache-busting query parameter. The download server is
// fronted by a CDN that caches files for minutes, so a plain request can hand
// back a stale VERSION.txt or an old .exe right after a release — the cause of
// "it didn't update". Varying the URL per request forces a fresh fetch.
std::string BustCache(const std::string& url) {
    static unsigned counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string sep = url.find('?') == std::string::npos ? "?" : "&";
    return url + sep + "okaycb=" + std::to_string(now) + "_" + std::to_string(counter++);
}

// Download a URL to a file through the HTTP seam (NativeFetch.hpp: WinHTTP on Windows, our own client on Linux; no
// outside program). A refusal (a 404, a full disk, no answer) is final, so an error page is never cached as a binary.
// No-cache headers + a unique query string defeat the CDN cache. The checksum list is still REQUIRED by the caller
// (UpdaterCore.hpp); this only fetches bytes.
bool Download(const std::string& url, const fs::path& outPath) {
    std::error_code ec; fs::remove(outPath, ec);
    okay::http::OwnDownloadSpec sp;
    sp.userAgent = "OkaySpace-Updater"; sp.headers = {"Cache-Control: no-cache", "Pragma: no-cache"}; sp.retries = 2;   // a drop resumes with Range
    return okay::http::FetchToFile(BustCache(url), outPath, sp).outcome == okay::http::OwnOutcome::Ok;
}

// The releases bucket: the only place updates come from. OKAY_UPDATE_SERVER overrides the
// Supabase URL (testing); shipped builds get it from OKAY_DEFAULT_ACCOUNT_URL.
std::string SupabaseReleasesBase() {
    const char* e = std::getenv("OKAY_UPDATE_SERVER");
#ifdef OKAY_DEFAULT_ACCOUNT_URL
    const char* builtIn = OKAY_DEFAULT_ACCOUNT_URL;
#else
    const char* builtIn = "";
#endif
    return okayversions::ReleasesBucketBase(e ? e : "", builtIn);
}

// The newest release in the bucket (its VERSION.txt), with the folder its files are in.
okayversions::LatestRelease LatestRelease() {
    static unsigned n = 0;
    return okayversions::ResolveLatestRelease(SupabaseReleasesBase(), [](const std::string& url, std::string& out) {
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path(ec) / ("okayspace_ver_" + std::to_string(okay::restart::CurrentPid()) + "_" + std::to_string(n++) + ".txt");
        const bool got = Download(url, tmp);
        if (got) { std::ifstream f(tmp, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); out = ss.str(); }
        fs::remove(tmp, ec);
        return got;
    });
}

// URL of a published file. ref "sb:<ver>" = that version's folder in the bucket; anything else = the
// newest release's folder there ("" when the bucket can't be reached or none is built in).
std::string RawUrl(const std::string& ref, const std::string& name) {
    if (ref.rfind("sb:", 0) == 0) { const std::string b = SupabaseReleasesBase(); return b.empty() ? std::string() : b + "v" + ref.substr(3) + "/" + name; }
    const okayversions::LatestRelease r = LatestRelease();
    return r.base.empty() ? std::string() : r.base + name;
}

// Compare dotted versions ("1.4.0"); returns -1 / 0 / 1 for a<b / a==b / a>b (shared/ProjectVersion.hpp, which the launcher uses too).
int CompareVersions(const std::string& a, const std::string& b) { return okayver::CompareVersions(a, b); }

// Ask the releases bucket for the published version + release notes. No files are swapped;
// this only reports what's available (the install step is explicit).
UpdateInfo CheckLatest() {
    UpdateInfo info;
    info.current = OKAY_ENGINE_VERSION;
    info.checked = true;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    const okayversions::LatestRelease r = LatestRelease();
    info.latest = r.version;
    info.ref = "sb:" + r.version;
    if (info.latest.empty()) {
        info.error = r.error;
        return info;
    }

    // Optional human-readable release notes.
    fs::path cf = tmp / "okayspace_changelog.txt";
    if (Download(RawUrl(info.ref, "CHANGELOG.txt"), cf.string())) {
        std::ifstream f(cf);
        std::stringstream ss; ss << f.rdbuf();
        info.notes = ss.str();
        fs::remove(cf, ec);
    }
    info.available = !info.latest.empty() && CompareVersions(info.current, info.latest) < 0;
    return info;
}

// Launch a (new) executable detached from this process.
void Relaunch(const std::string& path) {
#if defined(_WIN32)
    int rc = SysNoWindow(("start \"\" \"" + path + "\"").c_str());
#else
    int rc = SysNoWindow(("\"" + path + "\" >/dev/null 2>&1 &").c_str());
#endif
    (void)rc;
}

// Download one published file (`asset`: "OkayEngine.exe" or "OkaySpacePlayer.exe") of `version` to `dest`.new and check it against the
// release server's checksum list (versions.json). Without that list, an entry for the version, or a SHA-256 for the file nothing is
// downloaded or installed: the reason comes back in `reason`. `dest` itself is never touched here.
bool FetchVerified(const std::string& version, const std::string& asset, const fs::path& dest, std::string& reason) {
    okayversions::Sources src;
    src.bucketBase = SupabaseReleasesBase();
    {   // an old signed list replayed is refused (shared/UpdateGuard.hpp); the floor sits beside this program
        const fs::path self = okay::restart::CurrentExecutable();
        if (!self.empty()) src.floorFile = self.parent_path() / okayupdate::kFloorName;
    }
    std::error_code ec;
    static unsigned n = 0;
    const okayversions::Index index = okayversions::LoadIndex(src, [&](const std::string& url, std::string& out) {
        const fs::path tmp = fs::temp_directory_path(ec) / ("okayspace_idx_" + std::to_string(okay::restart::CurrentPid()) + "_" + std::to_string(n++) + ".txt");
        const bool got = Download(url, tmp);
        if (got) { std::ifstream f(tmp, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); out = ss.str(); }
        fs::remove(tmp, ec);
        return got;
    });
    std::string v = okayver::NormalizeVersion(version);
    if (v.empty() || !index.Find(v)) v = index.ok ? index.latest : std::string();
    const okayversions::Entry* entry = index.ok && !v.empty() ? index.Find(v) : nullptr;
    if (const std::string why = okayupdater::IndexProblem(index, entry, v.empty() ? version : v); !why.empty()) { reason = why; return false; }
    const okayversions::InstallPlan plan = okayversions::PlanInstall(index, v, src.bucketBase, {}, false);
    if (!plan.ok) { reason = plan.error; return false; }
    for (const okayversions::PlanFile& pf : plan.files) {
        if (pf.indexName != asset) continue;
        okayversions::FileInfo check; check.size = pf.size; check.sha256 = pf.sha256;
        const okayupdater::Outcome o = okayupdater::StageFromUrls(asset, pf.urls, dest, &check,
            [&](const std::string& u, const fs::path& to, std::string& err) {
                const bool got = Download(u, to);
                if (!got) err = "the server could not be reached or the file was not found";
                return got;
            });
        if (!o.ok) reason = o.reason;
        return o.ok;
    }
    reason = asset + " is not in the version list for v" + v + ", so it can't be checked and was not downloaded.";
    return false;
}

// Download the published editor and swap it in for the running .exe, then
// relaunch. Returns a human-readable status. Only call when an update is known
// to be available (so this never loops on an already-current build).
std::string InstallUpdate(const std::string& latest, const std::string& ref) {
    std::error_code ec;
    fs::path self = okay::restart::CurrentExecutable();
    if (self.empty()) {
        const std::string narrow = SelfPath();
        if (!narrow.empty()) self = fs::path(narrow);
    }
    if (self.empty()) return "Couldn't locate the running executable.";
    fs::path newFile = self; newFile += ".new";
    // Only a file whose size and SHA-256 are in the release server's checksum list is installed. No list, no entry, no checksum, a failed
    // download or a mismatch all stop here with the reason, and the running program is untouched.
    {
        std::string reason;
        if (!FetchVerified(latest, "OkayEngine.exe", self, reason)) {
            okay::restart::AppendLog(self.parent_path() / "okay_update.log", "editor update refused: " + reason);
            return "Update to v" + latest + " was not installed. " + reason;
        }
    }
    const auto downloadedBytes = fs::file_size(newFile, ec);
    if (ec || downloadedBytes < 100000) {
        fs::remove(newFile, ec);
        return "Downloaded file looked invalid; update aborted.";
    }
    // Reject a cached HTML/error page: a real Windows .exe begins with "MZ".
    {
        std::ifstream chk(newFile, std::ios::binary);
        char magic[2] = {0, 0}; chk.read(magic, 2);
        if (magic[0] != 'M' || magic[1] != 'Z') {
            fs::remove(newFile, ec);
            return "Downloaded file wasn't a valid program (CDN cache?). Try again.";
        }
    }
    // Hand the swap and relaunch to a detached helper. It waits until THIS
    // process has exited (so the single-instance lock and the file lock are
    // gone), installs the staged build, and starts it once with the same
    // arguments and working directory. If the helper cannot be started, stay
    // open — quitting here used to leave nothing running.
    okay::restart::Request req;
    req.executable = self;
    req.arguments = okay::restart::CurrentArguments();
    req.workingDirectory = fs::current_path(ec);
    req.waitForPid = okay::restart::CurrentPid();
    req.logFile = self.parent_path() / "okay_update.log";
    req.replaceTarget = self;
    req.replaceSource = newFile;
    req.replaceBackup = self;
    req.replaceBackup += ".old";
    req.minBytes = 100000;
    req.keepBackup = true;
    req.expectVersion = latest;          // the helper asks the new file for its version before starting it; a bad one is rolled back
    req.rollbackTarget = self;
    std::string err;
    if (!okay::restart::StartHelper(req, err))
        return "Couldn't start the updater (" + err + "). This version is unchanged. See okay_update.log next to the app.";
    pendingQuit = true;
    return "Updating to v" + latest + "... reopening.";
}

} // namespace updater

} // namespace oked
