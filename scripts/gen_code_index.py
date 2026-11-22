#!/usr/bin/env python3
"""Writes docs/engine/code_index.md: every public class, struct, enum and free function in the engine's public headers
(engine/include, shared/, okayui/include), with file:line and the first line of its doc comment, grouped by folder.
Search it before writing anything new (docs/process/no_duplicates.md).

  python3 scripts/gen_code_index.py           write docs/engine/code_index.md and docs/engine/code_index/*.md
  python3 scripts/gen_code_index.py --check   exit 1 when the files are out of date (branch-lint warns)
  python3 scripts/gen_code_index.py --find tween easing    print the matching rows (a quick search)

"Public" means at namespace scope (not inside a class or a function), and not in an anonymous namespace or one named
detail, *_detail or internal. Methods are not listed: open the class's header. The index is split into one page per
folder group so no page gets huge. Standard library only; written from our own design.
"""
import argparse, os, re, sys

ROOTS = ("engine/include/", "shared/", "okayui/include/")
EXT = (".hpp", ".h", ".inl")
OUT = "docs/engine/code_index.md"
OUT_DIR = "docs/engine/code_index"
SKIP_NS = re.compile(r"^(?:detail|\w+_detail|internal|impl)$")
NOT_TYPES = frozenset("""return else case new delete throw goto if while for switch using typedef namespace sizeof decltype
static_assert alignof typeid catch do friend operator public private protected""".split())
COMMENT_RE = re.compile(r'//[^\n]*|/\*[\s\S]*?(?:\*/|\Z)|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
TYPE_RE = re.compile(r"^\s*(?:template\s*<.*>\s*)?(?:typedef\s+)?(class|struct|union|enum(?:\s+class|\s+struct)?)\s+"
                     r"(?:alignas\s*\([^)]*\)\s*|\[\[[^\]]*\]\]\s*|[A-Z][A-Z0-9_]+(?:\s*\([^)]*\))?\s+)*([A-Za-z_]\w*)\b(.*)$")
FUNC_RE = re.compile(r"^\s*(?:template\s*<.*>\s*)?((?:[\w:<>,\[\]]+[ \t*&]+)+?)[*&]?\s*([A-Za-z_]\w*)\s*\((.*)$")
NS_RE = re.compile(r"\bnamespace\s*([\w:]*)\s*(\{)?")


def blank(text):
    """Comments and string contents removed, newlines kept, so line numbers stay right."""
    def rep(m):
        s = m.group()
        if s.startswith(("//", "/*")):
            return "\n" * s.count("\n")
        return s[0] + s[-1] if len(s) >= 2 else s
    return COMMENT_RE.sub(rep, text)


def doc_line(raw, i):
    """First line of the doc comment just above raw line i (0-based), or ''."""
    j = i - 1
    while j >= 0 and re.match(r"^\s*template\s*<", raw[j]):
        j -= 1
    block = []
    while j >= 0:
        s = raw[j].strip()
        if s.startswith(("///", "//", "/*", "*")) or s.endswith("*/"):
            block.append(s)
            j -= 1
        else:
            break
    for s in reversed(block):
        t = re.sub(r"^(?:/\*\*?|///?<?|\*/?|//!)\s?", "", s).strip().rstrip("*/").strip()
        t = re.sub(r"^[-=#*\s]+$", "", t)
        if t and not t.startswith(("#pragma", "Copyright")):
            return t[:160]
    return ""


def entries(path, text):
    """[(kind, name, line, doc)] at public namespace scope in one header."""
    raw = text.split("\n")
    lines = blank(text).split("\n")
    stack, pending_ns, out, seen = [], None, [], set()
    for i, line in enumerate(lines):
        public = all(k == "ns" for k, _ in stack) and not any(SKIP_NS.match(n) or n == "" for k, n in stack if k == "ns")
        if public and not line.lstrip().startswith("#"):
            m = TYPE_RE.match(line)
            if m and not m.group(3).strip().startswith(";") and not re.match(r"^\s*\w*\s*;", m.group(3)):
                kind = m.group(1).split()[0]
                if (kind, m.group(2)) not in seen and m.group(2) not in ("final",):
                    seen.add((kind, m.group(2)))
                    out.append((kind, m.group(2), i + 1, doc_line(raw, i)))
            else:
                f = FUNC_RE.match(line)
                if f:
                    first = f.group(1).split()[0].strip("*&")
                    name = f.group(2)
                    if (first not in NOT_TYPES and not name.isupper() and not first.isupper() and name != "operator"
                            and "=" not in f.group(1) and ("function", name) not in seen and not line.rstrip().endswith(",")):
                        seen.add(("function", name))
                        out.append(("function", name, i + 1, doc_line(raw, i)))
        # update the scope stack with this line's braces
        nsm = NS_RE.search(line)
        if nsm and not re.search(r"\busing\s+namespace\b", line) and "=" not in line[nsm.start():]:
            pending_ns = nsm.group(1).split("::")[-1]
        if re.search(r'\bextern\s+"C"', line):
            pending_ns = "extern_c"
        for ch in line:
            if ch == "{":
                if pending_ns is not None:
                    stack.append(("ns", pending_ns)); pending_ns = None
                else:
                    stack.append(("block", ""))
            elif ch == "}" and stack:
                stack.pop()
    return out


def group_of(path):
    if path.startswith("engine/include/okay/"):
        sub = path[len("engine/include/okay/"):].split("/")
        return "engine-" + (sub[0] if len(sub) > 1 else "root")
    if path.startswith("engine/include/"):
        return "engine-root"
    return path.split("/")[0]


def collect(root):
    rows = {}
    for top in ROOTS:
        base = os.path.join(root, top)
        for d, dirs, files in os.walk(base):
            dirs.sort()
            for fn in sorted(files):
                if not fn.endswith(EXT):
                    continue
                full = os.path.join(d, fn)
                rel = os.path.relpath(full, root).replace(os.sep, "/")
                try:
                    text = open(full, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                if re.search(r"(?i)generated by|do not edit|exported using", text[:600]):
                    continue
                e = entries(rel, text)
                if e:
                    rows[rel] = e
    return rows


def render(rows):
    groups = {}
    for path, e in rows.items():
        groups.setdefault(group_of(path), {})[path] = e
    pages = {}
    head = ("<!-- Generated by scripts/gen_code_index.py. Do not edit by hand: run python3 scripts/gen_code_index.py -->\n")
    idx = [head, "# Code index: what already exists", "",
           "Every public class, struct, enum and free function in `engine/include/`, `shared/` and `okayui/include/`, with",
           "file:line and the first line of its doc comment. **Search this before you write a helper or a system** (for",
           "example `python3 scripts/gen_code_index.py --find json parse`), then [shared_code.md](shared_code.md) for the one",
           "file to use per job. Methods are not listed: open the class. Plain rules: [no_duplicates.md](../process/no_duplicates.md).",
           "", "| Folder | Files | Names | Page |", "|---|---|---|---|"]
    for g in sorted(groups):
        files = groups[g]
        n = sum(len(v) for v in files.values())
        idx.append(f"| {g} | {len(files)} | {n} | [code_index/{g}.md](code_index/{g}.md) |")
        page = [head, f"# Code index: {g}", "", f"Back to the [code index](../code_index.md). {len(files)} headers, {n} names.", ""]
        for path in sorted(files):
            page.append(f"## `{path}`")
            page.append("")
            for kind, name, line, doc in files[path]:
                doc = doc.replace("|", "/").replace("`", "'")
                page.append(f"- `{name}` {kind}, line {line}" + (f": {doc}" if doc else ""))
            page.append("")
        pages[f"{OUT_DIR}/{g}.md"] = "\n".join(page).rstrip() + "\n"
    idx.append("")
    pages[OUT] = "\n".join(idx)
    return pages


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--find", nargs="+", metavar="WORD")
    a = ap.parse_args()
    rows = collect(a.root)
    if a.find:
        words = [w.lower() for w in a.find]
        for path, e in sorted(rows.items()):
            for kind, name, line, doc in e:
                hay = (name + " " + doc + " " + path).lower()
                if all(w in hay for w in words):
                    print(f"{path}:{line}  {kind} {name}  {doc}")
        return 0
    pages = render(rows)
    old_pages = set()
    d = os.path.join(a.root, OUT_DIR)
    if os.path.isdir(d):
        old_pages = {f"{OUT_DIR}/{f}" for f in os.listdir(d) if f.endswith(".md")}
    stale = sorted(p for p, t in pages.items() if not os.path.exists(os.path.join(a.root, p)) or open(os.path.join(a.root, p), encoding="utf-8").read() != t)
    extra = sorted(old_pages - set(pages))
    n = sum(len(e) for e in rows.values())
    if a.check:
        if stale or extra:
            print(f"code index out of date ({len(stale)} pages differ, {len(extra)} to remove). Run: python3 scripts/gen_code_index.py")
            return 1
        print(f"code index up to date ({len(rows)} headers, {n} names, {len(pages)} pages)")
        return 0
    os.makedirs(d, exist_ok=True)
    for p in stale:
        with open(os.path.join(a.root, p), "w", encoding="utf-8") as f:
            f.write(pages[p])
    for p in extra:
        os.remove(os.path.join(a.root, p))
    print(f"wrote {len(stale)} pages, removed {len(extra)} ({len(rows)} headers, {n} names, {len(pages)} pages)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
