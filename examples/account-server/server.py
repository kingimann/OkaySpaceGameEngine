#!/usr/bin/env python3
"""Minimal reference auth server for OkaySpace accounts.

It speaks the exact contract the engine/launcher account client expects:

    POST /register   {"username": "...", "password": "..."}
    POST /login      {"username": "...", "password": "..."}

  * On success: HTTP 200 with JSON  {"token": "<opaque session token>"}
  * On failure: HTTP 4xx with JSON  {"error": "<human-readable reason>"}

This is intentionally tiny and dependency-free (Python standard library only)
so you can run it locally to try the online backend end to end. It is NOT
production-ready: it stores accounts in a local JSON file. Put a real
database and TLS in front of it before shipping. What it does so a test setup
is not an easy target:

  * request bodies are capped (OKAY_ACCOUNT_MAX_BODY, 64 KiB) and read with a
    time limit; a bigger or malformed Content-Length is refused before reading;
  * per-IP rate limits (OKAY_ACCOUNT_RATE_AUTH logins/registrations per minute,
    OKAY_ACCOUNT_RATE_API other requests per minute) and a short lock-out of a
    username after repeated wrong passwords: HTTP 429 with Retry-After;
  * session tokens expire (OKAY_ACCOUNT_TOKEN_TTL seconds, default 24 hours) and
    are stored only as SHA-256 hashes; with OKAY_ACCOUNT_SESSIONS=<file> they
    survive a restart (the file is owner-only), otherwise they live in memory;
  * passwords use PBKDF2-HMAC-SHA256 at 600,000 rounds with a per-user random
    salt; older records (200,000 rounds) are re-hashed at the next login, and
    unknown usernames cost the same time as wrong passwords;
  * by default it listens on 127.0.0.1 only (OKAY_ACCOUNT_HOST=0.0.0.0 opens it).

Run it:

    python3 examples/account-server/server.py            # listens on :8080

Then point the launcher or engine at it:

    export OKAY_ACCOUNT_SERVER=http://localhost:8080
    ./build/bin/OkaySpace                                # Account tab -> online

(Use https:// with a real certificate in production — the client sends the
password in the request body.)
"""
import hashlib
import hmac
import json
import os
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs


def _int_env(name, default):
    try:
        return int(os.environ.get(name, default))
    except ValueError:
        return int(default)


DB_PATH = os.environ.get("OKAY_ACCOUNT_DB", "accounts.json")
LB_PATH = os.environ.get("OKAY_LEADERBOARD_DB", "leaderboards.json")
HOST = os.environ.get("OKAY_ACCOUNT_HOST", "127.0.0.1")
PORT = _int_env("OKAY_ACCOUNT_PORT", 8080)
MAX_BODY = _int_env("OKAY_ACCOUNT_MAX_BODY", 64 * 1024)        # bytes a request body may be
READ_TIMEOUT = _int_env("OKAY_ACCOUNT_READ_TIMEOUT", 10)       # seconds to read one request
TOKEN_TTL = _int_env("OKAY_ACCOUNT_TOKEN_TTL", 24 * 3600)      # seconds a session token lives
SESSIONS_PATH = os.environ.get("OKAY_ACCOUNT_SESSIONS", "")    # "" = in memory only
RATE_AUTH = _int_env("OKAY_ACCOUNT_RATE_AUTH", 10)             # /login + /register per IP per minute
RATE_API = _int_env("OKAY_ACCOUNT_RATE_API", 120)              # every other request per IP per minute
LOCK_AFTER = _int_env("OKAY_ACCOUNT_LOCK_AFTER", 5)            # wrong passwords before a username is locked...
LOCK_SECONDS = _int_env("OKAY_ACCOUNT_LOCK_SECONDS", 60)       # ...for this long
PASSWORD_ROUNDS = 600_000
LEGACY_ROUNDS = 200_000                                        # records without an "iter" field
MAX_PASSWORD = 256                                             # longer is refused (it would only cost hashing time)
MAX_TRACKED = 10_000                                           # entries kept by the rate limiter

_LOCK = threading.Lock()
# Issued session tokens: sha256(token) -> {"user": name, "exp": unix seconds}. Raw tokens are never stored.
SESSIONS = {}
_hits = {}        # (ip, bucket) -> [timestamps within the last minute]
_failures = {}    # lower-case username -> [failure count, locked-until]


def _now():
    return time.time()


def _load(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _save(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f)
    os.replace(tmp, path)


def load_db():
    return _load(DB_PATH)


def save_db(db):
    _save(DB_PATH, db)


# Leaderboards: { board_name: { username: best_score } }.
def load_lb():
    return _load(LB_PATH)


def save_lb(lb):
    _save(LB_PATH, lb)


def hash_password(password, salt, rounds=None):
    rounds = PASSWORD_ROUNDS if rounds is None else rounds
    dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"),
                             bytes.fromhex(salt), rounds)
    return dk.hex()


_DUMMY_SALT = secrets.token_hex(16)


def check_password(rec, password):
    """True when `password` matches the record. Constant-time compare; an
    unknown user (rec None) still pays for one hash so timing doesn't tell
    whether the name exists."""
    if not rec:
        hash_password(password, _DUMMY_SALT)
        return False
    rounds = int(rec.get("iter", LEGACY_ROUNDS))
    if not 1000 <= rounds <= 20_000_000:
        return False
    return hmac.compare_digest(hash_password(password, rec["salt"], rounds), rec["hash"])


def _token_key(token):
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def _load_sessions():
    if not SESSIONS_PATH:
        return
    data = _load(SESSIONS_PATH)
    now = _now()
    with _LOCK:
        SESSIONS.clear()
        for k, v in data.items():
            if isinstance(v, dict) and isinstance(v.get("user"), str) and v.get("exp", 0) > now:
                SESSIONS[k] = {"user": v["user"], "exp": v["exp"]}


def _save_sessions():          # call with _LOCK held
    if not SESSIONS_PATH:
        return
    tmp = SESSIONS_PATH + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)   # owner-only from the first byte
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(SESSIONS, f)
    os.replace(tmp, SESSIONS_PATH)


def issue_token(username):
    token = secrets.token_hex(24)
    now = _now()
    with _LOCK:
        for k in [k for k, v in SESSIONS.items() if v["exp"] <= now]:   # drop the expired
            del SESSIONS[k]
        SESSIONS[_token_key(token)] = {"user": username, "exp": now + TOKEN_TTL}
        _save_sessions()
    return token


def lookup_token(token):
    """The username for a live token, else None (an expired one is removed)."""
    key = _token_key(token)
    with _LOCK:
        v = SESSIONS.get(key)
        if v is None:
            return None
        if v["exp"] <= _now():
            del SESSIONS[key]
            _save_sessions()
            return None
        return v["user"]


def rate_check(ip, bucket, limit):
    """None when allowed, else the seconds to wait. A sliding one-minute window per (ip, bucket)."""
    now = _now()
    with _LOCK:
        if len(_hits) > MAX_TRACKED:                 # bounded memory: forget idle clients
            for k in [k for k, v in _hits.items() if not v or v[-1] < now - 60]:
                del _hits[k]
            if len(_hits) > MAX_TRACKED:
                _hits.clear()
        hits = [t for t in _hits.get((ip, bucket), []) if t > now - 60]
        if len(hits) >= limit:
            _hits[(ip, bucket)] = hits
            return max(1, int(hits[0] + 60 - now) + 1)
        hits.append(now)
        _hits[(ip, bucket)] = hits
    return None


def locked_for(username):
    with _LOCK:
        f = _failures.get(username.lower())
        if f and f[1] > _now():
            return max(1, int(f[1] - _now()) + 1)
    return None


def note_login(username, ok):
    with _LOCK:
        key = username.lower()
        if ok:
            _failures.pop(key, None)
            return
        if len(_failures) > MAX_TRACKED:
            _failures.clear()
        f = _failures.setdefault(key, [0, 0])
        f[0] += 1
        if f[0] >= LOCK_AFTER:
            f[0] = 0
            f[1] = _now() + LOCK_SECONDS


def valid_username(name):
    return 2 <= len(name) <= 32 and all(32 <= ord(c) < 127 for c in name)


class Handler(BaseHTTPRequestHandler):
    def _send(self, status, obj):
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def setup(self):
        super().setup()
        self.connection.settimeout(READ_TIMEOUT)   # a client that stops sending can't hold a thread forever

    def _ip(self):
        return self.client_address[0]

    def _limited(self, bucket, limit):
        """Send a 429 and return True when this client is over its limit."""
        wait = rate_check(self._ip(), bucket, limit)
        if wait is None:
            return False
        payload = json.dumps({"error": "Too many requests. Try again in %d seconds." % wait}).encode("utf-8")
        self.send_response(429)
        self.send_header("Content-Type", "application/json")
        self.send_header("Retry-After", str(wait))
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        return True

    def _read_json(self):
        """The parsed JSON object, None for malformed JSON, or "refused" after the error was sent."""
        if self.headers.get("Transfer-Encoding"):
            self._send(411, {"error": "Content-Length is required."})
            return "refused"
        try:
            length = int(self.headers.get("Content-Length", 0))
        except ValueError:
            self._send(400, {"error": "Bad Content-Length."})
            return "refused"
        if length < 0:
            self._send(400, {"error": "Bad Content-Length."})
            return "refused"
        if length > MAX_BODY:                       # refused before a byte is read
            self.close_connection = True
            self._send(413, {"error": "Request too large (limit %d bytes)." % MAX_BODY})
            return "refused"
        try:
            raw = self.rfile.read(length) if length else b""
        except OSError:                             # timed out or reset mid-body
            self.close_connection = True
            return "refused"
        if len(raw) != length:
            self.close_connection = True
            return "refused"
        try:
            obj = json.loads(raw or b"{}")
        except (json.JSONDecodeError, UnicodeDecodeError, RecursionError):
            return None
        return obj if isinstance(obj, dict) else None

    def do_POST(self):
        path = urlsplit(self.path).path.rstrip("/")
        if self._limited("auth" if path in ("/login", "/register") else "api",
                         RATE_AUTH if path in ("/login", "/register") else RATE_API):
            return
        body = self._read_json()
        if body == "refused":
            return
        if body is None:
            return self._send(400, {"error": "Malformed JSON."})

        # Authenticated cloud save: POST /cloud/<key>  {"data": "..."}
        cloud_key = self._cloud_key(path)
        if cloud_key is not None:
            rec = self._bearer_record()
            if rec is None:
                return self._send(401, {"error": "Invalid or expired session."})
            rec.setdefault("cloud", {})[cloud_key] = body.get("data", "")
            db = load_db(); db[rec["username"].lower()] = rec; save_db(db)
            return self._send(200, {"ok": True})

        # Authenticated score submit: POST /leaderboard/<board>  {"score": N}
        board = self._board(path)
        if board is not None:
            rec = self._bearer_record()
            if rec is None:
                return self._send(401, {"error": "Invalid or expired session."})
            try:
                score = int(body.get("score", 0))
            except (TypeError, ValueError):
                return self._send(400, {"error": "Score must be a number."})
            lb = load_lb()
            board_scores = lb.setdefault(board, {})
            name = rec["username"]
            if score > board_scores.get(name, -(2 ** 62)):   # keep the best
                board_scores[name] = score
            save_lb(lb)
            return self._send(200, {"ok": True})

        username = body.get("username") if isinstance(body.get("username"), str) else ""
        password = body.get("password") if isinstance(body.get("password"), str) else ""
        username = username.strip()
        if not username or not password:
            return self._send(400, {"error": "Username and password are required."})
        if len(password) > MAX_PASSWORD or not valid_username(username):
            return self._send(400, {"error": "Username (2-32 plain characters) or password (up to %d) is not acceptable." % MAX_PASSWORD})

        db = load_db()
        key = username.lower()

        if path == "/register":
            if key in db:
                return self._send(409, {"error": "That username is already taken."})
            salt = secrets.token_hex(16)
            db[key] = {"username": username, "salt": salt, "iter": PASSWORD_ROUNDS,
                       "hash": hash_password(password, salt)}
            save_db(db)
            return self._send(200, {"token": issue_token(username)})

        if path == "/login":
            wait = locked_for(username)
            if wait is not None:
                self.send_response(429)
                payload = json.dumps({"error": "Too many wrong passwords. Try again in %d seconds." % wait}).encode("utf-8")
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", str(wait))
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                return
            rec = db.get(key)
            ok = check_password(rec, password)
            note_login(username, ok)
            if not ok:
                return self._send(401, {"error": "Invalid username or password."})
            if int(rec.get("iter", LEGACY_ROUNDS)) < PASSWORD_ROUNDS:   # transparent upgrade: the password is in hand
                rec["salt"] = secrets.token_hex(16)
                rec["iter"] = PASSWORD_ROUNDS
                rec["hash"] = hash_password(password, rec["salt"])
                db[key] = rec
                save_db(db)
            return self._send(200, {"token": issue_token(rec["username"])})

        return self._send(404, {"error": "Unknown endpoint."})

    def do_DELETE(self):
        if self._limited("api", RATE_API):
            return
        cloud_key = self._cloud_key(self.path.rstrip("/"))
        rec = self._bearer_record()
        if rec is None:
            return self._send(401, {"error": "Invalid or expired session."})
        if cloud_key is None:
            return self._send(404, {"error": "Unknown endpoint."})
        rec.get("cloud", {}).pop(cloud_key, None)
        db = load_db(); db[rec["username"].lower()] = rec; save_db(db)
        return self._send(200, {"ok": True})

    @staticmethod
    def _cloud_key(path):
        """The save slot name for a /cloud/<key> path, else None."""
        if path.startswith("/cloud/"):
            return path[len("/cloud/"):]
        return None

    @staticmethod
    def _board(path):
        """The board name for a /leaderboard/<board> path, else None."""
        if path.startswith("/leaderboard/"):
            return path[len("/leaderboard/"):]
        return None

    def _bearer_user(self):
        """The username for the request's bearer token, or None."""
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return None
        return lookup_token(auth[len("Bearer "):].strip())

    def _bearer_record(self):
        """The account record for the request's bearer token, or None."""
        user = self._bearer_user()
        return load_db().get(user.lower()) if user else None

    def do_GET(self):
        if self._limited("api", RATE_API):
            return
        # Authenticated endpoints: the client sends Authorization: Bearer <token>.
        user = self._bearer_user()
        split = urlsplit(self.path)
        path = split.path.rstrip("/")
        query = parse_qs(split.query)

        if path == "/verify":
            if not user:
                return self._send(401, {"error": "Invalid or expired session."})
            return self._send(200, {"username": user})

        if path == "/profile":   # sample protected resource
            if not user:
                return self._send(401, {"error": "Invalid or expired session."})
            return self._send(200, {"username": user, "level": 1, "coins": 0})

        # Cloud saves: GET /cloud lists slots; GET /cloud/<key> reads one.
        if path == "/cloud" or path.startswith("/cloud/"):
            rec = self._bearer_record()
            if rec is None:
                return self._send(401, {"error": "Invalid or expired session."})
            cloud = rec.get("cloud", {})
            cloud_key = self._cloud_key(path)
            if cloud_key is None:                      # GET /cloud
                return self._send(200, {"keys": sorted(cloud.keys())})
            if cloud_key not in cloud:                 # GET /cloud/<key>
                return self._send(404, {"error": "No such save slot."})
            return self._send(200, {"data": cloud[cloud_key]})

        # Leaderboard: GET /leaderboard/<board>?count=N -> top entries.
        board = self._board(path)
        if board is not None:
            if not user:
                return self._send(401, {"error": "Invalid or expired session."})
            try:
                count = int(query.get("count", ["10"])[0])
            except (TypeError, ValueError):
                count = 10
            scores = load_lb().get(board, {})
            ranked = sorted(scores.items(), key=lambda kv: kv[1], reverse=True)
            entries = [f"{i + 1},{name},{score}"
                       for i, (name, score) in enumerate(ranked[:max(0, count)])]
            return self._send(200, {"entries": entries})

        return self._send(404, {"error": "Unknown endpoint."})

    # Quieter logging.
    def log_message(self, fmt, *args):
        print("[account-server] " + (fmt % args))


def main():
    _load_sessions()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"OkaySpace account server listening on http://{HOST}:{PORT} "
          f"(db: {DB_PATH})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")


if __name__ == "__main__":
    main()
