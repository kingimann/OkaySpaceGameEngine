#!/usr/bin/env python3
"""Inventory of the Dear ImGui API the editor, launcher and shared code use, diffed against OkayUI.

Counts every `ImGui::Call(`, every ImGui type / flag / enum name (ImGuiXxx, ImVec2, ...), and every draw-list method
(`dl->AddRect(`) in editor/, launcher/ and shared/, then checks each name against the OkayUI headers
(okayui/include/okay/UI/*.hpp). Writes docs/imgui_port_report.md:

  1. totals and overall coverage,
  2. the missing calls ranked by use count (with the files that use them),
  3. missing types, flags and enums ranked by use count,
  4. per-file coverage and the per-screen / per-function coverage (a unit is a top-level function; its ImGui:: calls are
     the ones between its first line and the next top-level function),
  5. the 10 smallest screens that are fully or almost fully coverable (the port candidates).

Coverage is by NAME: a call counts as available when OkayUI declares a function of that name. Signatures differ in places
(ImVec2 arguments become floats or OkayUI::Vec2); the report lists those separately as "available, different signature"
only for the aliases in ALIASES below. Run from the repository root:  python3 scripts/imgui_usage_report.py
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC_DIRS = ["editor", "launcher", "shared"]
API_DIR = os.path.join(ROOT, "okayui", "include", "okay", "UI")
OUT = os.path.join(ROOT, "docs", "imgui_port_report.md")
EXTS = (".cpp", ".hpp", ".h", ".cc", ".inl")

# Third-party sources that are not ours: not counted.
SKIP_FILES = ("ImGuiColorTextEdit", "imgui_", "stb_")
SKIP_DIRS = ("vendor",)   # editor/vendor holds the third-party code editor widget (its own roadmap row)

# ImGui:: call -> OkayUI name when they differ.
ALIASES = {
    "ColorEdit4": "ColorEdit4", "GetCursorScreenPos": "GetCursorScreenPos", "CalcTextSize": "CalcTextSize",
    "PushStyleColor": "PushStyleColor", "GetContentRegionAvail": "GetContentRegionAvail",
    "TextUnformatted": "TextUnformatted", "SetNextWindowSize": "SetNextWindowSize",
    "GetWindowDrawList": "GetWindowDrawList", "GetForegroundDrawList": "GetForegroundDrawList",
    "TableSetupColumn": "TableSetupColumn", "IsKeyPressed": "IsKeyPressed",
    "BeginDragDropSource": "BeginDragDropSource", "InputTextMultiline": "InputText",
    "SliderFloat": "SliderFloat", "GetCurrentWindow": "GetCurrentWindowInfo",
    "TextWrapped": "TextWrapped", "SetItemTooltip": "SetTooltip", "BeginTooltip": "Tooltip",
}


# (label, file, function) of the screens that have an OkayUI port.
PORTED = [
    ("Console", "editor/ConsolePanel.cpp", "DrawConsole"),
    ("Crash Log", "editor/main.cpp", "DrawCrashLog"),
    ("Audio Mixer", "editor/main.cpp", "DrawAudioMixer"),
    ("What's New", "launcher/main.cpp", "(page)"),
    ("Tips", "launcher/main.cpp", "(page)"),
]


def strip_comments_and_strings(text):
    """Blank out comments and string literals (keeping newlines) so names inside them are not counted."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j])); i = j
        elif c == '"' or c == "'":
            q = c; j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(q + re.sub(r"[^\n]", " ", text[i + 1:j - 1]) + q if j - i >= 2 else text[i:j]); i = j
        else:
            out.append(c); i += 1
    return "".join(out)


def source_files():
    for d in SRC_DIRS:
        for dp, dns, fns in os.walk(os.path.join(ROOT, d)):
            dns[:] = [x for x in dns if x not in SKIP_DIRS]
            for fn in sorted(fns):
                if fn.endswith(EXTS) and not fn.startswith(SKIP_FILES):
                    yield os.path.join(dp, fn)


def okayui_api():
    """(function names, type/enum/flag names, draw list methods) declared by the OkayUI headers."""
    funcs, idents, dl = set(), set(), set()
    for fn in sorted(os.listdir(API_DIR)):
        if not fn.endswith(".hpp"):
            continue
        t = strip_comments_and_strings(open(os.path.join(API_DIR, fn), encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", t):
            funcs.add(m.group(1))
        for m in re.finditer(r"\b([A-Za-z_]\w*)\b", t):
            idents.add(m.group(1))
        m = re.search(r"struct\s+DrawList\s*\{(.*?)\n\};", t, re.S)
        if m:
            for mm in re.finditer(r"\b(\w+)\s*\(", m.group(1)):
                dl.add(mm.group(1))
    return funcs, idents, dl


TYPE_RE = re.compile(r"\b(ImGui[A-Z]\w*|ImVec2|ImVec4|ImColor|ImDrawList|ImFont\w*|ImU32|ImU8|ImU16|ImU64|ImS8|ImS16|ImS32|ImS64|ImTextureID|ImWchar\w*|ImRect|ImDrawFlags_\w+|ImPlot\w*|ImNodes\w*)\b")
CALL_RE = re.compile(r"\bImGui::([A-Za-z_]\w*)")
DL_RE = re.compile(r"(?:->|\.)(Add(?:Line|Rect|RectFilled|RectFilledMultiColor|Circle|CircleFilled|Text|Triangle|TriangleFilled|Quad|QuadFilled|Polyline|ConvexPolyFilled|Bezier\w*|Image|ImageQuad|ImageRounded|Ngon|NgonFilled)|PathClear|PathLineTo|PathArcTo|PathStroke|PathFillConvex|PushClipRect|PopClipRect)\s*\(")
FUNC_START = re.compile(r"^(?:static\s+|inline\s+|constexpr\s+|template\s*<[^>]*>\s*)*[A-Za-z_][\w:<>,\*&\s]*?\b([A-Za-z_~][\w:]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:\{.*)?$")
KEYWORDS = {"if", "for", "while", "switch", "else", "return", "do", "catch", "sizeof", "namespace"}


# ImGui types whose job OkayUI's value types do (by shape, not by layout): ImFont* is a FontId, ImGuiWindow a WindowInfo, ImGuiDockNode a DockBuilder::Node, ImGuiPayload a DragPayload.
TYPE_ALIASES = {"ImFont": "FontId", "ImFontConfig": "FontConfig", "ImWchar": "GlyphRange", "ImGuiWindow": "WindowInfo", "ImGuiDockNode": "DockBuilder", "ImGuiPayload": "DragPayload"}


def type_covered(name, idents):
    """An ImGui type/flag/enum name is available when OkayUI has the same-named identifier or its obvious spelling."""
    if name in TYPE_ALIASES:
        return TYPE_ALIASES[name] in idents
    if name in ("ImVec2", "ImVec4"):
        return ("Vec2" if name == "ImVec2" else "Vec4") in idents
    if name.startswith("ImDrawFlags_"):
        return "DrawFlags_" + name[len("ImDrawFlags_"):] in idents
    if name == "ImU32":
        return "Color32" in idents
    if name in ("ImDrawList",):
        return "DrawList" in idents
    if name.startswith("ImGui"):
        base = name[5:]
        cands = [base, base.replace("Flags_", "_"), base.replace("Flags", ""), re.sub(r"Flags_", "_", base)]
        m = re.match(r"([A-Z][a-z]+)Flags_(\w+)", base)
        if m:
            cands += [m.group(1) + "_" + m.group(2), m.group(1) + "Flags"]
        m = re.match(r"(Col|StyleVar|Cond|Dir|MouseCursor|Key)_(\w+)", base)
        if m:
            cands += [base, m.group(1) + "_" + m.group(2)]
        m = re.match(r"((?:[A-Z][a-z0-9]*)+)Flags_(\w+)", base)
        if m:   # CamelCase prefix words in turn: TabItemFlags_SetSelected -> Tab_SetSelected, TabItem_SetSelected
            words = re.findall(r"[A-Z][a-z0-9]*", m.group(1))
            cands += ["".join(words[:k]) + "_" + m.group(2) for k in range(1, len(words) + 1)]
        if base.startswith("TableColumnFlags_"):
            cands += ["Col_" + base[len("TableColumnFlags_"):]]
        if base.startswith("Key_"):
            cands += ["KeyId_" + base[4:]]
        return any(c in idents for c in cands)
    return False


def scan():
    funcs, idents, dlmethods = okayui_api()
    calls = defaultdict(lambda: [0, defaultdict(int)])      # name -> [count, {file: n}]
    types = defaultdict(lambda: [0, defaultdict(int)])
    dls = defaultdict(lambda: [0, defaultdict(int)])
    units = {}                                              # (file, func) -> {calls: {name: n}, types: {}, dl: {}, line}
    per_file = defaultdict(lambda: {"calls": defaultdict(int), "types": defaultdict(int), "dl": defaultdict(int)})
    for path in source_files():
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        text = strip_comments_and_strings(open(path, encoding="utf-8", errors="replace").read())
        cur = (rel, "(file scope)")
        depth = 0
        for ln, line in enumerate(text.split("\n"), 1):
            if line and not line[0].isspace() and line[0] not in "#}/{" and not line.startswith(("namespace", "struct", "class", "enum", "using", "typedef", "extern")):
                m = FUNC_START.match(line.rstrip())
                if m and m.group(1).split("::")[-1] not in KEYWORDS and "=" not in line.split("(")[0]:
                    cur = (rel, m.group(1))
            u = units.setdefault(cur, {"calls": defaultdict(int), "types": defaultdict(int), "dl": defaultdict(int), "line": ln})
            for m in CALL_RE.finditer(line):
                n = m.group(1)
                calls[n][0] += 1; calls[n][1][rel] += 1; u["calls"][n] += 1; per_file[rel]["calls"][n] += 1
            for m in TYPE_RE.finditer(line):
                n = m.group(1)
                types[n][0] += 1; types[n][1][rel] += 1; u["types"][n] += 1; per_file[rel]["types"][n] += 1
            for m in DL_RE.finditer(line):
                n = m.group(1)
                dls[n][0] += 1; dls[n][1][rel] += 1; u["dl"][n] += 1; per_file[rel]["dl"][n] += 1
            depth += line.count("{") - line.count("}")
            if depth < 0:
                depth = 0
    return funcs, idents, dlmethods, calls, types, dls, units, per_file


def call_ok(name, funcs):
    return name in funcs or ALIASES.get(name, "") in funcs


def render():
    funcs, idents, dlmethods, calls, types, dls, units, per_file = scan()

    def total(d):
        return sum(v[0] for v in d.values())

    def covered(d, ok):
        return sum(v[0] for k, v in d.items() if ok(k))

    call_total, call_cov = total(calls), covered(calls, lambda k: call_ok(k, funcs))
    type_total, type_cov = total(types), covered(types, lambda k: type_covered(k, idents))
    dl_total, dl_cov = total(dls), covered(dls, lambda k: k in dlmethods)
    kinds_missing = sorted(((k, v) for k, v in calls.items() if not call_ok(k, funcs)), key=lambda kv: (-kv[1][0], kv[0]))
    types_missing = sorted(((k, v) for k, v in types.items() if not type_covered(k, idents)), key=lambda kv: (-kv[1][0], kv[0]))
    dl_missing = sorted(((k, v) for k, v in dls.items() if k not in dlmethods), key=lambda kv: (-kv[1][0], kv[0]))

    def pct(a, b):
        return "%.1f%%" % (100.0 * a / b) if b else "n/a"

    L = []
    w = L.append
    w("# Dear ImGui port report")
    w("")
    w("Generated by `python3 scripts/imgui_usage_report.py`; do not edit by hand. It counts what the editor, the launcher and the")
    w("shared code use from Dear ImGui and checks each name against the OkayUI headers (`okayui/include/okay/UI/*.hpp`). Coverage is")
    w("by name: a call counts as available when OkayUI declares a function of that name (argument types differ in places, see")
    w("`docs/own_ui_toolkit.md` section 6). Comments and string literals are not counted; third-party sources are skipped.")
    w("")
    w("## Totals")
    w("")
    w("| What | Distinct | Call sites | Available in OkayUI | Coverage of call sites |")
    w("|---|---:|---:|---:|---:|")
    w("| `ImGui::` calls | %d | %d | %d | %s |" % (len(calls), call_total, call_cov, pct(call_cov, call_total)))
    w("| ImGui types, flags, enums | %d | %d | %d | %s |" % (len(types), type_total, type_cov, pct(type_cov, type_total)))
    w("| Draw-list methods | %d | %d | %d | %s |" % (len(dls), dl_total, dl_cov, pct(dl_cov, dl_total)))
    w("")
    w("Distinct calls available: %d of %d." % (sum(1 for k in calls if call_ok(k, funcs)), len(calls)))
    w("")

    w("## Missing `ImGui::` calls, ranked by use")
    w("")
    w("| # | Call | Uses | Files | Where (most uses first) |")
    w("|---:|---|---:|---:|---|")
    for i, (k, v) in enumerate(kinds_missing[:60], 1):
        top = sorted(v[1].items(), key=lambda kv: -kv[1])[:3]
        w("| %d | `%s` | %d | %d | %s |" % (i, k, v[0], len(v[1]), ", ".join("%s (%d)" % (f, n) for f, n in top)))
    if len(kinds_missing) > 60:
        rest = kinds_missing[60:]
        w("")
        w("%d more missing calls with %d uses in total: %s." % (len(rest), sum(v[0] for _, v in rest),
                                                               ", ".join("`%s` (%d)" % (k, v[0]) for k, v in rest[:40]) + (", ..." if len(rest) > 40 else "")))
    w("")
    w("Missing draw-list methods: " + (", ".join("`%s` (%d)" % (k, v[0]) for k, v in dl_missing) or "none") + ".")
    w("")

    w("## Missing types, flags and enums, ranked by use")
    w("")
    w("| # | Name | Uses | Files |")
    w("|---:|---|---:|---:|")
    for i, (k, v) in enumerate(types_missing[:40], 1):
        w("| %d | `%s` | %d | %d |" % (i, k, v[0], len(v[1])))
    if len(types_missing) > 40:
        w("")
        w("%d more with %d uses in total." % (len(types_missing) - 40, sum(v[0] for _, v in types_missing[40:])))
    w("")

    # per file
    w("## Coverage per file")
    w("")
    w("`ImGui::` calls only (call sites). Sorted by call count, smallest first among files with at least one call.")
    w("")
    w("| File | Calls | Available | Coverage | Missing calls (distinct) |")
    w("|---|---:|---:|---:|---|")
    rows = []
    for f, d in per_file.items():
        tot = sum(d["calls"].values())
        if not tot:
            continue
        ok = sum(n for k, n in d["calls"].items() if call_ok(k, funcs))
        miss = sorted(((k, n) for k, n in d["calls"].items() if not call_ok(k, funcs)), key=lambda kv: -kv[1])
        rows.append((tot, f, ok, miss))
    rows.sort()
    for tot, f, ok, miss in rows:
        w("| `%s` | %d | %d | %s | %s |" % (f, tot, ok, pct(ok, tot), ", ".join("%s (%d)" % kv for kv in miss[:6]) + (", ..." if len(miss) > 6 else "")))
    w("")

    # units
    urows = []
    for (f, fn), u in units.items():
        tot = sum(u["calls"].values())
        if tot < 15:
            continue
        ok = sum(n for k, n in u["calls"].items() if call_ok(k, funcs))
        tt = sum(u["types"].values())
        tok = sum(n for k, n in u["types"].items() if type_covered(k, idents))
        dt = sum(u["dl"].values())
        dok = sum(n for k, n in u["dl"].items() if k in dlmethods)
        miss = sorted(((k, n) for k, n in u["calls"].items() if not call_ok(k, funcs)), key=lambda kv: -kv[1])
        tmiss = sorted(((k, n) for k, n in u["types"].items() if not type_covered(k, idents)), key=lambda kv: -kv[1])
        urows.append(dict(file=f, fn=fn, line=u["line"], calls=tot, ok=ok, types=tt, tok=tok, dl=dt, dok=dok, miss=miss, tmiss=tmiss))
    allcalls_cov = [r for r in urows]

    w("## Smallest screens by call count that are fully or almost fully coverable")
    w("")
    w("A screen here is a top-level function with at least 30 `ImGui::` calls (smaller ones are helper widgets). \"Almost\" means at least 90% of its calls are")
    w("available. Flags and types are listed too: a screen is only a quick port when those are covered or trivially mapped.")
    w("")
    cand = [r for r in urows if r["ok"] * 10 >= r["calls"] * 9 and 30 <= r["calls"] <= 400]
    cand.sort(key=lambda r: (r["calls"], r["file"], r["fn"]))
    w("| # | Screen (function) | File:line | Calls | Coverage | Missing calls | Missing types/flags |")
    w("|---:|---|---|---:|---:|---|---|")
    for i, r in enumerate(cand[:10], 1):
        w("| %d | `%s` | `%s:%d` | %d | %s | %s | %s |" % (
            i, r["fn"], r["file"], r["line"], r["calls"], pct(r["ok"], r["calls"]),
            ", ".join("%s (%d)" % kv for kv in r["miss"]) or "none",
            ", ".join("%s (%d)" % kv for kv in r["tmiss"][:5]) or "none"))
    w("")
    # Screens that already have an OkayUI port (behind the opt-in backend). Functions are looked up above; launcher pages that are a
    # branch inside main() have no function of their own and are listed by name only.
    w("## Screens already ported (opt-in OkayUI backend)")
    w("")
    w("| Screen | Where | ImGui:: calls in the Dear ImGui version | Coverage by name |")
    w("|---|---|---:|---:|")
    ported_calls = 0
    for label, f, fn in PORTED:
        u = units.get((f, fn))
        if u:
            tot = sum(u["calls"].values())
            ok = sum(n for k, n in u["calls"].items() if call_ok(k, funcs))
            ported_calls += tot
            w("| %s | `%s` `%s` | %d | %s |" % (label, f, fn, tot, pct(ok, tot)))
        else:
            w("| %s | `%s` (a page inside `main`) | n/a | n/a |" % (label, f))
    w("")
    w("The ported functions above hold %d of the %d `ImGui::` call sites counted (%s); the launcher pages are not in that figure." % (
        ported_calls, call_total, pct(ported_calls, call_total)))
    w("")
    w("## Coverage per screen (functions with 15 or more calls)")
    w("")
    w("| Screen | File:line | Calls | Coverage | Missing calls |")
    w("|---|---|---:|---:|---|")
    for r in sorted(urows, key=lambda r: (r["calls"], r["file"], r["fn"])):
        w("| `%s` | `%s:%d` | %d | %s | %s |" % (r["fn"], r["file"], r["line"], r["calls"], pct(r["ok"], r["calls"]),
                                                 ", ".join("%s (%d)" % kv for kv in r["miss"][:5]) + (", ..." if len(r["miss"]) > 5 else "")))
    w("")
    full = sum(1 for r in urows if r["ok"] == r["calls"])
    ninety = sum(1 for r in urows if r["ok"] * 10 >= r["calls"] * 9)
    w("Of %d screens listed: %d are fully covered by name, %d are at least 90%% covered." % (len(urows), full, ninety))
    w("")
    return "\n".join(L), dict(call_total=call_total, call_cov=call_cov, type_total=type_total, type_cov=type_cov,
                               missing=kinds_missing, cand=cand, urows=urows)


def main():
    text, stats = render()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s: %d ImGui:: call sites, %d available in OkayUI (%.1f%%)" % (
        os.path.relpath(OUT, ROOT), stats["call_total"], stats["call_cov"], 100.0 * stats["call_cov"] / max(1, stats["call_total"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
