# Voice server

Voice calls (friend calls, group calls and server voice rooms) go through the
OkaySpace **relay**: every caller connects *out* to it over UDP and it forwards
audio between the people in the same call. Nobody has to open ports at home.
The relay never decodes the audio.

Until a voice server is configured, the launcher says "No voice server is set up
yet" when you try to call. Set one up once and every launcher picks it up.

## 1. Run the server

You need any machine with a public IP that can receive UDP: a small VPS (1 vCPU,
512 MB is plenty for hundreds of calls), a cloud VM, or a home PC with a
forwarded port.

**Ubuntu / Debian VPS (recommended):**

```
git clone https://github.com/kingimann/OkaySpaceGameEngine.git
cd OkaySpaceGameEngine
sudo bash deploy/voice-server/install.sh
```

This builds the relay, installs it as the `okayspace-voice` systemd service
(restarts automatically, starts on boot) and opens UDP 45100 in ufw. Also allow
**UDP 45100 inbound** in your cloud provider's firewall / security group.

**Docker:**

```
docker compose -f deploy/voice-server/docker-compose.yml up -d --build
```

**Windows:** run `Tools/okayspace-relay.exe` from the OkaySpace download and
forward UDP 45100 on your router to that PC.

## 2. Point everyone at it

Run `docs/sql/app_config.sql` once in the Supabase SQL editor. Then, signed in
to the launcher as an **admin**, open **Settings > Chat** and fill in
**Voice server for everyone** with `your.server.ip:45100` (or a DNS name).
Every launcher reads it at sign-in; nobody has to configure anything.

A player can still enter a **custom voice server** in the same place to use
their own relay instead.

## Checking it

- `systemctl status okayspace-voice` / `journalctl -u okayspace-voice` on the VPS.
- Start a call between two accounts on different networks. The call header
  shows "Connecting..." until both sides reach the relay.
- No audio but connected: check UDP 45100 is open in the provider firewall.
