#!/usr/bin/env python3
"""scripts/crash_to_issue.py without a network: mock rows -> the issue text, the dedupe decisions, hostile text.

  python3 -I tests/scripts/test_crash_to_issue.py

Every test names what it guards. No test talks to GitHub or Supabase; the two services are fakes (FakeGH, FakeSB)."""
import io
import json
import os
import re
import sys
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import crash_to_issue as c  # noqa: E402

SIG_A, SIG_B, SIG_C = "0123456789abcdef", "fedcba9876543210", "aaaaaaaaaaaaaaaa"


def report(frames=None, console=None, step="UI.Inspector", version="2.874.0", extra_header=""):
    frames = frames or ["#00 OkayEngine.exe + 0x1A2B3C  DrawThing +0x40", "#01 OkayEngine.exe + 0x22  main +0x9"]
    console = console if console is not None else ["[-5s] I hello", "[-1s] E boom"]
    return ("OkaySpace crash report\nVersion: %s\nBuild: pe-1-2\nWhen: 2026-11-15 10:00:00\nExe: C:\\Users\\bob\\x\\OkayEngine.exe\nLast step: %s\n"
            "Exception: 0xC0000005\nFault module: OkayEngine.exe + 0x1A2B3C\nOS: Windows 10.0.19045 (x64)\nGPU: Test | Vendor\n%s\nBacktrace:\n%s\n\n"
            "--- Breadcrumbs (oldest first: frame, step) ---\nframe 1: %s\n--- Console (last %d lines) ---\n%s\n--- End of report ---\n"
            % (version, step, extra_header, "\n".join("  " + f for f in frames), step, len(console), "\n".join(console)))


def row(n=1, sig=SIG_A, created="2026-11-15T10:00:00+00:00", version="2.874.0", os_="Windows 10.0.19045 (x64)", **kw):
    r = {"id": "%08d-0000-4000-8000-000000000000" % n, "created_at": created, "report_id": "%016x" % n, "signature": sig, "version": version, "build": "pe-1-2",
         "os": os_, "exception": "0xC0000005", "last_step": "UI.Inspector", "top_frame": "OkayEngine.exe!DrawThing", "report": report(version=version),
         "processed_at": None, "issue_number": None}
    r.update(kw)
    return r


def outside_code(body):
    """The body with fenced blocks, inline code spans and the hidden marker removed: what GitHub renders as live prose."""
    body = re.sub(r"<!--.*?-->", "", body, flags=re.S)
    out, fence = [], None
    for line in body.split("\n"):
        m = re.match(r"^(`{3,})", line)
        if fence is None and m:
            fence = len(m.group(1)); continue
        if fence is not None:
            if re.match(r"^`{%d,}\s*$" % fence, line):
                fence = None
            continue
        out.append(re.sub(r"`[^`\n]*`", "", line))
    return "\n".join(out)


class FakeGH:
    def __init__(self, issues=None, fail_create=False, found=None, fail_comment=False):
        self.issues, self.fail_create, self.found, self.fail_comment = issues or [], fail_create, found or {}, fail_comment
        self.created, self.comments, self.next = [], [], 100

    def list_crash_issues(self): return self.issues
    def search_signature(self, sig): return self.found.get(sig)

    def create_issue(self, title, body):
        if self.fail_create:
            raise RuntimeError("HTTP 500 from api.github.com")
        self.next += 1
        self.created.append((self.next, title, body))
        return self.next

    def comment(self, number, body):
        if self.fail_comment:
            raise RuntimeError("HTTP 403 from api.github.com")
        self.comments.append((number, body))


class FakeSB:
    def __init__(self, fail_mark=False): self.marked, self.fail_mark = [], fail_mark
    def total_for(self, sig): return 7
    def mark_processed(self, ids, number):
        if self.fail_mark:
            raise RuntimeError("HTTP 503 from x.supabase.co")
        self.marked.append((list(ids), number))


class Scrub(unittest.TestCase):
    def test_second_pass_catches_what_the_sender_missed(self):
        t = "C:\\Users\\Alice Smith\\x.png /home/bob/p /Users/carol/q alice@example.com 203.0.113.9 ghp_abcdefghijklmnopqrstuvwxyz0123456789 password=hunter2 Bearer abcdefghijkl"   # lint-allow: secret (a made-up value the scrubber must catch)
        s = c.scrub(t)
        for gone in ("Alice", "bob", "carol", "example.com", "203.0.113.9", "ghp_", "hunter2", "abcdefghijkl"):
            self.assertNotIn(gone, s, gone)
        self.assertIn("%USERPROFILE%\\x.png", s)
        self.assertIn("~", s)

    def test_scrubbed_text_is_unchanged(self):
        t = "%USERPROFILE%\\x ~/p <email> <ip> <token> password=<redacted> frame 12: Render3D:gl 0x7f1c2b3d4e5f v2.874.0 10.0.19045.2006 127.0.0.1 std::string"
        self.assertEqual(c.scrub(t), t)

    def test_not_over_eager(self):
        t = "#00 OkayEngine.exe + 0x1A2B3C  _ZN4okay5Scene6UpdateEf +0x40 at 21:14:05 Last step: Auth.Token"
        self.assertEqual(c.scrub(t), t)

    def test_invisible_and_control_characters_go(self):
        self.assertEqual(c.clean("a\u202eb\u200bc\x00d\x1be\r\nf"), "abcde\nf")
        self.assertEqual(c.clean(None), "")
        self.assertEqual(c.clean(12), "12")


class Titles(unittest.TestCase):
    def test_normal(self):
        self.assertEqual(c.issue_title(row()), "Crash: Access violation in OkayEngine.exe!DrawThing (v2.874.0)")

    def test_exception_names(self):
        for exc, name in (("0xC0000005", "Access violation"), ("SIGSEGV (signal 11)", "Access violation"), ("0xC00000FD", "Stack overflow"),
                          ("C++ exception (std::terminate): x", "Unhandled C++ exception"), ("SIGABRT (signal 6)", "Aborted"), ("", "Crash")):
            self.assertEqual(c.exception_name(exc), name)

    def test_hostile_title(self):
        t = c.issue_title(row(top_frame="@everyone #123 [x](javascript:1)\nsecond line `code` " + "z" * 500, version="1.0 @admin\n"))
        self.assertLessEqual(len(t), c.MAX_TITLE)
        self.assertNotIn("@", t)
        self.assertNotIn("#", t)
        self.assertNotIn("\n", t)

    def test_missing_fields(self):
        t = c.issue_title({"signature": SIG_A})
        self.assertTrue(t.startswith("Crash:"))
        self.assertLessEqual(len(t), c.MAX_TITLE)

    def test_exception_text_cannot_inject(self):
        self.assertEqual(c.exception_name("0xC0000005\" , \"injected\": \"yes"), "Access violation")
        self.assertNotIn("@", c.exception_name("custom @mention fault"))


class Body(unittest.TestCase):
    def test_structure(self):
        b = c.issue_body(row())
        self.assertTrue(b.startswith("<!-- okay-crash-signature: %s -->" % SIG_A))
        for want in ("| Signature | `%s` |" % SIG_A, "| Version | `2.874.0` |", "| OS | `Windows 10.0.19045 (x64)` |", "| GPU | `Test / Vendor` |",
                     "### Backtrace\n```text\n#00 OkayEngine.exe + 0x1A2B3C  DrawThing +0x40", "### Last steps", "<summary>Console (last 2 lines)</summary>",
                     "### How to reproduce"):
            self.assertIn(want, b)
        self.assertNotIn("Exe", b)                # the install path is not shown
        self.assertNotIn("bob", b)                # and the user's name in it is gone anyway
        self.assertTrue(c.MARKER_RE.search(b))

    def test_no_live_text_outside_code(self):
        hostile = report(console=["@everyone #123 <script>alert(1)</script> [x](javascript:1) ![i](http://e/x.png) ```", "``` ## heading </details> <!-- c -->",
                                  "\u202e evil", "x" * 5000, "```", "@everyone <script>alert(2)</script> #2", "````", "<details>"],
                         frames=["#00 @mention + 0x1 `tick` | pipe", "```"], step="@step #7 <b>bold</b>",
                         extra_header="Evil: @everyone `x` | y\nScene: <img src=x onerror=alert(1)>")
        r = row(report=hostile, version="1.0`<b>@x#1", os_="@os | `x` <i>", last_step="@s #1", build="<script>")
        live = outside_code(c.issue_body(r))
        for bad in ("@", "#1", "<script", "<img", "<b>", "javascript:", "!["):
            self.assertNotIn(bad, live.replace("### ", "").replace("<details><summary>", "").replace("</summary>", "").replace("</details>", ""), bad)

    def test_fence_is_longer_than_any_backtick_run(self):
        b = c.issue_body(row(report=report(console=["a ``````` b", "```` c"])))
        self.assertIn("````````text\n", b)   # 7 backticks inside -> an 8 backtick fence
        self.assertNotIn("```", outside_code(b))   # the block closed where we expect: no backticks left outside it

    def test_long_lines_and_big_reports_are_cut(self):
        long_line = "[-1s] I " + "w" * 10000
        b = c.issue_body(row(report=report(console=[long_line] + ["[-1s] I line %d %s" % (i, "p" * 300) for i in range(200)])))
        self.assertLessEqual(len(b), c.MAX_BODY)
        self.assertNotIn("w" * (c.MAX_LINE + 1), b)
        huge = row(report=report(frames=["#%02d OkayEngine.exe + 0x%X  F%d +0x1" % (i % 100, i, i) for i in range(5000)], console=["[-1s] I %s" % ("q" * 380)] * 2000))
        hb = c.issue_body(huge)
        self.assertLessEqual(len(hb), c.MAX_BODY)
        self.assertIn(c.MARKER, hb)

    def test_oldest_console_lines_are_dropped_first(self):
        console = ["[-%ds] I line number %d %s" % (i, i, "p" * 350) for i in range(200)]
        b = c.issue_body(row(report=report(console=console)), max_body=30000)
        self.assertLessEqual(len(b), 30000)
        self.assertIn("line number 199", b)
        self.assertNotIn("line number 0 ", b)

    def test_empty_and_garbage_reports(self):
        for rep in ("", "garbage", None, "OkaySpace crash report\n", "--- Console ---\n--- End ---", "\x00\x01\x02" * 100):
            b = c.issue_body(row(report=rep))
            self.assertTrue(b.startswith("<!-- okay-crash-signature"))
            self.assertIn("### How to reproduce", b)

    def test_row_columns_win_over_text(self):
        b = c.issue_body(row(version="9.9.9"))
        self.assertIn("| Version | `9.9.9` |", b)
        self.assertNotIn("| Version | `2.874.0` |", b)

    def test_comment(self):
        rows = [row(2, version="2.874.0"), row(3, version="2.875.0"), row(4, version="2.875.0", os_="Windows 11")]
        cb = c.comment_body(SIG_A, rows, total=9)
        self.assertIn("3 new reports", cb)
        self.assertIn("9 in total", cb)
        self.assertIn("`2.875.0` x2", cb)
        self.assertIn("`Windows 11` x1", cb)
        self.assertIn(c.MARKER, cb)
        self.assertIn("1 new report ", c.comment_body(SIG_A, rows[:1]))
        live = outside_code(c.comment_body(SIG_A, [row(5, version="@evil #1", os_="<script>@x")]))
        self.assertNotIn("@", live)
        self.assertNotIn("<script", live)
        self.assertLessEqual(len(c.comment_body(SIG_A, [row(i, version="v%d" % i, os_="os%d" % i) for i in range(300)])), c.MAX_COMMENT)

    def test_neutralize(self):
        self.assertEqual(c.neutralize("hi @alice and #12 and owner/repo#3 `x`"), "hi `@alice` and `#12` and owner/repo`#3` 'x'")


class Planning(unittest.TestCase):
    def test_new_and_known_signatures(self):
        rows = [row(1, SIG_A, "2026-11-15T10:00:00Z"), row(2, SIG_B, "2026-11-15T11:00:00Z"), row(3, SIG_A, "2026-11-15T12:00:00Z")]
        actions, deferred, invalid = c.plan(rows, {SIG_B: {"number": 7, "state": "open"}})
        self.assertEqual([(a["type"], a["signature"]) for a in actions], [("create", SIG_A), ("comment", SIG_B)])
        self.assertEqual(len(actions[0]["rows"]), 2)   # both rows of the new crash go to ONE issue
        self.assertEqual(actions[1]["number"], 7)
        self.assertEqual((deferred, invalid), ([], []))

    def test_closed_issue_still_gets_a_comment_not_a_second_issue(self):
        actions, _, _ = c.plan([row(1)], {SIG_A: {"number": 3, "state": "closed"}})
        self.assertEqual([(a["type"], a["number"]) for a in actions], [("comment", 3)])

    def test_cap_on_new_issues_leaves_rows_for_the_next_run(self):
        sigs = ["%016x" % (i + 1) for i in range(8)]
        rows = [row(i + 1, s, "2026-11-15T10:%02d:00Z" % i) for i, s in enumerate(sigs)]
        actions, deferred, _ = c.plan(rows, {}, max_new=3)
        self.assertEqual([a["signature"] for a in actions], sigs[:3])
        self.assertEqual(len(deferred), 5)
        # ... and a known signature is still commented on when the cap is reached
        actions, deferred, _ = c.plan(rows + [row(20, sigs[7], "2026-11-15T13:00:00Z")], {sigs[7]: {"number": 1}}, max_new=0)
        self.assertEqual([a["type"] for a in actions], ["comment"])

    def test_invalid_rows_are_set_aside(self):
        rows = [row(1, "nothex"), row(2, None), {"signature": SIG_A}, row(3, SIG_A.upper()), row(4, "0123456789abcdef0"), row(5, SIG_C)]
        actions, _, invalid = c.plan(rows, {})
        self.assertEqual([a["signature"] for a in actions], [SIG_C])
        self.assertEqual(len(invalid), 5)

    def test_order_is_by_time(self):
        rows = [row(2, SIG_B, "2026-11-15T12:00:00Z"), row(1, SIG_A, "2026-11-15T10:00:00Z")]
        actions, _, _ = c.plan(rows, {})
        self.assertEqual([a["signature"] for a in actions], [SIG_A, SIG_B])

    def test_existing_from_issues(self):
        issues = [{"number": 9, "state": "open", "body": "text <!-- okay-crash-signature: %s --> more" % SIG_A},
                  {"number": 4, "state": "closed", "body": "<!-- okay-crash-signature: %s -->" % SIG_A},      # the older one wins
                  {"number": 5, "state": "open", "body": "<!-- okay-crash-signature: %s -->" % SIG_B, "pull_request": {}},   # a PR is not an issue
                  {"number": 6, "state": "open", "body": "no marker"}, {"number": 7, "state": "open", "body": None},
                  {"number": "x", "body": "<!-- okay-crash-signature: %s -->" % SIG_C}, "junk", None,
                  {"number": 8, "state": "open", "body": "<!-- okay-crash-signature: ZZZ -->"}]
        self.assertEqual(c.existing_from_issues(issues), {SIG_A: {"number": 4, "state": "closed"}})
        self.assertEqual(c.existing_from_issues(None), {})


class Process(unittest.TestCase):
    def run_process(self, rows, gh=None, sb=None, **kw):
        gh, sb = gh or FakeGH(), sb or FakeSB()
        logs = []
        stats = c.process(rows, gh, sb, log=logs.append, **kw)
        return stats, gh, sb, logs

    def test_new_crash_makes_an_issue_and_marks_rows(self):
        rows = [row(1, SIG_A), row(2, SIG_A, "2026-11-15T11:00:00Z")]
        stats, gh, sb, _ = self.run_process(rows)
        self.assertEqual((stats["created"], stats["commented"], stats["marked"], stats["failed"]), (1, 1, 2, 0))
        self.assertEqual(len(gh.created), 1)
        self.assertEqual(gh.created[0][0], 101)
        self.assertIn("OkayEngine.exe!DrawThing", gh.created[0][1])
        self.assertEqual(len(gh.comments), 1)             # the second row of the same crash: one comment, not a second issue
        self.assertIn("1 new report", gh.comments[0][1])
        self.assertEqual(sb.marked, [([rows[0]["id"], rows[1]["id"]], 101)])

    def test_known_crash_gets_a_comment_only(self):
        issues = [{"number": 42, "state": "closed", "body": "<!-- okay-crash-signature: %s -->" % SIG_A}]
        stats, gh, sb, _ = self.run_process([row(1, SIG_A), row(2, SIG_A, "2026-11-15T11:00:00Z")], FakeGH(issues))
        self.assertEqual((stats["created"], stats["commented"], stats["marked"]), (0, 1, 2))
        self.assertEqual(gh.comments[0][0], 42)
        self.assertIn("2 new reports", gh.comments[0][1])
        self.assertIn("7 in total", gh.comments[0][1])
        self.assertEqual(sb.marked[0][1], 42)

    def test_hand_filed_issue_is_found_by_its_marker(self):
        gh = FakeGH(found={SIG_A: {"number": 77, "state": "open"}})
        stats, gh, sb, _ = self.run_process([row(1, SIG_A)], gh)
        self.assertEqual((stats["created"], stats["commented"]), (0, 1))
        self.assertEqual(gh.comments[0][0], 77)

    def test_github_failure_leaves_rows_unprocessed_and_does_not_stop_the_others(self):
        class Flaky(FakeGH):
            def create_issue(self, title, body):
                if "Scene" in title:
                    raise RuntimeError("HTTP 500")
                return FakeGH.create_issue(self, title, body)
        rows = [row(1, SIG_A, top_frame="OkayEngine.exe!Scene"), row(2, SIG_B, "2026-11-15T11:00:00Z")]
        stats, gh, sb, logs = self.run_process(rows, Flaky())
        self.assertEqual((stats["created"], stats["failed"], stats["marked"]), (1, 1, 1))
        self.assertEqual([ids for ids, _ in sb.marked], [[rows[1]["id"]]])
        self.assertTrue(any("::warning::" in l for l in logs))

    def test_marking_failure_is_reported(self):
        stats, gh, sb, logs = self.run_process([row(1)], sb=FakeSB(fail_mark=True))
        self.assertEqual((stats["created"], stats["failed"], stats["marked"]), (1, 1, 0))
        self.assertTrue(any("comment again" in l for l in logs))

    def test_dry_run_changes_nothing(self):
        stats, gh, sb, logs = self.run_process([row(1), row(2, SIG_B, "2026-11-15T11:00:00Z")], dry_run=True)
        self.assertEqual((stats["created"], stats["marked"]), (0, 0))
        self.assertEqual((gh.created, gh.comments, sb.marked), ([], [], []))
        self.assertEqual(sum("would create issue" in l for l in logs), 2)

    def test_no_rows_no_calls(self):
        class Boom(FakeGH):
            def list_crash_issues(self): raise AssertionError("must not be called")
        stats, _, _, _ = self.run_process([], Boom())
        self.assertEqual(sum(stats.values()), 0)

    def test_issue_cap_and_deferred_rows(self):
        rows = [row(i + 1, "%016x" % (i + 1), "2026-11-15T10:%02d:00Z" % i) for i in range(7)]
        stats, gh, sb, _ = self.run_process(rows, max_new=5)
        self.assertEqual((stats["created"], stats["deferred"], stats["marked"]), (5, 2, 5))
        # the next run, with the five marked rows gone, takes the last two
        stats2, gh2, _, _ = self.run_process(rows[5:], FakeGH(issues=[]), max_new=5)
        self.assertEqual(stats2["created"], 2)

    def test_the_issue_never_holds_what_a_hostile_row_tried(self):
        evil = row(1, report=report(console=["token=ghp_abcdefghijklmnopqrstuvwxyz0123456789 mail me bob@example.com from 198.51.100.4 C:\\Users\\bob\\x"]))   # lint-allow: secret (a made-up value the scrubber must catch)
        _, gh, _, _ = self.run_process([evil])
        text = gh.created[0][1] + gh.created[0][2]
        for gone in ("ghp_", "bob@", "198.51.100.4", "bob\\"):
            self.assertNotIn(gone, text)


class Main(unittest.TestCase):
    def test_missing_secret_is_a_green_noop(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = c.main([], env={})
        self.assertEqual(code, 0)
        self.assertIn("::notice", out.getvalue())
        self.assertIn("SUPABASE_SERVICE_KEY", out.getvalue())

    def test_bad_configuration_fails_loudly_and_never_prints_the_key(self):
        key = "sb_secret_TOPSECRETVALUE0123456789"
        for env, needle in (({"SUPABASE_SERVICE_KEY": key, "SUPABASE_URL": "http://evil.example.com", "GITHUB_TOKEN": "t", "GITHUB_REPOSITORY": "o/r"}, "SUPABASE_URL"),
                            ({"SUPABASE_SERVICE_KEY": key, "SUPABASE_URL": "https://abc.supabase.co", "GITHUB_REPOSITORY": "o/r"}, "GITHUB_TOKEN"),
                            ({"SUPABASE_SERVICE_KEY": key, "SUPABASE_URL": "https://abc.supabase.co", "GITHUB_TOKEN": "t", "GITHUB_REPOSITORY": "not a repo"}, "GITHUB_REPOSITORY")):
            out = io.StringIO()
            with redirect_stdout(out):
                code = c.main([], env=env)
            self.assertEqual(code, 1)
            self.assertIn(needle, out.getvalue())
            self.assertNotIn("TOPSECRET", out.getvalue())

    def test_supabase_url_check(self):
        for ok in ("https://tgthoueacrlanpojxoen.supabase.co", "https://abc123.supabase.co/"):
            self.assertTrue(c.check_supabase_url(ok), ok)
        for bad in ("http://abc.supabase.co", "https://abc.supabase.co.evil.com", "https://evil.com/abc.supabase.co", "https://abc.supabase.co@evil.com",
                    "https://abc.supabase.co/rest", "https://abc.supabase.co?x=1", "", "ftp://abc.supabase.co", "https://.supabase.co"):
            self.assertFalse(c.check_supabase_url(bad), bad)
        self.assertTrue(c.check_supabase_url("http://127.0.0.1:1", allow_any=True))

    def test_default_url_comes_from_cmake(self):
        url = c.default_supabase_url(ROOT)
        self.assertTrue(c.check_supabase_url(url), url)

    def test_mask_hides_secrets(self):
        c._SECRETS[:] = ["supersecretvalue123"]
        try:
            m = c.mask("failed with supersecretvalue123 and ghp_abcdefghijklmnopqrstuvwxyz0123 and eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.abcDEF")   # lint-allow: secret (a made-up value the scrubber must catch)
        finally:
            c._SECRETS[:] = []
        self.assertNotIn("supersecretvalue123", m)
        self.assertNotIn("ghp_", m)
        self.assertNotIn("eyJ", m)

    def test_dry_run_on_the_sample_file(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = c.main(["--rows-file", os.path.join(ROOT, "tests", "data", "crash_rows_sample.json"), "--dry-run"], env={})
        text = out.getvalue()
        self.assertEqual(code, 0)
        self.assertEqual(text.count("== would create issue"), 2)       # two signatures
        self.assertIn("== and comment with 1 more", text)             # the second report of the first crash
        self.assertIn("dry run: 3 rows read", text)
        for gone in ("someuser", "ghp_", "203.0.113.9"):               # the row the sender did not scrub
            self.assertNotIn(gone, text)

    def test_rows_file_needs_dry_run(self):
        with self.assertRaises(SystemExit):
            with redirect_stdout(io.StringIO()), open(os.devnull, "w") as dn:
                old, sys.stderr = sys.stderr, dn
                try:
                    c.main(["--rows-file", "x.json"], env={})
                finally:
                    sys.stderr = old


if __name__ == "__main__":
    unittest.main(verbosity=1)
