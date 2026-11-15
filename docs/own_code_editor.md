# Own code editor: the text data model (`okay::text::Buffer`) and the plan

Own code. Part of the "replace every third-party library" work (`docs/own_code_roadmap.md`, row ImGuiColorTextEdit). This page covers the data model underneath the widget and the port of the widget onto it; the widget itself is described in `docs/own_ui_code_editor.md`.

## Where things stand (read this first)

Three layers, and they are at different stages:

| Layer | What | State |
|---|---|---|
| Seam | `ScriptEditorWidget` (`editor/ScriptEditorWidget.hpp`) is the one type `editor/main.cpp` uses for its code view. It holds the vendored `TextEditor` (default) and `ScriptEditorCodeEdit` (adapter over our `OkayUI::CodeEdit`) and forwards every call to the active one. | Done before this slice (step 27 of `docs/own_ui_code_editor.md`). No new interface was added: a second one would only duplicate it. The calls it lists are the calls the script editor makes (see the feature list below). |
| Widget | `OkayUI::CodeEdit`: drawing, mouse, keys, scrolling, highlighting, markers. Since slice 2 it keeps no text model of its own: lines, edits, undo, search, brackets, tokens and line marks are `okay::text::Buffer`'s. | Built and tested headless, fuzzed under ASan and UBSan; never run in a real window; off unless the developer-only OkayUI backend is on. |
| Data model | `okay::text::Buffer` (`okayui/include/okay/Text/TextBuffer.hpp`, `okayui/src/TextBuffer.cpp`, library `okay_text`): a pure library with an edit log, several carets, hostile-input limits and the helpers the widget needs. | Built and tested headless (slice 1). **Slice 2: `CodeEdit` now runs on it** (same public API, its 407 checks and two fuzzers unchanged and green). |

So the honest state of "own code editor": the widget exists and is the replacement candidate, and it now sits on the tested data model. Nothing runs it in a real window yet (see "Next steps").

## What was written from what (rule 19a)

`Buffer` was written from general text-editor design, in our own structure: a line table of byte strings, a position type (line, byte column), an edit log of (position, removed text, inserted text) with groups, Knuth-Morris-Pratt for search, and the well-formed UTF-8 byte table from the Unicode standard (chapter 3, table of well-formed sequences). No code, comments, tests or shader text from ImGuiColorTextEdit or any other editor was read, copied or followed for it. `editor/vendor/TextEditor.h` was not opened for this slice; the feature list below comes from `docs/own_ui_code_editor.md`, which was produced from the script editor's own call sites.

## The model

- Text is a list of lines; a line never holds `\n`. A text with N newlines has N+1 lines. Line breaks are `\n` only: CRLF and lone CR are turned into `\n` on the way in.
- Everything stored is well-formed UTF-8. Invalid input is repaired on the way in (every bad byte, NUL, overlong form, surrogate, value above U+10FFFF becomes U+FFFD). No function ever meets a broken sequence.
- A position is `{line, byte column}`. Every position given to any function is clamped into the text and moved back to a character start, so huge numbers and the middle of a character are safe.
- Offsets (byte index in the whole text) convert both ways (`OffsetOf`, `PosOf`); display columns with tab stops convert both ways (`DisplayColumn`, `ByteForDisplayColumn`).
- Every change goes through the edit log. `BeginGroup` and `EndGroup` nest and make one undo step. Undo and redo restore the selections as well as the text. Caps: number of groups (default 1000) and bytes held (default 64 MiB); the oldest steps are dropped.
- The text may not grow past `Options::maxBytes` (default 256 MiB); a change that would is refused and changes nothing. All size arithmetic is overflow-safe (tested with a limit of `SIZE_MAX`).
- Several carets and selections at once: typing, Backspace, Delete and word delete apply to every caret as one undo step, and the carets are kept in step as other carets edit.
- Word movement (Ctrl+Left and Right), word under the caret, indent and outdent of line ranges, the text Enter should insert (keeps indent, one level deeper after an opening bracket), bracket matching with a caller predicate to skip strings and comments and a scan limit.
- Find (next, previous, wrap, whole word, ASCII case folding, needles that span lines), find all, replace all (one undo step, one linear edit whatever the number of matches). Search is KMP: linear time for any needle and text.
- Tokenizer hook: the caller gives one function (line, state in, tokens out, state out); `Buffer` caches the tokens per line and re-runs only from the first changed line. Out-of-range tokens from a careless tokenizer are clamped.
- Pure standard C++17: no `<mutex>`, no `<thread>`, no SDL, no Dear ImGui, no OkayUI. It also compiles with the MinGW cross compiler (syntax checked).

## What the script editor needs (the feature list)

The calls the script editor makes are listed in the HAVE and GAP table of `docs/own_ui_code_editor.md` (set and get text, lines, cursor, selection, undo, `InsertTextUndoable`, `Delete`, colouring with user types, error markers, breakpoints, geometry for the autocomplete popup, keyboard hand-over). Against that list, `Buffer` covers the model side of: text and lines, cursor and selection, undo and redo, insert and delete, find and replace, word and bracket helpers, indent, tokens. It does not do drawing, geometry (row height, caret screen position), the clipboard, error markers or breakpoints (those move with lines; `CodeEdit` does that today and would keep doing it, or be moved onto `Buffer` positions later).

## Tests

`okayui/tests/test_text_buffer.cpp` (ctest name `text_buffer`, about 83,000 checks in the default run):
- every feature on its own;
- hostile input: invalid and truncated UTF-8, NUL, a 1 MB paste with many lines (about 12 ms in this run, asserted under 2 s; budgets are multiplied by 20 under sanitizers), a 4 MB single line with 400 edits and a search, a needle built to make a naive search quadratic, 100,000 lines through replace all and indent plus undo, positions and offsets of `SIZE_MAX`, a size limit of 0 and of `SIZE_MAX`, the undo group cap and byte cap, junk bytes through every entry point with the invariants checked after each step;
- a randomised differential test against a `std::string` model: random insert, erase, replace, undo, redo, find (forward, backward, count) and replace all, text compared after every step, line count, offset round trip and undo depth compared too, then undo everything and redo everything. 6 seeds, `OKAY_TEXT_FUZZ_ITERS` steps each (default 4000), `OKAY_TEXT_FUZZ_SEED` changes the seed.

Run: default run clean (0 failures) with `-O1 -fsanitize=address,undefined`; 20,000 steps on five more seeds under the same flags: 0 failures (about 412,000 checks each); an `-O2` run without sanitizers: 0 failures. It also builds and passes through CMake (`ninja test_text_buffer`, `ctest -R text_buffer`). Only this test was run in this slice; the rest of the OkayUI tests and the editor were not rebuilt.

## Slice 2: `CodeEdit` ported onto `Buffer`

What moved into `Buffer` (and what `Buffer` gained for it):
- Storage, edits and undo. `CodeEdit` keeps only the rule for WHAT one undo step is (typing a word, a run of Backspaces, compound edits). `Buffer` gained `ReopenLastGroup` (the next edit joins the newest step) and `RecordSelectionsInLastGroup` (Redo puts the caret where it ended). The undo log is `Buffer`'s, capped at 2000 steps as before.
- Input repair, with options (`Options::badByte`, `dropControls`) so the widget keeps its old rule (bad bytes become `?`, control characters dropped) and the library keeps U+FFFD by default.
- Word movement, Prev and Next character, display column, Enter's indent text (`NewlineText`), find, find previous, count, replace all (`FindInLine` for the on-screen highlight), bracket matching with the token predicate (`MatchBracket` now also takes a line limit, and tells which bracket was next to the caret), the highlighter as the tokenizer hook (the model caches the end states and re-runs from the first changed line).
- Breakpoints and error markers as line marks (`NewMarkSet`, `SetMarks`, `Marks`): they move with every edit, undo and redo with the old rules (marks below move by the difference, marks on removed lines are dropped and do not come back). Tested in `test_text_buffer` (`TestEditorSupport`).
- Removed from `CodeEdit` (no duplicate left): the line vector, `applyRaw`, the undo structs, `shiftLines`, the sanitiser, `textIn`, `endOf`, `wordLeft`, `wordRight`, the per-line naive search, the bracket scan, the highlighter state cache.

Stayed in `CodeEdit` because it is widget behaviour, not model: caret and anchor, the desired column, word-at-point selection (it selects punctuation and blank runs too), Tab with a multi-line selection, the pixel and display code, the keys and the mouse.

Tests: `test_okayui_codeedit` (407 checks) and `test_okayui_codeedit_fuzz` (36 checks, 6 seeds) are unchanged and green; no check was edited. The tests that link `CodeEdit` (`okayui_script_adapter`, `okayui_host`, `okayui_compose`, `okayui_golden`, `okayui_render`, `okayui_render_bench`) are green too.

Small behaviour differences, said plainly (none is covered by a check, so none was caught by the unchanged tests):
- Find previous with an overlapping needle (`aa` in `aaaa`) now finds the last overlapping match before the caret; the old code walked non-overlapping matches from the line start.
- Replace all is one edit over the span from the first to the last match. If the replacement has newlines, markers and breakpoints on lines inside that span no longer move per match (marks below the span still move correctly). The caret still keeps its numeric position, as before.
- A bracket right at the caret that is inside a string or comment gives no pair (as before); the library now does this itself.
- The highlighter is run per line through the model's tokenizer hook; tokens that the callback gives out of range are clamped to the line.
- `Version()` counts model changes (a replace counts two); only equality was ever used.

## How to try it

Everything below was written from reading the code. NOTHING here was run in a real editor window (no window or GPU in the build machine), so every "you should see" is **unverified** unless it says otherwise.

**The switch.** The script editor has no switch of its own: its code view follows the editor's UI backend. The own code editor (`OkayUI::CodeEdit`) is used exactly when the OkayUI backend is on; the default (Dear ImGui backend) uses ImGuiColorTextEdit. Two ways to turn it on:
1. Start the editor with the environment variable `OKAY_UI_BACKEND=okayui` (Windows: `set OKAY_UI_BACKEND=okayui` then run `OkayEngine.exe`; Linux: `OKAY_UI_BACKEND=okayui ./OkayEngine`). It is honoured without any other flag and it wins over the saved setting.
2. Or with the developer flag `OKAY_DEV=1` set, open View > Rendering & Performance > UI backend and pick "OkayUI (preview)". It applies at once and is saved in `okay_settings.txt`. Pick "Dear ImGui (default)" to go back.
Turning the backend on also draws the Console, Crash Log and Audio Mixer with OkayUI; that is expected.

**What to open.** In a project, put a script file in the Project panel (for a quick test, copy `examples/editor-plugins/AlignTools.okay` into a project folder) and double-click it: it opens as a tab in the Script Editor window. Typing in a script attached to an object works the same way.

**Which backend is active** (these lines were added in this slice; they compile in a host syntax check but were not seen on screen):
- the Console's first lines say `Script editor: our own code editor (OkayUI backend, preview)` or `Script editor: ImGuiColorTextEdit (default)`;
- Help > About OkaySpace shows the same `Script editor:` line;
- the stderr line printed at startup (`okay::ownstack`) shows the UI toolkit (`ui=okayui` or `ui=imgui`).
Look of the editor itself: the own editor has a number gutter with a blue breakpoint column, a current-line band, and its own Darcula-like colours; ImGuiColorTextEdit has no breakpoint column of that kind. The picture of the own editor drawn by the headless renderer is in the screenshots named in the report of this change (not committed to the repo).

**What you should see in the own editor (unverified).** Keywords, strings, numbers and comments in different colours; Enter keeps the indent and goes one level deeper after `{`; Tab inserts spaces, Shift+Tab removes them; Ctrl+Z / Ctrl+Y undo and redo; Ctrl+C / X / V use the clipboard; clicking the narrow column left of the line numbers toggles a breakpoint; a script error shows a red mark in the gutter and a wavy underline; the caret next to a bracket highlights its partner; the autocomplete popup appears under the caret. Known to be rougher than the old editor: no code font (the cell width assumes a monospaced font, so the caret can drift with a proportional one), the editor's own palette is not used, show-whitespace and the left margin setting do nothing, and an ImGui popup that overlaps the code area is hidden behind it (see "NOT DONE" in `docs/own_ui_code_editor.md`).

**If it is wrong,** switch back with `OKAY_UI_BACKEND=imgui` (or the menu) and report what you saw; the text, caret and selection are copied across when the backend changes while a script is open.

## NOT DONE

- Multiple carets in the widget: `Buffer` supports them (typing, Backspace, Delete, word delete as one undo step), but `CodeEdit` still has one caret and one anchor (`Caret()`, `Anchor()`, the draw code, the mouse), so wiring them in is a widget change, not a cheap one. Left out.
- `Buffer` has no error marker or breakpoint tracking, no folding, no word wrap, no column (block) selection, no grapheme clusters (a combining mark is its own character; wide characters count as one column), no Unicode case folding (ASCII only), no regular-expression search.
- Replace all collapses a caret that sits inside the replaced span to the end of the replacement (it is one edit).
- Undo drops the oldest steps once the caps are reached; one paste bigger than the byte cap empties the log (and is itself not undoable).
- No real-use corpus (real scripts of real projects), no run on Windows, no GitHub CI run, no long soak, no libFuzzer harness. Only the tests above.
- The editor was not built here (it needs SDL2 and OpenGL and this slice does not touch it), so the editor compile is unverified. This slice changed no editor file.
- Owner approval of anything: none asked for, nothing flipped.
- Slice 2 was run on Linux only (Release for the CMake tests; the sanitizer runs compiled `OkayUICodeEdit.cpp` and `TextBuffer.cpp` with ASan and UBSan and linked the rest from the Release build). No editor file was changed or built (it needs ImGui and SDL with OpenGL); `editor/ScriptEditorCodeEdit.cpp`, `OkayUICodeEdit.cpp` and `TextBuffer.cpp` were syntax-checked with the host compiler and the MinGW cross compiler.

## How close is the replacement

The model layer: close to what the roadmap's criteria ask for headless (own tests, differential test, sanitizer clean), but it has no real-use corpus, no Windows run and no outside run. The widget layer (`CodeEdit`) now runs on it: 407 checks and both fuzzers green (the fuzzers and the model test also under ASan and UBSan, run here against the Release libraries with only the editor and model sources sanitised), never seen in a real window. The step that decides everything is still the same one as before this slice: run the OkayUI script editor in a real window, fix what that shows. By the roadmap's own criteria (real-use corpus, Windows and Linux CI, soak, no known bugs, owner approval) the ImGuiColorTextEdit replacement is NOT ready and the default stays ImGuiColorTextEdit. A rough guess: the code exists for about 80 percent of what the script editor does; the unproven part is the real window, the code font and the popups that overlap it.

## Next steps

1. (Done in slice 2: `CodeEdit` runs on `okay::text::Buffer`.) Add multiple carets to the widget.
2. A code font and a monospaced cell width from the font, not an assumption.
3. Run the script editor on the OkayUI backend in a real window (Linux, then Windows) and fix what that shows.
4. Real-use corpus: open and edit every script of the example projects through the adapter and compare with the old editor's text.
5. Owner approval, flip the default for one release, delete `editor/vendor/` one release later.
