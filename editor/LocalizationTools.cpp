#include "LocalizationTools.hpp"

#include <algorithm>
#include <set>

namespace oked {

namespace {

bool IsIssueShown(const okay::LocIssue& issue) {
    return issue.severity != okay::LocSeverity::Info;
}

void AppendUses(const LocWindowQuery& query, okay::LocScanResult& scan) {
    for (const auto& file : query.extraFiles)
        okay::LocScanText(file.first, file.second, {}, scan.uses);
    for (const std::string& key : query.sceneKeys) {
        if (key.empty()) continue;
        okay::LocScanUse use;
        use.key = key;
        use.file = "scene";
        use.how = "scene";
        scan.uses.push_back(std::move(use));
    }
}

} // namespace

okay::LocScanResult ScanLocWindow(const okay::StringTable& table, const LocWindowQuery& query) {
    okay::LocScanResult scan;
    if (!query.projectDir.empty()) scan = okay::LocScanProject(query.projectDir, table);
    AppendUses(query, scan);
    okay::LocScanResolve(table, {}, scan);
    return scan;
}

std::vector<LocWindowIssue> LocWindowIssues(const LocWindowModel& model) {
    std::vector<LocWindowIssue> out;
    for (const okay::LocIssue& issue : model.report.issues) {
        if (!IsIssueShown(issue)) continue;
        LocWindowIssue row;
        row.severity = issue.severity;
        row.kind = issue.kind;
        row.key = issue.key;
        row.language = issue.language;
        row.message = issue.message;
        out.push_back(std::move(row));
    }
    std::stable_sort(out.begin(), out.end(), [](const LocWindowIssue& a, const LocWindowIssue& b) {
        if (a.severity != b.severity) return a.severity == okay::LocSeverity::Error;
        return false;
    });
    if (model.scanned) {
        for (const okay::LocScanUse& use : model.scan.undefined) {
            LocWindowIssue row;
            row.severity = okay::LocSeverity::Warning;
            row.kind = okay::LocIssueKind::MissingText;
            row.key = use.key;
            row.undefined = true;
            row.message = "used in " + use.file;
            if (use.line > 0) row.message += ":" + std::to_string(use.line);
            if (!use.how.empty()) row.message += " (" + use.how + ")";
            row.message += ", but the table has no such key";
            out.push_back(std::move(row));
        }
    }
    return out;
}

std::string LocWindowIssueSummary(const LocWindowModel& model) {
    const int missing = model.report.Count(okay::LocIssueKind::MissingText);
    const int mismatch = model.report.Count(okay::LocIssueKind::PlaceholderMismatch);
    int other = 0;
    for (const okay::LocIssue& issue : model.report.issues) {
        if (!IsIssueShown(issue)) continue;
        if (issue.kind == okay::LocIssueKind::MissingText || issue.kind == okay::LocIssueKind::PlaceholderMismatch) continue;
        ++other;
    }
    int undefined = 0;
    if (model.scanned) {
        std::set<std::string> keys;
        for (const okay::LocScanUse& use : model.scan.undefined) keys.insert(use.key);
        undefined = (int)keys.size();
    }
    if (missing == 0 && mismatch == 0 && other == 0 && undefined == 0)
        return "No missing keys or placeholder problems.";
    std::string text;
    auto add = [&](int n, const char* one, const char* many) {
        if (n <= 0) return;
        if (!text.empty()) text += ", ";
        text += std::to_string(n) + " " + (n == 1 ? one : many);
    };
    add(missing, "missing translation", "missing translations");
    add(mismatch, "placeholder mismatch", "placeholder mismatches");
    add(other, "other problem", "other problems");
    add(undefined, "undefined key", "undefined keys");
    return text;
}

LocWindowPreview PreviewLocKey(const okay::StringTable& table, const std::string& key,
                               const std::string& language, bool pseudo) {
    okay::LocPreview preview(table);
    const bool pseudoLanguage = language == okay::Localization::kPseudo;
    if (!language.empty() && !pseudoLanguage) preview.SetLanguage(language);
    if (pseudo || pseudoLanguage) preview.SetPseudo(true);
    LocWindowPreview out;
    out.language = preview.Language();
    out.pseudo = preview.Pseudo();
    out.mirrored = preview.Rtl();
    if (!preview.TryGet(key, out.plain)) out.missing = true;
    out.text = preview.Get(key);
    return out;
}

bool LocWindowShowsPreview(const std::string& language) {
    return language == okay::Localization::kPseudo || okay::LocIsRtl(language);
}

LocWindowModel BuildLocWindowModel(const okay::StringTable& table, const LocWindowQuery& query,
                                   const okay::LocScanResult* previousScan) {
    okay::LocTable tools(table);
    okay::LocGridOptions options;
    options.search = query.search;
    options.sortByIssues = query.filter == LocWindowFilter::Issues || query.filter == LocWindowFilter::PlaceholderMismatch;
    if (query.filter == LocWindowFilter::PlaceholderMismatch)
        options.stateMask = okay::LocStateBit(okay::LocCellState::PlaceholderMismatch);
    else if (query.filter == LocWindowFilter::SameAsSource)
        options.stateMask = okay::LocStateBit(okay::LocCellState::SameAsSource);

    LocWindowModel model;
    model.grid = tools.Grid(options);
    model.stats = tools.AllStats();
    model.report = okay::LocCheck(table);
    model.missing = okay::LocMissingReport(table);

    if (query.scan) {
        model.scan = ScanLocWindow(table, query);
        model.scanned = true;
    } else if (previousScan) {
        model.scan = *previousScan;
        okay::LocScanResolve(table, {}, model.scan);
        model.scanned = true;
    }

    if (query.filter == LocWindowFilter::MissingIn) {
        const int column = model.grid.ColumnOf(query.filterLanguage);
        if (column < 0) model.grid.rows.clear();
        else {
            model.grid.rows.erase(std::remove_if(model.grid.rows.begin(), model.grid.rows.end(),
                                                 [&](const okay::LocGridRow& row) {
                                                     return column >= (int)row.cells.size() ||
                                                            row.cells[(std::size_t)column].state != okay::LocCellState::Missing;
                                                 }),
                                  model.grid.rows.end());
        }
    } else if (query.filter == LocWindowFilter::Unused) {
        std::set<std::string> unused;
        if (model.scanned) unused.insert(model.scan.unused.begin(), model.scan.unused.end());
        model.grid.rows.erase(std::remove_if(model.grid.rows.begin(), model.grid.rows.end(),
                                             [&](const okay::LocGridRow& row) { return unused.count(row.key) == 0; }),
                              model.grid.rows.end());
    } else if (query.filter == LocWindowFilter::Issues) {
        const std::vector<LocWindowIssue> issues = LocWindowIssues(model);
        std::set<std::string> keys;
        for (const LocWindowIssue& issue : issues)
            if (!issue.undefined) keys.insert(issue.key);
        model.grid.rows.erase(std::remove_if(model.grid.rows.begin(), model.grid.rows.end(),
                                             [&](const okay::LocGridRow& row) { return keys.count(row.key) == 0; }),
                              model.grid.rows.end());
    }
    return model;
}

} // namespace oked
