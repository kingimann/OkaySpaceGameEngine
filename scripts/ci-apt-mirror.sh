#!/usr/bin/env bash
# CI helper: if the runner image's default Ubuntu mirror (azure.archive.ubuntu.com)
# cannot be reached, point apt at archive.ubuntu.com instead, so package installs
# do not fail for an outage that has nothing to do with our code.
# Safe to run when the mirror is fine: it then changes nothing.
set -u
# After the mirror choice, make sure apt really has the Ubuntu package index. A job once failed with
# "Unable to locate package cmake" because `apt-get update` fetched only the Microsoft repository (a transient
# mirror problem, nothing to do with our code). Retry the update a few times until a known package is found.
ensure_index() {
  for i in 1 2 3 4; do
    sudo apt-get update -qq || true
    if apt-cache show ninja-build >/dev/null 2>&1; then return 0; fi
    echo "apt index has no Ubuntu packages yet (try $i of 4), waiting"
    sleep $((i * 10))
  done
  echo "apt index still empty after 4 tries (the install step will fail with the real apt error)"
}
# The runner image also lists Microsoft's package repository, which none of our installs need. On 2026-11-27 it answered
# "403 Forbidden ... is no longer signed", and `apt-get update` (run by `playwright install --with-deps`) then failed the
# WebGL job. If that repository cannot be read, switch it off for this job; when it is fine it is left alone.
for f in /etc/apt/sources.list.d/*microsoft*.list /etc/apt/sources.list.d/*microsoft*.sources; do
  [ -f "$f" ] || continue
  url=$(grep -oE 'https?://packages\.microsoft\.com/[^ ]*' "$f" | head -1)
  [ -n "$url" ] || continue
  if ! curl -fsS -m 20 -o /dev/null "${url%/}/dists/$(. /etc/os-release && echo "$VERSION_CODENAME")/InRelease"; then
    echo "Microsoft package repository unreadable ($url), switching it off for this job: $f"
    sudo mv "$f" "$f.disabled"
  fi
done
PROBE="http://azure.archive.ubuntu.com/ubuntu/dists/noble/InRelease"
if curl -fsS -m 20 -o /dev/null "$PROBE"; then
  echo "apt mirror ok, no change"
  ensure_index
  exit 0
fi
echo "default apt mirror unreachable, switching to archive.ubuntu.com"
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
  [ -f "$f" ] || continue
  sudo sed -i \
    -e 's#mirror+file:/etc/apt/apt-mirrors.txt#http://archive.ubuntu.com/ubuntu/#g' \
    -e 's#http://azure\.archive\.ubuntu\.com/ubuntu/\?#http://archive.ubuntu.com/ubuntu/#g' \
    "$f"
done
ensure_index
exit 0
