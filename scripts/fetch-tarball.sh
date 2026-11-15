#!/usr/bin/env bash
# Download a .tar.gz and make sure it really is one.
#
# Usage: scripts/fetch-tarball.sh [--sha256 <hex>] <output-file> <url> [<fallback-url> ...]
#
# --sha256 <hex> (or the environment variable OKAY_FETCH_SHA256) pins the file: the download only counts when its SHA-256
# is exactly that, whichever URL it came from. A wrong hash is a failure (never a silent skip), and so is a value that is
# not 64 hex digits, including a "TODO" placeholder. Use it for anything that is linked into a release (libsodium).
#
# CI used to run a bare `curl -sSL` and hand whatever came back to tar. When the
# download host answered with a 504 page, curl still exited 0 and the build died
# later with "gzip: stdin: not in gzip format". This helper fails loudly instead:
# every URL gets several attempts with a growing pause, an HTTP error status is a
# failure (--fail), and the file must list cleanly with `tar tzf` before it counts.
# Later URLs are only tried after the earlier ones are used up.
set -u

want=${OKAY_FETCH_SHA256:-}
pinned=0
[ -n "$want" ] && pinned=1
if [ "${1:-}" = "--sha256" ]; then
  want=${2:-}
  pinned=1   # an empty value is an error below, not "no pin"
  [ "$#" -ge 2 ] || { echo "usage: $0 [--sha256 <hex>] <output-file> <url> [<fallback-url> ...]" >&2; exit 2; }
  shift 2
fi
if [ "$#" -lt 2 ]; then
  echo "usage: $0 [--sha256 <hex>] <output-file> <url> [<fallback-url> ...]" >&2
  exit 2
fi
if [ "$pinned" -eq 1 ]; then
  want=$(printf '%s' "$want" | tr 'A-F' 'a-f')
  case "$want" in
    *[!0-9a-f]*|"") bad=1 ;;
    *) bad=0; [ "${#want}" -eq 64 ] || bad=1 ;;
  esac
  if [ "$bad" -ne 0 ]; then
    echo "error: the SHA-256 pin is not 64 hex digits (a TODO placeholder counts): refusing to download without a real pin" >&2
    exit 2
  fi
fi
sha_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$1" | cut -d' ' -f1; }

out=$1
shift
attempts=${FETCH_ATTEMPTS:-4}

for url in "$@"; do
  n=1
  while [ "$n" -le "$attempts" ]; do
    rm -f "$out"
    if curl -sSL --fail --max-time 300 -o "$out" "$url" && tar tzf "$out" >/dev/null 2>&1; then
      if [ "$pinned" -eq 1 ]; then
        got=$(sha_of "$out")
        if [ "$got" != "$want" ]; then
          echo "SHA-256 MISMATCH for $url: expected $want, got $got" >&2
          n=$((n + 1))
          rm -f "$out"
          [ "$n" -le "$attempts" ] && sleep $((n * 5))
          continue
        fi
        echo "fetched $url ($(wc -c < "$out") bytes, sha256 $got matches the pin)"
      else
        echo "fetched $url ($(wc -c < "$out") bytes, no SHA-256 pin)"
      fi
      exit 0
    fi
    echo "fetch attempt $n/$attempts failed for $url" >&2
    n=$((n + 1))
    if [ "$n" -le "$attempts" ]; then
      sleep $((n * 5))
    fi
  done
done

rm -f "$out"
echo "could not download a valid archive for $out" >&2
exit 1
