#!/usr/bin/env python3
"""Shared helpers for the collaboration scripts (find_existing, collision_check, branch_lint, bug_patterns).

Standard library and git only. Plain rules: docs/process/collaboration.md. Lanes table: docs/lanes.json.
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


_default_lanes = None


def default_lanes():
    """docs/lanes.json of the checkout these scripts live in (used when a caller passes no lanes table)."""
    global _default_lanes
    if _default_lanes is None:
        _default_lanes = load_lanes(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return _default_lanes


def lane_defs(lanes=None):
    """{lane: {"prefix": "claude3/", "name": "Claude 3", "description": ...}} from lanes.json's "lanes" map."""
    return (lanes or default_lanes()).get("lanes", {})


def lane_names(lanes=None):
    return list(lane_defs(lanes))


def lane_prefixes(lanes=None):
    """{lane: branch prefix}, e.g. {"coordinator": "claude/", "claude2": "claude2/", "claude3": "claude3/"}."""
    return {k: v["prefix"] for k, v in lane_defs(lanes).items()}


def lane_display(lane, lanes=None):
    d = lane_defs(lanes).get(lane)
    return d.get("name", lane) if d else (lane or "?")


def lane_key(text, lanes=None):
    """'claude3', 'Claude 3', 'claude3/' or 'Coordinator' -> the lane name; None when it names no lane."""
    t = re.sub(r"[\s`*_]+", " ", (text or "")).strip().lower()
    if not t:
        return None
    for k, v in lane_defs(lanes).items():
        if t in (k.lower(), v.get("name", "").lower(), v["prefix"].lower(), v["prefix"].rstrip("/").lower()):
            return k
    return None


def rule_globs(rule):
    """A rule's globs: the ones that match files today plus the 'planned' ones (files that do not exist yet)."""
    return list(rule.get("globs", [])) + list(rule.get("planned", []))


def owner_of(path, lanes):
    """First matching rule's owner: a lane name from lanes.json, shared or open."""
    for r in lanes["rules"]:
        if any(glob_match(path, g) for g in rule_globs(r)):
            return r["owner"]
    return "open"


def lane_of_branch(name, lanes=None):
    """The lane whose branch prefix starts the name (the longest prefix wins); None for main, john/*, forks and the rest."""
    n = name[len("origin/"):] if name.startswith("origin/") else name
    best = None
    for k, p in lane_prefixes(lanes).items():
        if n.startswith(p) and (best is None or len(p) > len(best[1])):
            best = (k, p)
    return best[0] if best else None


def generated_entry(path, lanes):
    for g in lanes.get("generated", []):
        if g["file"] == path:
            return g
    return None


# ---------------------------------------------------------------- claims

# "Claim: claude3/x", "**CLAIM: claude3/x**", "Claim (Claude 3): claude3/x", "Agree (Claude 2): claude3/x". The name in brackets is optional.
CLAIM_RE = re.compile(r"^[\s>*#_-]*(Claim|Release|Agree)(?:\s+and\s+result)?(?:\s*\(([^)\n]{1,40})\))?\s*:\s*[*_`\s]*([A-Za-z0-9_./-]+)", re.I | re.M)
FIELD_KEYS = ["Existing", "Gap", "Files", "Test-first", "Try-it"]
FIELD_RE = re.compile(r"(?:^|\s/\s|\n)[\s>*_-]*(Existing|Gap|Files|Test-first|Try-it|Kind|Lane)\s*(?:\*\*|__)?\s*:\s*(?:\*\*|__)?\s*", re.I)


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


def _named_lane(m, fields, lanes):
    """The lane a Claim/Agree comment names: 'Claim (Claude 3): ...' or a 'Lane: claude3' line. None when it names none."""
    for t in (m.group(2), fields.get("lane", "")):
        k = lane_key((t or "").split("\n")[0].strip(" .;"), lanes)
        if k:
            return k
    return None


def parse_claims(comments, lanes=None):
    """comments: list of {body, created_at, html_url, user?}. Returns (claims dict branch->claim, events list).
    A later Release removes a claim; Agree marks the claim as agreed. Oldest first.
    A claim's lane is its 'Lane:' line or bracketed name when given ('Claim (Claude 3): claude3/x'), else its branch prefix.
    An Agree that names a lane ('Agree (Claude 2): b' or a 'Lane: claude2' line) counts only for that lane; one that names
    none counts for every lane (as before there were three lanes)."""
    claims, events = {}, []
    for c in sorted(comments, key=lambda x: x.get("created_at", "")):
        body = c.get("body") or ""
        for m in CLAIM_RE.finditer(body):
            kind, br = m.group(1).lower(), norm_branch(m.group(3))
            if "/" not in br and kind != "agree":
                continue
            events.append((kind, br, c.get("created_at"), c.get("html_url")))
            tail = body[m.end():]
            nxt = CLAIM_RE.search(tail)
            tail = tail[:nxt.start()] if nxt else tail
            f = split_fields(tail)
            if kind == "claim":
                named = _named_lane(m, f, lanes)
                stated = (m.group(2) or f.get("lane", "")).split("\n")[0].strip(" .;")
                claims[br] = {"branch": br, "created_at": c.get("created_at"), "url": c.get("html_url"),
                              "existing": f.get("existing", ""), "gap": f.get("gap", ""),
                              "files": split_globs(f.get("files", "")), "test_first": f.get("test-first", ""),
                              "try_it": f.get("try-it", ""), "agreed": False, "agreed_by": [],
                              "has_fields": bool(set(f) - {"lane", "kind"}),
                              "kind": f.get("kind", "").strip(" *_`.").lower(),
                              "lane": named or lane_of_branch(br, lanes), "lane_stated": stated if stated and not named else ""}
                if named and lane_of_branch(br, lanes) and named != lane_of_branch(br, lanes):
                    claims[br]["lane_mismatch"] = named
            elif kind == "release":
                claims.pop(br, None)
            elif kind == "agree" and br in claims:
                by = _named_lane(m, f, lanes)
                if by is None:
                    claims[br]["agreed"] = True
                elif by not in claims[br]["agreed_by"]:
                    claims[br]["agreed_by"].append(by)
    return claims, events


def agreed_by(claim, lane):
    """Did `lane` agree to this claim? A plain 'Agree:' (no lane named) or claim['agreed'] = True counts for every lane."""
    return bool(claim.get("agreed")) or lane in claim.get("agreed_by", [])


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
    """Problems that make a claim refused (docs/process/collaboration.md, claim protocol).
    A file owned by lane X, named by a claim of lane Y (Y != X), needs an Agree from X, unless the claim is 'Kind: bug'
    with a Test-first line. Two open claims naming the same file (shared files included) need an Agree too."""
    probs = []
    lane = claim.get("lane") or lane_of_branch(claim["branch"], lanes)
    names = set(lane_names(lanes))
    if claim.get("lane_mismatch"):
        probs.append(f"Lane: {claim['lane_mismatch']} does not match the branch prefix of {claim['branch']} "
                     f"({lane_of_branch(claim['branch'], lanes)}); use a {lane_prefixes(lanes).get(claim['lane_mismatch'], '?')} branch")
    elif claim.get("lane_stated"):
        probs.append(f"Lane: {claim['lane_stated']} names no lane in docs/lanes.json ({', '.join(sorted(names))})")
    if not claim.get("has_fields"):
        probs.append("claim has no Existing/Gap/Files/Test-first/Try-it lines")
    else:
        for k, label in (("existing", "Existing"), ("gap", "Gap"), ("files", "Files"), ("test_first", "Test-first"), ("try_it", "Try-it")):
            if not claim.get(k):
                probs.append(f"missing {label}")
    bug_fix = claim.get("kind", "").startswith("bug") and bool(claim.get("test_first"))   # owner 2026-11-15: bug fixes may touch any file
    for g in claim.get("files", []):
        # the owner of a glob is the rules' owner for its literal prefix
        sample = re.split(r"[*?\[]", g)[0]
        o = owner_of(sample if sample == g else sample + "x", lanes)
        if lane and o in names and o != lane and not agreed_by(claim, o) and not bug_fix:
            probs.append(f"{g} is in the {o} lane (needs 'Agree: {claim['branch']}' from {lane_display(o, lanes)})")
    for oc in other_claims:
        if oc["branch"] == claim["branch"]:
            continue
        olane = oc.get("lane") or lane_of_branch(oc["branch"], lanes)
        for a in claim.get("files", []):
            hit = next((b for b in oc.get("files", []) if globs_overlap(a, b, files)), None)
            if hit and not (agreed_by(claim, olane) if olane else claim.get("agreed")):
                probs.append(f"{a} overlaps {oc['branch']} ({hit}); release or agree first")
                break
    return probs


# ---------------------------------------------------------------- git views

def branches(root, prefixes=None):
    """The remote branches of every lane (prefixes from docs/lanes.json under root unless given)."""
    if prefixes is None:
        try:
            prefixes = tuple(lane_prefixes(load_lanes(root)).values())
        except OSError:
            prefixes = tuple(lane_prefixes().values())
    prefixes = tuple(prefixes)
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
