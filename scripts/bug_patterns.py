#!/usr/bin/env python3
"""Cheap look-for-the-usual-mistakes lint. Only the lines a branch ADDS are reported, as warnings (exit 0).

  python3 scripts/bug_patterns.py [--base origin/main] [--head HEAD | --rev <commit>] [--github] [--strict]
  python3 scripts/bug_patterns.py --files a.cpp b.cpp        scan whole files (for trying it out)

The classes come from the bugs that independent reviews found late (docs/process/review_log.md). Each rule is a heuristic with
a known false-positive risk, so nothing here fails a build; fix the finding or say in the PR why it is fine. Silence a
line with `lint-allow: <id>` in a comment on it.

  int-cast       (int) or static_cast<int> of strtol/strtoll/strtoul/atol/stod results with no INT_MAX / clamp / limits guard
  substr-loop    s = s.substr(i) or s.substr(i) (tail copy) inside a for/while body: quadratic
  loop-copy      per-item copies in loops over meshes, bones, vertices, clips: `for (auto x : meshes)`, `for (std::vector<..> v : ..)`,
                 `std::vector<..> copy = items[i];` inside a loop
  alloc-before-check  resize/reserve/vector(n)/new T[n]/assign(n) with n read from a file or packet within the last 10 lines and no
                 comparison of n against a limit in between (allocate only after the budget check)
  test-cannot-fail    CHECK(true), CHECK(1), or a timing check such as CHECK(ms < 2000) in a test (a 2 s limit never fails)
  own-table-test      ModelImport.cpp own-reader table (kOwnSlots / a {"OKAY_OWN_..."} row) edited with no test in the diff that
                 selects ImportBackend::Own (the option-ON case)
Written for this repo from the review log; standard library and git only.
"""
import argparse, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collab_lib as L
from branch_lint import added_lines

CODE = (".cpp", ".hpp", ".h", ".cc", ".inc")
GUARD_RE = re.compile(r"INT_MAX|INT_MIN|numeric_limits|clamp|std::min|std::max|\bkMax\w*|<=?\s*\d|>=?\s*\d")
STRTO_RE = re.compile(r"\b(?:std::)?(strtol|strtoll|strtoul|strtoull|atol|atoll|stol|stoll|strtod|strtof|stod)\s*\(")
INT_CAST_RE = re.compile(r"\(\s*int\s*\)\s*\(?\s*(?:std::)?(?:strtol|strtoll|strtoul|strtoull|atol|atoll|stol|stoll|strtod|strtof|stod)\s*\(|static_cast<\s*int\s*>\s*\(\s*(?:std::)?(?:strtol|strtoll|strtoul|strtoull|atol|atoll|stol|stoll|strtod|strtof|stod)\s*\(")
ASSIGN_STRTO_RE = re.compile(r"\b(?:long|long long|double|float|auto|unsigned long|int64_t|uint64_t)\s+(\w+)\s*=\s*(?:std::)?(?:strtol|strtoll|strtoul|strtoull|atol|atoll|stol|stoll|strtod|strtof|stod)\s*\(")
LOOP_RE = re.compile(r"\b(for|while)\s*\(")
ITEMS_RE = re.compile(r"(?i)(mesh|bone|vert|joint|skin|clip|face|tri|submesh|node|keyframe|channel)")
READ_COUNT_RE = re.compile(r"\b(\w+)\s*=\s*[^;]*\b(?:Read|read|Get|get|Parse|parse|U8|U16|U32|U64|Le16|Le32|Be16|Be32|ReadU\w+|GetU\w+|ntohl|ntohs)\w*\s*\(")
ALLOC_RE = re.compile(r"\.(?:resize|reserve|assign)\s*\(\s*(\w+)\b|std::(?:vector|string)<[^;]*>\s+\w+\s*[({]\s*(\w+)\s*[)}]|\bnew\s+[\w:<>]+\s*\[\s*(\w+)\s*\]")


def enclosing_loop(lines, idx, window=250):
    """Is lines[idx] inside a for/while body? Walks upward counting braces (heuristic, ignores braces in strings)."""
    depth = 0
    for j in range(idx - 1, max(-1, idx - window), -1):
        t = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', lines[j])
        t = t.split("//")[0]
        depth += t.count("}") - t.count("{")
        if depth < 0:
            if LOOP_RE.search(t) or (j > 0 and LOOP_RE.search(lines[j - 1]) and t.strip() == "{"):
                return True
            depth = 0
    return False


def scan_file(path, lines, only, tests_added=False):
    """Return findings for the lines whose 1-based number is in `only` (None = all)."""
    out = []
    def add(i, rid, msg):
        if only is not None and (i + 1) not in only: return
        if f"lint-allow: {rid}" in lines[i]: return
        out.append((path, i + 1, rid, msg))
    is_test = path.startswith("tests/")
    strto_vars = {}
    for i, line in enumerate(lines):
        code = line.split("//")[0]
        m = ASSIGN_STRTO_RE.search(code)
        if m: strto_vars[m.group(1)] = i
        if not is_test:
            if INT_CAST_RE.search(code) and not GUARD_RE.search(code):
                add(i, "int-cast", "(int) of a strto*/stod result: a big or hostile value overflows; clamp against INT_MAX/limits first")
            for v, vi in list(strto_vars.items()):
                if 0 < i - vi <= 12 and re.search(r"\(\s*int\s*\)\s*" + re.escape(v) + r"\b|static_cast<\s*int\s*>\s*\(\s*" + re.escape(v) + r"\s*\)", code):
                    if not any(re.search(r"\b" + re.escape(v) + r"\b", lines[k]) and GUARD_RE.search(lines[k]) for k in range(vi + 1, i + 1)):
                        add(i, "int-cast", f"(int){v} where {v} came from strto*/stod on line {vi + 1} with no range check in between")
            if (re.search(r"(\w+)\s*=\s*\1\.substr\s*\(", code) or re.search(r"\.substr\s*\(\s*[^,()]+\)", code)) and enclosing_loop(lines, i):
                add(i, "substr-loop", "substr tail copy inside a loop is quadratic; keep an index or string_view and advance it")
            m = re.search(r"for\s*\(\s*(?:const\s+)?auto\s+(?!&)(\w+)\s*:\s*([\w.>\[\]-]+)", code)
            if m and ITEMS_RE.search(m.group(2)):
                add(i, "loop-copy", f"`for (auto {m.group(1)} : {m.group(2)})` copies every item; use `const auto&` (meshes, bones and clips are big)")
            if re.search(r"for\s*\(\s*(?:const\s+)?std::(?:vector|string|map|unordered_map)<[^)]*>\s+\w+\s*:", code):
                add(i, "loop-copy", "range-for over a container by value copies each element; use a reference")
            if re.search(r"^\s*(?:const\s+)?std::vector<[^;]*>\s+\w+\s*=\s*\w+(?:\[[^\]]+\]|\.\w+(?:\.\w+)*)\s*;", code) and enclosing_loop(lines, i):
                add(i, "loop-copy", "vector copied inside a loop; take a const reference (or move) instead")
            ma = ALLOC_RE.search(code)
            if ma:
                var = ma.group(1) or ma.group(2) or ma.group(3)
                if var and not var.isdigit() and not var.startswith("k"):
                    for k in range(i - 1, max(-1, i - 11), -1):
                        mr = READ_COUNT_RE.search(lines[k].split("//")[0])
                        if mr and mr.group(1) == var:
                            checked = any(re.search(r"\b" + re.escape(var) + r"\b\s*(?:>|<|>=|<=|!=)|(?:>|<|>=|<=)\s*" + re.escape(var) + r"\b|std::min\([^)]*\b" + re.escape(var) + r"|clamp\([^)]*\b" + re.escape(var), lines[q]) for q in range(k + 1, i + 1))
                            if not checked:
                                add(i, "alloc-before-check", f"allocation sized by `{var}` read from data on line {k + 1} with no limit check before it (check the budget first)")
                            break
        else:
            if re.search(r"\b(?:CHECK|REQUIRE|ASSERT|assert)\s*\(\s*(?:true|1)\s*\)", code):
                add(i, "test-cannot-fail", "CHECK(true) can never fail; assert the real property")
            if re.search(r"\b(?:CHECK|REQUIRE|assert)\s*\([^;]*\b(?:ms|millis|msec|elapsed\w*|seconds|secs?)\b\s*<\s*\d{4,}", code) or re.search(r"\b(?:CHECK|REQUIRE|assert)\s*\([^;]*\b(?:seconds|secs?|elapsedSec\w*)\b\s*<\s*(?:[3-9]\d|\d{3,})\b", code):
                add(i, "test-cannot-fail", "a timing limit of seconds almost never fails; measure a count (steps, bytes, comparisons) or tighten the bound")
    return out


def scan_diff(root, base, head, rev=None):
    per_file = {}
    for path, ln, text in added_lines(root, base, head, rev):
        if path.endswith(CODE):
            per_file.setdefault(path, set()).add(ln)
    res, ref = [], rev or head
    for path, nums in sorted(per_file.items()):
        lines = L.git(root, "show", f"{ref}:{path}").splitlines()
        res += scan_file(path, lines, nums)
    # branch-level: own-reader table edited without an option-ON test
    table_edit = [(p, ln) for p, ln, t in added_lines(root, base, head, rev)
                  if p.endswith("ModelImport.cpp") and re.search(r"kOwnSlots|\{\s*\"OKAY_OWN_\w+\"", t)]
    if table_edit:
        has_test = any(p.startswith("tests/") and re.search(r"ImportBackend::Own|SetImportBackend\s*\([^)]*Own", t) for p, _, t in added_lines(root, base, head, rev))
        if not has_test:
            p, ln = table_edit[0]
            res.append((p, ln, "own-table-test", "own-reader table (kOwnSlots / OKAY_OWN_ row) edited, but no test in this diff selects ImportBackend::Own: add the option-ON test"))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--rev")
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--github", action="store_true", help="also print ::warning annotations")
    ap.add_argument("--strict", action="store_true", help="exit 1 when anything is found")
    a = ap.parse_args()
    root = L.repo_root(a.root)
    if a.files:
        res = []
        for f in a.files:
            res += scan_file(f.replace(root + "/", ""), open(f, encoding="utf-8", errors="replace").read().splitlines(), None)
    else:
        res = scan_diff(root, a.base, a.head, a.rev)
    for path, ln, rid, msg in res:
        print(f"{path}:{ln}: [{rid}] {msg}")
        if a.github: print(f"::warning file={path},line={ln},title=bug-pattern {rid}::{msg}")
    print(f"bug_patterns: {len(res)} warning(s)", file=sys.stderr)
    return 1 if (res and a.strict) else 0


if __name__ == "__main__":
    sys.exit(main())
