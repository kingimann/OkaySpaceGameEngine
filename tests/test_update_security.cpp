// SEC1 item 3: the self-update path against a hostile release server. A local test server (a map of address -> bytes the test
// fills and then damages in each way an attacker or a bad mirror can) serves a really signed release list and its files; the
// real LoadIndex, PlanInstall, Install and Stage run against it. Throwaway keys, temp folders, no network.
// Checked: tampered file, truncated file, tampered list, truncated and foreign signatures, mix-and-match (a file or a signature
// from another version), a replayed older signed list (downgrade), the "signature check ON/OFF" status line, and that an unsigned
// or forged list never raises the replay floor. Docs: docs/online/security.md "Updater signing".
#include "test_framework.hpp"
#include "VersionsManager.hpp"
#include "UpdaterCore.hpp"
#include "UpdateGuard.hpp"
#include "OfficialGame.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using okayac::ed25519::PublicKey;
using okayac::ed25519::Seed;

namespace {
Seed MakeSeed(std::uint8_t tag) {
    Seed s{};
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = (std::uint8_t)(tag * 37u + i * 11u + 5u);
    return s;
}
std::string PubHex(const Seed& s) { const PublicKey p = okayac::ed25519::PublicKeyFromSeed(s); return okayac::ToHex(p.data(), p.size()); }
std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
void Spit(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }

const std::string kBucket = "https://example.test/storage/v1/object/public/releases/";

// The release server. Everything it serves is in `files`; the test damages entries to play an attacker or a broken mirror.
struct Server {
    std::map<std::string, std::string> files;
    fs::path scratch;
    std::map<std::string, std::string> versions;   // version -> index entry json (published so far, oldest first order by key)
    std::string latest;
    Seed seed{};

    static std::string Sha(const std::string& bytes, const fs::path& tmp) { Spit(tmp, bytes); return okayofficial::FileSha256(tmp); }
    static std::string Payload(const std::string& v, const std::string& name) {   // a file's bytes differ per version and per name
        std::string s = name + "@" + v + ":";
        for (int i = 0; i < 4000; ++i) s += char('a' + (i * 7 + v.size() + name.size()) % 26);
        return s;
    }
    std::string IndexJson() const {
        std::string j = "{\"schema\": 1, \"latest\": \"" + latest + "\", \"lts\": \"\", \"archive\": \"\", \"bucketPath\": \"v{version}/{file}\", \"versions\": [";
        bool first = true;
        for (auto it = versions.rbegin(); it != versions.rend(); ++it) { if (!first) j += ", "; first = false; j += it->second; }
        return j + "]}";
    }
    // Publish `v`: its files go to <bucket>v<v>/<file>, the list is rewritten and signed with `seed` (not signed when sign is false).
    void Publish(const std::string& v, bool sign = true) {
        std::string entry = "{\"version\": \"" + v + "\", \"channel\": \"latest\", \"date\": \"2026-11-10\", \"inBucket\": true, \"files\": {";
        bool first = true;
        for (const char* name : {"OkayEngine.exe", "OkaySpacePlayer.exe", "OkaySpace.exe"}) {
            const std::string bytes = Payload(v, name);
            files[kBucket + "v" + v + "/" + name] = bytes;
            if (!first) entry += ", "; first = false;
            entry += std::string("\"") + name + "\": {\"size\": " + std::to_string(bytes.size()) + ", \"sha256\": \"" + Sha(bytes, scratch / "sha.tmp") + "\"}";
        }
        versions[v] = entry + "}}";
        latest = v;
        Resign(sign);
    }
    void Resign(bool sign = true) {
        files[kBucket + "versions.json"] = IndexJson();
        files[kBucket + "VERSION.txt"] = latest + "\n";
        if (sign) files[kBucket + okayupdate::kSigName] = okayupdate::MakeSignatureFile(files[kBucket + "versions.json"], seed);
        else files.erase(kBucket + okayupdate::kSigName);
    }
    okayversions::FetchTextFn Text() const {
        return [this](const std::string& url, std::string& out) { const auto it = files.find(url); if (it == files.end()) return false; out = it->second; return true; };
    }
    okayversions::DownloadFn Download() const {
        return [this](const std::string& url, const fs::path& dest, std::uint64_t, const std::function<void(std::uint64_t)>&, const std::function<bool()>&) {
            okayversions::DownloadResult r;
            const auto it = files.find(url);
            if (it == files.end()) { r.error = "404"; return r; }
            Spit(dest, it->second);
            r.status = okayversions::DownloadStatus::Ok;
            return r;
        };
    }
    okayupdater::FetchFn Fetch() const {
        return [this](const std::string& url, const fs::path& dest, std::string& err) {
            const auto it = files.find(url);
            if (it == files.end()) { err = "404"; return false; }
            Spit(dest, it->second);
            return true;
        };
    }
};

okayversions::Sources Src(const std::string& keys, const fs::path& floorFile) {
    okayversions::Sources s;
    s.bucketBase = kBucket;
    s.trustedKeys = keys;
    s.floorFile = floorFile;
    return s;
}

// Install `version` from `srv` the way the Versions page does; returns the result.
okayversions::Result TryInstall(const Server& srv, const std::string& keys, const fs::path& floorFile, const fs::path& launcherDir, const std::string& version) {
    const okayversions::Index idx = okayversions::LoadIndex(Src(keys, floorFile), srv.Text());
    const okayversions::InstallPlan plan = okayversions::PlanInstall(idx, version, kBucket, {}, /*requireSdl=*/false);
    if (!plan.ok) { okayversions::Result r; r.outcome = okayversions::Outcome::Refused; r.message = plan.error; return r; }
    okayversions::IO io;
    io.download = srv.Download();
    io.cancelled = [] { return false; };
    io.progress = [](const okayversions::Progress&) {};
    return okayversions::Install(launcherDir, plan, {}, io);
}
}  // namespace

int main() {
    RUN_SUITE("update_security");
    const Seed a = MakeSeed(1), other = MakeSeed(9);
    const std::string keysA = PubHex(a);
    const fs::path root = fs::temp_directory_path() / ("okay_updsec_" + std::to_string(std::rand()));
    std::error_code ec;
    fs::create_directories(root, ec);

    // ---- the status line: OFF is never silent ----
    {
        const auto off = okayupdate::SignatureStatus("");
        CHECK(off.state == okayupdate::SigState::Off && off.line == "update signature check: OFF (no key configured)");
        const auto on = okayupdate::SignatureStatus(keysA);
        CHECK(on.state == okayupdate::SigState::On && on.keys == 1 && on.line == "update signature check: ON (1 key built in)");
        CHECK(okayupdate::SignatureStatus(keysA + "," + PubHex(other)).line == "update signature check: ON (2 keys built in)");
        const auto bad = okayupdate::SignatureStatus("not-a-key");
        CHECK(bad.state == okayupdate::SigState::Broken && bad.line.find("BROKEN") != std::string::npos);
        CHECK(okayupdate::SignatureStatus(okayupdate::BuiltInTrustedKeys()).state == okayupdate::SigState::Off);   // this test binary has no key built in
    }

    // ---- a good signed release installs; every way of damaging it is refused and leaves nothing behind ----
    {
        Server srv; srv.seed = a; srv.scratch = root / "scratch";
        srv.Publish("2.900.0");
        const fs::path floor = root / "t1" / okayupdate::kFloorName;
        {
            const fs::path dir = root / "t1" / "ok";
            const auto r = TryInstall(srv, keysA, floor, dir, "2.900.0");
            CHECK(r.outcome == okayversions::Outcome::Installed);
            CHECK(fs::exists(dir / "Versions" / "2.900.0"));
            CHECK(okayupdate::ReadFloor(floor) == "2.900.0");       // a signed list raised the floor
        }
        const std::string url = kBucket + "v2.900.0/OkayEngine.exe";
        const std::string good = srv.files[url];
        auto expectRefused = [&](const char* what, const std::string& wantInMessage) {
            const fs::path dir = root / "t1" / (std::string("bad_") + what);
            const auto r = TryInstall(srv, keysA, floor, dir, "2.900.0");
            if (r.ok()) std::cerr << "  NOT REFUSED: " << what << "\n";
            CHECK(!r.ok());
            CHECK(wantInMessage.empty() || r.message.find(wantInMessage) != std::string::npos);
            CHECK(!fs::exists(dir / "Versions" / "2.900.0"));         // nothing was put in place
        };
        std::string t = good; t[100] = t[100] == 'x' ? 'y' : 'x';       // same size, one byte changed
        srv.files[url] = t;           expectRefused("flipped_byte", "checksum");
        srv.files[url] = good.substr(0, good.size() / 2);               expectRefused("truncated_file", "");
        srv.files[url] = good + "MZ-extra";                             expectRefused("extended_file", "");
        srv.files[url] = "";                                            expectRefused("empty_file", "");
        srv.files.erase(url);                                           expectRefused("missing_file", "");
        srv.files[url] = good;
        // Mix and match: another real, signed version's file served under this version's address.
        srv.Publish("2.901.0");
        srv.files[kBucket + "v2.900.0/OkayEngine.exe"] = srv.files[kBucket + "v2.901.0/OkayEngine.exe"];
        expectRefused("other_versions_file", "");
        srv.files[url] = good;
        // The list damaged after signing, and signatures that are cut, foreign or from another list.
        const std::string idxUrl = kBucket + "versions.json", sigUrl = kBucket + okayupdate::kSigName;
        const std::string idxGood = srv.files[idxUrl], sigGood = srv.files[sigUrl];
        auto expectListRefused = [&](const char* what) {
            const okayversions::Index idx = okayversions::LoadIndex(Src(keysA, floor), srv.Text());
            if (idx.ok || !idx.signatureRefused) std::cerr << "  LIST NOT REFUSED: " << what << "\n";
            CHECK(!idx.ok && idx.signatureRefused);
            CHECK(!okayversions::PlanInstall(idx, "2.900.0", kBucket, {}, false).ok);
        };
        std::string evil = idxGood; evil.replace(evil.find("2.900.0"), 7, "2.900.1");
        srv.files[idxUrl] = evil;                                       expectListRefused("list_changed_after_signing");
        srv.files[idxUrl] = idxGood;
        for (std::size_t keep : {std::size_t(0), std::size_t(9), std::size_t(10), std::size_t(60), sigGood.size() - 2}) {
            srv.files[sigUrl] = sigGood.substr(0, keep);                expectListRefused("truncated_signature");
        }
        srv.files[sigUrl] = sigGood + "00";                             expectListRefused("signature_too_long");
        srv.files[sigUrl] = okayupdate::MakeSignatureFile(idxGood, other);   expectListRefused("signed_by_another_key");
        srv.files[sigUrl] = okayupdate::MakeSignatureFile("{\"schema\": 1}", a);   expectListRefused("signature_of_another_list");
        srv.files[sigUrl] = "okay-sig1 " + std::string(128, '0') + "\n";     expectListRefused("all_zero_signature");
        srv.files.erase(sigUrl);                                        expectListRefused("signature_missing");
        srv.files[sigUrl] = sigGood;
        CHECK(okayversions::LoadIndex(Src(keysA, floor), srv.Text()).ok);   // and the undamaged server is fine again
    }

    // ---- a replayed older (but really signed) list is refused: the downgrade defence ----
    {
        Server srv; srv.seed = a; srv.scratch = root / "scratch";
        const fs::path floor = root / "t2" / okayupdate::kFloorName;
        srv.Publish("2.900.0");
        const std::string oldIdx = srv.files[kBucket + "versions.json"], oldSig = srv.files[kBucket + okayupdate::kSigName];
        CHECK(okayversions::LoadIndex(Src(keysA, floor), srv.Text()).ok);
        CHECK(okayupdate::ReadFloor(floor) == "2.900.0");
        srv.Publish("2.905.0");
        CHECK(okayversions::LoadIndex(Src(keysA, floor), srv.Text()).ok);
        CHECK(okayupdate::ReadFloor(floor) == "2.905.0");
        // The attacker serves the old list with its old, valid signature (and VERSION.txt of the day).
        srv.files[kBucket + "versions.json"] = oldIdx;
        srv.files[kBucket + okayupdate::kSigName] = oldSig;
        srv.files[kBucket + "VERSION.txt"] = "2.900.0\n";
        const okayversions::Index replay = okayversions::LoadIndex(Src(keysA, floor), srv.Text());
        CHECK(!replay.ok && replay.signatureRefused);
        CHECK(replay.error.find("older") != std::string::npos && replay.error.find("2.900.0") != std::string::npos && replay.error.find("2.905.0") != std::string::npos);
        CHECK(!okayversions::PlanInstall(replay, "2.900.0", kBucket, {}, false).ok);
        CHECK(okayupdate::ReadFloor(floor) == "2.905.0");            // the replay did not lower it
        // The same list is fine for a program that has not seen the newer one (a fresh install): the floor is per program.
        CHECK(okayversions::LoadIndex(Src(keysA, root / "t2" / "fresh_floor.txt"), srv.Text()).ok);
        // A newer genuine list moves on, and an equal one is fine (a re-publish).
        srv.Publish("2.906.0");
        CHECK(okayversions::LoadIndex(Src(keysA, floor), srv.Text()).ok);
        CHECK(okayupdate::ReadFloor(floor) == "2.906.0");
        CHECK(okayversions::LoadIndex(Src(keysA, floor), srv.Text()).ok);
        // The pure rule.
        CHECK(okayupdate::ReplayProblem("", "2.1.0").empty());
        CHECK(okayupdate::ReplayProblem("2.5.0", "2.5.0").empty());
        CHECK(okayupdate::ReplayProblem("2.5.0", "2.4.9").find("older") != std::string::npos);
    }

    // ---- only a list that verified can raise the floor; a damaged floor file is not a reason to refuse ----
    {
        Server srv; srv.seed = a; srv.scratch = root / "scratch";
        srv.Publish("2.950.0");
        const fs::path f1 = root / "t3" / "nokey.txt";
        CHECK(okayversions::LoadIndex(Src("", f1), srv.Text()).ok);                 // no key built in: accepted, as before
        CHECK(!fs::exists(f1));                                                      // ...and an unverified list does not write the floor
        srv.Resign(false);                                                           // unsigned list, key configured: refused
        const fs::path f2 = root / "t3" / "forged.txt";
        CHECK(!okayversions::LoadIndex(Src(keysA, f2), srv.Text()).ok);
        CHECK(!fs::exists(f2));
        srv.files[kBucket + okayupdate::kSigName] = okayupdate::MakeSignatureFile(srv.files[kBucket + "versions.json"], other);
        CHECK(!okayversions::LoadIndex(Src(keysA, f2), srv.Text()).ok);
        CHECK(!fs::exists(f2));                                                      // a forged list cannot set the floor sky-high
        srv.Resign(true);
        const fs::path f3 = root / "t3" / "damaged.txt";
        Spit(f3, "not a version at all\n");
        CHECK(okayversions::LoadIndex(Src(keysA, f3), srv.Text()).ok);
        CHECK(okayupdate::ReadFloor(f3) == "2.950.0");                               // a damaged floor is replaced
        Spit(f3, std::string(5000, '9'));
        CHECK(okayupdate::ReadFloor(f3).empty());
        Spit(root / "t3" / "a_file", "x");
        CHECK(okayversions::LoadIndex(Src(keysA, root / "t3" / "a_file" / "x.txt"), srv.Text()).ok);   // unwritable floor (its folder is a file): still works
    }

    // ---- the self-update path (Stage) refuses a damaged download and leaves the live file alone ----
    {
        Server srv; srv.seed = a; srv.scratch = root / "scratch";
        srv.Publish("2.960.0");
        const okayversions::Index idx = okayversions::LoadIndex(Src(keysA, {}), srv.Text());
        const okayversions::Entry* e = idx.Find("2.960.0");
        CHECK(e != nullptr);
        const okayversions::FileInfo* check = &e->files.at("OkaySpace.exe");
        const std::string url = kBucket + "v2.960.0/OkaySpace.exe";
        const fs::path live = root / "t4" / "OkaySpace.exe";
        Spit(live, "OLD LAUNCHER");
        CHECK(okayupdater::Stage("OkaySpace.exe", url, live, check, srv.Fetch()).ok);
        CHECK(Slurp(live) == "OLD LAUNCHER");                                        // staged beside it, not swapped
        okayupdater::Discard(live);
        const std::string good = srv.files[url];
        std::string t = good; t.back() = t.back() == 'q' ? 'r' : 'q';
        srv.files[url] = t;
        auto o = okayupdater::Stage("OkaySpace.exe", url, live, check, srv.Fetch());
        CHECK(!o.ok && o.reason.find("not the published file") != std::string::npos);
        CHECK(!fs::exists(fs::path(live) += ".new") && Slurp(live) == "OLD LAUNCHER");
        srv.files[url] = good.substr(0, good.size() - 7);
        CHECK(!okayupdater::Stage("OkaySpace.exe", url, live, check, srv.Fetch()).ok);
        srv.files[url] = srv.files[kBucket + "v2.960.0/OkayEngine.exe"];             // the editor's bytes offered as the launcher
        CHECK(!okayupdater::Stage("OkaySpace.exe", url, live, check, srv.Fetch()).ok);
        CHECK(!okayupdater::Stage("OkaySpace.exe", url, live, nullptr, srv.Fetch()).ok);   // no checksum in the list: refused outright
        CHECK(Slurp(live) == "OLD LAUNCHER");
    }

    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
