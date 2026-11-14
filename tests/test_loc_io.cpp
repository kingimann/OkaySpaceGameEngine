// The Localization window's import preview, JSON import and export, font coverage and
// right-to-left ordering (editor/LocalizationIO, no ImGui). The same functions the window calls.
#include "test_framework.hpp"
#include "LocalizationIO.hpp"
#include "LocalizationTools.hpp"

#include <algorithm>
#include <random>

using namespace okay;
using namespace oked;

static StringTable Sample() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("fr", "Francais");
    t.defaultLanguage = "en";
    t.Set("menu.start", "en", "Start");
    t.Set("menu.start", "fr", "Commencer");
    t.Set("menu.quit", "en", "Quit");
    t.Set("hud.score", "en", "Score: {0}");
    t.Set("hud.score", "fr", "Score : {0}");
    t.Add("menu.quit").note = "Leaves the game";
    return t;
}

static StringTable Tricky() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("ja", "Japanese");
    t.defaultLanguage = "en";
    t.Set("a.quote", "en", "He said \"hi\" \\ and left");
    t.Set("a.lines", "en", "one\ntwo\r\nthree\ttab");
    t.Set("a.uni", "en", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80");
    t.Set("a.uni", "ja", "\xE3\x81\x82\xE3\x81\x84");
    t.Set("a.plural", "en", "{0, plural, one {# coin} other {# coins}}");
    t.Set("a.comma", "en", "a, b; c");
    t.Add("a.comma").note = "note, with \"quotes\"";
    return t;
}

static void TestJsonRoundTrip() {
    RUN_SUITE("loc io json round trip");
    const StringTable t = Tricky();
    const std::string json = ExportLocJson(t);
    CHECK(DetectLocFormat(json) == LocImportFormat::Json);
    StringTable empty;
    const LocImportPreview p = PreviewLocImport(empty, json);
    CHECK(p.ok && p.format == LocImportFormat::Json);
    CHECK(empty.Empty());                              // the preview never touches the live table
    CHECK(p.after.Write() == t.Write());               // every text, note and language comes back
    StringTable live;
    CHECK(ApplyLocImport(live, p));
    CHECK(live.Write() == t.Write());
    // Exporting what was imported gives the same file.
    CHECK(ExportLocJson(live) == json);
    // Importing the same file again changes nothing, and cannot be applied.
    const LocImportPreview again = PreviewLocImport(live, json);
    CHECK(again.ok && again.NothingToDo());
    CHECK(!ApplyLocImport(live, again));

    // One language as {"key": "text"}.
    const std::string flat = ExportLocJson(t, "ja");
    CHECK(flat.find("a.uni") != std::string::npos && flat.find("a.quote") == std::string::npos);
    StringTable base = Tricky();
    base.Set("a.uni", "ja", "");
    const LocImportPreview fill = PreviewLocImport(base, flat, LocImportFormat::Auto, "ja");
    CHECK(fill.ok && fill.diff.filled.size() == 1 && fill.diff.changed.empty());
    CHECK(fill.lines.size() == 1 && fill.lines[0].find("+ ja a.uni") == 0);
    // The same file with no language to put it in is refused, not guessed.
    const LocImportPreview nolang = PreviewLocImport(base, flat);
    CHECK(!nolang.ok && !nolang.error.empty());
    // Nested keys join with a dot.
    const LocImportPreview nested = PreviewLocImport(Sample(), "{\"coins\": {\"one\": \"# coin\", \"other\": \"# coins\"}}",
                                                     LocImportFormat::Json, "fr");
    CHECK(nested.ok && nested.after.Has("coins.one") && *nested.after.Text("coins.other", "fr") == "# coins");
}

static void TestImportPreview() {
    RUN_SUITE("loc io import preview");
    const StringTable live = Sample();
    const std::string before = live.Write();
    const std::string csv = "key,note,en,FR,de\n"
                            "menu.start,,Start,Demarrer,Starten\n"
                            "menu.quit,,Quit,Quitter,\n"
                            "menu.new,Brand new,New,Nouveau,Neu\n"
                            "hud.score,,,,\n";
    const LocImportPreview p = PreviewLocImport(live, csv);
    CHECK(p.ok && p.format == LocImportFormat::Csv);
    CHECK(live.Write() == before);
    CHECK(p.rows == 4);
    CHECK(std::find(p.diff.addedLanguages.begin(), p.diff.addedLanguages.end(), "de") != p.diff.addedLanguages.end());
    CHECK(std::find(p.diff.addedLanguages.begin(), p.diff.addedLanguages.end(), "fr") == p.diff.addedLanguages.end());   // FR is fr
    CHECK(p.diff.addedKeys.size() == 1 && p.diff.addedKeys[0] == "menu.new");
    bool changedStart = false, filledQuit = false;
    for (const auto& kl : p.diff.changed) if (kl.first == "menu.start" && kl.second == "fr") changedStart = true;
    for (const auto& kl : p.diff.filled) if (kl.first == "menu.quit" && kl.second == "fr") filledQuit = true;
    CHECK(changedStart && filledQuit);
    CHECK(p.after.Text("hud.score", "fr") && *p.after.Text("hud.score", "fr") == "Score : {0}");   // an empty cell keeps the text
    bool sawChange = false;
    for (const std::string& line : p.lines)
        if (line == "~ fr menu.start: \"Commencer\" -> \"Demarrer\"") sawChange = true;
    CHECK(sawChange);
    CHECK(p.ChangeCount() > 0);
    CHECK(p.Summary().find("Imported 4 rows") == 0);

    // The confirm step: nothing is applied until ApplyLocImport, and it gives exactly the previewed table.
    StringTable target = live;
    CHECK(ApplyLocImport(target, p));
    CHECK(target.Write() == p.after.Write());
    CHECK(*target.Text("menu.start", "fr") == "Demarrer");

    // Fill-empty mode keeps the existing French and only adds what is missing.
    const LocImportPreview fill = PreviewLocImport(live, csv, LocImportFormat::Csv, "", LocImportOptions::Mode::FillEmpty);
    CHECK(fill.ok && fill.diff.changed.empty() && !fill.diff.filled.empty());

    // The line list is capped, with a count of what was left out.
    const LocImportPreview capped = PreviewLocImport(live, csv, LocImportFormat::Csv, "", LocImportOptions::Mode::Overwrite, 2);
    CHECK(capped.lines.size() == 2 && capped.hiddenLines > 0);
    // A long text is clipped in the line, not in the table.
    StringTable longLive;
    longLive.AddLanguage("en");
    longLive.Set("k", "en", "old");
    const LocImportPreview longP = PreviewLocImport(longLive, "key,en\nk," + std::string(500, 'x') + "\n");
    CHECK(longP.ok && longP.lines.size() == 1 && longP.lines[0].size() < 200);
    CHECK(longP.after.Text("k", "en")->size() == 500);
    // Nothing to do.
    const LocImportPreview same = PreviewLocImport(live, "key,en\nmenu.start,Start\n");
    CHECK(same.ok && same.NothingToDo() && same.Summary().find("nothing would change") != std::string::npos);
}

static void TestHostileImport() {
    RUN_SUITE("loc io hostile and malformed import");
    const StringTable live = Sample();
    const std::string before = live.Write();
    std::vector<std::string> inputs = {
        "", " ", "\xEF\xBB\xBF", "{", "}", "[", "{\"keys\":", "{\"keys\": 5}", "{\"keys\": {\"a\": 5}}",
        "{\"keys\": {\"a\": {\"text\": []}}}", "{\"keys\": {\"a\": {\"text\": {\"en\": 7}}}}", "{\"languages\": \"en\"}",
        "{\"keys\": {\"a\": {\"text\": {\"en\": \"\\ud800\"}}}}", "{\"keys\": {\"a\": {\"text\": {\"en\": \"\\u0000\"}}}}",
        "{\"keys\": {\"a\": {\"text\": {\"en\": \"x\"}}}, \"keys\": {\"b\": {}}}", "{\"a\": \"x\", \"a\": \"y\"}",
        "{\"\": \"x\"}", "{\"a b\\nc\": \"x\"}", "{\"a\": \"unterminated", "{\"a\": tru}", "{\"a\": 1e999999}",
        "key,en\n\"unterminated,x\n", "key,en\na,\"b\"c\n", "no key column,here\nx,y\n", "key\n\n\n\n", ",,,\n,,,\n",
        std::string("key,en\na,b\0c\n", 14), "\xFF\xFE\x00\x00", "\xFF\xFEk\x00", "\xC0\xAF\xED\xA0\x80,\xF8\x88\x80\x80\x80\n",
        "key,en\n" + std::string(2000, ','), std::string(1000, '\n'),
    };
    // Deep nesting: must be refused or flattened, never overflow the stack.
    inputs.push_back(std::string(200000, '['));
    inputs.push_back("{\"a\":" + std::string(200000, '{'));
    std::string deep = "{\"a\":";
    for (int i = 0; i < 5000; ++i) deep += "{\"b\":";
    deep += "\"x\"" + std::string(5000, '}') + "}";
    inputs.push_back(deep);
    // A very wide sheet and a very long field.
    std::string wide = "key";
    for (int i = 0; i < 20000; ++i) wide += ",l" + std::to_string(i);
    inputs.push_back(wide + "\nk\n");
    inputs.push_back("key,en\nk," + std::string(3 << 20, 'z') + "\n");
    // Random bytes, some of them structured like the formats.
    std::mt19937 rng(1234);
    for (int n = 0; n < 200; ++n) {
        std::string junk;
        const int len = (int)(rng() % 200);
        for (int i = 0; i < len; ++i) {
            const char alphabet[] = "{}[]\",:\\ \n\tkey,en0123456789tfnu";
            junk += (rng() % 4 == 0) ? (char)(rng() & 0xFF) : alphabet[rng() % (sizeof(alphabet) - 1)];
        }
        inputs.push_back(junk);
    }
    int accepted = 0, refused = 0;
    for (const std::string& in : inputs) {
        for (LocImportFormat f : {LocImportFormat::Auto, LocImportFormat::Csv, LocImportFormat::Json}) {
            const LocImportPreview p = PreviewLocImport(live, in, f, "xx");
            CHECK(live.Write() == before);               // the live table is never touched by a preview
            if (p.ok) { ++accepted; CHECK(p.error.empty()); }
            else { ++refused; CHECK(!p.error.empty() && !ApplyLocImport(const_cast<StringTable&>(live), p)); }
            (void)p.Summary();
        }
    }
    CHECK(refused > 0 && accepted > 0);
    CHECK(live.Write() == before);
    // Over the size limit: refused before parsing.
    const LocImportPreview huge = PreviewLocImport(live, std::string(kLocImportMaxBytes + 1, 'a'));
    CHECK(!huge.ok && huge.error.find("32 MB") != std::string::npos);
    // A refused file reports why, and Apply does nothing.
    StringTable target = live;
    const LocImportPreview bad = PreviewLocImport(live, "{\"keys\":", LocImportFormat::Json);
    CHECK(!bad.ok && !ApplyLocImport(target, bad) && target.Write() == before);
}

static std::string FontPath() { return std::string(OKAY_SOURCE_DIR) + "/assets/fonts/DejaVuSans.ttf"; }

static void TestFontCoverage() {
    RUN_SUITE("loc io font coverage");
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("ru", "Russian");
    t.AddLanguage("ja", "Japanese");
    t.defaultLanguage = "en";
    t.Set("greet", "en", "Hello");
    t.Set("greet", "ru", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");              // Privet
    t.Set("greet", "ja", "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF");   // konnichiwa (hiragana)
    t.Set("bye", "ja", "\xE3\x81\x93\xE3\x81\x93");                                          // the same character twice

    const LocFontView en = LocWindowFontCoverage(t, "en", FontPath());
    CHECK(en.ok && en.clean && en.missingCharacters == 0);
    CHECK(en.Summary() == "Every character can be drawn.");
    const LocFontView ru = LocWindowFontCoverage(t, "ru", FontPath());
    CHECK(ru.ok && ru.clean);                                                 // DejaVu Sans has Cyrillic
    const LocFontView ja = LocWindowFontCoverage(t, "ja", FontPath());
    CHECK(ja.ok && !ja.clean && ja.missingCharacters == 5);                   // the 5 distinct hiragana of konnichiwa
    CHECK(ja.undrawableTexts == 2);
    CHECK(!ja.lines.empty() && ja.lines[0].find("ja: no font draws U+") == 0);
    CHECK(ja.Summary().find("no font can draw") != std::string::npos);
    bool koSeen = false;
    for (const LocGlyphGap& g : ja.report.languages[0].missing)
        if (g.codePoint == 0x3053) { koSeen = true; CHECK(g.count == 3 && !g.firstKey.empty()); }
    CHECK(koSeen);
    // Every language at once: only ja is reported broken.
    const LocFontView all = LocWindowFontCoverage(t, "", FontPath());
    CHECK(all.ok && !all.clean && all.report.languages.size() == 3);
    // Wrong input is reported, not crashed on.
    const LocFontView bad = LocWindowFontCoverage(t, "en", FontPath() + ".missing");
    CHECK(!bad.ok && !bad.error.empty() && bad.Summary().find("Font check failed") == 0);
    const LocFontView nolang = LocWindowFontCoverage(t, "xx", FontPath());
    CHECK(!nolang.ok);
    // A file that is not a font.
    const LocFontView notFont = LocWindowFontCoverage(t, "en", std::string(OKAY_SOURCE_DIR) + "/CMakeLists.txt");
    CHECK(!notFont.ok);
    // The line cap.
    const LocFontView capped = LocWindowFontCoverage(t, "ja", FontPath(), "", false, 1);
    CHECK(capped.lines.size() == 1 && capped.missingCharacters == ja.missingCharacters);
}

static void TestVisualOrder() {
    RUN_SUITE("loc io right to left order");
    const std::string shalom = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";          // shin lamed vav final-mem
    const std::string shalomVisual = "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9";
    CHECK(LocVisualOrder(shalom, true) == shalomVisual);
    CHECK(LocVisualOrder("", true).empty());
    CHECK(LocVisualOrder("Hello World", true) == "Hello World");              // Latin only: the same order
    CHECK(LocVisualOrder("Hello World", false) == "Hello World");
    const std::string shin = "\xD7\xA9", lamed = "\xD7\x9C";
    // A Latin word inside right-to-left text keeps its letters in order; the first word ends up on the right.
    CHECK(LocVisualOrder("Hello " + shin + lamed, true) == lamed + shin + " Hello");
    // Digits keep their order too.
    CHECK(LocVisualOrder(shin + lamed + " 123", true) == "123 " + lamed + shin);
    // A right-to-left word inside left-to-right text.
    CHECK(LocVisualOrder("abc " + shin + lamed + " def", false) == "abc " + lamed + shin + " def");
    // Brackets flip in a right-to-left run.
    CHECK(LocVisualOrder("(" + shin + lamed + ")", true) == "(" + lamed + shin + ")");
    // A placeholder stays whole and readable.
    CHECK(LocVisualOrder(shin + lamed + " {0}", true) == "{0} " + lamed + shin);
    CHECK(LocVisualOrder(shin + " {name} " + lamed, true) == lamed + " {name} " + shin);
    // Each line on its own.
    CHECK(LocVisualOrder(shin + lamed + "\n" + shin + lamed, true) == lamed + shin + "\n" + lamed + shin);
    // A combining mark stays with its letter (Hebrew point after shin).
    CHECK(LocVisualOrder(shin + "\xD6\xB0" + lamed, true) == lamed + shin + "\xD6\xB0");
    // Arabic.
    const std::string alef = "\xD8\xA7", ba = "\xD8\xA8";
    CHECK(LocVisualOrder(alef + ba, true) == ba + alef);
    CHECK(LocVisualOrder(alef + " 12 " + ba, true) == ba + " 12 " + alef);
    // Arabic-Indic digits keep their order.
    CHECK(LocVisualOrder(alef + " \xD9\xA1\xD9\xA2", true) == "\xD9\xA1\xD9\xA2 " + alef);
    // Detection.
    CHECK(LocHasRtlText(shalom) && LocHasRtlText("abc " + alef) && !LocHasRtlText("abc 123 (x)") && !LocHasRtlText(""));
    // Garbage in, same number of bytes out, no crash.
    std::mt19937 rng(77);
    for (int n = 0; n < 300; ++n) {
        std::string s;
        const int len = (int)(rng() % 60);
        for (int i = 0; i < len; ++i) {
            switch (rng() % 5) {
                case 0: s += shin; break;
                case 1: s += "a"; break;
                case 2: s += "(){}[]<>"[rng() % 8]; break;
                case 3: s += " \n123"[rng() % 5]; break;
                default: s += (char)(rng() & 0xFF); break;
            }
        }
        CHECK(LocVisualOrder(s, true).size() == s.size());
        CHECK(LocVisualOrder(s, false).size() == s.size());
    }
    // Long input stays fast and bounded.
    CHECK(LocVisualOrder(std::string(200000, 'a') + shin, true).size() == 200001 + 1);
}

static void TestPreviewUsesVisualOrder() {
    RUN_SUITE("loc io preview uses visual order");
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("he", "Hebrew");
    t.defaultLanguage = "en";
    t.Set("score", "en", "Score: {0}");
    t.Set("score", "he", "\xD7\xA0\xD7\x99\xD7\xA7\xD7\x95\xD7\x93: {0}");   // "nikud: {0}"
    const LocWindowPreview he = PreviewLocKey(t, "score", "he", false);
    CHECK(he.mirrored && !he.visual.empty());
    CHECK(he.visual == LocVisualOrder(he.plain, true));
    CHECK(he.visual.find("{0}") != std::string::npos);                       // the placeholder is not reversed
    CHECK(he.visual.find("{0}") == 0 || he.visual.find("{0}") != std::string::npos);
    CHECK(he.text == LocMirrorText(he.plain));                              // the layout mirror is still there
    const LocWindowPreview en = PreviewLocKey(t, "score", "en", false);
    CHECK(!en.mirrored && en.visual.empty());
    // Pseudo-localized right-to-left text is reordered after pseudo-localization.
    const LocWindowPreview pseudo = PreviewLocKey(t, "score", "he", true);
    CHECK(pseudo.pseudo && pseudo.mirrored && !pseudo.visual.empty());
}

int main() {
    TestJsonRoundTrip();
    TestImportPreview();
    TestHostileImport();
    TestFontCoverage();
    TestVisualOrder();
    TestPreviewUsesVisualOrder();
    TEST_MAIN_RESULT();
}
