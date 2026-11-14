#!/usr/bin/env python3
"""A tiny stand-in for the Supabase Storage endpoints scripts/publish-supabase.sh uses, for tests (no network, no keys).

  mock_supabase.py PORT_FILE ROOT     serves on 127.0.0.1 (random port, written to PORT_FILE) and keeps the bucket under ROOT
Public reads, uploads, bucket creation, listing and deletes: only what the script calls."""
import http.server
import json
import os
import sys
import urllib.parse

ROOT = sys.argv[2]
PUB = "/storage/v1/object/public/releases/"
UP = "/storage/v1/object/releases/"
LIST = "/storage/v1/object/list/releases"


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body=b"{}", ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _path(self):
        return urllib.parse.unquote(urllib.parse.urlparse(self.path).path)

    def _body(self):
        return self.rfile.read(int(self.headers.get("Content-Length") or 0))

    def do_GET(self):
        p = self._path()
        if p.startswith(PUB):
            f = os.path.join(ROOT, p[len(PUB):])
            if os.path.isfile(f):
                with open(f, "rb") as fh:
                    return self._send(200, fh.read(), "application/octet-stream")
        self._send(404, b'{"error":"not found"}')

    def do_POST(self):
        p = self._path()
        data = self._body()
        if p == "/storage/v1/bucket":
            return self._send(200)
        if p.startswith(UP):
            f = os.path.join(ROOT, p[len(UP):])
            os.makedirs(os.path.dirname(f), exist_ok=True)
            with open(f, "wb") as fh:
                fh.write(data)
            return self._send(200)
        if p == LIST:
            prefix = json.loads(data or b"{}").get("prefix", "")
            d = os.path.join(ROOT, prefix)
            names = sorted(os.listdir(d)) if os.path.isdir(d) else []
            return self._send(200, json.dumps([{"name": n} for n in names]).encode())
        self._send(404)

    def do_DELETE(self):
        self._body()
        self._send(200)


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
with open(sys.argv[1], "w") as f:
    f.write(str(srv.server_address[1]))
srv.serve_forever()
