#!/usr/bin/env python3
"""Checks that two tests can never fight over the same files (docs/ci_failures.md, cause "shared temp folder").

  python3 scripts/lint_test_registration.py                 whole repository
  python3 scripts/lint_test_registration.py --base origin/main   also: new "/tmp/..." literals in tests/*.cpp added by this branch
  python3 scripts/lint_test_registration.py --self-test

Rules (all fatal):
  1. Two add_test() calls with the same command and arguments but different names run the same program twice. Under
     `ctest -j` both copies run at the same moment. The second name must carry the marker
         # okay-lint: same-command-twice <second test name>: <why that is safe>
     on a comment line in tests/CMakeLists.txt (tests/CMakeLists.txt gives every test its own TMPDIR, so a copy that only
     uses std::filesystem::temp_directory_path() is safe). Names that contain ${...} come from loops and are not compared.
  2. The same fixed "/tmp/..." string literal in two different tests/*.cpp files: two tests would write the same file.
  3. With --base: a NEW "/tmp/..." literal added to tests/*.cpp. Use std::filesystem::temp_directory_path() (every test has
     its own TMPDIR) or mkdtemp. Put `okay-lint: fixed-tmp-ok` on the line to allow one that is only a fake path
     string and never touches the disk.
Written for this repository from the ctest and CMake documentation. Standard library and git only.
"""
import argparse, collections, os, re, subprocess, sys

ADD_TEST_RE = re.compile(r"add_test\(\s*NAME\s+(\S+)\s+COMMAND\s+([^)]*)\)", re.S)
MARKER_RE = re.compile(r"#\s*okay-lint:\s*same-command-twice\s+(\S+?):\s*\S")
TMP_RE = re.compile(r'"(/tmp/[^"\s]*)"')


def strip_comments(text):
    return "\n".join(re.sub(r"(?<!\\)#.*", "", l) for l in text.splitlines())


def registration_findings(cmake_text):
    allowed = set(MARKER_RE.findall(cmake_text))
    seen = collections.OrderedDict()
    out = []
    for m in ADD_TEST_RE.finditer(strip_comments(cmake_text)):
        name, cmd = m.group(1), " ".join(m.group(2).split())
        if "${" in name:
            continue
        if cmd in seen and seen[cmd] != name and name not in allowed:
            out.append(f"test '{name}' runs the same command as '{seen[cmd]}' ({cmd[:80]}). Under ctest -j both run at once. "
                       f"Drop one, or add a comment '# okay-lint: same-command-twice {name}: <why it is safe>'")
        seen.setdefault(cmd, name)
    return out


def tmp_literal_findings(files):
    by_lit = collections.defaultdict(set)
    for path, text in files.items():
        for l in text.splitlines():
            if "okay-lint: fixed-tmp-ok" in l:
                continue
            for lit in TMP_RE.findall(l):
                by_lit[lit].add(path)
    return [f"the fixed path \"{lit}\" is used by {', '.join(sorted(ps))}: two tests would write the same file"
            for lit, ps in sorted(by_lit.items()) if len(ps) > 1]


def new_tmp_literals(root, base, head="HEAD"):
    try:
        d = subprocess.run(["git", "diff", "-U0", f"{base}...{head}", "--", "tests/*.cpp"], cwd=root, capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"lint_test_registration: cannot diff against {base}; skipping the new-literal check", file=sys.stderr); return []
    out, cur = [], ""
    for l in d.splitlines():
        if l.startswith("+++ b/"): cur = l[6:]
        elif l.startswith("+") and not l.startswith("+++") and "okay-lint: fixed-tmp-ok" not in l:
            for lit in TMP_RE.findall(l):
                out.append(f"{cur}: new fixed path \"{lit}\". Use std::filesystem::temp_directory_path() (every test has its own TMPDIR) or mkdtemp")
    return out


def run(root, base):
    cm = os.path.join(root, "tests", "CMakeLists.txt")
    problems = registration_findings(open(cm, encoding="utf-8").read())
    files = {}
    tdir = os.path.join(root, "tests")
    for n in sorted(os.listdir(tdir)):
        if n.endswith(".cpp"):
            files["tests/" + n] = open(os.path.join(tdir, n), encoding="utf-8", errors="replace").read()
    problems += tmp_literal_findings(files)
    if base:
        problems += new_tmp_literals(root, base)
    return problems


def self_test():
    ok = 'add_test(NAME a COMMAND test_x)\nadd_test(NAME b COMMAND test_x --other)\nadd_test(NAME ${f}_a COMMAND t)\nadd_test(NAME ${f}_b COMMAND t)\n'
    assert registration_findings(ok) == [], registration_findings(ok)
    bad = 'add_test(NAME a COMMAND test_x)\nadd_test(NAME b COMMAND test_x)\n'
    assert len(registration_findings(bad)) == 1
    marked = bad + '# okay-lint: same-command-twice b: only uses its own TMPDIR\n'
    assert registration_findings(marked) == []
    commented = '# add_test(NAME b COMMAND test_x)\nadd_test(NAME a COMMAND test_x)\n'
    assert registration_findings(commented) == []
    multi = 'add_test(NAME a COMMAND ${P} -I\n   ${S}/x.py)\nadd_test(NAME b COMMAND ${P} -I ${S}/x.py)\n'
    assert len(registration_findings(multi)) == 1
    assert len(tmp_literal_findings({"tests/a.cpp": 'f("/tmp/x.bin");', "tests/b.cpp": 'g("/tmp/x.bin");'})) == 1
    assert tmp_literal_findings({"tests/a.cpp": 'f("/tmp/x.bin");', "tests/b.cpp": 'g("/tmp/y.bin");'}) == []
    assert tmp_literal_findings({"tests/a.cpp": 'f("/tmp/x.bin");', "tests/b.cpp": 'g("/tmp/x.bin"); // okay-lint: fixed-tmp-ok'}) == []
    print("lint_test_registration self-test: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--base", help="also reject NEW fixed /tmp literals added since this ref (for example origin/main)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test(); return 0
    problems = run(os.path.abspath(a.root), a.base)
    for p in problems:
        print(f"::error title=test registration::{p}")
        print("ERROR " + p, file=sys.stderr)
    print(f"lint_test_registration: {len(problems)} problem(s)", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
