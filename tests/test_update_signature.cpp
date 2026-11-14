// Signed releases (shared/UpdateSignature.hpp, launcher/VersionsManager.cpp LoadIndex, launcher/UpdaterCore.hpp): versions.json counts only
// when it verifies under a trusted key. Throwaway keys made here; no network (a fake fetch function); the publish tool
// (scripts/sign_index.py) is run when python3 is there and its signature is checked by the C++ verifier (the two must agree).
#include "test_framework.hpp"
#include "VersionsManager.hpp"
#include "UpdaterCore.hpp"
#include "UpdateSignature.hpp"

#include <cstdio>
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
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = (std::uint8_t)(tag * 31u + i * 7u + 1u);
    return s;
}
std::string PubHex(const Seed& s) { const PublicKey p = okayac::ed25519::PublicKeyFromSeed(s); return okayac::ToHex(p.data(), p.size()); }

const char* kBucket = "https://example.test/storage/v1/object/public/releases/";
const std::string kSha(64, 'a');
std::string IndexBody(const std::string& ver = "2.900.0") {
    return "{\"schema\": 1, \"latest\": \"" + ver + "\", \"lts\": \"\", \"archive\": \"\", \"bucketPath\": \"v{version}/{file}\", \"versions\": [{\"version\": \"" +
           ver + "\", \"channel\": \"latest\", \"date\": \"2026-11-10\", \"inBucket\": true, \"files\": {"
           "\"OkayEngine.exe\": {\"size\": 10, \"sha256\": \"" + kSha + "\"}, \"OkaySpacePlayer.exe\": {\"size\": 10, \"sha256\": \"" + kSha +
           "\"}, \"OkaySpace.exe\": {\"size\": 10, \"sha256\": \"" + kSha + "\"}}}]}";
}
okayversions::FetchTextFn FakeServer(std::map<std::string, std::string> files) {
    return [files](const std::string& url, std::string& out) {
        const auto it = files.find(url);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    };
}
okayversions::Sources Src(const std::string& keys) {
    okayversions::Sources s;
    s.bucketBase = kBucket;
    s.trustedKeys = keys;
    return s;
}
std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
}  // namespace

int main() {
    RUN_SUITE("update_signature");
    const Seed a = MakeSeed(1), b = MakeSeed(2), other = MakeSeed(3);
    const std::string body = IndexBody();
    const std::string sigA = okayupdate::MakeSignatureFile(body, a);
    const std::string keysA = PubHex(a), keysAB = PubHex(a) + "," + PubHex(b), keysB = PubHex(b);

    // ---- the check itself ---------------------------------------------------------------------------------------------------
    {
        CHECK(okayupdate::Check("", body, "").kind == okayupdate::Kind::NotRequired);          // no key built in: the transition
        CHECK(okayupdate::Check("", body, "").Accepts());
        const auto ok = okayupdate::Check(keysA, body, sigA);
        CHECK(ok.kind == okayupdate::Kind::Valid && ok.keyIndex == 0 && ok.Accepts() && ok.message.empty());
        CHECK(okayupdate::Check(keysA, body, "").kind == okayupdate::Kind::Missing);
        CHECK(!okayupdate::Check(keysA, body, "").Accepts());
        CHECK(okayupdate::Check(keysA, body + " ", sigA).kind == okayupdate::Kind::Bad);        // one byte more
        CHECK(okayupdate::Check(keysA, "x" + body, sigA).kind == okayupdate::Kind::Bad);
        CHECK(okayupdate::Check(keysB, body, sigA).kind == okayupdate::Kind::Bad);              // signed by another key
        CHECK(okayupdate::Check(keysA, body, okayupdate::MakeSignatureFile(body, other)).kind == okayupdate::Kind::Bad);
        // Rotation: two trusted keys, either may have signed.
        CHECK(okayupdate::Check(keysAB, body, sigA).keyIndex == 0);
        CHECK(okayupdate::Check(keysAB, body, okayupdate::MakeSignatureFile(body, b)).keyIndex == 1);
        CHECK(!okayupdate::Check(keysAB, body, okayupdate::MakeSignatureFile(body, other)).Accepts());
        // Domain separation: a signature over the bare bytes (what another use of the key would make) is not a release signature.
        {
            const auto raw = okayac::ed25519::Sign(body, a);
            const std::string fake = std::string("okay-sig1 ") + okayac::ToHex(raw.data(), raw.size());
            CHECK(okayupdate::Check(keysA, body, fake).kind == okayupdate::Kind::Bad);
        }
        // Malformed signature files are "Bad", never a crash or an accept.
        for (const char* junk : {"garbage", "okay-sig1", "okay-sig1 zz", "okay-sig2 00", "\x01\x02", "okay-sig1 "}) CHECK(okayupdate::Check(keysA, body, junk).kind == okayupdate::Kind::Bad);
        CHECK(okayupdate::Check(keysA, body, std::string(5000, 'a')).kind == okayupdate::Kind::Bad);
        std::string flipped = sigA;
        flipped[12] = flipped[12] == '0' ? '1' : '0';
        CHECK(okayupdate::Check(keysA, body, flipped).kind == okayupdate::Kind::Bad);
        CHECK(okayupdate::Check(keysA, body, "  " + sigA + "\r\n").Accepts());                  // surrounding whitespace is fine
        // A bad baked key list refuses everything: never "no keys, so accept".
        for (const char* badKeys : {"zz", "abcd", "0000000000000000000000000000000000000000000000000000000000000000", ",", "ab,cd"})
            CHECK(okayupdate::Check(badKeys, body, sigA).kind == okayupdate::Kind::KeysInvalid);
        std::vector<PublicKey> parsed;
        CHECK(!okayupdate::ParseKeys(keysA + "," + keysB + "," + keysA + "," + keysB + "," + keysA, parsed));   // more than four
        CHECK(okayupdate::ParseKeys(keysAB, parsed) && parsed.size() == 2);
    }

    // ---- LoadIndex: the real loader with a fake release server --------------------------------------------------------------
    {
        const std::string base = kBucket;
        auto serve = [&](const std::string& json, const std::string* sig) {
            std::map<std::string, std::string> f{{base + "versions.json", json}, {base + "VERSION.txt", "2.900.0\n"}};
            if (sig) f[base + okayupdate::kSigName] = *sig;
            return FakeServer(f);
        };
        const std::string sigB = okayupdate::MakeSignatureFile(body, b);
        auto idx = okayversions::LoadIndex(Src(keysA), serve(body, &sigA));
        CHECK(idx.ok && idx.signedByPublisher && !idx.signatureRefused && !idx.fallback);
        CHECK(okayupdater::IndexProblem(idx, idx.Find("2.900.0"), "2.900.0").empty());

        idx = okayversions::LoadIndex(Src(keysA), serve(body, nullptr));                          // no .sig file
        CHECK(!idx.ok && idx.signatureRefused && !idx.fallback);
        CHECK(idx.error.find("not signed") != std::string::npos);
        CHECK(okayupdater::IndexProblem(idx, nullptr, "2.900.0").find("Update refused") == 0);

        std::string evil = body;
        evil.replace(evil.find(kSha), 64, std::string(64, 'b'));                                   // a changed checksum, the old signature
        idx = okayversions::LoadIndex(Src(keysA), serve(evil, &sigA));
        CHECK(!idx.ok && idx.signatureRefused && idx.Find("2.900.0") == nullptr);
        CHECK(!okayupdater::IndexProblem(idx, nullptr, "2.900.0").empty());
        // The attacker's own key does not help.
        const std::string evilSig = okayupdate::MakeSignatureFile(evil, other);
        idx = okayversions::LoadIndex(Src(keysA), serve(evil, &evilSig));
        CHECK(!idx.ok && idx.signatureRefused);
        // Rotation: signed by the second key while both are trusted; a build that only knows the old key refuses.
        idx = okayversions::LoadIndex(Src(keysAB), serve(body, &sigB));
        CHECK(idx.ok && idx.signedByPublisher);
        idx = okayversions::LoadIndex(Src(keysA), serve(body, &sigB));
        CHECK(!idx.ok && idx.signatureRefused);
        // Unusable baked keys refuse too.
        idx = okayversions::LoadIndex(Src("nonsense"), serve(body, &sigA));
        CHECK(!idx.ok && idx.signatureRefused && idx.error.find("unusable update key") != std::string::npos);
        // A refusal never falls back to the unsigned heads (VERSION.txt): the fallback index would offer a path around the signature.
        CHECK(!idx.fallback && idx.versions.empty());
        // Transition: no key built in, the same unsigned feed works exactly as before, signed or not.
        idx = okayversions::LoadIndex(Src(""), serve(body, nullptr));
        CHECK(idx.ok && !idx.signedByPublisher && !idx.signatureRefused);
        idx = okayversions::LoadIndex(Src(""), serve(evil, &sigA));
        CHECK(idx.ok && !idx.signedByPublisher);
        // Signature present but versions.json unreachable: the old fallback behaviour (no checksums, installs refused).
        idx = okayversions::LoadIndex(Src(keysA), FakeServer({{base + "VERSION.txt", "2.900.0\n"}}));
        CHECK(idx.fallback);
        CHECK(!okayupdater::IndexProblem(idx, nullptr, "2.900.0").empty());
        // Default Sources take the keys built into the program (none in this test binary).
        CHECK(okayversions::Sources().trustedKeys == okayupdate::BuiltInTrustedKeys());
    }

    // ---- the publish tool agrees with the C++ verifier ---------------------------------------------------------------------
    {
        const fs::path dir = fs::temp_directory_path() / ("okay_updsig_" + std::to_string(std::rand()));
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path jf = dir / "versions.json", sf = dir / "versions.json.sig";
        { std::ofstream(jf, std::ios::binary) << body; }
        const std::string script = std::string(OKAY_SOURCE_DIR) + "/scripts/sign_index.py";
        // The seed is passed in the environment of the child only, as the publish step does.
        const std::string cmd =
#ifdef _WIN32
            "set OKAY_UPDATE_SIGNING_SEED=" + okayac::ToHex(a.data(), a.size()) + "&& python3 \"" + script + "\" sign --file \"" + jf.string() + "\" --out \"" + sf.string() + "\" >NUL 2>&1";
#else
            "OKAY_UPDATE_SIGNING_SEED=" + okayac::ToHex(a.data(), a.size()) + " python3 -I '" + script + "' sign --file '" + jf.string() + "' --out '" + sf.string() + "' >/dev/null 2>&1";
#endif
        const int rc = std::system(cmd.c_str());
        if (rc != 0 || !fs::exists(sf)) {
            std::cout << "  (python3 not usable here: the publish-tool cross check is skipped)\n";
        } else {
            const std::string made = Slurp(sf);
            CHECK(okayupdate::Check(keysA, body, made).kind == okayupdate::Kind::Valid);           // python signed, C++ verified
            CHECK(made == sigA);                                                                  // ed25519 is deterministic: same bytes
            CHECK(okayupdate::Check(keysA, body + "x", made).kind == okayupdate::Kind::Bad);
        }
        fs::remove_all(dir, ec);
    }
    TEST_MAIN_RESULT();
}
