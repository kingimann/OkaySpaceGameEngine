#!/usr/bin/env python3
"""Duplicate-code guard for the whole repository, every code language.

  python3 scripts/dup_lint.py check [--base origin/main] [--head HEAD]   what a branch adds (CI, pre-push hook)
  python3 scripts/dup_lint.py scan  [--head HEAD] [--top 10]             whole-tree report, per language
  python3 scripts/dup_lint.py scan  --update-baseline                    rewrite scripts/dup_baseline.json

Plain rules: docs/process/no_duplicates.md. The one-place list of shared code: docs/engine/shared_code.md.

1. Fingerprints. Each rule is a regular expression for the telltale of a helper that already has one shared home
   (the FNV-1a constants, the CRC-32 polynomial, the Base64 alphabet, a hand-made Trim/Split/ToLower, a JSON escaper,
   HLSL text inside C++, ...) and the file(s) allowed to contain it. Today's hits are listed in
   scripts/dup_baseline.json (rule -> file -> count), one file for every language. A ratchet: a hit in a file that is
   not in the baseline fails, a count that goes up fails, a count that goes down passes and prints "baseline can
   shrink". The baseline itself may only shrink: a branch that adds an entry or raises a count in it fails.

2. Copy-paste. Every code file is cut into tokens by a small tokenizer for its language family (C-like: C, C++,
   C#, Java, JS/TS, GLSL/HLSL/.okayshader; hash comments: Python, shell, YAML, PowerShell, NSIS, .okayvs/.okaybt;
   CMake; Lua; SQL; HTML; batch). Comments, whitespace and #include/import lines are dropped; the words inside string
   literals are tokens too, so a shader pasted as C++ strings is caught. Two token streams: "exact" (as written) and
   "loose" (identifiers that are not keywords become $, numbers 0). Fingerprints: at content-chosen anchors (where a
   hash of two neighbouring tokens is 0 mod P) the next K tokens are hashed. Both copies of a shared run choose the
   same anchors, so a run of N tokens is found unless no anchor falls in its first N-K+1 tokens (chance
   ((P-1)/P)^(N-K+1), about 1 in 10 000 with the values below). Equal fingerprints are grown token by token in both
   directions to the longest real match. A block the branch ADDS that matches N or more tokens of code elsewhere
   (or another added block in a different file) is reported: an exact match fails, a loose-only match warns.

3. docs/engine/shared_code.md must only name files that exist (rows marked "planned" are skipped).

4. System names (CLAUDE.md rule 19e, the system registry docs/SYSTEMS.md). A file the branch ADDS whose name has a
   banned qualifier as a word (Own, New, Improved, Classic, V2: `OwnSolver.cpp`, `physics_v2.py`) is an error, and so is
   a new folder with one in its name: the system we write IS the system, so it gets the plain name. A new top-level
   folder, or a new folder directly under engine/include/okay/, engine/src/ or editor/ (a new system), is an error unless
   a system's `Home:` in the feature map (docs/feature_map/*.md, read at --head) names it. A file named exactly in a
   `Home:` or `Entry:`, or with `dup-allow: <reason>` in its first 15 lines, is allowed. A new class or function whose
   name has a banned qualifier is a warning.

Suppression: `dup-allow: <reason>` on the line (or the line above) of a fingerprint hit, or on any line of a pasted
block; `dup-allow-begin: <reason>` ... `dup-allow-end` for a block. The reason must not be empty: a bare `dup-allow:`
is itself an error.

Files are read from git objects at --head (the working tree is not used; nothing is cached). EXCLUDE lists what is
left out and why. Prints GitHub annotations (::error / ::warning). Exit 1 when there is an error.

Standard library and git only. Written from the general idea of fingerprinting k-token runs with a hash and keeping
a content-chosen sample of them (Karp-Rabin fingerprints; winnowing, Schleimer, Wilkerson and Aiken 2003); no code
from any other tool.
"""
import argparse, bisect, json, multiprocessing, os, re, subprocess, sys, time
from array import array
from collections import Counter, defaultdict
from itertools import accumulate, compress, count, repeat
from operator import eq, mod, not_, rshift, xor

# ------------------------------------------------------------------------------------------ what is read

# Extension -> (language shown in reports, tokenizer family).
LANG = {}
for _exts, _lang, _fam in [
    (".c .cc .cpp .cxx .h .hh .hpp .hxx .inc .inl .ipp .m .mm", "C/C++", "c"),
    (".cs", "C#", "c"), (".java .kt .swift .rs .go", "other C-like", "c"),
    (".js .mjs .cjs .jsx .ts .tsx", "JS/TS", "js"),
    (".glsl .vert .frag .comp .geom .tesc .tese .hlsl .hlsli .fx .okayshader", "shader", "c"),
    (".okayscript", "OkayScript", "c"),
    (".py", "Python", "py"), (".sh .bash", "shell", "hash"), (".ps1 .psm1", "PowerShell", "ps"),
    (".yml .yaml", "YAML", "hash"), (".cmake", "CMake", "cmake"), (".lua", "Lua", "lua"), (".sql", "SQL", "sql"),
    (".html .htm", "HTML", "html"), (".bat .cmd", "batch", "bat"), (".nsi .nsh", "NSIS", "nsis"),
    (".okayvs .okaybt", "Okay script graphs", "hash"),
]:
    for _e in _exts.split():
        LANG[_e] = (_lang, _fam)
NAMED = {"CMakeLists.txt": ("CMake", "cmake"), "Dockerfile": ("shell", "hash")}

# Left out of every check, with the reason.
EXCLUDE = (
    ("engine/third_party/", "vendored third-party code (ufbx), being replaced"),
    ("okayui/probe/", "copies of Dear ImGui headers kept only to compare our UI against"),
    ("editor/generated/", "generated by tools/gen_*_library.py"),
    ("tests/wycheproof/", "third-party test vectors"),
    ("tests/data/", "test fixtures and data files"), ("tests/golden/", "golden data"), ("tests/assets/", "test assets"),
    ("tests/export/", "export fixtures"),
    ("tools/okaycc/tests/progs", "programs the compiler compiles (test inputs, repeated on purpose)"),
    ("tools/okayms/stdinc/", "stand-in standard headers for the compiler"),
    ("scripts/dup_lint.py", "this guard: its rules spell out the telltales it looks for"),
    ("scripts/tests/test_dup_lint.py", "this guard's tests: they plant copies on purpose"),
)
INCLUDE_DIRS_DOCS = ("docs/sql/",)  # the only code under docs/
# A file whose first lines say it is generated (or a data export) is left out too.
GENERATED_RE = re.compile(r"(?im)\A(?:[^\n]*\n){0,4}?[^\n]*(?:\bgenerated by\b|\bauto-?generated\b|do not edit|exported using|binary_to_compressed)")

BASELINE = "scripts/dup_baseline.json"
SHARED_DOC = "docs/engine/shared_code.md"
CODE_INDEX = "docs/engine/code_index.md"
K = 32           # tokens hashed per fingerprint
P = 6            # one anchor every P tokens on average
N = 80           # shortest exact match reported (about 10 to 15 lines); see docs/process/no_duplicates.md
LOOSE_N = 120    # loose (renamed) matches must be longer before they warn: shorter ones are mostly look-alike setters
BUCKET_CAP = 48  # scan: a fingerprint seen more often than this is boilerplate and is not paired up
CHECK_CAP = 200  # check: the same, for a fingerprint of added code
WINDOW_CAP = 40  # check: an added window whose words occur together in more files than this is too common to search by
LOOSE_RARE = 60  # check: a word in at most this many files counts as rare when looking for renamed copies

# ------------------------------------------------------------------------------------------ fingerprint rules

C_LIKE = {"c", "js"}
ALL = None  # every language
_U = r"(?:[uU]?[lL]{0,2}|[lL]{0,2}[uU]?)"
RULES = [
    dict(id="fnv1a", what="FNV-1a hash constants", fams=ALL,
         rx=r"\b(?:0x811c9dc5|16777619|0x01000193|2166136261|0xcbf29ce484222325|1099511628211|0x100000001b3|14695981039346656037)" + _U + r"\b",
         allowed=["engine/include/okay/Core/StringId.hpp"], use="okay::StringId::HashOf (Core/StringId.hpp)"),
    dict(id="crc32", what="CRC-32 polynomial (a CRC table)", fams=ALL, rx=r"(?i)\b0x(?:EDB88320|04C11DB7)" + _U + r"\b",
         allowed=["engine/src/Core/ImageWrite.cpp"], use="okay::ImageWrite::Crc32 (Core/ImageWrite.hpp)"),
    dict(id="base64", what="Base64 alphabet or decode table", fams=ALL,
         rx=r"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789|-\s*'a'\s*\+\s*26\b",
         allowed=["engine/include/okay/Core/Base64.hpp"], use="Core/Base64.hpp (planned; dedup in progress)"),
    dict(id="actionlist-broadcast", what="hand-written FindObjectsOfType<ActionList> + ReceiveMessage loop", fams={"c"},
         rx=r"FindObjectsOfType<\s*ActionList\s*>\s*\(\s*\)[^;]{0,200}?ReceiveMessage\s*\(",
         allowed=["engine/src/Components/ActionList.cpp", "engine/src/Components/ActionListOps.cpp"], use="one ActionList broadcast helper (planned; dedup in progress)"),
    dict(id="string-helper", what="hand-made Trim / ToLower / ToUpper / Split function", fams={"c"},
         rx=r"(?m)^[ \t]*(?:(?:static|inline|constexpr)\s+)*(?:std::)?(?:string|wstring|string_view|vector\s*<\s*(?:std::)?string(?:_view)?\s*>|void|char)\s*[&*]?\s+"
            r"(?:\w+::)?[A-Za-z]*_?(?:[Tt]rim|[Ll]ower|[Uu]pper|[Ss]plit)(?:[A-Z0-9]\w*)?\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{",
         allowed=["engine/include/okay/Core/StringUtil.hpp"], use="Core/StringUtil.hpp (planned; dedup in progress)"),
    dict(id="perlin", what="Perlin permutation table or the 6t^5-15t^4+10t^3 fade", fams=ALL,
         rx=r"\b151\s*,\s*160\s*,\s*137\s*,\s*91\s*,\s*90\s*,\s*15\b|\*\s*6(?:\.0*)?f?\s*-\s*15(?:\.0*)?f?\s*\)\s*\+\s*10(?:\.0*)?f?\b",
         allowed=["engine/include/okay/Math/Noise.hpp"], use="okay::NoiseField (Math/Noise.hpp)"),
    dict(id="json-escape", what="a JSON escape function", fams=ALL,
         rx=r"(?m)^[ \t]*(?:(?:static|inline|export|async)\s+)*(?:(?:std::string|void|bool)\s*&?\s+(?:\w+::)?|def\s+|function\s+)"
            r"\w*(?:[Jj]son\w*[Ee]scape|[Ee]scape\w*[Jj]son|[Jj]son[Ss]tr(?:ing)?\b|[Jj]son[Qq]uote|[Qq]uote[Jj]son)\w*\s*\(|s/\"/\\\\\"/g",
         allowed=["engine/include/okay/Core/Json.hpp", "engine/src/Scripting/OkayScriptVM.cpp"], use="Core/Json.hpp (planned; until then JsonEscape in Scripting/OkayScriptVM.cpp; json.dumps in Python, JSON.stringify in JS, jq in shell)"),
    dict(id="two-bone-ik", what="a hand-made law-of-cosines solve (a*a + b*b - c*c) / (2*a*b)", fams={"c"},
         rx=r"\(\s*(\w+(?:\.\w+)?)\s*\*\s*\1\s*\+\s*(\w+(?:\.\w+)?)\s*\*\s*\2\s*-\s*(\w+(?:\.\w+)?)\s*\*\s*\3\s*\)\s*/\s*\(\s*2(?:\.0*)?f?\s*\*",
         allowed=["engine/include/okay/Math/TwoBoneIK.hpp"], use="okay::SolveTwoBoneIK (Math/TwoBoneIK.hpp)"),
    dict(id="sha2-constants", what="SHA-256 / SHA-512 round constants", fams=ALL, rx=r"(?i)\b0x428a2f98(?:d728ae22)?" + _U + r"\b",
         allowed=["engine/include/okay/Crypto/Sha256.hpp", "engine/src/Crypto/Sha512.cpp"], use="okay::crypto::Sha256Hash / Sha512Hash"),
    dict(id="splitmix-lcg", what="SplitMix64 / PCG / classic LCG random constants", fams=ALL,
         rx=r"(?i)\b0x(?:BF58476D1CE4E5B9|94D049BB133111EB)" + _U + r"\b|\b6364136223846793005" + _U + r"\b|\b1103515245" + _U + r"\b",
         allowed=["engine/include/okay/Core/Random.hpp"], use="okay::Random (Core/Random.hpp)"),
    dict(id="deflate-tables", what="a DEFLATE code-length order table (an inflater)", fams=ALL,
         rx=r"\b16\s*,\s*17\s*,\s*18\s*,\s*0\s*,\s*8\s*,\s*7\s*,\s*9\s*,\s*6\s*,\s*10\s*,\s*5\b",
         allowed=["engine/include/okay/Core/Inflate.hpp"], use="okay::inflate::Zlib / Raw / Gzip (Core/Inflate.hpp)"),
    dict(id="easing-back", what="the Back easing overshoot 1.70158 (a hand-made easing)", fams=ALL, rx=r"\b1\.70158f?\b",
         allowed=["engine/include/okay/Math/Easing.hpp"], use="okay::Easing::Evaluate (Math/Easing.hpp)"),
    dict(id="hlsl-in-cpp", what="HLSL shader text pasted into a C++ file", fams={"c"},
         rx=r"\"\s*cbuffer\s+\w+\s*:\s*register\s*\(\s*b0",
         allowed=["engine/include/okay/Render/D3D11Hlsl.hpp"], use="the HLSL in Render/D3D11Hlsl.hpp (include it; D3D12 dedup in progress)"),
    dict(id="workflow-mingw-install", what="the MinGW install block pasted into a workflow", fams={"hash"},
         rx=r"apt-get\s+\$APT\s+install\s+-y\s+--no-install-recommends\s+g\+\+-mingw-w64",
         allowed=[".github/workflows/release.yml"], use="a shared script under scripts/ (planned) that every workflow calls"),
]
# Cheap first test: a rule's regex only runs on a file whose lower-cased text contains one of these.
NEEDLES = {"fnv1a": ("811c9dc5", "16777619", "01000193", "2166136261", "cbf29ce484222325", "1099511628211", "100000001b3", "14695981039346656037"),
           "crc32": ("edb88320", "04c11db7"), "base64": ("abcdefghijklmnopqrstuvwxyz0123456789", "'a'"), "actionlist-broadcast": ("actionlist",),
           "string-helper": ("trim", "lower", "upper", "split"), "perlin": ("151", "15"), "json-escape": ("json", 's/"/'),
           "sha2-constants": ("428a2f98",), "splitmix-lcg": ("bf58476d1ce4e5b9", "94d049bb133111eb", "6364136223846793005", "1103515245"),
           "easing-back": ("1.70158",), "hlsl-in-cpp": ("cbuffer",), "workflow-mingw-install": ("mingw",)}
for _r in RULES:
    _r["re"] = re.compile(_r["rx"])
    _r["need"] = NEEDLES.get(_r["id"], ())

ALLOW_RE = re.compile(r"dup-allow(?P<kind>-begin|-end)?(?::(?P<reason>[^\n]*))?")

# ------------------------------------------------------------------------------------------ tokenizers

_STR_C = r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\''
_STR_PY = r'"""[\s\S]*?"""|\'\'\'[\s\S]*?\'\'\'|' + _STR_C
_HASH = r"(?<![^\s;])#[^\n]*"
STRIP = {  # (comment alternatives, string alternatives)
    "c": (r"//[^\n]*|/\*[\s\S]*?(?:\*/|\Z)", _STR_C),
    "js": (r"//[^\n]*|/\*[\s\S]*?(?:\*/|\Z)", _STR_C + r"|`(?:\\.|[^`\\])*`"),
    "py": (_HASH, _STR_PY),
    "hash": (_HASH, r'"(?:\\.|[^"\\\n])*"|\'[^\'\n]*\''),
    "ps": (r"<#[\s\S]*?#>|" + _HASH, r'"(?:`.|[^"`\n])*"|\'[^\'\n]*\''),
    "cmake": (r"#\[(?P<eq>=*)\[[\s\S]*?\](?P=eq)\]|#[^\n]*", r'"(?:\\.|[^"\\])*"'),
    "lua": (r"--\[(?P<eq>=*)\[[\s\S]*?\](?P=eq)\]|--[^\n]*", _STR_C),
    "sql": (r"--[^\n]*|/\*[\s\S]*?(?:\*/|\Z)", r"'(?:''|[^'])*'|\"[^\"\n]*\""),
    "html": (r"<!--[\s\S]*?-->|/\*[\s\S]*?(?:\*/|\Z)|(?<![:\w/])//[^\n]*", _STR_C),
    "bat": (r"(?im:^[ \t@]*(?:rem\b|::)[^\n]*)", r'"[^"\n]*"'),
    "nsis": (r"/\*[\s\S]*?(?:\*/|\Z)|(?<![^\s])[;#][^\n]*", r'"(?:\$\\.|[^"\n])*"|\'[^\'\n]*\''),
}
STRIP_RE = {f: re.compile(f"(?P<k>{c})|(?P<s>{s})") for f, (c, s) in STRIP.items()}
DROP_RE = {  # lines that say what a file uses, not what it does
    "c": re.compile(r"(?m)^[ \t]*#[ \t]*(?:include|import)\b[^\n]*"),
    "js": re.compile(r"(?m)^[ \t]*(?:import\b|export\s+\*\s+from\b)[^\n]*"),
    "py": re.compile(r"(?m)^[ \t]*(?:import|from\s+\S+\s+import)\b[^\n]*"),
}
TOK_RE = re.compile(r"\n|[A-Za-z_]\w*|\d[\w.]*|::|->|=>|&&|\|\||<<|>>|[-+*/%&|^=!<>]=|\S")
KEYWORDS = ("""alignas alignof and asm auto bool break case catch char class const constexpr const_cast continue decltype default
delete do double dynamic_cast else enum explicit extern false float for friend goto if inline int long mutable namespace new noexcept not
nullptr operator or private protected public register reinterpret_cast return short signed sizeof static static_assert static_cast struct
switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while std size_t override final
def elif except finally from import in is lambda None True False pass raise with yield async await global nonlocal self
function var let of typeof instanceof export then fi done esac local echo exit
if endif foreach endforeach set function endfunction macro endmacro
select insert update create table where join on as begin end returns language plpgsql
float2 float3 float4 float4x4 vec2 vec3 vec4 mat4 uniform in out""".split())
KEYWORD_SET = frozenset(KEYWORDS)
H_ID, H_NUM = hash("$"), hash("0")


def lang_of(path):
    # cross_branch keys are "<branch>\0<path>": the branch name may hold "/" too, so read only the path part
    base = path.rsplit("\0", 1)[-1].rsplit("/", 1)[-1]
    if base in NAMED:
        return NAMED[base]
    ext = os.path.splitext(base)[1].lower()
    return LANG.get(ext)


def excluded(path):
    for pre, _why in EXCLUDE:
        if path.startswith(pre):
            return True
    return path.startswith("docs/") and not path.startswith(INCLUDE_DIRS_DOCS)


def strip_comments(text, fam):
    def rep(m):
        return m.group() if m.lastgroup == "s" else " " + "\n" * m.group().count("\n")  # a space: a comment never joins two words
    return STRIP_RE[fam].sub(rep, text)


def tokenize(text, fam, want_text=False):
    """(exact token hashes, loose token hashes, line of each token[, token texts])."""
    d = DROP_RE.get(fam)
    if d is not None:
        text = d.sub("", text)
    toks = TOK_RE.findall(text)
    nl = list(map(eq, toks, repeat("\n")))
    keep = list(map(not_, nl))
    lines = list(compress(accumulate(nl, initial=1), keep + [False]))
    tk = list(compress(toks, keep))
    ex = list(map(hash, tk))
    loose = {}
    for t in set(tk):  # few distinct tokens per file: decide each once
        c = t[0]
        loose[t] = H_ID if (c.isalpha() or c == "_") and t not in KEYWORD_SET else H_NUM if c.isdigit() else hash(t)
    return (ex, list(map(loose.__getitem__, tk)), lines) + ((tk,) if want_text else ())


def quick_key(ids, i):
    """A cheap stand-in for a window's hash (its first and last token): windows with different keys differ."""
    return ids[i] ^ (ids[i + K - 1] << 1)


# check mode: {"ex": keys, "lo": keys} of the added windows; set before the worker processes start (fork shares it)
ONLY_KEYS = None


def fingerprints(ids, only=None):
    """(hashes, positions): a K-token hash at every anchor (two-token hash == 0 mod P). `only`: skip windows whose
    quick_key is not in this set (they cannot equal an added window)."""
    n = len(ids)
    if n < K:
        return [], []
    mix = map(xor, ids, map(rshift, ids[1:], repeat(3)))
    pos = list(compress(count(), map(not_, map(mod, mix, repeat(P)))))
    pos = pos[:bisect.bisect_right(pos, n - K)]
    if only is not None:
        pos = [i for i in pos if (ids[i] ^ (ids[i + K - 1] << 1)) in only]
    return [hash(tuple(ids[i:i + K])) for i in pos], pos


def line_starts(text):
    return [m.start() for m in re.finditer("\n", text)]


def allow_state(text):
    """(allowed line numbers, [(line, problem)]) from dup-allow markers in one file."""
    allowed, bad, block = set(), [], False
    for i, line in enumerate(text.split("\n"), 1):
        if "dup-allow" not in line:
            if block:
                allowed.add(i)
            continue
        m = ALLOW_RE.search(line)
        kind = m.group("kind")
        reason = re.sub(r"(\*/|-->|#>)\s*$", "", (m.group("reason") or "")).strip()
        if kind == "-end":
            if block:
                allowed.add(i)
            block = False
            continue
        if not reason:
            bad.append((i, f"`dup-allow{kind or ''}:` needs a reason after the colon, for example `dup-allow: the file format spec repeats this table`"))
            continue
        block = block or kind == "-begin"
        allowed.update((i, i + 1))  # the marker may sit on the line above
    return allowed, bad


def index_one(item):
    """Everything one file contributes: tokens, fingerprints, rule hits, dup-allow lines."""
    path, text, fam, want_tokens = item
    clean = strip_comments(text, fam)
    allowed, bad = allow_state(text) if "dup-allow" in text else (set(), [])
    hits, nls, low = {}, None, clean.lower() if ONLY_KEYS is None or want_tokens == 2 else ""
    for r in RULES if low else ():
        if (r["fams"] is not None and fam not in r["fams"]) or path in r["allowed"]:
            continue
        if r["need"] and not any(x in low for x in r["need"]):
            continue
        ms = [m.start() for m in r["re"].finditer(clean)]
        if not ms:
            continue
        if nls is None:
            nls = line_starts(clean)
        lns = sorted({bisect.bisect_left(nls, s) + 1 for s in ms} - allowed)
        if lns:
            hits[r["id"]] = lns
    rec = {"hits": hits, "allowed": allowed, "bad": bad}
    if want_tokens:
        ex, lo, lines, *tk = tokenize(clean, fam, want_tokens == 2)
        if tk:
            rec["tk"] = tk[0]
        only = ONLY_KEYS if want_tokens == 1 else None  # a file only searched for copies of added code
        fe, pe = fingerprints(ex, only and only["ex"])
        fl, pl = fingerprints(lo, only and only["lo"])
        rec.update(ex=array("q", ex), lo=array("q", lo), ln=array("i", lines),
                   fe=array("q", fe), pe=array("i", pe), fl=array("q", fl), pl=array("i", pl))
    return path, rec


def index_files(files, jobs, want_tokens=True):
    items = sorted(((p, t, lang_of(p)[1], want_tokens) for p, t in files.items()), key=lambda it: -len(it[1]))
    if jobs > 1 and len(items) > 16:
        ctx = multiprocessing.get_context("fork")  # fork keeps one string-hash seed across the workers
        with ctx.Pool(jobs) as pool:
            res = pool.map(index_one, items, chunksize=8)
    else:
        res = [index_one(it) for it in items]
    return dict(res)


# ------------------------------------------------------------------------------------------ git access


def git(root, *args):
    p = subprocess.run(["git", "-C", root, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode:
        raise RuntimeError(f"git {' '.join(args)}: {p.stderr.decode(errors='replace').strip()}")
    return p.stdout.decode("utf-8", errors="replace")


def code_paths(root, rev):
    """Every code file at `rev` that is not excluded."""
    return [p for p in git(root, "ls-tree", "-r", "-z", "--name-only", rev).split("\0") if p and lang_of(p) and not excluded(p)]


def read_tree(root, rev, stats=None, names=None):
    """{path: text} for the code files at `rev` (all, or `names`), generated ones left out; one `git cat-file --batch`."""
    names = code_paths(root, rev) if names is None else list(names)
    if not names:
        return {}
    req = "".join(f"{rev}:{p}\n" for p in names).encode()
    out = subprocess.run(["git", "-C", root, "cat-file", "--batch"], input=req, stdout=subprocess.PIPE, check=True).stdout
    files, pos = {}, 0
    for p in names:
        nl = out.index(b"\n", pos)
        head = out[pos:nl].split()
        pos = nl + 1
        if len(head) < 3 or head[1] != b"blob":
            continue
        size = int(head[2])
        data = out[pos:pos + size]
        pos += size + 1
        if b"\0" in data[:8192]:
            continue
        text = data.decode("utf-8", errors="replace")
        if GENERATED_RE.search(text[:2000]):
            if stats is not None:
                stats["generated"].append(p)
            continue
        files[p] = text
    return files


def read_blob(root, rev, path):
    try:
        return git(root, "show", f"{rev}:{path}")
    except RuntimeError:
        return None


MOVED_MIN_CHARS = 12  # shorter lines are too common to prove a move


def added_line_map(root, base, head):
    """{path: set(line numbers added at head)} from `git diff base...head`.

    Moved code is not added code: a line whose text (spaces ignored) the same branch deletes somewhere else is left out,
    so a pure move (cut from one file, pasted into another, as the editor split does) is never reported as a new copy.
    Short lines ("}", "return x;") are always counted, so a move cannot hide a real paste around it."""
    out = git(root, "diff", "-U0", "--no-renames", "--no-color", "--diff-filter=AMD", f"{base}...{head}")
    added, cur, ln = defaultdict(set), None, 0
    plus, removed = [], Counter()
    for line in out.splitlines():
        if line.startswith("+++ "):
            cur = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("--- "):
            continue
        elif line.startswith("@@"):
            m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)", line)
            ln = int(m.group(1)) if m else 0
        elif line.startswith("+") and cur:
            plus.append((cur, ln, "".join(line[1:].split())))
            ln += 1
        elif line.startswith("-"):
            key = "".join(line[1:].split())
            if len(key) >= MOVED_MIN_CHARS:
                removed[key] += 1
    for path, n, key in plus:
        if len(key) >= MOVED_MIN_CHARS and removed[key] > 0:
            removed[key] -= 1  # each deleted line can explain one moved line
            continue
        added[path].add(n)
    return added


# ------------------------------------------------------------------------------------------ baseline


def load_baseline(root, head, explicit):
    if explicit:
        try:
            return json.load(open(explicit)), explicit
        except (OSError, ValueError):
            return {}, explicit
    txt = read_blob(root, head, BASELINE)
    if txt is None:
        try:
            txt = open(os.path.join(root, BASELINE)).read()
        except OSError:
            return {}, None
    try:
        return json.loads(txt), BASELINE
    except ValueError:
        return {}, BASELINE


def baseline_counts(b):
    return {rid: {f: int(c) for f, c in v.items()} for rid, v in (b or {}).get("rules", {}).items()}


def collect_hits(index):
    hits = {r["id"]: {} for r in RULES}
    for path, rec in index.items():
        for rid, lns in rec["hits"].items():
            hits[rid][path] = lns
    return hits


def write_baseline(root, hits, path=None):
    data = {"about": "Known duplicate telltales that exist today (rule -> file -> count), every language. It may only shrink. "
                     "Regenerate after a dedup with: python3 scripts/dup_lint.py scan --update-baseline. See docs/process/no_duplicates.md.",
            "rules": {rid: {p: len(v) for p, v in sorted(by.items())} for rid, by in sorted(hits.items()) if by}}
    dest = path or os.path.join(root, BASELINE)
    with open(dest, "w") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    return dest


# ------------------------------------------------------------------------------------------ clone search


def extend(A, i, B, j, same):
    """Grow a match at A[i] == B[j] to its maximal length: (start in A, start in B, length)."""
    step = 64
    while i >= step and j >= step and A[i - step:i] == B[j - step:j]:
        i -= step; j -= step
    while i > 0 and j > 0 and A[i - 1] == B[j - 1]:
        i -= 1; j -= 1
    n, na, nb = 0, len(A), len(B)
    while i + n + step <= na and j + n + step <= nb and A[i + n:i + n + step] == B[j + n:j + n + step]:
        n += step
    while i + n < na and j + n < nb and A[i + n] == B[j + n]:
        n += 1
    if same and j > i:
        n = min(n, j - i)  # a block repeated right after itself: the two copies must not overlap
    return i, j, n


def find_pairs(index, loose, min_len, sources=None, cap=BUCKET_CAP):
    """Clone pairs (pathA, startA, pathB, startB, length) of at least min_len tokens.
    sources: {path: set(token positions)}: only start from fingerprints there (check mode); None = every pair (scan)."""
    tk, fk, pk = ("lo", "fl", "pl") if loose else ("ex", "fe", "pe")
    want = None
    if sources is not None:
        want = set()
        for p, ps in sources.items():
            r = index[p]
            want.update(h for h, i in zip(r[fk], r[pk]) if i in ps)
    buckets = defaultdict(list)
    for path, r in index.items():
        if fk not in r:
            continue
        for h, i in zip(r[fk], r[pk]):
            if want is None or h in want:
                buckets[h].append((path, i))
    seen = defaultdict(list)  # (pathA, pathB, diagonal) -> [(startA, endA)]
    pairs = []

    def try_pair(pa, ia, pb, ib):
        if (pa, ia) > (pb, ib):
            pa, ia, pb, ib = pb, ib, pa, ia
        key = (pa, pb, ib - ia)
        for s, e in seen[key]:
            if s <= ia < e:
                return
        sa, sb, n = extend(index[pa][tk], ia, index[pb][tk], ib, pa == pb)
        seen[key].append((sa, sa + max(n, 1)))
        if n >= min_len:
            pairs.append((pa, sa, pb, sb, n))

    if sources is None:
        for occ in buckets.values():
            if 2 <= len(occ) <= cap:
                for x in range(len(occ)):
                    for y in range(x + 1, len(occ)):
                        try_pair(*occ[x], *occ[y])
    else:
        for path, ps in sources.items():
            r = index[path]
            for h, i in zip(r[fk], r[pk]):
                if i in ps and len(buckets[h]) <= CHECK_CAP:
                    for pb, ib in buckets[h]:
                        if pb != path or ib != i:
                            try_pair(path, i, pb, ib)
    return pairs


# ------------------------------------------------------------------------------------------ shared_code.md check


def check_shared_doc(root, head, tree_paths, out):
    txt = read_blob(root, head, SHARED_DOC)
    if txt is None:
        return
    for ln, line in enumerate(txt.split("\n"), 1):
        if not line.startswith("|") or "planned" in line.lower():
            continue
        for name in re.findall(r"`([\w./-]+\.(?:hpp|h|cpp|inc|py|sh|yml))", line):
            if not any(p == name or p.endswith("/" + name) for p in tree_paths):
                out.emit("error", SHARED_DOC, ln, "shared code list",
                         f"names `{name}`, which is not in the tree. Fix the row, or mark it \"planned (dedup in progress)\".")


# ------------------------------------------------------------------------------------------ new names that look like old ones

NOT_TYPES = frozenset("""return else case new delete throw goto co_return co_await co_yield if while for switch using typedef namespace
sizeof decltype static_assert alignof typeid catch do""".split())
NAME_RE = {
    "c": re.compile(r"\b(?:class|struct|union|enum(?:\s+class)?)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(?:final\s*)?(?=[:{])"
                    r"|(?m:^[ \t]*((?:[\w:<>,*&]+[ \t]+)+)[*&]?([A-Za-z_]\w*)[ \t]*\([^;{}\n]*\)?[^;{}\n=]*[{;]?[ \t]*$)"),
    "js": re.compile(r"\bfunction\s+([A-Za-z_$][\w$]*)|\bclass\s+([A-Za-z_$][\w$]*)"),
    "py": re.compile(r"(?m)^[ \t]*(?:async\s+)?(?:def|class)\s+([A-Za-z_]\w*)"),
    "hash": re.compile(r"(?m)^[ \t]*(?:function\s+)?([A-Za-z_][\w-]*)\s*\(\)\s*\{"),
}
STOP_WORDS = frozenset("get of to the a an is do make compute calc find impl internal helper util utils".split())
OPPOSITES = [frozenset(p.split()) for p in """get set|add remove|begin end|open close|load save|push pop|enable disable|show hide|start stop|on off
min max|to from|encode decode|read write|in out|inc dec|lock unlock|attach detach|register unregister|insert erase|first last
left right|up down|front back|before after|enter exit|import export|pack unpack|serialize deserialize|width height|x y|src dst
begin finish|create destroy|alloc free|acquire release|send receive|play pause""".split("|")]
MIN_NAME = 4
CONVENTIONAL = 12  # a name defined in more files than this is a convention (Update, OnStart, Serialize), not a helper to reuse


def names_in(clean, fam, top_only=False):
    """[(name, line)] of the classes, structs and functions defined or declared in a file (comments already removed).
    top_only: only types, and functions that start at column 0 (free functions; not methods inside a class)."""
    rx = NAME_RE.get(fam)
    if rx is None:
        return []
    nls, out = line_starts(clean), []
    for m in rx.finditer(clean):
        if top_only:
            col0 = m.start() == 0 or clean[m.start() - 1] == "\n"
            is_type = clean.startswith(("class", "struct", "union", "enum"), m.start()) or (fam == "c" and m.group(1))
            if fam == "py":
                is_type = m.group(0).lstrip().startswith("class")
            if not (is_type or (col0 and not clean[m.start():m.start() + 1].isspace())):
                continue
        if fam == "c":
            if m.group(1):
                name = m.group(1)
            else:
                first = m.group(2).split()[0]
                if first in NOT_TYPES or first.isupper():
                    continue
                name = m.group(3)
                if name.isupper():
                    continue  # a macro
        else:
            name = next((g for g in m.groups() if g), None)
        if name and len(name) >= MIN_NAME and name not in KEYWORD_SET:
            out.append((name, bisect.bisect_left(nls, m.start(m.lastindex)) + 1))
    return out


def name_words(name):
    parts = re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|\d+", name.replace("-", "_"))
    return [p.lower() for p in parts]


def name_group(path):
    """Names are only compared within one language group: a Python helper is not a C++ one."""
    lf = lang_of(path)
    return {"c": "c:", "js": "j:", "py": "p:"}.get(lf[1] if lf else "", "o:")


def name_key(name):
    return name.replace("_", "").replace("-", "").lower()


def edit_distance(a, b, limit=2):
    """Levenshtein distance, or limit + 1 once it is certainly larger."""
    if abs(len(a) - len(b)) > limit:
        return limit + 1
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i] + [0] * len(b)
        lo = i
        for j, cb in enumerate(b, 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb))
            lo = min(lo, cur[j])
        if lo > limit:
            return limit + 1
        prev = cur
    return prev[-1]


def opposites_only(a, b):
    """True when two names differ only by a pair of opposite words (Get/Set, Load/Save), a number or a plural s."""
    wa, wb = name_words(a), name_words(b)
    da, db = [w for w in wa if w not in wb], [w for w in wb if w not in wa]
    if not da and not db:
        return False
    if all(w.isdigit() for w in da + db):
        return True
    if len(da) == 1 and len(db) == 1:
        x, y = da[0], db[0]
        if frozenset((x, y)) in OPPOSITES or x.rstrip("s") == y.rstrip("s") or x.isdigit() and y.isdigit():
            return True
    return False


class NameIndex:
    """Existing names, searchable for equal or near-equal ones."""
    def __init__(self):
        self.by_key, self.by_words, self.grams, self.files = defaultdict(list), defaultdict(list), defaultdict(set), defaultdict(set)

    def add(self, name, path, line):
        k = name_group(path) + name_key(name)
        self.files[name].add(path)
        if len(self.files[name]) > 1 and any(p == path for p, _l, _n in self.by_key.get(k, ())):
            return
        self.by_key[k].append((path, line, name))
        ws = (name_group(path),) + tuple(sorted(set(name_words(name)) - STOP_WORDS))
        if len(ws) > 1:
            self.by_words[ws].append((path, line, name))
        if len(k) >= 9:
            for i in range(len(k) - 1):
                self.grams[k[i:i + 2]].add(k)

    def similar(self, name, own_path, group=None):
        """[(path, line, existing name, why)] for an added name, best first. group: "c:", "p:", "j:" when there is no path."""
        group = group or name_group(own_path)
        k = group + name_key(name)
        found, seen = [], set()

        def take(rows, why):
            for p, l, n in rows:
                if p == own_path or (p, n) in seen or len(self.files[n]) > CONVENTIONAL:
                    continue  # same file (an overload), or a conventional name defined everywhere (Update, Serialize)
                if why != "same name" and opposites_only(name, n):
                    continue
                seen.add((p, n))
                found.append((p, l, n, why))
        take(self.by_key.get(k, ()), "same name" if any(n == name for _p, _l, n in self.by_key.get(k, ())) else "same name ignoring case and _")
        ws = (group,) + tuple(sorted(set(name_words(name)) - STOP_WORDS))
        if len(ws) > 1:
            take(self.by_words.get(ws, ()), "same words")
        if len(k) >= 9:
            counts = defaultdict(int)
            for i in range(len(k) - 1):
                for other in self.grams.get(k[i:i + 2], ()):
                    counts[other] += 1
            lim = 1 if len(k) < 13 else 2  # short names: one letter apart; longer: two
            need = len(k) - 1 - 2 * lim  # each edit changes at most two of the two-letter pieces
            for other, c in counts.items():
                if c >= need and other != k and edit_distance(k, other, lim) <= lim:
                    take(self.by_key[other], "spelled almost the same")
        return found[:3]


def new_names_report(tree_base_names, changed_names, added, out, limit=40):
    """Warn for each added class/function name that equals or nearly equals an existing one."""
    n = 0
    for path, rows in sorted(changed_names.items()):
        add = added.get(path, ())
        for name, line in rows:
            if line not in add:
                continue
            for p, l, other, why in tree_base_names.similar(name, path):
                n += 1
                if n <= limit:
                    out.emit("warning", path, line, "similar name",
                             f"new name {name} looks like the existing {other} at {p}:{l} ({why}). Did you mean to reuse {other}? "
                             f"Search {CODE_INDEX} and {SHARED_DOC} before adding a new helper.")
                break
    if n > limit:
        print(f"... and {n - limit} more similar-name warnings", file=sys.stderr)
    return n


def only_lines(text, keep):
    """The text with every line not in `keep` emptied (line numbers stay right): to look at added lines only."""
    return "\n".join(l if i in keep else "" for i, l in enumerate(text.split("\n"), 1))


def names_one(item):
    path, text = item
    fam = lang_of(path)[1]
    return path, names_in(text, fam) if fam in NAME_RE else []  # existing names: comments are not stripped (faster; a name in a comment is harmless)


def all_names(files, jobs):
    items = [(p, t) for p, t in files.items() if lang_of(p)[1] in NAME_RE]
    if jobs > 1 and len(items) > 16:
        with multiprocessing.get_context("fork").Pool(jobs) as pool:
            return dict(pool.map(names_one, items, chunksize=16))
    return dict(map(names_one, items))


# ------------------------------------------------------------------------------------------ retired (deprecated) names

DEPRECATED_RE = re.compile(
    r"(?:class|struct)\s+(?:OKAY_DEPRECATED\s*\((?:[^()\"]|\"(?:\\.|[^\"\\])*\")*\)|\[\[\s*deprecated[^\]]*\]\])\s*([A-Za-z_]\w*)"
    r"|(?:OKAY_DEPRECATED\s*\((?:[^()\"]|\"(?:\\.|[^\"\\])*\")*\)|\[\[\s*deprecated[^\]]*\]\])\s*(?:template\s*<[^;{>]*>\s*)?[^;{(=]*?\b([A-Za-z_]\w*)\s*[(=;{]")


def deprecated_names(root, rev):
    """{name: (path, line)} of everything marked OKAY_DEPRECATED or [[deprecated]] at `rev`."""
    p = subprocess.run(["git", "-C", root, "grep", "-l", "-z", "-E", r"OKAY_DEPRECATED *\(|\[\[ *deprecated", rev, "--"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pre = rev + ":"
    paths = [x[len(pre):] if x.startswith(pre) else x for x in p.stdout.decode("utf-8", "replace").split("\0") if x]
    out = {}
    for path in paths:
        lf = lang_of(path)
        if not lf or lf[1] not in C_LIKE or path == "engine/include/okay/Core/Deprecated.hpp":
            continue
        text = read_blob(root, rev, path) or ""
        clean = strip_comments(text, lf[1])
        nls = line_starts(clean)
        for m in DEPRECATED_RE.finditer(clean):
            name = m.group(1) or m.group(2)
            if name and name not in KEYWORD_SET:
                out.setdefault(name, (path, bisect.bisect_left(nls, m.start()) + 1))
    return out


def deprecated_uses(files, added, retired, out):
    """Errors for added lines (C-like code) that use a name already retired on main."""
    if not retired:
        return 0
    rx = re.compile(r"\b(" + "|".join(sorted(map(re.escape, retired), key=len, reverse=True)) + r")\b")
    n = 0
    for path, text in files.items():
        lf = lang_of(path)
        if not lf or lf[1] not in C_LIKE or path not in added:
            continue
        lines = strip_comments(text, lf[1]).split("\n")
        for ln in sorted(added[path]):
            if ln > len(lines):
                continue
            s = lines[ln - 1]
            m = rx.search(s)
            if not m or "OKAY_DEPRECATED" in s or "deprecated" in s:
                continue
            p, l = retired[m.group(1)]
            n += 1
            out.emit("error", path, ln, "retired name",
                     f"{m.group(1)} was retired (marked OKAY_DEPRECATED at {p}:{l}); a dedup replaced it. Use what its message names instead.")
    return n


# ------------------------------------------------------------------------------------------ copies between open branches


_main_text = {}


def on_main(root, base, ref, path, rec, start, n):
    """True when the lines of this match are already, word for word, in the file on main."""
    lo, hi = span_lines(rec, start, n)
    if (ref, path) not in _main_text:
        _main_text[(ref, path)] = (read_blob(root, ref, path) or "", read_blob(root, base, path) or "")
    mine, main = _main_text[(ref, path)]
    block = "\n".join(l.strip() for l in mine.split("\n")[lo - 1:hi])
    return bool(main) and block in "\n".join(l.strip() for l in main.split("\n"))


def cross_branch(root, base, refs, focus=None, jobs=1, stacked=None):
    """Exact clones and near-equal new names between the open branches.
    refs: {branch: ref}. focus: only findings that involve this branch (fast: other branches' files are only read in full
    when they share every word of one of its added blocks). stacked(a, b): True to skip a pair (one built on the other).
    Returns {"clones": [...], "names": [...]}."""
    texts, added_all, names = {}, {}, defaultdict(list)
    for br, ref in refs.items():
        try:
            added = added_line_map(root, base, ref)
        except RuntimeError:
            continue
        files = read_tree(root, ref, names=sorted(p for p in added if lang_of(p) and not excluded(p)))
        for p, t in files.items():
            key = br + "\0" + p
            t = only_lines(t, added[p])  # a copy between branches is added on both sides: only added lines matter
            texts[key], added_all[key] = t, added[p]
            fam = lang_of(p)[1]
            if fam in NAME_RE:
                for name, line in names_in(strip_comments(t, fam), fam, True):
                    if len(name_words(name)) >= 2:
                        names[br].append((name, p, line))
    if focus is None:
        index = index_files(texts, jobs, 1)
    else:
        mine = {k: t for k, t in texts.items() if k.split("\0", 1)[0] == focus}
        index = index_files(mine, jobs, 2)
        windows = []
        for k, rec in index.items():
            srcs = {i for i, x in enumerate(rec["ln"]) if x in added_all[k]}
            windows += [w for w in window_words(rec, srcs) if w[0]]
        others = {k: set(WORD_RE.findall(t)) for k, t in texts.items() if k not in mine}
        index.update(index_files({k: texts[k] for k in candidate_files(windows, others)}, jobs, 1))
    sources = {}
    for key, rec in index.items():
        if focus is None or key.split("\0", 1)[0] == focus:
            add = added_all[key]
            sources[key] = {i for i, x in enumerate(rec["ln"]) if x in add}
    clones = []
    for ka, sa, kb, sb, n in find_pairs(index, False, N, sources, cap=CHECK_CAP):
        ba, pa = ka.split("\0", 1)
        bb, pb = kb.split("\0", 1)
        if ba == bb or (stacked and stacked(ba, bb)):
            continue
        ok = True
        for key, s in ((ka, sa), (kb, sb)):
            ln, add = index[key]["ln"], added_all[key]
            best = run = 0
            for i in range(s, s + n):
                run = run + 1 if ln[i] in add else 0
                best = max(best, run)
            ok = ok and best >= N
        if ok and any(on_main(root, base, refs[x], f, index[k], st, n) for x, f, k, st in ((ba, pa, ka, sa), (bb, pb, kb, sb))):
            ok = False  # one side is already on main (a branch that was squash-merged): `check` reports a paste from main
        if ok:
            clones.append({"a": ba, "a_file": pa, "a_lines": list(span_lines(index[ka], sa, n)),
                           "b": bb, "b_file": pb, "b_lines": list(span_lines(index[kb], sb, n)), "tokens": n})
    seen_pairs, out_names = set(), []
    brs = sorted(names)
    for ba in brs:
        if focus and ba != focus:
            continue
        ni = NameIndex()
        for name, p, line in names[ba]:
            ni.add(name, p, line)
        for bb in brs:
            if bb == ba or (stacked and stacked(ba, bb)):
                continue
            for name, p, line in names[bb]:
                for op, ol, other, why in ni.similar(name, p):
                    key = tuple(sorted(((ba, other), (bb, name))))
                    if key in seen_pairs:
                        continue
                    seen_pairs.add(key)
                    out_names.append({"a": ba, "a_name": other, "a_where": f"{op}:{ol}", "b": bb, "b_name": name, "b_where": f"{p}:{line}", "why": why})
    return {"clones": clones, "names": out_names}


# ------------------------------------------------------------------------------------------ system names (rule 19e)

BANNED_WORDS = ("own", "new", "improved", "classic")
SYSTEM_DIRS = ("engine/include/okay/", "engine/src/", "editor/")  # a new folder directly under one is a new system
NAME_SKIP = ("docs/", "tests/data/", "tests/assets/", "tests/golden/", "examples/")  # data and prose, not code names


def banned_qualifier(name):
    """The banned qualifier a file, folder or code name uses as a word (Own, New, Improved, Classic, V2), or None."""
    words = name_words(os.path.splitext(name)[0] if "." in name[1:] else name)
    for i, w in enumerate(words):
        if w in BANNED_WORDS:
            return w.capitalize()
        if w == "v" and i + 1 < len(words) and words[i + 1] == "2" or w == "v2":
            return "V2"
    return None


def registry_paths(root, head):
    """(homes, named): the Home folders and files of every system, and the paths named exactly in a Home or Entry, from the
    feature map pages at `head` (scripts/gen_feature_map.py reads them)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gen_feature_map as G
    names = [p for p in git(root, "ls-tree", "--name-only", head, "docs/feature_map/").split("\n") if p.endswith(".md")]
    pages = {os.path.basename(p): read_blob(root, head, p) or "" for p in names}
    homes, named = set(), set()
    for e in G.registry(root, pages):
        for p in e["home"]:
            homes.add(p.rstrip("/"))
            named.add(p.rstrip("/"))
        if e["entry"]:
            named.add(e["entry"].rstrip("/"))
    return homes, named


def check_system_names(root, base, head, out):
    """New files and folders: no banned qualifier in the name; a new top-level or system folder needs a registry Home."""
    added = [p for p in git(root, "diff", "--name-only", "--no-renames", "--diff-filter=A", f"{base}...{head}").split("\n") if p]
    if not added:
        return 0
    base_paths = git(root, "ls-tree", "-r", "--name-only", base).split("\n")
    base_dirs = {"/".join(p.split("/")[:i]) for p in base_paths if p for i in range(1, p.count("/") + 1)}
    homes, named = registry_paths(root, head)
    import fnmatch

    def is_named(path):
        return any(path == n or fnmatch.fnmatch(path, n) for n in named)

    def covered(folder):  # a Home is this folder, or inside it, or holds it
        return any(h == folder or h.startswith(folder + "/") or folder.startswith(h + "/") for h in homes)

    def allowed(path):
        txt = read_blob(root, head, path) or ""
        return any(m.group(1).strip() for m in re.finditer(r"dup-allow:([^\n]*)", "\n".join(txt.split("\n")[:15])))

    n, seen = 0, set()
    for path in added:
        if excluded(path) or path.startswith(NAME_SKIP):
            continue
        parts = path.split("/")
        msg = None
        for i in range(1, len(parts)):
            folder = "/".join(parts[:i])
            if folder in base_dirs or folder in seen:
                continue
            q = banned_qualifier(parts[i - 1])
            if q:
                msg = (f"new folder {folder}/ has '{q}' in its name. One plain name per system (CLAUDE.md rule 19e): ours IS the "
                       f"system, so no Own/New/Improved/Classic/V2. Find the existing system: python3 scripts/find_existing.py <words>")
            elif i == 1 or any(folder.startswith(d) and "/" not in folder[len(d):] for d in SYSTEM_DIRS):
                if not covered(folder):
                    kind = "top-level folder" if i == 1 else "system folder"
                    msg = (f"new {kind} {folder}/ is not in the system registry. Check docs/SYSTEMS.md (python3 scripts/find_existing.py "
                           f"<words>) and add on to the system that does the job; if it really is a new system, give it a "
                           f"`- **System:**` line with `Home: `{folder}/`` in docs/feature_map/ (format: docs/feature_map/README.md)")
            if msg:
                seen.add(folder)
                break
        if msg is None and lang_of(path):
            q = banned_qualifier(parts[-1])
            if q and not is_named(path):
                msg = (f"new file {parts[-1]} has '{q}' in its name. One plain name per system (CLAUDE.md rule 19e): name it for what "
                       f"it does, and add on to the existing system (python3 scripts/find_existing.py <words>)")
        if msg and not is_named(path) and not allowed(path):
            out.emit("error", path, 1, "system name", msg + ". A real need: `dup-allow: <reason>` in the file's first 15 lines.")
            n += 1
    return n


def banned_names_report(changed_names, added, out):
    """Warn for an added class or function whose name has a banned qualifier."""
    n = 0
    for path, rows in sorted(changed_names.items()):
        for name, line in rows:
            q = banned_qualifier(name)
            if q and line in added.get(path, ()):
                n += 1
                out.emit("warning", path, line, "system name",
                         f"new name {name} has '{q}' in it. Name it for what it does, not for being ours, new or improved (CLAUDE.md rule 19e).")
    return n


# ------------------------------------------------------------------------------------------ reporting


class Out:
    def __init__(self):
        self.errors = self.warnings = 0

    def emit(self, sev, path, line, title, msg):
        print(f"::{sev} file={path},line={line},title={title}::{msg}")
        print(f"  {sev.upper()} {title} {path}:{line}: {msg}", file=sys.stderr)
        self.errors += sev == "error"
        self.warnings += sev != "error"


def span_lines(rec, s, n):
    ln = rec["ln"]
    return ln[s], ln[min(s + n - 1, len(ln) - 1)]


def run_scan(a, root):
    t0 = time.time()
    stats = {"generated": []}
    files = read_tree(root, a.head, stats)
    t_read = time.time() - t0
    index = index_files(files, a.jobs, not a.no_clones)
    t_index = time.time() - t0 - t_read
    hits = collect_hits(index)
    per_lang = defaultdict(lambda: {"files": 0, "kb": 0, "tokens": 0, "hits": 0, "pairs": 0, "loose": 0})
    for p, t in files.items():
        L = per_lang[lang_of(p)[0]]
        L["files"] += 1; L["kb"] += len(t) // 1024
        L["tokens"] += len(index[p].get("ex", ()))
        L["hits"] += sum(len(v) for v in index[p]["hits"].values())
    print(f"dup_lint scan of {a.head}: {len(files)} code files, {sum(len(t) for t in files.values()) // 1024} KB "
          f"({len(stats['generated'])} generated files skipped; excluded folders: {', '.join(p for p, _ in EXCLUDE)})")
    print("\nFingerprint rules (hits outside the shared file):")
    for r in RULES:
        by = hits[r["id"]]
        print(f"  {r['id']:24s} {sum(len(v) for v in by.values()):4d} hits in {len(by):3d} files   ({r['what']})")
    if a.update_baseline:
        print("\nbaseline written: " + write_baseline(root, hits, a.baseline))
    exact, loose = [], []
    if not a.no_clones:
        exact = sorted(find_pairs(index, False, a.min_tokens), key=lambda p: -p[4])
        if a.loose:
            loose = find_pairs(index, True, LOOSE_N)
        for pa, _sa, pb, _sb, _n in exact:
            per_lang[lang_of(pa)[0]]["pairs"] += 1
        ex_keys = {(p[0], p[2]) for p in exact}
        for pa, _sa, pb, _sb, _n in loose:
            if (pa, pb) not in ex_keys:
                per_lang[lang_of(pa)[0]]["loose"] += 1
        print(f"\nExact copy-paste pairs of {a.min_tokens}+ tokens (K={K}, P={P}): {len(exact)}")
        if a.histogram:
            for lim in (60, 80, 100, 120, 160, 200, 300, 500):
                print(f"    >= {lim:3d} tokens: {sum(1 for p in exact if p[4] >= lim)} pairs")
        for pa, sa, pb, sb, n in exact[:a.top]:
            la, lb = span_lines(index[pa], sa, n), span_lines(index[pb], sb, n)
            print(f"  {n:6d} tokens  {pa}:{la[0]}-{la[1]}  ==  {pb}:{lb[0]}-{lb[1]}")
        if a.loose:
            print(f"\nLoose (renamed) pairs of {LOOSE_N}+ tokens between files with no exact pair: {sum(v['loose'] for v in per_lang.values())}")
    print("\nPer language:")
    print(f"  {'language':20s} {'files':>6s} {'KB':>7s} {'tokens':>9s} {'rule hits':>9s} {'exact pairs':>11s}" + ("  loose-only" if a.loose else ""))
    for lang, L in sorted(per_lang.items(), key=lambda kv: -kv[1]["kb"]):
        print(f"  {lang:20s} {L['files']:6d} {L['kb']:7d} {L['tokens']:9d} {L['hits']:9d} {L['pairs']:11d}" + (f"  {L['loose']:10d}" if a.loose else ""))
    print(f"\ntime: read {t_read:.1f}s, tokens+hashes+rules {t_index:.1f}s ({a.jobs} jobs), total {time.time() - t0:.1f}s")
    rc = 0
    if a.fail_over_baseline:  # the weekly scan: did a copy land some other way, or did the baseline grow?
        base = baseline_counts(load_baseline(root, a.head, a.baseline)[0])
        over = [(rid, p, len(v), base.get(rid, {}).get(p, 0)) for rid, by in hits.items() for p, v in by.items() if len(v) > base.get(rid, {}).get(p, 0)]
        for rid, p, now, was in over:
            print(f"::error file={p},title=over the duplicate baseline::{rid}: {now} here, baseline {was}")
        old_rev = git(root, "rev-list", "-1", f"--before={a.fail_over_baseline} days ago", a.head).strip()
        old_txt = read_blob(root, old_rev, BASELINE) if old_rev else None
        grew = []
        if old_txt:
            old = baseline_counts(json.loads(old_txt))
            grew = [(rid, p, c, old.get(rid, {}).get(p, 0)) for rid, by in base.items() for p, c in by.items() if c > old.get(rid, {}).get(p, 0)]
            for rid, p, c, o in grew:
                print(f"::error file={BASELINE},title=baseline grew::{rid} for {p}: {o} -> {c} in the last {a.fail_over_baseline} days")
        print(f"\nbaseline: {len(over)} entries over it; {len(grew)} entries grew in the last {a.fail_over_baseline} days"
              + ("" if old_txt else " (no baseline that old to compare with)"))
        rc = 1 if over or grew else 0
    return rc


WORD_RE = re.compile(r"[A-Za-z_]\w{3,}")


def window_words(rec, sources):
    """[(exact?, words)] for every added fingerprint window: its distinct words of 4+ letters."""
    tk, out = rec["tk"], []
    for key, exact in (("pe", True), ("pl", False)):
        for i in rec[key]:
            if i in sources:
                ws = frozenset(t for t in tk[i:i + K] if len(t) >= 4 and (t[0].isalpha() or t[0] == "_"))
                if ws:
                    out.append((exact, ws))
    return out


def candidate_files(windows, wordsets):
    """Files that may hold a copy of an added window. Exact: the file has every word of the window (so a real copy is
    never missed). Loose (renamed names): the file has at least three of the window's five rarest words (best effort)."""
    need = frozenset().union(*(ws for _e, ws in windows)) if windows else frozenset()
    post = defaultdict(set)
    for p, ws in wordsets.items():
        for w in ws & need:
            post[w].add(p)
    cands = set()
    for exact, ws in set(windows):
        order = sorted(ws, key=lambda w: len(post[w]))
        if exact:
            c = set(post[order[0]])
            for w in order[1:]:
                if not c:
                    break
                c &= post[w]
            if len(c) <= WINDOW_CAP:
                cands |= c  # a window made only of common words (auto, const, first) is skipped: the block's other windows find its copy
        else:
            rare = [w for w in order if len(post[w]) <= LOOSE_RARE][:5]
            if len(rare) >= 3:
                seen = defaultdict(int)
                for w in rare:
                    for p in post[w]:
                        seen[p] += 1
                cands.update(p for p, c in seen.items() if c >= 3)
    return cands


def run_check(a, root):
    t0 = time.time()
    out = Out()
    added = added_line_map(root, a.base, a.head)
    changed = set(git(root, "diff", "--name-only", "--no-renames", f"{a.base}...{a.head}").split("\n")) - {""}
    tree = read_tree(root, a.head)
    files = {p: t for p, t in tree.items() if p in changed}
    touched = {p for p in added if p in files}
    index = index_files(files, a.jobs, 2 if touched else 0)

    for p, rec in index.items():
        for ln, msg in rec["bad"]:
            if ln in added.get(p, ()):
                out.emit("error", p, ln, "dup-allow", msg)

    # 1. fingerprints of the changed files against the baseline (other files are the same as on main)
    hits = collect_hits(index)
    base_json, base_src = load_baseline(root, a.head, a.baseline)
    base = baseline_counts(base_json)
    shrink = []
    for r in RULES:
        rid, known = r["id"], base.get(r["id"], {})
        for path, lns in sorted(hits[rid].items()):
            was = known.get(path, 0)
            if len(lns) <= was:
                continue
            new_lns = [ln for ln in lns if ln in added.get(path, ())] or lns
            out.emit("error", path, new_lns[0], f"duplicate {rid}",
                     f"a new {r['what']} ({len(lns)} in this file, baseline {was}). Use {r['use']} instead "
                     f"({SHARED_DOC}). If a copy is really needed, add `dup-allow: <reason>` on the line.")
        for path, was in known.items():
            if path in changed:
                now = len(hits[rid].get(path, ()))
                if now < was:
                    shrink.append(f"{rid} {path}: {was} -> {now}")
    if base_src == BASELINE:
        old_txt = read_blob(root, a.base, BASELINE)
        if old_txt:
            old = baseline_counts(json.loads(old_txt))
            for rid, by in base.items():
                for path, c in by.items():
                    o = old.get(rid, {}).get(path, 0)
                    if c > o:
                        out.emit("error", BASELINE, 1, "baseline grew",
                                 f"raises {rid} for {path} ({o} -> {c}). The baseline may only shrink: use the shared code, "
                                 f"or mark a real need with `dup-allow: <reason>` on the line.")
    if shrink:
        print(f"baseline can shrink ({len(shrink)} entries went down; good, a duplicate went away):", file=sys.stderr)
        for s in shrink[:15]:
            print("  " + s, file=sys.stderr)
        print("  run: python3 scripts/dup_lint.py scan --update-baseline   and commit scripts/dup_baseline.json", file=sys.stderr)

    # 2. no new use of a retired (OKAY_DEPRECATED) name; new names that look like existing ones (warnings)
    retired = deprecated_names(root, a.base)
    deprecated_uses(files, added, retired, out)
    nnames = 0
    if not a.no_names:
        changed_names = {p: [(n_, l) for n_, l in names_in(strip_comments(only_lines(t, added.get(p, ())), lang_of(p)[1]), lang_of(p)[1], True)
                             if len(name_words(n_)) >= 2]  # one-word names (Frame, Query) are too generic
                         for p, t in files.items() if lang_of(p)[1] in NAME_RE}
        changed_names = {p: v for p, v in changed_names.items() if v}
        if changed_names:
            base_paths = set(code_paths(root, a.base))
            base_files = {p: t for p, t in tree.items() if p not in files}  # unchanged files are as on main
            base_files.update(read_tree(root, a.base, names=sorted(p for p in files if p in base_paths)))  # changed ones: main's copy
            ni = NameIndex()
            for p, rows in all_names(base_files, a.jobs).items():
                for n_, l in rows:
                    ni.add(n_, p, l)
            nnames = new_names_report(ni, changed_names, added, out)
            banned_names_report(changed_names, added, out)

    # 3. the shared code list names real files
    check_shared_doc(root, a.head, set(git(root, "ls-tree", "-r", "--name-only", a.head).split("\n")), out)

    # 4. system names: no Own/New/Improved/Classic/V2 in new file and folder names; a new system folder needs a registry entry
    nsys = check_system_names(root, a.base, a.head, out)

    # 5. copy-paste in what the branch adds: only files that share a word with an added block can hold a copy of it
    t1 = time.time()
    found, ncand = [0, 0], 0
    if touched:
        sources, windows = {}, []
        for p in touched:
            add, ln = added[p], index[p]["ln"]
            sources[p] = {i for i, x in enumerate(ln) if x in add}
            windows += window_words(index[p], sources[p])
        wordsets = {p: set(WORD_RE.findall(t)) for p, t in tree.items() if p not in files}
        cand = candidate_files(windows, wordsets)
        more = {p: tree[p] for p in cand}
        ncand = len(more)
        global ONLY_KEYS
        ONLY_KEYS = {"ex": set(), "lo": set()}
        for p in touched:
            r = index[p]
            for tk_, pk, key in (("ex", "pe", "ex"), ("lo", "pl", "lo")):
                ONLY_KEYS[key].update(quick_key(r[tk_], i) for i in r[pk] if i in sources[p])
        index.update(index_files(more, a.jobs, 1))
        reported = []
        for loose, min_len, sev, kind in ((False, a.min_tokens, "error", "pasted"), (True, LOOSE_N, "warning", "near copy, renamed")):
            for pa, sa, pb, sb, n in sorted(find_pairs(index, loose, min_len, sources), key=lambda q: -q[4]):
                runs = []
                for p, s in ((pa, sa), (pb, sb)):
                    best = run = 0
                    if p in touched:
                        add, ln = added[p], index[p]["ln"]
                        for i in range(s, s + n):
                            run = run + 1 if ln[i] in add else 0
                            best = max(best, run)
                    runs.append(best)
                if max(runs) < min_len:
                    continue  # the branch only touched a line inside an old duplicate
                both_new = min(runs) >= min_len
                if both_new and pa == pb:
                    continue  # two look-alike blocks inside one new piece of code
                np_, ns, op, os_ = (pa, sa, pb, sb) if runs[0] >= runs[1] else (pb, sb, pa, sa)
                l_new, l_old = span_lines(index[np_], ns, n), span_lines(index[op], os_, n)
                if any(r[0] == np_ and r[1] <= l_new[1] and l_new[0] <= r[2] for r in reported):
                    continue
                ok = index[np_]["allowed"]
                if any(x in ok for x in range(l_new[0], l_new[1] + 1)):
                    continue
                reported.append((np_, l_new[0], l_new[1]))
                found[loose] += 1
                where = "another block this branch adds" if both_new else "existing code"
                out.emit(sev, np_, l_new[0], f"copy-paste ({kind})",
                         f"lines {l_new[0]}-{l_new[1]} ({n} tokens) {'are a copy' if not loose else 'look like a copy with renamed names'} of {where} at "
                         f"{op}:{l_old[0]}-{l_old[1]}. Move it into one shared function and call it from both places ({SHARED_DOC}). "
                         f"A real need for a copy: `dup-allow: <reason>` on one of the lines.")
    print(f"dup_lint check {a.base}...{a.head}: {len(touched)} changed code files, {ncand} other files share words with the added code; "
          f"{found[0]} pasted and {found[1]} renamed copies; {nnames} similar names; {nsys} system-name errors; {out.errors} error(s), {out.warnings} warning(s); "
          f"{time.time() - t0:.1f}s (clone search {time.time() - t1:.1f}s)", file=sys.stderr)
    if a.update_baseline:
        print("note: check only reads the changed files; use `scan --update-baseline` to rewrite the baseline", file=sys.stderr)
    return 1 if out.errors else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["check", "scan"])
    ap.add_argument("--root", default=".")
    ap.add_argument("--base", default="origin/main", help="check: compare against this (default origin/main)")
    ap.add_argument("--head", default="HEAD", help="the commit to read (default HEAD)")
    ap.add_argument("--baseline", help=f"baseline file (default: {BASELINE} at --head, else the working tree)")
    ap.add_argument("--update-baseline", action="store_true", help="rewrite the baseline from the hits found")
    ap.add_argument("--min-tokens", type=int, default=N, help=f"shortest exact match reported (default {N})")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--loose", action="store_true", help="scan: also count loose (renamed) pairs")
    ap.add_argument("--histogram", action="store_true", help="scan: pair counts at several lengths")
    ap.add_argument("--no-clones", action="store_true", help="scan: fingerprint rules only")
    ap.add_argument("--no-names", action="store_true", help="check: skip the similar-name warnings")
    ap.add_argument("--fail-over-baseline", type=int, metavar="DAYS",
                    help="scan: exit 1 if the tree is over the baseline, or the baseline grew in the last DAYS days (weekly workflow)")
    a = ap.parse_args()
    root = git(os.path.abspath(a.root), "rev-parse", "--show-toplevel").strip()
    return run_scan(a, root) if a.mode == "scan" else run_check(a, root)


if __name__ == "__main__":
    sys.exit(main())
