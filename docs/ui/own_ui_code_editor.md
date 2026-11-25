# OkayUI::CodeEdit: our own code editor widget (replaces ImGuiColorTextEdit)

> **What this is:** Own code; it replaced ImGuiColorTextEdit (deleted 2026-11-23). Part of the "replace every third-party library" work (`docs/status/own_code_roadmap.md`, rows ImGuiColorTextEdit and Dear ImGui; toolkit background in `docs/ui/own_ui_toolkit.md`, section 4.29).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-24 (step 32: the script editor upgrade, editing basics, folding, many carets; see the end).

Own code. Part of the "replace every third-party library" work (`docs/status/own_code_roadmap.md`, rows ImGuiColorTextEdit and Dear ImGui; toolkit background in `docs/ui/own_ui_toolkit.md`, section 4.29).

**Status (2026-11-23, step 31 below): the script editor's only code view.** The script editor always draws its code with `OkayUI::CodeEdit` (through `editor/ScriptEditorCodeEdit.*` and `ScriptEditorWidget`); the vendored ImGuiColorTextEdit (`editor/vendor/TextEditor.*`) and the developer switch `OKAY_UI_CODEEDIT` are deleted. Tested headless and run in a real window once (Linux, Xvfb, step 28); not run on Windows. The steps below are the history of how it got there.

## Where it is

| File | What |
|---|---|
| `okayui/include/okay/UI/OkayUICodeEdit.hpp` | the API (`OkayUI::CodeEdit`, `CodeLanguage`, `CodePalette`, `CodeMarker`, `CodeToken`) |
| `okayui/src/OkayUICodeEdit.cpp` | the editor, the highlighter, the drawing (one file added to the `okay_ui` library) |
| `okayui/tests/test_okayui_codeedit.cpp` | behaviour, widget (synthetic mouse and keys), highlighting, golden image, 100k-line performance |
| `okayui/tests/test_okayui_codeedit_fuzz.cpp` | model fuzz and widget fuzz |
| `okayui/tests/golden/codeedit_highlight.png` | the golden image |

Hooks added to the existing OkayUI files (three tiny functions, nothing else in `OkayUI.cpp` changed): `OkayUI::GetFrameInput()` (this frame's raw `Input`), `OkayUI::GetKeyboardFocusId()` and `OkayUI::SetKeyboardFocusId(id)` (takes the keyboard and marks the click as taken so the click does not drop focus again). A widget in another file needs them to read typed text and key edges and to own the keyboard.

## How it is used

```cpp
OkayUI::CodeEdit ed;                       // one per open script; owns its text, caret, undo history
ed.SetLanguage(OkayUI::OkayScriptLanguage());
ed.SetText(source);
ed.SetBreakpointCallback([](int line, bool set) { /* store it */ });
// every frame, inside an OkayUI window:
bool changed = ed.Draw("script", 0, 0);    // 0, 0 = fill what is left of the window
ed.SetMarkers({{line, colStart, colEnd, "unknown name 'x'"}});   // compile errors
```

`Draw` reads the frame's input itself (mouse, wheel, typed text, key edges, `ctrl` and `shift`), edits, draws only the visible lines, and returns true when the text changed. Every editing operation is also a public method (`InsertText`, `Backspace`, `NewLine`, `Tab`, `Undo`, `Move`, `FindNext`, ...), so the editor can be driven and tested with no input at all.

Keys the widget acts on while it has the keyboard: typed text; Enter; Backspace and Delete (with Ctrl: by word); Left, Right, Up, Down (Ctrl: by word on Left and Right); Home (first non-blank, then column 0) and End (Ctrl: start and end of text); PageUp and PageDown; Tab and Shift+Tab; Ctrl+A; Ctrl+C, Ctrl+X, Ctrl+V (as the host's `Input::copy`, `cut`, `paste` edges); Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y; Escape drops the selection; the line commands, the same keys as the Dear ImGui script editor (`SetLineCommandKeys`, on by default): Ctrl+/ comment or uncomment, Ctrl+D duplicate, Ctrl+Shift+K delete, Alt+Up and Alt+Down move the lines (Alt+Up does not also move the caret). Shift extends the selection on every movement key. Find, replace and go-to-line are methods; the dialog (Ctrl+F, Ctrl+G) is the caller's.

Mouse: click places the caret, Shift+click extends, drag selects (and scrolls when the pointer leaves the view), double click selects a word, triple click a line, a click on the line numbers selects the line, a click in the narrow column left of the numbers toggles a breakpoint, the wheel scrolls three lines (Shift+wheel sideways), both scroll bars can be dragged or clicked.

## HAVE and GAP against what the script editor uses of ImGuiColorTextEdit

The list is from `grep "te\." editor/main.cpp` (the script editor's calls) plus the features of the library it shows to the user.

| ImGuiColorTextEdit feature the script editor uses | Status | Here |
|---|---|---|
| Multi-line editing: typing, Backspace, Delete, Enter | HAVE | `InsertText`, `Backspace`, `DeleteForward`, `NewLine` |
| Auto-indent (keeps indent, deeper after `{`) | HAVE | `NewLine`; also after `(` and `[`, and `{\|}` opens a blank line between |
| Tab and Shift+Tab, tab size | HAVE | `Tab`, `SetTabSize`, `SetInsertSpaces`; multi-line selection indents and outdents every line |
| Line numbers | HAVE | gutter, `SetShowLineNumbers` |
| Caret, selection by mouse and keys, word and line selection | HAVE | see above; `SetSelection`, `SelectAll`, `SelectWordAt` (the library's `SelectWordUnderCursor`), `SelectedText`, `HasSelection` |
| `SetCursorPosition` / `GetCursorPosition` | HAVE | `SetCaret` / `Caret()` (line and byte offset; the library counted columns) |
| `LineLength`, `GetTotalLines`, `GetCurrentLineText`, `GetTextLines`, `GetText` / `SetText` | HAVE | `LineLength`, `LineCount`, `Line(Caret().line)`, `Line(i)`, `GetText`, `SetText` |
| `InsertTextUndoable`, `Delete` | HAVE | `InsertText`, `DeleteForward`; both undoable |
| Undo and redo | HAVE | grouped: a word of typing is one step, a run of Backspaces one, Enter, paste, cut, indent, replace-all are one each; the caret and selection come back; 2000 steps |
| Copy, cut, paste | HAVE | through `OkayUI::SetClipboardText` / `GetClipboardText` and `Input::copy` / `cut` / `paste`, or the host's clipboard (`SetClipboardFunctions`; the script editor passes Dear ImGui's, step 29) |
| `IsTextChanged` | HAVE | `Draw` returns it; also `Version()` |
| Syntax highlighting with a language definition | HAVE | `CodeLanguage` (keywords, types, known words, line and block comments), `OkayScriptLanguage()`, `CLikeLanguage()`; or any callback `CodeHighlighter` (token ranges with a colour id, one line at a time with a state that carries block comments across lines). Not regex based |
| `SetUserTypes` (classes and enums declared in the script) | HAVE | `SetUserTypes` |
| Known identifiers (VM built-ins in a different colour) | HAVE | `CodeLanguage::known`, colour `CodeColor_Known`. The hover tooltip with the signature and doc (the library had it): `SetHoverInfo` / `HoverText` (step 31); the script editor passes `LanguageWords::docs` |
| `SetColorizerEnable` | HAVE | `SetHighlighter(nullptr)` turns colouring off |
| Palette | HAVE | `CodePalette`, `DefaultCodePalette()` is the script editor's Darcula-flavoured one; the script editor passes its own `OkayScriptPalette` (`ScriptEditorCodeEdit::SetPaletteFromImGui`, step 29) |
| `SetErrorMarkers` | HAVE | `SetMarkers`: a gutter mark, a wash over the line, a wavy underline under a column range, the message as a tooltip |
| `SetBreakpoints` and the gutter click | HAVE | `SetBreakpoints`, `ToggleBreakpoint`, `SetBreakpointCallback`; breakpoints move with the text |
| Current line highlight | HAVE | while focused with no selection; with `currentLineInactive` also while unfocused, outlined with `currentLineEdge`, across the gutter (step 29) |
| Scrolling: wheel, scroll bars, caret kept visible, `FirstVisibleLine` | HAVE | `ScrollX`, `ScrollY`, `SetScroll`, `GoToLine`, `FirstVisibleLine`, `VisibleLineCount` |
| `SetLeftMargin` (space between the line numbers and the text; the Density setting) | HAVE | `SetTextMargin` (step 31) |
| `RowHeight`, `CharWidth`, `TextStartX`, `ContentOrigin`, `CaretScreenPosBelow`, `ScreenToCoords` | HAVE | `LineHeight`, `CharWidth`, `GutterWidth` / `ViewRect`, `PosToScreen`, `CaretScreenPosBelow`, `ScreenToPos` |
| `SetHandleKeyboardInputs` (the autocomplete popup owns the keys) | HAVE | `SetHandleKeyboardInput` |
| Read-only mode | HAVE | `SetReadOnly`: moving, selecting and copying work, nothing else |
| Find next and previous, highlight all | HAVE (new) | `SetFind`, `FindNext`, `FindPrev`, `FindCount`, every match in view is tinted |
| Replace, replace all | HAVE (new) | `ReplaceCurrent`, `ReplaceAll` (one undo step) |
| Bracket matching | HAVE (new) | `MatchBracket`; the pair is drawn when the caret is next to one; brackets in strings and comments are skipped |
| 100,000 lines | HAVE | only the visible lines are drawn and tokenised; see numbers below |
| `Render(...)` into an ImGui child | n/a | `Draw` into an OkayUI window |
| `te.tip`, `te.label`, `te.icon`, `te.group`, `te.textBtn`, `te.active` | not the library | those are the editor's own structs |

## Numbers (Release, this machine, 100,000 lines, 6.3 MB of OkayScript with block comments)

From `test_okayui_codeedit` (it prints them):

| What | Time |
|---|---|
| `SetText` of the whole file | about 20 ms |
| first frame at the top | about 1 ms |
| a steady frame at the top, 15 lines drawn | about 0.3 ms (the same frame on a 4 KB text takes the same) |
| jump to the last line (highlighter runs over all 100,000 lines once to learn the block comment state) | about 35 ms |
| a steady frame at the end | about 0.3 ms |
| type a character and a newline in the middle, plus the frame | about 0.5 ms |
| count 100,000 find matches | about 9 ms |
| select all, delete, undo | about 12 ms |
| vertices drawn at the top / at the end | about 27,000 / 36,000 (batch limit 262,144; nothing dropped) |

The test asserts generous limits (a frame under 20 ms, a jump under 2 s, ...) and multiplies them by 20 under ASan or UBSan.

## How it was tested

- `test_okayui_codeedit` (407 checks): every editing operation on its own (insert, replace a selection, Backspace, Delete, word delete, Enter with auto-indent in each case, Tab and Shift+Tab on one and many lines, tabs and spaces), movement (arrows across line ends, vertical movement keeping the column through short lines, tabs and wide characters, smart Home, End, document ends, words over punctuation and blanks, page moves, Shift extending, collapsing a selection), selection edge cases with UTF-8 (two, three and four byte characters never split, positions inside a character snap to its start, backwards selections), clipboard (round trip, multi-line, CRLF, control characters, read-only, a selection too big for the clipboard is not cut), undo and redo (grouping, caret and selection restored, redo dropped by a new edit, one step for compound edits, the 2000 step limit), find (case, wrap both ways, UTF-8 needles, replace, replace all as one step), highlighting (each token kind, escapes, `#` comments in OkayScript, `"#fff"` stays a string, block comments across lines, an unterminated string, edits re-highlighting the lines below, the highlighter called once per line and cached), bracket matching (nesting, strings and comments, unbalanced, follows edits), breakpoints and markers moving with the text.
- The same widget driven with synthetic input through `Sim`: focus on click and loss on a click elsewhere, typing without focus does nothing, click, Shift+click, drag, double and triple click, clicks past the ends, every key above, read-only, PageUp and PageDown, the caret staying in view, the wheel (clamped at both ends, ignored when the pointer is elsewhere), Shift+wheel, both scroll bars dragged, the track clicked, the breakpoint gutter and its callback.
- Pixels: breakpoint dot, error mark and wash, line numbers, selection over the newline, comment and keyword colours, the caret, no caret when unfocused, plus the golden image `codeedit_highlight.png` (`OKAYUI_UPDATE_GOLDEN=1` regenerates it).
- `test_okayui_codeedit_fuzz`: (1) editing against a flat-string reference (random selections, inserts, Backspace, Delete, cut, paste, undo, redo, tab, find), text compared after every step, invariants after every step (no `\n` or `\r` inside a line, valid UTF-8, caret and anchor inside the text and on character boundaries, scroll finite and not negative, breakpoints sorted, `GetText`/`SetText` round trip), an undo followed by a redo changes nothing, undo of everything gives the first text and redo of everything the last; (2) the widget under random mouse moves (including NaN and infinity), clicks, wheel, junk text with invalid UTF-8, key edges, held keys, Ctrl and Shift, blocked input, random sizes, random calls of the API, rasterised now and then. 6 seeds each, `OKAYUI_FUZZ_ITERS` (default 1500) steps, `OKAYUI_FUZZ_SEED`. Run under `-fsanitize=address,undefined` with 4000 steps on three more seeds: clean.

## Limits and decisions, said bluntly

- **A monospaced font is assumed.** The cell width is the width of `0`. The 8x8 classic font is monospaced; the launcher look's Roboto is not, and with it the text drifts off its cells (seen in the real window: `Start  ()`, `once ,`). `SetFont(font, px)` draws the code in another font made with `AddFontTtf`, for that Draw only (the window's font and text scale are put back). The script editor uses the embedded JetBrains Mono (`shared/JetBrainsMonoFontOwn.h`, OFL, already listed in `docs/status/THIRD_PARTY.md`; decoded once by `ScriptEditorCodeEdit::CodeFont`) at the size the Dear ImGui editor uses (its zoom and the UI scale).
- **One character is one cell.** Wide (CJK, emoji) characters are drawn one cell wide and may overlap their neighbour; combining marks take a cell. The 8x8 font has only ASCII glyphs, so other characters show as the font's fallback.
- **OkayUI's clipboard holds 4095 bytes** (`kClipboardMax`). With it, Copy of a bigger selection is cut and Cut of a bigger selection does nothing (it never deletes text it could not copy). A host that passes its own clipboard (`SetClipboardFunctions`) has no limit: the script editor passes Dear ImGui's (the system clipboard), so a whole script copies (step 29).
- **Columns are byte offsets** (on character boundaries). The library counted columns; a port has to convert (`ByteToColumn`, `ColumnToByte`).
- Tab size 1 to 16; tab characters are kept as they are, Tab inserts spaces by default.
- Breakpoints and markers move with inserted and removed lines. A marker on a removed line is dropped. A breakpoint at the line where a cut begins stays there. Undo does not bring a dropped marker or breakpoint back.
- Line endings are normalised to `\n` on input; the caller converts back on save if it wants `\r\n`.
- Typed text is read from `Input::text` only while the widget has focus. The host (not on this branch) still has to send key edges and Ctrl+C / X / V for OkayUI at all.
- Very long lines (hundreds of thousands of characters on one line) are walked from their start each frame; only the visible columns are drawn but the walk is linear in the line length.
- The highlighter state is cached per line from the top; an edit near the top of a 100,000 line file re-tokenises downwards only as far as the view needs.

## NOT DONE

- **IME** (composition text, candidate window): the widget shows the preedit, takes the commit and reports the caret rectangle (`docs/ui/own_ui_toolkit.md` section 4.31). Since step 29 the editor's host sends it: the sdl2 seam reports `SDL_TEXTEDITING` as `TextEditing`, `InputCollector` passes the preedit, and the caret rectangle goes to Dear ImGui's IME data (the candidate window). Tested headless with synthetic events only; no input method was run (none on the Xvfb test machine, Windows not run).
- **Ligatures**, **bidirectional text**, **wide-character cell widths**, **combining-mark clusters**.
- (Done in step 32: multiple cursors, column selection, code folding, word wrap, auto-close of brackets and quotes, auto-dedent.)
- Line commands (`ToggleLineComment`, `DuplicateLines`, `DeleteLines`, `MoveLines`, tested headless in `LineCommandTests`) are bound to keys in `Draw` (`LineCommandKeyTests`). With the OkayUI editor active, `editor/main.cpp` leaves those keys to the widget (they would run twice) and its toolbar comment button calls `ToggleLineComment`; the Dear ImGui editor keeps its own handling. Ctrl+Shift+J (join lines) and Alt+Enter (quick fix) stay the script editor's. Written from the editor's documented behaviour, no outside code. `FormatOkayScript` stays the editor's.
- **Autocomplete popup and the hover tooltip**: the editor draws them itself (they use `CaretScreenPosBelow`, `ScreenToPos`, `LineTokens`); the widget only offers the geometry and `SetHandleKeyboardInput`. Since step 29 they are drawn over the code (the code is drawn in Dear ImGui's order).
- **Drop of files onto the editor** (drag and drop of selected text: done in step 32).
- **Minimap** in the widget: the script editor draws its own beside the widget (`editor/main.cpp`, View > Minimap); show whitespace, indent guides and matching word highlight: done in step 32.
- (Find and replace across lines: done in step 32.)
- **Mouse cursor shape** is set (`MouseCursor_TextInput` over the text) but, like the rest of OkayUI, only the host shows it.
- **Not verified**: a real window on screen with the SDL_Renderer or Direct3D 11 backend (only the headless draw-list rasteriser was used; it skips textured quads, so the launcher look's Roboto glyphs are not in the golden), Windows and macOS builds, a real keyboard (host key repeat, dead keys, layouts), the real system clipboard, the editor's script editor actually running on it.

## The script editor's port: adapter and widget (step 27)

The script editor (`editor/main.cpp`) now holds its code widget through `ScriptEditorWidget` (`editor/ScriptEditorWidget.hpp`) instead of a bare `TextEditor`. Default behaviour is unchanged: with the Dear ImGui backend (everyone, unless the developer-only OkayUI backend is on) every call goes to the same `TextEditor` as before.

- `editor/ScriptEditorCodeEdit.{hpp,cpp}`: a pure adapter over `OkayUI::CodeEdit` (no Dear ImGui, no SDL) in the vocabulary the script editor already uses. Coordinates are `Coord{line, column}` with a DISPLAY column (one character is one column, a tab goes to the next multiple of the tab size), converted to and from CodeEdit's byte offsets with `ByteToColumn` and `ColumnToByte`. Error markers and breakpoints are 1-based by line, like `TextEditor::ErrorMarkers` and `Breakpoints`. `GetText()` ends with a newline like `TextEditor::GetText()` (the script editor strips one). `InsertTextUndoable` is its own undo step. The geometry calls (`RowHeight`, `CharWidth`, `TextStartX`, `ContentOrigin`, `CaretScreenPosBelow`, `ScreenToCoords`, `FirstVisibleLine`) give the last Draw's screen pixels, so the script editor's autocomplete popup and Ctrl+click work off the same numbers.
- `editor/ScriptEditorWidget.hpp`: owns both editors and forwards each call to the active one. The active one is OkayUI only while `oked::UseOkayUIBackend()` is true. If the backend changes while a script is open, the text, caret and selection are copied to the editor that becomes active. `Render` for OkayUI reserves an ImGui rectangle (`InvisibleButton`, `OkayUIRegion().Offer`) and draws the `CodeEdit` into it inside the open OkayUI frame, the way the Console does. `main.cpp` changed only the type name (`TextEditor` to `ScriptEditorWidget`, and `TextEditor::Coordinates`, `ErrorMarkers`, `Breakpoints`, `Keywords` to the same names on the widget) and one line deciding whether the code has the keyboard.
- `shared/OkayUIHost.hpp` `InputCollector` now also carries what a code editor needs: Up, Down, Ctrl+C, Ctrl+X, Ctrl+V (as `Input::copy`, `cut`, `paste`), the `ctrl` state (Ctrl+Left and Right move by word, Ctrl+Backspace and Delete delete a word, Ctrl+Home and End go to the document ends), and the held state of the named keys (`Input::keyDown`: PageUp, PageDown, Tab, Ctrl+Z, Ctrl+Y, the letters, digits, F-keys). The Command key counts as Ctrl, as before. The clipboard itself is OkayUI's (`SetClipboardText`, mirrored to the system clipboard when a window system runs).

Tested: `test_okayui_script_adapter` (display-column conversion with tabs and two- and three-byte characters, the round trip for every column, selection given backwards, word under the caret, undo steps, change flag, 1-based marks, colouring on and off, user types, geometry after a real Draw, ImGui-free) and `test_okayui_host` (the new keys and states, and a `CodeEdit` driven only by SDL events through `InputCollector`: arrows, Ctrl+word move, Ctrl+C then Ctrl+V, Ctrl+Z and Ctrl+Y, Ctrl+Home, Tab).

Not done, said plainly (updated by step 28):
- (Fixed in step 29) ImGui popups, tooltips and modals over the code were hidden behind it.
- (Fixed in step 29) The OkayUI editor used `DefaultCodePalette()` and its own OkayScript word list; it now takes the script editor's palette and language definition. `SetShowWhitespaces` and `SetLeftMargin` still only apply to the ImGui editor; of the language regexes only a `<literal>.*` comment token (OkayScript's `#`) is carried over.
- Error markers cover the whole line's text (the script editor gives a line, not a column range).

## Step 28: first run in a real window (Linux, Xvfb)

The editor (Release, `-O1 -g0`) was run under Xvfb with `OKAY_DEV=1 OKAY_UI_BACKEND=okayui`, the existing screenshot hooks (`OKAY_UI_SELECT="Main Camera" OKAY_UI_SCRIPT=1 OKAY_UI_SHOT=<png> OKAY_UI_QUIT=1`) and `xdotool` for clicks and keys. The screenshots were looked at one by one; they are not in the repository.

What it showed, and what was done:

| Seen | Cause | Now |
|---|---|---|
| The code was drawn 160 px right and 50 px down of its rectangle, over the Inspector and the status line; the rectangle itself was empty | `ScriptEditorWidget::Render` gave OkayUI Dear ImGui's desktop coordinates (multi-window viewports); OkayUI draws in the window's own pixels. The Console already took the viewport position off; the script editor did not | Fixed: the viewport position is taken off for drawing and added back for the geometry the script editor uses for its popups and Ctrl+click (`ScriptEditorCodeEdit::SetScreenOffset`, tested in `test_okayui_script_adapter`) |
| Text in the proportional UI font (Roboto) at 13 px: tokens drifted off their cells (`Start  ()`, `once ,`), the zoom (125%) was ignored | No code font on the OkayUI side | Fixed: `CodeEdit::SetFont`; the script editor draws in the embedded JetBrains Mono at the ImGui editor's size (`ImGui::GetFontSize()` inside its `PushFont`: zoom and UI scale). `MMMM` and `iiii` now take the same width; the text lines up with the Dear ImGui editor's (tested: cell width, line height, the window font put back, a click on the right cell) |
| Typing a space or Enter in the code also pressed Dear ImGui buttons (the "Scripts" button got the navigation focus and the script list opened while typing; the editor then moved and later keys were lost) | Dear ImGui keyboard navigation stayed on while OkayUI had the keyboard | Fixed: `g_scriptCodeOwnsKeys`; `main.cpp` turns ImGui keyboard navigation off for the next frame while it is set (the same switch Play and mesh edit use) |
| Ctrl+D in the code duplicated the selected GameObject | A click on the OkayUI code did not focus the Script Editor's ImGui window, so the scene shortcuts ran | Fixed: a press inside the code focuses the Script Editor window; a press anywhere else takes the keyboard from the code |
| Ctrl+/, Ctrl+D, Ctrl+Shift+K, Alt+Up / Alt+Down not in the widget | Not bound | Fixed: bound in `CodeEdit::Draw` (`LineCommandKeyTests`), the host now names the `/` key (`KeyId_Slash`, `test_okayui_host`), and `main.cpp` leaves those keys to the widget on the OkayUI path (its own Ctrl+/ chord too, or the comment toggled twice) |
| The autocomplete popup, its documentation tooltip and a modal ("Unsaved Changes") were drawn under the OkayUI code | Draw order: OkayUI's frame is flushed after Dear ImGui's | Fixed in step 29: the code's shapes are captured and drawn in the Script Editor window's ImGui draw list |

Checked working in the window after the fixes: click to place the caret, typing (auto-indent after `{`), Enter, Ctrl+D, Ctrl+Shift+K, Alt+Up and Alt+Down, Ctrl+Z, the error marker (gutter mark, wash, wavy underline, tooltip), bracket match, current line, the status bar's line and column. With the Dear ImGui backend (the default) the screenshot is the same as before except the frames-per-second counter.

Not checked in the window: Ctrl+/ after the last fix (fixed from the cause found in the window, with headless tests; the rebuilt editor was not run again for it), drag selection, the wheel and the scroll bars, find (Ctrl+F) and replace, copy and paste with the real clipboard, IME. `xdotool` drops modifiers on `ctrl+slash` under Xvfb (the `/` arrived without Ctrl), so that key is checked with a held Ctrl. Windows: not built, not run.


## Step 29: Dear ImGui popups over the code, the ImGui editor's look, languages, clipboard, IME (2026-11-22, branch `claude2/own-codeedit-2`)

Everything in this step is tested headless only. `editor/main.cpp` and `editor/ScriptEditorWidget.hpp` with these changes were compiled, but the editor was not linked or run: the shared build machine kept running out of memory. No screenshots were taken.

| Item from #1104 | Done | How | Tests |
|---|---|---|---|
| ImGui popups, tooltips, modals drawn under the code | Yes (headless) | `OkayUI::BeginCapture` / `EndCapture` (`okay/UI/OkayUICapture.hpp`) cut the code window's shapes out of the OkayUI frame; `shared/OkayUIImGuiEmbed.hpp` puts them into the Script Editor's ImGui draw list (OkayUI's textures become ImGui user textures). The code is then drawn where its window is in ImGui's order; the foreground overlays of `main.cpp` (inline diagnostics, diff bars, sticky header) also show over it again | `okayui_capture` (43 checks), `okayui_codeedit_imgui` (86 checks: a popup over the code is on top, the old way is not; same pixels as the old way with nothing on top) |
| Line numbers outside the frame, no current-line outline, no horizontal scroll bar | Yes (headless) | `ScriptEditorWidget::Render` uses the same framed ImGui child as the ImGui editor; CodeEdit got content padding, the caret-line box across gutter and text with an outline and an inactive colour, a breakpoint wash, per-marker washes, scroll bar size and rounding from ImGui's style and an always-shown horizontal bar | `okayui_codeedit` `ScriptLookTests` |
| Palette and languages | Yes (headless) | `SetPaletteFromImGui` takes `OkayScriptPalette`; `SetLanguage(LanguageWords)` takes the words of the ImGui language definition (`ScriptEditorWidget::WordsOf`: keywords, types, known names, line and block comments, the `#.*` token); colouring order call > member > known as the ImGui editor; block comments are checked before line comments (Lua's `--[[`). On main every script uses `OkayScriptLangDef`; the Lua definition on `claude2/lua-csharp-merge` goes through the same path | `okayui_script_adapter`, `okayui_codeedit` |
| Inline end-of-line diagnostic | Yes (headless) | CodeEdit draws no message text after the line (the editor's own overlay draws exactly one); warnings (`warning: ` prefix) get the amber wash, the tooltip says "Error/Warning at line N:" | `okayui_codeedit` (no pixels after the line), `okayui_script_adapter` |
| Clipboard | Yes (headless) | `CodeEdit::SetClipboardFunctions`; the script editor passes Dear ImGui's clipboard (the system clipboard, no 4 KB limit) | `okayui_codeedit` `HostClipboardTests` |
| IME | Host side done (headless) | the sdl2 seam reports `SDL_TEXTEDITING` as `TextEditing`; `InputCollector` passes the preedit; `main.cpp` gives OkayUI's caret rectangle to ImGui's IME data | `okayui_host` (synthetic SDL and seam events) |
| Drag selection, wheel, scroll bars, find/replace in the real window | Not checked | the code paths are the same as before (tested headless in `okayui_codeedit`), the editor was not run | - |

Test results (Release, Linux): `okayui_codeedit` 491 checks 0 failures (before the fix 25 failures), `okayui_script_adapter` 98/0 (18), `okayui_host` 93/0 (4), `okayui_capture` 43/0 (16), `okayui_codeedit_imgui` 86/0 (failed, then crashed). The golden image `codeedit_highlight.png` is unchanged (the new look is off in the default palette). Not run: `okayui_codeedit_fuzz` and the other OkayUI suites (not rebuilt).

Wiring (until step 31): `oked::UseOkayUICodeEdit()` (the OkayUI backend, or the developer variable `OKAY_UI_CODEEDIT=1` with the Dear ImGui screens) picked the widget; `oked::OkayUIInputOn()` routes mouse and keys to OkayUI in either case.

NOT DONE (step 29):
- Build and run the editor with these changes (Xvfb), take side-by-side screenshots (vendored against ours) and look at them: popups over the code, the frame, line numbers, caret-line box, scroll bars.
- In the window: drag selection, wheel, both scroll bars, find and replace (Ctrl+F bar), copy and paste through the system clipboard, Ctrl+/ with a real Ctrl.
- IME with a real input method (none on the test machine; Windows not run).
- The Settings > Experimental item ("Our own code editor", id `codeedit`, `OKAY_UI_CODEEDIT=1`, `kNeedsOkayUi`, one line in `shared/OwnSwitches.hpp` Items() plus a check in `tests/test_own_switches.cpp`): held back until the window checks above pass.
- `okayui_codeedit_fuzz` and the full OkayUI suite after the `OkayUI.cpp` change (capture, `finalizeFrame` ends an open capture).
- Windows build and run; the embed's textures with the Direct3D 11 / own ImGui renderers were not exercised (only the headless rasteriser).
- A captured CodeEdit on a secondary viewport (Script Editor dragged out to its own OS window): mouse coordinates there were already wrong before this step.


## Step 31: the switch, and the vendored copy deleted (2026-11-23, branch `claude/third-party-4`)

The script editor's code is now always `OkayUI::CodeEdit`. `ScriptEditorWidget` holds only the OkayUI widget (no second editor, no
carrying text between two); `oked::UseOkayUICodeEdit()` is always true in a build with OkayUI, so `OkayUIInputOn()` always routes
the mouse over the code and the keys while it has the keyboard (the path `OKAY_UI_CODEEDIT=1` used in steps 28 to 30). The
developer variable `OKAY_UI_CODEEDIT` is no longer read. `editor/vendor/` (`TextEditor.cpp`, `.h`, `.LICENSE`) is deleted, with its
CMake lines and the script exclusions that named it.

Gaps found by reading every call the script editor made on the vendored widget, closed in CodeEdit with tests written first:

| Gap | Now | Tests |
|---|---|---|
| `SetLeftMargin`: the space between the line numbers and the text (Density setting, 0 to 32 px) | `CodeEdit::SetTextMargin` (default 4, 0 to 200); a click in it is column 0; the adapter's `SetLeftMargin` keeps at least 4 | `okayui_codeedit` `ScriptEditorGapTests`, `okayui_script_adapter` |
| Hover tooltip on a known word: a built-in's signature and doc, "X (built-in type)" | `CodeEdit::SetHoverInfo` (word to text) and `HoverText(x, y)`: the cell under the pointer, letters, digits and `_`; an error's message wins on its line; Draw shows it as a tooltip. `LanguageWords::docs` carries the texts, `OkayScriptLanguageWords()` fills them as before | both suites |
| The types the editor took from the vendored header (`TextEditor::Palette`, `PaletteIndex`, `LanguageDefinition`, `Coordinates`, `ErrorMarkers`, `Breakpoints`, `Keywords`) | ours: `scripteditor::ScriptPalette`, `PaletteIndex`, `LanguageWords`, `Coord` (`editor/ScriptEditorCodeEdit.hpp`); `ScriptEditorWidget` names the containers with standard types | `okayui_script_adapter` |
| `SetShowWhitespaces(false)` | not needed: CodeEdit never draws whitespace marks | - |
| The regex tokens of the language definition | not needed: only the `#` line comment used one; it is a plain line comment now (`lineComments = {"//", "#"}`) | `okayui_script_adapter` (Lua-like words) |

Checked and left as is: the error's message after the end of a line is drawn by the script editor's own overlay in `main.cpp`
(the widget draws none, so it shows once).

NOT DONE (step 31): the editor was not run in a window after the switch; not built or run on Windows (only the changed headless
files were syntax-checked with MinGW); the editor built on OkayUI itself (`OKAY_EDITOR_ON_OKAYUI`, off, not in CI) got a plain
fallback `Render` that was not compiled; IME only with synthetic events.


## Step 32: the script editor upgrade (2026-11-24, branch `claude/script-editor-1`)

Written from the behaviour of common code editors as users see it (no other editor's code read or followed); Lua's block words
from the Lua 5.4 reference manual's grammar.

| What | Where | How it behaves |
|---|---|---|
| Auto-close | `CodeEdit::Type` (typed text; `InsertText` never pairs), `SetAutoClose` | `( [ { " '` add their closer; a closer already next to the caret is typed over; Backspace between a pair removes both; an opener or quote typed over a selection wraps it and keeps it selected. Not inside strings or comments, not before a word, a quote not after a letter. The script editor's own pairing in `main.cpp` (which used display columns as byte offsets and added a second blank line after Enter between braces) is removed; its View menu switch now sets the widget's |
| Auto-dedent | `SetAutoDedent` | a `}` typed as the first thing on a line takes the indent of its `{` line (else one level less); a dedent word of the language (`CodeLanguage::dedentWords`, Lua: `end else elseif until`) typed alone takes one level off, once, and gives it back when the word goes on (`end` to `ending`). One undo step with the character |
| Enter after a block word | `NewLine` | Lua's `then`, `do`, `function`, `repeat`, `else` (outside strings and comments) indent the next line, as `{` does |
| Matching word | palette `wordMatch` (0 in `DefaultCodePalette`, set by the adapter) | the other whole-word places of the word at the caret (or the selected word) are tinted |
| Indent guides | palette `indentGuide` | a 1 px line at every indent level left of the text; a blank line takes the smaller indent of its neighbours |
| Whitespace | `SetShowWhitespace` | a dot per space, an arrow per tab |
| Word wrap | `SetWordWrap` | rows break after the last space that leaves the row half full, else at the column limit; no horizontal scroll; Up and Down, PageUp and PageDown, clicks, drags, the selection, find washes, error underlines and the caret follow rows; one line number per line |
| Find across lines | `SetFind` with `\n` | the first part must end its line, the middle parts are whole lines, the last part starts a line; FindNext, FindPrev, FindCount, ReplaceCurrent, ReplaceAll (one undo step, matches taken from the top without overlap) |
| Folding | `SetFolding`, `Fold`, `Unfold`, `ToggleFold`, `FoldAll`, `UnfoldAll`, `FoldEnd`, `IsLineHidden` | blocks by `{ }` and the language's block words (strings and comments skipped), and by indent (`foldByIndent`, on for Lua); a fold column with arrows in the gutter, a `...` box after a folded line (a click unfolds), Ctrl+Shift+[ and ], Ctrl+Alt+[ and ] for all. The closing `}` or `end` line stays in view. Folds move with edits above them, are checked again after every edit (a block that lost its brace unfolds) and a caret put into a hidden line unfolds it |
| Many carets | `AddCaret`, `RemoveCaretAt`, `ClearExtraCarets`, `Selections`, `SelectNextOccurrence`, `SetColumnSelection` | Ctrl+click adds (or removes) a caret, Ctrl+D selects the word then adds its next whole-word place, Alt+drag makes a column selection, Escape goes back to one. Every edit works at every caret as one undo step (typing a word at many carets is one step, as with one): typing with auto-close, Backspace, Delete, Enter, Tab, Cut, Copy (one line per caret), Paste (one line each when the counts match), the line commands (once per line), MoveLines, every movement key; undo and redo bring the carets back |
| Drag and drop of text | `MoveSelectionTo` | a press inside the selection and a drag moves it (Ctrl at the drop: copies), one undo step; a press without a drag places the caret |

Key change: Ctrl+D was Duplicate Lines (the Rider key the Dear ImGui editor used); it is now the next match (the owner's request),
and Duplicate Lines is Ctrl+Shift+D. `editor/main.cpp` leaves line keys to the widget, so only the widget changed.

The script editor (`editor/ScriptEditorCodeEdit.*`) turns folding on, gives the new colours from its palette (word match from the
selection colour, guides and fold marks from the line numbers, whitespace from the comments) and takes a language's block words
from `LanguageWords` (`blockOpenWords`, `blockCloseWords`, `dedentWords`, `foldByIndent`); a language with Lua's `--` comments and
no block words of its own gets `LuaBlockWords()`. `editor/main.cpp` got View > Word Wrap, Show Whitespace, Fold All, Unfold All and
the same in the command palette.

Inside: a row map (`ensureRows`) turns lines into what is drawn; with no folds and no wrap there is none at all (a row is a line),
so the 100,000-line numbers above are unchanged in that case. An edit at many carets runs the one-caret edit at each caret, from
the bottom up (MoveLines up: from the top), while every caret's place is moved by each edit (`applyRaw`).

Tests (all headless): `okayui_codeedit` (`AutoCloseTests`, `DedentTests`, `WordWrapTests`, `MultiLineFindTests`, `FoldTests`,
`MultiCaretTests`, `DragDropTests`, `MultiCaretWidgetTests`, `FeatureDrawTests`; new golden images `codeedit_wrap.png` and
`codeedit_features.png`, `codeedit_highlight.png` unchanged), `okayui_codeedit_fuzz` (a third scenario, `ExtraFuzz`, and the
widget fuzz with the new options, Alt, Ctrl+D and the fold keys).

NOT DONE (step 32): the editor was not run in a window with these changes, not on Windows; a real keyboard, mouse and IME were
not used (synthetic input only); go to definition across the project's scripts; error markers with the column of the error (the
diagnostics carry one, the script editor still gives a whole line); a minimap inside the widget (the script editor's own is kept).

