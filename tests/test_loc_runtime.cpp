// Localization runtime, second pass: keys for names in any script, language matching that
// keeps Traditional and Simplified Chinese apart, parent fallbacks, the plural rules of
// more languages, {n, number} / {x, select} / {n, currency, CODE} placeholders, CSV in
// UTF-16 and with differently written language headers, and atomic table files
// (docs/localization.md, "What changed in the second pass").
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/LocTable.hpp"
#include "okay/Core/Localization.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string Form(const char* lang, double n) { return Localization::PluralName(Localization::Plural(lang, n)); }

StringTable Langs(const std::vector<std::string>& codes) {
    StringTable t;
    for (const auto& c : codes) t.AddLanguage(c);
    t.defaultLanguage = codes.empty() ? "" : codes[0];
    return t;
}

std::string Utf16(const std::string& ascii, bool little) {
    std::string out = little ? "\xFF\xFE" : "\xFE\xFF";
    for (char c : ascii) {
        if (little) { out += c; out += '\0'; }
        else { out += '\0'; out += c; }
    }
    return out;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    RUN_SUITE("loc_runtime");
    Localization::Reset();

    // ---- Keys for names in any script ----
    {
        CHECK(Localization::KeyPart("Old Smith") == "old_smith");
        CHECK(Localization::KeyPart("  A-B_c! ") == "a-b_c_");
        CHECK(Localization::KeyPart("") == "");
        const std::string yamada = "山田", tanaka = "田中", alice = "Алиса", bob = "Боб";
        CHECK(Localization::KeyPart(yamada) == yamada && Localization::KeyPart(alice) == alice);
        CHECK(Localization::KeyPart(yamada) != Localization::KeyPart(tanaka));
        CHECK(Localization::KeyPart(alice) != Localization::KeyPart(bob));
        CHECK(Localization::KeyPart("Mr. 山田") == std::string("mr__") + yamada);
        CHECK(Localization::KeyPart("a\xC2\xA0" "b") == "a_b");                 // a no-break space is a space
        CHECK(Localization::KeyPart("a\xE3\x80\x80" "b") == "a_b");             // so is an ideographic one
        CHECK(Localization::KeyPart("a\xFF" "z\xE5\xB1") == "a_z_");            // broken UTF-8 never reaches a key
        // The key survives the table's two text formats and a lookup.
        StringTable t = Langs({"en", "ja"});
        const std::string key = "speaker." + Localization::KeyPart(yamada);
        t.Set(key, "en", "Yamada");
        t.Set(key, "ja", "山田さん");
        t.Set("speaker." + Localization::KeyPart(tanaka), "en", "Tanaka");
        StringTable back;
        CHECK(back.Parse(t.Write()));
        CHECK(back.Text(key, "en") && *back.Text(key, "en") == "Yamada" && back.Size() == 2);
        StringTable viaCsv = Langs({"en", "ja"});
        CHECK(viaCsv.ImportCsv(t.ExportCsv()));
        CHECK(viaCsv.Text(key, "ja") && *viaCsv.Text(key, "ja") == "山田さん" && viaCsv.Size() == 2);
        Localization::SetTable(t);
        CHECK(Localization::Get(key) == "Yamada");
        // A placeholder named in another script.
        CHECK(Localization::Format("こんにちは、{名前}さん", {LocArg::Named("名前", "太郎")}, "ja") == "こんにちは、太郎さん");
        Localization::Reset();
    }

    // ---- The language that fits the OS locale ----
    {
        Localization::SetTable(Langs({"en", "zh-Hans", "zh-Hant"}));
        CHECK(Localization::PickLanguage({"zh-TW"}) == "zh-Hant");
        CHECK(Localization::PickLanguage({"zh_HK.UTF-8"}) == "zh-Hant");
        CHECK(Localization::PickLanguage({"zh-MO"}) == "zh-Hant");
        CHECK(Localization::PickLanguage({"zh-Hant-TW"}) == "zh-Hant");
        CHECK(Localization::PickLanguage({"zh-Hant"}) == "zh-Hant");
        CHECK(Localization::PickLanguage({"zh-CN"}) == "zh-Hans");
        CHECK(Localization::PickLanguage({"zh_SG"}) == "zh-Hans");
        CHECK(Localization::PickLanguage({"zh-Hans-CN"}) == "zh-Hans");
        CHECK(Localization::PickLanguage({"zh"}) == "zh-Hans");                  // no hint: the first Chinese one
        CHECK(Localization::PickLanguage({"fr-CA", "zh-TW"}) == "zh-Hant");      // the first locale that matches
        Localization::SetTable(Langs({"en", "zh"}));                             // one Chinese table: it serves both
        CHECK(Localization::PickLanguage({"zh-TW"}) == "zh" && Localization::PickLanguage({"zh-CN"}) == "zh");
        Localization::SetTable(Langs({"en", "zh-CN", "zh-TW"}));                 // codes with regions only
        CHECK(Localization::PickLanguage({"zh-Hant-HK"}) == "zh-TW" && Localization::PickLanguage({"zh-Hans"}) == "zh-CN");
        Localization::SetTable(Langs({"en", "fr-FR", "fr", "pt-BR", "pt", "es", "es-419", "sr-Latn", "sr-Cyrl"}));
        CHECK(Localization::PickLanguage({"fr-CA"}) == "fr");                    // no region beats another region
        CHECK(Localization::PickLanguage({"fr-FR"}) == "fr-FR");                 // exact
        CHECK(Localization::PickLanguage({"pt_PT.UTF-8"}) == "pt");
        CHECK(Localization::PickLanguage({"es-419"}) == "es-419" && Localization::PickLanguage({"es-MX"}) == "es");
        CHECK(Localization::PickLanguage({"sr-RS"}) == "sr-Latn" && Localization::PickLanguage({"sr-Cyrl-RS"}) == "sr-Cyrl");
        Localization::SetTable(Langs({"en", "fr-FR", "de"}));
        CHECK(Localization::PickLanguage({"fr-CA"}) == "fr-FR");                 // a regional table language
        CHECK(Localization::PickLanguage({"C", "POSIX", "ko"}) == "en");
        CHECK(Localization::PickLanguage({}) == "en");
        Localization::Reset();
    }

    // ---- A language falls back through its parents before the default ----
    {
        StringTable t = Langs({"en", "zh-Hant", "zh-Hant-TW", "zh-Hans"});
        t.Set("only.en", "en", "english");
        t.Set("hant.only", "zh-Hant", "traditional");
        t.Set("hant.only", "en", "english");
        t.Set("both", "zh-Hant-TW", "taiwan");
        t.Set("both", "zh-Hant", "traditional");
        Localization::SetTable(t);
        CHECK(Localization::SetLanguage("zh-Hant-TW"));
        CHECK(Localization::Get("both") == "taiwan");
        CHECK(Localization::Get("hant.only") == "traditional");                  // zh-Hant-TW -> zh-Hant
        CHECK(Localization::Get("only.en") == "english");                        // ... then the default
        CHECK(Localization::SetLanguage("zh-Hans"));
        CHECK(Localization::Get("hant.only") == "english");                      // Simplified never borrows Traditional
        Localization::Reset();
    }

    // ---- Plural rules of more languages ----
    {
        // Balkan: endings 1 / 2-4 / the rest, except 11-14.
        for (const char* l : {"sr", "hr", "bs"}) {
            CHECK(Form(l, 1) == "one" && Form(l, 21) == "one" && Form(l, 101) == "one" && Form(l, 11) == "other" && Form(l, 111) == "other");
            CHECK(Form(l, 2) == "few" && Form(l, 4) == "few" && Form(l, 22) == "few" && Form(l, 12) == "other" && Form(l, 14) == "other");
            CHECK(Form(l, 5) == "other" && Form(l, 0) == "other" && Form(l, 25) == "other" && Form(l, 1.5) == "other");
        }
        CHECK(Form("sr-Latn", 21) == "one" && Form("hr-HR", 3) == "few");
        CHECK(Form("sl", 1) == "one" && Form("sl", 101) == "one" && Form("sl", 2) == "two" && Form("sl", 102) == "two");
        CHECK(Form("sl", 3) == "few" && Form("sl", 4) == "few" && Form("sl", 5) == "other" && Form("sl", 100) == "other" && Form("sl", 1.5) == "few");
        CHECK(Form("lt", 1) == "one" && Form("lt", 21) == "one" && Form("lt", 11) == "other" && Form("lt", 2) == "few" && Form("lt", 9) == "few");
        CHECK(Form("lt", 12) == "other" && Form("lt", 20) == "other" && Form("lt", 1.5) == "many");
        CHECK(Form("lv", 0) == "zero" && Form("lv", 10) == "zero" && Form("lv", 11) == "zero" && Form("lv", 19) == "zero");
        CHECK(Form("lv", 1) == "one" && Form("lv", 21) == "one" && Form("lv", 2) == "other" && Form("lv", 22) == "other");
        CHECK(Form("ro", 1) == "one" && Form("ro", 0) == "few" && Form("ro", 2) == "few" && Form("ro", 19) == "few" && Form("ro", 101) == "few");
        CHECK(Form("ro", 20) == "other" && Form("ro", 120) == "other" && Form("ro", 1.5) == "few");
        CHECK(Form("he", 1) == "one" && Form("he", 2) == "two" && Form("he", 3) == "other" && Form("he", 0.5) == "one" && Form("he", 20) == "other");
        for (const char* l : {"hi", "bn", "fa", "gu", "zu"}) CHECK(Form(l, 0) == "one" && Form(l, 1) == "one" && Form(l, 0.5) == "one" && Form(l, 2) == "other");
        CHECK(Form("cy", 0) == "zero" && Form("cy", 1) == "one" && Form("cy", 2) == "two" && Form("cy", 3) == "few" && Form("cy", 6) == "many" && Form("cy", 4) == "other");
        CHECK(Form("ga", 1) == "one" && Form("ga", 2) == "two" && Form("ga", 3) == "few" && Form("ga", 6) == "few" && Form("ga", 7) == "many" && Form("ga", 10) == "many" && Form("ga", 11) == "other");
        CHECK(Form("mt", 1) == "one" && Form("mt", 2) == "two" && Form("mt", 0) == "few" && Form("mt", 10) == "few" && Form("mt", 11) == "many" && Form("mt", 19) == "many" && Form("mt", 20) == "other" && Form("mt", 103) == "few");
        CHECK(Form("is", 1) == "one" && Form("is", 21) == "one" && Form("is", 11) == "other" && Form("is", 2) == "other" && Form("is", 1.5) == "one");
        CHECK(Form("mk", 21) == "one" && Form("mk", 11) == "other");
        CHECK(Form("da", 1) == "one" && Form("da", 0) == "other" && Form("da", 0.5) == "one" && Form("da", 2) == "other");
        for (const char* l : {"lo", "my", "km", "jv"}) CHECK(Form(l, 1) == "other" && Form(l, 5) == "other");
        // The rules that were there stay as they were.
        CHECK(Form("en", 1) == "one" && Form("en", 2) == "other" && Form("ru", 21) == "one" && Form("ru", 22) == "few" && Form("ru", 25) == "many");
        CHECK(Form("pl", 5) == "many" && Form("ar", 0) == "zero" && Form("fr", 0) == "one" && Form("ja", 1) == "other");
        // In a sentence.
        const char* pat = "{0, plural, one {# kovanica} few {# kovanice} other {# kovanica}}";
        CHECK(Localization::Format(pat, {21}, "hr") == "21 kovanica");
        CHECK(Localization::Format(pat, {22}, "hr") == "22 kovanice");
        CHECK(Localization::Format(pat, {11}, "hr") == "11 kovanica");
        // A rule you install still wins.
        Localization::SetPluralRule("sr", [](double) { return Localization::PluralForm::Many; });
        CHECK(Form("sr", 1) == "many" && Form("sr-Latn", 1) == "many");
        Localization::SetPluralRule("sr", nullptr);
        CHECK(Form("sr", 1) == "one");
        Localization::Reset();
    }

    // ---- {n, number}, {n, currency, CODE}, {x, select}, and placeholders without an argument ----
    {
        CHECK(Localization::Format("{0, number}", {1234567}, "en") == "1,234,567");
        CHECK(Localization::Format("{0, number}", {1234567}, "de") == "1.234.567");
        CHECK(Localization::Format("{0, number}", {1234567}, "fr") == "1\xE2\x80\xAF" "234\xE2\x80\xAF" "567");
        CHECK(Localization::Format("{0, number}", {1234.5}, "en") == "1,234.5");
        CHECK(Localization::Format("{0, number}", {1234.56789}, "en") == "1,234.568");
        CHECK(Localization::Format("{0, number}", {1234.5}, "de") == "1.234,5");
        CHECK(Localization::Format("{0, number}", {-1234}, "en") == "-1,234");
        CHECK(Localization::Format("{0, number}", {0}, "en") == "0");
        CHECK(Localization::Format("{0, number, integer}", {1234.7}, "en") == "1,235");
        CHECK(Localization::Format("{0, number, percent}", {0.256}, "en") == "26%");
        CHECK(Localization::Format("{0, number, #,##0.00}", {1234.5}, "en") == "1,234.50");
        CHECK(Localization::Format("{0, number, 0.0}", {2}, "en") == "2.0");
        CHECK(Localization::Format("{0, number}", {"42"}, "en") == "42");           // text that is a number
        CHECK(Localization::Format("{score, number}", {LocArg::Named("score", 9876543)}, "en") == "9,876,543");
        CHECK(Localization::Format("{0, currency, USD}", {1234.5}, "en") == "$1,234.50");
        CHECK(Localization::Format("{0, currency, EUR}", {1234.5}, "de") == "1.234,50\xC2\xA0\xE2\x82\xAC");
        CHECK(Localization::Format("{0, currency, JPY}", {1500}, "ja") == "\xEF\xBF\xA5" "1,500");
        CHECK(Localization::Format("{0, number}", {std::nan("")}, "en") == "NaN");
        CHECK(Localization::Format("You have {0, number} coins", {1200000}, "en") == "You have 1,200,000 coins");
        // Select.
        const char* sel = "{who, select, male {He} female {She} other {They}} won";
        CHECK(Localization::Format(sel, {LocArg::Named("who", "female")}, "en") == "She won");
        CHECK(Localization::Format(sel, {LocArg::Named("who", "male")}, "en") == "He won");
        CHECK(Localization::Format(sel, {LocArg::Named("who", "robot")}, "en") == "They won");
        CHECK(Localization::Format("{who, select, male {He}}", {LocArg::Named("who", "robot")}, "en") == "robot");
        CHECK(Localization::Format("{who, select, male {{name} won} other {Someone won}}",
                                   {LocArg::Named("who", "male"), LocArg::Named("name", "Ada")}, "en") == "Ada won");
        CHECK(Localization::Format("{0, select, 1 {one} other {many}}", {1}, "en") == "one");   // numbers select by their text
        // A placeholder with no argument stays as written, so a translator sees the mistake.
        CHECK(Localization::Format("{0} and {1}", {"a"}, "en") == "a and {1}");
        CHECK(Localization::Format("{1, number}", {5}, "en") == "{1, number}");
        CHECK(Localization::Format("{name}", {}, "en") == "{name}");
        CHECK(Localization::Format("{0, plural, one {# coin} other {# coins}}", {}, "en") == "{0, plural, one {# coin} other {# coins}}");
        CHECK(Localization::Format("{0, foo}", {"x"}, "en") == "x");               // an unknown type: the argument as text
        CHECK(Localization::Format("{{literal}} {0}", {"x"}, "en") == "{literal} x");
        // Through a key.
        StringTable t = Langs({"en", "de"});
        t.Set("score", "en", "Score: {0, number}");
        t.Set("score", "de", "Punkte: {0, number}");
        Localization::SetTable(t);
        CHECK(Localize("score", 1234567) == "Score: 1,234,567");
        Localization::SetLanguage("de");
        CHECK(Localize("score", 1234567) == "Punkte: 1.234.567");
        Localization::Reset();
    }

    // ---- CSV: UTF-16 from a spreadsheet, language headers written differently, round trips ----
    {
        // "Unicode text" from Excel: UTF-16 little endian with a mark, tab separated.
        const std::string rows = "key\tnote\ten\tfr\r\nmenu.start\tthe button\tStart\tCommencer\r\nmenu.quit\t\tQuit\t\r\n";
        for (bool little : {true, false}) {
            StringTable t;
            std::string err;
            int n = 0;
            CHECK(t.ImportCsv(Utf16(rows, little), &err, &n) && n == 2);
            CHECK(t.Text("menu.start", "fr") && *t.Text("menu.start", "fr") == "Commencer" && t.Find("menu.start")->note == "the button");
            CHECK(t.Text("menu.quit", "en") && *t.Text("menu.quit", "en") == "Quit" && !t.Text("menu.quit", "fr"));
        }
        // UTF-16 with accents: a surrogate pair too.
        {
            std::string u16 = "\xFF\xFE";
            auto put = [&](unsigned v) { u16 += (char)(v & 0xFF); u16 += (char)(v >> 8); };
            for (char c : std::string("key,en\nk,")) put((unsigned char)c);
            put(0xE9); put(0xD83D); put(0xDE00); put('\n');
            StringTable t;
            CHECK(t.ImportCsv(u16) && t.Text("k", "en") && *t.Text("k", "en") == "\xC3\xA9\xF0\x9F\x98\x80");
        }
        // "FR" and "pt_br" are the table's "fr" and "pt-BR"; a column the table doesn't have is added as written.
        StringTable t = Langs({"en", "fr", "pt-BR"});
        CHECK(t.ImportCsv("Key,EN,FR,pt_br,DE\nhello,Hello,Bonjour,Ol\xC3\xA1,Hallo\n"));
        CHECK(t.languages.size() == 4 && t.FindLanguage("DE") != nullptr);
        CHECK(t.Text("hello", "fr") && *t.Text("hello", "fr") == "Bonjour" && t.Text("hello", "pt-BR") && *t.Text("hello", "pt-BR") == "Ol\xC3\xA1");
        CHECK(t.Text("hello", "en") && *t.Text("hello", "en") == "Hello");
        // Semicolons (a spreadsheet in a comma-decimal locale) with quoted fields.
        StringTable s;
        CHECK(s.ImportCsv("key;en\na;\"x; y\"\nb;plain\n") && s.Text("a", "en") && *s.Text("a", "en") == "x; y" && *s.Text("b", "en") == "plain");
        // Export and import give the same text for everything awkward.
        StringTable awk = Langs({"en", "ja"});
        const std::vector<std::string> tricky = {"a,b", "say \"hi\"", "two\nlines", "tab\there", "semi;colon", " lead", "trail ", "cr\rlone", "\xE6\x97\xA5\xE6\x9C\xAC", "=SUM(A1)", "{0, plural, one {# a} other {# b}}"};
        int i = 0;
        for (const auto& v : tricky) awk.Set("k" + std::to_string(i++), "en", v);
        awk.Set("k0", "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        StringTable round = Langs({"en", "ja"});
        std::string err;
        CHECK(round.ImportCsv(awk.ExportCsv(), &err));
        for (int k = 0; k < (int)tricky.size(); ++k) CHECK(round.Text("k" + std::to_string(k), "en") && *round.Text("k" + std::to_string(k), "en") == tricky[(std::size_t)k]);
        CHECK(round.Text("k0", "ja") && *round.Text("k0", "ja") == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        // The file limits still hold.
        std::string wide = "key";
        for (int c = 0; c < 600; ++c) wide += ",l" + std::to_string(c);
        StringTable w;
        CHECK(!w.ImportCsv(wide + "\nk\n", &err) && err.find("too many") != std::string::npos);
        CHECK(!w.ImportCsv("", &err) && !w.ImportCsv("a,b\n1,2\n", &err) && err.find("key") != std::string::npos);
    }

    // ---- Table files are written whole or not at all ----
    {
        const fs::path dir = fs::temp_directory_path() / "okay_test_loc_runtime";
        fs::remove_all(dir);
        fs::create_directories(dir);
        StringTable t = Langs({"en", "ja"});
        t.Set("a", "en", "A");
        const std::string p = (dir / "table.okayloc").string();
        CHECK(t.Save(p) && !fs::exists(p + ".tmp"));
        StringTable back;
        CHECK(back.Load(p) && back.Text("a", "en"));
        // The temp name taken by a folder: the save fails and the old table stays whole.
        fs::create_directories(p + ".tmp");
        StringTable newer = t;
        newer.Set("b", "en", "B");
        CHECK(!newer.Save(p));
        StringTable still;
        CHECK(still.Load(p) && still.Size() == 1);
        fs::remove_all(p + ".tmp");
        CHECK(!t.Save((dir / "no" / "such" / "t.okayloc").string()));
        // The translator-facing tool files, too.
        LocTable lt(t);
        const std::string csv = (dir / "t.csv").string(), json = (dir / "t.json").string();
        CHECK(lt.SaveCsv(csv) && lt.SaveJson(json) && !fs::exists(csv + ".tmp") && !fs::exists(json + ".tmp"));
        CHECK(ReadText(csv).compare(0, 3, "\xEF\xBB\xBF") == 0);
        CHECK(!lt.SaveCsv((dir / "no" / "t.csv").string()));
        fs::remove_all(dir);
    }

    Localization::Reset();
    TEST_MAIN_RESULT();
}
