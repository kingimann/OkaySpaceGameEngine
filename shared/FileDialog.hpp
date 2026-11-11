// FileDialog: open file / save file / choose folder, written for OkaySpace (replaces the vendored
// tinyfiledialogs). No third-party code, no ImGui in this header, no <mutex>/<thread>.
//
//   Windows : the system's own dialogs (IFileOpenDialog / IFileSaveDialog, Unicode paths).
//   Linux   : `zenity`, then `kdialog` (started with an argument vector, never a shell string),
//   macOS   : `osascript` (the same, argument vector),
//   anywhere that has none of those (or when OKAY_FILE_DIALOG=builtin is set): the built-in
//   browser drawn with ImGui by FileDialogUI.cpp (DrawBuiltin), so the feature never needs an
//   outside tool.
//
// Callers use Open / Save / Folder with a callback. With a system dialog the callback runs before
// the call returns; with the built-in browser it runs later, from DrawBuiltin(), when the user
// picks something. Cancel never calls the callback. Callbacks must therefore not keep
// references to locals of the calling function (capture by value).
//
// The pure helpers (patterns, paths, argument lists, result parsing, the browser model) have no
// OS or UI dependency and are covered by tests/test_file_dialog.cpp.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace okay::filedialog {

struct Filter {
    std::string name;                    // "Images"
    std::vector<std::string> patterns;   // {"*.png", "*.jpg"}; "*" or "*.*" means everything
};

enum class Kind { OpenFile, SaveFile, Folder };

struct Request {
    Kind kind = Kind::OpenFile;
    std::string title;
    std::string startPath;               // a folder, or (for save) a full suggested path
    std::vector<Filter> filters;         // none = every file
    bool multi = false;                  // open only: allow several files
};

// One filter group from the old-style (description, pattern array) pair; none when n == 0.
inline std::vector<Filter> Filters(const char* description, const char* const* patterns, int n) {
    std::vector<Filter> v;
    if (n > 0 && patterns) { Filter f; f.name = description ? description : ""; for (int i = 0; i < n; ++i) f.patterns.push_back(patterns[i]); v.push_back(std::move(f)); }
    return v;
}
inline std::vector<Filter> Filters(const char* description, std::initializer_list<const char*> patterns) {
    return Filters(description, patterns.begin(), (int)patterns.size());
}

using Callback = std::function<void(const std::vector<std::string>&)>;

// ---- Pure helpers ------------------------------------------------------------------------

// Backslashes become '/', "a//b" and "a/./b" collapse, "a/b/../c" resolves, a trailing '/'
// goes (except for the root "/" and "C:/"). Empty stays empty. Does not touch the disk.
std::string NormalizePath(const std::string& path);
// The last path component / everything before it (normalised).
std::string FileNameOf(const std::string& path);
std::string ParentOf(const std::string& path);
// Lower-case extension with the dot (".png"), "" when none. ".bashrc" has none.
std::string ExtensionOf(const std::string& path);

// Splits "*.png;*.jpg", "*.png *.jpg" or "*.png,*.jpg" into patterns (trimmed, empties dropped).
std::vector<std::string> ParsePatterns(const std::string& text);
// Case-insensitive glob with '*' and '?'. "*" and "*.*" match every name.
bool MatchPattern(const std::string& pattern, const std::string& name);
bool MatchesAny(const std::vector<std::string>& patterns, const std::string& name);
// True when `name` passes the filter (an empty filter or one with no patterns passes everything).
bool MatchesFilter(const Filter& filter, const std::string& name);
// The plain extension a pattern asks for: "*.png" -> ".png", "*.tar.gz" -> ".tar.gz"; "" for "*",
// "*.*", "data*.png" and other patterns that are not "*" + a literal suffix.
std::string LiteralExtension(const std::string& pattern);
// Save dialogs: when `path` has no extension and the filter names exactly one literal extension
// (its first pattern), append it. A path that already has one is left alone.
std::string EnforceExtension(const std::string& path, const Filter* filter);

// Splits a start path into the folder to show and a suggested file name. A folder (existing or
// ending in '/') gives {folder, ""}; "dir/name.ext" gives {dir, "name.ext"}; "name.ext" alone
// gives {"", "name.ext"}.
struct StartSplit { std::string dir, name; };
StartSplit SplitStart(const std::string& startPath, bool pathIsFolderOnDisk);

// Argument vectors for the helper programs (element 0 is the program name). User text only ever
// travels as one element, so nothing can be injected into a shell (there is none).
std::vector<std::string> ZenityArgs(const Request& r);
std::vector<std::string> KdialogArgs(const Request& r);
std::vector<std::string> OsascriptArgs(const Request& r);
// Quotes text for an AppleScript string literal (no raw newlines, backslash and quote escaped).
std::string AppleScriptQuote(const std::string& text);
// Turns what a helper printed into paths: one per line, CR/LF stripped, empty lines dropped, and
// only the first entry kept when the request is not multi-select.
std::vector<std::string> ParseOutput(const std::string& out, bool multi);
// Single-file results: appends the extension a save dialog should have added.
std::vector<std::string> FinishResults(const Request& r, std::vector<std::string> paths);

// ---- Backends ----------------------------------------------------------------------------

enum class Backend { Win32, Zenity, Kdialog, Osascript, Builtin };
const char* BackendName(Backend b);
// The backend this process uses. Decided once (searches PATH); OKAY_FILE_DIALOG=builtin|zenity|
// kdialog|osascript forces one for testing.
Backend CurrentBackend();
// Test hook: forget the cached decision.
void ResetBackendForTests();
// Looks for a program on PATH (no shell). Used for the Linux/macOS probes.
bool FindOnPath(const std::string& program, std::string* fullPath = nullptr);

// Runs a program (argv[0] looked up on PATH), captures stdout. Returns false when it could not be
// started. exitCode gets the process's exit status.
bool RunProcess(const std::vector<std::string>& argv, std::string& out, int& exitCode);

// Blocking call into the system dialog. Returns true when a system dialog handled the request
// (paths is empty on cancel). Returns false when none is available (use the built-in browser).
bool RunNative(const Request& r, std::vector<std::string>& paths);

// ---- The call sites use these -------------------------------------------------------------

void Open(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, bool multi, Callback cb);
void Save(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, Callback cb);
void Folder(const std::string& title, const std::string& startPath, Callback cb);
void Show(const Request& r, Callback cb);   // the general form of the three above

// Single-path convenience wrappers: cb gets the one chosen path.
void OpenOne(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, std::function<void(const std::string&)> cb);
void SaveOne(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, std::function<void(const std::string&)> cb);
void FolderOne(const std::string& title, const std::string& startPath, std::function<void(const std::string&)> cb);

// ---- Built-in browser -----------------------------------------------------------------------

struct BrowserEntry { std::string name; bool isDir = false; std::uint64_t size = 0; };

// The state of the built-in browser, without any drawing, so it can be tested.
class BrowserModel {
public:
    void Begin(const Request& r);
    const Request& Req() const { return req_; }
    const std::string& Dir() const { return dir_; }
    // Shows `dir`; false (and unchanged) when it is not a readable folder.
    bool GoTo(const std::string& dir);
    bool Up();
    bool Refresh();
    // Entering a sub folder by name. A file name selects it instead (Enter == false).
    bool Enter(const std::string& name);
    const std::vector<BrowserEntry>& Entries() const { return entries_; }   // folders first, then files, A-Z; filtered
    int FilterIndex() const { return filterIndex_; }
    void SetFilterIndex(int i);          // re-lists
    bool ShowHidden() const { return showHidden_; }
    void SetShowHidden(bool v);          // re-lists
    // Selection (files; for Folder requests the current folder is the answer unless one is selected).
    void Select(const std::string& name, bool additive);
    void ClearSelection() { selected_.clear(); }
    const std::vector<std::string>& Selected() const { return selected_; }
    bool IsSelected(const std::string& name) const;
    // The text of the name field (save: the file name).
    std::string typed;
    // What Accept would give back; empty when nothing valid is chosen yet (Why() says why).
    std::vector<std::string> Result();
    const std::string& Why() const { return why_; }
    // Save only: true when the single result already exists on disk (the dialog asks to replace it).
    bool ResultExists();
    // Shortcuts: home, root / drives, and the folder Begin started in.
    std::vector<std::pair<std::string, std::string>> Places() const;   // {label, path}
private:
    bool List();
    Request req_;
    std::string dir_;
    std::vector<BrowserEntry> entries_;
    std::vector<std::string> selected_;
    std::string why_;
    int filterIndex_ = 0;
    bool showHidden_ = false;
};

// Draws the built-in browser as a modal window when a request is waiting; call once per frame
// from the UI loop (FileDialogUI.cpp, needs ImGui). When the user accepts, the callback runs
// from inside this call. Does nothing when no request is pending.
void DrawBuiltin();
bool BuiltinOpen();                    // a built-in dialog is showing or queued
BrowserModel* BuiltinModel();          // the one showing now, or null
// Ends the showing dialog: accepted -> the callback gets model->Result(); otherwise nothing is
// called. The next queued request (if any) starts.
void FinishBuiltin(bool accepted);
void CancelBuiltin();                  // drop the showing and every queued dialog without calling back

// Tests / automation: answers the showing built-in dialog with `paths` as if the user chose them.
bool AnswerBuiltinForTests(const std::vector<std::string>& paths);

}  // namespace okay::filedialog
