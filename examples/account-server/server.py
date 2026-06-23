#!/usr/bin/env python3
"""Minimal reference auth server for OkaySpace accounts.

It speaks the exact contract the engine/launcher account client expects:

    POST /register   {"username": "...", "password": "..."}
    POST /login      {"username": "...", "password": "..."}

  * On success: HTTP 200 with JSON  {"token": "<opaque session token>"}
  * On failure: HTTP 4xx with JSON  {"error": "<human-readable reason>"}

This is intentionally tiny and dependency-free (Python standard library only)
so you can run it locally to try the online backend end to end. It is NOT
production-ready: it stores accounts in a local JSON file and issues random
in-memory tokens. Put a real database, TLS, and rate limiting in front of it
before shipping.

Run it:

    python3 examples/account-server/server.py            # listens on :8080

Then point the launcher or engine at it:

    export OKAY_ACCOUNT_SERVER=http://localhost:8080
    ./build/bin/OkaySpace                                # Account tab -> online

(Use https:// with a real certificate in production — the client sends the
password in the request body.)
"""
import hashlib
import json
import os
import secrets
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DB_PATH = os.environ.get("OKAY_ACCOUNT_DB", "accounts.json")
HOST = os.environ.get("OKAY_ACCOUNT_HOST", "0.0.0.0")
PORT = int(os.environ.get("OKAY_ACCOUNT_PORT", "8080"))

# Issued session tokens -> username (in memory; lost on restart).
SESSIONS = {}


def load_db():
    try:
        with open(DB_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_db(db):
    tmp = DB_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(db, f)
    os.replace(tmp, DB_PATH)


def hash_password(password, salt):
    # PBKDF2-HMAC-SHA256; a real server can tune the iteration count up.
    dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"),
                             bytes.fromhex(salt), 200_000)
    return dk.hex()


def issue_token(username):
    token = secrets.token_hex(24)
    SESSIONS[token] = username
    return token


class Handler(BaseHTTPRequestHandler):
    def _send(self, status, obj):
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return None

    def do_POST(self):
        body = self._read_json()
        if body is None:
            return self._send(400, {"error": "Malformed JSON."})
        username = (body.get("username") or "").strip()
        password = body.get("password") or ""
        if not username or not password:
            return self._send(400, {"error": "Username and password are required."})

        db = load_db()
        key = username.lower()

        if self.path.rstrip("/") == "/register":
            if key in db:
                return self._send(409, {"error": "That username is already taken."})
            salt = secrets.token_hex(16)
            db[key] = {"username": username, "salt": salt,
                       "hash": hash_password(password, salt)}
            save_db(db)
            return self._send(200, {"token": issue_token(username)})

        if self.path.rstrip("/") == "/login":
            rec = db.get(key)
            if not rec or hash_password(password, rec["salt"]) != rec["hash"]:
                return self._send(401, {"error": "Invalid username or password."})
            return self._send(200, {"token": issue_token(rec["username"])})

        return self._send(404, {"error": "Unknown endpoint."})

    # Quieter logging.
    def log_message(self, fmt, *args):
        print("[account-server] " + (fmt % args))


def main():
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"OkaySpace account server listening on http://{HOST}:{PORT} "
          f"(db: {DB_PATH})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")


if __name__ == "__main__":
    main()
