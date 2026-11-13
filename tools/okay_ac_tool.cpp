// okay_ac_tool: the anti-cheat build tool (docs/anticheat.md, "Hardening: code and import integrity").
//
//   okay_ac_tool baseline --build <id> --out <Data/codebaseline.json> <game.exe> [<game.dll> ...]
//       Hashes the code sections of each file (relocation-aware, the way the player does at run
//       time) and writes a baseline, signed when OKAY_AC_SIGNING_KEY_FILE / OKAY_AC_SIGNING_KEY is
//       set (the same key as the build manifest). Run it after the link, BEFORE File > Build Game
//       writes the manifest, so the manifest lists the baseline too.
//   okay_ac_tool sign-rules <rules.json> <out.json>
//       Signs a module rules file (okayspace-modules/1). The result goes to .anticheat/modules.json
//       next to the game; it is not part of the manifest, so it can be replaced without a rebuild.
//   okay_ac_tool verify <file> [--trusted <public key hex>]
//       Checks a baseline or rules file: format, signature, and (with --trusted) the signer.
//
// Never prints key material; a malformed key is an error, not a silently unsigned file.
#include <cstdio>
#include <cstring>
#include <iostream>

#include "AntiCheatHardening.hpp"

namespace {
using namespace okayac;

int Fail(const std::string& m) { std::fprintf(stderr, "okay_ac_tool: %s\n", m.c_str()); return 1; }

bool WriteFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary);
    o << text;
    return (bool)o;
}

int Baseline(int argc, char** argv) {
    std::string build, out;
    std::vector<fs::path> files;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--build" && i + 1 < argc) build = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a.rfind("--", 0) == 0) return Fail("unknown option " + a);
        else files.push_back(fs::u8path(a));
    }
    if (build.empty() || out.empty() || files.empty()) return Fail("usage: baseline --build <id> --out <file> <exe/dll>...");
    pe::CodeBaseline b;
    b.build = build;
    for (const fs::path& f : files) {
        pe::ModuleBaseline m;
        std::string err;
        if (!pe::BaselineForFile(f, m, &err)) return Fail(f.string() + ": " + err);
        std::printf("  %s:", m.name.c_str());
        for (const auto& s : m.sections) std::printf(" %s(%u bytes, %.12s...)", s.name.c_str(), s.size, s.sha256.c_str());
        std::printf("\n");
        b.modules.push_back(std::move(m));
    }
    SigningKey key = LoadSigningKey();
    if (!key.error.empty()) return Fail(key.error);
    if (key.present) { pe::Sign(b, key.seed); key.Clear(); }
    if (!WriteFile(fs::u8path(out), pe::ToJson(b))) return Fail("can't write " + out);
    std::printf("wrote %s (%s)\n", out.c_str(), b.hasSignature ? "signed" : "UNSIGNED: no signing key set; a player with trusted keys will refuse it");
    return 0;
}

int SignRules(int argc, char** argv) {
    if (argc != 4) return Fail("usage: sign-rules <rules.json> <out.json>");
    const std::string text = ReadTextFile(fs::u8path(argv[2]));
    ModuleRules r;
    std::string err;
    if (text.empty() || !ParseModuleRules(text, r, &err)) return Fail(std::string("rules: ") + (text.empty() ? "can't read the file" : err));
    SigningKey key = LoadSigningKey();
    if (!key.error.empty()) return Fail(key.error);
    if (!key.present) return Fail("no signing key: set OKAY_AC_SIGNING_KEY_FILE or OKAY_AC_SIGNING_KEY");
    Sign(r, key.seed);
    key.Clear();
    if (!WriteFile(fs::u8path(argv[3]), ToJson(r))) return Fail(std::string("can't write ") + argv[3]);
    std::printf("wrote %s (version %ld, %zu block, %zu allow, signed)\n", argv[3], r.version, r.block.size(), r.allow.size());
    return 0;
}

int Verify(int argc, char** argv) {
    if (argc < 3) return Fail("usage: verify <file> [--trusted <hex>]");
    std::string trustedHex;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--trusted") && i + 1 < argc) trustedHex = argv[++i];
    const std::string text = ReadTextFile(fs::u8path(argv[2]));
    if (text.empty()) return Fail("can't read the file");
    ed25519::PublicKey key{};
    bool signedOk = false;
    std::string kind, err;
    pe::CodeBaseline b;
    ModuleRules r;
    if (pe::ParseCodeBaseline(text, b, &err)) { kind = "code baseline"; signedOk = pe::CheckSignature(b); key = b.key; }
    else if (ParseModuleRules(text, r, &err)) { kind = "module rules"; signedOk = CheckSignature(r); key = r.key; }
    else return Fail("not a baseline or rules file: " + err);
    std::printf("%s: signature %s\n", kind.c_str(), signedOk ? "valid" : "MISSING or INVALID");
    if (!trustedHex.empty()) {
        std::vector<ed25519::PublicKey> keys;
        if (!ParseKeyList(trustedHex, keys)) return Fail("--trusted isn't a valid public key");
        const bool trusted = signedOk && std::find(keys.begin(), keys.end(), key) != keys.end();
        std::printf("signed by the trusted key: %s\n", trusted ? "yes" : "NO");
        return trusted ? 0 : 2;
    }
    return signedOk ? 0 : 2;
}
} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string c = argv[1];
        if (c == "baseline") return Baseline(argc, argv);
        if (c == "sign-rules") return SignRules(argc, argv);
        if (c == "verify") return Verify(argc, argv);
    }
    std::fprintf(stderr, "usage: okay_ac_tool baseline|sign-rules|verify ... (see docs/anticheat.md)\n");
    return 1;
}
