#!/usr/bin/env python3
"""Shared helpers for the collaboration scripts (find_existing, collision_check, branch_lint, bug_patterns).

Standard library and git only. Plain rules: docs/collaboration.md. Lanes table: docs/lanes.json.
Written from our own design; no code from another project.
"""
import datetime, fnmatch, json, os, re, subprocess, sys, urllib.request

REPO_DEFAULT = "kingimann/OkaySpaceGameEngine"
CLAIM_ISSUE = 422
CLAIM_EXPIRY_HOURS = 24


def run(args, cwd=None, check=False):
    """Run a command, return stdout text ('' on failure unless check)."""
    p = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
    if check and p.returncode:
        raise RuntimeError(f"{' '.join(args)}: {p.stderr.strip()}")
    return p.stdout


def git(root, *args):
    return run(["git", "-C", root, *args])


def repo_root(start="."):
    out = run(["git", "-C", start, "rev-parse", "--show-toplevel"]).strip()
    return out or os.path.abspath(start)


# ---------------------------------------------------------------- globs

_glob_cache = {}


def glob_to_re(g):
    """Glob with ** (any depth), * (no slash), ? and [abc] -> compiled regex."""
    if g in _glob_cache:
        return _glob_cache[g]
    i, out = 0, ""
    while i < len(g):
        c = g[i]
        if g.startswith("**/", i):
            out += "(?:.*/)?"; i += 3; continue
        if g.startswith("**", i):
            out += ".*"; i += 2; continue
        if c == "*": out += "[^/]*"
        elif c == "?": out += "[^/]"
        elif c == "[":
            j = g.find("]", i)
            if j < 0: out += "\\["
            else: out += g[i:j + 1]; i = j
        else: out += re.escape(c)
        i += 1
    rx = re.compile("^" + out + "$")
    _glob_cache[g] = rx
    return rx


def glob_match(path, g):
    return bool(glob_to_re(g).match(path))


def globs_overlap(a, b, files=None):
    """Could two globs name the same file? Uses the real file list when given, else a prefix test."""
    if a == b:
        return True
    if glob_match(a, b) or glob_match(b, a):
        return True
    if files is not None:
        ra, rb = glob_to_re(a), glob_to_re(b)
        return any(ra.match(f) and rb.match(f) for f in files)
    pa, pb = re.split(r"[*?\[]", a)[0], re.split(r"[*?\[]", b)[0]
    return pa.startswith(pb) or pb.startswith(pa)


# ---------------------------------------------------------------- lanes

def load_lanes(root):
    p = os.path.join(root, "docs", "lanes.json")
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def owner_of(path, lanes):
    """First matching rule's owner: claude2, coordinator, shared or open."""
    for r in lanes["rules"]:
        if any(glob_match(path, g) for g in r["globs"]):
            return r["owner"]
    return "open"


def lane_of_branch(name):
    n = name[len("origin/"):] if name.startswith("origin/") else name
    if n.startswith("claude2/"): return "claude2"
    if n.startswith("claude/"): return "coordinator"
    return None


def generated_entry(path, lanes):
    for g in lanes.get("generated", []):
        if g["file"] == path:
            return g
    return None


# ---------------------------------------------------------------- claims

CLAIM_RE = re.compile(r"^[\s>*#_-]*(Claim|Release|Agree)(?:\s+and\s+result)?\s*:\s*[*_`\s]*([A-Za-z0-9_./-]+)", re.I | re.M)
FIELD_KEYS = ["Existing", "Gap", "Files", "Test-first", "Try-it"]
FIELD_RE = re.compile(r"(?:^|\s/\s|\n)[\s>*_-]*(Existing|Gap|Files|Test-first|Try-it|Kind)\s*(?:\*\*|__)?\s*:\s*(?:\*\*|__)?\s*", re.I)


def norm_branch(b):
    b = b.strip("`*_ .,;")
    return b[len("origin/"):] if b.startswith("origin/") else b


def split_fields(text):
    """Cut 'Existing: .. / Gap: .. / Files: ..' (one line or many) into a dict."""
    ms = list(FIELD_RE.finditer(text))
    out = {}
    for i, m in enumerate(ms):
        end = ms[i + 1].start() if i + 1 < len(ms) else len(text)
        out[m.group(1).lower()] = text[m.end():end].strip(" \n\t")
    return out


def split_globs(s):
    s = re.sub(r"\([^)]*\)", " ", s)
    parts = re.split(r"[,\s;]+", s.replace("`", " "))
    return [p.strip("*_.") if not p.endswith("*") else p.strip("_") for p in parts if re.search(r"[A-Za-z0-9_*]/|\.[a-z]{1,4}$|\*", p)]


def parse_claims(comments):
    """comments: list of {body, created_at, html_url, user?}. Returns (claims dict branch->claim, events list).
    A later Release removes a claim; Agree marks the claim as agreed by the other lane. Oldest first."""
    claims, events = {}, []
    for c in sorted(comments, key=lambda x: x.get("created_at", "")):
        body = c.get("body") or ""
        for m in CLAIM_RE.finditer(body):
            kind, br = m.group(1).lower(), norm_branch(m.group(2))
            if "/" not in br and kind != "agree":
                continue
            events.append((kind, br, c.get("created_at"), c.get("html_url")))
            if kind == "claim":
                tail = body[m.end():]
                nxt = CLAIM_RE.search(tail)
                tail = tail[:nxt.start()] if nxt else tail
                f = split_fields(tail)
                claims[br] = {"branch": br, "created_at": c.get("created_at"), "url": c.get("html_url"),
                              "existing": f.get("existing", ""), "gap": f.get("gap", ""),
                              "files": split_globs(f.get("files", "")), "test_first": f.get("test-first", ""),
                              "try_it": f.get("try-it", ""), "agreed": False, "has_fields": bool(f),
                              "kind": f.get("kind", "").strip(" *_`.").lower()}
            elif kind == "release":
                claims.pop(br, None)
            elif kind == "agree" and br in claims:
                claims[br]["agreed"] = True
    return claims, events


def parse_ts(s):
    if not s: return None
    try:
        return datetime.datetime.strptime(s[:19], "%Y-%m-%dT%H:%M:%S").replace(tzinfo=datetime.timezone.utc)
    except ValueError:
        return None


def now_utc():
    return datetime.datetime.now(datetime.timezone.utc)


def claim_status(claim, last_push, now=None):
    """'active' or 'stale': a claim expires after 24 hours with no push to its branch."""
    now = now or now_utc()
    t0 = parse_ts(claim["created_at"])
    t = max([x for x in (t0, last_push) if x] or [now])
    return "stale" if (now - t).total_seconds() > CLAIM_EXPIRY_HOURS * 3600 else "active"


def check_claim_rules(claim, lanes, other_claims, files=None):
    """Problems that make a claim refused (docs/collaboration.md, claim protocol)."""
    probs = []
    lane = lane_of_branch(claim["branch"])
    if not claim.get("has_fields"):
        probs.append("claim has no Existing/Gap/Files/Test-first/Try-it lines")
    else:
        for k, label in (("existing", "Existing"), ("gap", "Gap"), ("files", "Files"), ("test_first", "Test-first"), ("try_it", "Try-it")):
            if not claim.get(k):
                probs.append(f"missing {label}")
    for g in claim.get("files", []):
        # a glob is cross-lane when any sample of the rules owner for its literal prefix is the other lane
        sample = re.split(r"[*?\[]", g)[0]
        o = owner_of(sample if sample == g else sample + "x", lanes)
        bug_fix = claim.get("kind", "").startswith("bug") and bool(claim.get("test_first"))   # owner 2026-11-15: bug fixes may touch any file
        if lane and o in ("claude2", "coordinator") and o != lane and not claim.get("agreed") and not bug_fix:
            probs.append(f"{g} is in the {o} lane (needs 'Agree: {claim['branch']}' from that lane)")
        elif o == "shared" and not claim.get("agreed"):
            pass  # shared files are only a handshake when two claims overlap; see overlaps below
    for oc in other_claims:
        if oc["branch"] == claim["branch"]:
            continue
        for a in claim.get("files", []):
            hit = next((b for b in oc.get("files", []) if globs_overlap(a, b, files)), None)
            if hit and not claim.get("agreed"):
                probs.append(f"{a} overlaps {oc['branch']} ({hit}); release or agree first")
                break
    return probs


# ---------------------------------------------------------------- git views

def branches(root, prefixes=("claude/", "claude2/")):
    out = git(root, "for-each-ref", "--format=%(refname:short)\t%(objectname)\t%(committerdate:iso-strict)", "refs/remotes/origin")
    res = []
    for line in out.splitlines():
        n, sha, d = line.split("\t")
        n = n[len("origin/"):] if n.startswith("origin/") else n
        if n.startswith(prefixes):
            res.append({"name": n, "sha": sha, "date": parse_ts(d.replace("+00:00", "")[:19]) if d else None, "raw_date": d})
    return res


def _cache_path(root):
    gd = git(root, "rev-parse", "--git-dir").strip()
    gd = gd if os.path.isabs(gd) else os.path.join(root, gd)
    return os.path.join(gd, "collab-cache.json")


class DiffCache:
    """Caches per-branch diff facts by (branch sha, main sha) in .git/collab-cache.json so reruns are quick."""
    def __init__(self, root, base="origin/main", use_disk=True):
        self.root, self.base, self.use_disk = root, base, use_disk
        self.main_sha = git(root, "rev-parse", base).strip()
        self.data = {}
        if use_disk:
            try:
                with open(_cache_path(root)) as f:
                    self.data = json.load(f)
            except Exception:
                self.data = {}

    def save(self):
        if not self.use_disk: return
        try:
            with open(_cache_path(self.root), "w") as f:
                json.dump(self.data, f)
        except Exception:
            pass

    def light(self, ref, sha):
        """Just the changed file names (cheap) and whether main already holds them. Reuses full facts when cached."""
        key = f"{sha}:{self.main_sha}"
        if key in self.data:
            f = self.data[key]
            return {"files": list(f["files"]), "merged_like": f.get("merged_like", False)}
        lk = key + ":l"
        if lk not in self.data:
            files = [x for x in git(self.root, "diff", "--name-only", "--no-renames", f"{self.base}...{ref}").splitlines() if x]
            ml = False
            if files and len(files) <= 300:
                ml = subprocess.run(["git", "-C", self.root, "diff", "--quiet", self.base, ref, "--", *files], stderr=subprocess.DEVNULL).returncode == 0
            self.data[lk] = {"files": files, "merged_like": ml}
        return self.data[lk]

    def facts(self, ref, sha):
        """files {path: status}, hunks {path: [[start,end],...]} (old side, against the merge base), merge_base, behind, ahead."""
        key = f"{sha}:{self.main_sha}"
        if key in self.data:
            return self.data[key]
        r = self.root
        mb = git(r, "merge-base", self.base, ref).strip()
        files = {}
        for line in git(r, "diff", "--name-status", "--no-renames", f"{self.base}...{ref}").splitlines():
            st, _, p = line.partition("\t")
            files[p] = st
        hunks = {}
        cur = None
        for line in git(r, "diff", "-U0", "--no-renames", f"{self.base}...{ref}").splitlines():
            if line.startswith("+++ "):
                cur = line[6:] if line.startswith("+++ b/") else None
            elif line.startswith("@@") and cur:
                m = re.match(r"@@ -(\d+)(?:,(\d+))? \+", line)
                if m:
                    a, n = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
                    hunks.setdefault(cur, []).append([a, a + max(n, 1) - 1])
        # Squash merges leave the branch "unmerged" for git. If main already holds the branch's content for every
        # file the branch changed, treat it as merged (or superseded): it is not live work.
        merged_like = False
        if files and len(files) <= 300:
            p = subprocess.run(["git", "-C", r, "diff", "--quiet", self.base, ref, "--", *files], stderr=subprocess.DEVNULL)
            merged_like = p.returncode == 0
        behind = git(r, "rev-list", "--count", f"{ref}..{self.base}").strip()
        ahead = git(r, "rev-list", "--count", f"{self.base}..{ref}").strip()
        f = {"files": files, "hunks": hunks, "merge_base": mb, "behind": int(behind or 0), "ahead": int(ahead or 0), "merged_like": merged_like}
        self.data[key] = f
        return f


# ---------------------------------------------------------------- GitHub

def gh_api(path, repo=REPO_DEFAULT, token=None):
    url = path if path.startswith("http") else f"https://api.github.com/repos/{repo}{path}"
    req = urllib.request.Request(url, headers={"User-Agent": "collab", "Accept": "application/vnd.github+json"})
    tok = token or os.environ.get("GITHUB_TOKEN")
    if tok: req.add_header("Authorization", f"Bearer {tok}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def gh_pages(path, repo=REPO_DEFAULT, maxpages=10):
    out, sep = [], "&" if "?" in path else "?"
    for p in range(1, maxpages + 1):
        try:
            d = gh_api(f"{path}{sep}per_page=100&page={p}", repo)
        except Exception:
            break
        out += d
        if len(d) < 100: break
    return out
