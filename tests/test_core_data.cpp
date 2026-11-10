// Core wave 6, 1.14 Data assets and data packs: audit fixes and additions.
//   - DataAsset: a UTF-8 byte order mark no longer hides the first key, "nan" / "inf" / "1e999" are not numbers a
//     game can use, GetInt / GetBool / TryGetNumber, Invalidate keeps references valid, multi-line values round trip,
//     a file created after a failed Cached() is picked up.
//   - DataPack: empty, binary and large data round trip; every truncation and every flipped byte of a pack is
//     reported as damaged (never a crash, never "plain text"); a pack cut inside its header is recognised as a pack.
#include "test_framework.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Core/DataPack.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void PutFile(const std::string& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

int main() {
    RUN_SUITE("core_data");

    const fs::path dir = fs::temp_directory_path() / ("okay_core_data_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path oldCwd = fs::current_path();
    fs::current_path(dir);

    // --- DataAsset: byte order mark ---
    {
        PutFile("bom.okaydata", "\xEF\xBB\xBF" "name = Goblin\nhp = 30\n");
        DataAsset a;
        CHECK(a.Load("bom.okaydata"));
        CHECK(a.Has("name"));                    // the first key used to be stored as "<BOM>name"
        CHECK(a.GetString("name") == "Goblin");
        CHECK(a.GetNumber("hp") == 30.0);
        CHECK(a.Fields().size() == 2);
        // A BOM in front of the header line of a saved file too.
        DataAsset b; b.Set("k", "a\nb");
        CHECK(b.Save("plain.okaydata"));
        std::ifstream in("plain.okaydata", std::ios::binary);
        std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        PutFile("bom2.okaydata", "\xEF\xBB\xBF" + saved);
        DataAsset c; CHECK(c.Load("bom2.okaydata"));
        CHECK(c.GetString("k") == "a\nb");       // still read as an escaped file
    }

    // --- DataAsset: numbers ---
    {
        DataAsset a;
        a.Set("hp", "30"); a.Set("f", "-2.5e1"); a.Set("nan", "nan"); a.Set("inf", "inf"); a.Set("ninf", "-Infinity");
        a.Set("huge", "1e999"); a.Set("word", "goblin"); a.Set("lead", "12abc"); a.Set("empty", "");
        CHECK(a.GetNumber("hp") == 30.0 && a.GetNumber("f") == -25.0);
        CHECK(a.GetNumber("nan", 7.0) == 7.0);   // NaN used to reach scripts and poison every sum
        CHECK(a.GetNumber("inf", 7.0) == 7.0 && a.GetNumber("ninf", 7.0) == 7.0 && a.GetNumber("huge", 7.0) == 7.0);
        CHECK(a.GetNumber("word", 7.0) == 0.0);  // text without a number still reads 0, as before
        CHECK(a.GetNumber("lead", 7.0) == 12.0);
        CHECK(a.GetNumber("missing", 7.0) == 7.0);
        CHECK(std::isfinite(a.GetNumber("nan")) && std::isfinite(a.GetNumber("inf")));

        CHECK(a.GetInt("hp") == 30 && a.GetInt("f") == -25);
        a.Set("big", "1e12"); a.Set("small", "-1e12"); a.Set("frac", "3.99"); a.Set("nfrac", "-3.99");
        CHECK(a.GetInt("big") == std::numeric_limits<int>::max() && a.GetInt("small") == std::numeric_limits<int>::min());
        CHECK(a.GetInt("frac") == 3 && a.GetInt("nfrac") == -3);
        CHECK(a.GetInt("nan", 5) == 5 && a.GetInt("inf", 5) == 5 && a.GetInt("missing", 5) == 5);

        a.Set("b1", "true"); a.Set("b2", "YES"); a.Set("b3", " On "); a.Set("b4", "1");
        a.Set("n1", "false"); a.Set("n2", "No"); a.Set("n3", "off"); a.Set("n4", "0"); a.Set("odd", "maybe");
        CHECK(a.GetBool("b1") && a.GetBool("b2") && a.GetBool("b3") && a.GetBool("b4"));
        CHECK(!a.GetBool("n1", true) && !a.GetBool("n2", true) && !a.GetBool("n3", true) && !a.GetBool("n4", true));
        CHECK(a.GetBool("odd", true) && !a.GetBool("odd", false));   // anything else: the default
        CHECK(a.GetBool("missing", true) && !a.GetBool("missing"));

        double v = -1.0;
        CHECK(a.TryGetNumber("hp", v) && v == 30.0);
        CHECK(a.TryGetNumber("f", v) && v == -25.0);
        v = -1.0;
        CHECK(!a.TryGetNumber("lead", v) && v == -1.0);   // strict: "12abc" is a typo, not 12
        CHECK(!a.TryGetNumber("word", v) && !a.TryGetNumber("empty", v) && !a.TryGetNumber("nan", v));
        CHECK(!a.TryGetNumber("inf", v) && !a.TryGetNumber("huge", v) && !a.TryGetNumber("missing", v));
        CHECK(v == -1.0);
        a.Set("padded", "  4.5  ");
        CHECK(a.TryGetNumber("padded", v) && v == 4.5);
    }

    // --- DataAsset: multi-line, awkward values and keys round trip (an old note said they did not) ---
    {
        DataAsset a;
        a.Set("multi", "line1\nline2\r\nline3");
        a.Set("tab", "a\tb");
        a.Set("eq key=", "v=1");
        a.Set("#hash", "#v");
        a.Set(" lead", " trail ");
        a.Set("bs", "c:\\dir\\n");
        a.Set("", "emptykey");
        a.Set("empty", "");
        a.Set("uni", "h\xC3\xA9llo \xE2\x82\xAC");
        a.Set("nul", std::string("a\0b", 3));
        a.Set("long", std::string(100000, 'x') + "\n" + std::string(100000, 'y'));
        CHECK(a.Save("rt.okaydata"));
        DataAsset b;
        CHECK(b.Load("rt.okaydata"));
        CHECK(a.Fields() == b.Fields());
    }

    // --- DataAsset cache: a file that appears later is picked up; Reload; Invalidate keeps references valid ---
    {
        DataAsset::InvalidateAll();
        DataAsset& miss = DataAsset::Cached("later.okaydata");
        CHECK(!miss.Has("x"));
        PutFile("later.okaydata", "x = 1\n");
        CHECK(DataAsset::Cached("later.okaydata").Has("x"));      // a failed load is not remembered
        CHECK(&DataAsset::Cached("later.okaydata") == &miss);

        PutFile("later.okaydata", "x = 2\ny = 3\n");
        CHECK(DataAsset::Cached("later.okaydata").GetNumber("x") == 1.0);   // cached: the edit is not seen ...
        CHECK(DataAsset::Reload("later.okaydata"));                          // ... until Reload
        CHECK(DataAsset::Cached("later.okaydata").GetNumber("x") == 2.0);
        CHECK(!DataAsset::Reload("never_existed.okaydata"));

        DataAsset& held = DataAsset::Cached("later.okaydata");    // a script or panel keeps this reference
        DataAsset::Invalidate("later.okaydata");
        CHECK(!held.Has("x"));                                    // emptied, but still a live object (not freed)
        PutFile("later.okaydata", "x = 9\n");
        DataAsset& fresh = DataAsset::Cached("later.okaydata");
        CHECK(&fresh == &held);                                   // the same object, filled again
        CHECK(held.GetNumber("x") == 9.0);
        DataAsset::Invalidate("not_cached.okaydata");             // harmless
        DataAsset::InvalidateAll();
        CHECK(!held.Has("x"));
        CHECK(DataAsset::Cached("later.okaydata").GetNumber("x") == 9.0);

        // Values set by a script into an asset that has no file yet are kept until the file exists.
        DataAsset& scratch = DataAsset::Cached("scratch.okaydata");
        scratch.Set("score", "10");
        CHECK(DataAsset::Cached("scratch.okaydata").GetNumber("score") == 10.0);
        CHECK(scratch.Save("scratch.okaydata"));
        DataAsset::InvalidateAll();
        CHECK(DataAsset::Cached("scratch.okaydata").GetNumber("score") == 10.0);
        DataAsset::InvalidateAll();
    }

    // --- DataPack: round trips ---
    {
        std::string empty;
        const std::string packedEmpty = DataPack::Pack(empty);
        CHECK(packedEmpty.size() == DataPack::kHeader && DataPack::IsPacked(packedEmpty));
        bool ok = false; std::string err;
        CHECK(DataPack::Unpack(packedEmpty, &ok, &err).empty() && ok);   // a valid empty file is not "tampered"
        std::string out;
        CHECK(DataPack::TryDecode(packedEmpty, DataPack::DefaultKey(), out) && out.empty());

        std::string bin;
        for (int i = 0; i < 70001; ++i) bin += (char)(i * 31 + (i >> 8));   // every byte value, odd length
        const std::string packedBin = DataPack::Pack(bin);
        CHECK(DataPack::Unpack(packedBin, &ok) == bin && ok);
        CHECK(packedBin.find(bin.substr(100, 64)) == std::string::npos);    // the body is not left readable
        std::string nuls(1000, '\0');
        CHECK(DataPack::Unpack(DataPack::Pack(nuls), &ok) == nuls && ok);

        // Every body length around the 8-byte keystream blocks.
        for (std::size_t n = 0; n < 40; ++n) {
            const std::string s(n, 'q');
            CHECK(DataPack::Unpack(DataPack::Pack(s), &ok) == s && ok);
        }

        std::string big(6u << 20, 'x');                                     // 6 MB
        for (std::size_t i = 0; i < big.size(); i += 997) big[i] = (char)(i >> 3);
        CHECK(DataPack::Unpack(DataPack::Pack(big), &ok) == big && ok);

        // A different key is damage too.
        CHECK(!DataPack::TryDecode(packedBin, DataPack::DefaultKey() ^ 1, out, &err) && out.empty() && !err.empty());
        // Plain text passes through untouched.
        CHECK(DataPack::Unpack("scene text", &ok) == "scene text" && ok);
        CHECK(DataPack::Unpack("OKP", &ok) == "OKP" && ok);                 // shorter than the magic: plain text
    }

    // --- DataPack: damage is always reported, never a crash and never plain text ---
    {
        const std::string text = "gameobject 1 \"Player\"\n  position 1 2 3\nend\n";
        const std::string packed = DataPack::Pack(text);
        CHECK(packed.size() == DataPack::kHeader + text.size());
        bool ok = true; std::string err;
        // Cut anywhere after the 4 magic bytes: shorter than the header, or a body that no longer matches its checksum.
        for (std::size_t cut = 4; cut < packed.size(); ++cut) {
            const std::string t = packed.substr(0, cut);
            CHECK(DataPack::IsPacked(t));                                    // a damaged pack is still a pack ...
            ok = true; err.clear();
            const std::string out = DataPack::Unpack(t, &ok, &err);
            CHECK(!ok && out.empty() && !err.empty());                       // ... and says so
            CHECK(DataPack::Unpack(t).empty());                              // the short form gives nothing, not the raw bytes
            std::string tmp;
            CHECK(!DataPack::TryDecode(t, DataPack::DefaultKey(), tmp));
            CHECK(DataPack::Decode(t, DataPack::DefaultKey()).empty());
        }
        CHECK(DataPack::Unpack(packed.substr(0, 10), &ok, &err).empty() && !ok);
        CHECK(err.find("truncated") != std::string::npos);
        // Flip every single byte after the magic: the checksum (or the nonce it depends on) catches each one.
        int caught = 0;
        for (std::size_t i = 4; i < packed.size(); ++i) {
            std::string t = packed;
            t[i] = (char)(t[i] ^ 0x5A);
            ok = true;
            if (DataPack::Unpack(t, &ok).empty() && !ok) ++caught;
        }
        CHECK(caught == (int)packed.size() - 4);
        // The magic itself damaged: it is no pack any more and goes through as plain text (the loader then fails on it).
        std::string t = packed; t[0] = 'X';
        CHECK(!DataPack::IsPacked(t));
    }

    fs::current_path(oldCwd);
    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
