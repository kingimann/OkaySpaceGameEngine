#!/usr/bin/env python3
"""Publishing several releases at once, against a FAKE Supabase Storage on 127.0.0.1 and a fake `gh`.

It runs the real scripts/publish-supabase.sh as separate processes, two at a time, and checks the things that only go wrong when
publishes overlap: both versions end up in versions.json with their checksums, "latest" (VERSION.txt, NOTES.txt) never moves backwards,
the release lock is taken, waited for, taken over when stale, and always given back (also after an error and after SIGTERM). It also
runs scripts/promote-latest.sh (GitHub's "latest" release only moves forward) with a stub `gh`.

  python3 -I tests/scripts/test_publish_lock.py                          everything (about two minutes)
  python3 -I tests/scripts/test_publish_lock.py PublishLock.test_stale_lock_is_taken_over    one test

No network, no real key, nothing outside a temp folder. The fake follows the Supabase Storage REST API as documented for the few calls
the script makes: create a bucket; upload with and without `x-upsert`; delete by prefixes; list; the public (CDN-cached) read and the
authenticated read. It is NOT the real service, so this shows that the script's side is correct and safe against a service that behaves
as documented, not that the real service does. Two behaviours are modelled both ways because the public documentation disagrees with
itself: an upload of an existing object answers 409, or 400 with the 409 in the body ("duplicate_http"), and a missing object 404, or 400
with the 404 in the body ("missing_http"). The first real release after this change is the real check (docs/release_channels.md,
"Several releases at once", "Trying it").
"""
import hashlib
import json
import os
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
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
                if r["gate"]:
                    r["gate"].go.set()   # never leave a request hanging from the last test
            self.buckets, self.objects, self.log, self.rules, self.cdn, self.seq = set(), {}, [], [], {}, 0
            self.duplicate_http, self.missing_http = 409, 404
            self.cdn_ttl, self.jitter = 0.0, 0.0

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
    def rule(self, pattern, method=None, who=None, delay=0.0, status=None, gate=None, times=None, store_then_fail=False):
        r = {"pattern": re.compile(pattern), "method": method, "who": who, "delay": delay, "status": status, "gate": gate, "times": times,
             "store_then_fail": store_then_fail, "fired": 0}
        with self.cond:
            self.rules.append(r)
        return r

    def gate(self, pattern, method="POST", who=None):
        g = Gate()
        self.rule(pattern, method=method, who=who, gate=g, times=1)
        return g

    def match(self, method, target, who):
        with self.cond:
            for r in self.rules:
                if r["method"] not in (None, method) or r["who"] not in (None, who) or not r["pattern"].search(target):
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

        def _reply(self, status, body, ctype="application/json", extra=None):
            raw = body if isinstance(body, (bytes, bytearray)) else json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(raw)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
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
            """Apply what the test injected for this request. Returns (already answered, status to answer after doing the work)."""
            if store.jitter:
                time.sleep(random.uniform(0, store.jitter))
            r = store.match(method, "%s/%s" % (bucket, key), who)
            if r is None:
                return False, None
            if r["delay"]:
                time.sleep(r["delay"])
            if r["gate"]:
                r["gate"].entered.set()
                r["gate"].go.wait(120)
            if r["status"] and not r["store_then_fail"]:
                with store.cond:
                    store.record(who=who, method=method, kind=kind, bucket=bucket, key=key, status=r["status"], injected=True, **rec)
                self._reply(r["status"], {"statusCode": str(r["status"]), "error": "injected", "message": "injected failure"})
                return True, None
            return False, r["status"]

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
            answered, fail = self._intercept(method, bucket, key, who, "upload", upsert=upsert)
            if answered:
                return None
            with store.cond:
                if bucket not in store.buckets:
                    outcome, status = "bucket", 404
                elif (bucket, key) in store.objects and not upsert:
                    outcome, status = "duplicate", 409
                else:
                    store.objects[(bucket, key)] = {"data": body, "ctype": self.headers.get("Content-Type", "")}
                    outcome, status = "ok", 200
                store.record(who=who, method=method, kind="upload", bucket=bucket, key=key, upsert=upsert, status=status, size=len(body))
            if outcome == "ok":
                return self._reply(fail or 200, {"Key": "%s/%s" % (bucket, key)})
            return self._err(outcome)

        def _delete(self, who, bucket, body):
            names = json.loads(body or b"{}").get("prefixes", [])
            answered, fail = self._intercept("DELETE", bucket, ",".join(names), who, "delete", deleted=[])
            if answered:
                return None
            with store.cond:
                deleted = [n for n in names if store.objects.pop((bucket, n), None) is not None]
                store.record(who=who, method="DELETE", kind="delete", bucket=bucket, key=",".join(names), deleted=deleted, status=200)
            return self._reply(fail or 200, [{"name": n, "bucket_id": bucket} for n in deleted])

        def _auth_get(self, who, sub):
            bucket, _, key = sub.partition("/")
            answered, _ = self._intercept("GET", bucket, key, who, "get")
            if answered:
                return None
            with store.cond:
                obj = store.objects.get((bucket, key))
                store.record(who=who, method="GET", kind="get", bucket=bucket, key=key, status=200 if obj else 404)
            if obj is None:
                return self._err("missing")
            return self._reply(200, obj["data"], obj["ctype"] or "application/octet-stream")

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


class Publisher:
    """One scripts/publish-supabase.sh process, started at once. `who` names it in the fake's log (it is the key's suffix)."""

    def __init__(self, case, who, version, tag="x", **env):
        self.case, self.who, self.version = case, who, version
        self.dist = make_dist(case.tmp, who, version, tag)
        notes = os.path.join(case.tmp, "notes-%s-%s-%s.txt" % (who, version, tag))
        with open(notes, "w") as f:
            f.write("- notes for %s\n" % version)
        e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": case.tmp, "LC_ALL": "C", "PYTHONDONTWRITEBYTECODE": "1",
             "no_proxy": "127.0.0.1,localhost", "NO_PROXY": "127.0.0.1,localhost", "https_proxy": DEAD_PROXY, "HTTPS_PROXY": DEAD_PROXY,
             "SUPABASE_URL": case.url, "SUPABASE_SERVICE_KEY": "test-key-" + who, "DIST_DIR": self.dist, "NOTES_FILE": notes,
             "ARCHIVE_REPO": "o/n", "KEEP": "3", "OKAY_UPDATE_SIGNING_SEED": case.seed,
             "LOCK_POLL_SECONDS": "1", "LOCK_WAIT_SECONDS": "90"}
        e.update(env)
        self.log_path = os.path.join(case.tmp, "out-%s-%s-%s.log" % (who, version, tag))
        self.out = open(self.log_path, "w")
        self.proc = subprocess.Popen(["bash", PUBLISH], env=e, stdout=self.out, stderr=subprocess.STDOUT, start_new_session=True)
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
                if e["key"] == LOCK or e["key"] == "" or (kind == "delete" and e["deleted"] == [LOCK]) or e.get("injected"):
                    continue
                w = e["who"]
                self.assertIn(w, iv, "publisher %s wrote %s without ever holding the lock" % (w, e["key"]))
                self.assertTrue(iv[w][0] < e["seq"] < iv[w][1], "publisher %s wrote %s outside its time with the lock" % (w, e["key"]))

    def plant_lock(self, age, token="deadbeefdeadbeef", holder="github-run-1-attempt-1", version="2.800.0", epoch=True):
        e = int(time.time()) - age
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


# ---- the tests: promote-latest.sh and the workflow ---------------------------------------------------------------------------

FAKE_GH = r'''#!/usr/bin/env bash
# a stand-in for the GitHub CLI: $FAKE_GH_DIR/latest holds the tag that is "latest" now, calls is every call made
set -u
echo "$*" >> "$FAKE_GH_DIR/calls"
case "$1 $2" in
  "release view")
      case "$*" in *"--json tagName"*"--jq .tagName"*) ;; *) echo "unexpected: $*" >&2; exit 2 ;; esac
      if [ -n "${FAKE_GH_FAIL:-}" ]; then echo "HTTP 502: Bad Gateway" >&2; exit 1; fi
      if [ -s "$FAKE_GH_DIR/latest" ]; then cat "$FAKE_GH_DIR/latest"; exit 0; fi
      echo "release not found" >&2; exit 1 ;;
  "release edit")
      case "$*" in *" --latest"*) ;; *) echo "unexpected: $*" >&2; exit 2 ;; esac
      printf '%s\n' "$3" > "$FAKE_GH_DIR/latest"; exit 0 ;;
esac
echo "unexpected gh call: $*" >&2; exit 2
'''


class PromoteLatest(Base):
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
        e = {"PATH": d + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"), "FAKE_GH_DIR": d, "GITHUB_REPOSITORY": "o/n", "HOME": self.tmp,
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


class Workflow(unittest.TestCase):
    """What the release workflow and scripts must keep: builds do not wait for each other, only the finalize jobs take turns, and
    every way of marking a release latest goes through scripts/promote-latest.sh."""

    def test_the_scripts_parse(self):
        for name in ("publish-supabase.sh", "promote-latest.sh", "release-local.sh"):
            r = subprocess.run(["bash", "-n", os.path.join(REPO, "scripts", name)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, "%s: %s" % (name, r.stderr))

    def test_a_local_release_goes_through_the_same_guards(self):
        with open(os.path.join(REPO, "scripts", "release-local.sh")) as f:
            local = f.read()
        self.assertIn('scripts/promote-latest.sh "$VER"', local)
        self.assertIn("ALLOW_REPUBLISH=$FORCE", local)   # --force is the owner saying "replace it"
        self.assertNotRegex(local, r"(?m)^\s*gh release edit .*--latest", "release-local.sh must not mark a release latest by itself")

    def setUp(self):
        with open(RELEASE_YML) as f:
            self.text = f.read()

    def test_no_workflow_level_concurrency_group(self):
        self.assertIsNone(re.search(r"(?m)^concurrency:", self.text), "a workflow-level concurrency group would make releases build one at a time")

    def test_only_the_finalize_job_takes_turns(self):
        groups = re.findall(r"(?m)^    concurrency:\n      group: (\S+)\n      cancel-in-progress: (\S+)", self.text)
        self.assertEqual(groups, [("release-finalize", "false")])
        job = self.text[self.text.index("\n  finalize:\n"):]
        self.assertIn("    concurrency:\n      group: release-finalize", job)
        self.assertIn("bash scripts/promote-latest.sh", job)
        self.assertNotRegex(job, r"(?m)^\s+gh release edit ", "finalize must go through scripts/promote-latest.sh, which only moves latest forward")

    def test_the_publish_step_can_wait_for_a_lock_inside_the_job_time_limit(self):
        step = self.text[self.text.index("- name: Publish to Supabase"):]
        wait = int(re.search(r'LOCK_WAIT_SECONDS: "(\d+)"', step).group(1))
        limit = int(re.search(r"(?m)^    timeout-minutes: (\d+)\n    outputs:", self.text).group(1))
        self.assertLessEqual(wait, 2400)
        self.assertGreaterEqual(limit, 45 + wait // 60, "the job time limit must cover the build plus the longest wait for the lock")


if __name__ == "__main__":
    unittest.main(verbosity=2)
