#!/usr/bin/env python3
"""A fake of the part of the Supabase Storage API that the publish scripts use, for tests. Loopback only; nothing leaves this machine.

  fake_storage.py --root DIR --log FILE --key KEY --port-file FILE

Objects are plain files under DIR/<bucket>/<path>, so a test seeds the "bucket" and looks at the result with ordinary file tools, and can empty
or refill it between cases while the server keeps running (it keeps no state of its own). Every request is appended to --log as one line,
"<METHOD> <path> <status>" (headers and bodies are never logged). The key is whatever the test passes; a request with another key gets 401.

  GET    /storage/v1/object/public/<bucket>/<path>      public read; a missing object is HTTP 400 with "not_found" in the body, as the real service does
  GET    /storage/v1/object/<bucket>/<path>             authenticated read
  POST   /storage/v1/object/<bucket>/<path>             upload; refused with 409 when the object exists, unless the request says x-upsert: true
  POST   /storage/v1/object/list/<bucket>               {"prefix": "...", "limit": N} -> the names directly under the prefix (folders have "id": null)
  DELETE /storage/v1/object/<bucket>                    {"prefixes": ["a/b", ...]}
  POST   /storage/v1/bucket                             logged and answered 200 (a test asserts the dev publisher never calls it)
"""
import argparse
import http.server
import json
import os
import sys
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "fake-storage"

    def log_message(self, fmt, *args):   # (the default writes to stderr: the tests keep their own log)
        pass

    # ---- helpers
    def _record(self, status):
        with self.server.lock:
            with open(self.server.log, "a", encoding="utf-8") as f:
                f.write("%s %s %d\n" % (self.command, self.path.split("?")[0], status))

    def _send(self, status, body=b"", ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self._record(status)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def _authorized(self):
        return self.headers.get("Authorization") == "Bearer " + self.server.key

    def _object_path(self, bucket, rel):
        parts = [p for p in rel.split("/") if p]
        if not parts or any(p in (".", "..") for p in parts) or "\\" in rel:
            return None
        return os.path.join(self.server.root, bucket, *parts)

    def _split(self):
        return self.path.split("?")[0].split("/")   # ['', 'storage', 'v1', 'object', ...]

    def _not_found(self):
        self._send(400, {"statusCode": "404", "error": "not_found", "message": "Object not found"})

    # ---- verbs
    def do_GET(self):
        p = self._split()
        if p[:5] == ["", "storage", "v1", "object", "public"] and len(p) >= 7:
            bucket, rel = p[5], "/".join(p[6:])
        elif p[:4] == ["", "storage", "v1", "object"] and len(p) >= 6 and p[4] != "list":
            if not self._authorized():
                return self._send(401, {"statusCode": "401", "error": "Unauthorized"})
            bucket, rel = p[4], "/".join(p[5:])
        else:
            return self._send(404, {"error": "no such endpoint"})
        path = self._object_path(bucket, rel)
        if path is None or not os.path.isfile(path):
            return self._not_found()
        with open(path, "rb") as f:
            self._send(200, f.read(), "application/octet-stream")

    def do_POST(self):
        p = self._split()
        body = self._body()
        if not self._authorized():
            return self._send(401, {"statusCode": "401", "error": "Unauthorized"})
        if p == ["", "storage", "v1", "bucket"]:
            return self._send(200, {"name": "bucket"})
        if p[:5] == ["", "storage", "v1", "object", "list"] and len(p) == 6:
            try:
                req = json.loads(body.decode() or "{}")
            except ValueError:
                return self._send(400, {"error": "bad json"})
            prefix = str(req.get("prefix", "")).strip("/")
            base = os.path.join(self.server.root, p[5], *[x for x in prefix.split("/") if x])
            out = []
            if os.path.isdir(base):
                for name in sorted(os.listdir(base)):
                    full = os.path.join(base, name)
                    out.append({"name": name, "id": None} if os.path.isdir(full)
                               else {"name": name, "id": "id-" + name, "metadata": {"size": os.path.getsize(full)}})
            return self._send(200, out[: int(req.get("limit", 100))])
        if p[:4] == ["", "storage", "v1", "object"] and len(p) >= 6 and p[4] != "list":
            path = self._object_path(p[4], "/".join(p[5:]))
            if path is None:
                return self._send(400, {"error": "bad path"})
            if os.path.exists(path) and (self.headers.get("x-upsert") or "").lower() != "true":
                return self._send(409, {"statusCode": "409", "error": "Duplicate", "message": "The resource already exists"})
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(body)
            return self._send(200, {"Key": "/".join(p[4:])})
        self._send(404, {"error": "no such endpoint"})

    def do_DELETE(self):
        p = self._split()
        body = self._body()
        if not self._authorized():
            return self._send(401, {"statusCode": "401", "error": "Unauthorized"})
        if p[:4] == ["", "storage", "v1", "object"] and len(p) == 5:
            try:
                prefixes = json.loads(body.decode() or "{}").get("prefixes", [])
            except ValueError:
                return self._send(400, {"error": "bad json"})
            gone = []
            for rel in prefixes:
                path = self._object_path(p[4], str(rel))
                if path and os.path.isfile(path):
                    os.remove(path)
                    gone.append({"name": rel})
            # (empty folders vanish, as in the real service, where folders are only a naming habit)
            root = os.path.join(self.server.root, p[4])
            for dirpath, dirnames, filenames in os.walk(root, topdown=False):
                if dirpath != root and not os.listdir(dirpath):
                    os.rmdir(dirpath)
            return self._send(200, gone)
        self._send(404, {"error": "no such endpoint"})


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--key", required=True)
    ap.add_argument("--port-file", required=True)
    a = ap.parse_args(argv)
    os.makedirs(a.root, exist_ok=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    srv.root, srv.log, srv.key, srv.lock = os.path.abspath(a.root), a.log, a.key, threading.Lock()
    tmp = a.port_file + ".tmp"
    with open(tmp, "w") as f:
        f.write(str(srv.server_address[1]))
    os.replace(tmp, a.port_file)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
