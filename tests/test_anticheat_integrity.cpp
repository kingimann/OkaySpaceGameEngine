#include "test_framework.hpp"
#include "AntiCheatIntegrity.hpp"
#include "okay/Core/DataPack.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

// Anti-cheat P3: the build's integrity manifest (Data/manifest.json), its
// ed25519 signature, and what the launcher / player do with it before a launch.
// Every key here is a throwaway made at runtime; no real key is ever used.
namespace {
namespace fs = std::filesystem;
using namespace okayac;

void Put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string Get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}
std::vector<std::uint8_t> Hex(const std::string& h) {
    std::vector<std::uint8_t> v;
    FromHex(h, v);
    return v;
}
template <std::size_t N> std::array<std::uint8_t, N> Arr(const std::string& h) {
    std::array<std::uint8_t, N> a{};
    const auto v = Hex(h);
    for (std::size_t i = 0; i < N && i < v.size(); ++i) a[i] = v[i];
    return a;
}

// A throwaway seed from the OS RNG + the clock: made for this run, never stored.
ed25519::Seed ThrowawaySeed() {
    std::random_device rd;
    std::mt19937_64 g(((std::uint64_t)rd() << 32) ^ rd() ^
                      (std::uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    ed25519::Seed s{};
    for (auto& b : s) b = (std::uint8_t)(g() & 0xff);
    return s;
}

fs::path TempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("okay_ac_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

// A small built game: exe + DLL at the root, scenes / config / a pak in Data/.
fs::path MakeGame(const char* name) {
    const fs::path root = TempDir(name);
    Put(root / "My Game.exe", "MZ fake game exe");
    Put(root / "SDL2.dll", "MZ fake sdl");
    Put(root / "Data/game.okayscene", "{\"objects\": []}");
    Put(root / "Data/level2.okayscene", okay::DataPack::Pack("{\"objects\": [1]}"));
    Put(root / "Data/game.okayconfig", "title=My Game\nanticheat=enforce\n");
    Put(root / "Data/assets.pak", std::string("PAK\0\x01\x02\x03", 7));
    Put(root / "Data/textures/hero.png", "png bytes");
    return root;
}

void TestSha512() {
    RUN_SUITE("SHA-512 (ed25519's hash, portable)");
    const auto d0 = sha512::Hash(nullptr, 0);
    CHECK(ToHex(d0.data(), d0.size()) ==
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
          "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    const std::string abc = "abc";
    const auto d1 = sha512::Hash((const std::uint8_t*)abc.data(), abc.size());
    CHECK(ToHex(d1.data(), d1.size()) ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    // Two blocks (112+ bytes forces the length into a second block).
    const std::string two(200, 'a');
    const auto d2 = sha512::Hash((const std::uint8_t*)two.data(), two.size());
    CHECK(ToHex(d2.data(), d2.size()) ==
          "4b11459c33f52a22ee8236782714c150a3b2c60994e9acee17fe68947a3e6789"
          "f31e7668394592da7bef827cddca88c4e6f86e4df7ed1ae6cba71f3e98faee9f");
}

void TestHex() {
    RUN_SUITE("hex");
    std::vector<std::uint8_t> v;
    CHECK(FromHex("00ff10Ab", v) && v.size() == 4 && v[1] == 0xff && v[3] == 0xab);
    CHECK(FromHex("  00ff\n", v) && v.size() == 2);   // surrounding whitespace (a key file's newline)
    CHECK(!FromHex("0", v) && !FromHex("zz", v) && !FromHex("00 ff", v));
    const std::uint8_t b[3] = {1, 0xab, 0xff};
    CHECK(ToHex(b, 3) == "01abff");
}

// RFC 8032 section 7.1, tests 1-3: the published keys and signatures (public
// test vectors, not anyone's key).
void TestRfc8032() {
    RUN_SUITE("ed25519 RFC 8032 vectors");
    struct V { const char* seed; const char* pk; const char* msg; const char* sig; };
    const V vs[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    for (const V& v : vs) {
        const auto seed = Arr<32>(v.seed);
        const auto msg = Hex(v.msg);
        const auto want = Arr<64>(v.sig);
        const auto pk = Arr<32>(v.pk);
        // The portable code is checked on every build (it's what the launcher and
        // a no-sodium build use)...
        CHECK(ed25519::portable::PublicKeyFromSeed(seed) == pk);
        CHECK(ed25519::portable::Sign(msg.data(), msg.size(), seed) == want);
        CHECK(ed25519::portable::Verify(msg.data(), msg.size(), want, pk));
        // ...and the backend in use (libsodium when the build has it) agrees.
        CHECK(ed25519::PublicKeyFromSeed(seed) == pk);
        CHECK(ed25519::Sign(msg.data(), msg.size(), seed) == want);
        CHECK(ed25519::Verify(msg.data(), msg.size(), want, pk));
        auto bad = want; bad[5] ^= 1;
        CHECK(!ed25519::Verify(msg.data(), msg.size(), bad, pk));
        CHECK(!ed25519::portable::Verify(msg.data(), msg.size(), bad, pk));
    }
    std::cout << "  ed25519 backend: " << ed25519::Backend() << "\n";
}

void TestThrowawayKeys() {
    RUN_SUITE("ed25519 with a throwaway runtime keypair");
    ed25519::Seed seed = ThrowawaySeed();
    const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(seed);
    const std::string msg = "the manifest body";
    const ed25519::Signature sig = ed25519::Sign(msg, seed);
    CHECK(ed25519::Verify(msg, sig, pk));
    CHECK(!ed25519::Verify(msg + " ", sig, pk));                 // other bytes
    ed25519::Seed other = ThrowawaySeed();
    CHECK(!ed25519::Verify(msg, sig, ed25519::PublicKeyFromSeed(other)));   // other key
    // A non-canonical S (S + L) must not pass: no malleable signatures.
    ed25519::Signature mal = sig;
    static const std::uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                       0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
    unsigned carry = 0;
    for (int i = 0; i < 32; ++i) { carry += (unsigned)mal[32 + i] + L[i]; mal[32 + i] = (std::uint8_t)carry; carry >>= 8; }
    CHECK(!ed25519::Verify(msg, mal, pk));
    CHECK(!ed25519::portable::Verify((const std::uint8_t*)msg.data(), msg.size(), mal, pk));
    Wipe(seed.data(), seed.size());
    Wipe(other.data(), other.size());
    bool zero = true;
    for (auto b : seed) zero = zero && b == 0;
    CHECK(zero);
}

void TestModes() {
    RUN_SUITE("anti-cheat modes");
    CHECK(ParseMode("") == Mode::Off && ParseMode("off") == Mode::Off && ParseMode("0") == Mode::Off);
    CHECK(ParseMode("report") == Mode::Report && ParseMode("1") == Mode::Report);
    CHECK(ParseMode("Enforce") == Mode::Enforce && ParseMode("2") == Mode::Enforce);
    CHECK(ParseMode("nonsense") == Mode::Off);   // unknown never turns enforcement on
    CHECK(std::string(ModeName(Mode::Report)) == "report");
    CHECK(Stricter(Mode::Report, Mode::Enforce) == Mode::Enforce && Stricter(Mode::Report, Mode::Off) == Mode::Report);
}

void TestManifestRoundTrip() {
    RUN_SUITE("integrity manifest round trip");
    const fs::path root = MakeGame("roundtrip");
    std::vector<okayofficial::File> files;
    std::string err;
    CHECK(ScanForManifest(root, files, &err));
    CHECK(files.size() == 7);
    CHECK(files.front().path == "Data/assets.pak");                 // sorted bytewise
    bool sawExe = false;
    for (const auto& f : files) {
        CHECK(f.sha256.size() == 64);
        if (f.path == "My Game.exe") sawExe = f.size == 16 && f.sha256 == okayofficial::FileSha256(root / "My Game.exe");
    }
    CHECK(sawExe);

    Integrity m;
    m.game = "My Game \"quoted\""; m.version = "1.2.0"; m.files = files;
    const std::string text = ToJson(m);
    Integrity back;
    CHECK(ParseIntegrity(text, back, &err));
    CHECK(back.game == m.game && back.version == "1.2.0" && back.files.size() == files.size() && !back.hasSignature);
    for (std::size_t i = 0; i < files.size(); ++i)
        CHECK(back.files[i].path == files[i].path && back.files[i].size == files[i].size && back.files[i].sha256 == files[i].sha256);
    CHECK(ToJson(back) == text);
    CHECK(SignedPayload(back) == SignedPayload(m));

    // Untrusted input: malformed / unsafe manifests are refused.
    CHECK(!ParseIntegrity("not json", back));
    CHECK(!ParseIntegrity("{\"format\": \"other/9\", \"files\": []}", back));
    std::string unsafe = text;
    unsafe.replace(unsafe.find("Data/assets.pak"), 15, "../outside.pak");
    CHECK(!ParseIntegrity(unsafe, back, &err));
    std::string badHash = text;
    badHash.replace(badHash.find("\"sha256\": \"") + 11, 1, "Z");
    CHECK(!ParseIntegrity(badHash, back));
}

void TestWriteAndVerify() {
    RUN_SUITE("build writes Data/manifest.json; a clean install verifies");
    const fs::path root = MakeGame("clean");
    const BuildManifestResult w = WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{});
    CHECK(w.ok && !w.isSigned && w.files == 7);
    CHECK(fs::exists(root / "Data/manifest.json"));
    // The manifest never lists itself, and a rebuild is stable.
    CHECK(Get(root / "Data/manifest.json").find("Data/manifest.json") == std::string::npos);
    const std::string first = Get(root / "Data/manifest.json");
    CHECK(WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{}).ok && Get(root / "Data/manifest.json") == first);

    const VerifyResult r = VerifyInstall(root, {});
    CHECK(r.ran && r.ok && r.problems.empty() && r.filesChecked == 7);
    CHECK(!r.signatureValid);   // unsigned: a note, not a problem, when no key is trusted
    CHECK(!r.notes.empty());
    // Files the game writes itself (saves, logs) aren't the manifest's business.
    Put(root / "Data/save.okaysave", "save");
    CHECK(VerifyInstall(root, {}).ok);
}

void TestTamper() {
    RUN_SUITE("a tampered pak or scene fails, and enforcement refuses the launch");
    for (const char* victim : {"Data/assets.pak", "Data/game.okayscene", "Data/level2.okayscene", "SDL2.dll"}) {
        const fs::path root = MakeGame("tamper");
        CHECK(WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{}).ok);
        std::string body = Get(root / victim);
        body[body.size() / 2] ^= 0x20;   // same size, one byte changed
        Put(root / victim, body);
        const VerifyResult r = VerifyInstall(root, {});
        CHECK(r.ran && !r.ok);
        bool named = false;
        for (const auto& p : r.problems) named = named || p.find(victim) != std::string::npos;
        CHECK(named);
        // Enforcing: refused. Reporting: allowed, with the problem. Off: not even looked at.
        const LaunchDecision enforce = CheckLaunch(root, Mode::Enforce, {});
        CHECK(!enforce.allow && !enforce.message.empty());
        const LaunchDecision report = CheckLaunch(root, Mode::Report, {});
        CHECK(report.allow && !report.result.ok && !report.result.problems.empty());
        const LaunchDecision off = CheckLaunch(root, Mode::Off, {});
        CHECK(off.allow && !off.result.ran);
    }
    {   // a deleted file
        const fs::path root = MakeGame("deleted");
        CHECK(WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{}).ok);
        fs::remove(root / "Data/textures/hero.png");
        CHECK(!VerifyInstall(root, {}).ok && !CheckLaunch(root, Mode::Enforce, {}).allow);
    }
    {   // a DLL dropped beside the exe (proxy-DLL injection) is a problem; it isn't listed
        const fs::path root = MakeGame("dropped");
        CHECK(WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{}).ok);
        Put(root / "version.dll", "MZ injected");
        const VerifyResult r = VerifyInstall(root, {});
        CHECK(!r.ok);
    }
    {   // no manifest at all: enforcement refuses, report mode lets it run
        const fs::path root = MakeGame("nomanifest");
        CHECK(!CheckLaunch(root, Mode::Enforce, {}).allow);
        CHECK(CheckLaunch(root, Mode::Report, {}).allow);
        CHECK(CheckLaunch(root, Mode::Off, {}).allow);
    }
}

void TestSignedManifest() {
    RUN_SUITE("signed manifests (throwaway keys)");
    ed25519::Seed seed = ThrowawaySeed();
    const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(seed);
    SigningKey key;
    key.present = true; key.seed = seed; key.source = "test";
    const fs::path root = MakeGame("signed");
    const BuildManifestResult w = WriteBuildManifest(root, "My Game", "1.0.0", key);
    CHECK(w.ok && w.isSigned);

    Integrity m;
    CHECK(ParseIntegrity(Get(root / "Data/manifest.json"), m));
    CHECK(m.hasSignature && m.key == pk && CheckSignature(m));

    // Trusted key: verified. No trusted key: still checked against the embedded one.
    VerifyResult r = VerifyInstall(root, {pk});
    CHECK(r.ok && r.signatureValid && r.trustedSigner);
    r = VerifyInstall(root, {});
    CHECK(r.ok && r.signatureValid && !r.trustedSigner);

    // Signed by a key the build doesn't trust: refused when enforcing.
    ed25519::Seed otherSeed = ThrowawaySeed();
    const ed25519::PublicKey other = ed25519::PublicKeyFromSeed(otherSeed);
    r = VerifyInstall(root, {other});
    CHECK(!r.ok && !r.trustedSigner);
    CHECK(!CheckLaunch(root, Mode::Enforce, {other}).allow);

    // Editing the manifest (to match a modded file) breaks the signature.
    {
        std::string body = Get(root / "Data/assets.pak");
        body[0] = 'X';
        Put(root / "Data/assets.pak", body);
        Integrity edited = m;
        for (auto& f : edited.files)
            if (f.path == "Data/assets.pak") f.sha256 = okayofficial::FileSha256(root / "Data/assets.pak");
        Put(root / "Data/manifest.json", ToJson(edited));   // keeps the old signature
        CHECK(!CheckSignature(edited));
        r = VerifyInstall(root, {pk});
        CHECK(!r.ok && !r.signatureValid);
        CHECK(!CheckLaunch(root, Mode::Enforce, {pk}).allow);
        CHECK(!VerifyInstall(root, {}).ok);   // a bad signature fails even with no trusted key
    }
    // Stripping the signature doesn't help when the build trusts a key.
    {
        const fs::path r2 = MakeGame("stripped");
        CHECK(WriteBuildManifest(r2, "My Game", "1.0.0", SigningKey{}).ok);
        CHECK(!VerifyInstall(r2, {pk}).ok);
        CHECK(VerifyInstall(r2, {}).ok);
    }
    Wipe(seed.data(), seed.size()); Wipe(otherSeed.data(), otherSeed.size()); Wipe(key.seed.data(), key.seed.size());
}

void TestKeyLoading() {
    RUN_SUITE("signing key + trusted keys plug in from the environment");
    ed25519::Seed seed = ThrowawaySeed();
    const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(seed);
    const std::string seedHex = ToHex(seed.data(), seed.size());
    const std::string pkHex = ToHex(pk.data(), pk.size());

    std::map<std::string, std::string> env;
    auto getenv = [&](const char* k) -> const char* {
        auto it = env.find(k);
        return it == env.end() ? nullptr : it->second.c_str();
    };
    CHECK(!LoadSigningKey(getenv).present);   // nothing set: unsigned, no error
    CHECK(LoadSigningKey(getenv).error.empty());

    // A key file (hex seed, optional newline).
    const fs::path dir = TempDir("keys");
    Put(dir / "dev.key", seedHex + "\n");
    env["OKAY_AC_SIGNING_KEY_FILE"] = (dir / "dev.key").string();
    SigningKey k = LoadSigningKey(getenv);
    CHECK(k.present && k.seed == seed && k.error.empty());
    // libsodium's 64-byte secret key (seed || public key) works too.
    Put(dir / "dev.key", seedHex + pkHex);
    k = LoadSigningKey(getenv);
    CHECK(k.present && k.seed == seed);
    // ...but not one whose public half doesn't match.
    Put(dir / "dev.key", seedHex + std::string(64, '0'));
    k = LoadSigningKey(getenv);
    CHECK(!k.present && !k.error.empty());
    Put(dir / "dev.key", "garbage");
    CHECK(!LoadSigningKey(getenv).present && !LoadSigningKey(getenv).error.empty());
    env["OKAY_AC_SIGNING_KEY_FILE"] = (dir / "missing.key").string();
    CHECK(!LoadSigningKey(getenv).present && !LoadSigningKey(getenv).error.empty());
    env.erase("OKAY_AC_SIGNING_KEY_FILE");
    env["OKAY_AC_SIGNING_KEY"] = seedHex;
    CHECK(LoadSigningKey(getenv).present);
    // Error text never echoes key material.
    env["OKAY_AC_SIGNING_KEY"] = seedHex.substr(0, 60) + "zz";
    k = LoadSigningKey(getenv);
    CHECK(!k.present && k.error.find(seedHex.substr(0, 16)) == std::string::npos);
    env.clear();

    // Trusted public keys: comma / space separated hex.
    std::vector<ed25519::PublicKey> keys;
    CHECK(ParseKeyList(pkHex + ", " + std::string(64, 'a'), keys) && keys.size() == 2 && keys[0] == pk);
    CHECK(!ParseKeyList("abcd", keys));
    env["OKAY_AC_TRUSTED_KEY"] = pkHex;
    keys = TrustedKeys(getenv, "");          // no key built in: the env var (dev) is used
    CHECK(keys.size() == 1 && keys[0] == pk);
    keys = TrustedKeys(getenv, std::string(64, 'b'));   // a key built in: the env var can't add one
    CHECK(keys.size() == 1 && keys[0] != pk);
    Wipe(seed.data(), seed.size()); Wipe(k.seed.data(), k.seed.size());
}

void TestOfficialFolderIgnored() {
    RUN_SUITE("the launcher's own .okayofficial folder isn't part of the game");
    const fs::path root = MakeGame("official");
    CHECK(WriteBuildManifest(root, "My Game", "1.0.0", SigningKey{}).ok);
    Put(root / ".okayofficial/manifest.json", "{}");
    Put(root / ".okayofficial/staging/x.dll", "MZ");
    CHECK(VerifyInstall(root, {}).ok);
}
} // namespace

int main() {
    TestSha512();
    TestHex();
    TestRfc8032();
    TestThrowawayKeys();
    TestModes();
    TestManifestRoundTrip();
    TestWriteAndVerify();
    TestTamper();
    TestSignedManifest();
    TestKeyLoading();
    TestOfficialFolderIgnored();
    TEST_MAIN_RESULT();
}
