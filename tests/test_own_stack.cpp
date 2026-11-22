// The own-stack dogfood switch and the startup line (shared/OwnStack.hpp, docs/status/own_stack_rollout.md).
//
// Two modes, two ctest entries, because the okay::platform default and the FBX choice are decided on first use and then kept:
//   test_own_stack          what this BUILD does with nothing set in the environment: the old line in a default build
//                           (nothing of ours switched on), the all-own line in a -DOKAY_OWN_STACK=ON build, and never
//                           own crypto unless -DOKAY_OWN_STACK_CRYPTO_TEST=ON.
//   test_own_stack env      what OKAY_OWN_STACK=1 sets at run time, that a variable already set wins, that crypto is not
//                           among them, and that every setting reaches the real selectors.
#include "test_framework.hpp"
#include "OwnStack.hpp"
#include "okay/Core/ThirdParty.hpp"

#include <cstdio>

#include <cstring>
#include <set>
#include <string>

using namespace okay::ownstack;

static void Unset(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
static void Set(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
static void ClearStackEnvironment() {
    for (const Setting& s : RuntimeSettings()) Unset(s.var);
    Unset("OKAY_OWN_STACK");
    Unset("OKAY_HTTP_CURL");
    Unset("OKAY_DEV");
}
static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static void TestText() {
    Info old;   // the stack of a default build
    old.platform = "sdl2"; old.http = "curl"; old.ufbx = true; old.assimp = true; old.crypto = "libsodium"; old.ui = "imgui";
    CHECK(FormatLine(old) == "[okay] stack: platform=sdl2 http=curl readers=ufbx+assimp crypto=libsodium ui=imgui");
    old.assimp = false;
    CHECK(FormatLine(old) == "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=libsodium ui=imgui");
    old.switches = "wav,http";   // ids turned on from Settings > Experimental (shared/OwnSwitches.hpp) are appended; none adds nothing
    CHECK(FormatLine(old) == "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=libsodium ui=imgui switches=wav,http");
    old.switches.clear();

    Info mix;
    mix.platform = "own"; mix.http = "curl"; mix.ownReaders = {"fbx", "dae", "3ds", "bvh"}; mix.assimp = true;
    mix.crypto = "libsodium"; mix.ui = "imgui";
    CHECK(FormatLine(mix) == "[okay] stack: platform=own http=curl readers=own(fbx,dae,3ds,bvh)+assimp crypto=libsodium ui=imgui");
    mix.ufbx = true;
    CHECK(FormatLine(mix) == "[okay] stack: platform=own http=curl readers=own(fbx,dae,3ds,bvh)+ufbx+assimp crypto=libsodium ui=imgui");

    Info all;
    all.platform = "own"; all.http = "own"; all.ownReaders = {"fbx"}; all.crypto = "own(test)"; all.ui = "okayui";
    CHECK(FormatLine(all) == "[okay] stack: platform=own http=own readers=own(fbx) crypto=own(test) ui=okayui");
    Info none;
    none.platform = "sdl2"; none.http = "winhttp"; none.crypto = "own(no libsodium)"; none.ui = "none";
    CHECK(FormatLine(none) == "[okay] stack: platform=sdl2 http=winhttp readers=none crypto=own(no libsodium) ui=none");
    CHECK(FormatLine(none).find('\n') == std::string::npos);   // one line
}

// The crypto word is honest: libsodium only when it is really in use; the fallback says so; the own builds say "own".
static void TestCryptoLabel() {
    CHECK(CryptoLabel(false, false, true) == "libsodium");
    CHECK(CryptoLabel(false, false, false) == "own(no libsodium)");   // libsodium missing or failed to start: the fallback is NOT hidden
    CHECK(CryptoLabel(true, true, false) == "own(test)");
    CHECK(CryptoLabel(true, false, false) == "own");
    CHECK(CryptoLabel(true, true, true) == "own(test)");              // OKAY_OWN_CRYPTO wins even if libsodium is present
    CHECK(CryptoLabel(true, false, true) == "own");
    // The real build agrees with what the real backend answers.
    const std::string c = Gather("none").crypto;
#if defined(OKAY_OWN_CRYPTO)
    CHECK(!okayac::ed25519::UseSodium());
    CHECK(c == "own(test)" || c == "own");
#elif defined(OKAY_HAVE_SODIUM)
    CHECK(c == "libsodium" && okayac::ed25519::UseSodium() && std::string(okayac::ed25519::Backend()) == "libsodium");
#else
    CHECK(c == "own(no libsodium)" && std::string(okayac::ed25519::Backend()) == "okay::crypto");
#endif
    // The release guard's question (OKAY_PRINT_STACK_AND_EXIT) is exactly "1".
    Unset("OKAY_PRINT_STACK_AND_EXIT");
    CHECK(!PrintAndExitRequested());
    Set("OKAY_PRINT_STACK_AND_EXIT", "1");  CHECK(PrintAndExitRequested());
    Set("OKAY_PRINT_STACK_AND_EXIT", "0");  CHECK(!PrintAndExitRequested());
    Set("OKAY_PRINT_STACK_AND_EXIT", "11"); CHECK(!PrintAndExitRequested());
    Unset("OKAY_PRINT_STACK_AND_EXIT");
}

static void TestTables() {
    // Every reader switch is one the import table knows, and the names are unique.
    std::set<std::string> vars, labels;
    for (const ReaderSwitch& r : Readers()) {
        CHECK(vars.insert(r.var).second);
        CHECK(labels.insert(r.label).second);
        if (r.ext[0]) CHECK(okay::HasOwnReader(r.ext));
        CHECK(std::strncmp(r.var, "OKAY_OWN_", 9) == 0);
    }
    CHECK(Readers().size() == 20);   // FBX + the 19 opt-in readers of ModelImport.cpp (slots 3 to 21)
    // Cryptography is not among the settings, by name or by value.
    for (const Setting& s : RuntimeSettings()) {
        CHECK(!Has(s.var, "CRYPTO") && !Has(s.var, "SODIUM") && !Has(s.value, "crypto"));
    }
    // What the dogfood switch sets besides the readers: HTTP, the okay::platform backend (Linux), the UI toolkit.
    bool http = false, ui = false, plat = false;
    for (const Setting& s : RuntimeSettings()) {
        if (!std::strcmp(s.var, "OKAY_OWN_HTTP")) http = !std::strcmp(s.value, "1");
        if (!std::strcmp(s.var, "OKAY_UI_BACKEND")) ui = !std::strcmp(s.value, "okayui");
        if (!std::strcmp(s.var, "OKAY_PLATFORM_BACKEND")) plat = !std::strcmp(s.value, "own");
    }
    CHECK(http && ui);
#if defined(__linux__)
    CHECK(plat);
#else
    CHECK(!plat);   // the Win32 backend has never run on real Windows: not switched on there
#endif
}

// What the build alone does (nothing in the environment).
static void TestBuildDefaults() {
    ClearStackEnvironment();
    CHECK(!Requested());
    CHECK(ApplyEnvironment() == 0);   // not requested: nothing is set
    const Info i = Gather("imgui");
    const std::string line = FormatLine(i);
    std::printf("%s\n", line.c_str());
    CHECK(line.rfind("[okay] stack: platform=", 0) == 0);
    CHECK(Has(line, " http=") && Has(line, " readers=") && Has(line, " crypto=") && Has(line, " ui=imgui"));

#if defined(OKAY_OWN_STACK_BUILD)   // -DOKAY_OWN_STACK=ON: every own backend that exists on this system is the default
    CHECK(i.ownReaders.size() == Readers().size());
    CHECK(!i.ufbx);
#  if defined(__linux__)
    CHECK(i.platform == "own");
#  endif
#  if defined(OKAY_OWN_HTTP) && !defined(_WIN32) && !defined(__APPLE__)
    CHECK(i.http == "own");
    Set("OKAY_OWN_HTTP", "0");   // the run-time switch still wins over the build default
    CHECK(Describe("imgui").find("http=curl") != std::string::npos);
    Unset("OKAY_OWN_HTTP");
#  endif
    // Crypto: still the library, unless this is the test-only crypto build.
#  if defined(OKAY_OWN_STACK_CRYPTO_TEST)
    CHECK(i.crypto == "own(test)");
#  else
    CHECK(i.crypto != "own" && i.crypto != "own(test)");
#  endif
#elif defined(OKAY_EXPECT_OWN_READERS)   // a default build since 2026-11-17: our own model readers, the rest still the old stack
    CHECK(i.ownReaders.size() == Readers().size());
    CHECK(!i.ufbx);
    CHECK(i.platform == "sdl2");
    CHECK(i.http == "curl" || i.http == "winhttp");
    CHECK(Has(line, "readers=own(fbx,dae,"));
#else               // a build with the reader options switched OFF: exactly the old stack
    CHECK(i.ownReaders.empty());
    CHECK(i.ufbx);
    CHECK(i.platform == "sdl2");
    CHECK(i.http == "curl" || i.http == "winhttp");
    CHECK(i.crypto != "own" && i.crypto != "own(test)");
    CHECK(Has(line, "readers=ufbx"));
#endif
#if !defined(OKAY_OWN_CRYPTO)
    CHECK(i.crypto != "own" && i.crypto != "own(test)");
#endif
    // The ui field: the caller's word, "none" for a program without a toolkit, else the resolved default.
    CHECK(Describe("none").find(" ui=none") != std::string::npos);
    CHECK(Describe("okayui").find(" ui=okayui") != std::string::npos);
    const std::string def = Describe();
    CHECK(Has(def, " ui=imgui") || Has(def, " ui=okayui"));
}

// OKAY_OWN_STACK=1 at run time.
static void TestEnvironment() {
    ClearStackEnvironment();
    Set("OKAY_OWN_STACK", "0");
    CHECK(!Requested() && ApplyEnvironment() == 0);
    Set("OKAY_OWN_STACK", "yes");
    CHECK(!Requested() && ApplyEnvironment() == 0);   // only the exact value 1
    CHECK(!std::getenv("OKAY_OWN_FBX"));

    Set("OKAY_OWN_STACK", "1");
    Set("OKAY_OWN_3DS", "0");                          // a developer's own choice wins
    Set("OKAY_UI_BACKEND", "imgui");
    CHECK(Requested());
    const int n = ApplyEnvironment();
    CHECK(n == (int)RuntimeSettings().size() - 2);     // every setting but the two already set
    CHECK(std::string(std::getenv("OKAY_OWN_3DS")) == "0");
    CHECK(std::string(std::getenv("OKAY_UI_BACKEND")) == "imgui");
    CHECK(std::string(std::getenv("OKAY_OWN_FBX")) == "1" && std::string(std::getenv("OKAY_OWN_BVH")) == "1");
    CHECK(ApplyEnvironment() == 0);                    // idempotent
    CHECK(!std::getenv("OKAY_OWN_CRYPTO") && !std::getenv("OKAY_OWN_STACK_CRYPTO_TEST"));

    // The settings reach the real selectors: every reader but the one switched off is now ours.
    for (const ReaderSwitch& r : Readers()) {
        const bool fbx = r.ext[0] == '\0';
        const bool own = fbx ? okay::fbx::OwnReaderEnabled() : okay::EffectiveImportBackend(r.ext) == okay::ImportBackend::Own;
        if (!std::strcmp(r.var, "OKAY_OWN_3DS")) CHECK(!own);
        else CHECK(own);
    }
    const Info i = Gather("imgui");
    CHECK(i.ownReaders.size() == Readers().size() - 1);
    CHECK(!Has(FormatLine(i), "3ds"));
    CHECK(i.ufbx == false);
#if defined(__linux__)
    CHECK(i.platform == "own");
#endif
#if defined(OKAY_OWN_HTTP) && !defined(_WIN32) && !defined(__APPLE__)
    CHECK(i.http == "own");
#else
    CHECK(i.http != "own");
#endif
    CHECK(i.crypto != "own" || Has(FormatLine(i), "crypto=own"));   // crypto is a build property, whatever the environment says
    Set("OKAY_OWN_CRYPTO", "1");                                   // a variable of that name does nothing at run time
    CHECK(Gather("imgui").crypto == i.crypto);
}

// OKAY_THIRD_PARTY=1 at run time (okay/Core/ThirdParty.hpp): every model reader back to Assimp / ufbx, whatever the build default
// and whatever OKAY_OWN_<FMT> says. Its own ctest entry: the FBX choice is decided on first use and then kept.
static void TestThirdParty() {
    ClearStackEnvironment();
    Set("OKAY_THIRD_PARTY", "1");
    Set("OKAY_OWN_3DS", "1");   // the master switch wins over a per-format 1
    CHECK(okay::ThirdPartyForced());
    const Info i = Gather("imgui");
    std::printf("%s\n", FormatLine(i).c_str());
    CHECK(i.ufbx && !okay::fbx::OwnReaderEnabled());
#ifdef OKAY_HAVE_ASSIMP
    CHECK(i.ownReaders.empty());
    CHECK(Has(FormatLine(i), "readers=ufbx+assimp"));
#else
    // no Assimp compiled in: ours still read those formats (nothing else could), only FBX goes back (to ufbx)
    CHECK(i.ownReaders.size() + 1 >= Readers().size() || i.ownReaders.empty());
#endif
    Set("OKAY_THIRD_PARTY", "0");
    CHECK(!okay::ThirdPartyForced());
    Set("OKAY_THIRD_PARTY", "yes");
    CHECK(!okay::ThirdPartyForced());   // only the exact value 1
    Set("OKAY_THIRD_PARTY", "11");
    CHECK(!okay::ThirdPartyForced());
    Unset("OKAY_THIRD_PARTY");
    CHECK(!okay::ThirdPartyForced());
    Unset("OKAY_OWN_3DS");
}

int main(int argc, char** argv) {
    RUN_SUITE("own stack switch and startup line");
    if (argc > 1 && std::string(argv[1]) == "thirdparty") { TestThirdParty(); TEST_MAIN_RESULT(); }
    TestText();
    TestTables();
    if (argc > 1 && std::string(argv[1]) == "env") TestEnvironment();
    else { TestCryptoLabel(); TestBuildDefaults(); }   // not in env mode: Gather() would fix the selectors before the environment is set
    TEST_MAIN_RESULT();
}
