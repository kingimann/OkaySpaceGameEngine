#!/usr/bin/env python3
"""examples/account-server/server.py hardening: body cap, per-IP rate limit, username lock-out, token expiry and persistence,
password hashing (versioned rounds, transparent upgrade). Loopback only, temp files, no network beyond 127.0.0.1."""
import http.client
import importlib.util
import json
import os
import socket
import stat
import sys
import tempfile
import threading

sys.dont_write_bytecode = True   # the module under test is loaded from a tracked folder: leave no .pyc there
HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "..", "..", "examples", "account-server", "server.py")
spec = importlib.util.spec_from_file_location("okay_account_server", SERVER)
srv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(srv)

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL:", what)


tmp = tempfile.mkdtemp(prefix="okay_acct_srv_")
srv.DB_PATH = os.path.join(tmp, "accounts.json")
srv.LB_PATH = os.path.join(tmp, "lb.json")
srv.SESSIONS_PATH = ""
srv.PASSWORD_ROUNDS = 2000        # fast for the test; the production value is checked below
srv.LEGACY_ROUNDS = 1500
srv.READ_TIMEOUT = 2
check(srv.__dict__.get("HOST") == os.environ.get("OKAY_ACCOUNT_HOST", "127.0.0.1"), "listens on loopback by default")

httpd = srv.ThreadingHTTPServer(("127.0.0.1", 0), srv.Handler)
port = httpd.server_address[1]
threading.Thread(target=httpd.serve_forever, daemon=True).start()


def call(method, path, body=None, headers=None, raw=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    h = dict(headers or {})
    data = raw
    if body is not None:
        data = json.dumps(body).encode()
        h["Content-Type"] = "application/json"
    c.request(method, path, body=data, headers=h)
    r = c.getresponse()
    text = r.read().decode("utf-8", "replace")
    out = (r.status, text, dict(r.getheaders()))
    c.close()
    return out


def reset_limits(auth=1000, api=1000):
    srv.RATE_AUTH, srv.RATE_API = auth, api
    srv._hits.clear()
    srv._failures.clear()


# ---- the production defaults ------------------------------------------------------------------------------------------------
check(srv.MAX_BODY == 64 * 1024 or "OKAY_ACCOUNT_MAX_BODY" in os.environ, "default body cap is 64 KiB")
check(srv.TOKEN_TTL == 24 * 3600 or "OKAY_ACCOUNT_TOKEN_TTL" in os.environ, "tokens live 24 h by default")
# (the module constant was patched for speed above; the real one is 600000)
src = open(SERVER, encoding="utf-8").read()
check("PASSWORD_ROUNDS = 600_000" in src, "production password rounds are 600000")

# ---- normal use still works ---------------------------------------------------------------------------------------------------
reset_limits()
st, body, _ = call("POST", "/register", {"username": "alice", "password": "s3cret!"})
check(st == 200 and "token" in json.loads(body), "register")
tok = json.loads(body)["token"]
st, body, _ = call("GET", "/verify", headers={"Authorization": "Bearer " + tok})
check(st == 200 and json.loads(body)["username"] == "alice", "verify")
st, _, _ = call("POST", "/login", {"username": "alice", "password": "wrong"})
check(st == 401, "wrong password")
st, body, _ = call("POST", "/login", {"username": "alice", "password": "s3cret!"})
check(st == 200, "login")
db = json.load(open(srv.DB_PATH))
check(db["alice"]["iter"] == srv.PASSWORD_ROUNDS and "s3cret" not in json.dumps(db), "stored with its rounds, no plaintext")

# ---- request size cap ---------------------------------------------------------------------------------------------------------
reset_limits()
s = socket.create_connection(("127.0.0.1", port), timeout=5)
s.sendall(b"POST /login HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: 1000000000\r\n\r\n")
reply = s.recv(4096).decode("latin-1")          # answered without the body ever being sent
check(" 413 " in reply.split("\r\n")[0], "huge Content-Length refused with 413 before reading: " + reply[:40])
s.close()
big = b'{"username":"a","password":"' + b"x" * (srv.MAX_BODY + 10) + b'"}'
try:
    st, _, _ = call("POST", "/login", raw=big, headers={"Content-Type": "application/json"})
    check(st == 413, "oversized body: 413")
except (ConnectionError, OSError):
    pass   # the server may close as soon as it has answered; either way nothing was processed
s = socket.create_connection(("127.0.0.1", port), timeout=5)
s.sendall(b"POST /login HTTP/1.0\r\nContent-Length: -5\r\n\r\n")
check(" 400 " in s.recv(4096).decode("latin-1").split("\r\n")[0], "negative Content-Length refused")
s.close()
s = socket.create_connection(("127.0.0.1", port), timeout=5)
s.sendall(b"POST /login HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
check(" 411 " in s.recv(4096).decode("latin-1").split("\r\n")[0], "chunked bodies refused (no size cap possible)")
s.close()
st, _, _ = call("POST", "/login", raw=b"[1,2,3]", headers={"Content-Type": "application/json"})
check(st == 400, "a JSON array is not an object")
st, _, _ = call("POST", "/login", raw=b"{not json", headers={"Content-Type": "application/json"})
check(st == 400, "malformed JSON")
st, _, _ = call("POST", "/login", {"username": ["x"], "password": 5})
check(st == 400, "non-string fields")
st, _, _ = call("POST", "/register", {"username": "x", "password": "longenough"})
check(st == 400, "username too short")
st, _, _ = call("POST", "/register", {"username": "bad\nname", "password": "longenough"})
check(st == 400, "control characters in the username")
st, _, _ = call("POST", "/register", {"username": "carol", "password": "p" * (srv.MAX_PASSWORD + 1)})
check(st == 400, "password length cap")
# A stalled client (headers sent, body never) is dropped by the read timeout instead of holding the thread.
s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.sendall(b"POST /login HTTP/1.0\r\nContent-Length: 50\r\n\r\n{")
s.settimeout(8)
try:
    data = s.recv(4096)
    check(True, "stalled body: connection ended")
except socket.timeout:
    check(False, "a stalled client held the connection past the read timeout")
s.close()

# ---- per-IP rate limit --------------------------------------------------------------------------------------------------------
reset_limits(auth=5, api=1000)
codes = [call("POST", "/login", {"username": "nobody", "password": "whatever"})[0] for _ in range(8)]
check(codes[:5] == [401] * 5 and codes[5:] == [429] * 3, "6th login in a minute is 429: %s" % codes)
st, body, hdr = call("POST", "/login", {"username": "nobody", "password": "x"})
check(st == 429 and int(hdr.get("Retry-After", "0")) >= 1 and "Too many" in body, "429 carries Retry-After")
reset_limits(auth=1000, api=3)
codes = [call("GET", "/profile", headers={"Authorization": "Bearer " + tok})[0] for _ in range(5)]
check(codes == [200, 200, 200, 429, 429], "general API limit: %s" % codes)
reset_limits()

# ---- username lock-out ----------------------------------------------------------------------------------------------------------
srv.LOCK_AFTER, srv.LOCK_SECONDS = 3, 60
codes = [call("POST", "/login", {"username": "alice", "password": "bad%d" % i})[0] for i in range(3)]
check(codes == [401, 401, 401], "three wrong passwords")
st, _, hdr = call("POST", "/login", {"username": "alice", "password": "s3cret!"})
check(st == 429 and "Retry-After" in hdr, "locked even with the right password")
srv._failures.clear()
st, _, _ = call("POST", "/login", {"username": "alice", "password": "s3cret!"})
check(st == 200, "lock lifted")

# ---- token expiry ---------------------------------------------------------------------------------------------------------------
real_now = srv._now
clock = [real_now()]
srv._now = lambda: clock[0]
srv.TOKEN_TTL = 100
tok2 = srv.issue_token("alice")
check(srv.lookup_token(tok2) == "alice", "a fresh token works")
clock[0] += 99
check(srv.lookup_token(tok2) == "alice", "still valid just before the end")
clock[0] += 2
check(srv.lookup_token(tok2) is None, "expired")
check(srv._token_key(tok2) not in srv.SESSIONS, "an expired token is removed")
st, _, _ = call("GET", "/verify", headers={"Authorization": "Bearer " + tok2})
check(st == 401, "an expired token is refused over HTTP")
check(srv.lookup_token("not-a-token") is None, "an unknown token")

# ---- persistence (opt in) ------------------------------------------------------------------------------------------------------
srv.SESSIONS_PATH = os.path.join(tmp, "sessions.json")
srv.TOKEN_TTL = 1000
tok3 = srv.issue_token("alice")
text = open(srv.SESSIONS_PATH, encoding="utf-8").read()
check(tok3 not in text, "the raw token is never written")
if os.name == "posix":
    check(stat.S_IMODE(os.stat(srv.SESSIONS_PATH).st_mode) == 0o600, "the session file is owner-only")
srv.SESSIONS.clear()
check(srv.lookup_token(tok3) is None, "memory cleared (a restart)")
srv._load_sessions()
check(srv.lookup_token(tok3) == "alice", "the token survives a restart")
clock[0] += 5000
srv._load_sessions()
check(srv.lookup_token(tok3) is None, "an expired token is not loaded back")
srv.SESSIONS_PATH = ""
srv._now = real_now

# ---- password records: rounds are versioned and upgraded ------------------------------------------------------------------------
db = srv.load_db()
salt = "00" * 16
db["legacy"] = {"username": "legacy", "salt": salt, "hash": srv.hash_password("oldpass1", salt, srv.LEGACY_ROUNDS)}   # no "iter"
srv.save_db(db)
reset_limits()
st, _, _ = call("POST", "/login", {"username": "legacy", "password": "nope"})
check(st == 401 and "iter" not in srv.load_db()["legacy"], "a wrong password changes nothing")
st, _, _ = call("POST", "/login", {"username": "legacy", "password": "oldpass1"})
rec = srv.load_db()["legacy"]
check(st == 200 and rec["iter"] == srv.PASSWORD_ROUNDS and rec["salt"] != salt, "legacy record upgraded at login")
st, _, _ = call("POST", "/login", {"username": "legacy", "password": "oldpass1"})
check(st == 200, "the upgraded record works")
db = srv.load_db()
db["tampered"] = {"username": "tampered", "salt": salt, "iter": 5, "hash": "00" * 32}
srv.save_db(db)
check(not srv.check_password(srv.load_db()["tampered"], "x"), "an absurd round count in a record never matches")
check(not srv.check_password(None, "x"), "unknown user")

httpd.shutdown()
print("account_server: %s" % ("ok" if not failures else "%d failures" % failures))
sys.exit(1 if failures else 0)
