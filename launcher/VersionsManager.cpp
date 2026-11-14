// The installed-versions store: see VersionsManager.hpp. No SDL, ImGui or network code (downloads are injected).
#include "VersionsManager.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <ostream>
#include <set>
#include <sstream>

#include "OfficialGame.hpp"   // the strict JSON reader and the bundled SHA-256

namespace okayversions {
namespace {

using okayofficial::json::Reader;
using okayofficial::json::Value;

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool IsHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}
/// Printable ASCII with none of the characters that could end a quote or start a path trick.
bool PlainText(const std::string& s, std::size_t maxLen) {
    if (s.empty() || s.size() > maxLen) return false;
    for (unsigned char c : s) {
        if (c < 0x21 || c > 0x7E) return false;
        if (std::strchr("\"'`\\<>|^", c)) return false;
    }
    return true;
}
bool Contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

std::string Num(std::uint64_t n) { return std::to_string((unsigned long long)n); }

/// The scratch folder names are unique per process run, so two jobs never share one.
std::string ScratchName(const char* prefix, const std::string& version) {
    static std::atomic<unsigned> counter{0};
    return std::string(prefix) + version + "-" + std::to_string(++counter);
}

/// Removes a folder when it goes out of scope, unless released: every early return of an install cleans up after itself.
struct ScratchGuard {
    fs::path path;
    ~ScratchGuard() { Drop(); }
    void Drop() {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
        path.clear();
    }
    void Release() { path.clear(); }
};

bool NoSpace(const std::error_code& ec) {
    return ec == std::errc::no_space_on_device || ec == std::errc::file_too_large;
}

bool Hook(const IO& io, const std::string& step) { return io.fail && io.fail(step); }

std::uintmax_t Available(const IO& io, const fs::path& where) {
    std::error_code ec;
    if (io.availableSpace) {
        const std::uintmax_t a = io.availableSpace(where, ec);
        return ec ? 0 : a;
    }
    const fs::space_info si = fs::space(where, ec);
    return ec ? 0 : si.available;
}

fs::path VersionsRoot(const fs::path& launcherDir) { return okayver::VersionsDir(launcherDir); }

std::string TodayText() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return buf;
}

bool WriteMarker(const fs::path& versionDir, const std::string& version, const std::string& source) {
    std::ofstream f(versionDir / "install.txt", std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "okayspace install 1\nversion=" << version << "\nsource=" << source << "\ndate=" << TodayText() << "\n";
    f.flush();
    return (bool)f;
}
void ReadMarker(const fs::path& versionDir, std::string* source, std::string* date) {
    std::ifstream f(versionDir / "install.txt", std::ios::binary);
    std::string line;
    int n = 0;
    while (n++ < 16 && std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("source=", 0) == 0 && source) { *source = line.substr(7); if (*source != "kept" && *source != "installed") source->clear(); }
        else if (line.rfind("date=", 0) == 0 && date) *date = line.substr(5, 32);
    }
}

void SetExecutable(const fs::path& p) {
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
}

}  // namespace

// ---- the files of a release ----------------------------------------------------------------------------------------------

const char* IndexName(Asset a) {
    switch (a) {
        case Asset::Editor: return "OkayEngine.exe";
        case Asset::Player: return "OkaySpacePlayer.exe";
        case Asset::Sdl: return "SDL2.dll";
    }
    return "";
}
std::string DiskName(Asset a) { return a == Asset::Editor ? std::string(okayver::EditorExeName()) : std::string(IndexName(a)); }

bool IsSafeFileName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    bool anyAlnum = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) anyAlnum = true;
        else if (c != '.' && c != '_' && c != '-') return false;
    }
    return anyAlnum;
}

// ---- versions.json -------------------------------------------------------------------------------------------------------

const Entry* Index::Find(const std::string& version) const {
    const std::string v = okayver::NormalizeVersion(version);
    if (v.empty()) return nullptr;
    for (const Entry& e : versions) if (e.version == v) return &e;
    return nullptr;
}

std::string ExpandTemplate(const std::string& tmpl, const std::string& version, const std::string& file) {
    std::string out;
    out.reserve(tmpl.size() + 32);
    for (std::size_t i = 0; i < tmpl.size();) {
        if (tmpl.compare(i, 9, "{version}") == 0) { out += version; i += 9; }
        else if (tmpl.compare(i, 6, "{file}") == 0) { out += file; i += 6; }
        else out += tmpl[i++];
    }
    return out;
}

bool IsAcceptableUrl(const std::string& url) {
    if (url.size() < 12 || url.size() > 2048) return false;
    const bool https = url.compare(0, 8, "https://") == 0;
    const bool local = url.compare(0, 17, "http://127.0.0.1:") == 0 || url.compare(0, 17, "http://127.0.0.1/") == 0 ||
                       url.compare(0, 17, "http://localhost:") == 0 || url.compare(0, 17, "http://localhost/") == 0;
    if (!https && !local) return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c >= 0x7F) return false;
        if (std::strchr("\"'`\\<>|^{}$()*", c)) return false;
    }
    return true;
}

std::string ParseVersionTxt(std::string_view text) {
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {                                   // the first line that has something on it
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        line = std::string(okayver::TrimSpace(text.substr(i, e - i)));
        i = e + 1;
        if (!line.empty()) break;
    }
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) line.erase(0, 3);
    if (line.empty() || line.size() > 24 || line.find('.') == std::string::npos) return {};
    for (char c : line) if (!(c == '.' || (c >= '0' && c <= '9'))) return {};
    return okayver::NormalizeVersion(line);
}

Index ParseIndex(std::string_view text) {
    Index idx;
    auto bad = [&](const std::string& why) {
        idx.ok = false;
        idx.error = why;
        idx.versions.clear();
        return idx;
    };
    if (text.size() > kMaxIndexBytes) return bad("The version list is far bigger than it should be.");
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
    const std::string s(text);
    Value root;
    if (!Reader(s).Parse(root) || root.kind != Value::Object) return bad("The version list is not valid JSON.");

    const Value* sch = root.get("schema");
    std::uint64_t schema = 0;
    if (!sch || sch->kind != Value::Number) return bad("The version list does not say which format it is in.");
    if (!okayofficial::ParseSize(sch, schema)) return bad("The version list is in a format this launcher does not know.");
    idx.schema = schema > (std::uint64_t)INT_MAX ? INT_MAX : (int)schema;
    if (schema != 1)
        return bad("The version list is in format " + Num(schema) + ", which this launcher does not read. Update the launcher.");

    auto head = [&](const char* key) -> std::string {
        const Value* v = root.get(key);
        if (!v || v->kind != Value::String) return {};
        const std::string n = okayver::NormalizeVersion(v->str);
        return n == v->str ? n : std::string();
    };
    idx.latest = head("latest");
    idx.lts = head("lts");

    if (const Value* a = root.get("archive"); a && a->kind == Value::String && a->str.size() <= 512 &&
                                              a->str.compare(0, 8, "https://") == 0 && Contains(a->str, "{file}") &&
                                              IsAcceptableUrl(ExpandTemplate(a->str, "2.0.0", "OkayEngine.exe")))
        idx.archive = a->str;
    if (const Value* b = root.get("bucketPath"); b && b->kind == Value::String && PlainText(b->str, 128) && Contains(b->str, "{file}") &&
                                                 !Contains(b->str, "..") && !Contains(b->str, "://") && b->str[0] != '/')
        idx.bucketPath = b->str;

    const Value* vs = root.get("versions");
    std::set<std::string> seen;
    if (vs && vs->kind == Value::Array) {
        std::size_t scanned = 0;
        for (const Value& ev : vs->arr) {
            if (++scanned > kMaxIndexVersions * 4 || idx.versions.size() >= kMaxIndexVersions) break;
            if (ev.kind != Value::Object) continue;
            const Value* ver = ev.get("version");
            if (!ver || ver->kind != Value::String) continue;
            const std::string v = okayver::NormalizeVersion(ver->str);
            if (v.empty() || v != ver->str || !seen.insert(v).second) continue;
            Entry e;
            e.version = v;
            if (const Value* c = ev.get("channel"); c && c->kind == Value::String && (c->str == "latest" || c->str == "lts")) e.channel = c->str;
            if (const Value* d = ev.get("date"); d && d->kind == Value::String && d->str.size() <= 32 && PlainText(d->str, 32)) e.date = d->str;
            if (const Value* b = ev.get("inBucket"); b && b->kind == Value::Bool) e.inBucket = b->b;
            if (const Value* fs_ = ev.get("files"); fs_ && fs_->kind == Value::Object) {
                for (const auto& kv : fs_->obj) {
                    if (e.files.size() >= 16) break;
                    if (!IsSafeFileName(kv.first) || kv.second.kind != Value::Object) continue;
                    FileInfo fi;
                    std::uint64_t size = 0;
                    const bool haveSize = okayofficial::ParseSize(kv.second.get("size"), size) && size > 0 && size <= kMaxFileBytes;
                    if (const Value* h = kv.second.get("sha256"); h && h->kind == Value::String && IsHex64(h->str)) fi.sha256 = Lower(h->str);
                    if (!haveSize) continue;          // a file whose size is not believable is left out (it will be treated as unlisted)
                    fi.size = size;
                    e.files[kv.first] = fi;
                }
            }
            idx.versions.push_back(std::move(e));
        }
    }
    std::stable_sort(idx.versions.begin(), idx.versions.end(),
                     [](const Entry& a, const Entry& b) { return okayver::CompareVersions(a.version, b.version) > 0; });
    if (idx.latest.empty()) for (const Entry& e : idx.versions) if (e.channel == "latest") { idx.latest = e.version; break; }
    if (idx.lts.empty()) for (const Entry& e : idx.versions) if (e.channel == "lts") { idx.lts = e.version; break; }
    idx.ok = true;
    return idx;
}

Index FallbackIndex(const std::string& latestTxt, const std::string& ltsTxt, const std::string& archiveTemplate, bool bucketAvailable) {
    Index idx;
    idx.fallback = true;
    idx.schema = 0;
    idx.latest = ParseVersionTxt(latestTxt);
    idx.lts = ParseVersionTxt(ltsTxt);
    if (!archiveTemplate.empty() && archiveTemplate.compare(0, 8, "https://") == 0 && Contains(archiveTemplate, "{file}") &&
        IsAcceptableUrl(ExpandTemplate(archiveTemplate, "2.0.0", "OkayEngine.exe")))
        idx.archive = archiveTemplate;
    auto add = [&](const std::string& v, const char* channel) {
        if (v.empty() || idx.Find(v)) return;
        Entry e;
        e.version = v;
        e.channel = channel;
        e.inBucket = bucketAvailable;
        idx.versions.push_back(std::move(e));
    };
    add(idx.latest, "latest");
    add(idx.lts, "lts");
    std::stable_sort(idx.versions.begin(), idx.versions.end(),
                     [](const Entry& a, const Entry& b) { return okayver::CompareVersions(a.version, b.version) > 0; });
    if (idx.versions.empty()) { idx.ok = false; idx.error = "No version information could be read."; return idx; }
    idx.ok = true;
    return idx;
}

namespace {
std::string Fetch(const FetchTextFn& fetch, const std::string& url) {
    std::string body;
    if (!fetch || !IsAcceptableUrl(url) || !fetch(url, body) || body.size() > kMaxIndexBytes) return {};
    return body;
}
void AddHead(Index& idx, const std::string& version, const char* channel, bool inBucket) {
    if (version.empty() || idx.Find(version)) return;
    Entry e;
    e.version = version;
    e.channel = channel;
    e.inBucket = inBucket;
    idx.versions.push_back(std::move(e));
    std::stable_sort(idx.versions.begin(), idx.versions.end(),
                     [](const Entry& a, const Entry& b) { return okayver::CompareVersions(a.version, b.version) > 0; });
}
}  // namespace

Index LoadIndex(const Sources& src, const FetchTextFn& fetch) {
    std::string base = src.bucketBase;
    if (!base.empty() && base.back() != '/') base += '/';
    const bool haveBucket = IsAcceptableUrl(base + "x");
    if (!haveBucket) base.clear();
    std::string why = "The full version list is not available.";
    if (haveBucket) {
        const std::string body = Fetch(fetch, base + "versions.json");
        if (body.empty()) why = "The version list could not be downloaded.";
        else {
            // With a trusted key built in, the index counts only when its signature verifies. A refusal is final: it never falls back
            // to the unsigned VERSION.txt heads, so a forged index cannot be replaced by a "softer" one that still offers installs.
            okayupdate::Verdict sig;
            if (!src.trustedKeys.empty()) sig = okayupdate::Check(src.trustedKeys, body, Fetch(fetch, base + okayupdate::kSigName));
            if (!sig.Accepts()) {
                Index refused;
                refused.signatureRefused = true;
                refused.error = sig.message;
                return refused;
            }
            Index idx = ParseIndex(body);
            idx.signedByPublisher = sig.kind == okayupdate::Kind::Valid;
            if (idx.ok) {
                if (idx.archive.empty()) idx.archive = src.archiveTemplate.compare(0, 8, "https://") == 0 ? src.archiveTemplate : std::string();
                const std::string v = ParseVersionTxt(Fetch(fetch, base + "VERSION.txt")), l = ParseVersionTxt(Fetch(fetch, base + "LTS.txt"));
                if (!v.empty() && okayver::CompareVersions(v, idx.latest) > 0) { idx.latest = v; AddHead(idx, v, "latest", true); }
                if (!l.empty() && (idx.lts.empty() || okayver::CompareVersions(l, idx.lts) > 0)) { idx.lts = l; AddHead(idx, l, "lts", true); }
                return idx;
            }
            why = idx.error;
        }
    }
    std::string latestTxt, ltsTxt;
    std::vector<std::string> urls;
    if (haveBucket) urls.push_back(base + "VERSION.txt");
    for (const std::string& u : src.versionTxtUrls) urls.push_back(u);
    for (const std::string& u : urls) {                              // the newest of every place that answers
        const std::string t = Fetch(fetch, u);
        if (ParseVersionTxt(t).empty()) continue;
        if (ParseVersionTxt(latestTxt).empty() || okayver::CompareVersions(ParseVersionTxt(t), ParseVersionTxt(latestTxt)) > 0) latestTxt = t;
    }
    if (haveBucket) ltsTxt = Fetch(fetch, base + "LTS.txt");
    Index idx = FallbackIndex(latestTxt, ltsTxt, src.archiveTemplate, haveBucket);
    if (!idx.ok) {
        idx.error = "Couldn't reach the version list. Check your internet connection and try again.";
        return idx;
    }
    idx.note = why + " Only the newest versions are shown, and their downloads can't be checked automatically.";
    return idx;
}

std::string PickHighlight(const Index& index, const std::string& requested) {
    const std::string v = okayver::NormalizeVersion(requested);
    if (v.empty()) return {};
    if (index.Find(v)) return v;
    if (!index.lts.empty() && okayver::SameLine(index.lts, v)) return index.lts;
    for (const Entry& e : index.versions)           // newest first
        if (okayver::SameLine(e.version, v)) return e.version;
    return {};
}

// ---- the plan --------------------------------------------------------------------------------------------------------------

std::uint64_t InstallPlan::totalBytes() const {
    std::uint64_t n = 0;
    for (const PlanFile& f : files) n += f.size;
    return n;
}

InstallPlan PlanInstall(const Index& index, const std::string& versionIn, const std::string& bucketBase, const fs::path& localSdl,
                        bool requireSdl) {
    InstallPlan p;
    auto fail = [&](const std::string& why) { p.ok = false; p.error = why; p.files.clear(); return p; };
    p.version = okayver::NormalizeVersion(versionIn);
    if (p.version.empty()) return fail("\"" + versionIn + "\" is not a version number.");
    if (!index.ok) return fail(index.error.empty() ? "The version list is not available." : index.error);
    const Entry* e = index.Find(p.version);
    Entry synth;
    if (!e) {
        if (p.version != index.latest && p.version != index.lts) return fail("v" + p.version + " is not in the version list.");
        synth.version = p.version;
        synth.inBucket = true;
        e = &synth;
    }
    std::string base = bucketBase;
    if (!base.empty() && base.back() != '/') base += '/';

    for (Asset a : {Asset::Editor, Asset::Player, Asset::Sdl}) {
        PlanFile f;
        f.asset = a;
        f.indexName = IndexName(a);
        f.diskName = DiskName(a);
        const auto it = e->files.find(f.indexName);
        const bool listed = it != e->files.end();
        if (listed) { f.size = it->second.size; f.sha256 = it->second.sha256; }
        f.verified = listed && f.sha256.size() == 64;
        if (a == Asset::Sdl && !listed) {                       // not in the index: the launcher's own copy (same SDL 2.x)
            std::error_code ec;
            if (!localSdl.empty() && fs::is_regular_file(localSdl, ec)) {
                f.localCopy = localSdl;
                f.verified = true;
                const std::uintmax_t sz = fs::file_size(localSdl, ec);
                f.size = ec ? 0 : (std::uint64_t)sz;
                p.files.push_back(f);
            } else if (requireSdl) {
                return fail("SDL2.dll is not in the version list, and there is no SDL2.dll next to the launcher to copy.");
            }
            continue;
        }
        auto add = [&](const std::string& url) {
            if (IsAcceptableUrl(url) && std::find(f.urls.begin(), f.urls.end(), url) == f.urls.end()) f.urls.push_back(url);
        };
        if (e->inBucket && !base.empty()) add(base + ExpandTemplate(index.bucketPath, p.version, f.indexName));
        if (!index.archive.empty()) add(ExpandTemplate(index.archive, p.version, f.indexName));
        if (f.urls.empty()) return fail("There is nowhere to download v" + p.version + " from.");
        if (!f.verified)   // REQUIRED: a download with no SHA-256 in the index is refused here, before anything is fetched
            return fail(f.indexName + " has no checksum in the version list" + (index.fallback ? " (versions.json could not be read from the release server)" : e == &synth ? " (v" + p.version + " is not in versions.json yet)" : "") +
                        ", so v" + p.version + " cannot be checked and was not installed. Updates are never installed without a checksum.");
        p.files.push_back(f);
    }
    p.ok = true;
    return p;
}

// ---- installing ------------------------------------------------------------------------------------------------------------

const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::Installed: return "Installed";
        case Outcome::AlreadyInstalled: return "AlreadyInstalled";
        case Outcome::Removed: return "Removed";
        case Outcome::Cancelled: return "Cancelled";
        case Outcome::NotEnoughSpace: return "NotEnoughSpace";
        case Outcome::Failed: return "Failed";
        case Outcome::Refused: return "Refused";
        case Outcome::Skipped: return "Skipped";
    }
    return "?";
}

std::string VerifyFile(const fs::path& path, std::uint64_t size, const std::string& sha256) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return "the file is missing";
    const std::uintmax_t got = fs::file_size(path, ec);
    if (ec) return "its size can't be read";
    if (size != 0 && got != size) return "it is " + Num(got) + " bytes, not the " + Num(size) + " it should be";
    if (!sha256.empty()) {
        const std::string h = okayofficial::FileSha256(path);
        if (h.empty()) return "it can't be read to check it";
        if (h != sha256) return "its checksum does not match";
    }
    return {};
}

Result Install(const fs::path& launcherDir, const InstallPlan& plan, const InstallOptions& opt, const IO& io) {
    Result r;
    auto done = [&](Outcome o, const std::string& m) { r.outcome = o; r.message = m; return r; };
    try {
        if (!plan.ok || plan.files.empty()) return done(Outcome::Failed, plan.error.empty() ? "There is nothing to install." : plan.error);
        const std::string v = okayver::NormalizeVersion(plan.version);
        if (v.empty() || v != plan.version) return done(Outcome::Failed, "\"" + plan.version + "\" is not a version number.");
        for (const PlanFile& f : plan.files)
            if (!IsSafeFileName(f.indexName) || !IsSafeFileName(f.diskName)) return done(Outcome::Failed, "The version list names a file that is not allowed.");
        if (okayver::IsInstalled(launcherDir, v)) { r.path = VersionsRoot(launcherDir) / v; return done(Outcome::AlreadyInstalled, "v" + v + " is already installed."); }
        for (const PlanFile& f : plan.files) {
            // Defence in depth: PlanInstall never plans a downloaded file without a checksum, but a plan is a plain struct.
            if (f.localCopy.empty() && (!f.verified || f.sha256.size() != 64 || f.size == 0))
                return done(Outcome::Refused, f.indexName + " has no checksum in the version list, so it can't be checked and nothing was downloaded. Updates are never installed unchecked.");
            if (f.localCopy.empty() && !io.download) return done(Outcome::Failed, "Downloading is not available.");
        }
        const std::uint64_t need = plan.totalBytes();
        const fs::path versions = VersionsRoot(launcherDir);
        std::error_code ec;
        if (Hook(io, "create-versions")) return done(Outcome::Failed, "Couldn't create the Versions folder.");
        fs::create_directories(versions, ec);
        if (ec) return done(NoSpace(ec) ? Outcome::NotEnoughSpace : Outcome::Failed, "Couldn't create the Versions folder (" + ec.message() + ").");
        if (Hook(io, "space")) return done(Outcome::NotEnoughSpace, "Not enough free disk space.");
        const std::uint64_t avail = Available(io, versions);
        if (need > 0 && avail < need + 16 * kMiB)
            return done(Outcome::NotEnoughSpace, "Not enough free disk space for v" + v + ": it needs " + HumanSize(need) + " and " + HumanSize(avail) + " is free.");

        ScratchGuard scratch;
        scratch.path = versions / ScratchName(".partial-", v);
        const fs::path tools = scratch.path / okayver::ToolsDirName();
        if (Hook(io, "create-scratch")) { scratch.path.clear(); return done(Outcome::Failed, "Couldn't create a folder to download into."); }
        fs::create_directories(tools, ec);
        if (ec) return done(NoSpace(ec) ? Outcome::NotEnoughSpace : Outcome::Failed, "Couldn't create a folder to download into (" + ec.message() + ").");

        Progress prog;
        prog.fileCount = (int)plan.files.size();
        prog.totalBytes = need;
        std::uint64_t doneBytes = 0;
        auto cancelled = [&]() { return io.cancelled && io.cancelled(); };
        auto report = [&]() { if (io.progress) io.progress(prog); };

        int index = 0;
        for (const PlanFile& f : plan.files) {
            prog.fileIndex = ++index;
            prog.file = f.indexName;
            prog.fileBytes = 0;
            prog.fileTotal = f.size;
            prog.doneBytes = doneBytes;
            report();
            if (cancelled()) return done(Outcome::Cancelled, "Cancelled. Nothing was installed.");
            const fs::path tmp = tools / (f.diskName + ".part");
            const fs::path finalPath = tools / f.diskName;

            if (!f.localCopy.empty()) {                              // the launcher's own SDL2.dll
                if (Hook(io, "copy:" + f.indexName)) return done(Outcome::Failed, "Couldn't copy " + f.indexName + ".");
                fs::copy_file(f.localCopy, tmp, fs::copy_options::overwrite_existing, ec);
                if (ec) return done(NoSpace(ec) ? Outcome::NotEnoughSpace : Outcome::Failed, "Couldn't copy " + f.indexName + " (" + ec.message() + ").");
            } else {
                std::string lastError = "no download address", verifyError;   // a file that did not check out is what to report, not a later "no connection"
                bool got = false;
                for (const std::string& url : f.urls) {
                    fs::remove(tmp, ec);
                    if (Hook(io, "download:" + f.indexName)) { lastError = "the download failed"; continue; }
                    const DownloadResult dr = io.download(url, tmp, f.size ? f.size + 64 * 1024 : kMaxFileBytes,
                        [&](std::uint64_t b) { prog.fileBytes = b; prog.doneBytes = doneBytes + b; report(); }, cancelled);
                    if (dr.status == DownloadStatus::Cancelled) return done(Outcome::Cancelled, "Cancelled. Nothing was installed.");
                    if (dr.status == DownloadStatus::DiskFull) return done(Outcome::NotEnoughSpace, "The disk filled up while downloading " + f.indexName + ".");
                    if (dr.status != DownloadStatus::Ok) { lastError = "Couldn't download " + f.indexName + ": " + (dr.error.empty() ? std::string("the download failed") : dr.error); continue; }
                    if (cancelled()) return done(Outcome::Cancelled, "Cancelled. Nothing was installed.");
                    if (Hook(io, "verify:" + f.indexName)) { lastError = "it could not be checked"; continue; }
                    std::string why = VerifyFile(tmp, f.size, f.sha256);
                    if (!why.empty()) {
                        lastError = verifyError = why == "its checksum does not match"
                                        ? f.indexName + " did not match its checksum, so it was not installed. The download may be damaged: try again."
                                        : f.indexName + ": " + why + ".";
                        continue;
                    }
                    got = true;
                    break;
                }
                if (!got) return done(Outcome::Failed, verifyError.empty() ? lastError : verifyError);
            }
            if (Hook(io, "move:" + f.indexName)) return done(Outcome::Failed, "Couldn't put " + f.indexName + " in place.");
            fs::rename(tmp, finalPath, ec);
            if (ec) return done(NoSpace(ec) ? Outcome::NotEnoughSpace : Outcome::Failed, "Couldn't put " + f.indexName + " in place (" + ec.message() + ").");
            if (f.asset != Asset::Sdl) SetExecutable(finalPath);
            const std::uintmax_t sz = fs::file_size(finalPath, ec);
            doneBytes += ec ? f.size : (std::uint64_t)sz;
        }
        if (cancelled()) return done(Outcome::Cancelled, "Cancelled. Nothing was installed.");
        if (Hook(io, "marker") || !WriteMarker(scratch.path, v, opt.markKept ? "kept" : "installed"))
            return done(Outcome::Failed, "Couldn't finish writing the new version's folder.");
        prog.file.clear();
        prog.doneBytes = doneBytes;
        report();

        const fs::path finalDir = versions / v;
        if (Hook(io, "commit")) return done(Outcome::Failed, "Couldn't move the new version into place.");
        fs::remove_all(finalDir, ec);                              // an incomplete folder of that name (no editor in it) is in the way
        ec.clear();
        fs::rename(scratch.path, finalDir, ec);
        if (ec) return done(NoSpace(ec) ? Outcome::NotEnoughSpace : Outcome::Failed, "Couldn't move the new version into place (" + ec.message() + ").");
        scratch.Release();
        r.path = finalDir;
        return done(Outcome::Installed, "Installed v" + v + ".");
    } catch (const std::exception& ex) {
        return done(Outcome::Failed, std::string("The install stopped: ") + ex.what());
    }
}

// ---- the store -------------------------------------------------------------------------------------------------------------

std::uint64_t DirectorySize(const fs::path& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    int n = 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end && n < 200000; it.increment(ec), ++n) {
        std::error_code e2;
        if (it->is_regular_file(e2) && !e2) {
            const std::uintmax_t s = it->file_size(e2);
            if (!e2) total += s;
        }
    }
    return total;
}

std::uint64_t DiskUsage(const fs::path& launcherDir) { return DirectorySize(VersionsRoot(launcherDir)); }

std::vector<Installed> ListInstalled(const fs::path& launcherDir) {
    std::vector<Installed> out;
    for (const std::string& v : okayver::InstalledVersions(launcherDir)) {
        Installed i;
        i.version = v;
        i.dir = VersionsRoot(launcherDir) / v;
        i.bytes = DirectorySize(i.dir);
        ReadMarker(i.dir, &i.source, &i.date);
        out.push_back(std::move(i));
    }
    return out;
}

int CleanStale(const fs::path& launcherDir) {
    int removed = 0;
    std::error_code ec;
    std::vector<fs::path> stale;
    for (fs::directory_iterator it(VersionsRoot(launcherDir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(".partial-", 0) == 0 || name.rfind(".removing-", 0) == 0) stale.push_back(it->path());
    }
    for (const fs::path& p : stale) {
        std::error_code e2;
        fs::remove_all(p, e2);
        if (!e2) ++removed;
    }
    return removed;
}

Result Remove(const fs::path& launcherDir, const std::string& versionIn, const RemoveGuards& g, const IO& io) {
    Result r;
    auto done = [&](Outcome o, const std::string& m) { r.outcome = o; r.message = m; return r; };
    try {
        const std::string v = okayver::NormalizeVersion(versionIn);
        if (v.empty()) return done(Outcome::Refused, "\"" + versionIn + "\" is not a version number.");
        if (!okayver::IsInstalled(launcherDir, v)) return done(Outcome::Refused, "v" + v + " is not installed.");
        if (!g.running.empty() && okayver::NormalizeVersion(g.running) == v)
            return done(Outcome::Refused, "v" + v + " is the version you are running, so it can't be removed.");
        if (!g.launcher.empty() && okayver::NormalizeVersion(g.launcher) == v)
            return done(Outcome::Refused, "v" + v + " is the version of this launcher, so it can't be removed.");
        for (const auto& k : g.keep)
            if (okayver::NormalizeVersion(k.first) == v) return done(Outcome::Refused, "v" + v + " can't be removed: " + k.second);
        if (g.inUse && g.inUse(v)) return done(Outcome::Refused, "An editor of v" + v + " is open. Close it first.");
        const fs::path dir = VersionsRoot(launcherDir) / v;
        const fs::path trash = VersionsRoot(launcherDir) / ScratchName(".removing-", v);
        std::error_code ec;
        if (Hook(io, "remove-rename")) return done(Outcome::Failed, "Couldn't remove v" + v + ". Is one of its files in use?");
        fs::rename(dir, trash, ec);
        if (ec) return done(Outcome::Failed, "Couldn't remove v" + v + " (" + ec.message() + "). Is one of its files in use?");
        std::error_code e2;
        if (Hook(io, "remove-delete")) e2 = std::make_error_code(std::errc::io_error);
        else fs::remove_all(trash, e2);
        if (e2) return done(Outcome::Removed, "Removed v" + v + ". Some of its files could not be deleted yet; they are deleted the next time the launcher starts.");
        return done(Outcome::Removed, "Removed v" + v + ".");
    } catch (const std::exception& ex) {
        return done(Outcome::Failed, std::string("The removal stopped: ") + ex.what());
    }
}

void DiscardKept(const fs::path& launcherDir, const std::string& versionIn) {
    const std::string v = okayver::NormalizeVersion(versionIn);
    if (v.empty()) return;
    std::string source;
    ReadMarker(VersionsRoot(launcherDir) / v, &source, nullptr);
    if (source != "kept") return;                                   // only ever a copy the update itself made
    std::error_code ec;
    const fs::path trash = VersionsRoot(launcherDir) / ScratchName(".removing-", v);
    fs::rename(VersionsRoot(launcherDir) / v, trash, ec);
    if (!ec) fs::remove_all(trash, ec);
}

// ---- keeping the current editor --------------------------------------------------------------------------------------------

CurrentInstall FindCurrentInstall(const fs::path& launcherDir) {
    CurrentInstall c;
    std::error_code ec;
    auto find = [&](const std::string& name) -> fs::path {
        for (const fs::path& d : {launcherDir / okayver::ToolsDirName(), launcherDir}) {
            const fs::path p = d / name;
            if (fs::is_regular_file(p, ec)) return p;
        }
        return {};
    };
    c.editor = find(DiskName(Asset::Editor));
    if (c.editor.empty() && DiskName(Asset::Editor) != IndexName(Asset::Editor)) c.editor = find(IndexName(Asset::Editor));
    c.player = find(DiskName(Asset::Player));
    c.sdl = find(DiskName(Asset::Sdl));
    return c;
}

Result KeepCurrent(const fs::path& launcherDir, const std::string& currentVersion, const CurrentInstall& cur, int keepPrevious,
                   bool requireSdl, const IO& io) {
    Result r;
    auto done = [&](Outcome o, const std::string& m) { r.outcome = o; r.message = m; return r; };
    try {
        if (keepPrevious <= 0) return done(Outcome::Skipped, "Keeping previous versions is turned off.");
        const std::string v = okayver::NormalizeVersion(currentVersion);
        if (v.empty()) return done(Outcome::Skipped, "This is a development build; it has no version to keep.");
        if (okayver::IsInstalled(launcherDir, v)) { r.path = VersionsRoot(launcherDir) / v; return done(Outcome::AlreadyInstalled, "v" + v + " is already kept."); }
        std::error_code ec;
        if (cur.editor.empty() || !fs::is_regular_file(cur.editor, ec)) return done(Outcome::Skipped, "The current editor was not found, so there is nothing to keep.");
        const bool havePlayer = !cur.player.empty() && fs::is_regular_file(cur.player, ec);
        const bool haveSdl = !cur.sdl.empty() && fs::is_regular_file(cur.sdl, ec);
        if (requireSdl && !haveSdl) return done(Outcome::Skipped, "SDL2.dll was not found next to the current editor, so v" + v + " was not kept.");

        struct Item { fs::path from; std::string name; bool exec; };
        std::vector<Item> items{{cur.editor, DiskName(Asset::Editor), true}};
        if (havePlayer) items.push_back({cur.player, DiskName(Asset::Player), true});
        if (haveSdl) items.push_back({cur.sdl, DiskName(Asset::Sdl), false});
        std::uint64_t need = 0;
        for (const Item& it : items) { const std::uintmax_t s = fs::file_size(it.from, ec); if (!ec) need += s; }

        const fs::path versions = VersionsRoot(launcherDir);
        fs::create_directories(versions, ec);
        if (ec) return done(Outcome::Failed, "Couldn't keep v" + v + ": the Versions folder could not be made (" + ec.message() + ").");
        const std::uint64_t avail = Hook(io, "keep-space") ? 0 : Available(io, versions);
        if (avail < need + kSpaceReserve)
            return done(Outcome::Skipped, "Not enough free disk space to keep v" + v + " (it needs " + HumanSize(need) + " and " + HumanSize(avail) +
                                              " is free; the launcher leaves " + HumanSize(kSpaceReserve) + " free). The update goes on without keeping it.");

        ScratchGuard scratch;
        scratch.path = versions / ScratchName(".partial-", v);
        const fs::path tools = scratch.path / okayver::ToolsDirName();
        if (Hook(io, "keep-create")) { scratch.path.clear(); return done(Outcome::Failed, "Couldn't keep v" + v + ": no folder to copy into."); }
        fs::create_directories(tools, ec);
        if (ec) return done(Outcome::Failed, "Couldn't keep v" + v + " (" + ec.message() + ").");
        for (const Item& it : items) {
            if (Hook(io, "keep-copy:" + it.name)) return done(Outcome::Failed, "Couldn't keep v" + v + ": copying " + it.name + " failed.");
            fs::copy_file(it.from, tools / it.name, fs::copy_options::overwrite_existing, ec);
            if (ec) return done(Outcome::Failed, "Couldn't keep v" + v + ": copying " + it.name + " failed (" + ec.message() + ").");
            if (it.exec) SetExecutable(tools / it.name);
            const std::string why = VerifyFile(tools / it.name, (std::uint64_t)fs::file_size(it.from, ec), {});
            if (!why.empty()) return done(Outcome::Failed, "Couldn't keep v" + v + ": " + it.name + " was not copied whole.");
        }
        if (Hook(io, "keep-marker") || !WriteMarker(scratch.path, v, "kept")) return done(Outcome::Failed, "Couldn't keep v" + v + ": its folder could not be finished.");
        if (Hook(io, "keep-commit")) return done(Outcome::Failed, "Couldn't keep v" + v + ": it could not be moved into place.");
        const fs::path finalDir = versions / v;
        fs::remove_all(finalDir, ec);
        ec.clear();
        fs::rename(scratch.path, finalDir, ec);
        if (ec) return done(Outcome::Failed, "Couldn't keep v" + v + " (" + ec.message() + ").");
        scratch.Release();
        r.path = finalDir;
        return done(Outcome::Installed, "Kept v" + v + " so projects made with it can still be opened.");
    } catch (const std::exception& ex) {
        return done(Outcome::Failed, std::string("Couldn't keep the current version: ") + ex.what());
    }
}

std::vector<std::string> PlanPrune(const std::vector<Installed>& installed, int keepPrevious, const std::string& ltsVersion,
                                   const std::vector<std::string>& keep) {
    std::vector<Installed> sorted = installed;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Installed& a, const Installed& b) { return okayver::CompareVersions(a.version, b.version) > 0; });
    std::set<std::string> protect;
    for (const std::string& k : keep) { const std::string n = okayver::NormalizeVersion(k); if (!n.empty()) protect.insert(n); }
    if (!okayver::LineOf(ltsVersion).empty())                       // the newest installed patch of the LTS line stays
        for (const Installed& i : sorted)
            if (okayver::SameLine(i.version, ltsVersion)) { protect.insert(i.version); break; }
    std::vector<std::string> out;
    int keptSoFar = 0;
    for (const Installed& i : sorted) {
        if (i.source != "kept") continue;                           // what the person installed (or put there) is theirs to remove
        if (keptSoFar < (std::max)(0, keepPrevious)) { ++keptSoFar; continue; }
        if (protect.count(i.version)) continue;
        out.push_back(i.version);
    }
    return out;
}

PruneResult Prune(const fs::path& launcherDir, int keepPrevious, const std::string& ltsVersion, const RemoveGuards& guards, const IO& io) {
    PruneResult pr;
    std::vector<std::string> keep;
    for (const auto& k : guards.keep) keep.push_back(k.first);
    for (const std::string& v : PlanPrune(ListInstalled(launcherDir), keepPrevious, ltsVersion, keep)) {
        const Result r = Remove(launcherDir, v, guards, io);
        (r.ok() ? pr.removed : pr.failed).push_back(v);
    }
    return pr;
}

// ---- the channel -----------------------------------------------------------------------------------------------------------

const char* ChannelName(Channel c) { return c == Channel::Lts ? "lts" : "latest"; }
Channel ParseChannel(std::string_view text) { return Lower(std::string(okayver::TrimSpace(text))) == "lts" ? Channel::Lts : Channel::Latest; }

ChannelDecision DecideChannel(Channel channel, const std::string& localIn, const std::string& latestHeadIn, const std::string& ltsHeadIn,
                              const std::vector<std::string>& installed) {
    ChannelDecision d;
    const std::string local = okayver::NormalizeVersion(localIn);
    const std::string latest = okayver::NormalizeVersion(latestHeadIn), lts = okayver::NormalizeVersion(ltsHeadIn);
    if (channel == Channel::Latest) {
        if (latest.empty()) { d.kind = ChannelDecision::NoInfo; d.note = "Couldn't find out which version is the newest."; return d; }
        if (okayver::CompareVersions(localIn, latest) < 0) { d.kind = ChannelDecision::UpdateInPlace; d.version = latest; d.note = "v" + latest + " is available."; }
        return d;
    }
    if (lts.empty()) {
        d.kind = ChannelDecision::NoInfo;
        d.note = "The long-term-support version could not be found (offline, or none has been published yet), so nothing was changed.";
        return d;
    }
    const bool localIsVersion = !local.empty();
    if (localIsVersion && okayver::SameLine(local, lts)) {                  // on the LTS line: its patches are normal updates
        if (okayver::CompareVersions(local, lts) < 0) { d.kind = ChannelDecision::UpdateInPlace; d.version = lts; d.note = "v" + lts + " fixes problems in " + okayver::LineLabel(lts) + "."; }
        else d.note = "You're on the newest " + okayver::LineLabel(lts) + " release.";
        return d;
    }
    if (localIsVersion && okayver::CompareVersions(local, lts) < 0) {       // older than the LTS line: moving up to it is an upgrade
        d.kind = ChannelDecision::UpdateInPlace;
        d.version = lts;
        d.note = "v" + lts + " is the long-term-support version.";
        return d;
    }
    const std::string have = okayver::ResolveKeepTarget(lts, installed);    // newer than the LTS line (or a development build): never changed in place
    if (have.empty()) { d.kind = ChannelDecision::InstallSideBySide; d.version = lts; d.note = "Install v" + lts + " next to this version."; }
    else if (okayver::CompareVersions(have, lts) < 0) { d.kind = ChannelDecision::UpdateSideBySide; d.version = lts; d.note = "v" + lts + " is out; v" + have + " is installed."; }
    else { d.version = have; d.note = "v" + have + " is installed next to this version."; }
    return d;
}

Settings& GlobalSettings() {
    static Settings s;
    return s;
}

bool ApplySettingLine(Settings& s, const std::string& key, const std::string& value) {
    if (key == "update_channel") { s.channel = ParseChannel(value); return true; }
    if (key == "keep_previous") {
        long n = kDefaultKeepPrevious;
        try { n = std::stol(value); } catch (...) {}
        s.keepPrevious = (int)std::clamp<long>(n, 0, kMaxKeepPrevious);
        return true;
    }
    if (key == "default_editor") { s.defaultEditor = okayver::NormalizeVersion(value); return true; }
    if (key == "lts_line") { s.ltsLine = okayver::LineOf(value); return true; }
    return false;
}
void WriteSettings(const Settings& s, std::ostream& out) {
    out << "update_channel=" << ChannelName(s.channel) << "\n";
    out << "keep_previous=" << std::clamp(s.keepPrevious, 0, kMaxKeepPrevious) << "\n";
    if (!s.defaultEditor.empty()) out << "default_editor=" << s.defaultEditor << "\n";
    if (!s.ltsLine.empty()) out << "lts_line=" << s.ltsLine << "\n";
}

// ---- projects and "Open with" ----------------------------------------------------------------------------------------------

ProjectBadge BadgeFor(const std::string& projectVersion, const std::string& currentVersion) {
    ProjectBadge b;
    const std::string pv = okayver::NormalizeVersion(projectVersion);
    b.version = pv.empty() ? "unknown" : "v" + pv;
    switch (okayver::Decide(projectVersion, currentVersion)) {
        case okayver::Decision::Older:
            b.tone = ProjectBadge::Older;
            b.text = "older: " + okayver::LineLabel(pv);
            b.tip = "Made with v" + pv + ". Opening it here asks whether to upgrade it; you can also open it with an installed " + okayver::LineLabel(pv) + " editor.";
            break;
        case okayver::Decision::Newer:
            b.tone = ProjectBadge::Newer;
            b.text = "newer";
            b.tip = "Made with v" + pv + ", which is newer than this editor (v" + okayver::NormalizeVersion(currentVersion) + "). Update OkaySpace, or open it with a newer editor.";
            break;
        default: break;
    }
    return b;
}

std::vector<EditorChoice> OpenWithChoices(const std::string& projectVersion, const std::vector<std::string>& installed,
                                          const std::string& currentVersion) {
    std::vector<EditorChoice> out;
    const std::string cur = okayver::NormalizeVersion(currentVersion);
    std::set<std::string> seen;
    for (const std::string& v : installed) {
        const std::string n = okayver::NormalizeVersion(v);
        if (n.empty() || n == cur || !seen.insert(n).second) continue;
        out.push_back({n, okayver::SameLine(n, projectVersion)});
    }
    std::stable_sort(out.begin(), out.end(), [](const EditorChoice& a, const EditorChoice& b) {
        if (a.sameLine != b.sameLine) return a.sameLine;
        return okayver::CompareVersions(a.version, b.version) > 0;
    });
    return out;
}

std::vector<std::string> OpenWithArgs(unsigned long launcherPid, const std::string& scene) {
    std::vector<std::string> args{"--launcher", std::to_string(launcherPid)};
    for (std::string& a : okayver::KeepVersionArgs(scene)) args.push_back(std::move(a));
    return args;
}

std::string QuoteArgWindows(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') { out.append(backslashes * 2 + 1, '\\'); out += '"'; }
        else { out.append(backslashes, '\\'); out += c; }
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}
std::string QuoteArgPosix(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) { if (c == '\'') out += "'\\''"; else out += c; }
    out += '\'';
    return out;
}
std::string QuoteArg(const std::string& arg) {
#if defined(_WIN32)
    return QuoteArgWindows(arg);
#else
    return QuoteArgPosix(arg);
#endif
}
std::string JoinArgs(const std::vector<std::string>& args) {
    std::string out;
    for (const std::string& a : args) { if (!out.empty()) out += ' '; out += QuoteArg(a); }
    return out;
}

OpenTarget ChooseEditorForOpen(const std::string& projectVersion, const std::string& currentVersion,
                               const std::vector<std::string>& installed, const std::string& defaultEditor) {
    OpenTarget t;
    const std::string def = okayver::NormalizeVersion(defaultEditor);
    if (!okayver::NormalizeVersion(projectVersion).empty() || def.empty() || def == okayver::NormalizeVersion(currentVersion)) return t;
    for (const std::string& v : installed)
        if (okayver::NormalizeVersion(v) == def) { t.version = def; t.keepFlag = true; return t; }
    return t;
}

bool MustCloseEditorFirst(bool editorRunning, const std::string& runningVersion, const std::string& currentVersion,
                          const std::string& targetVersion) {
    if (!editorRunning) return false;
    const std::string target = okayver::NormalizeVersion(targetVersion.empty() ? currentVersion : targetVersion);
    std::string running = okayver::NormalizeVersion(runningVersion);
    if (running.empty()) running = okayver::NormalizeVersion(currentVersion);   // started outside the launcher: assumed to be this one
    return running != target;
}

// ---- command line and hand-off ---------------------------------------------------------------------------------------------

LauncherArgs ParseLauncherArgs(const std::vector<std::string>& args) {
    LauncherArgs out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--versions") {
            out.versions = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) out.version = okayver::NormalizeVersion(args[i + 1]);
        } else if (a.rfind("--versions=", 0) == 0) {
            out.versions = true;
            out.version = okayver::NormalizeVersion(a.substr(11));
        }
    }
    return out;
}

fs::path HandoffFile() {
    std::error_code ec;
    return fs::temp_directory_path(ec) / "okayspace_launcher_open.txt";
}

bool WriteHandoff(const fs::path& file, const LauncherArgs& args, long long nowSeconds) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "versions " << (args.version.empty() ? "-" : args.version) << " " << nowSeconds << "\n";
    f.flush();
    return (bool)f;
}

bool TakeHandoff(const fs::path& file, long long nowSeconds, LauncherArgs* out) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    std::string line;
    {
        std::ifstream f(file, std::ios::binary);
        std::getline(f, line);
    }
    fs::remove(file, ec);
    std::istringstream ss(line);
    std::string kind, ver;
    long long when = 0;
    if (!(ss >> kind >> ver >> when) || kind != "versions") return false;
    if (when < nowSeconds - 120 || when > nowSeconds + 120) return false;    // stale (a crash left it) or from a wrong clock
    if (out) {
        out->versions = true;
        out->version = ver == "-" ? std::string() : okayver::NormalizeVersion(ver);
    }
    return true;
}

// ---- words -----------------------------------------------------------------------------------------------------------------

std::string HumanSize(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * kMiB) std::snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / (1024.0 * (double)kMiB));
    else if (bytes >= kMiB) std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (double)kMiB);
    else if (bytes >= 1024) std::snprintf(buf, sizeof(buf), "%llu KB", (unsigned long long)(bytes / 1024));
    else std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

std::string LtsSentence(const std::string& version) {
    const std::string l = okayver::LineLabel(version);
    return (l.empty() ? std::string("This version") : l) + " is the long-term-support version: it only gets fixes.";
}

}  // namespace okayversions
