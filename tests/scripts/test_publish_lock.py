#!/usr/bin/env python3
"""Publishing several releases at once, against a FAKE Supabase Storage on 127.0.0.1 and a fake `gh`.

It runs the real scripts/publish-supabase.sh (the NEW publish path, PUBLISH_V2=1) as separate processes, two at a time, and checks the
things that only go wrong when publishes overlap: both versions end up in versions.json with their checksums, "latest" (VERSION.txt,
NOTES.txt) never moves backwards, the release lock is taken, waited for, taken over when stale, and always given back (also after an
error, after SIGTERM and when only the script is signalled). It also runs scripts/promote-latest.sh (GitHub's "latest" release only moves
forward) with a stub `gh`.

Test classes:
  PublishLock       the new script against the fake: lock, takeover claims, retries, cut-off answers, signals, the server clock, recovery
  PromoteLatest     scripts/promote-latest.sh with a stub `gh`
  Seam              the switch: scripts/publish-supabase-v1.sh is byte for byte the old script, and with the switch off it is what runs
  CheckUnreleased   scripts/check-unreleased.sh (a version that is already released is stopped before the build)
  Rehearsal         scripts/release-rehearsal.sh, what .github/workflows/release-rehearsal.yml runs, against the fake
  DocRecipes        the commands docs/release_channels.md tells a person to type, run as written
  RehearsalWorkflow, Workflow    properties of release.yml and release-rehearsal.yml (the switch is one line and off, no workflow-level
                    concurrency group, the rehearsal can only touch a throwaway bucket, ...)

  python3 -I tests/scripts/test_publish_lock.py                          everything (a few minutes)
  python3 -I tests/scripts/test_publish_lock.py PublishLock.test_stale_lock_is_taken_over    one test

No network, no real key, nothing outside a temp folder (https goes to a dead proxy, so nothing can reach a real service). The fake
follows the Supabase Storage REST API as documented for the few calls the scripts make: create a bucket; empty and delete one; upload
with and without `x-upsert`; delete by prefixes; list; the public (CDN-cached) read and the authenticated read. It is NOT the real
service, so this shows that the script's side is correct and safe against a service that behaves as documented, not that the real
service does: that is what the "Release rehearsal" workflow is for. Two behaviours are modelled both ways because the public
documentation disagrees with itself: an upload of an existing object answers 409, or 400 with the 409 in the body ("duplicate_http"),
and a missing object 404, or 400 with the 404 in the body ("missing_http").
"""
import hashlib
import json
import os
import random
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

os.environ["no_proxy"] = os.environ["NO_PROXY"] = "127.0.0.1,localhost"   # a sandbox proxy must not see these calls

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PUBLISH = os.environ.get("PUBLISH_SCRIPT") or os.path.join(REPO, "scripts", "publish-supabase.sh")   # (PUBLISH_SCRIPT: to try an older copy)
PROMOTE = os.path.join(REPO, "scripts", "promote-latest.sh")
SIGN = os.path.join(REPO, "scripts", "sign_index.py")
RELEASE_YML = os.path.join(REPO, ".github", "workflows", "release.yml")
BUCKET, LOCK = "releases", "release.lock"
DEAD_PROXY = "http://127.0.0.1:9"   # (the discard port, nothing listens: a call that tried to leave this machine over https would fail at once)
EXES = {"OkaySpace.exe": "OkaySpace.exe", "OkayEngine.exe": "Tools/OkayEngine.exe", "OkaySpacePlayer.exe": "Tools/OkaySpacePlayer.exe"}


# ---- the fake storage -------------------------------------------------------------------------------------------------------

class Gate:
    """Holds a request inside the fake until the test says go: a publisher is paused in the middle of its critical section."""

    def __init__(self):
        self.entered = threading.Event()
        self.go = threading.Event()


class Store:
    """Buckets, objects, the request log and the failures a test injects. All access goes through `cond`."""

    def __init__(self):
        self.cond = threading.Condition()
        self.reset()

    def reset(self):
        with self.cond:
            for r in getattr(self, "rules", []):
                for g in (r["gate"], r.get("hold_after")):
                    if g:
                        g.go.set()   # never leave a request hanging from the last test
            self.buckets, self.objects, self.log, self.rules, self.cdn, self.seq = set(), {}, [], [], {}, 0
            self.duplicate_http, self.missing_http = 409, 404
            self.cdn_ttl, self.jitter = 0.0, 0.0
            self.clock_skew = 0   # seconds the server's clock (its Date header) is ahead of this machine's
            self.allow_duplicates = False   # True: a create without upsert overwrites instead of being refused (a service that is not atomic)

    # -- the log (call record() with cond held)
    def record(self, **e):
        self.seq += 1
        e["seq"] = self.seq
        self.log.append(e)
        self.cond.notify_all()

    def events(self, **want):
        with self.cond:
            return [dict(e) for e in self.log if all(e.get(k) == v for k, v in want.items())]

    def wait_for(self, pred, timeout=60):
        with self.cond:
            return self.cond.wait_for(pred, timeout)

    # -- objects, the test's view
    def put(self, key, data, bucket=BUCKET):
        with self.cond:
            self.buckets.add(bucket)
            self.objects[(bucket, key)] = {"data": data if isinstance(data, bytes) else data.encode(), "ctype": "text/plain"}
            self.cond.notify_all()

    def get(self, key, bucket=BUCKET):
        with self.cond:
            o = self.objects.get((bucket, key))
            return None if o is None else o["data"]

    def text(self, key):
        d = self.get(key)
        return None if d is None else d.decode().strip()

    def delete(self, key, bucket=BUCKET):
        with self.cond:
            self.objects.pop((bucket, key), None)
            self.cond.notify_all()

    def names(self, prefix="", bucket=BUCKET):
        with self.cond:
            return sorted(k for (b, k) in self.objects if b == bucket and k.startswith(prefix))

    def list_entries(self, bucket, prefix):   # (with cond held) what POST object/list returns for a folder
        if prefix and not prefix.endswith("/"):
            prefix += "/"
        files, folders = {}, set()
        for (b, key), obj in self.objects.items():
            if b != bucket or not key.startswith(prefix):
                continue
            rest = key[len(prefix):]
            if "/" in rest:
                folders.add(rest.split("/", 1)[0])
            else:
                files[rest] = obj
        out = [{"name": f, "id": None, "metadata": None} for f in folders]
        out += [{"name": n, "id": "id-" + n, "metadata": {"size": len(o["data"])}} for n, o in files.items()]
        return sorted(out, key=lambda e: e["name"])

    # -- failures a test injects: delay, hold at a gate, or answer with a status (instead of, or after, doing the work)
    def rule(self, pattern, method=None, who=None, delay=0.0, status=None, gate=None, times=None, store_then_fail=False,
             after=0, cut=None, drop=False, hold_after=None):
        """pattern: a regex on "<bucket>/<key>". after: skip that many matching requests first. times: apply to that many (None: all).
        delay / gate: before the work. status: answer with it instead of doing the work (store_then_fail: do the work, then answer with it).
        cut: answer 200 for a read but send only that many bytes of the body, then hang up. drop: hang up without any answer.
        hold_after: do the work, then hold the answer at that gate."""
        r = {"pattern": re.compile(pattern), "method": method, "who": who, "delay": delay, "status": status, "gate": gate, "times": times,
             "store_then_fail": store_then_fail, "fired": 0, "after": after, "seen": 0, "cut": cut, "drop": drop, "hold_after": hold_after}
        with self.cond:
            self.rules.append(r)
        return r

    def clear_rules(self):
        with self.cond:
            for r in self.rules:
                for g in (r["gate"], r["hold_after"]):
                    if g:
                        g.go.set()
            self.rules = []

    def gate(self, pattern, method="POST", who=None, after=0):
        g = Gate()
        self.rule(pattern, method=method, who=who, gate=g, times=1, after=after)
        return g

    def gate_after(self, pattern, method="POST", who=None, after=0):
        """Like gate(), but the request is carried out first and only its ANSWER is held."""
        g = Gate()
        self.rule(pattern, method=method, who=who, hold_after=g, times=1, after=after)
        return g

    def match(self, method, target, who):
        with self.cond:
            for r in self.rules:
                if r["method"] not in (None, method) or r["who"] not in (None, who) or not r["pattern"].search(target):
                    continue
                r["seen"] += 1
                if r["seen"] <= r["after"]:
                    continue
                if r["times"] is not None and r["fired"] >= r["times"]:
                    continue
                r["fired"] += 1
                return r
        return None


def make_handler(store):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, *a):
            pass

        def date_time_string(self, timestamp=None):
            return super().date_time_string((time.time() if timestamp is None else timestamp) + store.clock_skew)

        def _hang_up(self):
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

        def _reply(self, status, body, ctype="application/json", extra=None, cut=None):
            raw = body if isinstance(body, (bytes, bytearray)) else json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(raw)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if cut is not None:   # the answer promises the whole body, delivers the first `cut` bytes, and the connection ends
                self.wfile.write(raw[:cut])
                self.wfile.flush()
                self._hang_up()
                return
            self.wfile.write(raw)

        def _err(self, kind):
            if kind == "duplicate":
                self._reply(store.duplicate_http, {"statusCode": "409", "error": "Duplicate", "message": "The resource already exists"})
            elif kind == "missing":
                self._reply(store.missing_http, {"statusCode": "404", "error": "not_found", "message": "Object not found"})
            else:
                self._reply(store.missing_http, {"statusCode": "404", "error": "Bucket not found", "message": "Bucket not found"})

        def _who(self):
            m = re.fullmatch(r"Bearer test-key-([A-Za-z0-9]+)", self.headers.get("Authorization", ""))
            if not m or self.headers.get("apikey", "") != "test-key-" + m.group(1):
                self._reply(401, {"statusCode": "401", "error": "Unauthorized", "message": "Invalid JWT"})
                return None
            return m.group(1)

        def _intercept(self, method, bucket, key, who, kind, **rec):
            """Apply what the test injected for this request. Returns (already answered, status to answer after doing the work,
            bytes of a read to deliver before hanging up, gate to hold the answer at)."""
            if store.jitter:
                time.sleep(random.uniform(0, store.jitter))
            r = store.match(method, "%s/%s" % (bucket, key), who)
            if r is None:
                return False, None, None, None
            if r["delay"]:
                time.sleep(r["delay"])
            if r["gate"]:
                r["gate"].entered.set()
                r["gate"].go.wait(120)
            if r["drop"]:
                with store.cond:
                    store.record(who=who, method=method, kind=kind, bucket=bucket, key=key, status=0, injected=True, **rec)
                self._hang_up()
                return True, None, None, None
            if r["status"] and not r["store_then_fail"]:
                with store.cond:
                    store.record(who=who, method=method, kind=kind, bucket=bucket, key=key, status=r["status"], injected=True, **rec)
                self._reply(r["status"], {"statusCode": str(r["status"]), "error": "injected", "message": "injected failure"})
                return True, None, None, None
            return False, (r["status"] if r["store_then_fail"] else None), r["cut"], r["hold_after"]

        def _handle(self, method):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n) if n else b""
            path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
            if not path.startswith("/storage/v1/"):
                return self._reply(404, {"statusCode": "404", "error": "not_found", "message": "no such route"})
            rest = path[len("/storage/v1/"):]
            if method == "GET" and rest.startswith("object/public/"):
                return self._public_get(rest[len("object/public/"):])
            who = self._who()
            if who is None:
                return None
            if method == "POST" and rest == "bucket":
                spec = json.loads(body or b"{}")
                with store.cond:
                    exists = spec.get("id") in store.buckets
                    store.buckets.add(spec.get("id"))
                    store.record(who=who, method=method, kind="bucket", bucket=spec.get("id"), key="", status=409 if exists else 200)
                return self._err("duplicate") if exists else self._reply(200, {"name": spec.get("id")})
            if method == "POST" and rest.startswith("bucket/") and rest.endswith("/empty"):
                bucket = rest[len("bucket/"):-len("/empty")]
                with store.cond:
                    known = bucket in store.buckets
                    gone = [k for (b, k) in list(store.objects) if b == bucket]
                    for k in gone:
                        del store.objects[(bucket, k)]
                    store.record(who=who, method=method, kind="bucket-empty", bucket=bucket, key="", deleted=gone, status=200 if known else 404)
                return self._reply(200, {"message": "Successfully emptied"}) if known else self._err("bucket")
            if method == "GET" and rest.startswith("bucket/"):
                bucket = rest[len("bucket/"):]
                with store.cond:
                    known = bucket in store.buckets
                    store.record(who=who, method=method, kind="bucket-get", bucket=bucket, key="", status=200 if known else 404)
                return self._reply(200, {"id": bucket, "name": bucket, "public": True}) if known else self._err("bucket")
            if method == "DELETE" and rest.startswith("bucket/"):
                bucket = rest[len("bucket/"):]
                with store.cond:
                    known = bucket in store.buckets
                    full = any(b == bucket for (b, _k) in store.objects)
                    if known and not full:
                        store.buckets.discard(bucket)
                    store.record(who=who, method=method, kind="bucket-delete", bucket=bucket, key="", status=404 if not known else (400 if full else 200))
                if not known:
                    return self._err("bucket")
                return self._reply(400, {"statusCode": "400", "error": "InvalidRequest", "message": "Bucket not empty"}) if full \
                    else self._reply(200, {"message": "Successfully deleted"})
            if method == "POST" and rest.startswith("object/list/"):
                bucket, spec = rest[len("object/list/"):], json.loads(body or b"{}")
                with store.cond:
                    entries = store.list_entries(bucket, spec.get("prefix", ""))
                    store.record(who=who, method=method, kind="list", bucket=bucket, key=spec.get("prefix", ""), status=200)
                return self._reply(200, entries)
            if method in ("POST", "PUT") and rest.startswith("object/"):
                return self._upload(who, method, rest[len("object/"):], body)
            if method == "DELETE" and rest.startswith("object/"):
                return self._delete(who, rest[len("object/"):], body)
            if method == "GET" and rest.startswith("object/"):
                sub = rest[len("object/"):]
                return self._auth_get(who, sub[len("authenticated/"):] if sub.startswith("authenticated/") else sub)
            return self._reply(404, {"statusCode": "404", "error": "not_found", "message": "no such route"})

        def _upload(self, who, method, target, body):
            bucket, _, key = target.partition("/")
            upsert = method == "PUT" or self.headers.get("x-upsert", "").lower() == "true"
            answered, fail, _, hold = self._intercept(method, bucket, key, who, "upload", upsert=upsert)
            if answered:
                return None
            with store.cond:
                if bucket not in store.buckets:
                    outcome, status = "bucket", 404
                elif (bucket, key) in store.objects and not upsert and not store.allow_duplicates:
                    outcome, status = "duplicate", 409
                else:
                    store.objects[(bucket, key)] = {"data": body, "ctype": self.headers.get("Content-Type", "")}
                    outcome, status = "ok", 200
                store.record(who=who, method=method, kind="upload", bucket=bucket, key=key, upsert=upsert, status=status, size=len(body))
            if hold:
                hold.entered.set()
                hold.go.wait(120)
            if outcome == "ok":
                return self._reply(fail or 200, {"Key": "%s/%s" % (bucket, key)})
            return self._err(outcome)

        def _delete(self, who, bucket, body):
            names = json.loads(body or b"{}").get("prefixes", [])
            answered, fail, _, hold = self._intercept("DELETE", bucket, ",".join(names), who, "delete", deleted=[])
            if answered:
                return None
            with store.cond:
                deleted = [n for n in names if store.objects.pop((bucket, n), None) is not None]
                store.record(who=who, method="DELETE", kind="delete", bucket=bucket, key=",".join(names), deleted=deleted, status=200)
            if hold:
                hold.entered.set()
                hold.go.wait(120)
            return self._reply(fail or 200, [{"name": n, "bucket_id": bucket} for n in deleted])

        def _auth_get(self, who, sub):
            bucket, _, key = sub.partition("/")
            answered, _, cut, _ = self._intercept("GET", bucket, key, who, "get")
            if answered:
                return None
            with store.cond:
                obj = store.objects.get((bucket, key))
                store.record(who=who, method="GET", kind="get", bucket=bucket, key=key, status=200 if obj else 404)
            if obj is None:
                return self._err("missing")
            return self._reply(200, obj["data"], obj["ctype"] or "application/octet-stream", cut=cut)

        def _public_get(self, sub):
            """The public address: a CDN in front of the bucket. With cdn_ttl set it keeps what it served (by full URL, query included)
            for that many seconds, unless the request says Cache-Control: no-cache, as a real edge cache does by default."""
            bucket, _, key = sub.partition("/")
            bypass = "no-cache" in (self.headers.get("Cache-Control") or "").lower()
            now = time.monotonic()
            with store.cond:
                hit = store.cdn.get(self.path) if store.cdn_ttl > 0 and not bypass else None
                if hit and hit[0] > now:
                    status, data, ctype, cache = hit[1], hit[2], hit[3], "HIT"
                else:
                    obj = store.objects.get((bucket, key))
                    if obj:
                        status, data, ctype = 200, obj["data"], obj["ctype"] or "application/octet-stream"
                    else:
                        status, data, ctype = store.missing_http, b'{"statusCode":"404","error":"not_found","message":"Object not found"}', "application/json"
                    cache = "MISS"
                    if store.cdn_ttl > 0 and not bypass:
                        store.cdn[self.path] = (now + store.cdn_ttl, status, data, ctype)
                store.record(who=None, method="GET", kind="public", bucket=bucket, key=key, status=status, cache=cache)
            return self._reply(status, data, ctype, {"X-Cache": cache})

        def do_GET(self):
            self._handle("GET")

        def do_POST(self):
            self._handle("POST")

        def do_PUT(self):
            self._handle("PUT")

        def do_DELETE(self):
            self._handle("DELETE")

    return Handler


# ---- publishers -------------------------------------------------------------------------------------------------------------

def make_dist(root, who, version, tag):
    """A build folder for scripts/publish-supabase.sh with the names it expects; `tag` changes the bytes (so the checksums)."""
    d = os.path.join(root, "dist-%s-%s-%s" % (who, version, tag))
    os.makedirs(os.path.join(d, "Tools"))
    with open(os.path.join(d, "VERSION.txt"), "w") as f:
        f.write(version + "\n")
    for rel in EXES.values():
        with open(os.path.join(d, rel), "wb") as f:
            f.write(b"MZ" + ("%s|%s|%s|" % (tag, version, rel)).encode() * 40)
    return d


def make_release_zip(tmp, dist, version):
    """The zip the release workflow makes (`cp -r build-win-dist/* OkaySpace-windows-v<ver>/; zip -r OkaySpace-windows-v<ver>.zip ...`)
    from a dist folder. Returns its path."""
    name = "OkaySpace-windows-v%s" % version
    path = os.path.join(tmp, name + ".zip")
    with zipfile.ZipFile(path, "w") as z:
        for root, _, files in os.walk(dist):
            for f in sorted(files):
                full = os.path.join(root, f)
                z.write(full, os.path.join(name, os.path.relpath(full, dist)))
    return path


class Publisher:
    """One scripts/publish-supabase.sh process, started at once. `who` names it in the fake's log (it is the key's suffix)."""

    def __init__(self, case, who, version, tag="x", dist=None, script=None, **env):
        self.case, self.who, self.version = case, who, version
        self.dist = dist or make_dist(case.tmp, who, version, tag)
        notes = os.path.join(case.tmp, "notes-%s-%s-%s.txt" % (who, version, tag))
        with open(notes, "w") as f:
            f.write("- notes for %s\n" % version)
        e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": case.tmp, "TMPDIR": case.tmp, "LC_ALL": "C", "PYTHONDONTWRITEBYTECODE": "1",
             "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost", "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY,
             "SUPABASE_URL": case.url, "SUPABASE_SERVICE_KEY": "test-key-" + who, "DIST_DIR": self.dist, "NOTES_FILE": notes,
             "ARCHIVE_REPO": "o/n", "KEEP": "3", "OKAY_UPDATE_SIGNING_SEED": case.seed, "PUBLISH_V2": "1",
             "LOCK_POLL_SECONDS": "1", "LOCK_WAIT_SECONDS": "90", "READ_RETRY_BASE_SECONDS": "0"}
        e.update(env)
        self.log_path = os.path.join(case.tmp, "out-%s-%s-%s.log" % (who, version, tag))
        self.out = open(self.log_path, "w")
        self.proc = subprocess.Popen(["bash", script or PUBLISH], env=e, stdout=self.out, stderr=subprocess.STDOUT, start_new_session=True)
        case.publishers.append(self)

    def wait(self, timeout=120):
        try:
            rc = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            self.kill()
            raise AssertionError("publisher %s v%s did not finish in %s s:\n%s" % (self.who, self.version, timeout, self.output()))
        self.out.close()
        return rc

    def output(self):
        with open(self.log_path, "r", errors="replace") as f:
            return f.read()

    def signal(self, sig):
        try:
            os.killpg(self.proc.pid, sig)
        except ProcessLookupError:
            pass

    def kill(self):
        self.signal(signal.SIGKILL)

    def sha(self, name):
        with open(os.path.join(self.dist, EXES[name]), "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()

    def bytes_of(self, name):
        with open(os.path.join(self.dist, EXES[name]), "rb") as f:
            return f.read()


class QuietServer(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        pass   # (a publisher that is killed on purpose leaves a broken connection behind: not worth a traceback)


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        signal.signal(signal.SIGINT, signal.default_int_handler)   # (a child inherits "ignored", not a handler: bash could not trap it then)
        cls.store = Store()
        cls.srv = QuietServer(("127.0.0.1", 0), make_handler(cls.store))
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.url = "http://127.0.0.1:%d" % cls.srv.server_address[1]
        out = subprocess.run([sys.executable, "-I", SIGN, "genkey"], capture_output=True, text=True, check=True).stdout   # a throwaway key
        cls.seed = re.search(r"OKAY_UPDATE_SIGNING_SEED=([0-9a-f]{64})", out).group(1)
        cls.pub = re.search(r"OKAY_UPDATE_TRUSTED_KEYS=([0-9a-f]{64})", out).group(1)

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()

    def setUp(self):
        self.store.reset()
        self.tmp = tempfile.mkdtemp(prefix="okay_publock_")
        self.publishers = []

    def tearDown(self):
        for p in self.publishers:
            p.kill()
        self.store.reset()
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- small helpers
    def publish(self, who, version, tag="x", timeout=120, **env):
        p = Publisher(self, who, version, tag, **env)
        return p.wait(timeout), p.output(), p

    def index(self):
        raw = self.store.get("versions.json")
        self.assertIsNotNone(raw, "versions.json is not in the bucket")
        return json.loads(raw)

    def listed(self, version):
        return next((v for v in self.index()["versions"] if v["version"] == version), None)

    def assert_listed(self, p):
        """p's version is in versions.json with the size and SHA-256 of the files p uploaded, and the files in the bucket are p's."""
        entry = self.listed(p.version)
        self.assertIsNotNone(entry, "v%s is missing from versions.json (lost update?)" % p.version)
        for n in EXES:
            self.assertEqual(entry["files"][n]["sha256"], p.sha(n), "%s of v%s: wrong checksum in versions.json" % (n, p.version))
            self.assertEqual(entry["files"][n]["size"], len(p.bytes_of(n)))

    def assert_files_in_bucket(self, p):
        for n in EXES:
            self.assertEqual(self.store.get("v%s/%s" % (p.version, n)), p.bytes_of(n), "v%s/%s is not the file that was published" % (p.version, n))

    def assert_no_lock(self):
        self.assertIsNone(self.store.get(LOCK), "release.lock is still in the bucket:\n%s" % self.store.text(LOCK))

    def assert_signed(self):
        body, sig = self.store.get("versions.json"), self.store.get("versions.json.sig")
        self.assertIsNotNone(sig, "versions.json.sig was not published")
        for name, data in (("v.json", body), ("v.sig", sig)):
            with open(os.path.join(self.tmp, name), "wb") as f:
                f.write(data)
        r = subprocess.run([sys.executable, "-I", SIGN, "verify", "--file", os.path.join(self.tmp, "v.json"),
                            "--sig", os.path.join(self.tmp, "v.sig"), "--pub", self.pub], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, "the published versions.json does not verify against the published signature: " + r.stderr)

    def spans(self):
        """who -> (seq of its successful lock create, seq of its lock delete or None): the interval in which it held the lock."""
        out = {}
        for e in self.store.events(kind="upload", key=LOCK, upsert=False, status=200):
            out[e["who"]] = [e["seq"], None]
        for e in self.store.events(kind="delete"):
            if LOCK in e["deleted"] and e["who"] in out and out[e["who"]][1] is None and e["seq"] > out[e["who"]][0]:
                out[e["who"]][1] = e["seq"]
        return out

    def assert_exclusive(self, expect_released=True):
        """No two publishers held the lock at the same time, and each only wrote to the bucket (outside the lock object) while holding it."""
        spans = self.spans()
        end = max([e["seq"] for e in self.store.events()] + [0]) + 1
        iv = {w: (a, b if b is not None else end) for w, (a, b) in spans.items()}
        if expect_released:
            for w, (a, b) in spans.items():
                self.assertIsNotNone(b, "publisher %s never gave the lock back" % w)
        for w1 in iv:
            for w2 in iv:
                if w1 < w2:
                    self.assertTrue(iv[w1][1] <= iv[w2][0] or iv[w2][1] <= iv[w1][0],
                                    "publishers %s and %s held the lock at the same time: %s" % (w1, w2, iv))
        for kind in ("upload", "delete"):
            for e in self.store.events(kind=kind):
                if e["key"].startswith(LOCK) or e["key"] == "" or (kind == "delete" and all(d.startswith(LOCK) for d in e["deleted"])) \
                        or e.get("injected"):
                    continue   # (the lock and the takeover claims are what the span is made of; they are not "writes under the lock")
                w = e["who"]
                self.assertIn(w, iv, "publisher %s wrote %s without ever holding the lock" % (w, e["key"]))
                self.assertTrue(iv[w][0] < e["seq"] < iv[w][1], "publisher %s wrote %s outside its time with the lock" % (w, e["key"]))

    def plant_lock(self, age, token="deadbeefdeadbeef", holder="github-run-1-attempt-1", version="2.800.0", epoch=True, now=None):
        e = int(time.time() if now is None else now) - age
        body = "okayspace-release-lock-v1\nversion=%s\nchannel=latest\nholder=%s\ntoken=%s\nutc=%s\n" % (
            version, holder, token, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(e)))
        if epoch:
            body += "epoch=%d\n" % e
        self.store.put(LOCK, body)

    def assert_clean_output(self, *procs):
        for p in procs:
            out = p.output()
            self.assertNotIn(self.seed, out, "the signing seed was printed")
            self.assertNotIn("test-key-", out, "the service key was printed")

    def version_folders(self):
        return sorted({k.split("/")[0] for k in self.store.names() if re.match(r"v\d[^/]*/", k)})

    def run_promote(self, version, gh_latest=None, published=None, isolated=False, **env):
        d = os.path.join(self.tmp, "gh")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "gh"), "w") as f:
            f.write(FAKE_GH)
        os.chmod(os.path.join(d, "gh"), 0o755)
        with open(os.path.join(d, "latest"), "w") as f:
            f.write(("v%s\n" % gh_latest) if gh_latest else "")
        open(os.path.join(d, "calls"), "w").close()   # (a fresh call log for every run)
        if published:
            self.store.put("versions.json", json.dumps({"schema": 1, "latest": published, "versions": []}))
        e = {"PATH": d + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"), "FAKE_GH_DIR": d, "GITHUB_REPOSITORY": "o/n", "HOME": self.tmp, "TMPDIR": self.tmp,
             "RELEASES_BASE": "%s/storage/v1/object/public/%s" % (self.url, BUCKET), "TRIES": "2", "TRY_PAUSE": "0",
             "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost", "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY}
        e.update(env)
        script = PROMOTE
        if isolated:   # a copy with no CMakeLists.txt next to it, so it cannot work out the project's real address by itself
            os.makedirs(os.path.join(self.tmp, "tree", "scripts"), exist_ok=True)
            script = os.path.join(self.tmp, "tree", "scripts", "promote-latest.sh")
            shutil.copy(PROMOTE, script)
        r = subprocess.run(["bash", script, version], env=e, capture_output=True, text=True, timeout=60)
        with open(os.path.join(d, "calls")) as f:
            calls = f.read().splitlines()
        edits = [c for c in calls if c.startswith("release edit")]
        with open(os.path.join(d, "latest")) as f:
            now = f.read().strip()
        return r, edits, now

    def snapshot(self):
        """Every object in the bucket, name -> bytes, with the dates in versions.json made equal (two runs may straddle midnight)."""
        out = {}
        for k in self.store.names():
            data = self.store.get(k)
            if k == "versions.json":
                doc = json.loads(data)
                for e in doc["versions"]:
                    e["date"] = "DATE"
                data = json.dumps(doc, indent=1, sort_keys=True).encode()
            out[k] = data
        return out

    def writes_by(self, who):
        return [e for e in self.store.events(who=who) if e["kind"] in ("upload", "delete") and e["key"] != LOCK and e["key"] != ""]


# ---- the tests: publish-supabase.sh ----------------------------------------------------------------------------------------

class PublishLock(Base):
    def pair(self, first, second, **env):
        """`first` starts and is paused inside its critical section (holding the lock); `second` starts and has to wait; then `first`
        is let go. Returns the two Publisher objects, finished."""
        (w1, v1), (w2, v2) = first, second
        gate = self.store.gate(r"^releases/v%s/OkayEngine\.exe$" % re.escape(v1), who=w1)
        p1 = Publisher(self, w1, v1, **env)
        self.assertTrue(gate.entered.wait(60), "the first publisher never reached its upload:\n" + p1.output())
        lock = self.store.text(LOCK)
        self.assertIsNotNone(lock, "the first publisher is uploading but holds no lock")
        self.assertIn("version=%s" % v1, lock)
        self.assertRegex(lock, r"holder=local-pid-\d+")
        self.assertRegex(lock, r"epoch=\d+")
        p2 = Publisher(self, w2, v2, **env)
        self.assertTrue(self.store.wait_for(lambda: sum(1 for e in self.store.log if e["who"] == w2 and e["key"] == LOCK and e["status"] == 409) >= 2),
                        "the second publisher did not keep asking for the lock:\n" + p2.output())
        self.assertEqual(self.writes_by(w2), [], "the second publisher wrote to the bucket while the first held the lock")
        self.assertIsNone(p2.proc.poll(), "the second publisher gave up or finished while the lock was held:\n" + p2.output())
        gate.go.set()
        self.assertEqual((p1.wait(), p2.wait()), (0, 0), "first:\n%s\nsecond:\n%s" % (p1.output(), p2.output()))
        return p1, p2

    def assert_two_versions_published(self, p_old, p_new):
        idx = self.index()
        self.assertEqual([v["version"] for v in idx["versions"]], [p_new.version, p_old.version])
        self.assertEqual(idx["latest"], p_new.version)
        self.assert_listed(p_old)
        self.assert_listed(p_new)
        self.assert_files_in_bucket(p_old)
        self.assert_files_in_bucket(p_new)
        self.assertEqual(self.store.text("VERSION.txt"), p_new.version)
        self.assertEqual(self.store.text("NOTES.txt"), "- notes for %s" % p_new.version)
        self.assert_no_lock()
        self.assert_exclusive()
        self.assert_signed()

    def test_a_newer_release_publishes_first_and_the_older_one_finishes_last(self):
        new, old = self.pair(("N", "2.876.0"), ("O", "2.875.0"))
        self.assertIn("is held by local-pid-", old.output())
        self.assertIn("v2.876.0", old.output())
        self.assertIn("is OLDER than the version that is latest now (v2.876.0)", old.output())
        self.assertNotIn("OLDER", new.output())
        self.assert_two_versions_published(old, new)
        self.assertEqual([e["key"] for e in self.store.events(who="O", kind="upload") if e["key"] in ("VERSION.txt", "NOTES.txt")], [],
                         "the older release wrote VERSION.txt or NOTES.txt")
        self.assert_clean_output(new, old)

    def test_an_older_release_publishes_first_and_the_newer_one_waits(self):
        old, new = self.pair(("O", "2.875.0"), ("N", "2.876.0"))
        self.assertIn("is held by local-pid-", new.output())
        self.assertNotIn("OLDER", new.output())
        self.assert_two_versions_published(old, new)
        self.assert_clean_output(new, old)

    def test_the_lock_works_when_the_store_answers_400_for_duplicate_and_missing(self):
        # The public documentation says both 409 and 400 for "this object exists already", and 404 or 400 for "no such object".
        self.store.duplicate_http, self.store.missing_http = 400, 400
        new, old = self.pair(("N", "2.876.0"), ("O", "2.875.0"))
        self.assertIn("is held by", old.output())
        self.assert_two_versions_published(old, new)

    def test_started_at_the_same_moment_in_either_order(self):
        for rnd in range(4):
            self.store.reset()
            self.store.jitter = 0.05   # every write takes a random 0-50 ms ...
            self.store.rule(r"^releases/versions\.json$", method="POST", delay=0.4)   # ... and the index upload is slow: wide windows
            a, b = ("O", "2.875.0"), ("N", "2.876.0")
            order = [a, b] if rnd % 2 == 0 else [b, a]
            ps = [Publisher(self, w, v, tag="r%d" % rnd) for w, v in order]
            self.assertEqual([p.wait() for p in ps], [0, 0], "\n".join(p.output() for p in ps))
            old = next(p for p in ps if p.version == "2.875.0")
            new = next(p for p in ps if p.version == "2.876.0")
            self.assert_two_versions_published(old, new)

    def test_a_held_lock_is_waited_for_then_taken_when_it_is_released(self):
        self.plant_lock(age=1, token="someoneelse")
        p = Publisher(self, "A", "2.875.0")
        self.assertTrue(self.store.wait_for(lambda: sum(1 for e in self.store.log if e["who"] == "A" and e["key"] == LOCK and e["status"] == 409) >= 2))
        self.assertEqual(self.writes_by("A"), [])
        self.store.delete(LOCK)   # the other publish is done
        self.assertEqual(p.wait(), 0, p.output())
        self.assertIn("is held by github-run-1-attempt-1 (v2.800.0", p.output())
        self.assertIn("took the release lock", p.output())
        self.assert_listed(p)
        self.assert_no_lock()

    def test_a_fresh_lock_is_not_taken_and_the_wait_has_an_end(self):
        self.plant_lock(age=5, token="someoneelse")
        rc, out, _ = self.publish("A", "2.875.0", LOCK_WAIT_SECONDS="4", LOCK_STALE_SECONDS="1200")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("gave up after 4 s waiting for the release lock", out)
        self.assertIn("Nothing was published", out)
        self.assertIn("delete release.lock", out)
        self.assertEqual(self.writes_by("A"), [])
        self.assertIn("token=someoneelse", self.store.text(LOCK), "someone else's lock was touched")
        self.assertIsNone(self.store.get("versions.json"))
        self.assertIsNone(self.store.get("VERSION.txt"))

    def test_stale_lock_is_taken_over(self):
        self.plant_lock(age=7200, token="deadpublish", holder="github-run-77-attempt-1", version="2.800.0")
        rc, out, p = self.publish("A", "2.875.0", GITHUB_ACTIONS="true")
        self.assertEqual(rc, 0, out)
        self.assertIn("TAKING IT OVER", out)
        self.assertIn("::warning title=Release publish::the release lock is", out)   # (an annotation on the run page in GitHub Actions)
        self.assertIn("github-run-77-attempt-1", out)
        self.assertIn("v2.800.0", out)
        dels = [e for e in self.store.events(who="A", kind="delete") if LOCK in e["deleted"]]
        self.assertEqual(len(dels), 2, "one delete of the dead lock and one release of ours: %s" % dels)
        self.assert_listed(p)
        self.assertEqual(self.store.text("VERSION.txt"), "2.875.0")
        self.assert_no_lock()
        self.assert_exclusive()

    def test_the_stale_age_is_configurable_and_counts_from_the_last_renewal(self):
        self.plant_lock(age=30, token="old")
        rc, out, _ = self.publish("A", "2.875.0", LOCK_STALE_SECONDS="10")
        self.assertEqual(rc, 0, out)
        self.assertIn("TAKING IT OVER", out)
        self.assertIn("(v2.800.0", out)
        self.store.reset()
        self.plant_lock(age=30, token="old")
        rc, out, _ = self.publish("B", "2.876.0", LOCK_STALE_SECONDS="100", LOCK_WAIT_SECONDS="3")
        self.assertNotEqual(rc, 0, "30 s old is not stale when the limit is 100 s:\n" + out)
        self.assertNotIn("TAKING IT OVER", out)

    def test_a_lock_stamped_in_the_future_or_without_a_time_is_never_taken_over(self):
        self.plant_lock(age=-3600, token="future")
        rc, out, _ = self.publish("A", "2.875.0", LOCK_WAIT_SECONDS="3")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("time stamp is in the future", out)
        self.assertNotIn("TAKING IT OVER", out)
        self.assertIn("token=future", self.store.text(LOCK))
        self.store.reset()
        self.plant_lock(age=99999, token="notime", epoch=False)
        rc, out, _ = self.publish("B", "2.875.0", LOCK_WAIT_SECONDS="3")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("no readable time stamp", out)
        self.assertNotIn("TAKING IT OVER", out)
        self.assertIn("token=notime", self.store.text(LOCK))

    def test_our_own_lock_is_recognised_when_the_answer_to_the_create_was_lost(self):
        # The create goes through but the answer is an error: the retry meets 409 on our own lock and must carry on, not wait for itself.
        self.store.rule(r"^releases/release\.lock$", method="POST", status=500, times=1, store_then_fail=True)
        rc, out, p = self.publish("A", "2.875.0")
        self.assertEqual(rc, 0, out)
        self.assertIn("taking the release lock failed (HTTP 500); trying again", out)
        self.assertNotIn("is held by", out)
        self.assert_listed(p)
        self.assert_no_lock()

    def test_a_store_that_keeps_failing_stops_the_publish_instead_of_publishing_without_the_lock(self):
        self.store.rule(r"^releases/release\.lock$", method="POST", status=500)
        rc, out, _ = self.publish("A", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("cannot take the release lock: HTTP 500", out)
        self.assertEqual(self.writes_by("A"), [], "it wrote to the bucket without the lock")
        self.assertEqual(len(self.store.events(who="A", key=LOCK, status=500)), 5, "five tries, then stop")

    def test_the_lock_names_the_run_and_never_a_host_or_user(self):
        gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")
        p = Publisher(self, "A", "2.875.0", GITHUB_RUN_ID="9876543210", GITHUB_RUN_ATTEMPT="2", USER="someone-private", HOSTNAME="a-private-host")
        self.assertTrue(gate.entered.wait(60), p.output())
        lock = self.store.text(LOCK)
        gate.go.set()
        self.assertEqual(p.wait(), 0, p.output())
        self.assertIn("holder=github-run-9876543210-attempt-2", lock)
        self.assertRegex(lock, r"utc=\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ")
        for private in ("someone-private", "a-private-host", self.seed, "test-key-"):
            self.assertNotIn(private, lock)
        self.assertEqual(sorted(l.split("=")[0] for l in lock.splitlines()[1:]), ["channel", "epoch", "holder", "token", "utc", "version"])

    # -- the lock is always given back
    def test_an_error_in_the_middle_still_gives_the_lock_back(self):
        self.store.rule(r"^releases/v2\.875\.0/OkayEngine\.exe$", method="POST", status=403)
        rc, out, p = self.publish("A", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("upload of v2.875.0/OkayEngine.exe failed (403)", out)
        self.assertIn("gave the release lock back", out)
        self.assert_no_lock()
        self.assertIsNone(self.store.get("versions.json"), "an index was written by a publish that failed")
        self.assertIsNone(self.store.get("VERSION.txt"))
        self.assert_exclusive()

    def test_a_cancelled_job_gives_the_lock_back(self):
        # GitHub cancels a job with SIGINT and then SIGTERM to its processes: the lock must not stay behind for 20 minutes.
        for sig, code in ((signal.SIGTERM, 143), (signal.SIGINT, 130)):
            self.store.reset()
            gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")
            p = Publisher(self, "A", "2.875.0", tag=str(code))
            self.assertTrue(gate.entered.wait(60), p.output())
            self.assertIsNotNone(self.store.get(LOCK))
            p.signal(sig)
            gate.go.set()
            self.assertEqual(p.wait(), code, p.output())
            self.assert_no_lock()
            self.assertIsNone(self.store.get("VERSION.txt"), "a cancelled publish must not announce its version")
            self.assertIsNone(self.store.get("versions.json"))

    def test_a_publish_that_was_taken_over_stops_before_it_commits_and_leaves_the_new_lock_alone(self):
        gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")
        p = Publisher(self, "A", "2.875.0", LOCK_RENEW_SECONDS="100000")   # (no renewals: the check before the commit must catch it)
        self.assertTrue(gate.entered.wait(60), p.output())
        self.plant_lock(age=0, token="thief", holder="github-run-5-attempt-1", version="2.876.0")   # someone took the stale lock
        gate.go.set()
        rc = p.wait()
        self.assertNotEqual(rc, 0, p.output())
        self.assertIn("was taken over by github-run-5-attempt-1 (v2.876.0)", p.output())
        self.assertIsNone(self.store.get("versions.json"), "it committed the index after losing the lock")
        self.assertIsNone(self.store.get("VERSION.txt"))
        self.assertIn("token=thief", self.store.text(LOCK), "it deleted the lock of whoever took over")

    def test_a_lock_deleted_by_hand_stops_the_publish_before_it_commits(self):
        gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")
        p = Publisher(self, "A", "2.875.0", LOCK_RENEW_SECONDS="100000")
        self.assertTrue(gate.entered.wait(60), p.output())
        self.store.delete(LOCK)
        gate.go.set()
        self.assertNotEqual(p.wait(), 0, p.output())
        self.assertIn("was deleted while this publish of v2.875.0 was running", p.output())
        self.assertIsNone(self.store.get("versions.json"))
        self.assertIsNone(self.store.get("VERSION.txt"))

    def test_a_slow_publish_renews_its_lock_so_the_next_one_does_not_take_it_over(self):
        # 3 uploads of 2.5 s each is longer than the 6 s stale limit, but the lock is renewed after every upload.
        self.store.rule(r"^releases/v2\.875\.0/Okay", method="POST", delay=2.5)
        p = Publisher(self, "A", "2.875.0", LOCK_STALE_SECONDS="6", LOCK_RENEW_SECONDS="0")
        self.assertTrue(self.store.wait_for(lambda: any(e["key"] == LOCK and e["status"] == 200 for e in self.store.log)))
        q = Publisher(self, "B", "2.876.0", LOCK_STALE_SECONDS="6", LOCK_RENEW_SECONDS="0", LOCK_WAIT_SECONDS="120")
        self.assertEqual((p.wait(), q.wait()), (0, 0), p.output() + q.output())
        self.assertNotIn("TAKING IT OVER", q.output())
        renewals = [e for e in self.store.events(who="A", kind="upload", key=LOCK) if e["upsert"]]
        self.assertGreaterEqual(len(renewals), 3, "the lock was not renewed while the files were uploading")
        self.assert_exclusive()
        self.assert_two_versions_published(p, q)

    def test_a_dry_run_takes_no_lock_and_writes_nothing(self):
        self.plant_lock(age=1, token="someoneelse")   # (a held lock must not even slow a dry run down)
        rc, out, _ = self.publish("A", "2.875.0", DRY_RUN="1", LOCK_WAIT_SECONDS="1")
        self.assertEqual(rc, 0, out)
        self.assertIn("DRY_RUN", out)
        self.assertEqual([e for e in self.store.events() if e["method"] in ("POST", "PUT", "DELETE")], [], "a dry run wrote to the bucket")
        self.assertEqual([e for e in self.store.events() if e["key"] == LOCK], [], "a dry run looked at the lock")
        self.assertIn("token=someoneelse", self.store.text(LOCK))

    def test_the_lock_settings_must_be_numbers(self):
        rc, out, _ = self.publish("A", "2.875.0", LOCK_WAIT_SECONDS="soon")
        self.assertNotEqual(rc, 0)
        self.assertIn("LOCK_WAIT_SECONDS must be a whole number of seconds", out)
        self.assertEqual(self.store.events(), [])

    def test_an_unreadable_bucket_stops_the_publish_instead_of_guessing(self):
        # If VERSION.txt cannot be read, "no version yet" is a guess that could move latest backwards: stop, give the lock back.
        self.store.rule(r"^releases/VERSION\.txt$", method="GET", status=500)
        rc, out, _ = self.publish("A", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("cannot read VERSION.txt from the bucket (HTTP 500)", out)
        self.assertEqual(self.writes_by("A"), [])
        self.assert_no_lock()

    # -- no downgrade, rule 12, pruning, caches
    def test_an_older_version_never_moves_latest_back(self):
        rc, out, a = self.publish("A", "2.10.0")
        self.assertEqual(rc, 0, out)
        notes_before = self.store.text("NOTES.txt")
        rc, out, b = self.publish("B", "2.9.0")   # (a text compare would call 2.9.0 the newer one)
        self.assertEqual(rc, 0, out)
        self.assertIn("is OLDER than the version that is latest now (v2.10.0)", out)
        self.assertIn("latest does not move backwards", out)
        self.assertEqual(self.store.text("VERSION.txt"), "2.10.0")
        self.assertEqual(self.store.text("NOTES.txt"), notes_before)
        self.assertEqual(self.index()["latest"], "2.10.0")
        self.assert_listed(a)
        self.assert_listed(b)
        self.assert_files_in_bucket(b)
        self.assertIn("v2.9.0/CHANGELOG.txt", self.store.names("v2.9.0/"))
        self.assertTrue(self.listed("2.9.0")["inBucket"])
        rc, out, c = self.publish("C", "2.11.0")
        self.assertEqual(rc, 0, out)
        self.assertNotIn("OLDER", out)
        self.assertEqual(self.store.text("VERSION.txt"), "2.11.0")
        self.assertEqual(self.index()["latest"], "2.11.0")
        self.assertEqual(self.store.text("NOTES.txt"), "- notes for 2.11.0")
        self.assert_no_lock()
        self.assert_signed()

    def test_a_version_is_never_published_again_with_different_files(self):
        rc, out, a = self.publish("A", "2.876.0")
        self.assertEqual(rc, 0, out)
        before = {k: self.store.get(k) for k in self.store.names()}
        files_before = self.listed("2.876.0")["files"]
        # the same files again: a safe re-run
        rc, out, _ = self.publish("A2", "2.876.0", tag="x")
        self.assertEqual(rc, 0, out)
        self.assertIn("already published with exactly these files", out)
        self.assertEqual(self.listed("2.876.0")["files"], files_before, "a safe re-run changed the index")
        self.assert_listed(a)
        # different files under the same version: refused, and nothing in the bucket changed
        rc, out, b = self.publish("B", "2.876.0", tag="other-build")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("already published with DIFFERENT files", out)
        self.assertIn("ALLOW_REPUBLISH=1", out)
        self.assertIn("rule 12", out)
        self.assertEqual(self.writes_by("B"), [], "files were written before the refusal")
        self.assert_files_in_bucket(a)
        self.assert_listed(a)
        self.assert_no_lock()
        for k in ("v2.876.0/OkayEngine.exe", "VERSION.txt"):
            self.assertEqual(self.store.get(k), before[k])
        # on purpose: replaces it
        rc, out, c = self.publish("C", "2.876.0", tag="other-build", ALLOW_REPUBLISH="1")
        self.assertEqual(rc, 0, out)
        self.assertIn("ALLOW_REPUBLISH=1: replacing", out)
        self.assert_listed(c)
        self.assert_files_in_bucket(c)

    def test_pruning_keeps_the_newest_and_never_deletes_what_was_just_published(self):
        pubs = {}
        for v in ("2.871.0", "2.872.0", "2.873.0", "2.874.0"):
            rc, out, pubs[v] = self.publish("P", v, tag=v)
            self.assertEqual(rc, 0, out)
        self.assertEqual(self.version_folders(), ["v2.872.0", "v2.873.0", "v2.874.0"])
        self.assertFalse(self.listed("2.871.0")["inBucket"])
        self.assertTrue(self.listed("2.871.0")["files"]["OkayEngine.exe"]["sha256"], "a pruned version keeps its checksums in the index")
        # An older release that finishes late: it is below the newest three, but it is not deleted the moment it is uploaded.
        rc, out, old = self.publish("O", "2.870.0", tag="late")
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.store.text("VERSION.txt"), "2.874.0")
        self.assert_files_in_bucket(old)
        self.assertTrue(self.listed("2.870.0")["inBucket"], "the index says the files are not there, or says they are but they were deleted")
        self.assertNotIn("removed old v2.870.0", out)
        # the next release prunes it with the other old ones
        rc, out, _ = self.publish("P", "2.875.0", tag="2.875.0")
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.version_folders(), ["v2.873.0", "v2.874.0", "v2.875.0"])
        self.assertFalse(self.listed("2.870.0")["inBucket"])
        self.assertFalse(self.listed("2.872.0")["inBucket"])
        self.assertTrue(self.listed("2.875.0")["inBucket"])

    def test_what_is_read_under_the_lock_is_not_a_stale_cached_copy(self):
        # The public address is served from a CDN that can hold an overwritten file for a while. A publish that read VERSION.txt or
        # versions.json from there could think "latest" is older than it is and drop the version published a moment ago.
        self.store.cdn_ttl = 300
        rc, out, a = self.publish("A", "2.876.0")
        self.assertEqual(rc, 0, out)
        for name in ("VERSION.txt", "versions.json"):   # a launcher looks (and the CDN keeps the answer)
            try:
                urllib.request.urlopen("%s/storage/v1/object/public/%s/%s" % (self.url, BUCKET, name)).read()
            except urllib.error.HTTPError:
                pass   # (an older script's plain reads can have left a cached "not found" here: the point is only that the CDN has an answer)
        rc, out, b = self.publish("B", "2.877.0")
        self.assertEqual(rc, 0, out)
        try:
            stale = urllib.request.urlopen("%s/storage/v1/object/public/%s/VERSION.txt" % (self.url, BUCKET)).read().decode().strip()
        except urllib.error.HTTPError:
            stale = "not found"
        self.assertIn(stale, ("2.876.0", "not found"), "the fake CDN is not serving an old copy of VERSION.txt: this test would prove nothing")
        rc, out, c = self.publish("C", "2.875.0")   # reads both files while the CDN still shows the state before B
        self.assertEqual(rc, 0, out)
        self.assertIn("is OLDER than the version that is latest now (v2.877.0)", out)
        self.assertEqual(self.store.text("VERSION.txt"), "2.877.0", "latest went backwards because of a cached read")
        self.assertEqual([v["version"] for v in self.index()["versions"]], ["2.877.0", "2.876.0", "2.875.0"], "an index update was lost to a cached read")
        for p in (a, b, c):
            self.assert_listed(p)
        self.assert_signed()

    def test_channel_rules_are_checked_early_and_again_under_the_lock(self):
        self.store.put("LTS.txt", "2.850.1\n")
        rc, out, _ = self.publish("A", "2.850.2")
        self.assertNotEqual(rc, 0)
        self.assertIn("v2.850.2 is in the LTS line 2.850", out)
        self.assertEqual(self.store.events(who="A", method="POST"), [], "a wrong channel should fail before anything is written")
        self.store.reset()
        # Now the rule becomes true while the publish is waiting for the lock: the early check passes, the one under the lock must catch it.
        self.plant_lock(age=1, token="someoneelse")
        p = Publisher(self, "B", "2.875.0")
        self.assertTrue(self.store.wait_for(lambda: sum(1 for e in self.store.log if e["who"] == "B" and e["key"] == LOCK and e["status"] == 409) >= 2))
        self.store.put("LTS.txt", "2.875.1\n")
        self.store.delete(LOCK)
        self.assertNotEqual(p.wait(), 0, p.output())
        self.assertIn("v2.875.0 is in the LTS line 2.875", p.output())
        self.assertEqual(self.writes_by("B"), [])
        self.assert_no_lock()
        # an LTS patch takes the same lock and never touches VERSION.txt
        self.store.reset()
        self.store.put("LTS.txt", "2.850.1\n")
        rc, out, p = self.publish("C", "2.850.2", CHANNEL="lts")
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.store.text("LTS.txt"), "2.850.2")
        self.assertIsNone(self.store.get("VERSION.txt"))
        self.assertEqual(self.index()["lts"], "2.850.2")
        self.assertNotIn("latest", self.index())
        self.assert_no_lock()
        self.assertEqual(len(self.spans()), 1)


    # ---- fixes from the review (each of these failed against the first version of the script) --------------------------------
    def seed_history(self, versions=("2.873.0", "2.874.0", "2.875.0")):
        """Publish a few versions first, so there is a real history in versions.json to lose."""
        out = []
        for v in versions:
            rc, text, p = self.publish("H", v, tag="hist-" + v)
            self.assertEqual(rc, 0, text)
            out.append(p)
        return out

    def test_a_cut_off_answer_does_not_wipe_the_version_history(self):
        hist = self.seed_history()
        # the next two reads of versions.json say 200 and promise the whole file, but the connection ends after 25 bytes
        self.store.rule(r"^releases/versions\.json$", method="GET", cut=25, times=2)
        rc, out, p = self.publish("N", "2.876.0")
        self.assertEqual(rc, 0, out)
        self.assertEqual([v["version"] for v in self.index()["versions"]], ["2.876.0", "2.875.0", "2.874.0", "2.873.0"], "the history was wiped")
        for q in hist + [p]:
            self.assert_listed(q)
        self.assert_no_lock()

    def test_an_index_that_is_cut_off_every_time_stops_the_publish_and_is_left_alone(self):
        self.seed_history(("2.873.0", "2.874.0"))
        before = self.store.get("versions.json")
        self.store.rule(r"^releases/versions\.json$", method="GET", cut=25)
        rc, out, _ = self.publish("N", "2.876.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("cannot read versions.json", out)
        self.assertEqual(self.store.get("versions.json"), before, "the index was rewritten")
        self.assertEqual(self.store.text("VERSION.txt"), "2.874.0")
        self.assertEqual(len(self.store.events(who="N", kind="get", key="versions.json")), 6, "six tries, then stop")
        self.assert_no_lock()

    def test_an_index_that_is_not_a_valid_index_is_never_overwritten(self):
        self.seed_history(("2.874.0",))
        for bad in ("this is not json at all", json.dumps({"schema": 2, "versions": []}), json.dumps({"schema": 1}), "[]"):
            self.store.put("versions.json", bad)
            rc, out, _ = self.publish("N", "2.876.0", tag="bad%d" % len(bad))
            self.assertNotEqual(rc, 0, "an index like %r was accepted:\n%s" % (bad, out))
            self.assertIn("not a usable index", out)
            self.assertEqual(self.store.text("versions.json"), bad, "an index that was not understood was overwritten")
            self.assert_no_lock()

    def test_the_version_files_are_read_six_times_before_the_publish_gives_up(self):
        self.store.rule(r"^releases/VERSION\.txt$", method="GET", status=500)
        rc, out, _ = self.publish("A", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("cannot read VERSION.txt from the bucket (HTTP 500)", out)
        self.assertEqual(len(self.store.events(who="A", kind="get", key="VERSION.txt")), 6)
        self.assertEqual(self.writes_by("A"), [])
        self.assert_no_lock()

    def test_the_index_read_and_the_check_before_the_commit_survive_four_failures_in_a_row(self):
        self.store.rule(r"^releases/versions\.json$", method="GET", status=500, times=4)
        self.store.rule(r"^releases/release\.lock$", method="GET", status=500, times=4)   # (the first look at the lock is the check before the commit)
        rc, out, p = self.publish("A", "2.875.0", LOCK_RENEW_SECONDS="100000")
        self.assertEqual(rc, 0, out)
        self.assert_listed(p)
        self.assert_no_lock()

    def test_a_release_that_cannot_delete_the_lock_at_once_keeps_trying(self):
        self.store.rule(r"^releases/release\.lock$", method="DELETE", status=500, times=4)
        rc, out, p = self.publish("A", "2.875.0")
        self.assertEqual(rc, 0, out)
        self.assert_listed(p)
        self.assert_no_lock()

    def test_a_failure_does_not_print_the_reply_to_an_earlier_call(self):
        # every try of the engine upload gets no answer at all, so curl has no reply of its own to show
        self.store.rule(r"^releases/v2\.875\.0/OkayEngine\.exe$", method="POST", drop=True)
        rc, out, _ = self.publish("A", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("upload of v2.875.0/OkayEngine.exe failed", out)
        self.assertNotIn('"Key"', out, "the die message shows the reply of an earlier upload")
        self.assert_no_lock()

    # -- taking over a stale lock
    def test_two_waiters_taking_over_a_stale_lock_never_delete_each_others_fresh_lock(self):
        self.plant_lock(age=7200, token="deadpublish", holder="github-run-77-attempt-1", version="2.800.0")
        gate = self.store.gate(r"^releases/release\.lock$", method="DELETE", who="A")   # A's DELETE of the stale lock is sent but processed late
        a = Publisher(self, "A", "2.875.0", LOCK_WAIT_SECONDS="120")
        self.assertTrue(gate.entered.wait(60), a.output())
        b = Publisher(self, "B", "2.876.0", LOCK_WAIT_SECONDS="120")
        time.sleep(6)   # B meets the same stale lock and acts on it (a waiter that takes over without care deletes it and takes the lock now)
        gate.go.set()   # A's DELETE is processed now
        self.assertEqual((a.wait(), b.wait()), (0, 0), "A:\n%s\nB:\n%s" % (a.output(), b.output()))
        self.assert_exclusive()
        spans = self.spans()
        b_deletes = [e for e in self.store.events(who="B", kind="delete") if LOCK in e["deleted"]]
        self.assertEqual(len(b_deletes), 1, "B deleted the lock other than to give back its own: %s" % b_deletes)
        self.assertGreater(b_deletes[0]["seq"], spans["A"][1], "B deleted a lock while A still held it")
        self.assert_listed(a)
        self.assert_listed(b)
        self.assert_no_lock()
        self.assertEqual([k for k in self.store.names() if k.startswith(LOCK)], [], "a claim was left behind")

    def test_a_holder_that_renews_while_a_waiter_is_deciding_keeps_its_lock(self):
        self.plant_lock(age=7200, token="slowholder")
        # the waiter's second look at the lock (the one it makes just before it would delete) is held in the fake; the holder renews meanwhile
        gate = self.store.gate(r"^releases/release\.lock$", method="GET", who="W", after=1)
        w = Publisher(self, "W", "2.875.0", LOCK_WAIT_SECONDS="6")
        self.assertTrue(gate.entered.wait(60), w.output())
        self.plant_lock(age=0, token="slowholder")   # same holder, same token, fresh time
        gate.go.set()
        self.assertNotEqual(w.wait(), 0, w.output())
        self.assertIn("gave up after 6 s", w.output())
        self.assertEqual([e for e in self.store.events(who="W", kind="delete") if LOCK in e["deleted"]], [], "a live holder's lock was deleted")
        self.assertIn("token=slowholder", self.store.text(LOCK))
        self.assertEqual([k for k in self.store.names() if k.startswith(LOCK + ".claim")], [], "a claim was left behind")

    def test_a_live_claim_blocks_a_second_takeover_and_a_dead_claim_does_not(self):
        claim = "okayspace-release-lock-claim-v1\ntoken=other\nfor=deadpublish\nepoch=%d\n"
        self.plant_lock(age=7200, token="deadpublish")
        self.store.put("release.lock.claim-deadpublish", claim % int(time.time()))   # someone is taking it over right now
        rc, out, _ = self.publish("A", "2.875.0", LOCK_WAIT_SECONDS="4", LOCK_CLAIM_STALE_SECONDS="60")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("token=deadpublish", self.store.text(LOCK), "the lock was taken while someone else held the claim")
        self.assertIsNotNone(self.store.get("release.lock.claim-deadpublish"), "a live claim was removed")
        # a claim nobody finished (older than the claim limit) does not block for ever
        self.store.reset()
        self.plant_lock(age=7200, token="deadpublish")
        self.store.put("release.lock.claim-deadpublish", claim % (int(time.time()) - 600))
        rc, out, p = self.publish("B", "2.875.0", LOCK_CLAIM_STALE_SECONDS="60")
        self.assertEqual(rc, 0, out)
        self.assert_listed(p)
        self.assert_no_lock()
        self.assertEqual([k for k in self.store.names() if k.startswith(LOCK)], [], "a claim was left behind")

    # -- a cancelled job: the runner may signal only the script, and curl and sleep must not hold the signal back
    def test_a_signal_to_the_script_alone_gives_the_lock_back_at_once(self):
        gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")   # an upload is in flight
        p = Publisher(self, "A", "2.875.0")
        self.assertTrue(gate.entered.wait(60), p.output())
        self.assertIsNotNone(self.store.get(LOCK))
        os.kill(p.proc.pid, signal.SIGTERM)   # only the script, not its curl (the whole group is signalled in another test)
        released = self.store.wait_for(lambda: (BUCKET, LOCK) not in self.store.objects, 10)
        gate.go.set()
        self.assertTrue(released, "the lock was still there 10 s after the signal: the script was waiting for its curl")
        self.assertEqual(p.wait(30), 143, p.output())
        self.assertIsNone(self.store.get("VERSION.txt"))

    def test_a_signal_while_waiting_for_the_lock_ends_the_wait_at_once(self):
        self.plant_lock(age=1, token="someoneelse")
        p = Publisher(self, "A", "2.875.0", LOCK_POLL_SECONDS="30", LOCK_WAIT_SECONDS="300")
        self.assertTrue(self.store.wait_for(lambda: any(e["who"] == "A" and e["key"] == LOCK and e["status"] == 409 for e in self.store.log)))
        time.sleep(1)   # (now in its pause between two looks)
        os.kill(p.proc.pid, signal.SIGTERM)
        self.assertEqual(p.wait(10), 143, "the script went on sleeping after the signal:\n" + p.output())

    def test_a_lock_created_just_as_the_job_is_cancelled_is_not_left_behind(self):
        gate = self.store.gate_after(r"^releases/release\.lock$", method="POST")   # the create goes through; its ANSWER is held
        p = Publisher(self, "A", "2.875.0")
        self.assertTrue(gate.entered.wait(60), p.output())
        self.assertIsNotNone(self.store.get(LOCK))
        os.kill(p.proc.pid, signal.SIGTERM)
        released = self.store.wait_for(lambda: (BUCKET, LOCK) not in self.store.objects, 10)
        gate.go.set()
        self.assertTrue(released, "a lock that was created but never answered for was left behind")
        self.assertEqual(p.wait(30), 143, p.output())

    # -- clocks
    def test_lock_ages_use_the_servers_clock_not_this_machines(self):
        # The server's clock is an hour AHEAD of this machine's (its Date header says so); lock times are in the server's time.
        self.store.clock_skew = 3600
        self.plant_lock(age=1500, token="old", now=time.time() + 3600)   # 25 minutes old by the server's clock: stale (the limit is 20)
        rc, out, p = self.publish("A", "2.875.0", LOCK_WAIT_SECONDS="8")
        self.assertEqual(rc, 0, out)
        self.assertIn("TAKING IT OVER", out)
        # ... and an hour BEHIND: a lock that is 100 s old by the server's clock is alive, however old this machine thinks it is
        self.store.reset()
        self.store.clock_skew = -3600
        self.plant_lock(age=100, token="alive", now=time.time() - 3600)
        rc, out, _ = self.publish("B", "2.876.0", LOCK_WAIT_SECONDS="4")
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn("TAKING IT OVER", out)
        self.assertIn("token=alive", self.store.text(LOCK))

    def test_the_time_a_lock_carries_is_the_servers_time(self):
        self.store.clock_skew = 3600
        gate = self.store.gate(r"^releases/v2\.875\.0/OkayEngine\.exe$")
        p = Publisher(self, "A", "2.875.0")
        self.assertTrue(gate.entered.wait(60), p.output())
        lock = self.store.text(LOCK)
        gate.go.set()
        self.assertEqual(p.wait(), 0, p.output())
        epoch = int(re.search(r"epoch=(\d+)", lock).group(1))
        self.assertLess(abs(epoch - (time.time() + 3600)), 60, "the lock carries this machine's time, not the server's")

    # -- recovery when the Supabase step of a CI run failed (the runner is gone; its artifact is not)
    def make_artifact_folder(self, dist, version):
        """What a person ends up with after downloading the run's artifact: GitHub's zip holds the release zip, which holds the folder
        the workflow made with `cp -r build-win-dist/* OkaySpace-windows-v<ver>/; zip -r ...`. Returns the folder after two unzips."""
        name = "OkaySpace-windows-v%s" % version
        release_zip = make_release_zip(self.tmp, dist, version)
        artifact = os.path.join(self.tmp, "artifact-download.zip")
        with zipfile.ZipFile(artifact, "w") as z:
            z.write(release_zip, name + ".zip")
        out1, out2 = os.path.join(self.tmp, "artifact"), os.path.join(self.tmp, "release")
        with zipfile.ZipFile(artifact) as z:
            z.extractall(out1)
        with zipfile.ZipFile(os.path.join(out1, name + ".zip")) as z:
            z.extractall(out2)
        return os.path.join(out2, name)

    def test_a_failed_ci_publish_is_recovered_from_the_artifact_of_that_run(self):
        # (1) the step failed AFTER versions.json was written: VERSION.txt could not be uploaded, so nobody was offered the version
        self.store.rule(r"^releases/VERSION\.txt$", method="POST", status=403)
        rc, out, ci = self.publish("CI", "2.875.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIsNotNone(self.listed("2.875.0"))
        self.assertIsNone(self.store.get("VERSION.txt"))
        self.store.clear_rules()
        # the artifact holds the same bytes the failed run built; DIST_DIR=<that folder> scripts/publish-supabase.sh says "same" and finishes
        folder = self.make_artifact_folder(ci.dist, "2.875.0")
        rc, out, rec = self.publish("FIX", "2.875.0", dist=folder)
        self.assertEqual(rc, 0, out)
        self.assertIn("already published with exactly these files", out)
        self.assertEqual(self.store.text("VERSION.txt"), "2.875.0")
        self.assert_listed(ci)
        self.assert_no_lock()
        # ... and then scripts/promote-latest.sh <version> makes the GitHub release latest
        r, edits, now = self.run_promote("2.875.0", gh_latest="2.874.0", published="2.875.0")
        self.assertEqual((r.returncode, edits), (0, ["release edit v2.875.0 --repo o/n --latest"]), r.stderr)
        # (2) the step failed BEFORE the index: the version is new to it, and the artifact publishes it all the same
        self.store.reset()
        self.store.rule(r"^releases/v2\.876\.0/OkayEngine\.exe$", method="POST", status=403)
        rc, out, ci2 = self.publish("CI2", "2.876.0")
        self.assertNotEqual(rc, 0, out)
        self.assertIsNone(self.store.get("versions.json"))
        self.store.clear_rules()
        rc, out, _ = self.publish("FIX2", "2.876.0", dist=self.make_artifact_folder(ci2.dist, "2.876.0"))
        self.assertEqual(rc, 0, out)
        self.assertNotIn("already published", out)
        self.assert_listed(ci2)
        self.assertEqual(self.store.text("VERSION.txt"), "2.876.0")
        # (3) a local REBUILD of the same version has different bytes and is refused: that is why the artifact is the way
        rc, out, _ = self.publish("REBUILD", "2.876.0", tag="rebuilt-here")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("already published with DIFFERENT files", out)


# ---- the tests: promote-latest.sh and the workflow ---------------------------------------------------------------------------

FAKE_GH = r'''#!/usr/bin/env bash
# a stand-in for the GitHub CLI: $FAKE_GH_DIR/latest holds the tag that is "latest" now, calls is every call made
set -u
echo "$*" >> "$FAKE_GH_DIR/calls"
case "$1 $2" in
  "release view")
      case "$*" in *"--json tagName"*"--jq .tagName"*) ;; *) echo "unexpected: $*" >&2; exit 2 ;; esac
      if [ -n "${FAKE_GH_FAIL:-}" ]; then echo "HTTP 502: Bad Gateway" >&2; exit 1; fi
      for a in "$@"; do   # a tag was given: does that release exist? (FAKE_GH_RELEASES lists the tags that do)
          case "$a" in v[0-9]*)
              case " ${FAKE_GH_RELEASES:-} " in *" $a "*) echo "$a"; exit 0 ;; esac
              echo "release not found" >&2; exit 1 ;;
          esac
      done
      if [ -s "$FAKE_GH_DIR/latest" ]; then cat "$FAKE_GH_DIR/latest"; exit 0; fi
      echo "release not found" >&2; exit 1 ;;
  "release edit")
      case "$*" in *" --latest"*) ;; *) echo "unexpected: $*" >&2; exit 2 ;; esac
      printf '%s\n' "$3" > "$FAKE_GH_DIR/latest"; exit 0 ;;
esac
echo "unexpected gh call: $*" >&2; exit 2
'''


class PromoteLatest(Base):
    def test_an_older_release_finishing_last_does_not_take_latest_back(self):
        r, edits, now = self.run_promote("2.875.0", gh_latest="2.876.0", published="2.876.0")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(edits, [])
        self.assertEqual(now, "v2.876.0")
        self.assertIn("A newer release (v2.876.0) is already latest on GitHub", r.stdout)

    def test_a_newer_release_takes_latest(self):
        r, edits, now = self.run_promote("2.876.0", gh_latest="2.875.0", published="2.876.0")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(edits, ["release edit v2.876.0 --repo o/n --latest"])
        self.assertEqual(now, "v2.876.0")
        self.assertIn("v2.876.0 is now the latest release", r.stdout)

    def test_a_newer_version_is_already_published_but_not_yet_latest_on_github(self):
        r, edits, now = self.run_promote("2.875.0", gh_latest="2.874.0", published="2.876.0")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(edits, [])
        self.assertEqual(now, "v2.874.0")
        self.assertIn("A newer release (v2.876.0) is already published", r.stdout)

    def test_the_comparison_is_by_number_not_by_text(self):
        r, edits, _ = self.run_promote("2.9.0", gh_latest="2.10.0", published="2.10.0")
        self.assertEqual((r.returncode, edits), (0, []), r.stderr)
        r, edits, _ = self.run_promote("2.10.0", gh_latest="2.9.0", published="2.10.0")
        self.assertEqual((r.returncode, edits), (0, ["release edit v2.10.0 --repo o/n --latest"]), r.stderr)

    def test_the_first_release_ever_and_an_equal_one(self):
        r, edits, now = self.run_promote("2.876.0", gh_latest=None, published="2.876.0")   # gh: "release not found"
        self.assertEqual((r.returncode, edits, now), (0, ["release edit v2.876.0 --repo o/n --latest"], "v2.876.0"), r.stderr)
        r, edits, _ = self.run_promote("2.876.0", gh_latest="2.876.0", published="2.876.0")
        self.assertEqual((r.returncode, edits), (0, []), r.stderr)
        self.assertIn("already the latest release", r.stdout)

    def test_an_unreadable_list_does_not_block_a_release_that_github_says_is_newer(self):
        r, edits, _ = self.run_promote("2.876.0", gh_latest="2.875.0", published=None)   # no versions.json at all
        self.assertEqual((r.returncode, edits), (0, ["release edit v2.876.0 --repo o/n --latest"]), r.stderr)
        r, edits, _ = self.run_promote("2.876.0", gh_latest="2.875.0", published=None, RELEASES_BASE="", isolated=True)
        self.assertEqual((r.returncode, edits), (0, ["release edit v2.876.0 --repo o/n --latest"]), r.stderr)
        self.assertIn("no release server address", r.stderr)

    def test_when_github_cannot_say_what_is_latest_nothing_is_changed(self):
        r, edits, _ = self.run_promote("2.876.0", gh_latest="2.875.0", published="2.876.0", FAKE_GH_FAIL="1")
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(edits, [])
        self.assertIn("cannot tell which release is latest on GitHub", r.stderr)

    def test_dry_run_and_bad_arguments(self):
        r, edits, _ = self.run_promote("2.876.0", gh_latest="2.875.0", published="2.876.0", DRY_RUN="1")
        self.assertEqual((r.returncode, edits), (0, []), r.stderr)
        self.assertIn("DRY_RUN: would mark v2.876.0", r.stdout)
        r, edits, _ = self.run_promote("v2.876.0", gh_latest="2.875.0", published="2.876.0")   # a leading v is fine
        self.assertEqual((r.returncode, edits), (0, ["release edit v2.876.0 --repo o/n --latest"]), r.stderr)
        r, edits, _ = self.run_promote("latest", gh_latest="2.875.0")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("usage:", r.stderr)


V1 = os.path.join(REPO, "scripts", "publish-supabase-v1.sh")
V1_SHA256 = "0c605523a6b6320bba2b53844e93c20aebc40de2fdc4f4bd3e731b88b4601ddc"   # main's scripts/publish-supabase.sh as of commit 05feef3b
V1_SOURCE_COMMIT = "05feef3bc8856f5ea044f99edbefdf8ee639f8a5"


def normalised_output(text, url):
    """A publish's output with what differs between two runs made equal: the fake's port and the temp file names."""
    text = text.replace(url, "URL")
    return re.sub(r"tmp\.[A-Za-z0-9]+\.", "tmp.X.", text)


class Seam(Base):
    """The new publish path is OFF by default: scripts/publish-supabase.sh hands over to the old script (kept as scripts/publish-supabase-v1.sh)
    unless PUBLISH_V2=1, and for a normal release the new script leaves the bucket exactly as the old one does."""

    def release_with(self, who, script=None, **env):
        """One normal release of 2.875.0 on an empty bucket; returns (output, bucket snapshot, request log) and clears the store."""
        rc, out, p = self.publish(who, "2.875.0", tag="seam", script=script, **env)
        self.assertEqual(rc, 0, out)
        self.assert_listed(p)
        result = (normalised_output(out, self.url), self.snapshot(), self.store.events(), p)
        self.store.reset()
        return result

    def test_v1_is_byte_for_byte_the_script_that_released_everything_so_far(self):
        with open(V1, "rb") as f:
            self.assertEqual(hashlib.sha256(f.read()).hexdigest(), V1_SHA256, "scripts/publish-supabase-v1.sh was edited: it must stay verbatim")
        have = subprocess.run(["git", "-C", REPO, "cat-file", "-e", V1_SOURCE_COMMIT], capture_output=True)
        if have.returncode == 0:   # (a shallow clone does not have the commit; the pinned hash above is the check then)
            old = subprocess.run(["git", "-C", REPO, "show", "%s:scripts/publish-supabase.sh" % V1_SOURCE_COMMIT], capture_output=True, check=True).stdout
            with open(V1, "rb") as f:
                self.assertEqual(f.read(), old, "v1 differs from main's script at %s" % V1_SOURCE_COMMIT)

    def test_without_the_switch_the_new_script_is_the_old_one(self):
        # the default way in (scripts/publish-supabase.sh, no PUBLISH_V2) and the old script itself: same output, same bucket, same requests
        out_default, bucket_default, log_default, _ = self.release_with("D", PUBLISH_V2="0")
        out_v1, bucket_v1, log_v1, _ = self.release_with("O", script=V1)
        self.assertEqual(out_default, out_v1)
        self.assertEqual({k: v for k, v in bucket_default.items() if k != "versions.json.sig"},
                         {k: v for k, v in bucket_v1.items() if k != "versions.json.sig"})
        shape = lambda log: [(e["method"], e["kind"], e["key"]) for e in log]
        self.assertEqual(shape(log_default), shape(log_v1))
        # and it is the OLD behaviour: no lock, no authenticated reads, only the public address for what is published
        for log in (log_default, log_v1):
            self.assertEqual([e for e in log if e["key"].startswith(LOCK)], [], "the old script took a lock")
            self.assertEqual([e for e in log if e["kind"] == "get"], [], "the old script read through the authenticated API")
            self.assertTrue([e for e in log if e["kind"] == "public"])

    def test_a_normal_release_leaves_the_same_bucket_with_the_new_script(self):
        _, bucket_v1, _, p1 = self.release_with("O", script=V1)
        out_v2, bucket_v2, log_v2, p2 = self.release_with("N")   # (PUBLISH_V2=1 is the test's default)
        self.assertEqual({k: v for k, v in bucket_v2.items() if k != "versions.json.sig"},
                         {k: v for k, v in bucket_v1.items() if k != "versions.json.sig"},
                         "for a normal release the new script must leave exactly what the old one leaves")
        self.assertEqual(set(bucket_v2), set(bucket_v1))
        self.assertIn("took the release lock", out_v2)
        self.assertIn("gave the release lock back", out_v2)
        self.assertNotIn(LOCK, bucket_v2, "the new script left its lock behind")
        self.assertEqual(p1.version, p2.version)


class CheckUnreleased(Base):
    SCRIPT = os.path.join(REPO, "scripts", "check-unreleased.sh")

    def run_check(self, version, releases="", **env):
        d = os.path.join(self.tmp, "gh")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "gh"), "w") as f:
            f.write(FAKE_GH)
        os.chmod(os.path.join(d, "gh"), 0o755)
        open(os.path.join(d, "latest"), "w").close()
        open(os.path.join(d, "calls"), "w").close()
        e = {"PATH": d + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"), "FAKE_GH_DIR": d, "FAKE_GH_RELEASES": releases,
             "GITHUB_REPOSITORY": "o/n", "HOME": self.tmp, "TMPDIR": self.tmp, "RELEASES_BASE": "%s/storage/v1/object/public/%s" % (self.url, BUCKET),
             "TRIES": "2", "TRY_PAUSE": "0", "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost",
             "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY}
        e.update(env)
        return subprocess.run(["bash", self.SCRIPT, version], env=e, capture_output=True, text=True, timeout=60)

    def listed_index(self, *versions):
        files = {n: {"size": 10, "sha256": "a" * 64} for n in EXES}
        self.store.put("versions.json", json.dumps({"schema": 1, "latest": versions[0], "versions": [
            {"version": v, "channel": "latest", "date": "2026-11-01", "inBucket": True, "files": files} for v in versions]}))

    def test_a_new_version_may_be_built(self):
        self.listed_index("2.874.0")
        r = self.run_check("2.875.0", releases="v2.874.0")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("fine to build", r.stdout)

    def test_a_version_with_a_github_release_is_stopped_before_it_builds(self):
        self.listed_index("2.874.0")
        r = self.run_check("2.875.0", releases="v2.874.0 v2.875.0", GITHUB_ACTIONS="true")
        self.assertEqual(r.returncode, 1)
        self.assertIn("v2.875.0 already has a GitHub release", r.stderr)
        self.assertIn("CLAUDE.md rule 12", r.stderr)
        self.assertIn("::error title=Version already released::", r.stdout)

    def test_a_version_in_the_checksum_list_is_stopped_before_it_builds(self):
        self.listed_index("2.875.0", "2.874.0")
        r = self.run_check("2.875.0")
        self.assertEqual(r.returncode, 1)
        self.assertIn("already listed in versions.json", r.stderr)

    def test_after_a_tag_push_only_the_checksum_list_counts(self):
        self.listed_index("2.874.0")
        r = self.run_check("2.875.0", releases="v2.875.0", CHECK_GITHUB_RELEASE="0")   # (a release made by hand just before the tag)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.listed_index("2.875.0")
        self.assertEqual(self.run_check("2.875.0", CHECK_GITHUB_RELEASE="0").returncode, 1)

    def test_the_first_release_ever_and_an_unreadable_list(self):
        r = self.run_check("2.875.0")   # no versions.json at all
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("no versions.json yet", r.stdout)
        r = self.run_check("2.875.0", RELEASES_BASE="http://127.0.0.1:9/none")   # nothing answers: only the GitHub check is made
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("WARNING: could not read versions.json", r.stderr)

    def test_when_github_cannot_be_asked_the_release_is_stopped(self):
        r = self.run_check("2.875.0", FAKE_GH_FAIL="1")
        self.assertEqual(r.returncode, 1)
        self.assertIn("cannot tell whether v2.875.0 already has a GitHub release", r.stderr)

    def test_bad_arguments(self):
        self.assertEqual(self.run_check("latest").returncode, 2)


class Rehearsal(Base):
    """scripts/release-rehearsal.sh (what .github/workflows/release-rehearsal.yml runs) against the fake. On the fake it can only show that the
    script's own logic is right; the point of the real run is the real service's answers."""
    SCRIPT = os.path.join(REPO, "scripts", "release-rehearsal.sh")
    BUCKET_NAME = "zz-publish-test-4242"

    def run_rehearsal(self, mode="run", bucket=BUCKET_NAME, **env):
        summary = os.path.join(self.tmp, "summary-%s.md" % mode)
        e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": self.tmp, "TMPDIR": self.tmp, "LC_ALL": "C", "PYTHONDONTWRITEBYTECODE": "1",
             "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost", "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY,
             "SUPABASE_URL": self.url, "SUPABASE_SERVICE_KEY": "test-key-Z", "BUCKET": bucket, "LOCK_POLL_SECONDS": "1",
             "READ_RETRY_BASE_SECONDS": "0", "GITHUB_STEP_SUMMARY": summary}
        e.update(env)
        r = subprocess.run(["bash", self.SCRIPT, mode], env=e, capture_output=True, text=True, timeout=300)
        text = ""
        if os.path.exists(summary):
            with open(summary) as f:
                text = f.read()
        return r, text

    def test_a_full_rehearsal_passes_when_the_service_behaves_as_documented(self):
        self.store.cdn_ttl = 30   # a caching edge in front of the public address, as the docs warn
        r, summary = self.run_rehearsal()
        self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-2000:])
        self.assertIn("**PASSED.**", summary)
        self.assertNotIn("**FAIL**", summary)
        self.assertGreaterEqual(summary.count("**PASS**"), 14)
        for wanted in ("Both publishes finished without an error", "One publish found the lock held and waited for it",
                       "versions.json lists both versions with the SHA-256 of OkaySpace.exe, OkayEngine.exe and OkaySpacePlayer.exe",
                       "The same version with different files is refused", "does not move \"latest\" back",
                       "WITHOUT upsert is refused", "The throwaway bucket was emptied and deleted"):
            self.assertIn(wanted, summary)
        self.assertIn("What the real storage service answered", summary)
        self.assertRegex(summary, r"create without upsert, object already exists \(this is the lock\) \| HTTP 409")
        # the throwaway bucket is gone, and it was the only bucket anything touched
        self.assertNotIn(self.BUCKET_NAME, self.store.buckets)
        touched = {e["bucket"] for e in self.store.events() if e.get("bucket")}
        self.assertEqual(touched, {self.BUCKET_NAME}, "the rehearsal touched another bucket: %s" % touched)
        self.assertNotIn("test-key-", r.stdout + r.stderr + summary, "the service key was printed")

    def test_it_refuses_any_bucket_that_is_not_a_throwaway_one_before_any_request(self):
        for bad in ("releases", "", "zz-publish-test-", "zz-publish-test-A", "official", "zz-publish-testing-1", "x-zz-publish-test-1",
                    "zz-publish-test-1/../releases", "zz-publish-test-1 releases"):
            for mode in ("run", "cleanup"):
                r, _ = self.run_rehearsal(mode, bucket=bad)
                self.assertNotEqual(r.returncode, 0, "BUCKET=%r was accepted in %s mode" % (bad, mode))
                self.assertIn("refusing to run", r.stderr)
        self.assertEqual(self.store.events(), [], "a request was made before the bucket name was checked")
        r, _ = self.run_rehearsal("run", BUCKET="")   # (and with the variable left out entirely)
        self.assertNotEqual(r.returncode, 0)

    def test_a_service_that_does_not_refuse_a_second_create_fails_the_rehearsal_and_still_cleans_up(self):
        self.store.allow_duplicates = True   # a create without upsert overwrites instead of being refused: the lock could not work
        r, summary = self.run_rehearsal()
        self.assertEqual(r.returncode, 1, r.stdout[-2000:])
        self.assertIn("**FAILED.**", summary)
        self.assertRegex(summary, r"WITHOUT upsert is refused[^\n]*\*\*FAIL\*\*")
        self.assertNotIn(self.BUCKET_NAME, self.store.buckets, "a failed rehearsal left its bucket behind")

    def test_cleanup_deletes_a_left_over_bucket_and_can_be_repeated(self):
        name = "zz-publish-test-77"
        self.store.put("VERSION.txt", "2.900.0\n", bucket=name)
        self.store.put("v2.900.0/OkayEngine.exe", b"MZ", bucket=name)
        self.store.put("VERSION.txt", "2.0.0\n")   # the real bucket's content: must survive
        r, summary = self.run_rehearsal("cleanup", bucket=name)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn(name, self.store.buckets)
        self.assertEqual(self.store.text("VERSION.txt"), "2.0.0", "the real bucket was touched")
        self.assertIn("is emptied and deleted", summary)
        r, _ = self.run_rehearsal("cleanup", bucket=name)   # (it is already gone: still fine)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_cleanup_without_a_key_does_nothing(self):
        r, _ = self.run_rehearsal("cleanup", SUPABASE_SERVICE_KEY="")
        self.assertEqual(r.returncode, 0)
        self.assertIn("nothing to clean up", r.stdout)
        self.assertEqual(self.store.events(), [])
        r, _ = self.run_rehearsal("run", SUPABASE_SERVICE_KEY="")
        self.assertNotEqual(r.returncode, 0)

    def test_it_never_runs_under_a_trace(self):
        r = subprocess.run(["bash", "-x", self.SCRIPT, "cleanup"], env={"PATH": os.environ["PATH"], "BUCKET": self.BUCKET_NAME,
                                                                       "SUPABASE_SERVICE_KEY": "test-key-Z", "SUPABASE_URL": self.url},
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("refusing to run with 'set -x'", r.stderr)
        self.assertNotIn("test-key-Z", r.stderr, "the trace printed the key")

    def test_the_answer_shape_checks_are_the_ones_of_the_publish_script(self):
        def shapes(path):
            with open(path) as f:
                text = f.read()
            return [re.search(r"(?ms)^%s\(\) \{\n(.*?)^\}" % fn, text).group(1) for fn in ("is_missing", "is_duplicate")]
        self.assertEqual(shapes(self.SCRIPT), shapes(PUBLISH), "release-rehearsal.sh and publish-supabase.sh must accept the same answer shapes")


FAKE_GH_ARTIFACT = r"""#!/usr/bin/env bash
# a stand-in for `gh run download <run> -n <artifact> -D <dir>`: GitHub unpacks the artifact, whose content is the release zip
set -u
echo "$*" >> "$FAKE_GH_DIR/calls"
if [ "${1:-} ${2:-}" = "run download" ]; then
    run="$3"; shift 3
    name=""; dir="."
    while [ $# -gt 0 ]; do
        case "$1" in
            -n) name="$2"; shift 2 ;;
            -D) dir="$2"; shift 2 ;;
            *) echo "unexpected flag: $1" >&2; exit 2 ;;
        esac
    done
    [ "$run" = "$FAKE_RUN" ] || { echo "no such run: $run" >&2; exit 1; }
    [ "$name" = "$FAKE_ARTIFACT" ] || { echo "no artifact named $name" >&2; exit 1; }
    mkdir -p "$dir"
    cp "$FAKE_ARTIFACT_ZIP" "$dir/$name.zip"
    exit 0
fi
echo "unexpected gh call: $*" >&2
exit 2
"""


class DocRecipes(Base):
    """The commands docs/release_channels.md tells a person to type, run as written against the fake. The only change is the line that
    asks for the key (`read -rs SUPABASE_SERVICE_KEY`): the fake's made-up key comes from the environment instead of the keyboard. This
    keeps the guide from drifting away from the scripts."""
    GUIDE = os.path.join(REPO, "docs", "release_channels.md")

    def blocks(self, start, end):
        """The ```bash blocks of the guide between two markers, with their list indentation removed."""
        with open(self.GUIDE) as f:
            doc = f.read()
        a = doc.index(start)
        b = doc.index(end, a + 1)
        out = []
        for m in re.finditer(r"^([ ]*)```bash\n(.*?)^\1```", doc[a:b], re.S | re.M):
            ind = m.group(1)
            out.append("".join(l[len(ind):] if l.startswith(ind) else l for l in m.group(2).splitlines(True)))
        return out

    def recipe_env(self, **extra):
        notes = os.path.join(self.tmp, "recipe-notes.txt")
        with open(notes, "w") as f:
            f.write("- notes from the recipe\n")
        e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": self.tmp, "TMPDIR": self.tmp, "LC_ALL": "C", "PYTHONDONTWRITEBYTECODE": "1",
             "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost", "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY,
             "SUPABASE_URL": self.url, "SUPABASE_SERVICE_KEY": "test-key-D", "NOTES_FILE": notes, "ARCHIVE_REPO": "o/n",
             "OKAY_UPDATE_SIGNING_SEED": self.seed, "LOCK_POLL_SECONDS": "1", "READ_RETRY_BASE_SECONDS": "0"}
        e.update(extra)
        return e

    def run_recipe(self, block, env, timeout=180):
        lines = block.splitlines()
        asks = [l for l in lines if l.startswith("read -rs SUPABASE_SERVICE_KEY")]
        self.assertEqual(len(asks), 1, "the recipe must ask for the key with `read -rs SUPABASE_SERVICE_KEY` (never a line that puts the key "
                                       "in the command, which ends up in the shell history):\n" + block)
        script = "\n".join(l for l in lines if l not in asks)
        # (run in a copy of the scripts folder: the commands use relative paths, and a recipe that is wrong must not litter the repository)
        tree = os.path.join(self.tmp, "tree")
        if not os.path.isdir(tree):
            shutil.copytree(os.path.join(REPO, "scripts"), os.path.join(tree, "scripts"), ignore=shutil.ignore_patterns("__pycache__"))
        return subprocess.run(["bash", "-c", "set -e\n" + script], cwd=tree, env=env, capture_output=True, text=True, timeout=timeout)

    def test_the_rehearsal_command_runs_as_written(self):
        (block,) = self.blocks("**3. The same rehearsal from your computer**", "**4. Watching the lock yourself**")
        r = self.run_recipe(block, self.recipe_env())
        out = r.stdout + r.stderr
        self.assertEqual(r.returncode, 0, out[-3000:])
        self.assertIn("release-rehearsal: PASSED", out)
        self.assertEqual(self.store.buckets, set(), "the recipe left a throwaway bucket behind: %s" % self.store.buckets)
        self.assertNotIn("test-key-", out, "the service key was printed")

    def test_the_two_publishes_block_runs_as_written(self):
        (block,) = self.blocks("**4. Watching the lock yourself**", "**5. On the first real release")
        cleanup = [l for l in block.splitlines() if l.startswith("scripts/release-rehearsal.sh cleanup")]
        self.assertEqual(len(cleanup), 1, "the block must end by cleaning up the throwaway bucket:\n" + block)
        marker = os.path.join(self.tmp, "bucket-name")
        # (the clean-up line is held back, so the bucket can be looked at first)
        r = self.run_recipe(block.replace(cleanup[0], 'echo "$BUCKET" > "%s"' % marker), self.recipe_env())
        out = r.stdout + r.stderr
        self.assertEqual(r.returncode, 0, out[-3000:])
        with open(marker) as f:
            bucket = f.read().strip()
        self.assertRegex(bucket, r"^zz-publish-test-\d+$", "the recipe must use a throwaway bucket, never the real one")
        self.assertEqual(out.count("took the release lock"), 2, out)
        self.assertEqual(out.count("gave the release lock back"), 2, out)
        self.assertEqual(self.store.get("VERSION.txt", bucket=bucket).decode().strip(), "2.901.0")
        idx = json.loads(self.store.get("versions.json", bucket=bucket))
        self.assertEqual([v["version"] for v in idx["versions"]], ["2.901.0", "2.900.0"])
        self.assertIsNone(self.store.get(LOCK, bucket=bucket))
        self.assertEqual({e["bucket"] for e in self.store.events() if e.get("bucket")}, {bucket}, "the recipe touched another bucket")
        self.assertNotIn("test-key-", out, "the service key was printed")
        # and the last line of the block removes the bucket again (with the same environment the block left behind)
        r = self.run_recipe("read -rs SUPABASE_SERVICE_KEY\n" + cleanup[0], self.recipe_env(BUCKET=bucket))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn(bucket, self.store.buckets)

    def test_the_recovery_steps_run_as_written(self):
        download, publish = self.blocks("### If the Supabase step of a run failed", "### What you will see in the log")
        version = re.search(r"V=(\d+\.\d+\.\d+)", download).group(1)
        run = re.search(r"RUN=(\d+)", download).group(1)
        name = "OkaySpace-windows-v" + version
        gh = os.path.join(self.tmp, "gh")
        os.makedirs(gh)
        with open(os.path.join(gh, "gh"), "w") as f:
            f.write(FAKE_GH_ARTIFACT)
        os.chmod(os.path.join(gh, "gh"), 0o755)
        for switch in ("1", "0"):   # the new script says "same"; the old one (the default today) uploads everything again: both finish the release
            self.store.reset()
            # a run whose publish failed AFTER versions.json was written: VERSION.txt could not be uploaded, so nobody was offered the version
            self.store.rule(r"^releases/VERSION\.txt$", method="POST", status=403)
            rc, out, ci = self.publish("CI" + switch, version)
            self.assertNotEqual(rc, 0, out)
            self.assertIsNotNone(self.listed(version))
            self.assertIsNone(self.store.get("VERSION.txt"))
            self.store.clear_rules()
            open(os.path.join(gh, "calls"), "w").close()
            shutil.rmtree(os.path.join(self.tmp, "recover-" + version), ignore_errors=True)
            env = self.recipe_env(PATH=gh + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"), FAKE_GH_DIR=gh, FAKE_RUN=run, FAKE_ARTIFACT=name,
                                  FAKE_ARTIFACT_ZIP=make_release_zip(self.tmp, ci.dist, version), PUBLISH_V2=switch)
            r = self.run_recipe(download + "\n" + publish, env)
            out = r.stdout + r.stderr
            self.assertEqual(r.returncode, 0, "switch %s:\n%s" % (switch, out[-3000:]))
            with open(os.path.join(gh, "calls")) as f:
                self.assertEqual(f.read().splitlines(), ["run download %s -n %s -D %s/recover-%s" % (run, name, self.tmp, version)])
            if switch == "1":
                self.assertIn("already published with exactly these files", out)
            self.assertEqual(self.store.text("VERSION.txt"), version)
            self.assert_listed(ci)
            self.assert_no_lock()
            self.assertNotIn("test-key-", out, "the service key was printed")
        # step 4: make the GitHub release latest
        r, edits, now = self.run_promote(version, gh_latest="2.875.0", published=version)
        self.assertEqual((r.returncode, edits), (0, ["release edit v%s --repo o/n --latest" % version]), r.stderr)


class RehearsalWorkflow(unittest.TestCase):
    """Properties of .github/workflows/release-rehearsal.yml that keep a rehearsal harmless."""
    PATH = os.path.join(REPO, ".github", "workflows", "release-rehearsal.yml")

    def setUp(self):
        with open(self.PATH) as f:
            self.text = f.read()

    def test_it_only_starts_when_somebody_starts_it(self):
        self.assertRegex(self.text, r"(?m)^on:\n  workflow_dispatch:\n\npermissions:")
        for event in ("push:", "pull_request", "schedule:", "workflow_run", "pull_request_target", "repository_dispatch"):
            self.assertNotIn(event, self.text, "the rehearsal must not start by itself (%s)" % event)

    def test_it_can_only_read_the_repository(self):
        self.assertRegex(self.text, r"(?m)^permissions:\n  contents: read\n")
        self.assertNotRegex(self.text, r"(?m)^\s+(contents|issues|pull-requests|actions|packages|id-token): write")

    def test_the_bucket_is_a_new_throwaway_one_and_the_cleanup_always_runs(self):
        self.assertIn("BUCKET: zz-publish-test-${{ github.run_id }}", self.text)
        self.assertIn("zz-publish-test-?*)", self.text)   # the first step's own refusal
        cleanup = self.text[self.text.index("- name: Empty and delete the throwaway bucket"):]
        self.assertIn("if: always()", cleanup)
        self.assertIn("bash scripts/release-rehearsal.sh cleanup", cleanup)
        self.assertLess(self.text.index("bash scripts/release-rehearsal.sh run"), self.text.index("bash scripts/release-rehearsal.sh cleanup"))

    def test_the_secret_is_used_like_release_yml_and_never_printed(self):
        self.assertEqual(self.text.count("secrets.SUPABASE_SERVICE_KEY"), 3)   # the existence check and the two steps that need the key
        self.assertEqual(self.text.count("SUPABASE_SERVICE_KEY: ${{ secrets.SUPABASE_SERVICE_KEY }}"), 2)
        with open(os.path.join(REPO, "scripts", "release-rehearsal.sh")) as f:
            script = f.read()
        for name, body in (("workflow", self.text), ("script", script)):
            self.assertNotRegex(body, r"(?m)^\s*set -[a-z]*x", "%s traces commands" % name)
            self.assertNotRegex(body, r"curl[^\n]* (-v|--verbose|-vv)( |$)", "%s runs curl verbosely" % name)
            self.assertNotRegex(body, r"echo[^\n]*\$\{?(KEY|SUPABASE_SERVICE_KEY|SEED)\b", "%s echoes a key" % name)
            self.assertNotRegex(body, r"(?m)^\s*(printenv|env)\s*$")


# What GitHub's workflow syntax allows at each level (https://docs.github.com/actions/reference/workflow-syntax-for-github-actions). A typo in
# a key breaks the whole workflow ("Invalid workflow file"), which for release.yml would stop the next real release, and nothing here can
# run GitHub's own validator, so the keys are checked against this list.
WORKFLOW_KEYS = {"name", "run-name", "on", "permissions", "env", "defaults", "concurrency", "jobs"}
JOB_KEYS = {"name", "needs", "if", "runs-on", "permissions", "environment", "concurrency", "outputs", "env", "defaults", "steps",
            "timeout-minutes", "strategy", "continue-on-error", "container", "services", "uses", "with", "secrets"}
STEP_KEYS = {"id", "if", "name", "uses", "run", "shell", "with", "env", "continue-on-error", "timeout-minutes", "working-directory"}
EXPRESSION = re.compile(r"\$\{\{(.*?)\}\}", re.S)


def load_workflow(path):
    import yaml   # (PyYAML ships with the runner image's Python; the tests that need it are skipped where it is missing)
    with open(path) as f:
        return yaml.safe_load(f)


def workflow_problems(doc, path):
    """A list of what is structurally wrong with a parsed workflow: unknown keys, a `needs` that names no job, a step with both or neither
    of uses and run, an output or expression that names a step or job that does not exist."""
    bad = []
    on = doc.get("on", doc.get(True))   # (YAML 1.1 reads a bare `on` as true)
    if on is None:
        bad.append("no 'on:'")
    for k in doc:
        if k not in WORKFLOW_KEYS and k is not True:
            bad.append("workflow: unknown key %r" % k)
    jobs = doc.get("jobs") or {}
    for jid, job in jobs.items():
        for k in job:
            if k not in JOB_KEYS:
                bad.append("job %s: unknown key %r" % (jid, k))
        needs = job.get("needs", [])
        for n in ([needs] if isinstance(needs, str) else needs):
            if n not in jobs:
                bad.append("job %s: needs %r, which is not a job" % (jid, n))
        ids = {st.get("id") for st in job.get("steps", []) if st.get("id")}
        for st in job.get("steps", []):
            for k in st:
                if k not in STEP_KEYS:
                    bad.append("job %s step %r: unknown key %r" % (jid, st.get("name") or st.get("uses"), k))
            if ("uses" in st) == ("run" in st):
                bad.append("job %s step %r: needs exactly one of uses and run" % (jid, st.get("name") or st.get("uses")))
        blob = json.dumps(job)
        for ref in re.findall(r"steps\.([A-Za-z0-9_-]+)\.outputs", blob):
            if ref not in ids:
                bad.append("job %s refers to steps.%s, which has no such id" % (jid, ref))
        for ref in re.findall(r"needs\.([A-Za-z0-9_-]+)\.", blob):
            if ref not in jobs:
                bad.append("job %s refers to needs.%s, which is not a job" % (jid, ref))
        for out, expr in (job.get("outputs") or {}).items():
            for ref in re.findall(r"steps\.([A-Za-z0-9_-]+)\.outputs\.([A-Za-z0-9_-]+)", str(expr)):
                if ref[0] not in ids:
                    bad.append("job %s output %s refers to steps.%s" % (jid, out, ref[0]))
    with open(path) as f:
        text = f.read()
    for expr in EXPRESSION.findall(text):
        if expr.count("(") != expr.count(")") or expr.count("'") % 2:
            bad.append("an expression is not balanced: %s" % expr.strip())
    return bad


class Workflow(unittest.TestCase):
    """What the release workflow and scripts must keep: the new publish path is OFF by default (one switch), builds do not wait for each other,
    and with the switch on only the finalize jobs take turns."""

    def setUp(self):
        with open(RELEASE_YML) as f:
            self.text = f.read()

    def test_the_scripts_parse(self):
        for name in ("publish-supabase.sh", "publish-supabase-v1.sh", "promote-latest.sh", "release-local.sh", "check-unreleased.sh"):
            r = subprocess.run(["bash", "-n", os.path.join(REPO, "scripts", name)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, "%s: %s" % (name, r.stderr))

    def test_the_switch_is_one_line_and_off(self):
        self.assertEqual(re.findall(r'(?m)^\s*PUBLISH_V2: "([^"]*)"', self.text), ["0"], "PUBLISH_V2 must be defined once, as \"0\"")
        self.assertRegex(self.text, r'(?m)^env:\n  PUBLISH_V2: "0"\n', "the switch must be the workflow-level env")

    def test_off_means_the_old_script_and_the_old_latest_command(self):
        step = self.text[self.text.index("- name: Publish to Supabase"):]
        step = step[:step.index("\n\n")]
        self.assertIn('if [ "$PUBLISH_V2" = "1" ]; then exec scripts/publish-supabase.sh; fi', step)
        self.assertRegex(step, r"\n\s+exec scripts/publish-supabase-v1\.sh\s*$", "the default must be the old script, called directly")
        job = self.text[self.text.index("\n  finalize:\n"):]
        self.assertIn('if [ "$PUBLISH_V2" = "1" ]; then\n            # Marks', job)
        self.assertIn('else\n            gh release edit "v$VER" --repo "$GITHUB_REPOSITORY" --latest', job, "the old latest command must stay as the default")
        self.assertIn('bash scripts/promote-latest.sh "$VER"', job)
        self.assertIn("if: env.PUBLISH_V2 == '1'", job)   # the sparse checkout is only for the new path

    def test_the_job_time_limit_is_the_old_one_and_still_covers_the_wait(self):
        limit = int(re.search(r"(?m)^    timeout-minutes: (\d+)\n    outputs:", self.text).group(1))
        self.assertEqual(limit, 45, "the job's time limit is not part of this change")
        wait = int(re.search(r'LOCK_WAIT_SECONDS: "(\d+)"', self.text).group(1))
        self.assertLessEqual(wait // 60 + 15, limit, "a 15 minute build plus the longest wait for the lock must fit in the job's time limit")

    def test_no_workflow_level_concurrency_group(self):
        self.assertIsNone(re.search(r"(?m)^concurrency:", self.text), "a workflow-level concurrency group would make releases build one at a time")

    def test_only_the_finalize_job_takes_turns_and_only_with_the_switch_on(self):
        blocks = re.findall(r"(?m)^    concurrency:\n      group: (.*)\n      cancel-in-progress: (\S+)", self.text)
        self.assertEqual(len(blocks), 1)
        group, cancel = blocks[0]
        self.assertEqual(cancel, "false")
        self.assertIn("needs.windows-exe.outputs.v2 == '1' && 'release-finalize'", group)
        self.assertIn("format('release-finalize-{0}-{1}', github.run_id, github.run_attempt)", group, "off: every run needs a group of its own")
        self.assertGreater(self.text.index("    concurrency:\n"), self.text.index("\n  finalize:\n"), "the group must belong to the finalize job")
        self.assertIn("v2: ${{ steps.switch.outputs.v2 }}", self.text)

    def test_the_early_guard_is_only_on_with_the_switch(self):
        step = self.text[self.text.index("- name: Refuse a version that is already released"):]
        step = step[:step.index("\n\n")]
        self.assertIn("if: env.PUBLISH_V2 == '1'", step)
        self.assertIn('bash scripts/check-unreleased.sh "$VER"', step)
        self.assertLess(self.text.index("- name: Refuse a version that is already released"), self.text.index("- name: Install toolchain"),
                        "the guard must come before the long build")

    def test_a_local_release_is_off_by_default_too(self):
        with open(os.path.join(REPO, "scripts", "release-local.sh")) as f:
            local = f.read()
        self.assertIn('CHANNEL=$CHANNEL DIST_DIR="$PWD/build-win-dist" scripts/publish-supabase-v1.sh', local)
        self.assertIn('if [ "${PUBLISH_V2:-0}" = 1 ]; then scripts/promote-latest.sh "$VER"; else gh release edit "$TAG" --latest; fi', local)
        self.assertIn("ALLOW_REPUBLISH=$FORCE scripts/publish-supabase.sh", local)   # --force is the owner saying "replace it"

    def test_the_checker_itself_catches_mistakes(self):
        good = {"name": "x", "on": {"workflow_dispatch": {}}, "jobs": {"a": {"runs-on": "u", "steps": [{"run": "true", "id": "s1"}]},
                                                                      "b": {"needs": "a", "runs-on": "u", "outputs": {"o": "${{ steps.s2.outputs.v }}"},
                                                                            "steps": [{"id": "s2", "run": "true"}]}}}
        self.assertEqual(workflow_problems(good, RELEASE_YML), [])
        import copy
        for mutate, expect in (
                (lambda d: d["jobs"]["a"].update({"timeout-minute": 5}), "unknown key 'timeout-minute'"),
                (lambda d: d["jobs"]["b"].update({"needs": "nope"}), "which is not a job"),
                (lambda d: d["jobs"]["a"]["steps"][0].update({"uses": "x/y@v1"}), "exactly one of uses and run"),
                (lambda d: d["jobs"]["b"]["outputs"].update({"o": "${{ steps.ghost.outputs.v }}"}), "steps.ghost"),
                (lambda d: d.update({"enviroment": {}}), "workflow: unknown key 'enviroment'")):
            bad = copy.deepcopy(good)
            mutate(bad)
            self.assertTrue(any(expect in problem for problem in workflow_problems(bad, RELEASE_YML)), "not caught: " + expect)

    def test_the_workflow_files_are_well_formed(self):
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("PyYAML is not installed")
        for name in ("release.yml", "release-rehearsal.yml"):   # (the two this change owns; other workflows are their authors' business)
            path = os.path.join(os.path.dirname(RELEASE_YML), name)
            if os.path.exists(path):
                self.assertEqual(workflow_problems(load_workflow(path), path), [], name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
