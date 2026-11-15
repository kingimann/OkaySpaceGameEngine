#!/usr/bin/env python3
"""scripts/crash_to_issue.py end to end over real HTTP, against two fake servers on 127.0.0.1 (no network, no secrets):
a PostgREST-like crash_reports table and a GitHub-like issues API. It checks what the unit tests cannot: the URLs, query strings and
headers the script really sends, the JSON it reads back, the count header, and what a run leaves behind.

  python3 -I tests/scripts/test_crash_to_issue_http.py

The fakes follow the PostgREST and GitHub REST documentation for the few calls the script makes; they are not the real services, so this
shows the script's side of the conversation is well formed, not that the real servers accept it (that is checked by the first real run:
Actions > Crash reports to issues > Run workflow with 'dry run' ticked)."""
import contextlib
import datetime
import io
import json
import os
import re
import sys
import threading
import unittest
import urllib.parse
from contextlib import redirect_stdout
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import crash_to_issue as c  # noqa: E402

os.environ["no_proxy"] = "127.0.0.1,localhost"   # the sandbox's proxy must not see these calls
os.environ["NO_PROXY"] = "127.0.0.1,localhost"

SERVICE_KEY = "sb_secret_localtestkey0123456789"  # lint-allow: secret (made up; a non-JWT key goes in apikey only)
GH_TOKEN = "ghs_localtesttoken_not_real_0123456789"  # lint-allow: secret (made up)
SIG_A, SIG_B, SIG_C = "0123456789abcdef", "fedcba9876543210", "aaaaaaaaaaaaaaaa"


def serve(handler_cls):
    srv = ThreadingHTTPServer(("127.0.0.1", 0), handler_cls)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


class FakeSupabase(BaseHTTPRequestHandler):
    rows = []          # shared per test (reset in setUp)
    calls = []         # (method, path, headers dict)
    deleted = []       # every DELETE as (kind, cutoff): ("processed", time) or ("unprocessed", time)
    no_table = False   # answer like PostgREST does when the table has not been created
    really_delete = False   # only the test that checks the filters against real dates lets a DELETE remove rows (the other rows have fixed dates)

    def log_message(self, *a):
        pass

    def _send(self, status, body=None, headers=None):
        raw = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _auth(self):
        FakeSupabase.calls.append((self.command, self.path, {k.lower(): v for k, v in self.headers.items()}))
        if self.headers.get("apikey") != SERVICE_KEY:
            self._send(401, {"message": "Invalid API key"})
            return False
        if self.headers.get("Authorization"):   # a non-JWT secret key must never be sent as a bearer token
            self._send(401, {"message": "Invalid JWT"})
            return False
        if FakeSupabase.no_table:
            self._send(404, {"code": "PGRST205", "message": "Could not find the table 'public.crash_reports' in the schema cache"})
            return False
        return True

    def do_GET(self):
        if not self._auth():
            return
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        assert u.path == "/rest/v1/crash_reports", u.path
        rows = list(FakeSupabase.rows)
        if q.get("processed_at") == ["is.null"]:
            rows = [r for r in rows if r["processed_at"] is None]
        if "signature" in q:
            rows = [r for r in rows if "eq.%s" % r["signature"] == q["signature"][0]]
        total = len(rows)
        rows.sort(key=lambda r: r["created_at"], reverse=q.get("order") == ["created_at.desc"])
        rows = rows[: int(q.get("limit", ["1000"])[0])]
        hdr = {"Content-Range": "0-%d/%d" % (max(len(rows) - 1, 0), total)} if self.headers.get("Prefer") == "count=exact" else {}
        self._send(200, rows, hdr)

    def do_PATCH(self):
        if not self._auth():
            return
        u = urllib.parse.urlsplit(self.path)
        idq = urllib.parse.parse_qs(u.query)["id"][0]
        m = re.match(r"^in\.\((.*)\)$", idq) or re.match(r"^eq\.(.*)$", idq)
        ids = m.group(1).split(",")
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert set(body) in ({"processed_at", "issue_number"}, {"attempts"}), body
        for r in FakeSupabase.rows:
            if r["id"] in ids:
                r.update(body)
        self._send(204)

    def do_DELETE(self):
        if not self._auth():
            return
        q = {k: v[0] for k, v in urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).items()}
        if set(q) == {"processed_at"} and q["processed_at"].startswith("lt."):
            kind, cutoff = "processed", q["processed_at"][3:]       # processed rows older than this
            hit = lambda r: bool(r.get("processed_at")) and r["processed_at"] < cutoff
        elif set(q) == {"created_at", "processed_at"} and q["processed_at"] == "is.null" and q["created_at"].startswith("lt."):
            kind, cutoff = "unprocessed", q["created_at"][3:]       # rows nobody processed, created before this
            hit = lambda r: not r.get("processed_at") and r["created_at"] < cutoff
        else:
            raise AssertionError("an unfiltered or unexpected delete: %r" % q)
        FakeSupabase.deleted.append((kind, cutoff))
        if FakeSupabase.really_delete:
            FakeSupabase.rows = [r for r in FakeSupabase.rows if not hit(r)]
        self._send(204)


class FakeGitHub(BaseHTTPRequestHandler):
    issues = []        # {"number", "title", "body", "state", "labels"}
    comments = []      # (number, body)
    labels_posted = []
    fail_issue_create = False
    calls = []

    def log_message(self, *a):
        pass

    def _send(self, status, body):
        raw = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n)) if n else None

    def _auth(self):
        FakeGitHub.calls.append((self.command, self.path))
        if self.headers.get("Authorization") != "Bearer " + GH_TOKEN:
            self._send(401, {"message": "Bad credentials"})
            return False
        return True

    def do_GET(self):
        if not self._auth():
            return
        u = urllib.parse.urlsplit(self.path)
        if u.path == "/repos/o/r/issues":
            q = urllib.parse.parse_qs(u.query)
            assert q["labels"] == ["crash"] and q["state"] == ["all"], q
            items = [i for i in FakeGitHub.issues if "crash" in i["labels"]] if q["page"] == ["1"] else []
            self._send(200, items)
        elif u.path == "/search/issues":
            self._send(200, {"items": []})
        else:
            self._send(404, {"message": "Not Found"})

    def do_POST(self):
        if not self._auth():
            return
        body = self._body()
        if self.path == "/repos/o/r/labels":
            FakeGitHub.labels_posted.append(body["name"])
            self._send(201 if len(FakeGitHub.labels_posted) == 1 else 422, {"name": body["name"]})
        elif self.path == "/repos/o/r/issues":
            if FakeGitHub.fail_issue_create:
                self._send(500, {"message": "boom"})
                return
            n = 100 + len(FakeGitHub.issues) + 1
            FakeGitHub.issues.append({"number": n, "title": body["title"], "body": body["body"], "state": "open", "labels": body["labels"],
                                      "created_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")})
            self._send(201, {"number": n})
        else:
            m = re.match(r"^/repos/o/r/issues/(\d+)/comments$", self.path)
            assert m, self.path
            FakeGitHub.comments.append((int(m.group(1)), body["body"]))
            self._send(201, {"id": 1})


def row(n, sig, created, version="2.874.0"):
    rep = ("OkaySpace crash report\nVersion: %s\nWhen: 2026-11-15 10:00:00\nLast step: UI.Inspector\nException: 0xC0000005\nOS: Windows 10.0.19045 (x64)\n"
           "GPU: Test | Vendor\n\nBacktrace:\n  #00 OkayEngine.exe + 0x1A2B3C  DrawThing +0x40\n\n--- Breadcrumbs (oldest first: frame, step) ---\nframe 1: UI.Inspector\n"
           "--- Console (last 1 lines) ---\n[-1s] E boom\n--- End of report ---\n" % version)
    return {"id": "%08d-0000-4000-8000-000000000000" % n, "created_at": created, "report_id": "%016x" % n, "signature": sig, "version": version, "build": "pe-1-2",
            "os": "Windows 10.0.19045 (x64)", "exception": "0xC0000005", "last_step": "UI.Inspector", "top_frame": "OkayEngine.exe!DrawThing", "report": rep,
            "processed_at": None, "issue_number": None}


class Pipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sb, cls.gh = serve(FakeSupabase), serve(FakeGitHub)

    @classmethod
    def tearDownClass(cls):
        cls.sb.shutdown()
        cls.gh.shutdown()

    def setUp(self):
        FakeSupabase.rows, FakeSupabase.calls, FakeSupabase.deleted, FakeSupabase.no_table, FakeSupabase.really_delete = [], [], [], False, False
        FakeGitHub.issues, FakeGitHub.comments, FakeGitHub.labels_posted, FakeGitHub.calls, FakeGitHub.fail_issue_create = [], [], [], [], False
        self.env = {"SUPABASE_SERVICE_KEY": SERVICE_KEY, "SUPABASE_URL": "http://127.0.0.1:%d" % self.sb.server_address[1], "GITHUB_TOKEN": GH_TOKEN,
                    "GITHUB_REPOSITORY": "o/r", "GITHUB_API_URL": "http://127.0.0.1:%d" % self.gh.server_address[1]}

    def run_main(self, *extra):
        out = io.StringIO()
        with redirect_stdout(out):
            code = c.main(["--allow-any-host"] + list(extra), env=dict(self.env))
        return code, out.getvalue()

    def test_two_crashes_one_issue_each_and_a_comment(self):
        FakeSupabase.rows = [row(1, SIG_A, "2026-11-15T10:00:00+00:00"), row(2, SIG_B, "2026-11-15T10:30:00+00:00"), row(3, SIG_A, "2026-11-15T11:00:00+00:00", "2.875.0")]
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertEqual(len(FakeGitHub.issues), 2)                       # one issue per signature
        self.assertEqual({i["labels"][0] for i in FakeGitHub.issues}, {"crash"})
        first = FakeGitHub.issues[0]
        self.assertEqual(first["title"], "Crash: Access violation in OkayEngine.exe!DrawThing (v2.874.0)")
        self.assertIn("<!-- okay-crash-signature: %s -->" % SIG_A, first["body"])
        self.assertEqual(len(FakeGitHub.comments), 1)                     # the second report of the first crash
        self.assertEqual(FakeGitHub.comments[0][0], first["number"])
        self.assertIn("1 new report of this crash", FakeGitHub.comments[0][1])
        self.assertIn("2 in the last 7 days", FakeGitHub.comments[0][1])   # the count header was read back
        self.assertIn("`2.875.0` x1", FakeGitHub.comments[0][1])
        self.assertTrue(all(r["processed_at"] for r in FakeSupabase.rows))   # every row marked
        self.assertEqual(sorted(r["issue_number"] for r in FakeSupabase.rows if r["signature"] == SIG_A), [first["number"]] * 2)
        self.assertEqual(FakeGitHub.labels_posted, ["crash"])             # the label is created first
        self.assertEqual(sorted(k for k, _ in FakeSupabase.deleted), ["processed", "unprocessed"])   # old rows are pruned: processed 7 days, unprocessed 14
        for _, cutoff in FakeSupabase.deleted:
            self.assertRegex(cutoff, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
        for method, path, hdr in FakeSupabase.calls:                       # the key travels in apikey only, never as a bearer token
            self.assertEqual(hdr.get("apikey"), SERVICE_KEY)
            self.assertNotIn("authorization", hdr)

    def test_second_run_finds_nothing_and_a_new_report_of_a_known_crash_is_a_comment(self):
        FakeSupabase.rows = [row(1, SIG_A, "2026-11-15T10:00:00+00:00")]
        self.assertEqual(self.run_main()[0], 0)
        n_issues, n_comments = len(FakeGitHub.issues), len(FakeGitHub.comments)
        code, out = self.run_main()
        self.assertEqual(code, 0)
        self.assertIn("0 unprocessed", out)
        self.assertEqual((len(FakeGitHub.issues), len(FakeGitHub.comments)), (n_issues, n_comments))   # nothing new, nothing sent twice
        FakeSupabase.rows.append(row(2, SIG_A, "2026-11-15T12:00:00+00:00", "2.876.0"))
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertEqual(len(FakeGitHub.issues), 1)                       # still one issue
        self.assertEqual(len(FakeGitHub.comments), n_comments + 1)        # found again by its label and marker, commented on
        self.assertEqual(FakeGitHub.comments[-1][0], FakeGitHub.issues[0]["number"])

    def test_github_failure_leaves_rows_for_the_next_run_and_fails_the_job(self):
        FakeSupabase.rows = [row(1, SIG_A, "2026-11-15T10:00:00+00:00")]
        FakeGitHub.fail_issue_create = True
        code, out = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("::warning::", out)
        self.assertNotIn(SERVICE_KEY, out)
        self.assertNotIn(GH_TOKEN, out)
        self.assertIsNone(FakeSupabase.rows[0]["processed_at"])           # not marked: it is retried
        self.assertEqual(len(FakeSupabase.deleted), 2)                    # the pruning ran first, before the work that failed
        FakeGitHub.fail_issue_create = False
        self.assertEqual(self.run_main()[0], 0)
        self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertTrue(FakeSupabase.rows[0]["processed_at"])

    def test_dry_run_reads_but_changes_nothing(self):
        FakeSupabase.rows = [row(1, SIG_A, "2026-11-15T10:00:00+00:00")]
        code, out = self.run_main("--dry-run")
        self.assertEqual(code, 0)
        self.assertIn("would create issue", out)
        self.assertEqual((FakeGitHub.issues, FakeGitHub.comments, FakeGitHub.labels_posted, FakeSupabase.deleted), ([], [], [], []))
        self.assertIsNone(FakeSupabase.rows[0]["processed_at"])
        self.assertFalse([m for m, _ in FakeGitHub.calls if m == "POST"])

    def test_wrong_service_key_is_reported_without_printing_it(self):
        self.env["SUPABASE_SERVICE_KEY"] = "sb_secret_wrongkey0123456789"  # lint-allow: secret (made up)
        code, out = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("::error::", out)
        self.assertNotIn("wrongkey", out)

    def test_issue_cap_defers_rows(self):
        FakeSupabase.rows = [row(i, "%016x" % i, "2026-11-15T10:%02d:00+00:00" % i) for i in range(1, 8)]
        code, out = self.run_main("--max-new", "3")
        self.assertEqual(code, 0, out)
        self.assertEqual(len(FakeGitHub.issues), 3)
        self.assertEqual(len([r for r in FakeSupabase.rows if r["processed_at"] is None]), 4)
        self.assertIn("deferred 4", out)


    # ---- storage and spam (review findings 2 and 6) ------------------------------------------------------------------------------
    def test_the_newest_rows_are_read_first_so_a_junk_backlog_cannot_starve_a_real_report(self):
        junk = [row(i, "%016x" % (0x1000 + i), "2026-11-10T%02d:%02d:00+00:00" % (i // 60, i % 60)) for i in range(1, 251)]   # 250 older rows, 250 signatures
        real = row(900, SIG_A, "2026-11-15T09:00:00+00:00")
        FakeSupabase.rows = junk + [real]
        code, out = self.run_main("--max-new", "5")
        self.assertEqual(code, 0, out)
        self.assertTrue([i for i in FakeGitHub.issues if "<!-- okay-crash-signature: %s -->" % SIG_A in i["body"]], "the real crash got no issue")
        self.assertEqual(len(FakeGitHub.issues), 5)                                  # the cap still holds
        read = [path for method, path, _ in FakeSupabase.calls if method == "GET" and "processed_at=is.null" in path][0]
        self.assertIn("order=created_at.desc", read)
        self.assertIn("limit=200", read)
        self.assertEqual(len([r for r in FakeSupabase.rows if r["processed_at"] is None]), 246)   # the other 245 junk rows and none of the real one wait

    def test_the_prune_runs_before_the_work_and_deletes_at_7_and_14_days(self):
        FakeSupabase.rows = [row(1, SIG_A, "2026-11-15T10:00:00+00:00")]
        FakeGitHub.fail_issue_create = True      # the run fails ...
        code, out = self.run_main()
        self.assertEqual(code, 1)
        first_delete = min(i for i, (m, _, _) in enumerate(FakeSupabase.calls) if m == "DELETE")
        first_read = min(i for i, (m, p_, _) in enumerate(FakeSupabase.calls) if m == "GET" and "processed_at=is.null" in p_)
        self.assertLess(first_delete, first_read)                                  # ... but the old rows were already deleted, before anything else
        now = datetime.datetime.now(datetime.timezone.utc)
        ages = {}
        for kind, cutoff in FakeSupabase.deleted:
            ages[kind] = (now - datetime.datetime.strptime(cutoff, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)).total_seconds() / 86400
        self.assertAlmostEqual(ages["processed"], 7, delta=0.01)
        self.assertAlmostEqual(ages["unprocessed"], 14, delta=0.01)

    def test_old_rows_really_go(self):
        now = datetime.datetime.now(datetime.timezone.utc)

        def ago(days):
            return (now - datetime.timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")
        old_done = dict(row(1, SIG_A, ago(30)), processed_at=ago(8), issue_number=5)     # processed 8 days ago: gone (7)
        recent_done = dict(row(4, SIG_A, ago(10)), processed_at=ago(6), issue_number=5)  # processed 6 days ago: kept
        old_waiting = row(2, SIG_B, ago(15))                                              # waited 15 days: gone (14)
        young_waiting = row(5, SIG_B, ago(13))                                            # waited 13 days: kept (and then turned into an issue)
        fresh = row(3, SIG_C, ago(0))
        FakeSupabase.rows = [old_done, recent_done, old_waiting, young_waiting, fresh]
        FakeSupabase.really_delete = True
        code, out = self.run_main("--dry-run")      # a dry run deletes nothing
        self.assertEqual((code, len(FakeSupabase.rows)), (0, 5))
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertEqual(sorted(r["id"] for r in FakeSupabase.rows), sorted([recent_done["id"], young_waiting["id"], fresh["id"]]))

    def test_a_daily_cap_on_new_issues(self):
        today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        FakeGitHub.issues = [{"number": 1 + i, "title": "t", "body": "<!-- okay-crash-signature: %016x -->" % (0x7000 + i), "state": "open", "labels": ["crash"],
                              "created_at": today} for i in range(19)]
        FakeSupabase.rows = [row(i, "%016x" % i, "2026-11-15T10:%02d:00+00:00" % i) for i in range(1, 6)]
        code, out = self.run_main("--max-new", "5")
        self.assertEqual(code, 0, out)
        self.assertEqual(len(FakeGitHub.issues), 20)                              # 19 were already made today: one more, then the day's budget is spent
        self.assertEqual(len([r for r in FakeSupabase.rows if r["processed_at"] is None]), 4)

    def test_a_poison_row_is_quarantined_and_never_read_again(self):
        bad = row(1, SIG_A, "2026-11-15T10:00:00+00:00")
        bad["report"] = bad["report"].replace("boom", "POISONPOISONPOISON")
        good = row(2, SIG_B, "2026-11-15T11:00:00+00:00")
        FakeSupabase.rows = [bad, good]
        real = c.scrub

        def fake(text):
            if "POISONPOISONPOISON" in str(text):
                raise ValueError("a bug triggered by this row")
            return real(text)
        c.scrub = fake
        try:
            for run in range(3):
                code, out = self.run_main("--max-attempts", "3")
                self.assertEqual(code, 0, out)                                    # a bad row does not turn the job red
                self.assertNotIn("POISONPOISONPOISON", out)
        finally:
            c.scrub = real
        self.assertEqual(len(FakeGitHub.issues), 1)                               # the good row's issue, once
        self.assertTrue(FakeSupabase.rows[0]["processed_at"])                     # quarantined: marked processed, no issue
        self.assertEqual(FakeSupabase.rows[0]["issue_number"], 0)
        self.assertIn("quarantined", out)
        reads = [p_ for m, p_, _ in FakeSupabase.calls if m == "GET" and "processed_at=is.null" in p_]
        self.assertEqual(len(reads), 3)
        code, out = self.run_main()
        self.assertIn("0 unprocessed", out)                                       # the next run does not see it at all

    def test_a_missing_table_says_what_to_do(self):
        FakeSupabase.no_table = True
        code, out = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("docs/sql/crash_reports.sql", out)
        self.assertNotIn(SERVICE_KEY, out)


class Spy(BaseHTTPRequestHandler):
    seen = []

    def log_message(self, *a):
        pass

    def do_GET(self):
        Spy.seen.append({k.lower(): v for k, v in self.headers.items()})
        raw = b"[]"
        self.send_response(200)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class Redirector(BaseHTTPRequestHandler):
    target = ""

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/same"):   # to another page of the same server
            self.send_response(301)
            self.send_header("Location", "/landed")
        elif self.path.startswith("/landed"):
            raw = b"[]"
            self.send_response(200)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        else:
            self.send_response(302)
            self.send_header("Location", Redirector.target)
        self.send_header("Content-Length", "0")
        self.end_headers()


class Redirects(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spy, cls.redir = serve(Spy), serve(Redirector)
        Redirector.target = "http://127.0.0.1:%d/rest/v1/crash_reports" % cls.spy.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.spy.shutdown()
        cls.redir.shutdown()

    def test_keys_are_not_sent_to_another_host_after_a_redirect(self):
        Spy.seen = []
        url = "http://127.0.0.1:%d/anything" % self.redir.server_address[1]
        with self.assertRaises(RuntimeError) as cm:
            c._http("GET", url, {"apikey": SERVICE_KEY, "Authorization": "Bearer " + GH_TOKEN})
        self.assertIn("302", str(cm.exception))
        self.assertNotIn(SERVICE_KEY, str(cm.exception))
        self.assertEqual(Spy.seen, [], "the redirect was followed to another host")       # nothing reached it, so no header did either

    def test_a_redirect_inside_the_same_host_is_followed(self):
        url = "http://127.0.0.1:%d/same" % self.redir.server_address[1]
        status, body, _ = c._http("GET", url, {"apikey": SERVICE_KEY})
        self.assertEqual((status, body), (200, []))


if __name__ == "__main__":
    unittest.main(verbosity=1)
