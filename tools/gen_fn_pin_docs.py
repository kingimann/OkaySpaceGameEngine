#!/usr/bin/env python3
"""Generate engine/include/okay/VisualScript/VsBuiltinDocs.gen.hpp: for each text-script builtin,
the names of its arguments (in order), a one-line description and what it gives back.

The Visual Script editor uses the table to name the inputs of each `Fn.<builtin>` node (instead of
First / Second / Third ...) and to write its tooltip (#888 finding 34, part C).

Where the names come from (first source that has a usable signature wins):
  1. docs/scripting_reference.md   table rows   | `name(a, b[, c])` | what it does |
  2. docs/scripting.md             table rows   (same format)
  3. editor/main.cpp               fapi("name(a, b)", "what it does")   (Help -> Scripting Reference)
  4. engine/src/Scripting/OkayScriptVM.cpp   comments such as // name(a, b) does ...
  5. editor/main.cpp               ScriptSignatureMap {"name","name(a, b)"}  (Script Editor hints)
A builtin made with alias("New", "old") in OkayScriptVM.cpp takes the entry of "old" when it has
none of its own.

A signature is used only when every argument reads as a name (`x`, `"prefab"`, `[dist]`); a call
with example values (`raycast3(x, y, z, 0, -1, 0)`) is skipped. A signature with more arguments
than the node has inputs is skipped too (the node's input count comes from kWideBuiltins in
engine/src/VisualScript/VsBuiltins.cpp, at least four).

Usage:  python3 tools/gen_fn_pin_docs.py           write the header
        python3 tools/gen_fn_pin_docs.py --check   fail when the header is out of date
        python3 tools/gen_fn_pin_docs.py --stats   print the coverage of each source
The output depends only on the files above, so it is the same on every run.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "engine", "include", "okay", "VisualScript", "VsBuiltinDocs.gen.hpp")
VM = os.path.join(ROOT, "engine", "src", "Scripting", "OkayScriptVM.cpp")
VS_BUILTINS = os.path.join(ROOT, "engine", "src", "VisualScript", "VsBuiltins.cpp")
EDITOR_MAIN = os.path.join(ROOT, "editor", "main.cpp")
DOCS = [os.path.join(ROOT, "docs", "scripting_reference.md"), os.path.join(ROOT, "docs", "scripting.md")]
MAX_PINS = 13          # okay::vs::kBuiltinMaxPins
MIN_PINS = 4
MAX_DESC = 320

SIG = re.compile(r'(?<![\w.])([A-Za-z_][\w.]*)\(([^()]*)\)')
IDENT = re.compile(r'^[A-Za-z_]\w*$')
QUOTED = re.compile(r'^"([^"]*)"$')
# A quoted argument is a name only when it reads like one ("prefab", "fn", "Label"); "Enemy" or
# "hello world" is an example value, and the signature is skipped.
QUOTED_PLACEHOLDERS = {"Label", "Title", "Body", "Name", "Text", "Message"}
QUOTED_NAME = re.compile(r'^[a-z][a-z_]*$')


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def builtin_names(vm):
    """Every builtin the VM registers: b["name"] = ... and alias("name", "target")."""
    names = set(re.findall(r'\bb\["([A-Za-z0-9_.]+)"\]\s*=', vm))
    aliases = {}
    for new, old in re.findall(r'\balias\("([A-Za-z0-9_.]+)",\s*"([A-Za-z0-9_.]+)"\)', vm):
        names.add(new)
        aliases.setdefault(new, old)
    return names, aliases


def pin_counts(src):
    counts = {}
    m = re.search(r'kWideBuiltins\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if m:
        for name, n in re.findall(r'\{"([^"]+)",\s*(\d+)\}', m.group(1)):
            counts[name] = max(MIN_PINS, min(MAX_PINS, int(n)))
    return counts


def parse_args(text):
    """'ox, oy[, dist]' -> [('ox', False), ('oy', False), ('dist', True)], or None when an
    argument is not a plain name (an example value, 'a or b', 'o..')."""
    text = text.strip()
    if not text:
        return []
    pieces, depth, cur, starts, in_q = [], 0, "", [0], False
    for ch in text:
        if ch == '"':
            in_q = not in_q
        if not in_q and ch == ',':
            pieces.append(cur)
            cur = ""
            starts.append(depth)
            continue
        if not in_q and ch == '[':
            depth += 1
        elif not in_q and ch == ']':
            depth = max(0, depth - 1)
        cur += ch
    pieces.append(cur)
    out = []
    for piece, start_depth in zip(pieces, starts):
        p = piece.strip()
        optional = start_depth > 0 or p.startswith('[')
        p = p.replace('[', '').replace(']', '').strip()
        variadic = p.endswith('...')
        if variadic:
            p = p[:-3].strip()
            if not p:
                if not out:
                    return None      # name(...): the arguments are not listed
                break
        if '=' in p:
            p = p.split('=', 1)[0].strip()
            optional = True
        q = QUOTED.match(p)
        quoted = q is not None
        if quoted:
            p = q.group(1)
            if not (QUOTED_NAME.match(p) or p in QUOTED_PLACEHOLDERS):
                return None
        elif not IDENT.match(p):
            return None
        out.append((p, optional, quoted))
        if variadic:
            break
    return out


def clean(text):
    """Markdown / C string text -> one plain line."""
    t = text.replace('\\"', '"').replace('\\|', '|').replace('\\n', ' ')
    t = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', t)      # [text](link) -> text
    t = t.replace('`', '').replace('**', '')
    t = re.sub(r'(^|\s)[-=]{3,}(?=\s|$)', ' ', t)          # "---- section ----" rules in comments
    t = re.sub(r'\s+', ' ', t).strip()
    if len(t) > MAX_DESC:
        cut = t.rfind('. ', 0, MAX_DESC)
        t = t[:cut + 1] if cut > 80 else t[:MAX_DESC].rstrip() + "..."
    return t


def returns_of(name, desc, hint="", single=False):
    if hint:
        return hint
    m = re.search(r'\b[Rr]eturns?\s+(.+?)(?:[.;]\s|[.;]$|$)', desc)
    who = re.search(r'([A-Za-z_][\w.]*)\s+[Rr]eturns?\b', desc)
    if m and who and who.group(1) != name and "_" in who.group(1):
        m = None                     # "set_clipboard_text returns 1 ..." in a row shared with get_clipboard_text
    if m and not single and not (who and who.group(1) == name):
        m = None                     # a row shared by several functions: the "returns" may be another one's
    if m and m.group(1).lower().startswith(("the same", "to ", "new ", "type")):
        m = None                     # not a value ("returns to the previous one")
    if m:
        r = m.group(1).strip()
        return r[:140]
    low = desc.lower()
    if single and low.startswith(("true if", "true when", "true while", "1 if", "1 when", "1 while")):
        return "true or false"
    return ""


def split_cells(line):
    cells, cur, i = [], "", 0
    while i < len(line):
        if line[i] == '\\' and i + 1 < len(line):
            cur += line[i:i + 2]
            i += 2
            continue
        if line[i] == '|':
            cells.append(cur)
            cur = ""
        else:
            cur += line[i]
        i += 1
    cells.append(cur)
    return cells


def table_source(path, names):
    found = {}
    for line in read(path).splitlines():
        if not line.startswith('| `'):
            continue
        cells = split_cells(line)
        if len(cells) < 3:
            continue
        sig_cell, desc = cells[1].replace('\\|', '|'), clean(cells[2])
        arrow = re.search(r'`\s*(?:→|->)\s*([^`/]+?)\s*$', sig_cell)
        hint = clean(arrow.group(1)) if arrow else ""
        codes = re.findall(r'`([^`]*)`', sig_cell)
        single = len(codes) == 1
        for code in codes:
            for m in SIG.finditer(code):
                if m.group(1) in names:
                    found.setdefault(m.group(1), []).append((m.group(2), desc, hint, single))
    return found


def function_body(src, head):
    i = src.find(head)
    if i < 0:
        return ""
    j = src.find("\n}\n", i)
    return src[i:j]


def fapi_source(main, names):
    found = {}
    for sig, desc in re.findall(r'fapi\("((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\)', main):
        sig = sig.replace('\\"', '"')
        for m in SIG.finditer(sig):
            if m.group(1) in names:
                found.setdefault(m.group(1), []).append((m.group(2), clean(desc), "", "/" not in sig))
    return found


def script_docs(main):
    body = function_body(main, "static const std::string* ScriptDoc(")
    return {n: clean(d) for n, d in re.findall(r'\{"([A-Za-z0-9_.]+)",\s*\{?"((?:[^"\\]|\\.)*)"', body)}


def signature_map_source(main, names, docs):
    found = {}
    body = function_body(main, "ScriptSignatureMap()")
    for name, sig in re.findall(r'\{"([A-Za-z0-9_.]+)",\s*"((?:[^"\\]|\\.)*)"\}', body):
        sig = sig.replace('\\"', '"')
        m = SIG.match(sig.strip())
        if name in names and m and m.group(1) == name:
            found.setdefault(name, []).append((m.group(2), docs.get(name, ""), "", True))
    return found


def vm_comment_source(vm, names):
    found = {}
    block = []
    for line in vm.splitlines() + [""]:
        s = line.strip()
        if s.startswith("//"):
            block.append(s[2:].strip())
            continue
        if block:
            text = " ".join(block)
            for m in SIG.finditer(text):
                if m.group(1) in names:
                    found.setdefault(m.group(1), []).append((m.group(2), clean(text), "", False))
            block = []
    return found


def reads_arguments(vm, name):
    """True when the code of b["name"] uses its argument list, False when it does not, None when
    the builtin is not a plain b["name"] = [..](std::vector<Value>& a) lambda."""
    m = re.search(r'\bb\["%s"\]\s*=\s*\[[^\]]*\]\s*\(([^)]*)\)' % re.escape(name), vm)
    if not m:
        return None
    params = m.group(1)
    pm = re.search(r'std::vector<Value>\s*&\s*(\w+)', params)
    if not pm:
        return False                      # the argument list has no name: nothing is read
    arg = pm.group(1)
    nxt = re.compile(r'\n\s*b\["[A-Za-z0-9_.]+"\]\s*=').search(vm, m.end())
    body = vm[m.end(): nxt.start() if nxt else m.end() + 4000]
    return re.search(r'(?<![\w.])%s(?:\[|\.size\(|\.empty\(|\s*[,)])' % re.escape(arg), body) is not None


# ---- Pin labels ------------------------------------------------------------------------------------
# A node's input label must pass the editor's naming check (LintVsPin in editor/VisualScriptNames.hpp,
# docs/visual_scripting_naming.md): whole words of three letters or more, each starting with a capital,
# no abbreviations, no digits. The raw argument name ("ox") stays in the table and in the tooltip; the
# label is that name in words ("Origin Across"). Written for this tool; the word list is ours.
BANNED = {"anim", "api", "bool", "dt", "euler", "float", "fov", "fps", "gameobject", "id", "idx", "ik", "int",
          "max", "min", "nav", "num", "obj", "pos", "prefab", "old", "quat", "rad", "rigidbody", "rot", "rpc",
          "string", "transform", "ui", "var", "vec", "vec3"}
SMALL = {"a", "and", "at", "by", "for", "in", "is", "of", "on", "or", "the", "to", "up"}
ORDINALS = ["First", "Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eighth", "Ninth", "Tenth",
            "Eleventh", "Twelfth", "Thirteenth"]
WORDS = {
    "addr": "address", "amt": "amount", "ang": "angle", "app": "app", "arr": "list", "bool": "enabled",
    "btn": "button", "cb": "function", "cfg": "settings", "cur": "current", "def": "default", "deg": "degrees",
    "degs": "degrees", "dest": "destination", "dir": "direction", "dist": "distance", "dst": "destination",
    "dur": "duration", "exp": "exponent", "fn": "function", "func": "function", "hi": "high", "hp": "health",
    "id": "identifier", "idx": "index", "img": "image", "init": "initial", "ip": "address", "len": "length",
    "lo": "low", "mag": "strength", "max": "largest", "min": "smallest", "ms": "milliseconds", "msg": "message",
    "num": "number", "obj": "object", "param": "parameter", "pct": "percent", "pos": "position",
    "prefab": "saved object", "prev": "previous", "repl": "replacement", "rot": "rotation",
    "rpc": "network message", "sec": "seconds", "secs": "seconds", "sep": "separator", "seq": "sequence",
    "spd": "speed", "src": "source", "str": "text", "tex": "texture", "txt": "text", "ui": "screen",
    "val": "value", "var": "variable", "vel": "velocity", "vib": "vibration", "vol": "volume", "json": "json",
    "int": "number", "float": "number", "string": "text", "obj_name": "object name",
}
AXES = {"x": "across", "y": "height", "z": "ahead"}
AXIS_PREFIX = {"o": "origin", "c": "center", "p": "point", "t": "target", "n": "normal", "v": "velocity",
               "s": "scale", "f": "force", "a": "start", "b": "end", "h": "half size", "e": "end"}


def lint_label(label):
    """Python copy of the rule LintVsPin applies (it is re-checked in C++ by test_vs_fn_pin_docs)."""
    ws = label.split(" ")
    if not label or "" in ws:
        return False
    for i, w in enumerate(ws):
        low = w.lower()
        if low in BANNED:
            return False
        major = len(w) >= 3 and w[0].isupper() and w[1:].islower() and w[1:].isalpha()
        small = low in SMALL
        if i == 0:
            if not (major or (small and w[0].isupper() and (len(w) == 1 or w[1:].islower()))):
                return False
        elif small:
            if w != low:
                return False
        elif not major:
            return False
    return True


def split_words(raw):
    out, cur = [], ""
    for i, ch in enumerate(raw):
        if ch in "_.- ":
            if cur:
                out.append(cur)
            cur = ""
            continue
        if ch.isdigit():
            if cur:
                out.append(cur)
            out.append(ch)
            cur = ""
            continue
        if ch.isupper() and cur and (cur[-1].islower() or (i + 1 < len(raw) and raw[i + 1].islower())):
            out.append(cur)
            cur = ""
        cur += ch
    if cur:
        out.append(cur)
    return [w.lower() for w in out]


def join_label(ws):
    parts = []
    for w in " ".join(ws).split():
        parts.append(w if parts and w in SMALL else w[:1].upper() + w[1:])
    return " ".join(parts)


def friendly_label(builtin, raw, quoted, all_args):
    """'ox' -> 'Origin Across', 'maxDist' -> 'Largest Distance', '"n"' -> 'Name'; None when no plain
    words come out."""
    builtin = builtin.lower()
    others = {a for a, _, _ in all_args}
    color = {"r", "g", "b"} <= others
    ray = any(k in builtin for k in ("cast", "ray", "dir", "look", "aim"))
    low = raw.lower()
    if len(raw) == 1:
        single = {
            "a": "opacity" if color else "first", "b": "blue" if color else "second", "g": "green" if color else None,
            "r": "red" if color else "radius", "i": "index", "k": "key",
            "m": "amount", "n": "name" if quoted else "count", "o": "object", "p": "point",
            "s": "scale" if "scale" in builtin else "text", "t": "portion" if {"a", "b"} <= others else "time",
            "v": "value", "w": "width", "h": "height", "x": "across",
            "y": "vertical" if ("h" in others or "height" in others) else "height", "z": "ahead",
        }.get(low)
        return join_label([single]) if single else None
    ws = split_words(raw)
    # Two letters, a prefix and an axis: ox / dy / hz, when a sibling axis with the same prefix exists.
    if len(ws) == 1 and len(low) == 2 and low[1] in AXES:
        siblings = {low[0] + ax for ax in AXES} - {low}
        if siblings & others:
            if low[0] == "d":
                pre = "direction" if ray else "change"
            else:
                pre = AXIS_PREFIX.get(low[0])
            if pre:
                return join_label([pre, AXES[low[1]]])
    # x1 / y2 / v1: an ordinal word before the rest.
    if len(ws) >= 2 and ws[-1].isdigit() and ws[-1] != "0":
        idx = int(ws[-1]) - 1
        rest = friendly_label(builtin, "".join(ws[:-1]), quoted, all_args) if len(ws) == 2 else None
        if rest and 0 <= idx < len(ORDINALS):
            return ORDINALS[idx] + " " + rest
        return None
    if len(ws) == 2 and ws[0] in ("max", "min") and ws[1] not in AXES:
        rest = friendly_label(builtin, ws[1], False, all_args)
        if rest:                                   # maxDist -> Distance Limit, minSpeed -> Lowest Speed
            label = rest + " Limit" if ws[0] == "max" else "Lowest " + rest
            return label if lint_label(label) else None
    out = []
    for w in ws:
        if w.isdigit():
            return None
        if w in WORDS:
            out.append(WORDS[w])
        elif w in AXES and len(ws) > 1:
            out.append(AXES[w])
        elif len(w) >= 3 or w in SMALL:
            out.append(w)
        else:
            return None
    label = join_label(out)
    return label if lint_label(label) else None


def pin_labels(builtin, args, pin_count):
    """One label per input of the node: the argument's words where the docs name it, the ordinal
    (First, Second ...) otherwise. Labels on one node are all different."""
    labels = []
    for i in range(pin_count):
        cands = []
        if i < len(args):
            f = friendly_label(builtin, args[i][0], args[i][2], args)
            if f:
                cands.append(f)
        cands += [ORDINALS[i], ORDINALS[i] + " Value"]
        for c in cands:
            if lint_label(c) and c not in labels:
                labels.append(c)
                break
    return labels


def words(text):
    return len(text.split())


def build():
    vm = read(VM)
    main = read(EDITOR_MAIN)
    names, aliases = builtin_names(vm)
    pins = pin_counts(read(VS_BUILTINS))
    docs = script_docs(main)
    sources = [
        ("docs/scripting_reference.md", table_source(DOCS[0], names)),
        ("docs/scripting.md", table_source(DOCS[1], names)),
        ("editor Help reference", fapi_source(main, names)),
        ("OkayScriptVM.cpp comment", vm_comment_source(vm, names)),
        ("editor signature hint", signature_map_source(main, names, docs)),
    ]
    rows, stats = {}, []
    rejected = {"not plain names": 0, "more arguments than inputs": 0, "no arguments, but the code reads some": 0}
    for label, found in sources:
        usable = 0
        for name in sorted(found):
            for argtext, desc, hint, single in found[name]:
                args = parse_args(argtext)
                if args is None:
                    rejected["not plain names"] += 1
                    continue
                if len(args) > pins.get(name, MIN_PINS):
                    rejected["more arguments than inputs"] += 1
                    continue
                if not args and reads_arguments(vm, name):
                    rejected["no arguments, but the code reads some"] += 1
                    continue
                usable += 1
                if name not in rows:
                    rows[name] = [args, desc, hint, single, label]
                break
        stats.append((label, usable))
    # A short heading ("Geometry") or no text at all: take the description from another source.
    for name, row in rows.items():
        if words(row[1]) >= 3:
            continue
        better = docs.get(name, "")
        if words(better) < 3:
            for _, found in sources:
                for _, desc, _, _ in found.get(name, []):
                    if words(desc) >= 3:
                        better = desc
                        break
                if words(better) >= 3:
                    break
        if words(better) >= 3:
            row[1] = better
    rows = {n: (r[0], r[1], returns_of(n, r[1], r[2], r[3]), r[4], pins.get(n, MIN_PINS)) for n, r in rows.items()}
    via_alias = 0
    for new in sorted(aliases):
        old, seen = aliases[new], set()
        while old not in rows and old in aliases and old not in seen:
            seen.add(old)
            old = aliases[old]
        if new not in rows and old in rows:
            args, desc, ret, label, _ = rows[old]
            if len(args) <= pins.get(new, MIN_PINS):
                rows[new] = (args, desc, ret, "alias of " + old, pins.get(new, MIN_PINS))
                via_alias += 1
    return names, rows, stats, rejected, via_alias


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def render(names, rows):
    out = [
        "// GENERATED by tools/gen_fn_pin_docs.py from docs/scripting_reference.md, docs/scripting.md,",
        "// editor/main.cpp (Help reference and signature hints) and OkayScriptVM.cpp comments.",
        "// Do not edit: fix the source and run  python3 tools/gen_fn_pin_docs.py",
        "// Included by engine/src/VisualScript/VsBuiltins.cpp only; read it with okay::vs::BuiltinDocFor.",
        "#pragma once",
        "#include \"okay/VisualScript/VsBuiltins.hpp\"",
        "",
        "namespace okay::vs::gen {",
        "",
        "inline constexpr int kFnDocTotalBuiltins = %d;" % len(names),
        "// name, summary, returns, source, argc, optional bits, pin count, argument names, pin labels",
        "inline constexpr BuiltinDoc kFnDocs[] = {",
    ]
    for name in sorted(rows):
        args, desc, ret, label, npins = rows[name]
        mask = 0
        for i, (_, opt, _) in enumerate(args):
            if opt:
                mask |= 1 << i
        arglist = ", ".join(c_str(a) for a, _, _ in args)
        labels = ", ".join(c_str(l) for l in pin_labels(name, args, npins))
        out.append("    {%s, %s, %s, %s, %d, 0x%xu, %d, {%s}, {%s}}," % (
            c_str(name), c_str(desc), c_str(ret), c_str(label), len(args), mask, npins, arglist, labels))
    out += ["};", "", "inline constexpr int kFnDocCount = int(sizeof(kFnDocs) / sizeof(kFnDocs[0]));", "",
            "} // namespace okay::vs::gen", ""]
    return "\n".join(out)


def main():
    names, rows, stats, rejected, via_alias = build()
    text = render(names, rows)
    if "--stats" in sys.argv:
        for label, n in stats:
            print("%-30s %4d usable signatures" % (label, n))
        print("%-30s %4d" % ("filled from an alias", via_alias))
        for why, n in rejected.items():
            print("skipped (%s): %d" % (why, n))
        print("documented: %d of %d builtins" % (len(rows), len(names)))
    if "--check" in sys.argv:
        try:
            current = read(OUT)
        except OSError:
            current = ""
        if current != text:
            print("VsBuiltinDocs.gen.hpp is out of date: run python3 tools/gen_fn_pin_docs.py", file=sys.stderr)
            return 1
        print("VsBuiltinDocs.gen.hpp is up to date (%d of %d builtins documented)" % (len(rows), len(names)))
        return 0
    if "--stats" not in sys.argv:
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s (%d of %d builtins documented)" % (os.path.relpath(OUT, ROOT), len(rows), len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
