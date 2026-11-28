#!/bin/bash
# Run okayui golden / port tests under changed environments and require the same result everywhere: the committed golden pictures are the reference, so a test
# that passes in every variant draws the same pixels whatever the machine says about time, locale, home folder, network, fonts or working folder.
# usage: scripts/okayui_hermetic_check.sh test_binary [more test binaries...]      (the binaries are built with OKAYUI_GOLDEN_DIR set, e.g. by CMake)
# Variants: normal; empty environment; other time zones and locales; other home folder and working folder; the clock moved by +46 days and by -2.8 years
# (LD_PRELOAD shim built here from scripts/okayui_timeshim.c); no network at all (unshare -n); a fake HTTP proxy that answers every request with junk
# (http_proxy / https_proxy / ALL_PROXY); installed fonts hidden (mount namespace, root only). Variants that cannot run here are reported as skipped.
cd "$(dirname "$0")/.."
tmp=$(mktemp -d); trap 'kill $srv 2>/dev/null; rm -rf "$tmp"' EXIT
gcc -shared -fPIC -O1 scripts/okayui_timeshim.c -o "$tmp/shim.so" -ldl 2>/dev/null && haveshim=1 || haveshim=0
cat > "$tmp/junk.py" <<'PY'
import http.server, socketserver
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self): self.send_response(200); self.send_header("Content-Length","24"); self.end_headers(); self.wfile.write(b"junk junk junk junk jun\n")
    def log_message(self,*a): pass
    do_POST = do_GET; do_CONNECT = do_GET
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("127.0.0.1", 18765), H) as s: s.serve_forever()
PY
python3 "$tmp/junk.py" & srv=$!; sleep 0.5
mkdir -p "$tmp/empty-fonts" "$tmp/home" "$tmp/cwd"
fail=0
run() {  # name, command...
  local name=$1; shift
  local out; out=$("$@" 2>&1); local rc=$?
  if [ $rc -ne 0 ] || echo "$out" | grep -q "FAIL"; then echo "  FAIL [$name]"; echo "$out" | grep -E "FAIL|golden" | head -3; fail=1; else echo "  ok   [$name]"; fi
}
for t in "$@"; do
  echo "== $t"
  run normal "$t"
  run "empty environment" env -i "$t"
  run "TZ Auckland, LANG ja" env TZ=Pacific/Auckland LANG=ja_JP.UTF-8 LC_ALL=ja_JP.UTF-8 "$t"
  run "TZ Los Angeles, LANG de" env TZ=America/Los_Angeles LANG=de_DE.UTF-8 "$t"
  run "other HOME and cwd" bash -c "cd '$tmp/cwd' && HOME='$tmp/home' XDG_DATA_HOME='$tmp/home' XDG_CONFIG_HOME='$tmp/home' '$t'"
  if [ $haveshim = 1 ]; then
    run "clock +46 days" env LD_PRELOAD="$tmp/shim.so" FAKE_OFFSET=4000000 "$t"
    run "clock -2.8 years" env LD_PRELOAD="$tmp/shim.so" FAKE_OFFSET=-90000000 "$t"
  else echo "  skip [clock shift: no C compiler]"; fi
  if unshare -n true 2>/dev/null; then run "no network (unshare -n)" unshare -n env "$t"; else echo "  skip [no network: unshare -n not allowed]"; fi
  run "fake HTTP proxy answering junk" env http_proxy=http://127.0.0.1:18765 https_proxy=http://127.0.0.1:18765 HTTP_PROXY=http://127.0.0.1:18765 HTTPS_PROXY=http://127.0.0.1:18765 ALL_PROXY=http://127.0.0.1:18765 "$t"
  if unshare -m true 2>/dev/null; then run "installed fonts hidden" unshare -m bash -c "mount --bind '$tmp/empty-fonts' /usr/share/fonts && '$t'"; else echo "  skip [fonts hidden: no mount namespace]"; fi
done
[ $fail = 0 ] && echo "all variants passed" || echo "SOME VARIANTS FAILED"
exit $fail
