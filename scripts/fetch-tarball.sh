#!/usr/bin/env bash
# Download a .tar.gz and make sure it really is one.
#
# Usage: scripts/fetch-tarball.sh <output-file> <url> [<fallback-url> ...]
#
# CI used to run a bare `curl -sSL` and hand whatever came back to tar. When the
# download host answered with a 504 page, curl still exited 0 and the build died
# later with "gzip: stdin: not in gzip format". This helper fails loudly instead:
# every URL gets several attempts with a growing pause, an HTTP error status is a
# failure (--fail), and the file must list cleanly with `tar tzf` before it counts.
# Later URLs are only tried after the earlier ones are used up.
set -u

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <output-file> <url> [<fallback-url> ...]" >&2
  exit 2
fi

out=$1
shift
attempts=${FETCH_ATTEMPTS:-4}

for url in "$@"; do
  n=1
  while [ "$n" -le "$attempts" ]; do
    rm -f "$out"
    if curl -sSL --fail --max-time 300 -o "$out" "$url" && tar tzf "$out" >/dev/null 2>&1; then
      echo "fetched $url ($(wc -c < "$out") bytes)"
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
