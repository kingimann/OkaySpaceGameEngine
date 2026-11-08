// Localization formatting and plural rules, typed Prefs, FixedTimestep.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Localization.hpp"
#include "okay/Core/Prefs.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
using PF = Localization::PluralForm;

static std::string Form(const char* lang, double n) { return Localization::PluralName(Localization::Plural(lang, n)); }
static const char* const NB = "\xC2\xA0";
static const char* const NNB = "\xE2\x80\xAF";

int main() {
    RUN_SUITE("loc_prefs_time");
    Localization::Reset();

    // ---- Plural rules ----
    {
        CHECK(Form("en", 1) == "one"); CHECK(Form("en", 0) == "other"); CHECK(Form("en", 2) == "other");
        CHECK(Form("en", 1.5) == "other"); CHECK(Form("en", -1) == "one");
        CHECK(Form("de", 1) == "one"); CHECK(Form("de", 5) == "other");
        CHECK(Form("fr", 0) == "one"); CHECK(Form("fr", 1.5) == "one"); CHECK(Form("fr", 2) == "other");
        CHECK(Form("fr", 1000000) == "many"); CHECK(Form("fr", 2000000) == "many");
        CHECK(Form("pt-BR", 0) == "one"); CHECK(Form("pt-BR", 1) == "one"); CHECK(Form("pt-BR", 2) == "other");
        CHECK(Form("pt-PT", 0) == "other"); CHECK(Form("pt-PT", 1) == "one");
        CHECK(Form("es", 1) == "one"); CHECK(Form("es", 0) == "other"); CHECK(Form("es", 1000000) == "many");
        CHECK(Form("es", 1000) == "other");
        // Russian
        CHECK(Form("ru", 1) == "one"); CHECK(Form("ru", 21) == "one"); CHECK(Form("ru", 11) == "many");
        CHECK(Form("ru", 2) == "few"); CHECK(Form("ru", 4) == "few"); CHECK(Form("ru", 22) == "few");
        CHECK(Form("ru", 12) == "many"); CHECK(Form("ru", 14) == "many"); CHECK(Form("ru", 5) == "many");
        CHECK(Form("ru", 0) == "many"); CHECK(Form("ru", 100) == "many"); CHECK(Form("ru", 101) == "one");
        CHECK(Form("ru", 1.5) == "other");
        // Polish
        CHECK(Form("pl", 1) == "one"); CHECK(Form("pl", 2) == "few"); CHECK(Form("pl", 22) == "few");
        CHECK(Form("pl", 5) == "many"); CHECK(Form("pl", 12) == "many"); CHECK(Form("pl", 21) == "many");
        CHECK(Form("pl", 0) == "many"); CHECK(Form("pl", 1.5) == "other");
        // Arabic
        CHECK(Form("ar", 0) == "zero"); CHECK(Form("ar", 1) == "one"); CHECK(Form("ar", 2) == "two");
        CHECK(Form("ar", 3) == "few"); CHECK(Form("ar", 10) == "few"); CHECK(Form("ar", 11) == "many");
        CHECK(Form("ar", 99) == "many"); CHECK(Form("ar", 100) == "other"); CHECK(Form("ar", 102) == "other");
        CHECK(Form("ar", 103) == "few"); CHECK(Form("ar", 0.5) == "other");
        // No plural
        for (const char* l : {"ja", "zh", "zh-CN", "ko", "vi"}) { CHECK(Form(l, 1) == "other"); CHECK(Form(l, 5) == "other"); }
        // Czech
        CHECK(Form("cs", 1) == "one"); CHECK(Form("cs", 3) == "few"); CHECK(Form("cs", 5) == "other"); CHECK(Form("cs", 1.5) == "many");
        // Case and region don't matter, NaN is other, rules can still be overridden.
        CHECK(Form("RU", 2) == "few"); CHECK(Form("ru_RU.UTF-8", 2) == "few");
        CHECK(Form("en", NAN) == "other");
        Localization::SetPluralRule("en", [](double) { return PF::Zero; });
        CHECK(Form("en", 1) == "zero");
        Localization::SetPluralRule("en", nullptr);
        CHECK(Form("en", 1) == "one");
    }

    // ---- PluralText over a table ----
    {
        StringTable t;
        t.AddLanguage("en", "English"); t.AddLanguage("ru", "Russian"); t.AddLanguage("ar", "Arabic");
        t.AddLanguage("ja", "Japanese"); t.defaultLanguage = "en";
        t.Set("coins.one", "en", "{count} coin"); t.Set("coins.other", "en", "{0} coins");
        t.Set("coins.one", "ru", "{count} moneta"); t.Set("coins.few", "ru", "{count} monety");
        t.Set("coins.many", "ru", "{count} monet");   // no "other": falls back to many/en other
        t.Set("coins.other", "ja", "{0}枚");
        t.Set("coins.zero", "ar", "no coins");
        t.Set("coins.other", "ar", "{0} c");
        t.Set("items", "en", "{0, plural, one {# item} other {# items}} in {1}");
        t.Set("only.other", "en", "{0} only");
        Localization::SetTable(t);
        CHECK(Localization::PluralText("coins", 1) == "1 coin");
        CHECK(Localization::PluralText("coins", 0) == "0 coins");
        CHECK(Localization::PluralText("coins", 7) == "7 coins");
        CHECK(Localization::PluralText("only", 1) == "1 only");     // form missing: other
        CHECK(Localization::SetLanguage("ru"));
        CHECK(Localization::PluralText("coins", 1) == "1 moneta");
        CHECK(Localization::PluralText("coins", 3) == "3 monety");
        CHECK(Localization::PluralText("coins", 11) == "11 monet");
        CHECK(Localization::PluralText("coins", 1.5) == "1.5 coins");   // ru "other" missing: default language
        CHECK(Localization::PluralText("only", 4) == "4 only");
        CHECK(Localization::SetLanguage("ja"));
        CHECK(Localization::PluralText("coins", 1) == "1枚");
        CHECK(Localization::SetLanguage("ar"));
        CHECK(Localization::PluralText("coins", 0) == "no coins");
        CHECK(Localization::PluralText("coins", 2) == "2 c");           // no "two": the language's "other"
        Localization::SetLanguage("en");
        CHECK(Localization::PluralText("items", 2, {LocArg("box")}) == "2 items in box");
        CHECK(Localization::PluralText("items", 1, {LocArg("box")}) == "1 item in box");
        CHECK(Localization::PluralText("nothing.here", 3) == "nothing.here");
    }

    // ---- Fallback chain ----
    {
        StringTable t;
        t.AddLanguage("en", "English"); t.AddLanguage("pt", "Portugues"); t.AddLanguage("pt-BR", "Brasil");
        t.defaultLanguage = "en";
        t.Set("a", "en", "A-en"); t.Set("a", "pt", "A-pt");
        t.Set("b", "en", "B-en");
        t.Set("c", "pt-BR", "C-br");
        Localization::SetTable(t);
        CHECK(Localization::SetLanguage("pt-BR"));
        CHECK(Localization::Get("a") == "A-pt");      // pt-BR -> pt -> en
        CHECK(Localization::Get("b") == "B-en");
        CHECK(Localization::Get("c") == "C-br");
        CHECK(Localization::Get("zzz") == "zzz");
        std::string out;
        CHECK(Localization::TryGet("a", out) && out == "A-pt");
        CHECK(!Localization::TryGet("zzz", out));
        CHECK(Localization::GetOr("zzz", "dflt") == "dflt");
        CHECK(Localization::Localize("{0} of {1}", 1) == "1 of {1}");   // key not found: shown as is; a hole without an argument stays
        // 64-bit integers keep every digit.
        t.Set("id", "en", "id {0}");
        Localization::SetTable(t);
        Localization::SetLanguage("en");
        CHECK(Localization::Localize("id", 9007199254740993LL) == "id 9007199254740993");
        CHECK(Localization::Localize("id", 18446744073709551615ULL) == "id 18446744073709551615");
    }

    // ---- Number, currency, percent ----
    {
        CHECK(Localization::FormatNumber(1234567.891, 2, "en") == "1,234,567.89");
        CHECK(Localization::FormatNumber(1234567.891, 2, "en-US") == "1,234,567.89");
        CHECK(Localization::FormatNumber(1234567.891, 2, "de") == "1.234.567,89");
        CHECK(Localization::FormatNumber(1234567.891, 2, "fr") == std::string("1") + NNB + "234" + NNB + "567,89");
        CHECK(Localization::FormatNumber(1234567.891, 2, "ru") == std::string("1") + NB + "234" + NB + "567,89");
        CHECK(Localization::FormatNumber(1234, 0, "es") == "1234");        // es / pl group from 5 digits
        CHECK(Localization::FormatNumber(12345, 0, "es") == "12.345");
        CHECK(Localization::FormatNumber(1234, 0, "pt-BR") == "1.234");
        CHECK(Localization::FormatNumber(1234.5, 1, "ja") == "1,234.5");
        CHECK(Localization::FormatNumber(999, 0, "en") == "999");
        CHECK(Localization::FormatNumber(1000, 0, "en") == "1,000");
        CHECK(Localization::FormatNumber(0.6, 0, "en") == "1");
        CHECK(Localization::FormatNumber(-1234.5, 1, "en") == "-1,234.5");
        CHECK(Localization::FormatNumber(-0.001, 2, "en") == "0.00");      // no "-0.00"
        CHECK(Localization::FormatNumber(1234.5, 0, "en_GB.UTF-8") == "1,235");
        CHECK(Localization::FormatNumber(NAN, 2, "en") == "NaN");
        CHECK(Localization::FormatCurrency(1234.5, "USD", "en") == "$1,234.50");
        CHECK(Localization::FormatCurrency(-1234.5, "usd", "en") == "-$1,234.50");
        CHECK(Localization::FormatCurrency(1234.5, "EUR", "de") == std::string("1.234,50") + NB + "\xE2\x82\xAC");
        CHECK(Localization::FormatCurrency(1234.5, "EUR", "fr") == std::string("1") + NNB + "234,50" + NB + "\xE2\x82\xAC");
        CHECK(Localization::FormatCurrency(1234.5, "BRL", "pt-BR") == std::string("R$") + NB + "1.234,50");
        CHECK(Localization::FormatCurrency(1234.5, "EUR", "es") == std::string("1.234,50") + NB + "\xE2\x82\xAC");
        CHECK(Localization::FormatCurrency(1500, "JPY", "ja") == "\xEF\xBF\xA5" "1,500");
        CHECK(Localization::FormatCurrency(1500.6, "JPY", "en") == "\xC2\xA5" "1,501");
        CHECK(Localization::FormatCurrency(1.2345, "KWD", "en") == "KWD" + std::string(NB) + "1.235");
        CHECK(Localization::FormatCurrency(5, "XYZ", "en") == "XYZ" + std::string(NB) + "5.00");
        CHECK(Localization::FormatCurrency(1234.5, "GBP", "en-GB") == "\xC2\xA3" "1,234.50");
        CHECK(Localization::FormatPercent(0.256, 1, "en") == "25.6%");
        CHECK(Localization::FormatPercent(0.5, 0, "en") == "50%");
        CHECK(Localization::FormatPercent(0.5, 0, "de") == std::string("50") + NB + "%");
        CHECK(Localization::FormatPercent(0.256, 1, "fr") == std::string("25,6") + NNB + "%");
        CHECK(Localization::FormatPercent(0.5, 0, "pl") == "50%");
        CHECK(Localization::FormatPercent(-0.5, 0, "en") == "-50%");
        // Uses the current language when none is given.
        StringTable t; t.AddLanguage("en"); t.AddLanguage("de"); t.defaultLanguage = "en";
        Localization::SetTable(t);
        CHECK(Localization::FormatNumber(1234.5, 1) == "1,234.5");
        Localization::SetLanguage("de");
        CHECK(Localization::FormatNumber(1234.5, 1) == "1.234,5");
        Localization::SetLanguage(Localization::kPseudo);
        CHECK(Localization::FormatNumber(1234.5, 1) == "1,234.5");   // pseudo formats like the default language
        Localization::SetLanguage("en");
    }

    // ---- Dates and times ----
    {
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "en") == "3/5/2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "en-GB") == "05/03/2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "de") == "05.03.2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "fr") == "05/03/2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "ja") == "2025/3/5");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Long, "en") == "March 5, 2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Long, "en-GB") == "5 March 2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Long, "de") == "5. M\xC3\xA4rz 2025");
        CHECK(Localization::FormatDate(2025, 8, 1, Localization::DateStyle::Long, "fr") == "1 ao\xC3\xBBt 2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Long, "es") == "5 de marzo de 2025");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Long, "ja") == "2025\xE5\xB9\xB4" "3\xE6\x9C\x88" "5\xE6\x97\xA5");
        CHECK(Localization::FormatDate(2024, 2, 29, Localization::DateStyle::Short, "en") == "2/29/2024");
        CHECK(Localization::FormatDate(2025, 2, 29, Localization::DateStyle::Short, "en") == "2025-02-29");   // not a date: ISO text
        CHECK(Localization::FormatDate(2025, 13, 1, Localization::DateStyle::Long, "fr") == "2025-13-01");
        CHECK(Localization::FormatDate(2025, 3, 5, Localization::DateStyle::Short, "xx") == "2025-03-05");
        CHECK(Localization::FormatTime(15, 5, "en") == "3:05 PM");
        CHECK(Localization::FormatTime(0, 7, "en-US") == "12:07 AM");
        CHECK(Localization::FormatTime(12, 0, "en") == "12:00 PM");
        CHECK(Localization::FormatTime(15, 5, "de") == "15:05");
        CHECK(Localization::FormatTime(15, 5, "en-GB") == "15:05");
        CHECK(Localization::FormatTime(9, 5, "es") == "9:05");
        CHECK(Localization::FormatTime(99, -4, "de") == "23:00");
    }

    // ---- Typed Prefs ----
    {
        Prefs::DeleteAll();
        CHECK(!Prefs::HasKey("a"));
        std::vector<std::string> heard;
        const int id = Prefs::AddListener([&](const std::string& k) { heard.push_back(k); });
        Prefs::SetInt("level", 7);
        CHECK(Prefs::HasKey("level") && Prefs::GetInt("level") == 7);
        Prefs::SetInt("level", 7);                       // same value: silent
        CHECK(heard.size() == 1 && heard[0] == "level");
        Prefs::SetInt("level", 8);
        CHECK(heard.size() == 2);
        Prefs::SetBool("sound", true);
        CHECK(Prefs::GetBool("sound") && Prefs::GetString("sound") == "1");
        Prefs::SetBool("sound", false);
        CHECK(!Prefs::GetBool("sound", true));
        CHECK(Prefs::GetBool("missing", true) && !Prefs::GetBool("missing"));
        Prefs::SetString("b1", "TRUE"); CHECK(Prefs::GetBool("b1"));
        Prefs::SetString("b1", "Off");  CHECK(!Prefs::GetBool("b1", true));
        Prefs::SetString("b1", "2");    CHECK(Prefs::GetBool("b1"));
        Prefs::SetString("b1", "0.0");  CHECK(!Prefs::GetBool("b1", true));
        Prefs::SetString("b1", "perhaps"); CHECK(Prefs::GetBool("b1", true) && !Prefs::GetBool("b1", false));
        Prefs::SetFloat("vol", 0.1f); CHECK(Prefs::GetFloat("vol") == 0.1f);
        Prefs::SetVec3("spawn", Vec3(1.5f, -2.25f, 1e-7f));
        Vec3 v = Prefs::GetVec3("spawn");
        CHECK(v.x == 1.5f && v.y == -2.25f && v.z == 1e-7f);
        CHECK(Prefs::GetVec3("none", Vec3(1, 2, 3)).z == 3.0f);
        Prefs::SetString("spawn2", "1,2");              CHECK(Prefs::GetVec3("spawn2", Vec3(9, 9, 9)).x == 9.0f);
        Prefs::SetString("spawn2", "1,2,x");            CHECK(Prefs::GetVec3("spawn2", Vec3(9, 9, 9)).x == 9.0f);
        Prefs::SetString("spawn2", "1, 2 ,3");          CHECK(Prefs::GetVec3("spawn2").y == 2.0f);
        Prefs::SetColor("tint", Color(0.25f, 0.5f, 0.75f, 0.125f));
        Color c = Prefs::GetColor("tint");
        CHECK(c.r == 0.25f && c.g == 0.5f && c.b == 0.75f && c.a == 0.125f);
        Prefs::SetString("tint2", "0.1,0.2,0.3");       CHECK(Prefs::GetColor("tint2").a == 1.0f);
        Prefs::SetString("tint2", "red");               CHECK(Prefs::GetColor("tint2", Color::Blue).b == 1.0f);
        CHECK(Prefs::GetColor("none", Color::Green).g == 1.0f);

        heard.clear();
        Prefs::DeleteKey("level");
        CHECK(!Prefs::HasKey("level") && Prefs::GetInt("level", -1) == -1);
        CHECK(heard.size() == 1 && heard[0] == "level");
        Prefs::DeleteKey("level");                       // already gone: silent
        CHECK(heard.size() == 1);

        // The file format is the same one older builds read.
        const std::string path = (std::filesystem::temp_directory_path() / "okay_prefs_typed.okayprefs").string();
        CHECK(Prefs::Save(path));
        {
            std::ifstream f(path); std::string head; std::getline(f, head);
            CHECK(head == "okayprefs 2");
        }
        Prefs::DeleteAll();
        heard.clear();
        CHECK(Prefs::Load(path));
        CHECK(heard.size() == 1 && heard[0].empty());
        CHECK(Prefs::GetVec3("spawn").y == -2.25f);
        CHECK(Prefs::GetColor("tint").a == 0.125f);
        CHECK(!Prefs::GetBool("sound", true));
        // An old file without typed keys still reads.
        { std::ofstream f(path); f << "okayprefs 2\nscore\t42\nmute\ttrue\n#end\n"; }
        CHECK(Prefs::Load(path));
        CHECK(Prefs::GetInt("score") == 42 && Prefs::GetBool("mute"));
        std::filesystem::remove(path);

        Prefs::SetInt("x", 1); Prefs::SetInt("y", 2);   // plus score and mute from the file
        heard.clear();
        Prefs::DeleteAll();
        CHECK(heard.size() == 4);
        Prefs::RemoveListener(id);
        Prefs::SetInt("x", 5);
        CHECK(heard.size() == 4);
        Prefs::DeleteAll();
    }

    // ---- Fixed timestep ----
    {
        Time::SetTimeScale(1.0f); Time::SetFrozen(false);
        CHECK_NEAR(Time::FixedDeltaTime(), 0.02f, 1e-7);
        FixedTimestep f;
        CHECK_NEAR(f.Step(), 0.02f, 1e-7);
        CHECK(f.MaxSteps() == 5);
        // One 60 Hz frame is less than a step; three of them are 3 * 16.67 ms = 50 ms: 2 steps so far.
        int total = 0;
        for (int i = 0; i < 60; ++i) total += f.Advance(1.0f / 60.0f);
        CHECK(total == 50);                                  // one simulated second = 50 steps
        CHECK(f.TotalSteps() == 50);
        CHECK(f.Alpha() >= 0.0f && f.Alpha() < 1.0f);
        // Same count at 144 and 30 fps over 10 s (plus a bit so the last step is not on an edge).
        for (float fps : {30.0f, 144.0f, 240.0f, 20.0f}) {
            FixedTimestep g;
            int n = 0;
            const int frames = (int)std::lround(10.0f * fps);
            for (int i = 0; i < frames; ++i) n += g.Advance(1.0f / fps);
            CHECK(n >= 499 && n <= 500);
        }
        // Alpha is the leftover fraction.
        FixedTimestep h(0.1f);
        CHECK(h.Advance(0.25f) == 2);
        CHECK_NEAR(h.Alpha(), 0.5f, 1e-4);
        CHECK_NEAR(h.Accumulated(), 0.05f, 1e-4);
        CHECK(h.Advance(0.0f) == 0);
        CHECK(h.Advance(0.05f) == 1);
        CHECK_NEAR(h.Alpha(), 0.0f, 1e-3);
        // Catch-up is capped and the surplus dropped.
        FixedTimestep c(0.1f, 3);
        CHECK(c.Advance(10.0f) == 3);
        CHECK(c.LastSteps() == 3);
        CHECK_NEAR(c.DroppedTime(), 9.7, 1e-4);
        CHECK(c.Advance(0.0f) == 0);                         // nothing owed afterwards
        CHECK(c.Advance(0.1f) == 1);
        // Bad times add nothing.
        FixedTimestep b(0.1f);
        CHECK(b.Advance(-5.0f) == 0);
        CHECK(b.Advance(NAN) == 0);
        CHECK(b.Advance(INFINITY) == 0);
        CHECK_NEAR(b.Accumulated(), 0.0f, 1e-9);
        // Run calls once per step with the step length.
        FixedTimestep r(0.5f);
        float sum = 0.0f; int calls = 0;
        CHECK(r.Run(1.6f, [&](float dt) { sum += dt; ++calls; }) == 3);
        CHECK(calls == 3); CHECK_NEAR(sum, 1.5f, 1e-6);
        r.Reset();
        CHECK(r.TotalSteps() == 0 && r.Accumulated() == 0.0f);
        // Bad configuration is replaced by sane values.
        FixedTimestep z(-1.0f, 0);
        CHECK_NEAR(z.Step(), 0.02f, 1e-7); CHECK(z.MaxSteps() == 1);
        Time::SetFixedDeltaTime(0.0f); Time::SetFixedDeltaTime(NAN);
        CHECK_NEAR(Time::FixedDeltaTime(), 0.02f, 1e-7);
        Time::SetFixedDeltaTime(0.01f);
        CHECK_NEAR(FixedTimestep().Step(), 0.01f, 1e-7);
        Time::SetFixedDeltaTime(0.02f);

        // Time scale and pause: fed with Time::DeltaTime it follows them; unscaled does not.
        FixedTimestep s, u;
        int ns = 0, nu = 0;
        Time::SetTimeScale(0.5f);
        for (int i = 0; i < 100; ++i) { Time::Step(0.02f); ns += s.Advance(Time::DeltaTime()); nu += u.Advance(Time::UnscaledDeltaTime()); }
        CHECK(ns == 50); CHECK(nu == 100);
        Time::SetFrozen(true);                               // paused: no steps
        for (int i = 0; i < 100; ++i) { Time::Step(0.02f); ns += s.Advance(Time::DeltaTime()); nu += u.Advance(Time::UnscaledDeltaTime()); }
        CHECK(ns == 50); CHECK(nu == 200);
        Time::SetFrozen(false);
        Time::Step(0.02f);
        CHECK_NEAR(Time::DeltaTime(), 0.01f, 1e-6);          // the remembered scale is back
        Time::SetTimeScale(2.0f);
        Time::Step(0.02f);
        CHECK_NEAR(Time::DeltaTime(), 0.04f, 1e-6);
        Time::SetTimeScale(-3.0f);                           // negative clamps to 0
        Time::Step(0.02f);
        CHECK(Time::DeltaTime() == 0.0f);
        Time::SetTimeScale(1.0f);

        // Scene physics keeps its own accumulator: stepping a scene is unchanged by FixedTimestep.
        Scene sc("p");
        sc.fixedTimeStep = Scene::kDefaultFixedStep;
        sc.interpolatePhysics = false;
        sc.Start();
        FixedTimestep mirror(Scene::kDefaultFixedStep, 5);
        int physSteps = 0, mirrorSteps = 0;
        for (int i = 0; i < 120; ++i) { sc.Update(1.0f / 60.0f); physSteps += sc.PhysicsStepsLastFrame(); mirrorSteps += mirror.Advance(1.0f / 60.0f); }
        CHECK(physSteps == mirrorSteps);
    }

    TEST_MAIN_RESULT();
}
