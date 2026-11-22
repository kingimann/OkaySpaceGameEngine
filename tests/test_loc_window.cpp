// The Localization window's path onto LocTable, LocCheck, LocScan and LocPreview
// (editor/LocalizationTools). DrawLocalizationWindow calls these same functions:
// a missing translation, a placeholder mismatch and a pseudo-locale preview have
// to be reachable here, with no ImGui.
#include "test_framework.hpp"
#include "LocalizationTools.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace oked;
namespace fs = std::filesystem;

static StringTable Sample() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("fr", "French");
    t.AddLanguage("ar", "Arabic");
    t.defaultLanguage = "en";
    t.Set("menu.start", "en", "Start");
    t.Set("menu.start", "fr", "Commencer");
    t.Set("menu.start", "ar", "Start");
    t.Set("menu.quit", "en", "Quit");                 // no French text
    t.Set("hud.score", "en", "Score: {0}");
    t.Set("hud.score", "fr", "Score");                // {0} dropped
    t.Set("menu.hello", "en", "Hello");
    t.Set("menu.hello", "fr", "Hello");               // same as the source
    t.Set("pause.title", "en", "Paused");             // a built-in key: never unused
    return t;
}

static bool HasKey(const LocGrid& grid, const std::string& key) {
    for (const LocGridRow& row : grid.rows)
        if (row.key == key) return true;
    return false;
}

static const LocCell* Cell(const LocGrid& grid, const std::string& key, const std::string& lang) {
    const int column = grid.ColumnOf(lang);
    if (column < 0) return nullptr;
    for (int r = 0; r < grid.RowCount(); ++r)
        if (grid.rows[(std::size_t)r].key == key) return &grid.Cell(r, column);
    return nullptr;
}

static bool HasIssue(const std::vector<LocWindowIssue>& issues, const std::string& key, LocIssueKind kind) {
    for (const LocWindowIssue& issue : issues)
        if (issue.key == key && issue.kind == kind && !issue.undefined) return true;
    return false;
}

static void TestMissingKey() {
    RUN_SUITE("loc window missing key");
    const StringTable t = Sample();
    LocWindowQuery query;
    query.filter = LocWindowFilter::MissingIn;
    query.filterLanguage = "fr";
    const LocWindowModel model = BuildLocWindowModel(t, query);
    CHECK(HasKey(model.grid, "menu.quit"));
    CHECK(!HasKey(model.grid, "menu.start"));          // French has a text
    CHECK(!HasKey(model.grid, "hud.score"));           // French has a text, even a broken one
    const LocCell* cell = Cell(model.grid, "menu.quit", "fr");
    CHECK(cell && cell->state == LocCellState::Missing);
    bool listed = false;
    for (const LocMissing& missing : model.missing)
        if (missing.language == "fr") {
            listed = true;
            CHECK(std::find(missing.keys.begin(), missing.keys.end(), "menu.quit") != missing.keys.end());
            CHECK(std::find(missing.keys.begin(), missing.keys.end(), "menu.start") == missing.keys.end());
        }
    CHECK(listed);
    const std::vector<LocWindowIssue> issues = LocWindowIssues(model);
    CHECK(HasIssue(issues, "menu.quit", LocIssueKind::MissingText));
    const std::string summary = LocWindowIssueSummary(model);
    CHECK(summary.find("missing translation") != std::string::npos);

    query.filter = LocWindowFilter::Issues;
    query.filterLanguage.clear();
    const LocWindowModel reported = BuildLocWindowModel(t, query);
    CHECK(HasKey(reported.grid, "menu.quit"));
    CHECK(!HasKey(reported.grid, "menu.start"));
}

static void TestPlaceholderMismatch() {
    RUN_SUITE("loc window placeholder mismatch");
    const StringTable t = Sample();
    LocWindowQuery query;
    query.filter = LocWindowFilter::PlaceholderMismatch;
    const LocWindowModel model = BuildLocWindowModel(t, query);
    CHECK(HasKey(model.grid, "hud.score"));
    CHECK(!HasKey(model.grid, "menu.quit"));           // missing is a different filter
    CHECK(!HasKey(model.grid, "menu.hello"));
    const LocCell* cell = Cell(model.grid, "hud.score", "fr");
    CHECK(cell && cell->state == LocCellState::PlaceholderMismatch);
    const LocCell* source = Cell(model.grid, "hud.score", "en");
    CHECK(source && source->state == LocCellState::Translated);
    CHECK(model.report.Count(LocIssueKind::PlaceholderMismatch) >= 1);
    const std::vector<LocWindowIssue> issues = LocWindowIssues(model);
    CHECK(HasIssue(issues, "hud.score", LocIssueKind::PlaceholderMismatch));
    bool named = false;
    for (const LocWindowIssue& issue : issues)
        if (issue.key == "hud.score" && issue.kind == LocIssueKind::PlaceholderMismatch) {
            named = issue.language == "fr" && issue.message.find("{0}") != std::string::npos;
            CHECK(issue.severity == LocSeverity::Error);
        }
    CHECK(named);
    const std::string summary = LocWindowIssueSummary(model);
    CHECK(summary.find("placeholder mismatch") != std::string::npos);

    query.search = "SCORE";                            // the grid's own search, case-insensitive
    const LocWindowModel found = BuildLocWindowModel(t, query);
    CHECK(HasKey(found.grid, "hud.score"));
    CHECK(!HasKey(found.grid, "menu.quit"));
    query.search = "no-such-text";
    CHECK(BuildLocWindowModel(t, query).grid.RowCount() == 0);

    query.search.clear();
    query.filter = LocWindowFilter::SameAsSource;
    const LocWindowModel same = BuildLocWindowModel(t, query);
    CHECK(HasKey(same.grid, "menu.hello"));          // French copied "Hello"
    CHECK(HasKey(same.grid, "menu.start"));          // Arabic is still "Start"
    CHECK(!HasKey(same.grid, "menu.quit"));          // missing is not a copy
    CHECK(!HasKey(same.grid, "hud.score"));
    const LocCell* hello = Cell(same.grid, "menu.hello", "fr");
    CHECK(hello && hello->state == LocCellState::SameAsSource);
    const LocCell* arabic = Cell(same.grid, "menu.start", "ar");
    CHECK(arabic && arabic->state == LocCellState::SameAsSource);
}

static void TestPseudoPreview() {
    RUN_SUITE("loc window pseudo preview");
    const StringTable t = Sample();
    Localization::Reset();
    Localization::SetTable(t);
    const std::string languageBefore = Localization::Language();
    const std::string tableBefore = Localization::Table().Write();
    const std::size_t reportedBefore = Localization::Reported().size();

    const LocWindowPreview preview = PreviewLocKey(t, "hud.score", "", true);
    CHECK(preview.pseudo);
    CHECK(!preview.missing);
    CHECK(preview.plain == "Score: {0}");
    CHECK(preview.text == LocPseudoText("Score: {0}"));
    CHECK(preview.text.find("[!!! ") == 0);
    CHECK(preview.text.find("{0}") != std::string::npos);   // the placeholder survives
    CHECK(preview.text != Localization::Pseudo("Score: {0}")); // the window line is LocPreview, not the scene's brackets
    CHECK(preview.text != preview.plain);

    const LocWindowPreview absent = PreviewLocKey(t, "no.such.key", "fr", true);
    CHECK(absent.missing);
    CHECK(absent.pseudo);
    CHECK(absent.text == LocPseudoText("no.such.key"));

    const LocWindowPreview french = PreviewLocKey(t, "menu.start", "fr", false);
    CHECK(!french.pseudo);
    CHECK(!french.mirrored);
    CHECK(french.text == "Commencer");
    CHECK(french.language == "fr");

    const LocWindowPreview arabic = PreviewLocKey(t, "menu.start", "ar", false);
    CHECK(arabic.mirrored);
    CHECK(!arabic.pseudo);
    CHECK(arabic.language == "ar");
    LocPreview reference(t);
    reference.SetLanguage("ar");
    CHECK(arabic.text == reference.Get("menu.start"));
    CHECK(arabic.text == LocMirrorText("Start"));

    const LocWindowPreview both = PreviewLocKey(t, "menu.start", "ar", true);
    CHECK(both.pseudo && both.mirrored);
    reference.SetPseudo(true);
    CHECK(both.text == reference.Get("menu.start"));

    CHECK(LocWindowShowsPreview(Localization::kPseudo));
    CHECK(LocWindowShowsPreview("ar"));
    CHECK(LocWindowShowsPreview("he"));
    CHECK(!LocWindowShowsPreview("fr"));
    CHECK(!LocWindowShowsPreview("en"));

    // The preview reads a table. It does not switch the language, log a missing key, or edit anything.
    CHECK(Localization::Language() == languageBefore);
    CHECK(Localization::Table().Write() == tableBefore);
    CHECK(Localization::Reported().size() == reportedBefore);
    Localization::Reset();
}

static void TestScan() {
    RUN_SUITE("loc window scan");
    const fs::path dir = fs::temp_directory_path() / "okay-loc-window-scan";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    {
        std::ofstream out(dir / "talk.okay");
        out << "title = loc(\"menu.start\")\nloc(\"missing.key\")\n";
    }
    StringTable t = Sample();
    LocWindowQuery query;
    query.scan = true;
    query.projectDir = dir.u8string();
    query.filter = LocWindowFilter::Unused;
    const LocWindowModel scanned = BuildLocWindowModel(t, query);
    CHECK(scanned.scanned);
    CHECK(scanned.scan.filesScanned >= 1);
    CHECK(std::find(scanned.scan.unused.begin(), scanned.scan.unused.end(), "menu.quit") != scanned.scan.unused.end());
    CHECK(std::find(scanned.scan.unused.begin(), scanned.scan.unused.end(), "menu.start") == scanned.scan.unused.end());
    CHECK(std::find(scanned.scan.unused.begin(), scanned.scan.unused.end(), "pause.title") == scanned.scan.unused.end());
    CHECK(HasKey(scanned.grid, "menu.quit"));
    CHECK(!HasKey(scanned.grid, "menu.start"));
    bool undefined = false;
    for (const LocScanUse& use : scanned.scan.undefined)
        if (use.key == "missing.key") {
            undefined = use.file.find("talk.okay") != std::string::npos && use.line > 0;
        }
    CHECK(undefined);
    bool inReport = false;
    for (const LocWindowIssue& issue : LocWindowIssues(scanned))
        if (issue.undefined && issue.key == "missing.key") inReport = true;
    CHECK(inReport);
    CHECK(LocWindowIssueSummary(scanned).find("undefined key") != std::string::npos);

    // A key the open scene uses is not unused, even when no file names it.
    query.sceneKeys.push_back("menu.quit");
    const LocWindowModel withScene = BuildLocWindowModel(t, query);
    CHECK(std::find(withScene.scan.unused.begin(), withScene.scan.unused.end(), "menu.quit") == withScene.scan.unused.end());

    // An inline script is read the same way as a file.
    query.sceneKeys.clear();
    query.projectDir.clear();
    query.extraFiles.push_back({"inline.okay", "loc(\"menu.hello\")"});
    const LocWindowModel inlineScript = BuildLocWindowModel(t, query);
    CHECK(std::find(inlineScript.scan.unused.begin(), inlineScript.scan.unused.end(), "menu.hello") == inlineScript.scan.unused.end());
    CHECK(std::find(inlineScript.scan.unused.begin(), inlineScript.scan.unused.end(), "menu.quit") != inlineScript.scan.unused.end());

    // A later edit of the table is compared with the same mentions, without reading the disk again.
    t.Set("menu.added", "en", "Added");
    LocWindowQuery again;
    again.filter = LocWindowFilter::Unused;
    const LocWindowModel refreshed = BuildLocWindowModel(t, again, &scanned.scan);
    CHECK(refreshed.scanned);
    CHECK(std::find(refreshed.scan.unused.begin(), refreshed.scan.unused.end(), "menu.added") != refreshed.scan.unused.end());
    CHECK(HasKey(refreshed.grid, "menu.added"));

    fs::remove_all(dir, ec);
}

static std::string Txt(const StringTable& t, const char* k, const char* l) {
    const std::string* s = t.Text(k, l);
    return s ? *s : std::string();
}

// The window's import confirm step: a plan says what would change and leaves the live table alone;
// CSV, whole-table JSON and one-language JSON (named by the file's stem) all read; a broken file
// says why; Export JSON reads back to the same table.
static void TestImportPlan() {
    RUN_SUITE("loc window import plan (CSV and JSON, confirm before applying)");
    const StringTable live = Sample();
    const std::string before = live.Write();

    CHECK(LocImportFormatFor("a/b/strings.JSON") == LocImportFormat::Json);
    CHECK(LocImportFormatFor("strings.csv") == LocImportFormat::Csv);
    CHECK(LocImportFormatFor("dir.json/strings") == LocImportFormat::Csv);

    // CSV: fills French for menu.quit, changes the French score, adds a key.
    const std::string csv = "key,note,en,fr\nmenu.quit,,Quit,Quitter\nhud.score,,Score: {0},Score : {0}\nmenu.new,,New,Nouveau\n";
    LocImportPlan p = PlanLocImport(live, csv, LocImportFormat::Csv, "sheet.csv");
    CHECK(p.ok && p.Changes());
    CHECK(live.Write() == before);                         // nothing applied yet
    CHECK(Txt(p.after, "menu.quit", "fr") == "Quitter");
    CHECK(p.diff.filled.size() >= 1 && p.diff.changed.size() >= 1 && p.diff.addedKeys.size() == 1);
    const std::string sum = LocImportSummary(p);
    CHECK(sum.find("3 rows") == 0);
    CHECK(sum.find("filled") != std::string::npos && sum.find("changed") != std::string::npos && sum.find("1 new key") != std::string::npos);
    const std::vector<std::string> lines = LocImportChangeLines(p, 100);
    CHECK(std::find(lines.begin(), lines.end(), "menu.new  (new key)") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "fr  menu.quit  filled") != lines.end());
    const std::vector<std::string> cut = LocImportChangeLines(p, 1);
    CHECK(cut.size() == 2 && cut.back().rfind("and ", 0) == 0);

    // The same file again on top of the result: nothing would change.
    const LocImportPlan again = PlanLocImport(p.after, csv, LocImportFormat::Csv, "sheet.csv");
    CHECK(again.ok && !again.Changes());
    CHECK(LocImportSummary(again).find("nothing would change") != std::string::npos);

    // Export JSON and read it into an empty table: the same texts come back.
    std::string exportedLang = "x";
    const std::string json = LocExportJson(p.after, "out/strings.json", &exportedLang);
    CHECK(exportedLang.empty());
    CHECK(json.find("\"keys\"") != std::string::npos);
    const LocImportPlan whole = PlanLocImport(StringTable(), json, LocImportFormat::Json, "export.json");
    CHECK(whole.ok && whole.language.empty());
    CHECK(Txt(whole.after, "menu.quit", "fr") == "Quitter" && Txt(whole.after, "menu.new", "en") == "New");
    CHECK(whole.after.FindLanguage("ar") != nullptr);

    // One language's JSON: the language is the file's stem.
    const LocImportPlan flat = PlanLocImport(live, "{\"menu.quit\": \"Beenden\", \"menu.start\": \"Start\"}",
                                             LocImportFormat::Json, "C:\\loc\\de.json");
    CHECK(flat.ok && flat.language == "de");
    CHECK(Txt(flat.after, "menu.quit", "de") == "Beenden");
    CHECK(flat.diff.addedLanguages.size() == 1 && flat.diff.addedLanguages[0] == "de");
    CHECK(LocImportSummary(flat).find("of de") != std::string::npos);
    CHECK(live.Write() == before);

    // Export to a file named after a language: that language alone, which reads back as it.
    std::string frLang;
    const std::string frJson = LocExportJson(p.after, "loc/fr.json", &frLang);
    CHECK(frLang == "fr" && frJson.find("\"keys\"") == std::string::npos && frJson.find("Quitter") != std::string::npos);
    StringTable enOnly;
    enOnly.AddLanguage("en", "English");
    enOnly.Set("menu.quit", "en", "Quit");
    const LocImportPlan back = PlanLocImport(enOnly, frJson, LocImportFormat::Json, "loc/fr.json");
    CHECK(back.ok && back.language == "fr" && Txt(back.after, "menu.quit", "fr") == "Quitter");

    // A broken file: not ok, the reason is in the summary, nothing to apply.
    const LocImportPlan bad = PlanLocImport(live, "{\"keys\": ", LocImportFormat::Json, "bad.json");
    CHECK(!bad.ok && !bad.Changes() && !bad.error.empty());
    CHECK(LocImportSummary(bad).rfind("Can't import: ", 0) == 0);
    CHECK(LocImportChangeLines(bad, 10).empty());
}

int main() {
    TestMissingKey();
    TestPlaceholderMismatch();
    TestPseudoPreview();
    TestScan();
    TestImportPlan();
    TEST_MAIN_RESULT();
}
