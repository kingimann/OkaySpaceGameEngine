// Console variables: registration, parsing edge cases, ranges, flags, callbacks, enumeration and
// autocomplete, command-line overrides, console lines and the Prefs round trip.
#include "test_framework.hpp"
#include "okay/Core/CVar.hpp"
#include "okay/Core/Prefs.hpp"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <locale>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void TestParsing() {
    RUN_SUITE("cvar: parsing");
    bool b = false;
    for (const char* t : {"1", "true", "TRUE", "True", "on", "ON", "yes", "Yes"}) { b = false; CHECK(CVarRegistry::ParseBool(t, b) && b); }
    for (const char* t : {"0", "false", "FALSE", "off", "Off", "no", "NO"}) { b = true; CHECK(CVarRegistry::ParseBool(t, b) && !b); }
    for (const char* t : {"", "2", "-1", "truee", "tru", "y", "n", " true", "true ", "t", "01", "1.0", "on off"}) CHECK(!CVarRegistry::ParseBool(t, b));

    int i = 0;
    struct IntCase { const char* text; bool ok; int value; };
    const IntCase ints[] = {
        {"0", true, 0}, {"7", true, 7}, {"-7", true, -7}, {"+7", true, 7}, {"007", true, 7},
        {"2147483647", true, INT_MAX}, {"-2147483648", true, INT_MIN},
        {"2147483648", false, 0}, {"-2147483649", false, 0}, {"99999999999999999999", false, 0},
        {"", false, 0}, {"-", false, 0}, {"+", false, 0}, {"--1", false, 0}, {"+-1", false, 0},
        {"1.5", false, 0}, {"1e3", false, 0}, {"0x10", false, 0}, {" 5", false, 0}, {"5 ", false, 0}, {"5x", false, 0}, {"abc", false, 0},
        {"1,000", false, 0}, {"\xD9\xA1", false, 0},
    };
    for (const IntCase& c : ints) {
        i = -12345;
        const bool ok = CVarRegistry::ParseInt(c.text, i);
        CHECK(ok == c.ok);
        if (c.ok) CHECK(ok && i == c.value);
        else CHECK(i == -12345);          // untouched on failure
    }

    float f = 0.0f;
    struct FloatCase { const char* text; bool ok; float value; };
    const FloatCase floats[] = {
        {"1.5", true, 1.5f}, {"-2", true, -2.0f}, {".5", true, 0.5f}, {"5.", true, 5.0f}, {"+3", true, 3.0f},
        {"1e3", true, 1000.0f}, {"1E-2", true, 0.01f}, {"0", true, 0.0f}, {"-0", true, -0.0f}, {"007.25", true, 7.25f},
        {"3.4028234e38", true, 3.4028234e38f},
        {"", false, 0}, {".", false, 0}, {"-", false, 0}, {"e5", false, 0}, {"1e", false, 0}, {"1e+", false, 0}, {"1.2.3", false, 0},
        {"--1", false, 0}, {"1e5.3", false, 0}, {"nan", false, 0}, {"NaN", false, 0}, {"inf", false, 0}, {"-inf", false, 0},
        {"1e999", false, 0}, {"-1e999", false, 0}, {"1e39", false, 0}, {"0x1p3", false, 0}, {"1,5", false, 0}, {" 1", false, 0}, {"1 ", false, 0},
        {"1f", false, 0}, {"abc", false, 0},
    };
    for (const FloatCase& c : floats) {
        f = -777.0f;
        const bool ok = CVarRegistry::ParseFloat(c.text, f);
        CHECK(ok == c.ok);
        if (c.ok) CHECK(f == c.value);
        else CHECK(f == -777.0f);
    }

    // formatting: shortest text that reads back exactly
    CHECK(CVarRegistry::FormatFloat(0.1f) == "0.1");
    CHECK(CVarRegistry::FormatFloat(1.0f) == "1");
    CHECK(CVarRegistry::FormatFloat(-0.5f) == "-0.5");
    CHECK(CVarRegistry::FormatFloat(0.0f) == "0");
    CHECK(CVarRegistry::FormatFloat(1.0f / 3.0f) == "0.33333334");
    CHECK(CVarRegistry::FormatFloat(INFINITY) == "inf" && CVarRegistry::FormatFloat(NAN) == "nan");
    std::mt19937 rng(5);
    bool roundTrip = true;
    for (int n = 0; n < 5000; ++n) {
        std::uint32_t bits = rng();
        float v;
        std::memcpy(&v, &bits, 4);
        if (!std::isfinite(v)) continue;
        float back = 0;
        if (!CVarRegistry::ParseFloat(CVarRegistry::FormatFloat(v), back) || back != v) { roundTrip = false; break; }
    }
    CHECK(roundTrip);

    // a global locale with a decimal comma changes nothing (skipped where it is not installed)
    try {
        std::locale old = std::locale::global(std::locale("de_DE.UTF-8"));
        float v = 0;
        CHECK(CVarRegistry::ParseFloat("1.5", v) && v == 1.5f);
        CHECK(!CVarRegistry::ParseFloat("1,5", v));
        CHECK(CVarRegistry::FormatFloat(2.5f) == "2.5");
        std::locale::global(old);
    } catch (const std::runtime_error&) {}

    CHECK(CVarRegistry::FormatValue(CVarValue::Bool(true)) == "true" && CVarRegistry::FormatValue(CVarValue::Int(-3)) == "-3");
    CHECK(CVarRegistry::FormatValue(CVarValue::String("a b")) == "a b");
    CHECK(CVarRegistry::IsValidName("render.max_fps") && CVarRegistry::IsValidName("_x") && CVarRegistry::IsValidName("A1"));
    for (const char* bad : {"", "1abc", "a b", "a-b", "a=b", ".a", "list", "RESET", "Toggle", "help", "caf\xC3\xA9"})
        CHECK(!CVarRegistry::IsValidName(bad));
    CHECK(!CVarRegistry::IsValidName(std::string(129, 'a')) && CVarRegistry::IsValidName(std::string(128, 'a')));
}

void TestRegistrationAndTypes() {
    RUN_SUITE("cvar: registration and types");
    CVarRegistry reg;
    CHECK(reg.Count() == 1 && reg.Exists("cheats"));         // the built-in
    CVar<bool> vsync(reg, "render.vsync", true, "Wait for the display");
    CVar<int> fps(reg, "render.max_fps", 60, "Frame cap", CVarFlags::None, 0, 1000);
    CVar<float> gamma(reg, "render.gamma", 2.2f, "Display gamma", CVarFlags::None, 1.0f, 3.0f);
    CVar<std::string> name(reg, "player.name", "Ada", "Display name");
    CHECK(vsync.Valid() && fps.Valid() && gamma.Valid() && name.Valid());
    CHECK(vsync.Get() == true && fps.Get() == 60 && gamma.Get() == 2.2f && name.Get() == "Ada");
    CHECK(vsync.Name() == "render.vsync" && fps.Default() == 60 && name.Default() == "Ada");

    CHECK(vsync.Set(false) && !vsync.Get());
    CHECK(fps.Set(144) && fps.Get() == 144);
    CHECK(gamma.Set(1.8f) && gamma.Get() == 1.8f);
    CHECK(name.Set("Grace Hopper") && name.Get() == "Grace Hopper");
    int asInt = fps;                                          // implicit conversion
    CHECK(asInt == 144);
    fps = 30;                                                 // assignment
    CHECK(fps.Get() == 30);
    CHECK(fps.TrySet(30) == CVarResult::Unchanged && fps.TrySet(31) == CVarResult::Ok);

    CVarInfo info;
    CHECK(reg.Info("render.max_fps", info));
    CHECK(info.type == CVarType::Int && info.value == "31" && info.defaultValue == "60" && info.modified);
    CHECK(info.hasRange && info.min == 0 && info.max == 1000 && info.description == "Frame cap");
    CHECK(reg.Info("render.vsync", info) && info.type == CVarType::Bool && info.value == "false" && !info.hasRange);
    CHECK(reg.Info("player.name", info) && info.type == CVarType::String && info.value == "Grace Hopper");
    CHECK(!reg.Info("nope", info));

    fps.Reset(); vsync.Reset(); name.Reset();
    CHECK(fps.Get() == 60 && vsync.Get() && name.Get() == "Ada");

    // the same name and type shares one variable; first registration's settings win
    CVar<int> fps2(reg, "render.max_fps", 999, "other", CVarFlags::ReadOnly, 5, 6);
    CHECK(fps2.Valid() && fps2.Get() == 60);
    fps.Set(75);
    CHECK(fps2.Get() == 75);
    CHECK(reg.Count() == 5);
    CHECK(fps2.Set(5000) && fps2.Get() == 1000);              // the original range, and not read-only

    // conflicts leave a detached handle, not a crash
    CVar<float> clash(reg, "render.max_fps", 1.5f, "wrong type");
    CHECK(!clash.Valid() && clash.Get() == 1.5f && !clash.Set(2.0f) && clash.Get() == 1.5f);
    CHECK(clash.TrySet(2.0f) == CVarResult::Unknown && clash.OnChange([](const float&, const float&) {}) == 0);
    clash.Reset();
    CVar<int> badName(reg, "not a name", 1, "x");
    CVar<int> reserved(reg, "list", 1, "x");
    CHECK(!badName.Valid() && !reserved.Valid() && badName.Name().empty());
    const auto errs = reg.RegistrationErrors();
    CHECK(errs.size() == 3 && Contains(errs[0], "render.max_fps") && Contains(errs[1], "not a name"));
    CHECK(reg.Count() == 5);

    // a range with min > max is swapped; the default is pulled into the range
    CVar<int> swapped(reg, "t.swapped", 50, "x", CVarFlags::None, 10, -10);
    CHECK(swapped.Get() == 10 && swapped.Set(-100) && swapped.Get() == -10);
    CVar<float> clampedDefault(reg, "t.cd", 100.0f, "x", CVarFlags::None, 0.0f, 1.0f);
    CHECK(clampedDefault.Get() == 1.0f && clampedDefault.Default() == 1.0f);
}

void TestClampAndFlags() {
    RUN_SUITE("cvar: ranges and flags");
    CVarRegistry reg;
    CVar<int> n(reg, "a.n", 5, "", CVarFlags::None, 0, 10);
    CVar<float> x(reg, "a.x", 0.5f, "", CVarFlags::None, 0.0f, 1.0f);
    CHECK(n.TrySet(11) == CVarResult::Clamped && n.Get() == 10);
    CHECK(n.TrySet(-4) == CVarResult::Clamped && n.Get() == 0);
    CHECK(n.TrySet(0) == CVarResult::Unchanged);
    CHECK(n.TrySet(7) == CVarResult::Ok);
    CHECK(x.TrySet(2.0f) == CVarResult::Clamped && x.Get() == 1.0f);
    CHECK(x.TrySet(-INFINITY) == CVarResult::Clamped && x.Get() == 0.0f);
    CHECK(x.TrySet(NAN) == CVarResult::ParseError && x.Get() == 0.0f);   // NaN is refused
    std::string err;
    CHECK(reg.SetFromString("a.n", "99", &err) == CVarResult::Clamped && n.Get() == 10);
    CHECK(reg.SetFromString("a.n", "abc", &err) == CVarResult::ParseError && Contains(err, "whole number") && n.Get() == 10);
    CHECK(reg.SetFromString("a.n", "1.5", &err) == CVarResult::ParseError);
    CHECK(reg.SetFromString("a.x", "nan", &err) == CVarResult::ParseError && Contains(err, "number"));
    CHECK(reg.SetFromString("a.x", " 0.25 ", &err) == CVarResult::Ok && x.Get() == 0.25f);   // numbers may be padded
    CHECK(reg.SetFromString("missing", "1", &err) == CVarResult::Unknown && Contains(err, "unknown variable"));

    // read-only: the user path refuses, code may still set it
    CVar<std::string> version(reg, "sys.version", "1.0", "Build", CVarFlags::ReadOnly);
    CHECK(reg.SetFromString("sys.version", "2.0", &err) == CVarResult::ReadOnly && Contains(err, "read-only"));
    CHECK(version.Get() == "1.0" && version.Set("1.1") && version.Get() == "1.1");
    CHECK(reg.Reset("sys.version", &err) == CVarResult::ReadOnly && version.Get() == "1.1");
    version.Reset();
    CHECK(version.Get() == "1.0");

    // cheat: refused until `cheats` is on
    CVar<bool> god(reg, "player.god", false, "", CVarFlags::Cheat);
    CHECK(!reg.CheatsEnabled());
    CHECK(reg.SetFromString("player.god", "1", &err) == CVarResult::CheatsDisabled && Contains(err, "cheat") && !god.Get());
    CHECK(god.Set(true) && god.Get());                                  // code is not bound by it
    god.Reset();
    CHECK(reg.SetFromString("cheats", "on") == CVarResult::Ok && reg.CheatsEnabled());
    CHECK(reg.SetFromString("player.god", "1") == CVarResult::Ok && god.Get());
    reg.SetCheatsEnabled(false);
    CHECK(!reg.CheatsEnabled() && reg.SetFromString("player.god", "0") == CVarResult::CheatsDisabled && god.Get());

    // restart required: the value in effect holds still
    CVar<int> msaa(reg, "gfx.msaa", 4, "", CVarFlags::RestartRequired | CVarFlags::Persist, 0, 16);
    CHECK(!msaa.NeedsRestart() && reg.RestartPending().empty());
    CHECK(reg.SetFromString("gfx.msaa", "8") == CVarResult::RestartPending);
    CHECK(msaa.Get() == 4 && msaa.NeedsRestart() && Has(reg.RestartPending(), "gfx.msaa"));
    CVarInfo info;
    CHECK(reg.Info("gfx.msaa", info) && info.hasPending && info.pendingValue == "8" && info.value == "4");
    CHECK(reg.SetFromString("gfx.msaa", "8") == CVarResult::RestartPending);   // the same pending value again
    CHECK(reg.SetFromString("gfx.msaa", "99") == CVarResult::RestartPending);  // clamped, still pending
    CHECK(reg.Info("gfx.msaa", info) && info.pendingValue == "16");
    CHECK(reg.SetFromString("gfx.msaa", "4") == CVarResult::Ok);               // back to what runs: the pending value is dropped
    CHECK(!msaa.NeedsRestart() && reg.RestartPending().empty());
    CHECK(msaa.TrySet(2) == CVarResult::RestartPending && msaa.Get() == 4);    // code path defers too
    msaa.Reset();                                                              // pending default == in effect: cleared
    CHECK(!msaa.NeedsRestart());
    reg.ResetAll();
    CHECK(msaa.Get() == 4 && !msaa.NeedsRestart());
}

void TestListeners() {
    RUN_SUITE("cvar: change callbacks");
    CVarRegistry reg;
    CVar<int> n(reg, "l.n", 1, "", CVarFlags::None, 0, 100);
    std::vector<std::pair<int, int>> calls;
    const int id1 = n.OnChange([&](const int& now, const int& was) { calls.emplace_back(now, was); });
    int second = 0;
    const int id2 = n.OnChange([&](const int& now, const int&) { second = now; });
    CHECK(id1 > 0 && id2 > id1);
    n.Set(5);
    CHECK(calls.size() == 1 && calls[0] == std::make_pair(5, 1) && second == 5);
    n.Set(5);                                              // unchanged: no call
    CHECK(calls.size() == 1);
    reg.SetFromString("l.n", "500");                       // clamped to 100: called with the stored value
    CHECK(calls.size() == 2 && calls[1] == std::make_pair(100, 5));
    n.Reset();
    CHECK(calls.size() == 3 && calls[2] == std::make_pair(1, 100));
    n.RemoveOnChange(id1);
    n.Set(9);
    CHECK(calls.size() == 3 && second == 9);
    n.RemoveOnChange(id1);                                 // removing twice is harmless
    n.RemoveOnChange(9999);
    reg.ResetAll();
    CHECK(second == 1);                                    // ResetAll notifies too

    // a listener may set other variables (and read its own)
    CVar<bool> mirror(reg, "l.mirror", false, "");
    CVar<int> src(reg, "l.src", 0, "");
    src.OnChange([&](const int& now, const int&) { mirror.Set(now > 10); });
    src.Set(50);
    CHECK(mirror.Get());
    src.Set(3);
    CHECK(!mirror.Get());

    // a listener that keeps setting its own variable is cut off instead of recursing forever
    CVar<int> loop(reg, "l.loop", 0, "");
    int depthCalls = 0;
    loop.OnChange([&](const int& now, const int&) { ++depthCalls; loop.Set(now + 1); });
    loop.Set(1);
    CHECK(depthCalls >= 1 && depthCalls <= 9);

    // an exception in a listener does not escape or block the others
    CVar<int> boom(reg, "l.boom", 0, "");
    int after = 0;
    boom.OnChange([](const int&, const int&) { throw std::runtime_error("listener"); });
    boom.OnChange([&](const int& now, const int&) { after = now; });
    CHECK(boom.Set(4) && after == 4 && boom.Get() == 4);

    // restart-required variables do not notify until they change in effect
    CVar<int> rr(reg, "l.rr", 1, "", CVarFlags::RestartRequired);
    int rrCalls = 0;
    rr.OnChange([&](const int&, const int&) { ++rrCalls; });
    rr.Set(2);
    CHECK(rrCalls == 0 && rr.Get() == 1);

    // string variables
    CVar<std::string> s(reg, "l.s", "a", "");
    std::string seen;
    s.OnChange([&](const std::string& now, const std::string& was) { seen = was + "->" + now; });
    s.Set("b");
    CHECK(seen == "a->b");
}

void TestEnumeration() {
    RUN_SUITE("cvar: enumeration and autocomplete");
    CVarRegistry reg;
    CVar<int> a(reg, "render.max_fps", 0, "x");
    CVar<bool> b(reg, "render.vsync", true, "x");
    CVar<float> c(reg, "render.gamma", 2.0f, "x");
    CVar<int> d(reg, "audio.volume", 5, "x");
    CVar<int> e(reg, "audio.mute_on_focus_loss", 0, "x");
    CVar<int> f(reg, "renderer2.x", 0, "x");
    const auto all = reg.Names();
    CHECK(all.size() == 7 && std::is_sorted(all.begin(), all.end()));
    const auto render = reg.Names("render.");
    CHECK(render.size() == 3 && render[0] == "render.gamma" && render[1] == "render.max_fps" && render[2] == "render.vsync");
    CHECK(reg.Names("render").size() == 4);                // "renderer2.x" starts with "render" too
    CHECK(reg.Names("zzz").empty() && reg.Names("render.zzz").empty());
    CHECK(reg.Complete("au").size() == 2);
    CHECK(reg.CompleteCommon("render.m") == "render.max_fps");
    CHECK(reg.CompleteCommon("render.") == "render.");
    CHECK(reg.CompleteCommon("a") == "audio.");
    CHECK(reg.CompleteCommon("nothing") == "nothing");
    CHECK(reg.CompleteCommon("") == "");
    const auto infos = reg.List("audio.");
    CHECK(infos.size() == 2 && infos[0].name == "audio.mute_on_focus_loss" && infos[1].name == "audio.volume");
    d.Set(9);
    CHECK(reg.List("audio.volume").front().modified && reg.List("audio.volume").front().value == "9");
    CHECK(reg.List("").size() == reg.Count());
}

void TestArgs() {
    RUN_SUITE("cvar: command line");
    CVarRegistry reg;
    CVar<int> fps(reg, "r.fps", 60, "", CVarFlags::Persist, 0, 240);
    CVar<bool> vsync(reg, "r.vsync", true, "");
    CVar<bool> windowed(reg, "r.windowed", false, "");
    CVar<float> scale(reg, "r.scale", 1.0f, "");
    CVar<std::string> name(reg, "p.name", "none", "");
    CVar<int> locked(reg, "sys.locked", 1, "", CVarFlags::ReadOnly);
    CVar<int> cheaty(reg, "dev.cheaty", 0, "", CVarFlags::Cheat);
    CVar<int> msaa(reg, "gfx.msaa", 2, "", CVarFlags::RestartRequired);

    const char* argv1[] = {"game.exe", "+r.fps=144", "+r.vsync", "off", "+r.windowed", "+r.scale", "1.5", "+p.name=Zed", "--other", "plain", nullptr};
    std::vector<std::string> errors;
    int applied = reg.ApplyArgs(9, argv1, &errors);
    CHECK(applied == 5 && errors.empty());
    CHECK(fps.Get() == 144 && !vsync.Get() && windowed.Get() && scale.Get() == 1.5f && name.Get() == "Zed");

    // bad input is reported, the rest still applies
    const char* argv2[] = {"+r.fps=abc", "+sys.locked=2", "+dev.cheaty=1", "+r.scale", "+p.name=", "+=5", "+", "+bad name=3", "+r.fps=9999"};
    errors.clear();
    applied = reg.ApplyArgs(9, argv2, &errors);
    CHECK(fps.Get() == 240);                               // clamped, and accepted
    CHECK(name.Get().empty());                             // `+name=` sets an empty string
    CHECK(locked.Get() == 1 && cheaty.Get() == 0);
    bool sawParse = false, sawRO = false, sawCheat = false, sawMissing = false, sawEmpty = false;
    for (auto& e : errors) {
        sawParse = sawParse || (Contains(e, "r.fps") && Contains(e, "whole number"));
        sawRO = sawRO || Contains(e, "read-only");
        sawCheat = sawCheat || Contains(e, "cheat");
        sawMissing = sawMissing || Contains(e, "missing value");
        sawEmpty = sawEmpty || Contains(e, "empty variable name");
    }
    CHECK(sawParse && sawRO && sawCheat && sawMissing && sawEmpty);
    CHECK(applied == 2 && errors.size() >= 5);

    // robust to nonsense argv
    const char* argv3[] = {nullptr, "", "+", "++x", "x=1", "+r.fps=1=2"};
    errors.clear();
    reg.ApplyArgs(6, argv3, &errors);
    CHECK(reg.ApplyArgs(0, nullptr, nullptr) == 0);

    // a restart-required variable takes the value at once from the command line
    const char* argv4[] = {"+gfx.msaa=8"};
    CHECK(reg.ApplyArgs(1, argv4) == 1 && msaa.Get() == 8 && !msaa.NeedsRestart());

    // `+name value` is not swallowed for a bool when the next word is not a bool
    windowed.Set(false);
    const char* argv5[] = {"+r.windowed", "level1.map"};
    CHECK(reg.ApplyArgs(2, argv5) == 1 && windowed.Get());

    // overrides for names that are not registered yet wait for them
    const char* argv6[] = {"+late.value=7", "+late.flag", "+late.text=hi"};
    errors.clear();
    CHECK(reg.ApplyArgs(3, argv6, &errors) == 0);
    CHECK(errors.size() == 1 && Contains(errors[0], "late.flag"));     // no value and the type is unknown
    const auto pending = reg.UnresolvedOverrides();
    CHECK(pending.size() == 2 && Has(pending, "late.value") && Has(pending, "late.text"));
    CVar<int> late(reg, "late.value", 1, "");
    CHECK(late.Get() == 7);
    CVar<std::string> lateText(reg, "late.text", "x", "");
    CHECK(lateText.Get() == "hi" && reg.UnresolvedOverrides().empty());
    const char* argv7[] = {"+later.one=1"};
    reg.ApplyArgs(1, argv7);
    CHECK(reg.UnresolvedOverrides().size() == 1);
    reg.ClearUnresolvedOverrides();
    CVar<int> laterOne(reg, "later.one", 5, "");
    CHECK(laterOne.Get() == 5);                                         // cleared before it registered
    const char* argv8[] = {"+later.bad=notanumber"};
    reg.ApplyArgs(1, argv8);
    CVar<int> laterBad(reg, "later.bad", 5, "");
    CHECK(laterBad.Get() == 5 && Contains(reg.RegistrationErrors().back(), "later.bad"));
}

void TestExecute() {
    RUN_SUITE("cvar: console lines");
    CVarRegistry reg;
    CVar<int> fps(reg, "render.max_fps", 0, "Frame cap (0 = off)", CVarFlags::Persist, 0, 1000);
    CVar<bool> vsync(reg, "render.vsync", true, "Wait for the display");
    CVar<float> gamma(reg, "render.gamma", 2.2f, "Display gamma", CVarFlags::None, 1.0f, 3.0f);
    CVar<std::string> name(reg, "player.name", "Ada", "Display name");
    CVar<int> locked(reg, "sys.build", 7, "Build", CVarFlags::ReadOnly);
    CVar<int> god(reg, "dev.god", 0, "God mode", CVarFlags::Cheat);
    CVar<int> msaa(reg, "gfx.msaa", 4, "Samples", CVarFlags::RestartRequired);

    CHECK(reg.Execute("").empty() && reg.Execute("   ").empty() && reg.Execute("# a comment").empty() && reg.Execute("// c").empty());
    std::string r = reg.Execute("render.max_fps");
    CHECK(Contains(r, "render.max_fps = 0") && Contains(r, "Frame cap") && Contains(r, "int") && Contains(r, "0..1000") && Contains(r, "saved"));
    r = reg.Execute("render.max_fps 120");
    CHECK(r == "render.max_fps = 120 (default 0)" && fps.Get() == 120);
    CHECK(reg.Execute("render.max_fps=60") == "render.max_fps = 60 (default 0)" && fps.Get() == 60);
    CHECK(reg.Execute("render.max_fps = 90") == "render.max_fps = 90 (default 0)" && fps.Get() == 90);
    CHECK(reg.Execute("  render.max_fps    75  ") == "render.max_fps = 75 (default 0)");
    r = reg.Execute("render.max_fps 5000");
    CHECK(Contains(r, "= 1000") && Contains(r, "clamped to 0..1000") && fps.Get() == 1000);
    r = reg.Execute("render.max_fps abc");
    CHECK(r.rfind("error: ", 0) == 0 && Contains(r, "whole number") && fps.Get() == 1000);
    CHECK(reg.Execute("render.max_fps 0") == "render.max_fps = 0");           // back at the default: no "(default ...)"

    CHECK(Contains(reg.Execute("render.vsync off"), "render.vsync = false") && !vsync.Get());
    CHECK(Contains(reg.Execute("render.vsync YES"), "render.vsync = true") && vsync.Get());
    CHECK(Contains(reg.Execute("toggle render.vsync"), "= false") && !vsync.Get());
    CHECK(Contains(reg.Execute("TOGGLE render.vsync"), "= true") && vsync.Get());
    CHECK(Contains(reg.Execute("toggle render.max_fps"), "error: ") && Contains(reg.Execute("toggle render.max_fps"), "not a bool"));
    CHECK(Contains(reg.Execute("toggle"), "error: ") && Contains(reg.Execute("toggle nope"), "unknown variable"));

    CHECK(Contains(reg.Execute("render.gamma 1.8"), "render.gamma = 1.8") && gamma.Get() == 1.8f);
    CHECK(Contains(reg.Execute("render.gamma 9"), "clamped") && gamma.Get() == 3.0f);

    // strings keep their spaces; surrounding quotes are dropped
    CHECK(reg.Execute("player.name Grace Hopper") == "player.name = Grace Hopper (default Ada)" && name.Get() == "Grace Hopper");
    reg.Execute("player.name \"  padded  \"");
    CHECK(name.Get() == "  padded  ");
    reg.Execute("player.name =");
    CHECK(name.Get().empty());
    reg.Execute("player.name \"x\"y\"");
    CHECK(name.Get() == "x\"y");

    // reset
    CHECK(Contains(reg.Execute("reset player.name"), "player.name = Ada") && name.Get() == "Ada");
    CHECK(Contains(reg.Execute("reset nope"), "unknown variable") && Contains(reg.Execute("reset"), "error: "));
    CHECK(Contains(reg.Execute("reset sys.build"), "read-only") && locked.Get() == 7);

    // refusals
    CHECK(Contains(reg.Execute("sys.build 9"), "error: ") && Contains(reg.Execute("sys.build 9"), "read-only") && locked.Get() == 7);
    CHECK(Contains(reg.Execute("dev.god 1"), "cheat") && god.Get() == 0);
    reg.Execute("cheats 1");
    CHECK(Contains(reg.Execute("dev.god 1"), "dev.god = 1") && god.Get() == 1);
    r = reg.Execute("gfx.msaa 8");
    CHECK(Contains(r, "restart required") && Contains(r, "8") && msaa.Get() == 4);
    r = reg.Execute("gfx.msaa");
    CHECK(Contains(r, "restart required") && Contains(r, "gfx.msaa = 4"));
    r = reg.Execute("render.max_fp 5");                       // a typo gets suggestions
    CHECK(Contains(r, "unknown variable 'render.max_fp'") && Contains(r, "render.max_fps"));
    CHECK(Contains(reg.Execute("RENDER.MAX_FPS"), "render.max_fps = "));      // case-insensitive when unambiguous
    CHECK(Contains(reg.Execute("totally.unknown"), "unknown variable") && !Contains(reg.Execute("totally.unknown"), "did you mean"));

    // list
    r = reg.Execute("list render.");
    CHECK(Contains(r, "render.gamma = 3 (default 2.2)") && Contains(r, "render.max_fps = 0") && Contains(r, "render.vsync = true"));
    CHECK(!Contains(r, "player.name"));
    int lines = 0;
    for (char ch : r) if (ch == '\n') ++lines;
    CHECK(lines == 2);                                         // three lines
    CHECK(reg.Execute("list zzz") == "no variables match 'zzz'");
    r = reg.Execute("list");
    CHECK(Contains(r, "cheats = true") && Contains(r, "gfx.msaa = 4 (restart required: 8 after restart)"));
    CHECK(Contains(reg.Execute("help"), "toggle") && Contains(reg.Execute("HELP"), "list"));

    // garbage never throws and never changes anything it should not
    std::mt19937 rng(99);
    const char alphabet[] = "abcXYZ .=\"\\\t\n\r0123456789+-_#/{}[]|\xC3\xA9\xFF";
    bool noThrow = true;
    for (int i = 0; i < 4000; ++i) {
        std::string line;
        const int n = rng() % 40;
        for (int k = 0; k < n; ++k) line += alphabet[rng() % (sizeof(alphabet) - 1)];
        try { reg.Execute(line); } catch (...) { noThrow = false; }
    }
    CHECK(noThrow);
    CHECK(locked.Get() == 7);
    std::string huge(100000, 'a');
    CHECK(Contains(reg.Execute(huge), "unknown variable"));
    CHECK(Contains(reg.Execute("player.name " + huge), "player.name = ") && name.Get() == huge);
}

void TestPrefsRoundTrip() {
    RUN_SUITE("cvar: Prefs persistence");
    Prefs::Clear();
    const fs::path file = fs::temp_directory_path() / ("okay_cvars_" + std::to_string(std::random_device{}()) + ".prefs");
    const std::string path = file.string();
    {
        CVarRegistry reg;
        CVar<int> fps(reg, "p.fps", 60, "", CVarFlags::Persist, 0, 500);
        CVar<float> sens(reg, "p.sens", 0.5f, "", CVarFlags::Persist);
        CVar<bool> invert(reg, "p.invert", false, "", CVarFlags::Persist);
        CVar<std::string> nick(reg, "p.nick", "anon", "", CVarFlags::Persist);
        CVar<int> notSaved(reg, "p.volatile", 1, "");
        CVar<int> msaa(reg, "p.msaa", 2, "", CVarFlags::Persist | CVarFlags::RestartRequired);
        CVar<int> untouched(reg, "p.untouched", 3, "", CVarFlags::Persist);
        fps.Set(144);
        sens.Set(0.123456789f);
        invert.Set(true);
        nick.Set("tab\there\nnewline \\ back\\slash \"quoted\"");
        notSaved.Set(99);
        msaa.Set(8);                                         // pending: the next start gets it
        CHECK(reg.StoreToPrefs() == 5);                      // not volatile, not at default
        CHECK(Prefs::Has("cvar.p.fps") && Prefs::GetString("cvar.p.fps") == "144");
        CHECK(!Prefs::Has("cvar.p.volatile") && !Prefs::Has("cvar.p.untouched"));
        CHECK(Prefs::GetString("cvar.p.msaa") == "8" && Prefs::GetString("cvar.p.invert") == "true");
        CHECK(Prefs::GetInt("cvar.p.fps") == 144);           // readable through the typed Prefs getters as well
        CHECK(Prefs::Save(path));
    }
    // a new run: nothing in memory, the file is loaded, then the variables are registered
    Prefs::Clear();
    CHECK(!Prefs::Has("cvar.p.fps"));
    CHECK(Prefs::Load(path));
    {
        CVarRegistry reg;                                    // a variable registered after the load picks its value up at once
        CVar<int> fps(reg, "p.fps", 60, "", CVarFlags::Persist, 0, 500);
        CVar<float> sens(reg, "p.sens", 0.5f, "", CVarFlags::Persist);
        CVar<bool> invert(reg, "p.invert", false, "", CVarFlags::Persist);
        CVar<std::string> nick(reg, "p.nick", "anon", "", CVarFlags::Persist);
        CVar<int> notSaved(reg, "p.volatile", 1, "");
        CVar<int> msaa(reg, "p.msaa", 2, "", CVarFlags::Persist | CVarFlags::RestartRequired);
        CVar<int> untouched(reg, "p.untouched", 3, "", CVarFlags::Persist);
        CHECK(fps.Get() == 144 && sens.Get() == 0.123456789f && invert.Get());
        CHECK(nick.Get() == "tab\there\nnewline \\ back\\slash \"quoted\"");
        CHECK(notSaved.Get() == 1 && untouched.Get() == 3);
        CHECK(msaa.Get() == 8 && !msaa.NeedsRestart());      // the start applies it
        CHECK(fps.Default() == 60);                          // the default itself is untouched
    }
    {
        // variables registered BEFORE the file is read (static initialisation) get it from LoadFromPrefs
        Prefs::Clear();
        CVarRegistry reg;
        CVar<int> fps(reg, "p.fps", 60, "", CVarFlags::Persist, 0, 500);
        CHECK(fps.Get() == 60);
        CHECK(reg.LoadPersistent(path) && fps.Get() == 144);
        fps.Set(30);
        CHECK(reg.LoadFromPrefs() == 1 && fps.Get() == 144);         // reloading puts the saved value back
        // SavePersistent writes through Prefs; a missing value is deleted when the variable is back at its default
        fps.Reset();
        CHECK(reg.SavePersistent(path));
        CHECK(!Prefs::Has("cvar.p.fps"));
        Prefs::Clear();
        CHECK(Prefs::Load(path) && !Prefs::Has("cvar.p.fps"));
    }
    {
        // a value that no longer parses or is out of range is handled, not fatal
        Prefs::Clear();
        Prefs::SetString("cvar.q.n", "not a number");
        Prefs::SetString("cvar.q.m", "100000");
        Prefs::SetString("cvar.q.f", "nan");
        CVarRegistry reg;
        CVar<int> n(reg, "q.n", 4, "", CVarFlags::Persist);
        CVar<int> m(reg, "q.m", 4, "", CVarFlags::Persist, 0, 10);
        CVar<float> f(reg, "q.f", 1.5f, "", CVarFlags::Persist);
        CHECK(n.Get() == 4 && m.Get() == 10 && f.Get() == 1.5f);
        Prefs::SetString("cvar.q.n", "12");
        CHECK(reg.LoadFromPrefs() == 3 || reg.LoadFromPrefs() >= 1);
        CHECK(n.Get() == 12);
    }
    {
        // values set from the command line are for this run only
        Prefs::Clear();
        CVarRegistry reg;
        CVar<int> fps(reg, "c.fps", 60, "", CVarFlags::Persist);
        CVar<int> other(reg, "c.other", 1, "", CVarFlags::Persist);
        const char* argv[] = {"+c.fps=200"};
        CHECK(reg.ApplyArgs(1, argv) == 1 && fps.Get() == 200);
        other.Set(5);
        CHECK(reg.StoreToPrefs() == 1 && !Prefs::Has("cvar.c.fps") && Prefs::Has("cvar.c.other"));
        fps.Set(210);                                        // a later explicit change counts as the user's own
        CHECK(reg.StoreToPrefs() == 2 && Prefs::GetInt("cvar.c.fps") == 210);
    }
    Prefs::Clear();
    std::error_code ec;
    fs::remove(file, ec);
}

void TestThreads() {
    RUN_SUITE("cvar: threads");
    CVarRegistry reg;
    CVar<int> n(reg, "t.n", 0, "", CVarFlags::None, 0, 1000000);
    CVar<std::string> s(reg, "t.s", "start", "");
    CVar<float> f(reg, "t.f", 0.0f, "");
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 3; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < 3000; ++i) {
                n.Set(i % 1000);
                s.Set("v" + std::to_string(t) + "_" + std::to_string(i));
                f.Set((float)i);
                const int v = n.Get();
                if (v < 0 || v >= 1000) ++bad;
                const std::string cur = s.Get();
                if (cur.empty() || (cur != "start" && cur[0] != 'v')) ++bad;
                reg.Execute("t.n " + std::to_string(i % 500));
                reg.Names("t.");
                CVarInfo info; reg.Info("t.s", info);
            }
        });
    std::vector<std::thread> regs;
    std::atomic<int> sameEntry{0};
    for (int t = 0; t < 3; ++t)
        regs.emplace_back([&] {
            for (int i = 0; i < 200; ++i) {
                CVar<int> c(reg, "reg.shared" + std::to_string(i % 10), 1, "");
                if (c.Valid()) ++sameEntry;
            }
        });
    for (auto& t : ts) t.join();
    for (auto& t : regs) t.join();
    CHECK(bad.load() == 0 && sameEntry.load() == 600);
    CHECK(reg.Names("reg.").size() == 10);

    // listeners fired from several threads
    CVar<int> watched(reg, "t.watched", 0, "");
    std::atomic<int> fired{0};
    watched.OnChange([&](const int&, const int&) { ++fired; });
    std::vector<std::thread> setters;
    for (int t = 0; t < 4; ++t)
        setters.emplace_back([&, t] { for (int i = 1; i <= 500; ++i) watched.Set(t * 1000 + i); });
    for (auto& t : setters) t.join();
    CHECK(fired.load() > 0 && fired.load() <= 2000);
}

void TestGlobal() {
    RUN_SUITE("cvar: global registry");
    static CVar<int> g_test("test.global.counter", 3, "a global", CVarFlags::None, 0, 10);
    CHECK(g_test.Valid() && g_test.Get() == 3);
    CHECK(CVarRegistry::Global().Exists("test.global.counter") && CVarRegistry::Global().Exists("cheats"));
    CHECK(CVarRegistry::Global().Execute("test.global.counter 8") == "test.global.counter = 8 (default 3)" && g_test.Get() == 8);
    CHECK(&CVarRegistry::Global() == &CVarRegistry::Global());
    // a second registry is independent
    CVarRegistry other;
    CHECK(!other.Exists("test.global.counter") && other.Count() == 1);
}

} // namespace

int main() {
    RUN_SUITE("cvars");
    TestParsing();
    TestRegistrationAndTypes();
    TestClampAndFlags();
    TestListeners();
    TestEnumeration();
    TestArgs();
    TestExecute();
    TestPrefsRoundTrip();
    TestThreads();
    TestGlobal();
    TEST_MAIN_RESULT();
}
