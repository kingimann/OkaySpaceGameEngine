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

int main() {
    TestMissingKey();
    TestPlaceholderMismatch();
    TestPseudoPreview();
    TestScan();
    TEST_MAIN_RESULT();
}
