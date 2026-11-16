#!/usr/bin/env python3
"""Cheap checks on what a branch adds (the diff against origin/main). Used by .github/workflows/branch-lint.yml.

  python3 scripts/branch_lint.py includes [--base origin/main] [--head HEAD | --rev <commit>]
  python3 scripts/branch_lint.py secrets  ...
  python3 scripts/branch_lint.py docs     ...
  python3 scripts/branch_lint.py all      ...      (includes + secrets + docs)
  python3 scripts/branch_lint.py mingw    ...      -fsyntax-only with the MinGW win32 compiler on the changed engine/ files

includes: CLAUDE.md rule 21a. The Windows programs are cross-compiled with MinGW's "win32" thread model, which has no
          <mutex>, <thread>, <condition_variable> or std::call_once. Added lines that use them under engine/ shared/
          okayui/ launcher/ editor/ player/ fail the check (a line inside an `#if` that mentions _WIN32 or a Linux/web
          macro is only a warning). Use debugutil::SpinLock (engine/include/okay/Debug/DebugUtil.hpp), atomics, or the
          existing wrappers. Tests/ may use <thread>. Add `lint-allow: win32-thread` on the line to silence it.
secrets:  private keys, sk-/ghp_/github_pat_/AKIA tokens, and long hex/base64 strings next to key/token/secret/password.
docs:     warns when engine/ editor/ launcher/ code changed and no docs/*.md changed (CLAUDE.md rule 5).
mingw:    compiles each changed header and source under engine/ with the MinGW cross compiler (MINGW_CXX, default
          x86_64-w64-mingw32-g++-win32, else x86_64-w64-mingw32-g++). Files that include SDL, imgui, GL, X11, sodium, curl
          or openssl headers are skipped (the SDK is not installed here) and listed. Errors are warnings, never fatal.
Exit code 1 only for includes and secrets findings that are errors. Prints GitHub annotations (::error / ::warning).
Standard library and git only.
"""
import argparse, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collab_lib as L

THREAD_RE = re.compile(r"#\s*include\s*<(mutex|thread|condition_variable|shared_mutex|future)>|\bstd::(call_once|once_flag|mutex|recursive_mutex|timed_mutex|shared_mutex|thread|jthread|condition_variable(?:_any)?|lock_guard|unique_lock|scoped_lock|async|future|promise)\b")
SCOPE_DIRS = ("engine/", "shared/", "okayui/", "launcher/", "editor/", "player/")
SRC_EXT = (".hpp", ".h", ".inc", ".cpp", ".cc", ".c")
PLATFORM_GUARD_RE = re.compile(r"#\s*(?:if|ifdef|ifndef|elif)\b.*(_WIN32|__linux__|__unix__|__APPLE__|__EMSCRIPTEN__|OKAY_PLATFORM|OKAY_NO_WIN)")
SECRET_PATTERNS = [
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP |ENCRYPTED )?PRIVATE KEY")),
    ("OpenAI-style key", re.compile(r"\bsk-[A-Za-z0-9_-]{20,}")),
    ("GitHub token", re.compile(r"\b(?:ghp|gho|ghs|ghu|ghr)_[A-Za-z0-9]{30,}|\bgithub_pat_[A-Za-z0-9_]{30,}")),
    ("AWS access key id", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("Slack token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{20,}")),
]
ENTROPY_RE = re.compile(r"(?i)(key|token|secret|passw(?:or)?d)[^\n]{0,40}?[\"'=:\s]([A-Fa-f0-9]{40,}|[A-Za-z0-9+/_-]{40,}={0,2})[\"'\s;,)]*$")
ENTROPY_SKIP_DIRS = ("tests/", "docs/", "engine/src/Crypto/", "engine/include/okay/Crypto/", "supabase/", "examples/", "assets/")


def added_lines(root, base, head, rev=None):
    """Yield (path, line number in the new file, text) for every added line."""
    args = ["diff", "-U0", "--no-renames", "--diff-filter=AM"]
    args += [f"{rev}^", rev] if rev else [f"{base}...{head}"]
    cur, ln = None, 0
    for line in L.git(root, *args).splitlines():
        if line.startswith("+++ "):
            cur = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@"):
            m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)", line)
            ln = int(m.group(1)) if m else 0
        elif line.startswith("+") and cur:
            yield cur, ln, line[1:]
            ln += 1


def changed_files(root, base, head, rev=None):
    args = ["diff", "--name-only", "--no-renames"] + ([f"{rev}^", rev] if rev else [f"{base}...{head}"])
    return [x for x in L.git(root, *args).splitlines() if x]


def guard_nearby(root, ref, path, line_no):
    """True when one of the 25 lines before line_no, in the file at ref, is an #if naming a platform macro."""
    txt = L.git(root, "show", f"{ref}:{path}").splitlines()
    lo = max(0, line_no - 26)
    return any(PLATFORM_GUARD_RE.search(t) for t in txt[lo:line_no - 1])


def check_includes(root, base, head, rev=None):
    findings = []
    ref = rev or head
    for path, ln, text in added_lines(root, base, head, rev):
        if not path.startswith(SCOPE_DIRS) or not path.endswith(SRC_EXT):
            continue
        if "lint-allow: win32-thread" in text:
            continue
        m = THREAD_RE.search(text)
        if not m:
            continue
        is_header = path.endswith((".hpp", ".h", ".inc"))
        guarded = guard_nearby(root, ref, path, ln)
        sev = "warning" if guarded else "error"
        what = m.group(0).strip()
        findings.append({"sev": sev, "file": path, "line": ln, "text": text.strip()[:120],
                         "msg": f"{what} in {'a header' if is_header else 'a source file'} the Windows build compiles (MinGW 'win32' threads have no "
                                f"<mutex>/<thread>/<condition_variable>/std::call_once). Use debugutil::SpinLock (Debug/DebugUtil.hpp), "
                                f"atomics or an existing wrapper; CLAUDE.md rule 21a." + (" (inside a platform #if: check it really is not built for Windows)" if guarded else "")})
    return findings


def check_secrets(root, base, head, rev=None):
    findings = []
    for path, ln, text in added_lines(root, base, head, rev):
        if "lint-allow: secret" in text:
            continue
        for name, rx in SECRET_PATTERNS:
            if rx.search(text):
                findings.append({"sev": "error", "file": path, "line": ln, "text": "<hidden>", "msg": f"looks like a {name}. Secrets go in GitHub secrets, never in the repo (rule 15)."})
        if not path.startswith(ENTROPY_SKIP_DIRS) and ENTROPY_RE.search(text) and not re.search(r"(?i)public|sha-?256|hash|checksum|digest", text):
            findings.append({"sev": "error", "file": path, "line": ln, "text": "<hidden>", "msg": "a long hex/base64 string next to key/token/secret/password. If it is not a secret, add 'lint-allow: secret' on the line."})
    return findings


def check_docs(root, base, head, rev=None):
    files = changed_files(root, base, head, rev)
    code = [f for f in files if f.startswith(("engine/", "editor/", "launcher/")) and f.endswith(SRC_EXT)]
    docs = [f for f in files if f.startswith("docs/") and f.endswith(".md")]
    if code and not docs:
        return [{"sev": "warning", "file": code[0], "line": 1, "text": "",
                 "msg": f"{len(code)} code files changed under engine/ editor/ launcher/ but no docs/*.md changed. Add or update the matching guide, a docs/try_it/<id>-<slug>.md entry (not the generated docs/try_it.md) and the feature map (rule 5, rule 4a)."}]
    return []


SKIP_INCLUDE_RE = re.compile(r'#\s*include\s*[<"](SDL|imgui|GL/|GLES|X11/|sodium|curl/|openssl/|libsodium|vulkan|emscripten|stb_)')


def check_mingw(root, base, head, rev=None, workers=4):
    import shutil, subprocess, tempfile
    from concurrent.futures import ThreadPoolExecutor
    cxx = os.environ.get("MINGW_CXX") or next((c for c in ("x86_64-w64-mingw32-g++-win32", "x86_64-w64-mingw32-g++") if shutil.which(c)), None)
    files = [f for f in changed_files(root, base, head, rev) if f.startswith("engine/") and f.endswith((".hpp", ".h", ".cpp"))
             and os.path.exists(os.path.join(root, f))]
    if not files:
        print("mingw: no changed engine/ headers or sources, nothing to check", file=sys.stderr); return []
    if not cxx:
        print("mingw: no MinGW compiler installed here; skipped", file=sys.stderr); return []
    todo, skipped = [], []
    for f in files:
        try: body = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
        except OSError: continue
        (skipped if SKIP_INCLUDE_RE.search(body) else todo).append(f)
    inc = ["-Iengine/include", "-Iengine/src", "-Ishared", "-Iokayui/include"]
    tmp = tempfile.mkdtemp(prefix="mingwlint")
    def one(f):
        src = os.path.join(root, f)
        if f.endswith((".hpp", ".h")):
            rel = f[len("engine/include/"):] if f.startswith("engine/include/") else os.path.abspath(src)
            src = os.path.join(tmp, re.sub(r"\W", "_", f) + ".cpp")
            open(src, "w").write(f'#include "{rel}"\n')
        p = subprocess.run([cxx, "-std=c++17", "-fsyntax-only", "-w", "-DNOMINMAX", *inc, src], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", timeout=120)
        return f, p.returncode, p.stderr
    findings = []
    with ThreadPoolExecutor(workers) as ex:
        for f, rc, err in ex.map(one, todo):
            if rc == 0: continue
            first = next((l for l in err.splitlines() if "error" in l), err.strip()[:200])
            if "No such file or directory" in first:
                skipped.append(f + " (a header is not installed here)"); continue
            findings.append({"sev": "warning", "file": f, "line": 1, "text": first[:160], "msg": "does not compile with the MinGW cross compiler: " + first[:200]})
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"mingw: compiler {cxx}; checked {len(todo)} files, skipped {len(skipped)}" + (": " + ", ".join(skipped[:8]) if skipped else ""), file=sys.stderr)
    return findings


def emit(findings, label):
    err = 0
    for f in findings:
        print(f"::{f['sev']} file={f['file']},line={f['line']},title={label}::{f['msg']}")
        print(f"  {f['sev'].upper()} {label} {f['file']}:{f['line']}: {f['msg']}" + (f"\n      {f['text']}" if f["text"] else ""), file=sys.stderr)
        err += f["sev"] == "error"
    return err


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("check", choices=["includes", "secrets", "docs", "mingw", "all"])
    ap.add_argument("--root", default=".")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--rev", help="check one commit (its changes against its parent) instead of base...head")
    a = ap.parse_args()
    root = L.repo_root(a.root)
    err = 0
    if a.check in ("includes", "all"):
        f = check_includes(root, a.base, a.head, a.rev); err += emit(f, "win32-threads")
        print(f"includes: {len(f)} finding(s)", file=sys.stderr)
    if a.check in ("secrets", "all"):
        f = check_secrets(root, a.base, a.head, a.rev); err += emit(f, "secret")
        print(f"secrets: {len(f)} finding(s)", file=sys.stderr)
    if a.check in ("docs", "all"):
        f = check_docs(root, a.base, a.head, a.rev); emit(f, "docs")
        print(f"docs: {len(f)} finding(s)", file=sys.stderr)
    if a.check == "mingw":
        f = check_mingw(root, a.base, a.head, a.rev); emit(f, "mingw")
    return 1 if err else 0


if __name__ == "__main__":
    sys.exit(main())
