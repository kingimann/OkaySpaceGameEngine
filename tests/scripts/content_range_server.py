#!/usr/bin/env python3
"""A tiny static file server WITH Range support (python's http.server has none), for the content delivery end-to-end test.
Usage: content_range_server.py <folder> <portfile>   (binds 127.0.0.1 on a free port, writes the port number to <portfile>, runs until killed)
Loopback only; serves files below <folder>; "--cut" in the folder's file name list is not special. An optional environment variable
CONTENT_SERVER_CUT=<name>:<bytes> makes the FIRST full GET of that file send only that many bytes (then close), so the client must resume."""
import http.server
import os
import sys

ROOT = os.path.abspath(sys.argv[1])
CUT = os.environ.get("CONTENT_SERVER_CUT", "")
cut_name, _, cut_bytes = CUT.partition(":")
state = {"cut_done": False}


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        rel = self.path.split("?", 1)[0].lstrip("/")
        from urllib.parse import unquote
        rel = unquote(rel)
        p = os.path.abspath(os.path.join(ROOT, rel))
        if not p.startswith(ROOT + os.sep) or not os.path.isfile(p):
            self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers(); return
        data = open(p, "rb").read()
        rng = self.headers.get("Range")
        start = 0
        status = 200
        if rng and rng.startswith("bytes=") and rng.endswith("-"):
            start = int(rng[6:-1])
            if start >= len(data):
                self.send_response(416); self.send_header("Content-Length", "0"); self.end_headers(); return
            status = 206
        body = data[start:]
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(data) - 1, len(data)))
        self.end_headers()
        if cut_name and rel == cut_name and not state["cut_done"] and start == 0:
            state["cut_done"] = True
            self.wfile.write(body[: int(cut_bytes)])
            self.wfile.flush()
            self.connection.close()
            return
        self.wfile.write(body)


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
open(sys.argv[2], "w").write(str(srv.server_address[1]))
srv.serve_forever()
