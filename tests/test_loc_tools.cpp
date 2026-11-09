// Localization tools: the translation grid, CSV / JSON import and export, the table checker,
// the key scanner, language previews and pseudo-localization, and the font coverage report.
#include "test_framework.hpp"
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Core/LocCheck.hpp"
#include "okay/Core/LocPreview.hpp"
#include "okay/Core/LocScan.hpp"
#include "okay/Core/LocTable.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void WriteAll(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

static StringTable GridTable() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("fr", "Français");
    t.AddLanguage("de", "Deutsch");
    t.defaultLanguage = "en";
    t.Set("menu.start", "en", "Start");  t.Set("menu.start", "fr", "Commencer"); t.Set("menu.start", "de", "Start");
    t.Set("menu.quit", "en", "Quit");    t.Set("menu.quit", "fr", "Quitter");
    t.Set("hud.score", "en", "Score: {0}"); t.Set("hud.score", "fr", "Points : {1}"); t.Set("hud.score", "de", "Punkte: {0}");
    t.Set("only.fr", "fr", "Seulement");
    t.Set("ok.btn", "en", "OK"); t.Set("ok.btn", "fr", "OK"); t.Set("ok.btn", "de", "OK");
    t.Set("num.key", "en", "100"); t.Set("num.key", "fr", "100");
    return t;
}

int main() {
    RUN_SUITE("loc_tools");
    int warnings = 0;
    Log::sink = [&](Log::Level l, const std::string&) { if (l == Log::Level::Warning) ++warnings; };
    Localization::Reset();
    const fs::path tmp = fs::temp_directory_path() / "okay_loc_tools_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    // ---- The grid: rows, columns, cell states -------------------------------------------------
    {
        LocTable lt(GridTable());
        CHECK(lt.Source() == "en");
        CHECK(lt.StateOf("menu.start", "en") == LocCellState::Translated);
        CHECK(lt.StateOf("menu.start", "fr") == LocCellState::Translated);
        CHECK(lt.StateOf("menu.start", "de") == LocCellState::SameAsSource);
        CHECK(lt.StateOf("menu.quit", "de") == LocCellState::Missing);
        CHECK(lt.StateOf("hud.score", "fr") == LocCellState::PlaceholderMismatch);
        CHECK(lt.StateOf("hud.score", "de") == LocCellState::Translated);
        CHECK(lt.StateOf("only.fr", "fr") == LocCellState::Extra);
        CHECK(lt.StateOf("only.fr", "de") == LocCellState::Missing);
        CHECK(lt.StateOf("ok.btn", "fr") == LocCellState::SameAsSource);
        CHECK(lt.StateOf("num.key", "fr") == LocCellState::Translated);   // nothing to translate in "100"
        lt.sameAsSourceOk.insert("ok.btn");
        CHECK(lt.StateOf("ok.btn", "fr") == LocCellState::Translated);

        LocGrid g = lt.Grid();
        CHECK(g.RowCount() == 6 && g.ColCount() == 3);
        CHECK(g.languages[0] == "en" && g.languages[2] == "de");
        CHECK(g.ColumnOf("fr") == 1 && g.ColumnOf("xx") == -1);
        CHECK(g.rows[0].key == "hud.score");   // sorted by key
        CHECK(g.Cell(0, 1).text == "Points : {1}" && g.Cell(0, 1).state == LocCellState::PlaceholderMismatch);

        LocGridOptions o;
        o.languages = {"de"};
        o.stateMask = LocStateBit(LocCellState::Missing);
        LocGrid missing = lt.Grid(o);
        CHECK(missing.ColCount() == 1 && missing.RowCount() == 3);   // menu.quit, only.fr, num.key
        o = {};
        o.search = "COMMENCER";
        CHECK(lt.Grid(o).RowCount() == 1);
        o = {};
        o.keyPrefix = "menu.";
        CHECK(lt.Grid(o).RowCount() == 2);
        o = {};
        o.sortByIssues = true;
        CHECK(lt.Grid(o).rows[0].key == "hud.score");   // a placeholder mismatch is the worst cell

        const LocStats fr = lt.Stats("fr");
        CHECK(fr.total == 6 && fr.placeholderMismatch == 1 && fr.extra == 1 && fr.missing == 0);
        CHECK_NEAR(fr.Completion(), 1.0f, 1e-6);
        const LocStats de = lt.Stats("de");
        CHECK(de.missing == 3 && de.translated == 2);   // hud.score and the OK button (marked fine above)
        CHECK(lt.AllStats().size() == 3);

        LocEntry& stray = lt.table.Add("stray");
        stray.text["xx"] = "orphan";
        CHECK(lt.ExtraTexts().size() == 1 && lt.ExtraTexts()[0].second == "xx");
    }

    // ---- CSV: commas, quotes, line breaks, accents, plural keys ------------------------------
    {
        StringTable t;
        t.AddLanguage("en", "English");
        t.AddLanguage("ja", "日本語");
        t.AddLanguage("pt-BR", "Português");
        t.defaultLanguage = "en";
        t.Set("plain", "en", "Hello");
        t.Set("comma", "en", "one, two, three");
        t.Set("quote", "en", "She said \"hi\" and left");
        t.Set("newline", "en", "Line one\nLine two");
        t.Set("crlf.free", "en", "a\r\nb");   // a CRLF inside a text reads back as a plain break
        t.Set("space", "en", " padded ");
        t.Set("semi;tab\tkey", "en", "odd key");
        t.Set("unicode", "ja", "日本語のテキスト、カンマ付き");
        t.Set("unicode", "pt-BR", "Ação e emoção");
        t.Set("emoji", "en", "Star \xF0\x9F\x8C\x9F");
        t.Set("coins.one", "en", "{0} coin");
        t.Set("coins.other", "en", "{0} coins");
        t.Set("coins.one", "pt-BR", "{0} moeda");
        t.Set("coins.other", "pt-BR", "{0} moedas");
        t.Add("with.note").note = "A note, with \"quotes\"\nand a line break";
        t.Set("with.note", "en", "x");
        LocTable lt(t);

        const std::string csv = lt.ToCsv();
        CHECK(csv.compare(0, 3, "\xEF\xBB\xBF") == 0);                      // byte order mark
        CHECK(csv.find("key,note,en,ja,pt-BR\r\n") == 3);                    // header, CRLF rows
        CHECK(csv.find("\"one, two, three\"") != std::string::npos);
        CHECK(csv.find("\"She said \"\"hi\"\" and left\"") != std::string::npos);   // RFC 4180 doubled quotes
        CHECK(csv.find("coins.one") != std::string::npos && csv.find("coins.other") != std::string::npos);

        LocTable back;
        LocImportResult r = back.ImportCsv(csv);
        CHECK(r.ok && r.rows == (int)t.Size());
        CHECK(r.diff.addedKeys.size() == t.Size());
        for (const auto& kv : t.Entries()) {
            const LocEntry* e = back.table.Find(kv.first);
            CHECK(e != nullptr);
            if (!e) continue;
            CHECK(e->note == kv.second.note);
            for (const auto& tx : kv.second.text) {
                const std::string* got = back.table.Text(kv.first, tx.first);
                const std::string want = kv.first == "crlf.free" ? "a\nb" : tx.second;
                CHECK(got && *got == want);
            }
        }
        CHECK(back.table.Text("newline", "en") && *back.table.Text("newline", "en") == "Line one\nLine two");
        CHECK(back.table.Text("coins.other", "pt-BR") && *back.table.Text("coins.other", "pt-BR") == "{0} moedas");
        CHECK(back.table.languages.size() == 3 && back.table.FindLanguage("pt-BR"));

        // The engine's own importer reads this export; this importer reads the engine's export.
        StringTable engine;
        std::string err;
        CHECK(engine.ImportCsv(csv, &err));
        CHECK(engine.Text("quote", "en") && *engine.Text("quote", "en") == "She said \"hi\" and left");
        LocTable fromEngine;
        CHECK(fromEngine.ImportCsv(t.ExportCsv()).ok);
        CHECK(fromEngine.table.Text("comma", "en") && *fromEngine.table.Text("comma", "en") == "one, two, three");
        CHECK(fromEngine.table.Text("unicode", "ja") && *fromEngine.table.Text("unicode", "ja") == "日本語のテキスト、カンマ付き");

        // Delimiters, line ends and the mark are options; a semicolon or tab file reads back too.
        for (char delim : {';', '\t'}) {
            LocCsvOptions o;
            o.delimiter = delim;
            o.bom = false;
            o.crlf = false;
            const std::string text = lt.ToCsv(o);
            CHECK(text.compare(0, 3, "\xEF\xBB\xBF") != 0 && text.find(std::string("pt-BR\n")) != std::string::npos && text.find("pt-BR\r") == std::string::npos);
            LocTable again;
            CHECK(again.ImportCsv(text).ok);
            CHECK(again.table.Text("comma", "en") && *again.table.Text("comma", "en") == "one, two, three");
            CHECK(again.table.Text("quote", "en") && *again.table.Text("quote", "en") == "She said \"hi\" and left");
        }

        // A translator's hand-off sheet: only what is missing, only some languages.
        LocCsvOptions sheet;
        sheet.languages = {"en", "ja"};
        sheet.missingIn = "ja";
        sheet.note = false;
        const std::string handoff = lt.ToCsv(sheet);
        CHECK(handoff.find("key,en,ja\r\n") == 3);
        CHECK(handoff.find("plain") != std::string::npos && handoff.find("\r\nunicode,") == std::string::npos);

        // UTF-16 text (a spreadsheet's "Unicode text"): tab separated, with a mark.
        std::string utf16 = "\xFF\xFE";
        const std::u16string wide = u"key\tnote\ten\r\nhello\tgreeting\tHi é \U0001F31F\r\n";
        for (char16_t c : wide) { utf16 += (char)(c & 0xFF); utf16 += (char)(c >> 8); }
        LocTable fromWide;
        CHECK(fromWide.ImportCsv(utf16).ok);
        CHECK(fromWide.table.Text("hello", "en") && *fromWide.table.Text("hello", "en") == "Hi \xC3\xA9 \xF0\x9F\x8C\x9F");
        CHECK(fromWide.table.Find("hello")->note == "greeting");

        // Malformed files fail with a message instead of reading garbage.
        LocTable bad;
        LocImportResult br = bad.ImportCsv("key,en\r\n\"never closed,abc\r\n");
        CHECK(!br.ok && !br.error.empty());
        CHECK(!bad.ImportCsv("name,en\r\nx,y\r\n").ok);     // no key column
        CHECK(!bad.ImportCsv("").ok);

        // Ragged rows, repeated keys and empty keys warn and carry on.
        LocTable ragged;
        LocImportResult rr = ragged.ImportCsv("key,en,fr\r\na,A\r\na,A2,A-fr\r\n,orphan,\r\nb,B,B-fr\r\n");
        CHECK(rr.ok && rr.rows == 3 && !rr.warnings.empty());
        CHECK(*ragged.table.Text("a", "en") == "A2" && *ragged.table.Text("a", "fr") == "A-fr");
    }

    // ---- CSV import modes and the dry run ------------------------------------------------------
    {
        LocTable lt(GridTable());
        const std::string sheet = "key,fr,de,es\r\nmenu.quit,Sortir,Beenden,Salir\r\nmenu.start,,Los,\r\nnew.key,Nouveau,Neu,Nuevo\r\n";

        LocImportOptions dry;
        dry.dryRun = true;
        const std::string before = lt.table.Write();
        LocImportResult d = lt.ImportCsv(sheet, dry);
        CHECK(d.ok && lt.table.Write() == before);                       // the dry run left the table alone
        CHECK(d.diff.addedKeys.size() == 1 && d.diff.addedKeys[0] == "new.key");
        CHECK(d.diff.addedLanguages.size() == 1 && d.diff.addedLanguages[0] == "es");
        CHECK(d.diff.changed.size() == 2);                               // menu.quit fr; menu.start de
        CHECK(d.diff.filled.size() >= 5);                                // menu.quit de, new.key x3 (+ es texts)

        LocImportOptions fill;
        fill.mode = LocImportOptions::Mode::FillEmpty;
        LocTable fillTable(GridTable());
        CHECK(fillTable.ImportCsv(sheet, fill).ok);
        CHECK(*fillTable.table.Text("menu.quit", "fr") == "Quitter");    // kept: it already had text
        CHECK(*fillTable.table.Text("menu.quit", "de") == "Beenden");    // filled

        LocImportOptions replace;
        replace.mode = LocImportOptions::Mode::Replace;
        LocTable repl(GridTable());
        LocImportResult rp = repl.ImportCsv(sheet, replace);
        CHECK(rp.ok && !repl.table.Text("menu.start", "fr"));            // the empty cell cleared it
        CHECK(rp.diff.cleared.size() == 1);

        LocImportOptions strict;
        strict.addKeys = false;
        strict.addLanguages = false;
        LocTable limited(GridTable());
        LocImportResult lr = limited.ImportCsv(sheet, strict);
        CHECK(lr.ok && !limited.table.Has("new.key") && !limited.table.FindLanguage("es"));
        CHECK(!lr.warnings.empty());

        LocTable all(GridTable());
        CHECK(all.ImportCsv(sheet).ok);
        CHECK(*all.table.Text("menu.start", "fr") == "Commencer");       // an empty cell keeps the text
        CHECK(*all.table.Text("new.key", "es") == "Nuevo" && all.table.FindLanguage("es"));
    }

    // ---- JSON: the whole table, one language, nested objects ------------------------------------
    {
        StringTable t;
        t.AddLanguage("en", "English");
        t.AddLanguage("ja", "日本語").font = "Fonts/Noto.ttf";
        t.defaultLanguage = "en";
        t.Set("menu.start", "en", "Start \"now\"\n\ttab");
        t.Set("menu.start", "ja", "スタート \xF0\x9F\x8C\x9F");
        t.Set("coins.one", "en", "{0} coin");
        t.Set("coins.other", "en", "{0} coins");
        t.Add("menu.start").note = "The first button";
        LocTable lt(t);
        const std::string json = lt.ToJson();
        CHECK(json.find("\"okayloc\": 1") != std::string::npos && json.find("\"default\": \"en\"") != std::string::npos);
        CHECK(json.find("\\\"now\\\"") != std::string::npos && json.find("\\n\\ttab") != std::string::npos);

        LocTable back;
        LocImportResult r = back.ImportJson(json);
        CHECK(r.ok && r.rows == 3);
        CHECK(back.table.Write() == t.Write());                          // the same table, texts, note, font, default
        CHECK(back.table.DefaultLanguage() == "en" && back.table.FindLanguage("ja")->font == "Fonts/Noto.ttf");

        const std::string flat = lt.ToJsonFlat("en");
        CHECK(flat.find("coins.one") != std::string::npos && flat.find("スタート") == std::string::npos);
        LocTable one;
        LocImportOptions fr;
        fr.language = "en";
        CHECK(one.ImportJson(flat, fr).ok);
        CHECK(*one.table.Text("coins.other", "en") == "{0} coins");
        CHECK(!one.ImportJson(flat).ok);                                 // a flat file needs its language

        LocTable nested;
        LocImportOptions de;
        de.language = "de";
        LocImportResult nr = nested.ImportJson(
            "{\"coins\": {\"one\": \"{0} M\\u00fcnze\", \"other\": \"{0} M\\u00fcnzen\"}, \"menu\": {\"start\": \"Los \\ud83c\\udf1f\"}, \"n\": 5, \"bad\": [1]}", de);
        CHECK(nr.ok && *nested.table.Text("coins.one", "de") == "{0} M\xC3\xBCnze");
        CHECK(*nested.table.Text("menu.start", "de") == "Los \xF0\x9F\x8C\x9F");   // a surrogate pair
        CHECK(*nested.table.Text("n", "de") == "5" && !nested.table.Has("bad") && !nr.warnings.empty());

        CHECK(!back.ImportJson("[1,2]").ok);
        CHECK(!back.ImportJson("{\"keys\": {\"a\": ").ok);
        CHECK(!back.ImportJson("{\"keys\": {\"a\": \"unterminated}}").ok);
    }

    // ---- Loading a folder of language files -----------------------------------------------------
    {
        const fs::path dir = tmp / "locales";
        fs::create_directories(dir);
        StringTable en;
        en.AddLanguage("en", "English");
        en.defaultLanguage = "en";
        en.Set("menu.start", "en", "Start");
        en.Set("menu.quit", "en", "Quit");
        en.Save((dir / "a_en.okayloc").u8string());
        WriteAll(dir / "fr.json", "{\"menu\": {\"start\": \"Commencer\"}}");
        WriteAll(dir / "de.csv", "key,de\r\nmenu.start,Starten\r\nmenu.quit,Beenden\r\n");
        WriteAll(dir / "readme.txt", "not a language file");
        WriteAll(dir / "z_broken.json", "{ nope");

        LocTable lt;
        std::string err;
        CHECK(lt.LoadFolder(dir.u8string(), &err));
        CHECK(lt.table.languages.size() == 3);
        CHECK(lt.files.size() == 3 && !lt.warnings.empty());             // the broken file is reported, not fatal
        CHECK(*lt.table.Text("menu.start", "fr") == "Commencer" && *lt.table.Text("menu.start", "de") == "Starten");
        CHECK(lt.table.DefaultLanguage() == "en");
        CHECK(lt.StateOf("menu.quit", "fr") == LocCellState::Missing && lt.StateOf("menu.quit", "de") == LocCellState::Translated);

        LocTable none;
        CHECK(!none.LoadFolder((tmp / "missing").u8string(), &err) && !err.empty());
        fs::create_directories(tmp / "empty");
        CHECK(!none.LoadFolder((tmp / "empty").u8string(), &err));

        // Saving writes the files back.
        const fs::path out = tmp / "out.csv";
        CHECK(lt.SaveCsv(out.u8string()));
        LocTable reread;
        CHECK(reread.LoadFile(out.u8string()) && reread.table.Size() == lt.table.Size());
        CHECK(lt.SaveJson((tmp / "out.json").u8string()) && !fs::exists(tmp / "out.json.tmp"));
    }

    // ---- The checker: placeholders, plurals, whitespace, length ---------------------------------
    {
        StringTable t;
        t.AddLanguage("en", "English");
        t.AddLanguage("ru", "Русский");
        t.AddLanguage("fr", "Français");
        t.defaultLanguage = "en";
        t.Set("coins", "en", "{n, plural, one {# coin} other {# coins}}");
        t.Set("coins", "ru", "{n, plural, one {# монета} other {# монет}}");        // lacks few and many
        t.Set("coins", "fr", "{n, plural, one {# pièce} other {# pièces}}");
        t.Set("greet", "en", "Hello, {player}! You have {0} messages");
        t.Set("greet", "fr", "Bonjour, {joueur} ! Vous avez {0} messages");
        t.Set("greet", "ru", "Привет, {player}! Сообщений: {0}");
        t.Set("bye", "en", "Bye ");
        t.Set("bye", "fr", "Au revoir");
        t.Set("save", "en", "Save game");
        t.Set("save", "fr", "Sauvegarder la partie en cours maintenant");
        t.Set("ok", "en", "OK");
        t.Set("ok", "fr", "D'accord");
        t.Set("broken", "en", "Score {0}");
        t.Set("broken", "fr", "Score {0");
        t.Set("same", "en", "Settings");
        t.Set("same", "fr", "Settings");
        t.Set("extra", "fr", "Seulement");
        t.Set("lonely", "en", "No translation");
        t.Set("items.one", "en", "{0} item");
        t.Set("items.other", "en", "{0} items");
        t.Set("items.one", "ru", "{0} предмет");
        t.Set("items.other", "ru", "{0} предметов");

        CHECK(LocRequiredPlurals("en") == std::vector<std::string>({"one", "other"}));
        CHECK(LocRequiredPlurals("ja") == std::vector<std::string>({"other"}));
        CHECK(LocRequiredPlurals("ru") == std::vector<std::string>({"one", "few", "many", "other"}));
        CHECK(LocRequiredPlurals("pt-BR") == std::vector<std::string>({"one", "other"}));
        CHECK(LocRequiredPlurals("ar").size() == 6);

        LocCheckOptions o;
        o.sameAsSource = true;
        const LocReport rep = LocCheck(t, o);
        CHECK(rep.keys == (int)t.Size() && rep.languages.size() == 3);

        auto issue = [&](LocIssueKind k, const std::string& key, const std::string& lang) -> const LocIssue* {
            for (const auto& i : rep.issues) if (i.kind == k && i.key == key && i.language == lang) return &i;
            return nullptr;
        };
        // {player} vs {joueur}: an error naming both
        const LocIssue* ph = issue(LocIssueKind::PlaceholderMismatch, "greet", "fr");
        CHECK(ph && ph->severity == LocSeverity::Error && ph->message.find("{player}") != std::string::npos && ph->message.find("{joueur}") != std::string::npos);
        CHECK(!issue(LocIssueKind::PlaceholderMismatch, "greet", "ru"));
        // Russian needs few and many in a plural block
        const LocIssue* pl = issue(LocIssueKind::PluralFormMissing, "coins", "ru");
        CHECK(pl && pl->message.find("few, many") != std::string::npos);
        CHECK(!issue(LocIssueKind::PluralFormMissing, "coins", "fr") && !issue(LocIssueKind::PluralFormMissing, "coins", "en"));
        // ... and the row-per-form scheme needs rows for them
        CHECK(issue(LocIssueKind::PluralKeyMissing, "items.few", "ru") && issue(LocIssueKind::PluralKeyMissing, "items.many", "ru"));
        CHECK(!issue(LocIssueKind::PluralKeyMissing, "items.one", "ru") && !issue(LocIssueKind::PluralKeyMissing, "items.few", "en"));
        // an English trailing space the French lacks
        CHECK(issue(LocIssueKind::TrailingWhitespace, "bye", "fr"));
        // text that grows past the UI: 40 wide against 9, but "OK" -> "D'accord" is short enough to pass
        CHECK(issue(LocIssueKind::LengthGrowth, "save", "fr") && !issue(LocIssueKind::LengthGrowth, "ok", "fr"));
        CHECK(issue(LocIssueKind::UnbalancedBraces, "broken", "fr"));
        CHECK(issue(LocIssueKind::SameAsSource, "same", "fr") && issue(LocIssueKind::SameAsSource, "same", "fr")->severity == LocSeverity::Info);
        CHECK(issue(LocIssueKind::ExtraText, "extra", "fr"));
        CHECK(issue(LocIssueKind::MissingText, "lonely", "fr") && issue(LocIssueKind::MissingText, "lonely", "ru"));
        CHECK(rep.HasErrors() && rep.Count(LocIssueKind::PlaceholderMismatch) == 1);
        CHECK(rep.ForLanguage("ru").size() > 0 && rep.ForKey("coins").size() == 1);
        CHECK(rep.ToText().find("error") != std::string::npos && rep.ToText().find("greet") != std::string::npos);
        CHECK(rep.ToCsv().compare(0, 3, "\xEF\xBB\xBF") == 0);

        // Switches and the language filter.
        LocCheckOptions only;
        only.languages = {"fr"};
        only.missing = only.plurals = only.whitespace = only.growth = false;
        const LocReport pr = LocCheck(t, only);
        CHECK(pr.Count(LocIssueKind::PlaceholderMismatch) == 1 && pr.Count(LocIssueKind::MissingText) == 0 && pr.languages.size() == 1);
        LocCheckOptions tolerant;
        tolerant.growthWarn = 5.0f;
        CHECK(LocCheck(t, tolerant).Count(LocIssueKind::LengthGrowth) == 0);

        // Messages
        CHECK(LocPlaceholders("{0} of {name} {{literal}} {n, plural, one {# {thing}} other {#}}") ==
              (std::set<std::string>{"0", "name", "n", "thing"}));
        std::vector<LocNode> nodes;
        std::string perr;
        CHECK(LocParseMessage("a {b", nodes, &perr) == false && !perr.empty());
        CHECK(LocParseMessage("stray }", nodes, &perr) == false);
        CHECK(LocParseMessage("ok {{ and }}", nodes, &perr));
        CHECK(LocPluralSelectors("{n, plural, =0 {none} one {#} other {#s}}")[0] == (std::set<std::string>{"=0", "one", "other"}));
        CHECK(LocMessageWidth("日本語") == 6 && LocMessageWidth("ab\ncdef") == 4 && LocMessageWidth("{0}") == 3);
        CHECK(!LocHasLetters("{name}%") && !LocHasLetters("100") && LocHasLetters("OK {0}") && LocHasLetters("日本"));

        // Missing keys
        const auto missing = LocMissingReport(t);
        CHECK(missing.size() == 2);   // ru, fr: the source isn't listed
        const LocMissing* ru = nullptr;
        for (const auto& m : missing) if (m.language == "ru") ru = &m;
        CHECK(ru && Has(ru->keys, "lonely") && Has(ru->keys, "bye") && ru->completion < 1.0f && ru->total == (int)t.Size());
        CHECK(LocMissingToText(missing).find("ru: ") != std::string::npos && LocMissingToText(missing, false).find("  lonely") == std::string::npos);
    }

    // ---- Which keys the project uses --------------------------------------------------------------
    {
        StringTable t;
        t.AddLanguage("en");
        t.defaultLanguage = "en";
        for (const char* k : {"menu.title", "menu.start", "menu.quit", "hud.score", "hud.coins", "shop.sword", "shop.shield",
                              "unused.one", "tip.main", "pause.title", "coins.one", "coins.other", "vs.key", "label.at", "dlg.guard.1"})
            t.Set(k, "en", std::string("text of ") + k);

        std::vector<std::pair<std::string, std::string>> files = {
            {"Scripts/menu.okay", "start() {\n  title = loc(\"menu.title\")\n  b = T('menu.start')\n  s = Localization.Get(\"hud.score\")\n"
                                  "  q = okay::Localization::GetOr(\"menu.quit\", \"Quit\")\n  bad = loc(\"missing.key\")\n  w = loc(\"shop.\" + item)\n"
                                  "  n = loc(\"coins\", 3)\n  alloc(\"not.a.key\")\n  Format(\"nope.key\")\n}\n"},
            {"Scripts/Player.cs", "void Start() { string c = Localization.Get(\"hud.coins\"); var x = Loc(\"cs.undefined\"); }\n"},
            {"Scenes/main.okayscene", "okayscene 3\nobject Title\n  text \"x\"\n  loc text=\"menu.title\" button=\"tip.undefined\" tooltip=\"tip.main\"\n"
                                      "object Script\n  script \"loc(\\\"menu.quit\\\"); loc(\\\"in.scene.script\\\")\"\n"},
            {"Prefabs/hud.okayprefab", "okayscene 3\nobject Hud\n  loc world=\"hud.coins\"\n"},
            {"Graphs/start.okayvs", "node 1 Start\nnode 5 Localize vs.key\nnode 6 Localize \"menu.start\"\n"},
            {"UI/hud.okayui", "text \"@label.at\" pos=1,1\ntext \"@undefined.at\"\n"},
            {"Notes/readme.md", "loc(\"readme.key\")"},
        };
        LocScanOptions o;
        LocScanResult r = LocScanFiles(files, t, o);
        CHECK(r.filesScanned == 6);                                       // the .md is no scanned type
        CHECK(r.usedKeys.count("menu.title") && r.usedKeys.count("menu.start") && r.usedKeys.count("hud.score"));
        CHECK(r.usedKeys.count("menu.quit") && r.usedKeys.count("hud.coins") && r.usedKeys.count("tip.main"));
        CHECK(r.usedKeys.count("vs.key"));
        CHECK(r.usedKeys.count("shop.sword") && r.usedKeys.count("shop.shield"));        // "shop." + item
        CHECK(r.usedKeys.count("coins.one") && r.usedKeys.count("coins.other"));         // loc("coins", n): its plural forms
        CHECK(!r.usedKeys.count("label.at"));                                              // "@key" is off by default
        CHECK(Has(r.unused, "unused.one") && Has(r.unused, "label.at") && !Has(r.unused, "menu.title"));
        CHECK(!Has(r.unused, "pause.title") && !Has(r.unused, "dlg.guard.1"));             // read by built-in components
        auto undefinedKeys = [&](const LocScanResult& res) {
            std::vector<std::string> k;
            for (const auto& u : res.undefined) k.push_back(u.key);
            return k;
        };
        const auto undef = undefinedKeys(r);
        CHECK(Has(undef, "missing.key") && Has(undef, "cs.undefined") && Has(undef, "tip.undefined") && Has(undef, "in.scene.script"));
        CHECK(!Has(undef, "not.a.key") && !Has(undef, "nope.key") && !Has(undef, "readme.key") && !Has(undef, "shop."));
        for (const auto& u : r.undefined) if (u.key == "missing.key") CHECK(u.file == "Scripts/menu.okay" && u.line == 6);
        CHECK(r.ToText().find("missing.key") != std::string::npos);

        o.atKeys = true;
        r = LocScanFiles(files, t, o);
        CHECK(r.usedKeys.count("label.at") && Has(undefinedKeys(r), "undefined.at"));

        LocScanOptions custom;
        custom.callNames = {"say"};
        custom.engineKeyPrefixes.clear();
        r = LocScanFiles({{"a.okay", "say(\"menu.title\"); loc(\"menu.quit\")"}}, t, custom);
        CHECK(r.usedKeys.count("menu.title") && !r.usedKeys.count("menu.quit") && Has(r.unused, "pause.title"));

        // A dialogue graph and a quest file make keys of their own (used when the table has them,
        // never "undefined" when it doesn't: the text typed in the asset shows).
        DialogueGraph graph;
        graph.name = "smith";
        DialogueNode& line = graph.Add(DialogueNode::Kind::Line, 0, 0);
        line.speaker = "Smith";
        line.text = "Hello";
        QuestDef quest;
        quest.id = "rescue";
        quest.title = "Rescue the cat";
        quest.description = "Find it";
        const std::string lineKey = DialogueLineKey("smith", line.id);
        StringTable d;
        d.AddLanguage("en");
        d.Set("quest.rescue.title", "en", "Rescue");
        d.Set(lineKey, "en", "Hello");
        d.Set("dlg.other.1", "en", "Hi");
        std::vector<std::pair<std::string, std::string>> assets = {{"Dialogue/smith.okaydialog", graph.Write()},
                                                                    {"Quests/q.okayquest", QuestFile::Write({quest})}};
        r = LocScanFiles(assets, d, {});
        CHECK(r.filesScanned == 2 && r.undefined.empty());
        CHECK(r.usedKeys.count("quest.rescue.title") && r.usedKeys.count(lineKey) && !r.usedKeys.count("dlg.other.1"));
        bool sawQuestDesc = false;
        for (const auto& u : r.uses) if (u.key == "quest.rescue.desc" && u.optional) sawQuestDesc = true;
        CHECK(sawQuestDesc);

        // Walking a real folder.
        const fs::path proj = tmp / "project";
        fs::create_directories(proj / "Scripts");
        fs::create_directories(proj / "build");
        fs::create_directories(proj / ".git");
        WriteAll(proj / "Scripts" / "a.okay", "x = loc(\"menu.title\")\ny = loc(\"gone.key\")\n");
        WriteAll(proj / "build" / "skip.okay", "loc(\"skipped.key\")");
        WriteAll(proj / ".git" / "skip.okay", "loc(\"hidden.key\")");
        r = LocScanProject(proj.u8string(), t, {});
        CHECK(r.filesScanned == 1 && r.usedKeys.count("menu.title"));
        CHECK(r.undefined.size() == 1 && r.undefined[0].key == "gone.key" && r.undefined[0].file == "Scripts/a.okay" && r.undefined[0].line == 2);
    }

    // ---- Language previews and pseudo-localization ----------------------------------------------------
    {
        StringTable t;
        t.AddLanguage("en", "English");
        t.AddLanguage("fr", "Français");
        t.AddLanguage("ar", "العربية");
        t.defaultLanguage = "en";
        t.Set("start", "en", "Start");
        t.Set("start", "fr", "Commencer");
        t.Set("start", "ar", "ابدأ");
        t.Set("hello", "en", "Hi {name}");
        t.Set("coins", "en", "{n, plural, one {# coin} other {# coins}}");
        t.Set("only.en", "en", "Only English");
        const std::string before = t.Write();
        Localization::Reset();
        Localization::SetTable(t);
        const std::uint32_t version = Localization::Version();
        const std::string langBefore = Localization::Language();
        const std::size_t reported = Localization::Reported().size();
        int heard = 0;
        Localization::AddListener([&](const std::string&) { ++heard; });
        warnings = 0;

        LocPreview p(t);
        CHECK(p.Language() == "en" && p.Get("start") == "Start" && !p.Pseudo());
        p.SetLanguage("fr");
        CHECK(p.Language() == "fr" && p.Get("start") == "Commencer");
        CHECK(p.Get("only.en") == "Only English");                          // falls back to the default language
        CHECK(p.Get("nope") == "nope");
        std::string out = "unchanged";
        CHECK(!p.TryGet("nope", out) && out == "unchanged" && p.TryGet("only.en", out) && out == "Only English");

        p.SetLanguage("en");
        p.SetPseudo(true);
        CHECK(p.Get("start") == "[!!! \xC5\xA0\xC5\xA3\xC3\xA5\xC5\x95\xC5\xA3~~ !!!]");          // [!!! Šţåŕţ~~ !!!]
        CHECK(p.Localize("hello", LocArg::Named("name", "Ana")) == "[!!! \xC4\xA4\xC3\xAD Ana~ !!!]");   // [!!! Ĥí Ana~ !!!]
        const std::string coins = p.Localize("coins", LocArg::Named("n", 1));
        CHECK(coins.find("1 \xC3\xA7\xC3\xB6\xC3\xAD\xC3\xB1") != std::string::npos && coins.compare(0, 5, "[!!! ") == 0);   // 1 çöíñ
        CHECK(p.Localize("coins", LocArg::Named("n", 3)).find("3 \xC3\xA7\xC3\xB6\xC3\xAD\xC3\xB1\xC5\xA1") != std::string::npos);

        LocPseudoOptions plain;
        plain.markers = false;
        plain.expand = 0.0f;
        CHECK(LocPseudoText("Start", plain) == "\xC5\xA0\xC5\xA3\xC3\xA5\xC5\x95\xC5\xA3");
        plain.accents = false;
        CHECK(LocPseudoText("Start", plain) == "Start");
        LocPseudoOptions wide;
        wide.markers = false;
        wide.expand = 1.0f;
        CHECK(LocPseudoText("abcd", wide) == "\xC3\xA5" "b\xC3\xA7\xC4\x8F" "~~~~");              // doubles the length
        CHECK(LocPseudoText("", {}) == "");
        // placeholders and the plural syntax survive, so the result still formats
        const std::string pat = LocPseudoText("{n, plural, one {# coin} other {# coins}} for {name}", {});
        CHECK(LocPlaceholders(pat) == LocPlaceholders("{n, plural, one {# coin} other {# coins}} for {name}"));
        CHECK(Localization::Format(pat, {LocArg::Named("n", 1), LocArg::Named("name", "Ana")}, "en").find("1 \xC3\xA7\xC3\xB6\xC3\xAD\xC3\xB1") != std::string::npos);

        // Mirrored text and right-to-left languages.
        CHECK(LocMirrorText("abc (d)") == "(d) cba");
        CHECK(LocMirrorText("Hi {name}") == "{name} iH");
        CHECK(LocMirrorText("ab\ncd") == "ba\ndc");
        CHECK(LocMirrorText("e\xCC\x81x") == "xe\xCC\x81");                  // a combining mark stays with its letter
        CHECK(LocIsRtl("ar") && LocIsRtl("he-IL") && LocIsRtl("fa_IR") && !LocIsRtl("en") && !LocIsRtl("fr"));
        p.SetPseudo(false);
        p.SetLanguage("ar");
        CHECK(p.Rtl() && p.Get("start") == LocMirrorText("ابدأ") && p.Get("start") != "ابدأ");
        p.SetMirrorRtl(false);
        CHECK(p.Get("start") == "ابدأ");
        p.SetLanguage("fr");
        p.SetPseudo(true, LocPseudoOptions{true, false, 0.0f, true});
        const LocPseudoOptions notMirrored{true, false, 0.0f, false};
        CHECK(p.Get("start") == LocMirrorText(LocPseudoText("Commencer", notMirrored)));

        // A baked copy for a running game; the project's table is unharmed.
        p.SetPseudo(true);
        p.SetLanguage("fr");
        const StringTable baked = p.Bake();
        CHECK(baked.languages.size() == 1 && baked.languages[0].code == "fr" && baked.DefaultLanguage() == "fr");
        CHECK(baked.Text("start", "fr") && baked.Text("start", "fr")->compare(0, 5, "[!!! ") == 0);
        CHECK(baked.Text("only.en", "fr") != nullptr);                       // filled from the default language

        // Nothing the preview did reached the saved table or the running service.
        CHECK(t.Write() == before);
        CHECK(Localization::Version() == version && Localization::Language() == langBefore);
        CHECK(Localization::Reported().size() == reported && heard == 0 && warnings == 0);
        p.SetLanguage(Localization::kPseudo);
        CHECK(p.Pseudo() && p.Language() == "en");
        Localization::Reset();
    }

    // ---- Font coverage ---------------------------------------------------------------------------------------
    {
        TtfFont* ui = BuiltinUIFont();
        CHECK(ui && ui->Valid());
        if (ui) {
            StringTable t;
            t.AddLanguage("en");
            t.AddLanguage("ru");
            t.AddLanguage("ja");
            t.defaultLanguage = "en";
            t.Set("a", "en", "Caf\xC3\xA9 {0}\n\t ok");
            t.Set("a", "ru", "Жизнь");
            t.Set("a", "ja", "日本語 ok");
            t.Set("b", "ja", "日本");
            t.Set("c", "ja", "ok");
            LocFontOptions o;
            o.fonts = {ui};
            const LocFontReport rep = LocFontCoverage(t, o);
            CHECK(rep.languages.size() == 3);
            CHECK(rep.languages[0].missing.empty() && rep.languages[1].missing.empty());   // Latin-1 and Cyrillic are in the UI font
            const LocFontLanguage& ja = rep.languages[2];
            CHECK(ja.missing.size() == 3);                                                    // 日 本 語
            CHECK(ja.missing[0].utf8 == "\xE6\x97\xA5" || ja.missing[0].utf8 == "\xE6\x9C\xAC");
            CHECK(ja.missing[0].count == 2 && ja.charactersUsed == 5);                        // 日 本 語 o k
            CHECK(Has(ja.undrawableTexts, "a") && Has(ja.undrawableTexts, "b") && !Has(ja.undrawableTexts, "c"));
            CHECK(!rep.Clean() && rep.ToText().find("no font draws 3 characters") != std::string::npos);
            CHECK(ja.primary == ui->Name());

            // A language font that isn't there is a warning, not a crash.
            t.FindLanguage("ja")->font = (tmp / "NoSuchFont.ttf").u8string();
            LocFontOptions chain;
            chain.useSystemFonts = false;
            const LocFontReport rep2 = LocFontCoverage(t, chain);
            CHECK(!rep2.languages[2].warnings.empty() && rep2.languages[2].missing.size() == 3);
        }
    }

    // ---- Garbage in: nothing crashes or hangs, and the invariants hold ---------------------------------------------------
    {
        std::uint32_t seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char alphabet[] = "ab {}()[]<>,;\t\"\r\n\\#=:\xC3\xA9\xE6\x97\xA5\xF0\x9F\x8C\x9F\xFF\x80 0123456789.:-,pluralone other";
        auto junk = [&](std::size_t len) {
            std::string out;
            for (std::size_t i = 0; i < len; ++i) out += alphabet[rnd() % (sizeof alphabet - 1)];
            return out;
        };
        int imported = 0;
        for (int i = 0; i < 1500; ++i) {
            const std::string text = junk(rnd() % 120);
            std::vector<std::vector<std::string>> rows;
            LocParseCsv(text, rows);
            LocTable csv;
            if (csv.ImportCsv("key,en\r\n" + text).ok) ++imported;
            LocTable json;
            json.ImportJson(text);
            json.ImportJson("{\"keys\": {" + text + "}}");
            std::vector<LocNode> nodes;
            LocParseMessage(text, nodes);
            LocPlaceholders(text);
            LocMessageWidth(text);
            LocPseudoText(text, {});
            const std::string mirrored = LocMirrorText(text);
            CHECK(mirrored.size() == LocMirrorText(text).size());
            std::vector<LocScanUse> uses;
            LocScanText("a.okayscene", text, {}, uses);
            LocScanText("a.okay", "loc(\"" + text, {}, uses);
        }
        CHECK(imported > 0);
        // Mirroring plain text twice gives the text back.
        for (int i = 0; i < 200; ++i) {
            std::string plain;
            const char* const bits[] = {"a", "b", " ", "(", "c", "d", ")", "[", "e", "]", "<", "f", ">", "\n", "\xC3\xA9", "\xE6\x97\xA5", "e\xCC\x81"};
            for (std::size_t k = rnd() % 30; k > 0; --k) plain += bits[rnd() % (sizeof bits / sizeof bits[0])];
            const std::string once = LocMirrorText(plain);
            CHECK(LocMirrorText(once) == plain);
        }
        CHECK(LocMirrorText(LocMirrorText("Hello (world) [x]\nline <2>")) == "Hello (world) [x]\nline <2>");
        // A table of junk survives the checker and the grid.
        StringTable t;
        t.AddLanguage("en");
        t.AddLanguage("fr");
        t.defaultLanguage = "en";
        for (int i = 0; i < 200; ++i) {
            t.Set("k" + std::to_string(i), "en", junk(rnd() % 40 + 1));
            t.Set("k" + std::to_string(i), "fr", junk(rnd() % 40 + 1));
        }
        LocTable lt(t);
        LocCheckOptions all;
        all.sameAsSource = true;
        CHECK(LocCheck(t, all).keys == 200);
        CHECK(lt.Grid().RowCount() == 200 && lt.AllStats().size() == 2);
        CHECK(!LocFontCoverage(t, LocFontOptions{"", {BuiltinUIFont()}, false}).languages.empty());
    }

    fs::remove_all(tmp, ec);
    Localization::Reset();
    TEST_MAIN_RESULT();
}
