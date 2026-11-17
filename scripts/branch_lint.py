#!/usr/bin/env python3
"""Cheap checks on what a branch adds (the diff against origin/main). Used by .github/workflows/branch-lint.yml.

  python3 scripts/branch_lint.py includes [--base origin/main] [--head HEAD | --rev <commit>]
  python3 scripts/branch_lint.py secrets  ...
  python3 scripts/branch_lint.py docs     ...
  python3 scripts/branch_lint.py binaries ...      (no built programs or big binary files in git)
  python3 scripts/branch_lint.py all      ...      (includes + secrets + docs)
  python3 scripts/branch_lint.py mingw    ...      -fsyntax-only with the MinGW win32 compiler on the changed engine/ shared/ okayui/ launcher/ editor/ player/ files

includes: CLAUDE.md rule 21a. The Windows programs are cross-compiled with MinGW's "win32" thread model, which has no
          <mutex>, <thread>, <condition_variable> or std::call_once. Added lines that use them under engine/ shared/
          okayui/ launcher/ editor/ player/ fail the check (a line inside an `#if` that mentions _WIN32 or a Linux/web
          macro is only a warning). Use debugutil::SpinLock (engine/include/okay/Debug/DebugUtil.hpp), atomics, or the
          existing wrappers. Tests/ may use <thread>. Add `lint-allow: win32-thread` on the line to silence it.
secrets:  private keys, sk-/ghp_/github_pat_/AKIA tokens, and long hex/base64 strings next to key/token/secret/password.
binaries: fails when a branch ADDS or CHANGES a built program or library (.exe .dll .so .dylib .a .lib .pdb .o .msi .dmg .appimage)
          or an archive (.zip .7z .rar .tar .gz .tgz .xz .bz2), or any other binary file over 200 KB, unless its extension
          is an asset type that is legitimately binary (fonts, png/jpg/gif/webp, audio, video, ico, .okaymesh/.okayscene
          examples, wycheproof/test fixtures under tests/). Programs are built by CI and the release workflow, never
          committed (the owner's rule, 2026-11-16). `lint-allow` cannot silence it: add an extension to ASSET_EXT in this file
          (and say why in the PR) if a new asset type is really needed. Everything under docs/images/ is allowed.
docs:     warns when engine/ editor/ launcher/ code changed and no docs/*.md changed (CLAUDE.md rule 5).
mingw:    compiles each changed header and source under engine/ with the MinGW cross compiler (MINGW_CXX, default
          x86_64-w64-mingw32-g++-win32, else x86_64-w64-mingw32-g++). Files that include SDL, imgui, GL, X11, sodium, curl
          or openssl headers are skipped (the SDK is not installed here) and listed. Errors are warnings, never fatal.
  python3 scripts/branch_lint.py tests    ...      -fsyntax-only (host compiler) on each changed tests/*.cpp (fatal)
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
ENTROPY_SKIP_DIRS = ("config/", "tests/", "docs/", "engine/src/Crypto/", "engine/include/okay/Crypto/", "supabase/", "examples/", "assets/")


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


MINGW_SCOPE = ("engine/", "shared/", "okayui/", "launcher/", "editor/", "player/")
# A header is compiled alone and with MinGW's <intrin.h> before and after it: a header that defines its own __cpuid use
# (the Sha256.hpp problem of 2026-11-17) only fails when <intrin.h> is also in the translation unit.
HEADER_VARIANTS = (("alone", "", ""), ("after <intrin.h>", "#include <intrin.h>\n", ""), ("before <intrin.h>", "", "\n#include <intrin.h>"))


def check_mingw(root, base, head, rev=None, workers=4, strict=False, files=None):
    import shutil, subprocess, tempfile
    from concurrent.futures import ThreadPoolExecutor
    cxx = os.environ.get("MINGW_CXX") or next((c for c in ("x86_64-w64-mingw32-g++-win32", "x86_64-w64-mingw32-g++") if shutil.which(c)), None)
    if files is None:
        files = changed_files(root, base, head, rev)
    files = [f for f in files if f.startswith(MINGW_SCOPE) and f.endswith((".hpp", ".h", ".cpp"))
             and os.path.exists(os.path.join(root, f))]
    if not files:
        print("mingw: no changed headers or sources under " + " ".join(MINGW_SCOPE) + ", nothing to check", file=sys.stderr); return []
    if not cxx:
        print("mingw: no MinGW compiler installed here; skipped", file=sys.stderr); return []
    todo, skipped = [], []
    for f in files:
        try: body = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
        except OSError: continue
        (skipped if SKIP_INCLUDE_RE.search(body) else todo).append(f)
    inc = ["-Iengine/include", "-Iengine/src", "-Ishared", "-Iokayui/include", "-Ilauncher", "-Ieditor", "-Iplayer"]
    tmp = tempfile.mkdtemp(prefix="mingwlint")
    def compile_one(src):
        p = subprocess.run([cxx, "-std=c++17", "-fsyntax-only", "-w", "-DNOMINMAX", *inc, src], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", timeout=120)
        return p.returncode, p.stderr
    def one(f):
        src = os.path.join(root, f)
        if not f.endswith((".hpp", ".h")):
            rc, err = compile_one(src); return f, rc, err
        rel = f[len("engine/include/"):] if f.startswith("engine/include/") else os.path.abspath(src)
        for label, pre, post in HEADER_VARIANTS:
            tu = os.path.join(tmp, re.sub(r"\W", "_", f) + "_" + re.sub(r"\W", "", label) + ".cpp")
            open(tu, "w").write(f'{pre}#include "{rel}"{post}\n')
            rc, err = compile_one(tu)
            if rc: return f, rc, ("(header " + label + ") " if label != "alone" else "") + err
        return f, 0, ""
    findings = []
    with ThreadPoolExecutor(workers) as ex:
        for f, rc, err in ex.map(one, todo):
            if rc == 0: continue
            first = next((l for l in err.splitlines() if "error" in l), err.strip()[:200])
            if "No such file or directory" in first:
                skipped.append(f + " (a header is not installed here)"); continue
            findings.append({"sev": "error" if strict else "warning", "file": f, "line": 1, "text": first[:160], "msg": "does not compile with the MinGW cross compiler: " + first[:200]})
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"mingw: compiler {cxx}; checked {len(todo)} files, skipped {len(skipped)}" + (": " + ", ".join(skipped[:8]) if skipped else ""), file=sys.stderr)
    return findings


def check_tests_syntax(root, base, head, rev=None, workers=4, files=None):
    """Host compiler -fsyntax-only on each changed tests/test_*.cpp (fatal: CI would fail on it after a long build)."""
    import shutil, subprocess
    from concurrent.futures import ThreadPoolExecutor
    cxx = os.environ.get("CXX") or next((c for c in ("g++", "c++", "clang++") if shutil.which(c)), None)
    if files is None:
        files = changed_files(root, base, head, rev)
    files = [f for f in files if f.startswith("tests/") and f.endswith(".cpp") and os.path.exists(os.path.join(root, f))]
    if not files or not cxx:
        print("tests: nothing to check" if not files else "tests: no C++ compiler; skipped", file=sys.stderr); return []
    inc = ["-Iengine/include", "-Iengine/src", "-Ishared", "-Iokayui/include", "-Ilauncher", "-Ieditor", "-Itests", f"-DOKAY_SOURCE_DIR=\"{root}\""]
    def one(f):
        p = subprocess.run([cxx, "-std=c++17", "-fsyntax-only", "-w", *inc, f], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", timeout=300)
        return f, p.returncode, p.stderr
    findings, skipped = [], []
    with ThreadPoolExecutor(workers) as ex:
        for f, rc, err in ex.map(one, files):
            if rc == 0: continue
            first = next((l for l in err.splitlines() if "error" in l), err.strip()[:200])
            if "No such file or directory" in first:
                skipped.append(f); continue
            findings.append({"sev": "error", "file": f, "line": 1, "text": first[:160], "msg": "does not compile: " + first[:200]})
    print(f"tests: compiler {cxx}; checked {len(files) - len(skipped)} of {len(files)} test files" + (f"; skipped (header not installed here): {', '.join(skipped[:6])}" if skipped else ""), file=sys.stderr)
    return findings


PROGRAM_EXT = (".exe", ".dll", ".so", ".dylib", ".a", ".lib", ".pdb", ".o", ".msi", ".dmg", ".appimage", ".apk", ".ipa", ".wasm", ".zip", ".7z", ".rar",
               ".tar", ".gz", ".tgz", ".xz", ".bz2", ".jar")
ASSET_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".ico", ".tga", ".ttf", ".otf", ".woff", ".woff2", ".wav", ".ogg", ".mp3", ".flac", ".mp4", ".webm",
             ".okaymesh", ".okayscene", ".okaypack", ".okayanim", ".pdf")
BIG_BINARY_BYTES = 200 * 1024
ALLOW_PATH_PREFIXES = ("docs/images/",)


def check_binaries(root, base, head, rev=None):
    """Added or changed files that are programs/archives (any size) or binary files over 200 KB with a non-asset extension."""
    args = ["diff", "--numstat", "--no-renames", "--diff-filter=AM"] + ([f"{rev}^", rev] if rev else [f"{base}...{head}"])
    findings = []
    for line in L.git(root, *args).splitlines():
        parts = line.split("\t", 2)
        if len(parts) != 3:
            continue
        path, is_bin = parts[2], parts[0] == "-" and parts[1] == "-"
        if path.startswith(ALLOW_PATH_PREFIXES):
            continue
        ext = os.path.splitext(path)[1].lower()
        if ext in PROGRAM_EXT:
            findings.append({"sev": "error", "file": path, "line": 1, "text": "", "msg": f"a built program, library or archive ({ext}) must not be committed. CI and the release workflow build it; add the path to .gitignore."})
            continue
        if is_bin and ext not in ASSET_EXT:
            try:
                size = int(L.git(root, "cat-file", "-s", f"{rev or head}:{path}").strip())
            except (ValueError, OSError, RuntimeError):
                size = BIG_BINARY_BYTES + 1
            if size > BIG_BINARY_BYTES:
                findings.append({"sev": "error", "file": path, "line": 1, "text": "", "msg": f"a binary file of {size // 1024} KB with the extension '{ext or '(none)'}' is not an allowed asset type. Do not commit built files; if it is a real asset, add its extension to ASSET_EXT in scripts/branch_lint.py and say why."})
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
    ap.add_argument("check", choices=["includes", "secrets", "docs", "binaries", "mingw", "tests", "all"])
    ap.add_argument("--root", default=".")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--files", nargs="+", help="mingw: check exactly these files (repo-relative) instead of the changed ones")
    ap.add_argument("--strict", action="store_true", help="mingw: compile errors are errors (exit 1), not warnings")
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
    if a.check in ("binaries", "all"):
        f = check_binaries(root, a.base, a.head, a.rev); err += emit(f, "binary-file")
        print(f"binaries: {len(f)} finding(s)", file=sys.stderr)
    if a.check in ("docs", "all"):
        f = check_docs(root, a.base, a.head, a.rev); emit(f, "docs")
        print(f"docs: {len(f)} finding(s)", file=sys.stderr)
    if a.check == "mingw":
        f = check_mingw(root, a.base, a.head, a.rev, strict=a.strict, files=a.files); err += emit(f, "mingw")
    if a.check == "tests":
        f = check_tests_syntax(root, a.base, a.head, a.rev, files=a.files); err += emit(f, "tests-syntax")
    return 1 if err else 0


if __name__ == "__main__":
    sys.exit(main())
