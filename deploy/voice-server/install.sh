#!/usr/bin/env bash
# One-shot setup of the OkaySpace voice server on a fresh Ubuntu/Debian VPS.
# Run as root from a checkout of the repository:
#   sudo bash deploy/voice-server/install.sh            # port 45100
#   sudo PORT=50000 bash deploy/voice-server/install.sh # custom port
set -euo pipefail
PORT="${PORT:-45100}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

apt-get update
apt-get install -y --no-install-recommends build-essential cmake

cmake -S "$ROOT" -B /tmp/okayspace-relay-build -DCMAKE_BUILD_TYPE=Release \
    -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_USE_SODIUM=OFF
cmake --build /tmp/okayspace-relay-build --target okayspace-relay -j"$(nproc)"
install -m 0755 /tmp/okayspace-relay-build/bin/okayspace-relay /usr/local/bin/okayspace-relay

sed "s/okayspace-relay 45100/okayspace-relay ${PORT}/" "$ROOT/deploy/voice-server/okayspace-voice.service" \
    > /etc/systemd/system/okayspace-voice.service
systemctl daemon-reload
systemctl enable --now okayspace-voice

# Open the UDP port if ufw is in use.
if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
    ufw allow "${PORT}/udp"
fi

IP="$(curl -s -4 https://ifconfig.me || hostname -I | awk '{print $1}')"
echo
echo "Voice server running on UDP ${PORT}."
echo "In the launcher (signed in as an admin): Settings > Chat > 'Voice server for everyone'"
echo "  -> ${IP}:${PORT}   (or a DNS name pointing at this machine)"
