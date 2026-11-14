#!/usr/bin/env bash
# The Linux publish path of scripts/publish-supabase.sh (PLATFORM=linux) and the Linux dist script, against a local stand-in for
# Supabase Storage (tests/scripts/mock_supabase.py): no network, no keys, no real build (the "programs" are copies of /bin/true).
# Checks: Windows publish unchanged in effect; Linux files are ADDED to the version's entry with their checksums and the Windows
# entries and VERSION.txt are left alone; a Linux publish before the Windows one is refused; wrong file types are refused; the
# dist script lays out the tarball like the Windows zip and its checksum verifies.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"
trap 'kill "${SRV:-0}" 2>/dev/null; rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

command -v python3 >/dev/null || { echo "skipped: no python3"; exit 0; }
[ -x /bin/true ] || { echo "skipped: no /bin/true"; exit 0; }
[ "$(head -c4 /bin/true | od -An -c | tr -d ' ')" = '177ELF' ] || { echo "skipped: /bin/true is not an ELF file"; exit 0; }

mkdir -p "$T/bucket"
python3 -I "$REPO/tests/scripts/mock_supabase.py" "$T/port" "$T/bucket" & SRV=$!
for _ in $(seq 1 50); do [ -s "$T/port" ] && break; sleep 0.1; done
[ -s "$T/port" ] || { echo "FAIL: stand-in server did not start"; exit 1; }
export SUPABASE_URL="http://127.0.0.1:$(cat "$T/port")" SUPABASE_SERVICE_KEY=test-key
export GITHUB_REPOSITORY=o/n NOTES_FILE="$T/notes.txt"
echo "- test" > "$T/notes.txt"
unset OKAY_UPDATE_SIGNING_SEED SUPABASE_ACCESS_TOKEN CHANNEL REQUIRE_SIGNED

# ---- fake dists ------------------------------------------------------------
W="$T/win"; mkdir -p "$W/Tools"; printf '2.900.0\n' > "$W/VERSION.txt"
for f in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe; do { printf 'MZ'; echo "$f windows"; } > "$W/$f"; done
L="$T/lin"; mkdir -p "$L/Tools"; printf '2.900.0\n' > "$L/VERSION.txt"
for f in OkaySpace Tools/OkayEngine Tools/OkaySpacePlayer; do cp /bin/true "$L/$f"; echo "$f linux" >> "$L/$f"; done

idx() { python3 -I - "$T/bucket/versions.json" "$@" <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); ver = sys.argv[2]
e = [v for v in d["versions"] if v["version"] == ver]
if not e: print("none"); sys.exit(0)
print(" ".join(sorted(e[0]["files"])), "|", d.get("latest"), "|", e[0]["channel"], e[0]["date"])
PY
}

# 1. Linux before Windows: refused (no index, no entry), nothing uploaded for it.
out="$(PLATFORM=linux DIST_DIR="$L" bash "$REPO/scripts/publish-supabase.sh" 2>&1)"; code=$?
[ "$code" -ne 0 ] || bad "linux publish before windows should fail"
case "$out" in *"publish the Windows build"*|*"not in the index"*) ;; *) bad "no reason for a linux-first publish: $out" ;; esac
[ ! -e "$T/bucket/VERSION.txt" ] || bad "linux-first publish wrote VERSION.txt"

# 2. Windows publish as before.
out="$(DIST_DIR="$W" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" || bad "windows publish failed: $out"
[ "$(cat "$T/bucket/VERSION.txt" 2>/dev/null)" = "2.900.0" ] || bad "windows publish did not write VERSION.txt"
r="$(idx 2.900.0)"; [ "${r%% |*}" = "OkayEngine.exe OkaySpace.exe OkaySpacePlayer.exe" ] || bad "windows index entry wrong: $r"
cp "$T/bucket/versions.json" "$T/before.json"

# 3. Linux publish adds its files; Windows data, VERSION.txt, NOTES untouched.
out="$(PLATFORM=linux DIST_DIR="$L" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" || bad "linux publish failed: $out"
r="$(idx 2.900.0)"
[ "${r%% |*}" = "OkayEngine-linux-x64 OkayEngine.exe OkaySpace-linux-x64 OkaySpace.exe OkaySpacePlayer-linux-x64 OkaySpacePlayer.exe" ] || bad "combined entry wrong: $r"
python3 -I - "$T/before.json" "$T/bucket/versions.json" "$L" <<'PY' || bad "checksums or untouched Windows data wrong"
import hashlib, json, sys
a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2])); L = sys.argv[3]
ea, eb = a["versions"][0], b["versions"][0]
for n in ("OkaySpace.exe", "OkayEngine.exe", "OkaySpacePlayer.exe"):
    assert ea["files"][n] == eb["files"][n], n
assert (ea["channel"], ea["date"], ea["inBucket"]) == (eb["channel"], eb["date"], eb["inBucket"])
assert a["latest"] == b["latest"] == "2.900.0"
for n, f in (("OkaySpace-linux-x64", "OkaySpace"), ("OkayEngine-linux-x64", "Tools/OkayEngine"), ("OkaySpacePlayer-linux-x64", "Tools/OkaySpacePlayer")):
    data = open(L + "/" + f, "rb").read()
    assert eb["files"][n] == {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}, n
PY
for n in OkaySpace-linux-x64 OkayEngine-linux-x64 OkaySpacePlayer-linux-x64; do [ -s "$T/bucket/v2.900.0/$n" ] || bad "$n not uploaded"; done
[ "$(cat "$T/bucket/VERSION.txt")" = "2.900.0" ] || bad "linux publish changed VERSION.txt"

# 4. Re-running the Linux publish is safe (same result), and a Windows re-publish of the same version drops nothing it should keep
#    only when Linux is published again: documented order is Windows first, then Linux.
out="$(PLATFORM=linux DIST_DIR="$L" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" || bad "linux re-publish failed: $out"

# 5. Wrong kinds of file are refused before any upload.
BADL="$T/badlin"; cp -r "$L" "$BADL"; { printf 'MZ'; echo x; } > "$BADL/Tools/OkayEngine"
out="$(PLATFORM=linux DIST_DIR="$BADL" bash "$REPO/scripts/publish-supabase.sh" 2>&1)"; code=$?
{ [ "$code" -ne 0 ] && case "$out" in *"isn't a Linux program"*) true ;; *) false ;; esac; } || bad "a non-ELF Linux file was accepted: $out"
BADW="$T/badwin"; cp -r "$W" "$BADW"; cp /bin/true "$BADW/OkaySpace.exe"
out="$(DIST_DIR="$BADW" bash "$REPO/scripts/publish-supabase.sh" 2>&1)"; code=$?
{ [ "$code" -ne 0 ] && case "$out" in *"isn't a Windows program"*) true ;; *) false ;; esac; } || bad "an ELF file was accepted as a Windows program: $out"
out="$(PLATFORM=macos DIST_DIR="$L" bash "$REPO/scripts/publish-supabase.sh" 2>&1)"; code=$?
[ "$code" -ne 0 ] || bad "PLATFORM=macos should be refused"

# 6. A LATER Windows version still publishes normally and does not need Linux files.
W2="$T/win2"; cp -r "$W" "$W2"; printf '2.901.0\n' > "$W2/VERSION.txt"; echo more >> "$W2/OkaySpace.exe"
out="$(DIST_DIR="$W2" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" || bad "second windows publish failed: $out"
r="$(idx 2.901.0)"; [ "${r%% |*}" = "OkayEngine.exe OkaySpace.exe OkaySpacePlayer.exe" ] || bad "windows-only entry wrong: $r"
r="$(idx 2.900.0)"; case "$r" in *OkayEngine-linux-x64*) ;; *) bad "the earlier version lost its Linux files: $r" ;; esac

# ---- the dist script (no compiler: SKIP_BUILD with fake programs) ----------
FB="$T/fakebuild/bin"; mkdir -p "$FB"
for f in OkaySpace OkayEngine OkaySpacePlayer okayspace-relay; do cp /bin/true "$FB/$f"; done
( cd "$REPO" && SKIP_BUILD=1 BUILD_DIR="$T/fakebuild" OUT_DIR="$T/dist" SYMBOLS_DIR="$T/sym" bash scripts/build-linux-dist.sh >"$T/dist.log" 2>&1 ) || { bad "build-linux-dist.sh failed: $(tail -5 "$T/dist.log")"; }
TAR="$(ls "$REPO"/OkaySpace-linux-x64-v*.tar.gz 2>/dev/null | head -1)"
if [ -n "$TAR" ]; then
    ( cd "$REPO" && sha256sum -c "$TAR.sha256" >/dev/null 2>&1 ) || bad "tarball checksum does not verify"
    listing="$(tar tzf "$TAR")"
    for p in "/OkaySpace" "/Tools/OkayEngine" "/Tools/OkaySpacePlayer" "/Tools/okayspace-relay" "/README.txt" "/VERSION.txt"; do
        printf '%s\n' "$listing" | grep -q -- "^OkaySpace-linux-x64-v[0-9.]*$p$" || bad "tarball lacks $p"
    done
    tar tvzf "$TAR" | grep -E ' OkaySpace-linux-x64-v[0-9.]*/OkaySpace$' | grep -q '^-rwx' || bad "launcher is not executable in the tarball"
    rm -f "$TAR" "$TAR.sha256"
else bad "no tarball was made"; fi
# a non-ELF "program" is refused
cp "$T/win/OkaySpace.exe" "$FB/OkaySpace"
( cd "$REPO" && SKIP_BUILD=1 BUILD_DIR="$T/fakebuild" OUT_DIR="$T/dist2" SYMBOLS_DIR="$T/sym2" bash scripts/build-linux-dist.sh >"$T/dist2.log" 2>&1 ) && bad "dist script accepted a non-ELF launcher"
rm -f "$REPO"/OkaySpace-linux-x64-v*.tar.gz "$REPO"/OkaySpace-linux-x64-v*.tar.gz.sha256

# ---- the Python and C++ name lists agree -------------------------------------
python3 -I - "$REPO" <<'PY' || bad "scripts/versions_index.py and shared/ReleaseAssets.hpp list different Linux names"
import re, sys
repo = sys.argv[1]
sys.dont_write_bytecode = True
sys.path.insert(0, repo + "/scripts")
import versions_index as vi
hpp = open(repo + "/shared/ReleaseAssets.hpp").read()
names = set(re.findall(r'"(Okay[A-Za-z]*-linux-x64)"', hpp))
assert names == set(vi.LINUX_FILES), (names, vi.LINUX_FILES)
assert set(re.findall(r'"(Okay[A-Za-z]*\.exe)"', hpp)) >= set(vi.REQUIRED_FILES)
PY

[ "$fail" = 0 ] && echo "publish_linux: ok"
exit "$fail"
