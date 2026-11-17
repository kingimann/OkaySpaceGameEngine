# OkayUI::CodeEdit: our own code editor widget (replaces ImGuiColorTextEdit)

Own code. Part of the "replace every third-party library" work (`docs/own_code_roadmap.md`, rows ImGuiColorTextEdit and Dear ImGui; toolkit background in `docs/own_ui_toolkit.md`, section 4.29).

**Status: built and tested headless; the script editor uses it when `OKAY_CODE_EDITOR=own` is set (step 28 below) or the developer-only OkayUI backend is on (step 27); since step 29 it looks like the ImGui editor (same code font, row height, letter spacing, inline error text, current-line box, breakpoint fill) and is drawn inside ImGui's window so popups sit on top; never run in a real window.** The default stays the vendored ImGuiColorTextEdit. OkayUI is off by default and the editor still draws its script editor with the vendored `editor/vendor/TextEditor.*` on Dear ImGui unless that developer switch is on. (This line used to say "Nothing was wired in"; step 27 below wired it through `ScriptEditorWidget`.) What blocks deleting the vendored widget: `docs/own_flip_proposal.md` section 4.

## Where it is

| File | What |
|---|---|
| `okayui/include/okay/UI/OkayUICodeEdit.hpp` | the API (`OkayUI::CodeEdit`, `CodeLanguage`, `CodePalette`, `CodeMarker`, `CodeToken`) |
| `okayui/src/OkayUICodeEdit.cpp` | the editor, the highlighter, the drawing (one file added to the `okay_ui` library) |
| `okayui/tests/test_okayui_codeedit.cpp` | behaviour, widget (synthetic mouse and keys), highlighting, golden image, 100k-line performance |
| `okayui/tests/test_okayui_codeedit_fuzz.cpp` | model fuzz and widget fuzz |
| `okayui/tests/golden/codeedit_highlight.png` | the golden image |
| `editor/OkayUIInImGui.hpp` | step 29: OkayUI geometry put into an ImGui window's draw list (the script editor hosts the code with it) |
| `okayui/tests/test_okayui_codeedit_imgui.cpp` | step 29: ImGui popups drawn over the hosted code (built with the editor) |
| `editor/tools/codeedit_bench.cpp` | ours against ImGuiColorTextEdit: speed, memory, screenshots, pixel difference |

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

Keys the widget acts on while it has the keyboard: typed text; Enter; Backspace and Delete (with Ctrl: by word); Left, Right, Up, Down (Ctrl: by word on Left and Right); Home (first non-blank, then column 0) and End (Ctrl: start and end of text); PageUp and PageDown; Tab and Shift+Tab; Ctrl+A; Ctrl+C, Ctrl+X, Ctrl+V (as the host's `Input::copy`, `cut`, `paste` edges); Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y; Escape drops the selection. Shift extends the selection on every movement key. Find, replace and go-to-line are methods; the dialog (Ctrl+F, Ctrl+G) is the caller's.

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
| Copy, cut, paste | HAVE | through `OkayUI::SetClipboardText` / `GetClipboardText` and `Input::copy` / `cut` / `paste` (see "limits" below) |
| `IsTextChanged` | HAVE | `Draw` returns it; also `Version()` |
| Syntax highlighting with a language definition | HAVE | `CodeLanguage` (keywords, types, known words, line and block comments), `OkayScriptLanguage()`, `CLikeLanguage()`; or any callback `CodeHighlighter` (token ranges with a colour id, one line at a time with a state that carries block comments across lines). Not regex based |
| `SetUserTypes` (classes and enums declared in the script) | HAVE | `SetUserTypes` |
| Known identifiers (VM built-ins in a different colour) | HAVE | `CodeLanguage::known`, colour `CodeColor_Known`. The hover tooltip with the signature (the library had it) is the caller's: `LineTokens` and `ScreenToPos` give what is under the pointer |
| `SetColorizerEnable` | HAVE | `SetHighlighter(nullptr)` turns colouring off |
| Palette | HAVE | `CodePalette`, `DefaultCodePalette()` is the script editor's Darcula-flavoured one |
| `SetErrorMarkers` | HAVE | `SetMarkers`: a gutter mark, a wash over the line, a wavy underline under a column range, the message as a tooltip |
| `SetBreakpoints` and the gutter click | HAVE | `SetBreakpoints`, `ToggleBreakpoint`, `SetBreakpointCallback`; breakpoints move with the text |
| Current line highlight | HAVE | while focused with no selection |
| Scrolling: wheel, scroll bars, caret kept visible, `FirstVisibleLine` | HAVE | `ScrollX`, `ScrollY`, `SetScroll`, `GoToLine`, `FirstVisibleLine`, `VisibleLineCount` |
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

- **A monospaced font is assumed.** The cell width is the width of `0`. The 8x8 classic font is monospaced. In the launcher look the code is drawn with the code font, JetBrains Mono (`OkayUI::PushCodeFont`, step 29), each character advancing a whole number of pixels; with the code font turned off (`SetUseCodeFont(false)`) a proportional font is drawn one character per cell (step 28).
- **One character is one cell.** Wide (CJK, emoji) characters are drawn one cell wide and may overlap their neighbour; combining marks take a cell. The 8x8 font has only ASCII glyphs, so other characters show as the font's fallback.
- **The clipboard holds 4095 bytes** (`kClipboardMax`). Copy of a bigger selection is cut; Cut of a bigger selection does nothing (it never deletes text it could not copy). This is the OkayUI clipboard's limit, not the editor's.
- **Columns are byte offsets** (on character boundaries). The library counted columns; a port has to convert (`ByteToColumn`, `ColumnToByte`).
- Tab size 1 to 16; tab characters are kept as they are, Tab inserts spaces by default.
- Breakpoints and markers move with inserted and removed lines. A marker on a removed line is dropped. A breakpoint at the line where a cut begins stays there. Undo does not bring a dropped marker or breakpoint back.
- Line endings are normalised to `\n` on input; the caller converts back on save if it wants `\r\n`.
- Typed text is read from `Input::text` only while the widget has focus. The host (not on this branch) still has to send key edges and Ctrl+C / X / V for OkayUI at all.
- Very long lines (hundreds of thousands of characters on one line) are walked from their start each frame; only the visible columns are drawn but the walk is linear in the line length.
- The highlighter state is cached per line from the top; an edit near the top of a 100,000 line file re-tokenises downwards only as far as the view needs.

## NOT DONE

- **IME** (composition text, candidate window): the widget shows the preedit, takes the commit and reports the caret rectangle (`docs/own_ui_toolkit.md` section 4.31); no host sends the events yet, so in a real window typed text still arrives as finished characters only.
- **Ligatures**, **bidirectional text**, **wide-character cell widths**, **combining-mark clusters**.
- **Multiple cursors** and **column (block) selection**.
- **Code folding.**
- **Word wrap.** Long lines scroll sideways.
- **Auto-close brackets and quotes**, **auto-dedent when typing `}`**, **toggle comment (Ctrl+/)**, **move line up and down**, **duplicate line** (these three are now in the widget, `ToggleLineComment`, `MoveLines`, `DuplicateLines`, keys Ctrl+/, Alt+Up / Alt+Down, Ctrl+Shift+D; `docs/own_ui_toolkit.md` section 4.39; written from the usual behaviour of code editors; moved lines do not take breakpoints with them): they were not in the library the script editor uses either, or done by the editor itself on top of the API (`FormatOkayScript`, comment toggle in `editor/main.cpp` work on the text through `SetSelection`, `InsertText`).
- **Autocomplete popup and the hover tooltip**: the editor draws them itself (they use `CaretScreenPosBelow`, `ScreenToPos`, `LineTokens`); the widget only offers the geometry and `SetHandleKeyboardInput`.
- **Drag and drop of selected text**, **drop of files onto the editor**.
- **Show whitespace**, **minimap**, **indent guides**, **matching word highlight**.
- **Replace across several lines of the needle** (a find text with a newline matches nothing).
- **Mouse cursor shape** is set (`MouseCursor_TextInput` over the text) but, like the rest of OkayUI, only the host shows it.
- **Not verified**: a real window on screen with the SDL_Renderer or Direct3D 11 backend (only the headless draw-list rasteriser was used; it skips textured quads, so the launcher look's Roboto glyphs are not in the golden), Windows and macOS builds, a real keyboard (host key repeat, dead keys, layouts), the real system clipboard, the editor's script editor actually running on it.

## The script editor's port: adapter and widget (step 27)

The script editor (`editor/main.cpp`) now holds its code widget through `ScriptEditorWidget` (`editor/ScriptEditorWidget.hpp`) instead of a bare `TextEditor`. Default behaviour is unchanged: with the Dear ImGui backend (everyone, unless the developer-only OkayUI backend is on) every call goes to the same `TextEditor` as before.

- `editor/ScriptEditorCodeEdit.{hpp,cpp}`: a pure adapter over `OkayUI::CodeEdit` (no Dear ImGui, no SDL) in the vocabulary the script editor already uses. Coordinates are `Coord{line, column}` with a DISPLAY column (one character is one column, a tab goes to the next multiple of the tab size), converted to and from CodeEdit's byte offsets with `ByteToColumn` and `ColumnToByte`. Error markers and breakpoints are 1-based by line, like `TextEditor::ErrorMarkers` and `Breakpoints`. `GetText()` ends with a newline like `TextEditor::GetText()` (the script editor strips one). `InsertTextUndoable` is its own undo step. The geometry calls (`RowHeight`, `CharWidth`, `TextStartX`, `ContentOrigin`, `CaretScreenPosBelow`, `ScreenToCoords`, `FirstVisibleLine`) give the last Draw's screen pixels, so the script editor's autocomplete popup and Ctrl+click work off the same numbers.
- `editor/ScriptEditorWidget.hpp`: owns both editors and forwards each call to the active one. The active one is OkayUI only while `oked::UseOkayUIBackend()` is true. If the backend changes while a script is open, the text, caret and selection are copied to the editor that becomes active. `Render` for OkayUI reserves an ImGui rectangle (`InvisibleButton`, `OkayUIRegion().Offer`) and draws the `CodeEdit` into it inside the open OkayUI frame, the way the Console does. `main.cpp` changed only the type name (`TextEditor` to `ScriptEditorWidget`, and `TextEditor::Coordinates`, `ErrorMarkers`, `Breakpoints`, `Keywords` to the same names on the widget) and one line deciding whether the code has the keyboard.
- `shared/OkayUIHost.hpp` `InputCollector` now also carries what a code editor needs: Up, Down, Ctrl+C, Ctrl+X, Ctrl+V (as `Input::copy`, `cut`, `paste`), the `ctrl` state (Ctrl+Left and Right move by word, Ctrl+Backspace and Delete delete a word, Ctrl+Home and End go to the document ends), and the held state of the named keys (`Input::keyDown`: PageUp, PageDown, Tab, Ctrl+Z, Ctrl+Y, the letters, digits, F-keys). The Command key counts as Ctrl, as before. The clipboard itself is OkayUI's (`SetClipboardText`, mirrored to the system clipboard when a window system runs).

Tested: `test_okayui_script_adapter` (display-column conversion with tabs and two- and three-byte characters, the round trip for every column, selection given backwards, word under the caret, undo steps, change flag, 1-based marks, colouring on and off, user types, geometry after a real Draw, ImGui-free) and `test_okayui_host` (the new keys and states, and a `CodeEdit` driven only by SDL events through `InputCollector`: arrows, Ctrl+word move, Ctrl+C then Ctrl+V, Ctrl+Z and Ctrl+Y, Ctrl+Home, Tab).

Not done, said plainly:
- The OkayUI path of the script editor was NOT run in a real window. Only the adapter and the host input are tested headless.
- ~~The ImGui side of the editor still draws first and OkayUI draws last, on top: an ImGui autocomplete or tooltip popup that overlaps the code rectangle is hidden behind the OkayUI editor~~ (fixed in step 29: the code is drawn into the ImGui window's draw list). ~~ImGui shortcuts (Ctrl+S, Ctrl+Z at the script level) are not blocked while the OkayUI editor has the keyboard~~ (step 30).
- ~~The code font (`g_codeFont`) is the ImGui editor's; the OkayUI editor uses OkayUI's font~~ (fixed in step 29: the same JetBrains Mono). (Step 28 passes the palette, the language words and the zoom on; step 29 also `SetLeftMargin`.) `SetShowWhitespaces` and the language regexes only apply to the ImGui editor.
- Error markers cover the whole line's text (the script editor gives a line, not a column range).


## Step 28: ready for an opt-in (`OKAY_CODE_EDITOR=own`)

### The switch

`editor/CodeEditorChoice.hpp` (pure, tested in `test_okayui_script_adapter`) reads `OKAY_CODE_EDITOR`:

| Value | Code widget |
|---|---|
| unset (the default) | the ImGui editor, unless the developer-only OkayUI backend is on (then ours, as in step 27) |
| `own` (also `okayui`, `okay`, `1`) | ours, even with the rest of the editor on Dear ImGui |
| `imgui` (also `0`, `vendored`) | the ImGui editor, even with the OkayUI backend on |

`main.cpp` defines `oked::UseOwnCodeEditor()`; `ScriptEditorWidget` asks it instead of `UseOkayUIBackend()`. The host's mouse and key
routing to hosted OkayUI regions (`HostsOkayUI()` in `MainShared.hpp`) is now on when either the OkayUI backend or our code editor is on,
so the code area gets the wheel, clicks and the full key set while it has the keyboard. The first script opened logs
`Script editor code widget: ...` (which one, and why). A build without OkayUI always uses the ImGui editor.

### HAVE and GAP, checked again against every call `editor/main.cpp` makes (before this step and after)

From `grep -oE "te(->|\.)[A-Za-z]+\(" editor/main.cpp` (40 different calls) plus what ImGuiColorTextEdit does by itself (its key list,
its hover tooltips). Everything in the table further up was HAVE already; these were the gaps found now.

| Feature the script editor gets from ImGuiColorTextEdit | Before | After | How |
|---|---|---|---|
| The script editor's own palette (`SetPalette(OkayScriptPalette())`) | GAP: ours used its built-in copy, so a palette change would not follow | HAVE | `ScriptEditorWidget::SetPalette` maps all 24 `TextEditor::PaletteIndex` colours (`ScriptEditorCodeEdit::SetPalette`) |
| The script editor's words (`SetLanguageDefinition(OkayScriptLangDef())`): its keyword list, type names, and every VM built-in as a known word | GAP: ours used `OkayScriptLanguage()`; the built-ins (`print`, `spawn`, ...) were not coloured | HAVE | `SetLanguageWords` from the definition's `mKeywords`, `mTypeNames`, `mIdentifiers`; kept when colouring is turned off and on |
| Hover tooltip with a built-in's signature and doc (the library shows `mDeclaration`) | GAP ("the caller's", and the caller did not do it) | HAVE | `CodeEdit::SetWordInfo`, `WordAtScreen`; an error message on the line wins |
| Insert key: overwrite mode with a block caret | GAP | HAVE | `CodeEdit::SetOverwrite`, `TypeText`; never overwrites past the end of the line; one undo step |
| Ctrl+Insert copy, Shift+Insert paste, Shift+Delete cut | GAP | HAVE | `handleKeys` |
| `SetReadOnly` (added to the widget on the `script-buffer-no-cut` branch) | adapter only | HAVE | `ScriptEditorWidget::SetReadOnly` forwards to both |
| Zoom (Ctrl+wheel, View > Zoom) | GAP: ours stayed at the UI scale | HAVE | `ScriptEditorWidget::SetZoom(s_zoom)`, drawn with `PushTextScale(uiScale * zoom)` |
| The launcher look's proportional font (Roboto): caret and selection drifted off the text | GAP | HAVE | each character drawn on its own, centred in a cell as wide as the font's wide letters (`MonospaceFont()` false); the 8x8 font still draws in runs |
| Opt-in switch for people, not just developers | GAP (only the developer-only backend) | HAVE | `OKAY_CODE_EDITOR=own` |
| Show whitespace, left margin, the language's regexes | n/a | n/a | the script editor turns whitespace off; the margin is the gutter; ours has its own tokenizer |
| ImGui popups (autocomplete, hover help) drawn over the code | GAP | GAP (HAVE since step 29) | OkayUI draws after ImGui, so an ImGui popup over the code area is hidden under it. Step 29 draws the code into ImGui's draw list |
| The ImGui editor's code font (`g_codeFont`) | GAP | GAP (HAVE since step 29) | step 29: JetBrains Mono through `OkayUI::PushCodeFont` |

Tests added: `test_okayui_codeedit` (proportional font: every glyph quad of `iiiiWWWW` and `mlmlmlml` lies in its cell; the 8x8 font is still
monospaced; the overwrite block caret's pixels; `TypeText` in read-only; `WordAtScreen` on words, blanks, outside, NaN; the hover tooltip shows
for a word with help, not for one without, and the error message wins) and `test_okayui_script_adapter` (the switch's parsing and decision;
the palette mapping of all colours, alpha halving, short arrays ignored; the language words colour keywords, types and built-ins and survive
colouring off and on; the tooltip in a drawn frame; overwrite through the API and through the Insert key; Ctrl+Insert, Shift+Insert,
Shift+Delete).

### ImGui and ours, measured (`codeedit_bench`)

`editor/tools/codeedit_bench.cpp` (target `codeedit_bench`, built with the editor and the tests ON) runs both widgets headless on the
same generated OkayScript (classes, functions, strings, numbers, `//`, `#` and block comments), with the script editor's language and
palette, in a 1000 x 700 view of a 1280 x 800 frame. Timed: the widget's CPU frame, `ImGui::NewFrame` to `ImGui::Render` and
`OkayUI::BeginFrame` to `OkayUI::EndFrameData` (rasterising is a GPU's job in the editor and is not timed). Idle: 240 frames after 60
of warm-up. Typing: one letter per frame in the middle of the file, 240 frames. Scrolling: one wheel step per frame from the top,
240 frames. Memory: bytes held through `operator new` after the editor was made, given the text and drawn 300 frames. Release build,
this shared Linux machine (other jobs ran at the same time, so single numbers move by about 0.05 ms between runs; the p95 is the
steadier one). `codeedit_bench bench 1000 10000 100000`:

| Lines | Editor | SetText ms | first frame ms | idle frame ms (mean / p95) | typing frame ms (mean / p95) | scrolling frame ms (mean / p95) | memory MB |
|---|---|---|---|---|---|---|---|
| 1000 | ImGuiColorTextEdit | 0.67 | 1.24 | 0.100 / 0.199 | 0.537 / 0.700 | 0.054 / 0.070 | 0.5 |
| 1000 | OkayUI::CodeEdit, 8x8 font | 0.12 | 0.28 | 0.051 / 0.064 | 0.040 / 0.063 | 0.096 / 0.136 | 0.1 |
| 1000 | OkayUI::CodeEdit, launcher look (as in the editor) | 0.19 | 0.15 | 0.087 / 0.118 | 0.053 / 0.087 | 0.077 / 0.103 | 0.1 |
| 10000 | ImGuiColorTextEdit | 10.65 | 14.31 | 0.223 / 0.288 | 3.578 / 4.100 | 0.126 / 0.160 | 4.9 |
| 10000 | OkayUI::CodeEdit, 8x8 font | 0.97 | 0.13 | 0.047 / 0.061 | 0.040 / 0.059 | 0.052 / 0.079 | 0.8 |
| 10000 | OkayUI::CodeEdit, launcher look (as in the editor) | 0.97 | 0.10 | 0.041 / 0.054 | 0.037 / 0.056 | 0.042 / 0.061 | 0.8 |
| 100000 | ImGuiColorTextEdit | 57.12 | 38.59 | 0.143 / 0.210 | 38.271 / 60.583 | 0.123 / 0.146 | 47.9 |
| 100000 | OkayUI::CodeEdit, 8x8 font | 9.92 | 0.30 | 0.046 / 0.056 | 0.117 / 0.169 | 0.045 / 0.054 | 7.2 |
| 100000 | OkayUI::CodeEdit, launcher look (as in the editor) | 8.83 | 0.26 | 0.044 / 0.057 | 0.105 / 0.153 | 0.053 / 0.053 | 7.2 |

**Met or not met** (ours against ImGuiColorTextEdit; "launcher look" is what the editor uses):

| Measure | 1k lines | 10k lines | 100k lines |
|---|---|---|---|
| Set the text | met (5x faster) | met (5 to 10x) | met (6x) |
| Idle frame | met (about equal at this size, within the noise) | met (3 to 5x faster) | met (3x) |
| Typing frame | met (8 to 10x faster: ImGui re-colours the line with regexes) | met (about 100x) | met (about 350x: ImGui takes 38 to 42 ms a key, ours 0.1 ms) |
| Scrolling frame | about equal (within the noise: 0.045 to 0.08 ms against 0.054 to 0.14 ms over five runs) | met (2 to 3x) | met (2 to 3x) |
| Memory | met (0.1 MB against 0.5 MB) | met (0.8 against 4.9 MB) | met (7.2 against 47.9 MB) |

**What was slow and was fixed.** The first run (before the fix) had ours slower on idle and scrolling frames at every size: 0.21 to 0.31 ms
against ImGui's 0.09 to 0.16 ms. A profile (valgrind callgrind, `codeedit_bench idle own 10000 200`) showed 76% of the frame in OkayUI's
8x8 text path asking the font's pixel function 64 times for every character drawn (`defGlyph`, `Font8x8::Glyph`, `drawText`); the editor
itself was 3%. `OkayUI.cpp` now works out each bitmap font's runs of lit pixels once (`GlyphRunCache`, fonts up to 8x8, two fonts cached)
and draws from them: the same quads, so the same pixels (the golden-image tests still pass), and 3.6x fewer instructions for the idle
frame. What is left of the classic-look frame is the vertex writing for those quads. The launcher look (Roboto, one textured quad per
character) never had the problem.

Screenshots of the same script with colouring, an error marker on line 10 and a breakpoint on line 14, from `codeedit_bench shots <dir>`:
`codeedit-imgui.png` (ImGuiColorTextEdit on Dear ImGui through SDL's software renderer), `codeedit-own.png` (ours, launcher look, as in the
editor) and `codeedit-own-classic.png` (ours, 8x8 font). They are not committed (they are not golden images); run the tool to make them.


## Step 29: up to par with the ImGui editor's look (`claude2/okayui-codeedit-par`)

The owner's rule: "we only switch when it's faster and up to par". Step 28 made ours faster; a side-by-side screenshot showed it was
not up to par. This step closes what the screenshots showed. The switch stays off (`OKAY_CODE_EDITOR` unset = the ImGui editor).

### What the screenshot comparison showed, and what was done

The same script, the same error on line 10 (`unknown name 'labl' (did you mean 'label'?)`), a breakpoint on line 14 and the caret on
line 7, from `codeedit_bench shots <dir> <suffix>`. `codeedit_bench` now loads the ImGui editor's real code font (JetBrains Mono baked at
48 px, shown at 16 px, `SetLeftMargin(22)`, exactly as `editor/main.cpp` does); before, its ImGui picture used Dear ImGui's default
13 px bitmap font, which is not what the editor shows.

| Gap seen in the screenshots | Before | After | How (own code; files) |
|---|---|---|---|
| 1. The error message at the end of the line | only a squiggle under the line (the message only as a tooltip) | the message after the line, four character widths on, in the error colour (230, 140, 128), cut to 72 characters, clipped to the view; a `warning: ` marker in amber (214, 176, 96) with an amber wash; the tooltip says `Error at line N: ...` | `CodeMarker::warning`, `CodeMarker::underline`, palette `errorText`, `warningText`, `warningLine`; the adapter reads the `warning: ` prefix and turns the underline off (the script editor draws its own squiggle under the token, for both widgets) |
| 2. Letter spacing and line height | Roboto drawn one character per wide cell (10 px at 16 px text): the code looked spread out; rows 25 px | JetBrains Mono, the ImGui editor's font, 7 px a character and 24.8 px a row at 16 px, the same numbers ImGuiColorTextEdit reports (`codeedit_bench shots` prints both); text starts 50 px right of the content origin in both | the code font is baked into OkayUI's atlas next to Roboto (same sizes, the UI font packed first so its glyphs keep their places); `OkayUI::PushCodeFont` / `PopCodeFont` / `CodeFontActive`; each character advances a whole number of pixels (7.27 px rounded to 7, as ImGui's `PixelSnapH`); rows are font size x spacing, not rounded; the text sits at the top of its row; the gutter is laid out as the ImGui one (left margin, then " N ", numbers ending two characters left of the text); `SetLeftMargin`, `SetPadding` (the child window's padding) |
| 3. Current line and breakpoint | a faint fill of the text area only, only with the keyboard; a blue dot in the gutter | a fill and a 1 px box across the whole row (gutter included) whenever there is no selection, a dimmer fill without the keyboard; a breakpoint fills the whole row with the palette's colour; an error or warning washes the whole row | palette `currentLineInactive`; the adapter maps `CurrentLineFillInactive` and passes the breakpoint colour as it is (no longer forced opaque) and the background as the gutter colour |
| 4. Popups hidden under the code | OkayUI drew its frame after ImGui, so the autocomplete list and hover help were under the code | the code's geometry is put into the ImGui window's draw list where the code is laid out; ImGui popups opened later (autocomplete, hover help, any window over it) are drawn after it | `OkayUI::BeginGeometryCapture` / `EndGeometryCapture` (take what a region draws out of the frame), `OkayUI::TextureForRenderer`, `editor/OkayUIInImGui.hpp` (`AppendGeometry`, `DrawOkayUIHere`, `SdlRendererTextures`, `StyleLikeImGuiChild`); `ScriptEditorWidget::Render` uses them and draws the child border |
| 5a. Child window look | no padding, 10 px scroll bars in our own colours, horizontal bar only when needed, no border | the child window's padding, ImGui's scroll bar thickness, colours and rounding, the horizontal bar always shown, the border | `SetPadding`, `SetScrollbarSize`, `SetAlwaysShowHorizontalScrollbar`, `okayimgui::StyleLikeImGuiChild` |
| 5b. Gutter colour | a separate darker gutter | the background (the ImGui editor has no gutter colour) | the adapter sets `gutter` to the background |

Left as they are, on purpose: ours boxes the bracket next to the caret (a feature the old one lacks); ours shows the caret at once (the old
one waits 400 ms). Left, not on purpose: glyph edges are a little different (our own TrueType rasteriser, no 2x oversampling: ImGui's
glyph ink is about one pixel wider to the left and top); the current-line box ends 2 px further right; ImGui's 1 px box lines are
anti-aliased over two rows at a 24.8 px pitch, ours are one sharp row; OkayUI tooltips of our own widget are still drawn by OkayUI on top
of everything (they follow the pointer, so they do not cover a popup the user is looking at, but they could).

### Pixel difference against the ImGui editor's screenshot

`codeedit_bench diff a.png b.png [out.png]`: the mean absolute difference per RGB channel and the share of pixels where a channel differs by
more than 32. Same 760 x 400 frame, same script, error and breakpoint.

| Pair | Mean difference (of 255) | Pixels off by more than 32 |
|---|---|---|
| step 28: `codeedit-own.png` against `codeedit-imgui.png` (ImGui with its default bitmap font) | 11.04 | 16.5 % |
| step 28 widget against the ImGui editor with its real code font (same harness as below) | 10.81 | 17.2 % |
| **step 29: `codeedit-own-v2.png` against `codeedit-imgui-v2.png`** | **3.24** | **4.6 %** |

What is left is almost all glyph edges (different rasterisers), the bracket boxes and the caret on line 7, and the box and scroll bar
edges. The screenshots are not committed (they are not golden images); `codeedit_bench shots <dir> -v2` writes them.

### Speed after (`codeedit_bench bench 1000 10000 100000`, Release, this shared machine)

A new row measures ours the way the editor runs it: inside the ImGui window (ImGui `NewFrame` to `Render` with the OkayUI frame around
it, the capture and the copy into ImGui's draw list included), which is the like-for-like comparison with the ImGui row. Second of two
runs (the first agrees within the noise; single numbers move by about 0.01 to 0.05 ms between runs).

| Lines | Editor | SetText ms | first frame ms | idle frame ms (mean / p95) | typing frame ms (mean / p95) | scrolling frame ms (mean / p95) | memory MB |
|---|---|---|---|---|---|---|---|
| 1000 | ImGuiColorTextEdit (with the code font) | 0.53 | 0.79 | 0.058 / 0.125 | 0.385 / 0.425 | 0.043 / 0.054 | 0.5 |
| 1000 | OkayUI::CodeEdit, 8x8 font, OkayUI frame only | 0.15 | 0.34 | 0.052 / 0.066 | 0.034 / 0.055 | 0.053 / 0.067 | 0.1 |
| 1000 | OkayUI::CodeEdit, launcher look, OkayUI frame only | 0.11 | 0.06 | 0.029 / 0.034 | 0.026 / 0.034 | 0.028 / 0.035 | 0.1 |
| 1000 | **OkayUI::CodeEdit hosted in the ImGui window (as in the editor)** | 0.11 | 0.12 | 0.036 / 0.041 | 0.034 / 0.045 | 0.039 / 0.049 | 0.1 |
| 10000 | ImGuiColorTextEdit (with the code font) | 5.20 | 3.60 | 0.109 / 0.127 | 3.420 / 3.904 | 0.120 / 0.142 | 4.9 |
| 10000 | OkayUI::CodeEdit, 8x8 font, OkayUI frame only | 0.85 | 0.13 | 0.051 / 0.064 | 0.046 / 0.064 | 0.053 / 0.072 | 0.8 |
| 10000 | OkayUI::CodeEdit, launcher look, OkayUI frame only | 0.94 | 0.08 | 0.029 / 0.035 | 0.032 / 0.043 | 0.026 / 0.031 | 0.8 |
| 10000 | **OkayUI::CodeEdit hosted in the ImGui window (as in the editor)** | 0.97 | 0.09 | 0.042 / 0.052 | 0.041 / 0.052 | 0.039 / 0.046 | 0.9 |
| 100000 | ImGuiColorTextEdit (with the code font) | 55.37 | 34.25 | 0.124 / 0.151 | 35.633 / 46.202 | 0.165 / 0.213 | 47.9 |
| 100000 | OkayUI::CodeEdit, 8x8 font, OkayUI frame only | 10.52 | 0.45 | 0.072 / 0.111 | 0.119 / 0.171 | 0.048 / 0.061 | 7.2 |
| 100000 | OkayUI::CodeEdit, launcher look, OkayUI frame only | 8.77 | 0.23 | 0.029 / 0.033 | 0.111 / 0.168 | 0.029 / 0.037 | 7.2 |
| 100000 | **OkayUI::CodeEdit hosted in the ImGui window (as in the editor)** | 9.10 | 0.28 | 0.041 / 0.047 | 0.120 / 0.182 | 0.040 / 0.051 | 7.3 |

**Met:** the hosted row (what the editor runs) is faster than or equal to ImGuiColorTextEdit on every measure at every size, in both runs.
The closest is scrolling at 1,000 lines (0.039 / 0.049 against 0.043 / 0.054; the other run 0.043 / 0.062 against 0.048 / 0.079). Under
callgrind an idle frame at 1,000 lines costs about 0.56 million instructions hosted against 1.12 million for ImGuiColorTextEdit
(`codeedit_bench idle hosted 1000 300`, `idle imgui 1000 300`). The first hosted version spent 0.68 million; the vertex copy out of the
frame, the ImGui vertex writes and a binary search per glyph were made cheaper (`EndGeometryCapture` copies the vertices as they are,
`AppendGeometry` writes through ImGui's write pointers, `fontatlas::Size::Find` has an ASCII table). Not met, and not what the editor runs:
the classic 8x8 look (only with `OKAY_CLASSIC_UI`) is slower than the ImGui editor at scrolling 1,000 lines (0.053 against 0.043 ms).

### Tests added or changed

- `test_okayui_codeedit` (475 checks): the code font (fixed width while pushed, 7 px and 24.8 px at 16 px, every glyph of `iiiiWWWW`,
  `mlmlmlml` and `0123456789` in its own column, the gutter's width with and without a margin and padding, back to the UI font and to
  rounded rows without it); the row look (the current-line box through the gutter, dimmer without the keyboard, none with a selection; a
  warning's amber message and no underline); the error message after the line in the error colour and nothing between the code and it;
  the breakpoint and error washes over the gutter; geometry capture in both looks (what is taken equals what the frame loses, every index
  in range, the atlas texture carried, the frame before and after kept, the capture drawn back gives the same pixels, clicks still reach
  the editor, an End without a Begin takes nothing). Golden `codeedit_highlight.png` regenerated (the new row look and gutter).
- `test_okayui_codeedit_imgui` (new, built with the editor; `ctest -R okayui` runs it): ours hosted in an ImGui window with an ImGui
  popup opened over the code after it: the popup's draw list comes after the one holding the code, the pixel under the popup is the
  popup's colour, the code is drawn around it, the atlas went through the SDL_Renderer texture lookup, OkayUI's own frame no longer
  holds the code, clicks still focus the editor; and the old way (OkayUI's frame drawn after ImGui) covers the popup. `AppendGeometry`
  alone: an unknown texture is skipped, flat geometry samples ImGui's white pixel, the clip rectangle is pushed, 80,000 vertices stay
  inside 16-bit indices.
- `test_okayui_script_adapter`: the warning prefix, no underline, the breakpoint colour as given, the inactive current-line colour,
  the gutter colour, `SetLeftMargin`.
- `test_okayui_compose`: goldens `compose_field.png` and `compose_code.png` regenerated (they hold a classic-look CodeEdit).
  `InputTextMultiline` (built on CodeEdit) keeps the UI font, a 4 px padding and no current-line wash: its golden is unchanged.

### Not done, said plainly

- **Never run in a real window, never on Windows.** Every picture above is from the headless harness (ImGui through SDL's software
  renderer; ours through the same renderer once hosted).
- ~~ImGui shortcuts are still not blocked while our editor has the keyboard (step 27).~~ (done in step 30)
- ~~Our own hover tooltip is drawn by OkayUI on top of everything (not moved into ImGui's layer).~~ (done in step 30)
- The pixel difference that is left (3.24 of 255) is mostly glyph edges from a different rasteriser; not chased further.


## Step 30: the gaps left after step 29 (`claude2/okayui-codeedit-par2`)

The owner's rule: "we only switch when it's faster and up to par". Step 29 left five gaps. The switch stays off
(`OKAY_CODE_EDITOR` unset = the ImGui editor).

### 1. The keyboard: our editor now takes it from Dear ImGui the way the ImGui editor does

How the ImGui editor does it (read in `editor/vendor/TextEditor.cpp`, not copied): while its child window is focused it sets
`io.WantCaptureKeyboard` and `io.WantTextInput` for the rest of the frame and empties ImGui's character queue. Its child window has no
navigable items, so ImGui's keyboard navigation has nothing to move to. What the script editor lets through was checked in
`editor/main.cpp`:

| Shortcut | ImGui editor | Ours before | Ours now |
|---|---|---|---|
| Ctrl+P (Play / Stop) | works (runs before the text-field check in `HandleShortcuts`) | works | works |
| Ctrl+S (save the script) | works (the script editor's own check: window focused) | works | works |
| Ctrl+W, Ctrl+Shift+T (close / reopen a script tab) | work (checked before the code draws, when `WantTextInput` is still false) | **blocked** (our flag was already set from the frame before) | work (let through by name while the code has the keys) |
| Scene shortcuts (Ctrl+Z on the scene, Delete, Ctrl+D, W/E/R...) | blocked (`OwnShortcutsWindowFocused`) | blocked | blocked |
| Arrows, Space, Enter, Tab, Escape, Page Up/Down, Home/End | only the code | **also ImGui's keyboard navigation**: the arrows moved ImGui's nav cursor off the code to a button in the Script Editor window, and Space or Enter then pressed it | only the code: `okayimgui::ClaimKeyboardForCode` makes the code item those keys' owner (`ImGui::SetKeyOwner`), so ImGui's navigation (which reads only keys nobody owns) leaves them alone; the script editor's own checks (`ImGui::IsKeyPressed` with no owner: autocomplete, Ctrl+S) still see them |
| Typed characters | taken out of ImGui's queue | left in the queue | taken out (`ClaimKeyboardForCode`) |
| Keys while a popup, the find box or another window has focus | go there (the child is no longer focused) | **still typed into the code** (OkayUI keeps its focus until a click) | go there: the code has the keyboard only while its ImGui window is focused, and gets it back with the window's focus; the end-of-frame keyboard capture in `main.cpp` runs only while the code really has the keys |
| The scene view's keys while typing with the pointer over the scene (W/E/R, V for vertex snap) | reach the scene (they run before the code sets `WantTextInput`) | blocked | blocked (ours is stricter here, on purpose: typing a `w` in code should not change the gizmo) |

Tested in `test_okayui_codeedit_imgui` (`KeyboardClaimTests`): with the claim, four Down presses leave ImGui's nav cursor on the code
and Space, Enter, Tab and Escape press nothing; Ctrl+S is still seen; `WantTextInput` and `WantCaptureKeyboard` are set; a typed
character leaves ImGui's queue. Without the claim (the state before), the arrows walk the nav cursor to the button and Space presses
it. The `main.cpp` lines (Ctrl+W, the capture at the end of the frame, `g_scriptCodeHasKeys`) are only syntax-checked.

### 2. The font atlas: measured, and it now grows for large code

| | Before | After |
|---|---|---|
| Atlas at start (every program in the launcher look) | 1024 x 1024 RGBA, 4 MB in memory (and the same again as a GPU texture), 554 of 1020 rows used, Roboto and JetBrains Mono at 13, 16, 20, 26, 34, 46 px, about 50 ms to build once | the same |
| Code at 16 px x UI scale (0.7 to 2) x zoom (0.7 to 3): up to 96 px | 214 of 648 scale x zoom pairs drawn from a smaller bake, scaled up to 2.09x (blurry) | the first time code is asked for past 46 px, the atlas grows (between two frames, never in the middle of one) to nine code sizes (62, 80, 96 px added): 1024 x 2048, 8 MB, 1361 of 2044 rows used, about 70 ms once; then no pair is drawn larger than its bake (worst 1.03x, inside the half-pixel rounding) |
| UI text after the growth | n/a | the same pixels (the UI font is packed first, so its glyphs keep their texels) |
| Growth path | doubled once from 1024 to 2048 rows if the code font did not fit; never tested | tested: from 128 rows it doubles three times to the same atlas; capped below what the code font needs, the atlas keeps the UI font alone and the code font falls back to it; capped below the UI font, no atlas (the 8x8 font is used) |

A program that never shows code past 46 px (the launcher, the player) keeps the 4 MB atlas. The SDL texture is made again after a
growth; the GL, Direct3D 11 and software backends already re-upload when the size changes. New: `OkayUI::GetFontAtlasInfo`,
`CodeFontBakedSizeFor`, `TestBuildFontAtlas` (`okayui/src/OkayUI_FontAtlas.cpp`, `OkayUI.cpp`). Tested in `test_okayui_codeedit`
(`AtlasTests`).

### 3. The hover help is an ImGui tooltip now

`CodeEdit::SetHostDrawsTooltip(true)` stops OkayUI drawing it; `CodeEdit::Hover()` says what is under the pointer (an error, a
warning or a word's help, its line and text) and `okayimgui::ShowCodeHover` draws it with `ImGui::BeginTooltip`, styled like the ImGui
editor's ("Error at line N:" in red or "Warning at line N:" in amber, a separator, the message; a built-in's help as plain text), in the
code font. It is an ImGui window in ImGui's own order, so it stacks with ImGui's popups the way the old one did. Tested
(`TooltipLayerTests`): the tooltip's draw list comes after the code's and after the autocomplete popup's, and OkayUI's frame draws
nothing for it; OkayUI's own tooltip, for contrast, is in OkayUI's frame. Screenshots: `codeedit-{imgui,own}-tip-{word,error}.png`
(difference 3.46 and 4.23 of 255).

### 4. The JetBrains Mono licence ships with the release

`docs/THIRD_PARTY.md` already listed the font, its source and the OFL, and `assets/fonts/JetBrainsMono-OFL.txt` sits next to the other
fonts. The dist script only copied it into `Assets/Fonts/`, silently (`|| true`). `scripts/build-windows-dist.sh` now also puts it in
`licenses/` and `Tools/licenses/`, writes `THIRD_PARTY_NOTICES.txt` (top level and `Tools/`) naming the font, the three programs that
carry it, the copyright line and the licence, adds a line to `README.txt`, and stops if a font licence file is missing. The zip and the
NSIS installer take the whole folder. Checked with `bash -n` and by running the new block alone into a scratch folder; the full Windows
build was not run. Not covered: the single `.exe` files the release uploads for the in-app updater.

### 5. Behaviour differences found in `main.cpp`'s use of the widget and in the ImGui editor

| Behaviour | ImGui editor | Ours before | Now |
|---|---|---|---|
| Typing with AltGr (Ctrl+Alt on Windows: `{ [ @ \` on German, French and other keyboards) | types | **dropped** (text was ignored with Ctrl held) | types |
| Alt+Backspace | undo | deleted a character | undo |
| Ctrl+wheel | zooms only (ImGui does not scroll with Ctrl) | zoomed and scrolled | zooms only |
| Wheel step | 5 font heights down, 2 sideways, at most 2/3 of the view (an ImGui child window) | 3 rows / 3 characters | the same as ImGui (`CodeEdit::SetWheelStep`, set by the widget from the font size) |
| Enter after `{` with the cursor before `}` (auto-pair on) | the widget copies the indent, then the script editor adds a level and pushes `}` down | **the block was opened twice** (ours did both, then the script editor did it again) | the script editor's way: the widget copies the indent only (`SetBracketIndent(false)`) |
| Enter after `{` otherwise | copies the indent | copied the indent plus one level | copies the indent |
| Typing `)` before a `)` (auto-pair on) | steps over it | **doubled it** (ours reads typed text from SDL, not from ImGui's queue the script editor empties) | steps over it (`DropTypedText`) |
| `)` before an auto-inserted `)` in the frame Escape closed the suggestion list (or the mouse moved) | steps over it (ImGui holds the character back a frame, then the list is closed) | **doubled it: `zz(1))`** (the pairing step read ImGui's queue, which was empty that frame, while ours typed the `)` at once; and Escape with the list open turned our keys off) | steps over it: the pairing step reads the characters the code editor really types this frame (`editor/ScriptAutoPair.hpp`, OkayUI's frame text for ours), Escape alone no longer turns our keys off (2026-11-17) |
| Alt+1 (show / hide the scripts list) | does not type the 1 | typed it where SDL sends it | does not (`DropTypedText`) |
| Drag after a double click / triple click | extends by words / lines | extended by characters | by words / lines |
| The pointer over the code | the I-beam | the arrow (the editor never read OkayUI's cursor) | the I-beam (`okayimgui::CursorOf`, `SetImGuiCursor`) |
| `SetUserTypes` every frame (the script editor does) | re-colours only when the set changes | **re-coloured from the top every frame**: 64 ms a frame on a 200,000-line script scrolled to the end | only on a change: 50 frames in 0.03 ms; the types also survive colouring off and on |
| Tab / Shift+Tab on a selection | a tab character at each line start | four spaces (the file style the status bar shows) | unchanged on purpose |
| Shift+Tab with no selection | types a tab | removes one level | unchanged on purpose |
| Home | column 0 | first non-blank, then column 0 | unchanged on purpose |
| Ctrl+Backspace / Ctrl+Delete, Ctrl+Shift+Z, Shift+click | nothing | delete a word, redo, extend the selection | unchanged on purpose |
| Page Up / Down | a page minus four lines | a page | unchanged (small) |
| Caret | blinks after 400 ms | shown at once | unchanged (step 29) |

Tested in `test_okayui_codeedit` (`ParityTests`), `test_okayui_script_adapter` (user types) and `test_okayui_codeedit_imgui`
(`CursorTests`; `PairingTests`: `zz(1`, then Escape, a mouse move and `)` in one frame gives `zz(1)` with the caret after it, and
deciding from ImGui's queue gives the old `zz(1))`). The fuzz test found one bug in the new drag code (an edit between the click and the drag left the clicked word past the
end of the text); fixed and run with 12 seeds.

### Speed after (`codeedit_bench bench 1000 10000 100000`, Release, this machine with a load of about 6)

The hosted row now does every per-frame step `ScriptEditorWidget::Render` does (the keyboard claim, the wheel step, the cursor, the
tooltip), and both editors get the calls `main.cpp` makes every frame (`SetColorizerEnable`, `SetUserTypes`). CodeEdit also keeps the
colour tokens of the lines it draws from one frame to the next. Second of two runs (the first agrees; single numbers move by 0.01 to
0.03 ms between runs on this busy machine):

| Lines | Editor | SetText ms | first frame ms | idle frame ms (mean / p95) | typing frame ms (mean / p95) | scrolling frame ms (mean / p95) | memory MB |
|---|---|---|---|---|---|---|---|
| 1000 | ImGuiColorTextEdit | 0.67 | 0.95 | 0.068 / 0.143 | 0.595 / 1.532 | 0.062 / 0.063 | 0.5 |
| 1000 | **ours hosted in the ImGui window (as in the editor)** | 0.12 | 0.17 | 0.034 / 0.048 | 0.036 / 0.050 | 0.032 / 0.041 | 0.2 |
| 10000 | ImGuiColorTextEdit | 5.23 | 3.98 | 0.133 / 0.185 | 4.299 / 7.888 | 0.176 / 0.319 | 4.9 |
| 10000 | **ours hosted** | 1.60 | 0.22 | 0.056 / 0.089 | 0.055 / 0.115 | 0.043 / 0.062 | 0.9 |
| 100000 | ImGuiColorTextEdit | 62.56 | 35.93 | 0.124 / 0.154 | 45.976 / 77.575 | 0.117 / 0.140 | 47.9 |
| 100000 | **ours hosted** | 9.84 | 0.29 | 0.034 / 0.047 | 0.117 / 0.169 | 0.040 / 0.057 | 7.3 |

**Met** on every measure at every size in both of these runs. The closest is still scrolling at 1,000 lines, where wall time is within
the noise of this busy machine: of eight full runs during this step, six had ours ahead (0.032 to 0.040 ms against 0.040 to 0.062) and
two, taken while the load rose past 6, had ours behind on the mean (0.045 against 0.042, and 0.064 against 0.045). So it was also
counted under callgrind, which does not depend on the load (`codeedit_bench idle hosted |
imgui | hosted-scroll | imgui-scroll 1000 N`, the difference between 100 and 200 frames): ours 0.48 million instructions a frame idle
and 0.48 scrolling (0.54 before the token cache), ImGuiColorTextEdit 0.82 and 0.85. Memory at 1,000 lines went from 0.1 to 0.2 MB (the
token cache: 256 lines).

### Tests

`ctest -R okayui`: 27 of 27 pass. New or changed: `test_okayui_codeedit` (518 checks: `ParityTests`, `AtlasTests`),
`test_okayui_codeedit_imgui` (34 checks: `TooltipLayerTests`, `CursorTests`, `KeyboardClaimTests`), `test_okayui_script_adapter`
(116 checks: user types). `editor/main.cpp` passes a syntax-only compile with and without OkayUI.

### Not done, said plainly

- **Still never run in a real window, never on Windows.** Every check above is headless. The keyboard claim was tested against Dear
  ImGui's real navigation code, but not with a real keyboard, an input method or AltGr on a real layout.
- The single `.exe` files of a release (the in-app updater's downloads) carry no licence text beside them.
- Ours and the old one still differ on purpose where ours does more (the table above).
