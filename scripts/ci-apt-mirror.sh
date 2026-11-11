#!/usr/bin/env bash
# CI helper: if the runner image's default Ubuntu mirror (azure.archive.ubuntu.com)
# cannot be reached, point apt at archive.ubuntu.com instead, so package installs
# do not fail for an outage that has nothing to do with our code.
# Safe to run when the mirror is fine: it then changes nothing.
set -u
PROBE="http://azure.archive.ubuntu.com/ubuntu/dists/noble/InRelease"
if curl -fsS -m 20 -o /dev/null "$PROBE"; then
  echo "apt mirror ok, no change"
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
exit 0
