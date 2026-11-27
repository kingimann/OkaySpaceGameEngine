#pragma once
// Script assist (ScriptAssist.cpp): the Scripting Reference window and the Script Editor's completion, signatures,
// hover docs, C# members, help card, snippet painter, palette and language definition. Called by the Script Editor
// (the Script Editor window's hooks, editor/ScriptEditorAppHost.cpp) and the Help menu. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 3).
#include "imgui.h"
#include "ScriptEditorCodeEdit.hpp" // scripteditor::ScriptPalette, scripteditor::LanguageWords (our code widget's types)
#include <string>
#include <unordered_map>
#include <vector>

namespace oked {

// The Scripting Reference window (Help > Scripting Reference, the Script Editor's Docs); shown while g_showScriptDocs.
void DrawScriptDocs();

// Pending editor commands, filled by the toolbar/popups and applied against the
// code widget each frame; the widget reports the live caret back into it.
struct ScriptCaret {
    int  line = 1, col = 1, pos = 0;   // out: caret position (1-based line/col)
    int  selLen = 0;                   // out: selected character count
    int  gotoLine = 0;                 // in: jump to this 1-based line (0 = none)
    int  gotoCol = 0;                  // in: 1-based column for gotoLine (0 = start of line)
    int  gotoPos = -1;                 // in: move caret to this byte offset (find-next)
    int  gotoSelLen = 0;               // in: select this many chars from gotoPos
    bool toggleComment = false;        // in: toggle "// " on the selected lines
    std::string insert;                // in: insert this text at the caret (snippets)
    int  insertReplaceLen = 0;         // in: (unused since the widget migration)
    std::string replaceAll;            // in: replace the WHOLE buffer (format/rename)
};
// Autocomplete popup state: item count shown this frame (0 = no popup), the
// keyboard-highlighted item, and Esc's hide-until-the-word-changes latch.
extern int g_acCount;
extern int g_acIndex;
extern bool g_acDismiss;

// Autocomplete words (keywords, types, common builtins), plus every identifier in the buffer added to `pool`.
const std::vector<std::string>& ScriptCompletions();
void AddBufferIdentifiers(const char* src, std::vector<std::string>& pool);
// Builtin name -> signature line ("" when unknown).
const std::unordered_map<std::string, std::string>& ScriptSignatureMap();
const std::string* ScriptSignature(const std::string& name);
// Completion kind: the coloured tag letter the popup shows before each item.
enum class AcKind { Keyword, Type, Function, Variable };
AcKind ClassifyCompletion(const std::string& name);
void AcKindTag(AcKind k, char& letter, ImU32& col);
// Fuzzy match score of `pat` against `cand` (higher is better, negative = no match).
int FuzzyScore(const std::string& pat, const std::string& cand);
// Hover doc for a builtin (null when none).
const std::string* ScriptDoc(const std::string& name);
// Members offered after "receiver." (OkayScript), and C#-aware members for a receiver in `src`.
const std::vector<std::string>& ScriptMembers(const std::string& receiver);
std::vector<std::string> CsMembersFor(const std::string& src, const std::string& receiver);
// The details pane beside the suggestion list: a description and an example ("Receiver.Member" or a bare name).
struct ScriptHelp { const char* doc; const char* example; };
const ScriptHelp* ScriptHelpFor(const std::string& key);
std::string ScriptExampleFromSignature(const std::string& sig);
// A code snippet coloured with the editor's palette, in the code font.
void DrawCodeSnippet(const std::string& code);
// The OkayScript palette and language words (keywords, types, built-ins with their hover docs) the code widget uses.
const scripteditor::ScriptPalette& OkayScriptPalette();
const scripteditor::LanguageWords& OkayScriptLanguageWords();
// The same for Lua scripts (language "lua"): Lua keywords and comments, the bound engine names with their docs.
const scripteditor::LanguageWords& LuaLanguageWords();
// The same for JavaScript scripts (language "javascript"): reserved words, // and /* */ comments, the bound engine names.
const scripteditor::LanguageWords& JavaScriptLanguageWords();
// The words for a script's language id (Lua, JavaScript, else OkayScript).
const scripteditor::LanguageWords& LanguageWordsFor(const std::string& language);

} // namespace oked
