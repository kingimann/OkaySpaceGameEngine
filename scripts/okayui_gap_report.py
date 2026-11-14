#!/usr/bin/env python3
"""Gap report: for each family of Dear ImGui calls the editor, launcher and shared code use, how many call sites there are and
whether OkayUI has an equivalent (Done / Partial / Missing), with the file that proves it. Writes docs/okayui_gap_report.md.

It builds on scripts/imgui_usage_report.py (the same scan: every `ImGui::Call(`, every ImGui type / flag / enum name and every
draw-list method in editor/, launcher/ and shared/, comments and strings ignored, vendored widget skipped). Counts are measured by the
scan. The status of a family is a judgement written in FAMILIES below; the script checks what it can: every OkayUI name a family
"needs" must be declared in okayui/include/okay/UI/*.hpp and every proof file must exist, otherwise the family is reported as
Missing / unproven. The numbers per family that are measured are: call sites (editor / launcher / shared), call sites whose NAME has no
OkayUI function, and uses of ImGui flags and types that have no OkayUI counterpart.

Run from the repository root:   python3 scripts/okayui_gap_report.py          (writes the report)
                                python3 scripts/okayui_gap_report.py --check  (fails if the report on disk is stale)
"""
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import imgui_usage_report as base   # noqa: E402

ROOT = base.ROOT
OUT = os.path.join(ROOT, "docs", "okayui_gap_report.md")


def names(s):
    return set(s.split())


# Each family: label, call names (a set) and/or a regex for call names, ImGui type prefixes that belong to it, the OkayUI names it needs
# (all must be declared), proof files (all must exist), the status and a note on what is still missing.
FAMILIES = [
    dict(name="Item and hover queries", calls=r"IsItem\w+|IsAnyItem\w+|GetItem\w+|SetItemKeyOwner|SetNextItemAllowOverlap|SetItemDefaultFocus|IsMouseHoveringRect",
         types=("HoveredFlags", "FocusedFlags"), needs="IsItemHovered IsItemEdited IsItemDeactivatedAfterEdit IsAnyItemHovered Hovered_AllowWhenDisabled",
         proof="okayui/tests/test_okayui_drawlist.cpp okayui/tests/test_okayui_gaps.cpp", status="Done",
         note="Edit tracking, hover flags with delays and disabled items are in. `SetItemKeyOwner` (5 uses) has no meaning without ImGui's key ownership and stays a host call."),
    dict(name="Layout and cursor", calls=r"SameLine|Dummy|Spacing|Separator|NewLine|Indent|Unindent|GetCursor\w+|SetCursor\w+|GetContentRegion\w+|GetWindowContentRegion\w+|BeginGroup|EndGroup|AlignTextToFramePadding|CalcTextSize|GetFrameHeight\w*|GetTextLineHeight\w*|SetNextItemWidth|PushItemWidth|PopItemWidth|CalcItemWidth|Columns|NextColumn|PushMultiItemsWidths|GetTreeNodeToLabelSpacing|GetFontSize",
         types=(), needs="SameLine Dummy GetCursorScreenPos GetContentRegionAvail CalcItemWidth GetCursorStartPos GetWindowContentRegionMin",
         proof="okayui/tests/test_okayui_drawlist.cpp okayui/tests/test_okayui_compat.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="`PushMultiItemsWidths` (1 use, a three-cell row in the editor) is missing; `GetTreeNodeToLabelSpacing` (1) is missing. `AlignTextToFramePadding` does nothing (OkayUI rows are already centred)."),
    dict(name="Text (display)", calls=r"Text\w*|BulletText|Bullet|SeparatorText|LabelText|PushTextWrapPos|PopTextWrapPos",
         types=(), needs="Text TextColored TextDisabled TextWrapped TextUnformatted BulletText SeparatorText PushTextWrapPos",
         proof="okayui/tests/test_okayui_widgets.cpp okayui/tests/test_okayui_values.cpp", status="Done",
         note="Callers format first (no printf-style `Text`)."),
    dict(name="Buttons, checkbox, radio, images", calls=r"Button|SmallButton|Checkbox|RadioButton|ArrowButton|ImageButton|InvisibleButton|ColorButton|Image|ProgressBar|ToggleSwitch",
         types=("ButtonFlags", "ImTextureID"), needs="Button SmallButton Checkbox RadioButton ArrowButton ImageButton InvisibleButton Image ProgressBar",
         proof="okayui/tests/test_okayui_behaviour.cpp okayui/tests/test_okayui_widgets.cpp", status="Partial",
         note="`ButtonFlags_MouseButtonLeft/Right` (12 uses on `InvisibleButton`) are not honoured: only the left button clicks. `ImTextureID` is an `SDL_Texture*` here (24 uses in the launcher)."),
    dict(name="Value widgets (drag, slider, number)", calls=r"Drag\w+|Slider\w+|InputInt|InputFloat\w*|InputDouble",
         types=("SliderFlags",), needs="DragFloat DragInt DragFloat2 DragFloat3 SliderFloat SliderInt InputInt InputFloat",
         proof="okayui/tests/test_okayui_values.cpp", status="Partial",
         note="No printf format strings, no `SliderFlags_Logarithmic` (5 uses) or `AlwaysClamp` (3 uses), no `DragIntRange2`."),
    dict(name="Text input, single line", calls=r"InputText|InputTextWithHint|SetKeyboardFocusHere",
         types=("InputTextFlags",), needs="InputText InputTextWithHint InputText_Password InputText_CharsDecimal InputText_CharsNoBlank SetKeyboardFocusHere",
         proof="okayui/tests/test_okayui_behaviour.cpp okayui/tests/test_okayui_gaps.cpp", status="Done",
         note="Password (one dot per byte), the character filters, read-only, select-all on click, enter-returns-true. Callbacks (`CallbackResize`, 2 uses) and `AllowTabInput` are accepted and do nothing."),
    dict(name="Text input, multi-line", calls=r"InputTextMultiline",
         types=(), needs="InputTextMultiline CodeEdit",
         proof="okayui/src/OkayUIText.cpp okayui/tests/test_okayui_widgets.cpp okayui/tests/test_okayui_codeedit.cpp", status="Partial",
         note="Built on `CodeEdit` (selection, undo, clipboard, scrolling). No word wrap; `CtrlEnterForNewLine` (4 uses) is accepted and ignored."),
    dict(name="Child windows", calls=r"BeginChild|EndChild",
         types=("ChildFlags",), needs="BeginChild EndChild Child_Borders Child_AlwaysUseWindowPadding Child_AutoResizeY",
         proof="okayui/tests/test_okayui_gaps.cpp okayui/tests/test_okayui_behaviour.cpp", status="Done",
         note="Borders, padding, auto height (one frame behind), negative sizes, no-scroll-bar. `AutoResizeX`, `ResizeX/Y` are accepted and ignored (2 uses)."),
    dict(name="Windows, scrolling, focus", calls=r"Begin|End|SetNextWindow\w*|GetWindow\w+|SetWindow\w+|IsWindow\w+|FindWindow\w+|BringWindow\w+|GetCurrentWindow|GetScroll\w+|SetScroll\w+|GetMainViewport",
         types=("WindowFlags", "ImGuiWindow", "ImGuiViewport", "ImGuiCond"), needs="Begin End SetNextWindowPos SetNextWindowSize SetNextWindowFocus SetNextWindowBgAlpha SetWindowPos IsWindowFocused GetScrollY SetScrollHereY",
         proof="okayui/tests/test_okayui_windows.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="`FindWindowByName`/`FindWindowByID`/`GetCurrentWindow` and `ImGuiWindow` internals (30 uses, editor layout code) are ImGui internals with no OkayUI shape. No horizontal scrolling. No `NoSavedSettings`, `NoNav` (accepted)."),
    dict(name="Popups", calls=r"OpenPopup|BeginPopup\w*|EndPopup|CloseCurrentPopup|IsPopupOpen|ClosePopupToLevel|GetMousePosOnOpeningCurrentPopup",
         types=("PopupFlags",), needs="OpenPopup BeginPopup BeginPopupModal BeginPopupContextItem BeginPopupContextWindow IsPopupOpen",
         proof="okayui/tests/test_okayui_behaviour.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="`GetMousePosOnOpeningCurrentPopup` (4 uses, the editor's node-graph menus) and `ClosePopupToLevel` (1, the file dialog) are missing; `BeginPopupEx` is an internal."),
    dict(name="Menus", calls=r"MenuItem|BeginMenu|EndMenu|BeginMenuBar|EndMenuBar",
         types=(), needs="MenuItem BeginMenu BeginMenuBar", proof="okayui/tests/test_okayui_values.cpp", status="Done", note="Nested menus, shortcuts text, checked and enabled state, keyboard navigation."),
    dict(name="Combo, selectable, list", calls=r"BeginCombo|EndCombo|Combo|Selectable|ListBox\w*",
         types=("ComboFlags", "SelectableFlags"), needs="BeginCombo EndCombo Combo Selectable Combo_NoPreview",
         proof="okayui/tests/test_okayui_widgets.cpp okayui/tests/test_okayui_gaps.cpp", status="Done", note="The combo popup height flags are accepted and ignored."),
    dict(name="Tree nodes", calls=r"TreeNode\w*|TreePop|CollapsingHeader|SetNextItemOpen|IsItemToggledOpen",
         types=("TreeNodeFlags",), needs="TreeNode TreeNodeEx CollapsingHeader SetNextItemOpen", proof="okayui/tests/test_okayui_widgets.cpp", status="Done", note=""),
    dict(name="Tables", calls=r"BeginTable|EndTable|Table\w+",
         types=("TableFlags", "TableColumnFlags", "TableSortSpecs", "SortDirection", "TableRowFlags", "TableBgTarget"), needs="BeginTable TableSetupColumn TableHeadersRow TableGetSortSpecs TableSetupScrollFreeze Col_WidthStretch Col_NoResize",
         proof="okayui/tests/test_okayui_tables.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="Sorting, scrolling boxes, frozen rows and columns, row clipping, `NoResize` per column. Missing: column reordering and hiding, the header context menu, auto-fit by double click, nested tables, `TableGetHoveredColumn`."),
    dict(name="Tabs", calls=r"BeginTabBar|EndTabBar|BeginTabItem|EndTabItem|TabItemButton|TabBarFindTabByID",
         types=("TabItemFlags", "TabBarFlags"), needs="BeginTabBar BeginTabItem EndTabItem TabItemButton",
         proof="okayui/tests/test_okayui_values.cpp", status="Partial", note="No closable or reorderable tabs; `TabBarFindTabByID` (an internal, 1 use) is missing."),
    dict(name="Drag and drop", calls=r"BeginDragDrop\w*|EndDragDrop\w*|AcceptDragDropPayload|SetDragDropPayload|GetDragDropPayload",
         types=("DragDropFlags", "ImGuiPayload"), needs="BeginDragDropSource BeginDragDropTarget AcceptDragDropPayload SetDragDropPayload",
         proof="okayui/tests/test_okayui_values.cpp", status="Partial",
         note="Typed payloads, preview text and accept-before-delivery exist. Missing: `GetDragDropPayload` (4 uses: peek at the payload while dragging) and `BeginDragDropTargetCustom` (4, a target on a bare rectangle)."),
    dict(name="Docking and viewports", calls=r"Dock\w+|SetNextWindowDockID|IsWindowDocked|GetWindowDockNode|SetNextWindowViewport|UpdatePlatformWindows|RenderPlatformWindowsDefault|GetPlatformIO|SaveIniSettings\w*|LoadIniSettings\w*",
         types=("DockNode", "ImGuiDockNodeFlags", "ConfigFlags", "ImGuiViewport"), needs="DockSpace DockBegin DockEnd",
         proof="okayui/tests/test_okayui_dock.cpp okayui/tests/test_okayui_dock_fuzz.cpp", status="Partial",
         note="OkayUI has its own dock system (dock space, splitters, tabs, layout save and load). Missing: every `DockBuilder*` call (21 uses, the editor's default layout), docking nodes as an API, multi-viewports (OS windows)."),
    dict(name="Colour editors and pickers", calls=r"ColorEdit\w*|ColorPicker\w*|ColorConvert\w*",
         types=("ColorEditFlags",), needs="ColorEdit3 ColorEdit4 ColorPicker3 ColorPicker4 ColorConvertHSVtoRGB", proof="okayui/tests/test_okayui_color.cpp", status="Done",
         note="HSV input mode, float display mode and the wheel picker are not there (not used)."),
    dict(name="Plots", calls=r"Plot\w+", types=(), needs="PlotLines PlotHistogram", proof="okayui/tests/test_okayui_widgets.cpp", status="Done", note=""),
    dict(name="Tooltips", calls=r"SetTooltip|SetItemTooltip|BeginTooltip|EndTooltip|BeginItemTooltip", types=(),
         needs="SetTooltip BeginTooltip EndTooltip BeginItemTooltip SetItemTooltip", proof="okayui/tests/test_okayui_gaps.cpp okayui/tests/test_okayui_values.cpp", status="Done",
         note="A tooltip made of widgets lays out off screen on its first frame and shows from the second."),
    dict(name="Style and colours", calls=r"PushStyleColor|PopStyleColor|PushStyleVar|PopStyleVar|GetColorU32|GetStyle\w*|StyleColors\w+|PushItemFlag|PopItemFlag",
         types=("ImVec4", "ImGuiCol", "ImGuiStyleVar", "ImGuiStyle"), needs="PushStyleColor PushStyleVar GetColorU32 GetStyle GetStyleColorVec4 Vec4",
         proof="okayui/tests/test_okayui_color.cpp okayui/tests/test_okayui_values.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="`GetStyle()` returns a read-only copy of the numbers a port reads (spacing, padding, scrollbar size), not the whole `ImGuiStyle` (no `Colors[]`). `StyleColorsDark/Light/Classic` (6 uses) and `PushItemFlag` (2) are missing; several `StyleVar`s are accepted and ignored."),
    dict(name="Mouse, keys, IO", calls=r"GetIO|IsKey\w+|IsMouse\w+|GetMouse\w+|ResetMouseDragDelta|SetMouseCursor|Shortcut|SetKeyOwner|SetNextFrameWantCapture\w+|GetTime|GetFrameCount",
         types=("ImGuiKey", "ImGuiMouseButton", "ImGuiMouseCursor", "ImGuiIO", "ImGuiModFlags", "ImGuiKeyChord"), needs="GetIO IsKeyPressed IsMouseClicked GetMouseDragDelta MouseButton_Middle SetMouseCursor KeyId_UpArrow",
         proof="okayui/tests/test_okayui_values.cpp okayui/tests/test_okayui_gaps.cpp", status="Partial",
         note="The middle button, drag deltas and key aliases are in. Missing: `Shortcut` and key chords (4 uses), key ownership (`SetKeyOwner`), `ImGuiKey` values beyond the 70 named `KeyId`s (the mouse wheel as a key, keypad keys)."),
    dict(name="Fonts and icons", calls=r"PushFont|PopFont|GetFont|SetWindowFontScale|GetFontTexUvWhitePixel",
         types=("ImFont", "ImFontConfig", "ImFontAtlas", "ImWchar"), needs="SetFont GetFont SetWindowFontScale",
         proof="okayui/tests/test_okayui_windows.cpp", status="Partial",
         note="`SetFont` swaps between OkayUI's fonts (the 8x8 and the launcher's Roboto); an arbitrary font (a code font, an icon font merged into the text font) cannot be loaded or pushed. `ImFont*` has 22 uses, the icon font setup in `launcher/main.cpp` and `editor/main.cpp` has no OkayUI counterpart yet."),
    dict(name="Clipboard", calls=r"SetClipboardText|GetClipboardText", types=(), needs="SetClipboardText GetClipboardText",
         proof="okayui/tests/test_okayui_behaviour.cpp okayui/tests/test_okayui_codeedit.cpp", status="Done", note="Limited to 4095 bytes."),
    dict(name="IDs", calls=r"PushID|PopID|GetID", types=("ImGuiID",), needs="PushID PopID GetID", proof="okayui/tests/test_okayui_compat.cpp", status="Done", note="`ImGuiID` is `unsigned` here."),
    dict(name="Disabled blocks", calls=r"BeginDisabled|EndDisabled", types=(), needs="BeginDisabled EndDisabled", proof="okayui/tests/test_okayui_drawlist.cpp", status="Done", note=""),
    dict(name="Draw list (custom drawing)", calls=r"GetWindowDrawList|GetForegroundDrawList|PushClipRect|PopClipRect|GetBackgroundDrawList",
         types=("ImDrawList", "ImDrawFlags", "ImU32", "ImRect", "ImVec2"), needs="GetWindowDrawList AddRectFilled AddRectFlags AddNgon AddImageRounded AddImageQuad DrawFlags_RoundCornersTop",
         proof="okayui/tests/test_okayui_drawlist.cpp okayui/tests/test_okayui_gaps.cpp", status="Done",
         note="Rounded corners by flag, polygons and textured quads are in. No anti-aliasing and no concave fill (`AddConcavePolyFilled`, not used). `ImRect` (29 uses) is an ImGui internal type."),
    dict(name="Context, frame and host", calls=r"CreateContext|DestroyContext|GetCurrentContext|SetCurrentContext|NewFrame|Render|GetDrawData|EndFrame|StyleColors\w*|ClearActiveID|GetActiveID|GetStateStorage|FindRenderedTextEnd",
         types=("ImGuiContext",), needs="BeginFrame EndFrame GetDrawData",
         proof="okayui/src/OkayUI.cpp shared/OkayUIHost.hpp", status="Partial",
         note="Not call-for-call: the host owns the frame (`OkayUIHost`: BeginFrame, EndFrame, draw data to a backend). Code that creates ImGui contexts (the launcher's second context for a popup, `VsGraphShot`) has to be reshaped, not ported."),
]
FAMILY_OF = {}


def compile_families():
    # priority for names two regexes match: the dock calls (SetNextWindowDockID, GetWindowDockNode) belong to docking, not to windows
    names_ = [f["name"] for f in FAMILIES]
    d = FAMILIES.pop(names_.index("Docking and viewports"))
    FAMILIES.insert([f["name"] for f in FAMILIES].index("Windows, scrolling, focus"), d)
    for f in FAMILIES:
        f["_re"] = re.compile(r"^(?:%s)$" % f["calls"]) if f["calls"] else None
        f["needs_l"] = f["needs"].split()
        f["proof_l"] = f["proof"].split()


def family_for_call(name):
    # more specific families come before the broad ones where a name matches two regexes: Draw list names before layout etc. The list order is
    # the priority, except that the broad `Begin|End` window rule and `Text\w*` must not swallow Table / Tab / Tree / Tooltip / Popup names.
    for f in FAMILIES:
        if f["_re"] and f["_re"].match(name):
            return f
    return None


def family_for_type(name):
    for f in FAMILIES:
        for pre in f["types"]:
            if pre.startswith("Im") and name.startswith(pre):
                return f
            if not pre.startswith("Im") and (name.startswith("ImGui" + pre) or name.startswith("Im" + pre)):
                return f
    return None


def declared(funcs, idents, n):
    return n in funcs or n in idents


def main():
    compile_families()
    funcs, idents, dlmethods, calls, types, dls, units, per_file = base.scan()
    other = dict(name="(not in a family)", calls="", types=(), needs="", proof="", status="n/a", note="", _re=None, needs_l=[], proof_l=[])
    rows = {f["name"]: dict(f=f, sites=0, ed=0, la=0, sh=0, distinct=set(), miss_sites=0, miss_names=defaultdict(int), tmiss=defaultdict(int), tmiss_n=0, tsites=0)
            for f in FAMILIES + [other]}
    for n, (cnt, per) in calls.items():
        f = family_for_call(n) or other
        r = rows[f["name"]]
        r["sites"] += cnt
        r["distinct"].add(n)
        for path, k in per.items():
            if path.startswith("editor/"):
                r["ed"] += k
            elif path.startswith("launcher/"):
                r["la"] += k
            else:
                r["sh"] += k
        if not base.call_ok(n, funcs):
            r["miss_sites"] += cnt
            r["miss_names"][n] += cnt
    for n, (cnt, per) in dls.items():
        f = rows["Draw list (custom drawing)"]
        f["sites"] += cnt
        for path, k in per.items():
            if path.startswith("editor/"):
                f["ed"] += k
            elif path.startswith("launcher/"):
                f["la"] += k
            else:
                f["sh"] += k
        if n not in dlmethods:
            f["miss_sites"] += cnt
            f["miss_names"]["dl->" + n] += cnt
    type_missing_all = {}
    for n, (cnt, per) in types.items():
        f = family_for_type(n) or other
        r = rows[f["name"]]
        r["tsites"] += cnt
        if not base.type_covered(n, idents):
            r["tmiss"][n] += cnt
            r["tmiss_n"] += cnt
            type_missing_all[n] = (cnt, per, f["name"])

    problems = []
    for f in FAMILIES:
        absent = [x for x in f["needs_l"] if not declared(funcs, idents, x) and not any(x in open(os.path.join(base.API_DIR, h), encoding="utf-8").read() for h in os.listdir(base.API_DIR) if h.endswith(".hpp"))]
        gone = [p for p in f["proof_l"] if not os.path.exists(os.path.join(ROOT, p))]
        f["absent"], f["gone"] = absent, gone
        if absent or gone:
            problems.append("%s: absent names %s, missing proof files %s" % (f["name"], absent, gone))
    # IME mentions in the sources the scan covers
    ime = 0
    ime_files = defaultdict(int)
    for path in base.source_files():
        t = base.strip_comments_and_strings(open(path, encoding="utf-8", errors="replace").read())
        k = len(re.findall(r"\b(?:Ime\w*|WantTextInput|SetPlatformImeData|InputQueueCharacters|ImGuiInputTextCallbackData)\b", t))
        if k:
            ime += k
            ime_files[os.path.relpath(path, ROOT).replace(os.sep, "/")] = k

    def status_of(f):
        if f["absent"] or f["gone"]:
            return "Missing (unproven)"
        return f["status"]

    total_calls = sum(v[0] for v in calls.values())
    la_total = sum(r["la"] for r in rows.values())
    ed_total = sum(r["ed"] for r in rows.values())
    sh_total = sum(r["sh"] for r in rows.values())

    L = []
    w = L.append
    w("# OkayUI gap report: what is left before the launcher and the editor can run without Dear ImGui")
    w("")
    w("Generated by `python3 scripts/okayui_gap_report.py`; do not edit by hand (`--check` fails when this file is stale). It uses the scan of")
    w("`scripts/imgui_usage_report.py` (every `ImGui::` call, ImGui type, flag and enum name and draw-list method in `editor/`, `launcher/` and")
    w("`shared/`; comments, strings and the vendored code editor are skipped) and groups the result by feature family.")
    w("")
    w("How to read it. **Call sites** are measured. **Names missing** counts call sites whose function name has no OkayUI function of that name")
    w("(argument types differ, see `docs/own_ui_toolkit.md`). **Flags missing** counts uses of ImGui flag, type and enum names with no OkayUI counterpart.")
    w("**Status** is a judgement: Done means every call the code makes in that family has a working OkayUI equivalent that a headless test exercises;")
    w("Partial means the main calls work and the gap column says what does not; Missing means nothing usable. The script checks that every OkayUI")
    w("name a family relies on is declared and every proof file exists, and says \"Missing (unproven)\" otherwise. Nothing here was run in a real window.")
    w("")
    w("## Totals")
    w("")
    w("| What | Value |")
    w("|---|---:|")
    w("| `ImGui::` call sites | %d (editor %d, launcher %d, shared %d) |" % (total_calls, ed_total, la_total, sh_total))
    mc = sum(r["miss_sites"] for r in rows.values())
    w("| Call sites whose name has no OkayUI function | %d (%.1f%%) |" % (mc, 100.0 * mc / total_calls if total_calls else 0))
    ts = sum(v[0] for v in types.values())
    tm = sum(r["tmiss_n"] for r in rows.values())
    w("| ImGui flag / type / enum uses | %d |" % ts)
    w("| ... of which no OkayUI counterpart | %d (%.1f%%) |" % (tm, 100.0 * tm / ts if ts else 0))
    w("| Families Done / Partial / Missing | %d / %d / %d |" % tuple(sum(1 for f in FAMILIES if status_of(f) == s) for s in ("Done", "Partial", "Missing (unproven)")))
    w("| IME mentions in the scanned sources (`Ime*`, `WantTextInput`, `SetPlatformImeData`) | %d |" % ime)
    w("")
    w("## By family")
    w("")
    w("| Family | Call sites | Launcher | Names missing | Flags missing | Status | Proof (file) |")
    w("|---|---:|---:|---:|---:|---|---|")
    for f in FAMILIES:
        r = rows[f["name"]]
        w("| %s | %d | %d | %d | %d | %s | %s |" % (f["name"], r["sites"], r["la"], r["miss_sites"], r["tmiss_n"], status_of(f), ", ".join("`%s`" % p for p in f["proof_l"])))
    r = rows["(not in a family)"]
    if r["sites"]:
        w("| (not in a family) | %d | %d | %d | %d | n/a | |" % (r["sites"], r["la"], r["miss_sites"], r["tmiss_n"]))
    w("")
    w("`Launcher` is the number of call sites in `launcher/` (`main.cpp`, `VersionsUI.cpp`); the file dialog in `shared/` is used by both programs.")
    w("")

    w("## Missing families ranked by what is still uncovered")
    w("")
    w("Ranked by call sites without an OkayUI function plus flag uses without a counterpart. Only families that are not Done are listed, with what is missing.")
    w("")
    w("| # | Family | Uncovered | Launcher | What is missing |")
    w("|---:|---|---:|---:|---|")
    rank = []
    for f in FAMILIES:
        r = rows[f["name"]]
        unc = r["miss_sites"] + r["tmiss_n"]
        if status_of(f) != "Done" or unc:
            rank.append((unc, f))
    rank.sort(key=lambda t: (-t[0], t[1]["name"]))
    for i, (unc, f) in enumerate(rank, 1):
        r = rows[f["name"]]
        w("| %d | %s | %d | %d | %s |" % (i, f["name"], unc, launcher_uncovered(f, r, calls, types, funcs, idents, per_file), f["note"] or "-"))
    w("")

    w("## What the launcher still needs (launcher/ only)")
    w("")
    la_calls = defaultdict(int)
    la_types = defaultdict(int)
    for path, d in per_file.items():
        if path.startswith("launcher/"):
            for n, k in d["calls"].items():
                la_calls[n] += k
            for n, k in d["types"].items():
                la_types[n] += k
    miss_c = sorted(((k, n) for n, k in la_calls.items() if not base.call_ok(n, funcs)), reverse=True)
    miss_t = sorted(((k, n) for n, k in la_types.items() if not base.type_covered(n, idents)), reverse=True)
    w("Calls with no OkayUI function (%d call sites in %d names):" % (sum(k for k, _ in miss_c), len(miss_c)))
    w("")
    for k, n in miss_c:
        f = family_for_call(n)
        w("- `%s` x%d (%s)" % (n, k, f["name"] if f else "internal / host"))
    w("")
    w("Flags and types with no OkayUI counterpart (%d uses in %d names):" % (sum(k for k, _ in miss_t), len(miss_t)))
    w("")
    for k, n in miss_t:
        w("- `%s` x%d" % (n, k))
    w("")
    w("These are by name. The context calls (`CreateContext`, `NewFrame`, ...) are host code, not widgets: the launcher makes a second ImGui context for one")
    w("popup and a screenshot path, and that code has to be reshaped when the launcher moves. See `docs/UNFINISHED.md` for the estimate.")
    w("")

    w("## Names still missing, by family")
    w("")
    for f in FAMILIES:
        r = rows[f["name"]]
        if not r["miss_names"] and not r["tmiss"]:
            continue
        w("**%s** (%s)" % (f["name"], status_of(f)))
        if r["miss_names"]:
            w("- calls: " + ", ".join("`%s` (%d)" % (n, k) for n, k in sorted(r["miss_names"].items(), key=lambda kv: (-kv[1], kv[0]))))
        if r["tmiss"]:
            w("- flags and types: " + ", ".join("`%s` (%d)" % (n, k) for n, k in sorted(r["tmiss"].items(), key=lambda kv: (-kv[1], kv[0]))[:12]) + (", ..." if len(r["tmiss"]) > 12 else ""))
        w("")
    r = rows["(not in a family)"]
    if r["miss_names"]:
        w("**(not in a family)**: " + ", ".join("`%s` (%d)" % (n, k) for n, k in sorted(r["miss_names"].items(), key=lambda kv: (-kv[1], kv[0]))))
        w("")

    w("## IME")
    w("")
    w("The scanned code mentions IME %d times: %s. OkayUI has the API (`Input::composition`, `Input::commit`, `GetImeRect`, text fields and `CodeEdit` show the" % (
        ime, ", ".join("`%s` (%d)" % kv for kv in sorted(ime_files.items(), key=lambda kv: -kv[1])[:5]) or "none"))
    w("preedit and report the caret rectangle; `okayui/tests/test_okayui_compose.cpp`). What is missing is the host side: nothing forwards the operating system's composition")
    w("events (SDL text editing events) into `Input`, and nothing places the system candidate window at `GetImeRect()`. Status: Partial, headless only.")
    w("")
    w("## CodeEdit against ImGuiColorTextEdit")
    w("")
    w("See `docs/own_ui_code_editor.md` (HAVE and GAP table). New in this change: overwrite mode (Insert), Ctrl+Insert, Shift+Insert, Shift+Delete, Alt+Backspace and")
    w("show-whitespace, tested in `okayui/tests/test_okayui_gaps.cpp`. Still missing: code folding, multiple cursors, word wrap (none of them in ImGuiColorTextEdit either), IME")
    w("through a real window, a code font.")
    w("")
    if problems:
        w("## Problems the script found")
        w("")
        for p in problems:
            w("- " + p)
        w("")
    text = "\n".join(L)
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if cur != text + "\n":
            print("docs/okayui_gap_report.md is stale: run python3 scripts/okayui_gap_report.py")
            return 1
        print("docs/okayui_gap_report.md is current")
        return 0
    open(OUT, "w", encoding="utf-8").write(text + "\n")
    print("wrote %s: %d call sites, %d without an OkayUI function name, %d problems" % (OUT, total_calls, mc, len(problems)))
    for p in problems:
        print("  problem:", p)
    return 0


def launcher_uncovered(f, r, calls, types, funcs, idents, per_file):
    n = 0
    for path, d in per_file.items():
        if not path.startswith("launcher/"):
            continue
        for name, k in d["calls"].items():
            if family_for_call(name) is f and not base.call_ok(name, funcs):
                n += k
        for name, k in d["types"].items():
            if family_for_type(name) is f and not base.type_covered(name, idents):
                n += k
    return n


if __name__ == "__main__":
    sys.exit(main())
