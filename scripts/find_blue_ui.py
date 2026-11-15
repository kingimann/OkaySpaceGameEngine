#!/usr/bin/env python3
"""List hard-coded BLUE colour literals in the editor's UI code (file:line).

  python3 scripts/find_blue_ui.py [paths...] [--threshold 0.08] [--show-allowed] [--count] [--check]

Why: the owner wants no blue anywhere in the editor chrome (2026-11-15). The editor has exactly two looks, Dark and
Light, built from the neutral palettes in editor/ThemeModel.hpp, and chrome colours must come from the central
helpers (oked::AccentCol / AccentU32 / TextCol / DimCol / SurfaceCol, the ImGui style colours), not from a literal. This script finds the literals that
still lean blue so they can be fixed or consciously kept.

A colour literal counts as blue when its blue channel dominates:  b > r + threshold  AND  b > g   (channels 0..1, so
0..255 values are divided by 255). Forms scanned:
  IM_COL32(r, g, b[, a])           integers 0..255
  ImVec4(r, g, b[, a])  /  ImVec4 name(r, g, b[, a])  /  ImVec4{...}   floats 0..1
  ImColor(r, g, b[, a])            floats 0..1 or integers 0..255
  OkayUI byte tuples: RGBA(r, g, b[, a]), set(t.x, r, g, b, a), `unsigned char x[4] = {r, g, b, a}`
  col(r, g, b)                     the small colour lambdas some drawing code defines (integers 0..255)
  0xAABBGGRR hex words (ImU32 / TextEditor palettes), only when the line or the lines just above mention a colour

What is NOT a blue site (kept on purpose, by convention):
  * gizmo axis colours (X red, Y green, Z blue) and anything marked in the code with a comment `blue-ok: <reason>`
    on the same line or the line above. Marked lines are hidden unless --show-allowed is given, so a reviewer can see
    why each one stayed (the reason is printed).
  * shared/OkayTheme.hpp (the launcher's palette) and okayui/ (the game-UI look) are not editor chrome: see PATH_ALLOW.
  * the user's scene content and the game-UI theme assets (UITheme) are never scanned (they are not in these folders).

Exit code: 0 always, except with --check, which exits 1 when any unmarked blue literal remains.
Written for this repo; standard library only.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PATHS = ["editor", "okayui", "shared"]
EXTS = (".cpp", ".hpp", ".h", ".inc")
SKIP_DIRS = {"vendor", "build", ".git", "tests"}   # vendor/ is the third-party text editor (its two built-in palettes are overridden by SetPalette); tests embed golden colours
NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f?"
TUP3 = r"\s*(%s)\s*,\s*(%s)\s*,\s*(%s)\s*(?:,\s*(%s)\s*)?" % (NUM, NUM, NUM, NUM)

RE_IMCOL32 = re.compile(r"IM_COL32\(" + TUP3 + r"\)")
RE_IMVEC4 = re.compile(r"ImVec4(?:\s+\w+)?\s*[({]" + TUP3 + r"[)}]")
RE_IMCOLOR = re.compile(r"ImColor\(" + TUP3 + r"\)")
RE_RGBA = re.compile(r"\bRGBA\(" + TUP3 + r"\)")
RE_SET = re.compile(r"\bset\(\s*[\w.\[\]>-]+\s*," + TUP3 + r"\)")
RE_COLFN = re.compile(r"(?<![\w.])col\(" + TUP3 + r"\)")   # a local `auto col = [](int r, int g, int b) { return IM_COL32(r, g, b, 235); }`
RE_BYTES = re.compile(r"unsigned char\s+[\w.]+\[4\]\s*=\s*\{" + TUP3 + r"\}")
RE_HEX = re.compile(r"\b0x([0-9A-Fa-f]{8})u?\b")
HEX_CONTEXT = re.compile(r"(?i)palette|colou?r|ImU32|IM_COL32|rgba|abgr|\bcol\b")
HEX_SKIP = re.compile(r"(?i)hash|seed|crc|fnv|magic|0x7fffffff|<<|>>|& *0x|\| *0x|mask|signature")
ALLOW_RE = re.compile(r"blue-ok\s*:?\s*(.*)")
# Whole files / folders that are not editor chrome. Their literals count as "marked" with this reason.
PATH_ALLOW = [
    ("shared/OkayTheme.hpp", "the launcher's own Slate palette: the launcher keeps its look (the editor does not use these colours any more, it has editor/ThemeModel.hpp)"),
    ("okayui/", "OkayUI library defaults, its launcher / game look (UseLauncherLook) and demo scenes: the game-UI theme, not editor chrome; hosted editor panels draw inside OkayUILookScope (editor/OkayPanels.cpp) with the editor's Dark / Light colours"),
]


def to_float(tok):
    return float(tok.rstrip("f"))


def rgb01(vals, kind):
    r, g, b = (to_float(v) for v in vals[:3])
    if kind in ("IM_COL32", "RGBA", "set", "bytes", "col()"):
        r, g, b = r / 255.0, g / 255.0, b / 255.0
    elif kind == "ImColor":   # ints (>1) are bytes, otherwise floats
        if max(r, g, b) > 1.0:
            r, g, b = r / 255.0, g / 255.0, b / 255.0
    return r, g, b


def classify(rgb, thr):
    """'blue' (blue channel dominates), 'tint' (a grey with a blue cast) or None."""
    r, g, b = rgb
    if b > r + thr and b > g:
        return "blue"
    if max(rgb) - min(rgb) <= 0.16 and b > r + 0.02 and b >= g:
        return "tint"
    return None


def is_blue(rgb, thr):
    return classify(rgb, thr) is not None


def strip_comment(line):
    i = line.find("//")
    return line if i < 0 else line[:i]


def scan_file(path, thr):
    hits = []
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
    except OSError:
        return hits
    in_block = False
    for n, raw in enumerate(lines, 1):
        code = strip_comment(raw)
        if in_block:
            if "*/" in code:
                code = code.split("*/", 1)[1]
                in_block = False
            else:
                continue
        while "/*" in code:
            a, _, rest = code.partition("/*")
            if "*/" in rest:
                code = a + rest.split("*/", 1)[1]
            else:
                code = a
                in_block = True
        found = []
        for kind, rx in (("IM_COL32", RE_IMCOL32), ("ImVec4", RE_IMVEC4), ("ImColor", RE_IMCOLOR),
                         ("RGBA", RE_RGBA), ("set", RE_SET), ("bytes", RE_BYTES), ("col()", RE_COLFN)):
            for m in rx.finditer(code):
                try:
                    rgb = rgb01(m.groups()[:3], kind)
                except ValueError:
                    continue
                cls = classify(rgb, thr)
                if cls:
                    found.append((kind if cls == "blue" else kind + "/tint", m.group(0).strip(), rgb))
        ctx = " ".join(lines[max(0, n - 8):n])
        if RE_HEX.search(code) and HEX_CONTEXT.search(ctx) and not HEX_SKIP.search(code):
            for m in RE_HEX.finditer(code):
                v = int(m.group(1), 16)
                r, g, b = (v & 255) / 255.0, ((v >> 8) & 255) / 255.0, ((v >> 16) & 255) / 255.0
                cls = classify((r, g, b), thr)
                if cls:
                    found.append(("hex" if cls == "blue" else "hex/tint", m.group(0), (r, g, b)))
        if not found:
            continue
        reason = None
        for look in (raw, lines[n - 2] if n >= 2 else ""):
            am = ALLOW_RE.search(look)
            if am and "//" in look:
                reason = am.group(1).strip() or "(no reason given)"
                break
        for kind, text, rgb in found:
            hits.append((n, kind, text, rgb, reason))
    return hits


def walk(paths):
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if os.path.isfile(full):
            yield full
            continue
        for d, dirs, files in os.walk(full):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for fn in sorted(files):
                if fn.endswith(EXTS):
                    yield os.path.join(d, fn)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", default=DEFAULT_PATHS)
    ap.add_argument("--threshold", type=float, default=0.08, help="b must exceed r by this much (default 0.08)")
    ap.add_argument("--show-allowed", action="store_true", help="also list literals marked `blue-ok: reason`")
    ap.add_argument("--count", action="store_true", help="print only the totals")
    ap.add_argument("--check", action="store_true", help="exit 1 when an unmarked blue literal remains")
    a = ap.parse_args()
    open_n = allowed_n = 0
    per_file = {}
    for path in walk(a.paths):
        rel = os.path.relpath(path, ROOT)
        path_reason = next((why for prefix, why in PATH_ALLOW if rel.replace(os.sep, "/").startswith(prefix)), None)
        for n, kind, text, rgb, reason in scan_file(path, a.threshold):
            if reason is None and path_reason is not None:
                reason = path_reason
            if reason is None:
                open_n += 1
                per_file[rel] = per_file.get(rel, 0) + 1
                if not a.count:
                    print("%s:%d: %s  rgb=(%.2f, %.2f, %.2f)  %s" % (rel, n, kind, rgb[0], rgb[1], rgb[2], text))
            else:
                allowed_n += 1
                if a.show_allowed and not a.count:
                    print("%s:%d: allowed: %s  rgb=(%.2f, %.2f, %.2f)  %s  [%s]" % (rel, n, kind, rgb[0], rgb[1], rgb[2], text, reason))
    if a.count:
        for rel, c in sorted(per_file.items(), key=lambda kv: -kv[1]):
            print("%5d  %s" % (c, rel))
    print("blue literals: %d unmarked, %d marked blue-ok (blue: b > r + %.2f and b > g; tint: grey with b > r + 0.02)" % (open_n, allowed_n, a.threshold))
    return 1 if (a.check and open_n) else 0


if __name__ == "__main__":
    sys.exit(main())
