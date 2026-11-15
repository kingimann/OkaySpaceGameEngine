#!/usr/bin/env python3
"""Turn new rows of the Supabase table `crash_reports` into GitHub issues (docs/crash_reports.md).

Run by .github/workflows/crash-issues.yml every 30 minutes. It needs two secrets that only the workflow has:
  SUPABASE_SERVICE_KEY  the service_role key of the Supabase project (the same secret release.yml already uses)
  GITHUB_TOKEN          the workflow's own token (permissions: issues: write)
The launcher never sees either. Without SUPABASE_SERVICE_KEY the script says so and exits 0 (a green no-op).

What it does, in order:
  1. reads the rows that have no `processed_at` yet (oldest first, at most 200);
  2. groups them by `signature` (the same crash: same exception, same top frames);
  3. a signature that already has an issue (label `crash`, or any issue whose body carries the hidden marker
     <!-- okay-crash-signature: ... -->) gets ONE comment with the count, versions and operating systems of the new rows;
     a new signature gets ONE new issue (at most --max-new per run, the rest wait for the next run);
  4. marks the rows `processed_at` with the issue number, only after GitHub accepted the issue or comment;
  5. deletes rows that were processed more than --prune-days days ago.

Rows are written by anyone who has the public anon key, so every byte of a row is treated as hostile: the text is scrubbed again
here (the launcher already did it), @mentions and #links are made harmless, everything goes inside code spans or code blocks whose
fence is longer than any backtick run inside, lines are cut, and the sizes are capped.

  python3 scripts/crash_to_issue.py --rows-file tests/data/crash_rows_sample.json --dry-run     # prints the issue text; no network, no secrets

Written from the GitHub REST API and PostgREST documentation; standard library only.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter, OrderedDict

MARKER = "okay-crash-signature"
LABEL = "crash"
LABEL_COLOR = "d73a4a"
MAX_TITLE = 110
MAX_BODY = 60000          # GitHub refuses bodies over 65,536 characters
MAX_COMMENT = 8000
MAX_LINE = 400
MAX_BACKTRACE_LINES = 60
MAX_CRUMBS = 24
MAX_CONSOLE = 200
SIG_RE = re.compile(r"^[0-9a-f]{16}$")
MARKER_RE = re.compile(re.escape(MARKER) + r":\s*([0-9a-f]{16})")

# ---------------------------------------------------------------------------------------------------------------------------
# Text safety
# ---------------------------------------------------------------------------------------------------------------------------
_INVISIBLE = re.compile("[​-‏‪-‮⁠-⁩﻿­]")
_CONTROL = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def clean(text):
    """Text from a row: a string, no control characters, no zero-width or bidi-override characters (they can make a line read
    differently from what it says), CR removed."""
    if not isinstance(text, str):
        text = "" if text is None else str(text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return _CONTROL.sub("", _INVISIBLE.sub("", text))


_WIN_PROFILE = re.compile(r"(?i)\b[A-Z]:[\\/]+(?:Users|Documents and Settings)[\\/]+[^\\/\r\n\"'`<>|*?:,;()\[\]{}]+")
_POSIX_HOME = re.compile(r"(?<![\w.~])/(?:home|Users)/[^/\s\"'`<>|*?:,;()\[\]{}]+")
_UNC = re.compile(r"(?<![\w%:/\\])\\\\(?![?.])[A-Za-z0-9._-]+(?=\\)")
_URL_CREDS = re.compile(r"(://)[^\s/@?#]+@")
_EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
_IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w]|\.\d)")
_IPV6 = re.compile(r"(?<![\w:])(?:[0-9A-Fa-f]{0,4}:){4,7}[0-9A-Fa-f]{0,4}(?![\w:])|(?<![\w:])(?:[0-9A-Fa-f]{1,4}:)+:(?:[0-9A-Fa-f]{1,4}(?::[0-9A-Fa-f]{1,4})*)?(?![\w:])")
_KEYS = re.compile(
    r"(?<![\w])(?:github_pat_|ghp_|gho_|ghu_|ghs_|ghr_|glpat-|sk-ant-|sk-|pk_live_|sk_live_|rk_live_|sb_publishable_|sb_secret_|"
    r"xox[bpars]-|AKIA|ASIA|AIza|hf_|npm_|shpat_)[A-Za-z0-9_\-]{10,}")
_JWT = re.compile(r"(?<![\w])eyJ[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{6,}\.[A-Za-z0-9_\-]*")
_BEARER = re.compile(r"(?i)\b(bearer|basic)\s+[A-Za-z0-9._~+/=\-]{8,}")
_AUTH_LINE = re.compile(r"(?im)\b(authorization)\s*[:=][^\n]*")
_SECRET_KV = re.compile(
    r"(?i)\b([A-Za-z0-9_\-]*(?:api[_\-]?key|token|secret|passw|pwd|authorization|credential|private[_\-]?key|access[_\-]?key|session[_\-]?key)[A-Za-z0-9_\-]*)"
    r"([\"']?\s*[:=]\s*)(?!:)([\"']?)[^\s\"'&,;)\n]+")
_HEX_RUN = re.compile(r"(?<![\w])[0-9a-fA-F]{32,}(?![\w])")


def scrub(text):
    """A second, conservative pass over a report (the launcher already scrubbed it; this does not trust that)."""
    text = clean(text)
    text = _WIN_PROFILE.sub("%USERPROFILE%", text)
    text = _POSIX_HOME.sub("~", text)
    text = _UNC.sub(r"\\\\<host>", text)
    text = _URL_CREDS.sub(r"\1<redacted>@", text)
    text = _AUTH_LINE.sub(lambda m: m.group(1) + ": <redacted>", text)
    text = _KEYS.sub("<token>", text)
    text = _JWT.sub("<token>", text)
    text = _BEARER.sub(lambda m: m.group(1) + " <token>", text)
    text = _SECRET_KV.sub(lambda m: m.group(1) + m.group(2) + m.group(3) + "<redacted>", text)
    text = _EMAIL.sub("<email>", text)

    def ip(m):
        parts = m.group(0).split(".")
        if any(int(p) > 255 for p in parts):
            return m.group(0)
        if parts[0] == "127" or m.group(0) == "0.0.0.0":
            return m.group(0)
        return "<ip>"

    text = _IPV4.sub(ip, text)
    text = _IPV6.sub(lambda m: "<ip>" if len(m.group(0)) >= 5 and any(c.isdigit() or c.lower() in "abcdef" for c in m.group(0)) else m.group(0), text)
    text = _HEX_RUN.sub("<token>", text)
    return text


def title_text(text, cap):
    """Plain text for a title: one line; no @ or # (they would turn into pings and links); cut at `cap` characters."""
    text = clean(text)
    text = re.sub(r"[\t\n]+", " ", text).replace("@", "").replace("#", "")
    text = re.sub(r" {2,}", " ", text).strip()
    if len(text) > cap:
        text = text[: cap - 3].rstrip() + "..."
    return text


def code_cell(text, cap=160):
    """A value in a table cell: one line inside a code span, so markdown, HTML, @mentions and #links do nothing (inside a code span
    GitHub does not turn them into links or pings). The pipe and the backtick are the two characters that could end the cell or the
    span, so they are replaced."""
    text = clean(text)
    text = re.sub(r"[\t\n]+", " ", text)
    text = re.sub(r" {2,}", " ", text).strip()
    if len(text) > cap:
        text = text[: cap - 3].rstrip() + "..."
    return "`" + text.replace("`", "'").replace("|", "/") + "`"


def fence(lines, cap_line=MAX_LINE):
    """A fenced code block holding `lines`. Each line is cut at cap_line characters; the fence is longer than any run of backticks
    inside, so nothing in the text can end it."""
    cut = []
    for line in lines:
        line = clean(line)
        if len(line) > cap_line:
            line = line[:cap_line] + "...[cut]"
        cut.append(line)
    longest = max((len(m.group(0)) for l in cut for m in re.finditer(r"`+", l)), default=0)
    ticks = "`" * max(3, longest + 1)
    return ticks + "text\n" + "\n".join(cut) + ("\n" if cut else "") + ticks


def neutralize(text):
    """Prose that has to be shown as it is (not inside a code span): every @name and every #number goes inside backticks."""
    text = clean(text).replace("`", "'")
    return re.sub(r"[@#][\w./-]+", lambda m: "`" + m.group(0) + "`", text)


# ---------------------------------------------------------------------------------------------------------------------------
# Reading a report
# ---------------------------------------------------------------------------------------------------------------------------
def exception_name(exc):
    u = clean(exc).upper()
    table = [("SIGSEGV", "Access violation"), ("C0000005", "Access violation"), ("C00000FD", "Stack overflow"), ("C0000094", "Divide by zero"),
             ("SIGFPE", "Divide by zero"), ("C000001D", "Illegal instruction"), ("SIGILL", "Illegal instruction"), ("C0000409", "Aborted"),
             ("SIGABRT", "Aborted"), ("80000003", "Breakpoint"), ("C0000017", "Out of memory"), ("SIGBUS", "Bus error"),
             ("TERMINATE", "Unhandled C++ exception"), ("C++ EXCEPTION", "Unhandled C++ exception")]
    for key, name in table:
        if key in u:
            return name
    return title_text(exc, 40) or "Crash"


def parse_report(text):
    """The report text -> {"header": [(key, value)], "backtrace": [...], "breadcrumbs": [...], "console": [...]} (the layout of
    shared/CrashContext.hpp; an older report simply has fewer parts)."""
    out = {"header": [], "backtrace": [], "breadcrumbs": [], "console": []}
    part = "header"
    for line in clean(text).split("\n"):
        if part != "header" and line.startswith("--- "):
            part = "breadcrumbs" if line[4:].startswith("Breadcrumbs") else "console" if line[4:].startswith("Console") else "ignored"
            continue
        if part == "header":
            if line.startswith("Backtrace:"):
                part = "backtrace"
                continue
            m = re.match(r"^([A-Za-z][A-Za-z ]{0,23}):\s*(.*)$", line)
            if m and len(out["header"]) < 64:
                out["header"].append((m.group(1), m.group(2).strip()))
        elif part == "backtrace":
            if line.strip() and len(out["backtrace"]) < 200:
                out["backtrace"].append(line.strip())
        elif part == "breadcrumbs":
            if line.strip() and len(out["breadcrumbs"]) < 500:
                out["breadcrumbs"].append(line.strip())
        elif part == "console":
            if line and len(out["console"]) < 2000:
                out["console"].append(line)
    return out


def _cxxfilt(text):
    exe = shutil.which("c++filt")
    if not exe:
        raise RuntimeError("c++filt is not installed")
    done = subprocess.run([exe], input=text.encode("utf-8", "replace"), capture_output=True, timeout=10, check=True)
    return done.stdout.decode("utf-8", "replace")


def demangle(text, run=None):
    """A Windows MinGW build names functions the Itanium way (_ZN4okay5Scene6UpdateEf); with binutils' c++filt on the machine they are
    shown readable (okay::Scene::Update(float)). Only ever a convenience: no c++filt, a failure, or an answer with a different number
    of lines leaves the text exactly as it was. The signature is made by the launcher from the original names and is not affected."""
    if not text or "_Z" not in text:
        return text
    try:
        out = (run or _cxxfilt)(text)
    except Exception:
        return text
    if not isinstance(out, str) or not out.strip() or out.count("\n") != text.count("\n"):
        return text
    return out


def demangle_lines(lines, run=None):
    lines = [l[:MAX_LINE] for l in lines]
    out = demangle("\n".join(lines), run)
    return out.split("\n") if out.count("\n") == len(lines) - 1 else lines


def valid_signature(sig):
    return isinstance(sig, str) and bool(SIG_RE.match(sig))


def issue_title(row):
    top = title_text(demangle(clean(row.get("top_frame") or "")), 60)
    exc = exception_name(row.get("exception") or "")
    version = title_text(row.get("version") or "?", 20)
    title = "Crash: %s%s (v%s)" % (exc, " in " + top if top else "", version)
    return title if len(title) <= MAX_TITLE else title[: MAX_TITLE - 3] + "..."


_ORDER = ["Version", "Build", "When", "Last step", "Exception", "Fault module", "Fault detail", "Thread", "OS", "CPU", "RAM", "Process memory", "GPU",
          "UI backend", "Own code", "Mode", "Scene", "Objects", "Assets loaded", "Uptime", "Recovery copy"]


def issue_body(row, max_body=MAX_BODY):
    """The issue's Markdown for one row. Everything from the row sits in a code span or a code block."""
    report = parse_report(scrub(row.get("report") or ""))
    report["backtrace"] = demangle_lines(report["backtrace"][:MAX_BACKTRACE_LINES])
    sig = row["signature"]
    head = OrderedDict()
    for k, v in report["header"]:
        head.setdefault(k, v)
    # the row's own columns win over the text for the fields the table also has
    for key, col in (("Version", "version"), ("Build", "build"), ("OS", "os"), ("Exception", "exception"), ("Last step", "last_step")):
        if row.get(col):
            head[key] = scrub(row[col])
    head.pop("Exe", None)   # the install path says nothing the report needs

    def build(console_from):
        b = ["<!-- %s: %s -->" % (MARKER, sig)]
        b.append("Created automatically from an anonymous crash report sent by the launcher. The text was cleaned of names, folders, "
                 "emails and addresses on the sender's computer and again here. First seen: %s." % code_cell(row.get("created_at") or "?", 40))
        b.append("")
        b.append("| | |")
        b.append("|---|---|")
        b.append("| Signature | %s |" % code_cell(sig))
        for k in _ORDER:
            if head.get(k):
                b.append("| %s | %s |" % (k, code_cell(head[k])))
        for k, v in head.items():
            if k not in _ORDER and v:
                b.append("| %s | %s |" % (code_cell(k, 30).strip("`"), code_cell(v)))
        b.append("")
        b.append("### Backtrace")
        b.append(fence(report["backtrace"][:MAX_BACKTRACE_LINES]) if report["backtrace"] else "_No frames in the report._")
        if report["breadcrumbs"]:
            b.append("")
            b.append("### Last steps")
            b.append(fence(report["breadcrumbs"][-MAX_CRUMBS:]))
        console = report["console"][-MAX_CONSOLE:][console_from:]
        if console:
            b.append("")
            b.append("<details><summary>Console (last %d lines)</summary>" % len(console))
            b.append("")
            b.append(fence(console))
            b.append("")
            b.append("</details>")
        b.append("")
        b.append("### How to reproduce")
        b.append("_Not filled in yet. Please add: what you were doing in the editor, the scene, and whether it happens every time._")
        b.append("")
        b.append("1. ")
        b.append("2. ")
        b.append("3. ")
        return "\n".join(b)

    drop = 0
    body = build(drop)
    total = len(report["console"][-MAX_CONSOLE:])
    while len(body) > max_body and drop < total:   # the oldest Console lines go first
        drop = min(total, drop + max(1, total // 8))
        body = build(drop)
    if len(body) > max_body:
        body = body[: max_body - 60].rstrip() + "\n\n_(cut: the report was too long)_\n"
    return body


def comment_body(sig, rows, total=None):
    """One comment for a crash that already has an issue: how many new reports, which versions and systems."""
    versions = Counter(title_text(r.get("version") or "?", 30) for r in rows)
    systems = Counter(title_text(r.get("os") or "unknown", 60) for r in rows)
    gpus = Counter()
    for r in rows:
        gpu = dict(parse_report(scrub(r.get("report") or ""))["header"]).get("GPU", "")
        if gpu:
            gpus[title_text(gpu, 60)] += 1
    firsts = sorted(r.get("created_at") or "" for r in rows)
    lines = ["<!-- %s: %s -->" % (MARKER, sig),
             "%d new report%s of this crash (signature %s)%s." % (len(rows), "" if len(rows) == 1 else "s", code_cell(sig),
                                                                  "; %d in total so far" % total if total else "")]
    def listing(title, counter):
        if counter:
            lines.append("")
            lines.append("**%s:** " % title + ", ".join("%s x%d" % (code_cell(k, 60), n) for k, n in counter.most_common(8)))
    listing("Versions", versions)
    listing("Systems", systems)
    listing("Graphics", gpus)
    if firsts and firsts[0]:
        lines.append("")
        lines.append("First of these: %s, last: %s." % (code_cell(firsts[0], 40), code_cell(firsts[-1], 40)))
    body = "\n".join(lines)
    return body if len(body) <= MAX_COMMENT else body[: MAX_COMMENT - 30] + "\n\n_(cut)_"


# ---------------------------------------------------------------------------------------------------------------------------
# Deciding what to do (pure)
# ---------------------------------------------------------------------------------------------------------------------------
def existing_from_issues(issues):
    """GitHub issues (the API's dicts) -> {signature: {"number", "state"}}. Pull requests are not issues here. The oldest issue wins
    when two carry the same marker."""
    found = {}
    for it in issues or []:
        if not isinstance(it, dict) or "pull_request" in it or not isinstance(it.get("number"), int):
            continue
        m = MARKER_RE.search(it.get("body") or "")
        if not m:
            continue
        sig = m.group(1)
        if sig not in found or it["number"] < found[sig]["number"]:
            found[sig] = {"number": it["number"], "state": it.get("state", "open")}
    return found


def plan(rows, existing, max_new=5):
    """-> (actions, deferred, invalid). actions: [{"type": "create"|"comment", "signature", "rows", "number"?}], oldest signature first.
    `existing` maps signature -> {"number", ...}. At most `max_new` new issues; the rows of the rest are `deferred` (left unprocessed)."""
    groups = OrderedDict()
    invalid = []
    for r in sorted(rows, key=lambda r: (str(r.get("created_at") or ""), str(r.get("id") or ""))):
        if not isinstance(r, dict) or not valid_signature(r.get("signature")) or not r.get("id"):
            invalid.append(r)
            continue
        groups.setdefault(r["signature"], []).append(r)
    actions, deferred, new = [], [], 0
    for sig, rs in groups.items():
        if sig in existing:
            actions.append({"type": "comment", "signature": sig, "rows": rs, "number": existing[sig]["number"]})
        elif new < max_new:
            new += 1
            actions.append({"type": "create", "signature": sig, "rows": rs})
        else:
            deferred.extend(rs)
    return actions, deferred, invalid


def process(rows, gh, sb, max_new=5, dry_run=False, log=print):
    """Do the plan. `gh` and `sb` are the two clients (fakes in the tests). Returns a dict of counts; failures are counted, never
    raised, so one bad row cannot stop the others."""
    stats = {"created": 0, "commented": 0, "marked": 0, "deferred": 0, "invalid": 0, "failed": 0}
    if not rows:
        return stats
    existing = existing_from_issues(gh.list_crash_issues())
    sigs_new = {r["signature"] for r in rows if isinstance(r, dict) and valid_signature(r.get("signature"))} - set(existing)
    for sig in sorted(sigs_new)[: max(max_new * 3, 10)]:   # a hand-filed issue (no label) is found by its marker, not by label
        found = gh.search_signature(sig)
        if found:
            existing[sig] = found
    actions, deferred, invalid = plan(rows, existing, max_new)
    stats["deferred"], stats["invalid"] = len(deferred), len(invalid)
    for a in actions:
        sig, rs = a["signature"], a["rows"]
        try:
            if a["type"] == "create":
                title, body = issue_title(rs[0]), issue_body(rs[0])
                if dry_run:
                    log("== would create issue: %s\n%s\n" % (title, body))
                    if len(rs) > 1:
                        log("== and comment with %d more\n%s\n" % (len(rs) - 1, comment_body(sig, rs[1:])))
                    continue
                number = gh.create_issue(title, body)
                stats["created"] += 1
                if len(rs) > 1:
                    gh.comment(number, comment_body(sig, rs[1:], sb.total_for(sig)))
                    stats["commented"] += 1
            else:
                number = a["number"]
                if dry_run:
                    log("== would comment on #%s\n%s\n" % (number, comment_body(sig, rs)))
                    continue
                gh.comment(number, comment_body(sig, rs, sb.total_for(sig)))
                stats["commented"] += 1
        except Exception as e:   # one crash's failure must not stop the others; the rows stay unprocessed and are retried
            stats["failed"] += 1
            log("::warning::signature %s: %s" % (sig, mask(str(e))))
            continue
        try:
            sb.mark_processed([r["id"] for r in rs], number)
            stats["marked"] += len(rs)
        except Exception as e:
            stats["failed"] += 1
            log("::warning::could not mark rows of %s processed (the next run would comment again): %s" % (sig, mask(str(e))))
    return stats


# ---------------------------------------------------------------------------------------------------------------------------
# The two services
# ---------------------------------------------------------------------------------------------------------------------------
_SECRETS = []   # values to hide from any message


def mask(text):
    for s in _SECRETS:
        if s:
            text = text.replace(s, "<secret>")
    return _KEYS.sub("<token>", _JWT.sub("<token>", text))


def _http(method, url, headers, data=None, timeout=30):
    body = None if data is None else json.dumps(data).encode("utf-8")
    req = urllib.request.Request(url, data=body, method=method, headers=dict(headers, **({"Content-Type": "application/json"} if data is not None else {})))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", "replace")
            return resp.status, (json.loads(raw) if raw.strip().startswith(("{", "[")) else raw), resp.headers
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")[:500]
        raise RuntimeError("HTTP %d from %s: %s" % (e.code, urllib.parse.urlsplit(url).netloc, mask(raw)))
    except urllib.error.URLError as e:
        raise RuntimeError("cannot reach %s: %s" % (urllib.parse.urlsplit(url).netloc, e.reason))


class Supabase:
    def __init__(self, url, key):
        self.base = url.rstrip("/")
        self.h = {"apikey": key}
        # The legacy service_role key is a JWT and goes in both headers. A newer "sb_secret_..." key is not a JWT: it goes in apikey only
        # (Supabase rejects it as a bearer token).
        if re.match(r"^eyJ[\w-]+\.[\w-]+\.[\w-]*$", key):
            self.h["Authorization"] = "Bearer " + key

    def fetch_rows(self, limit=200):
        q = "select=*&processed_at=is.null&order=created_at.asc&limit=%d" % limit
        return _http("GET", self.base + "/rest/v1/crash_reports?" + q, self.h)[1]

    def mark_processed(self, ids, number):
        for i in ids:
            if not re.match(r"^[0-9a-fA-F-]{36}$", str(i)):
                raise ValueError("unexpected row id")
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        _http("PATCH", self.base + "/rest/v1/crash_reports?id=in.(%s)" % ",".join(ids), dict(self.h, Prefer="return=minimal"),
              {"processed_at": now, "issue_number": number})

    def total_for(self, sig):
        try:
            _, _, hdr = _http("GET", self.base + "/rest/v1/crash_reports?select=id&signature=eq.%s&limit=1" % urllib.parse.quote(sig), dict(self.h, Prefer="count=exact"))
            m = re.search(r"/(\d+)$", hdr.get("Content-Range", ""))
            return int(m.group(1)) if m else None
        except Exception:
            return None

    def prune(self, days):
        cutoff = (datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")
        _http("DELETE", self.base + "/rest/v1/crash_reports?processed_at=lt." + urllib.parse.quote(cutoff), dict(self.h, Prefer="return=minimal"))


class GitHub:
    def __init__(self, repo, token, api="https://api.github.com"):
        self.repo = repo
        self.api = api.rstrip("/")   # GitHub Actions sets GITHUB_API_URL (https://api.github.com, or an enterprise server's address)
        self.h = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "okayspace-crash-issues"}

    def _api(self, method, path, data=None):
        return _http(method, "%s/repos/%s%s" % (self.api, self.repo, path), self.h, data)

    def ensure_label(self):
        try:
            self._api("POST", "/labels", {"name": LABEL, "color": LABEL_COLOR, "description": "An automatic crash report"})
        except RuntimeError:
            pass   # it exists already (422)

    def list_crash_issues(self):
        out = []
        for page in range(1, 21):
            _, items, _ = self._api("GET", "/issues?labels=%s&state=all&per_page=100&page=%d" % (LABEL, page))
            out.extend(items)
            if len(items) < 100:
                break
        return out

    def search_signature(self, sig):
        q = urllib.parse.quote("repo:%s is:issue %s in:body" % (self.repo, sig))
        try:
            _, res, _ = _http("GET", "%s/search/issues?q=%s&per_page=5" % (self.api, q), self.h)
        except RuntimeError:
            return None
        found = existing_from_issues(res.get("items", []) if isinstance(res, dict) else [])
        return found.get(sig)

    def create_issue(self, title, body):
        _, res, _ = self._api("POST", "/issues", {"title": title, "body": body, "labels": [LABEL]})
        return res["number"]

    def comment(self, number, body):
        self._api("POST", "/issues/%d/comments" % number, {"body": body})


def default_supabase_url(root):
    """The project URL baked into CMakeLists.txt (public: the launcher ships with it), as scripts/publish-supabase.sh reads it."""
    try:
        with open(os.path.join(root, "CMakeLists.txt"), encoding="utf-8") as f:
            for line in f:
                m = re.match(r'^set\(OKAY_DEFAULT_ACCOUNT_URL\s+"(https://[a-z0-9]+\.supabase\.co)"', line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return ""


def check_supabase_url(url, allow_any=False):
    p = urllib.parse.urlsplit(url)
    if allow_any:
        return True
    return p.scheme == "https" and bool(re.match(r"^[a-z0-9]+\.supabase\.co$", p.netloc)) and p.path in ("", "/") and not p.query


# ---------------------------------------------------------------------------------------------------------------------------
def main(argv=None, env=None):
    env = os.environ if env is None else env
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rows-file", help="read rows from this JSON file instead of Supabase (with --dry-run: no network at all)")
    ap.add_argument("--dry-run", action="store_true", help="print what would be done; create nothing, mark nothing")
    ap.add_argument("--max-new", type=int, default=5, help="new issues per run (default 5); the rest wait for the next run")
    ap.add_argument("--prune-days", type=int, default=90, help="delete processed rows older than this (0 = never)")
    ap.add_argument("--allow-any-host", action="store_true", help="tests only: accept a Supabase URL that is not https://<id>.supabase.co")
    args = ap.parse_args(argv)
    if args.rows_file and not args.dry_run:
        ap.error("--rows-file only makes sense with --dry-run (it never talks to GitHub or Supabase)")
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

    if args.rows_file:
        with open(args.rows_file, encoding="utf-8") as f:
            rows = json.load(f)
        if args.dry_run:
            class _NoGit:
                def list_crash_issues(self): return []
                def search_signature(self, sig): return None
            class _NoDb:
                def total_for(self, sig): return None
            process(rows, _NoGit(), _NoDb(), args.max_new, True)
            print("dry run: %d rows read" % len(rows))
            return 0
    key = env.get("SUPABASE_SERVICE_KEY", "")
    if not key:
        print("::notice title=Crash reports::SUPABASE_SERVICE_KEY is not set in this repository's secrets, so there is nothing to read. "
              "Nothing was done. (docs/crash_reports.md says how to add it.)")
        return 0
    _SECRETS.extend([key, env.get("GITHUB_TOKEN", "")])
    url = env.get("SUPABASE_URL") or default_supabase_url(root)
    if not url or not check_supabase_url(url, args.allow_any_host):
        print("::error::SUPABASE_URL is missing or is not an https://<project>.supabase.co address.")
        return 1
    token, repo = env.get("GITHUB_TOKEN", ""), env.get("GITHUB_REPOSITORY", "")
    if not token or not re.match(r"^[\w.-]+/[\w.-]+$", repo):
        print("::error::GITHUB_TOKEN and GITHUB_REPOSITORY (owner/name) are needed.")
        return 1
    api = env.get("GITHUB_API_URL") or "https://api.github.com"
    if not args.allow_any_host and not api.startswith("https://"):
        print("::error::GITHUB_API_URL must be an https address.")
        return 1
    sb, gh = Supabase(url, key), GitHub(repo, token, api)
    try:
        rows = sb.fetch_rows()
    except RuntimeError as e:
        print("::error::could not read the crash_reports table: %s" % mask(str(e)))
        return 1
    if not isinstance(rows, list):
        print("::error::unexpected answer from the crash_reports table")
        return 1
    print("%d unprocessed crash report row(s)." % len(rows))
    if rows and not args.dry_run:
        gh.ensure_label()
    stats = process(rows, gh, sb, args.max_new, args.dry_run)
    print("created %(created)d issue(s), commented %(commented)d time(s), marked %(marked)d row(s), deferred %(deferred)d, invalid %(invalid)d, failed %(failed)d" % stats)
    summary = env.get("GITHUB_STEP_SUMMARY")
    if summary and not args.dry_run:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### Crash reports\n- %d row(s) read\n- %d issue(s) created, %d comment(s), %d row(s) marked, %d left for the next run, %d failed\n" %
                    (len(rows), stats["created"], stats["commented"], stats["marked"], stats["deferred"], stats["failed"]))
    if args.prune_days > 0 and not args.dry_run and not stats["failed"]:
        try:
            sb.prune(args.prune_days)
        except RuntimeError as e:
            print("::warning::could not prune old rows: %s" % mask(str(e)))
    return 1 if stats["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
