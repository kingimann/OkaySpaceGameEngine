// okay-content: build, sign, check and fetch game content (docs/release/content_delivery.md). Own code; uses shared/Content*.hpp.
//
//   okay-content make-manifest <folder> --game <id> --version <n> [--min-app <x.y.z>] [--group <path-prefix>=<name>]...
//       Scans <folder>, writes <folder>/content.manifest and, when OKAY_CONTENT_SIGNING_SEED is set, <folder>/content.manifest.sig.
//       The folder is what gets uploaded. The seed (64 hex digits) is read from the environment only and is never printed.
//   okay-content verify <folder> --keys <hex>[,<hex>] [--game <id>] [--manifest-only]
//       Checks the signature and that every listed file has the listed size and SHA-256 (and that nothing else is in the folder).
//   okay-content sync <url|folder> <dir> --game <id> --keys <hex>[,<hex>] [--app-version <v>] [--group <g>]... [--budget-mb <n>] [--allow-unsigned]
//       Brings <dir> up to date from the server (http(s) or file:///folder). Prints progress. Exit code 0 = updated or up to date.
//   okay-content status <dir>        what version is committed in <dir>
//   okay-content rollback <dir>      make the previous version visible again
//   okay-content pubkey              the public key (hex) of the seed in OKAY_CONTENT_SIGNING_SEED
//
// Keys: make one with `python3 scripts/sign_index.py genkey` (same ed25519 keys as the release index; keep the seed private).
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "GameContent.hpp"

#if defined(_WIN32)
#include <windows.h>
static void SleepMs(int ms) { Sleep(static_cast<DWORD>(ms)); }
#else
#include <unistd.h>
static void SleepMs(int ms) { usleep(static_cast<useconds_t>(ms) * 1000); }
#endif

using namespace okaycontent;

namespace {

bool Seed(okayac::ed25519::Seed& out) {
    const char* e = std::getenv("OKAY_CONTENT_SIGNING_SEED");
    if (!e) return false;
    std::vector<std::uint8_t> raw;
    if (std::string(e).size() != 64 || !okayac::FromHex(e, raw) || raw.size() != 32) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}

Kind KindFor(const std::string& path, bool& known) {
    known = true;
    std::string ext;
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos) ext = detail::Lower(path.substr(dot + 1));
    static const std::map<std::string, Kind> m = {
        {"glb", Kind::Model}, {"gltf", Kind::Model}, {"obj", Kind::Model}, {"fbx", Kind::Model}, {"dae", Kind::Model}, {"bin", Kind::Model}, {"mtl", Kind::Model}, {"okaymodel", Kind::Model},
        {"png", Kind::Texture}, {"jpg", Kind::Texture}, {"jpeg", Kind::Texture}, {"tga", Kind::Texture}, {"bmp", Kind::Texture}, {"okaymat", Kind::Texture}, {"ktx", Kind::Texture},
        {"okayanim", Kind::Animation}, {"bvh", Kind::Animation}, {"okayclips", Kind::Animation}, {"okaycutscene", Kind::Animation},
        {"okayscene", Kind::Scene}, {"okayprefab", Kind::Scene},
        {"wav", Kind::Audio}, {"ogg", Kind::Audio}, {"mp3", Kind::Audio}, {"flac", Kind::Audio},
        {"okaydialog", Kind::Dialogue}, {"okayquest", Kind::Dialogue}, {"okayloc", Kind::Dialogue},
        {"okayvs", Kind::Script}, {"okayscript", Kind::Script}, {"okaybt", Kind::Script},
        {"okaydata", Kind::Data}, {"okayitems", Kind::Data}, {"okayweapon", Kind::Data}, {"json", Kind::Data}, {"txt", Kind::Data}, {"csv", Kind::Data}};
    const auto it = m.find(ext);
    if (it == m.end()) { known = false; return Kind::Data; }
    return it->second;
}

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::vector<std::string>> opt;
    std::vector<std::string> flags;
    bool Flag(const std::string& f) const { return std::find(flags.begin(), flags.end(), f) != flags.end(); }
    std::string One(const std::string& k) const { auto it = opt.find(k); return it == opt.end() || it->second.empty() ? std::string() : it->second.back(); }
};
Args ParseArgs(int argc, char** argv, int from) {
    Args a;
    for (int i = from; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--allow-unsigned" || s == "--manifest-only") a.flags.push_back(s);
        else if (s.compare(0, 2, "--") == 0 && i + 1 < argc) a.opt[s].push_back(argv[++i]);
        else a.pos.push_back(s);
    }
    return a;
}

int Usage() {
    std::fputs("usage: okay-content make-manifest <folder> --game <id> --version <n> [--min-app <v>] [--group <prefix>=<name>]...\n"
               "       okay-content verify <folder> --keys <hex>[,<hex>] [--game <id>] [--manifest-only]\n"
               "       okay-content sync <url|folder> <dir> --game <id> --keys <hex>[,<hex>] [--app-version <v>] [--group <g>]... [--budget-mb <n>] [--allow-unsigned]\n"
               "       okay-content status <dir> | rollback <dir> | pubkey\n", stderr);
    return 2;
}

int MakeManifest(const Args& a) {
    if (a.pos.size() != 1 || a.One("--game").empty() || a.One("--version").empty()) return Usage();
    const fs::path dir = fs::u8path(a.pos[0]);
    Manifest m;
    m.game = a.One("--game");
    std::uint64_t v = 0;
    if (!detail::Digits(a.One("--version"), v, 1ull << 53)) { std::fputs("--version must be a whole number\n", stderr); return 2; }
    m.version = v;
    m.minApp = a.One("--min-app");
    std::vector<std::pair<std::string, std::string>> groups;
    for (const std::string& g : a.opt.count("--group") ? a.opt.at("--group") : std::vector<std::string>()) {
        const std::size_t eq = g.find('=');
        if (eq == std::string::npos) { std::fputs("--group needs <path-prefix>=<name>\n", stderr); return 2; }
        groups.emplace_back(g.substr(0, eq), g.substr(eq + 1));
    }
    std::error_code ec;
    int problems = 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) { std::fprintf(stderr, "refused: %s is a link\n", it->path().string().c_str()); ++problems; continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        if (rel == kManifestName || rel == kSigName) continue;
        Entry e;
        e.path = rel;
        bool known = false;
        e.kind = KindFor(rel, known);
        if (!known) { std::fprintf(stderr, "refused: %s (unknown file type; content is models, textures, animations, scenes, audio, dialogue, scripts and data only)\n", rel.c_str()); ++problems; continue; }
        e.size = static_cast<std::uint64_t>(it->file_size(ec));
        e.sha256 = detail::FileSha256Hex(it->path());
        for (const auto& g : groups) if (rel.compare(0, g.first.size(), g.first) == 0) e.group = g.second;
        if (std::string w = EntryProblem(e, Limits()); !w.empty()) { std::fprintf(stderr, "refused: %s\n", w.c_str()); ++problems; continue; }
        m.entries.push_back(e);
    }
    if (problems) { std::fprintf(stderr, "%d file(s) refused: nothing written\n", problems); return 1; }
    std::sort(m.entries.begin(), m.entries.end(), [](const Entry& x, const Entry& y) { return x.path < y.path; });
    if (std::string w = Problem(m); !w.empty()) { std::fprintf(stderr, "manifest problem: %s\n", w.c_str()); return 1; }
    const std::string text = Serialize(m);
    if (!detail::WriteAtomic(dir / kManifestName, text)) { std::fputs("cannot write content.manifest\n", stderr); return 1; }
    okayac::ed25519::Seed seed{};
    if (Seed(seed)) {
        if (!detail::WriteAtomic(dir / kSigName, MakeSignatureFile(text, seed))) { std::fputs("cannot write content.manifest.sig\n", stderr); return 1; }
        std::printf("wrote content.manifest (version %llu, %zu files, %llu bytes) and content.manifest.sig\n", static_cast<unsigned long long>(m.version), m.entries.size(), static_cast<unsigned long long>(m.TotalBytes()));
    } else {
        std::error_code e2; fs::remove(dir / kSigName, e2);
        std::printf("wrote content.manifest (version %llu, %zu files) UNSIGNED: set OKAY_CONTENT_SIGNING_SEED to sign\n", static_cast<unsigned long long>(m.version), m.entries.size());
    }
    return 0;
}

int Verify(const Args& a) {
    if (a.pos.size() != 1 || a.One("--keys").empty()) return Usage();
    const fs::path dir = fs::u8path(a.pos[0]);
    const std::string text = SlurpFile((dir / kManifestName).u8string()), sig = SlurpFile((dir / kSigName).u8string());
    const SigVerdict v = CheckSignature(a.One("--keys"), text, sig);
    if (!v.Accepts()) { std::printf("FAIL: %s\n", v.message.c_str()); return 1; }
    Manifest m; std::string why;
    if (!Parse(text, m, &why)) { std::printf("FAIL: %s\n", why.c_str()); return 1; }
    if (!a.One("--game").empty() && m.game != a.One("--game")) { std::printf("FAIL: the manifest is for game '%s'\n", m.game.c_str()); return 1; }
    if (a.Flag("--manifest-only")) { std::printf("OK: version %llu, %zu files listed, signature valid (key %d)\n", static_cast<unsigned long long>(m.version), m.entries.size(), v.keyIndex); return 0; }
    int bad = 0;
    for (const Entry& e : m.entries) if (!detail::FileHasSizeAndHash(dir / detail::P(e.path), e, true)) { std::printf("FAIL: %s does not match the manifest\n", e.path.c_str()); ++bad; }
    std::set<std::string> listed;
    for (const Entry& e : m.entries) listed.insert(e.path);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        if (rel != kManifestName && rel != kSigName && !listed.count(rel)) { std::printf("NOTE: %s is not in the manifest (it will not be served)\n", rel.c_str()); }
    }
    if (bad) return 1;
    std::printf("OK: version %llu, %zu files, signature valid (key %d)\n", static_cast<unsigned long long>(m.version), m.entries.size(), v.keyIndex);
    return 0;
}

int DoSync(const Args& a) {
    if (a.pos.size() != 2 || a.One("--game").empty()) return Usage();
    std::string url = a.pos[0];
    if (url.find("://") == std::string::npos) url = "file://" + url;
    GameContentConfig cfg;
    cfg.url = url; cfg.game = a.One("--game"); cfg.keys = a.One("--keys"); cfg.allowUnsigned = a.Flag("--allow-unsigned");
    if (a.opt.count("--group")) cfg.groups = a.opt.at("--group");
    if (!a.One("--budget-mb").empty()) { std::uint64_t mb = 0; if (detail::Digits(a.One("--budget-mb"), mb, 1u << 20)) cfg.budgetBytes = mb << 20; }
    std::string lastFile;
    SyncConfig dummy;
    std::unique_ptr<Transport> t = MakeTransport(url, (fs::u8path(a.pos[1]) / "scratch").u8string());
    if (!t) { std::fputs("the address must be http://, https:// or a folder / file:///folder\n", stderr); return 2; }
    SyncConfig sc;
    sc.root = a.pos[1]; sc.game = cfg.game; sc.appVersion = a.One("--app-version"); sc.trustedKeys = cfg.keys; sc.allowUnsigned = cfg.allowUnsigned;
    sc.groups = cfg.groups; sc.budgetBytes = cfg.budgetBytes; sc.sleepMs = SleepMs;
    sc.log = [](const std::string& l) { std::printf("%s\n", l.c_str()); };
    sc.progress = [&](const Progress& p) {
        if (p.phase == Phase::Downloading && p.file != lastFile) { lastFile = p.file; std::printf("  [%d/%d] %s\n", p.fileIndex, p.fileCount, p.file.c_str()); }
        return true;
    };
    const SyncResult r = Sync(sc, *t).Run();
    std::printf("%s\n", r.message.c_str());
    std::printf("status=%s reason=%s version=%llu downloaded=%d reused=%d skipped=%d bytes=%llu retries=%d corrupt-rejected=%d\n", r.status == Status::Updated ? "updated" : r.status == Status::UpToDate ? "up-to-date" : r.status == Status::Cancelled ? "cancelled" : "failed",
                ReasonName(r.reason), static_cast<unsigned long long>(r.version), r.filesDownloaded, r.filesReused, r.filesSkipped, static_cast<unsigned long long>(r.bytesDownloaded), r.retries, r.corruptRejected);
    for (const std::string& x : r.removed) std::printf("removed: %s\n", x.c_str());
    std::error_code ec; fs::remove_all(fs::u8path(a.pos[1]) / "scratch", ec);
    return (r.status == Status::Updated || r.status == Status::UpToDate) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    const Args a = ParseArgs(argc, argv, 2);
    if (cmd == "make-manifest") return MakeManifest(a);
    if (cmd == "verify") return Verify(a);
    if (cmd == "sync") return DoSync(a);
    if (cmd == "status" && a.pos.size() == 1) {
        Commit c;
        if (!ReadCommit(StorePaths{a.pos[0]}, c)) { std::puts("no content committed here"); return 1; }
        std::printf("game=%s version=%llu previous=%llu floor=%llu manifest-sha256=%s\n", c.game.c_str(), static_cast<unsigned long long>(c.version), static_cast<unsigned long long>(c.previous), static_cast<unsigned long long>(c.floor), c.manifestSha256.c_str());
        return 0;
    }
    if (cmd == "rollback" && a.pos.size() == 1) { std::string why; if (Rollback(StorePaths{a.pos[0]}, &why)) { std::puts("rolled back"); return 0; } std::printf("%s\n", why.c_str()); return 1; }
    if (cmd == "pubkey") {
        okayac::ed25519::Seed s{};
        if (!Seed(s)) { std::fputs("OKAY_CONTENT_SIGNING_SEED is not set to 64 hex digits\n", stderr); return 2; }
        const auto p = okayac::ed25519::PublicKeyFromSeed(s);
        std::printf("%s\n", okayac::ToHex(p.data(), p.size()).c_str());
        return 0;
    }
    return Usage();
}
