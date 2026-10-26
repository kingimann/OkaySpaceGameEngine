# Relay & NAT traversal

Most players are behind a home router (NAT): they can connect *out* to the
internet, but nothing on the internet can connect *in* to them without
port-forwarding. Direct `net_host` / `StartServer` works great on a LAN or with a
public/forwarded port, but for two players on home connections you need a meeting
point. OkaySpace ships a small **relay** for exactly that.

Both the host and its clients connect *out* to the same relay and announce a
shared **session code**. The relay pairs them by code and forwards datagrams
between them, rewriting a per-peer "slot" id so each side sees a stable sender. No
port-forwarding, no public IP on the players' side.

The relay is a dumb forwarder — it never inspects or decrypts the payload, so the
engine's end-to-end encryption (`encryption = true`) still holds through it.

## Run a relay

Run the bundled binary on any host with a reachable IP/port (a cheap VPS, a port
you *have* forwarded, etc.):

```
okayspace-relay              # listens on UDP 45100
okayspace-relay 50000        # custom port
```

It prints the bound port and then forwards quietly. One relay handles many
independent sessions (keyed by code) at once.

## Use it from a game

C++:

```cpp
auto* net = obj->AddComponent<NetworkManager>();
// Host:
net->HostViaRelay("relay.example.com", 45100, "my-room-code");
// Client (same code):
net->JoinViaRelay("relay.example.com", 45100, "my-room-code");
// Optional: poll RelayReady() to know once the relay has paired you.
```

OkayScript:

```javascript
function start() {
  net_name("Host");
  net_host_relay("relay.example.com", 45100, "my-room-code");
}
// client side: net_join_relay("relay.example.com", 45100, "my-room-code");
// net_relay_ready() -> 1 once paired; from there net_send / net_poll / snapshots
// all work exactly as in a direct session.
```

Everything else — rooms, lobbies, reliable messages, synced vars, replicated
spawns, encryption — behaves identically to a direct session; only how the bytes
get there changes.

## How it works

- Each peer sends `RelayHello{role, code, version, hostKey}` to the relay
  (re-sent ~1/s, which also keeps the NAT mapping open).
- The relay assigns each peer a **slot** and tells it (and, for clients, the
  host's slot) in a `RelayWelcome`. The host's welcome also carries the
  session's **host key**.
- App traffic is wrapped as `RelayData{destSlot}` to the relay, which forwards it
  as `RelayData{srcSlot}` to the destination.
- Inside `NetworkManager`, a relayed peer is keyed by a synthetic
  `Endpoint{address = slot}`, so the entire server/client code path — handshake,
  snapshots, fragmentation, encryption — is unchanged between direct and relayed
  play.

## What the relay enforces

The relay is on the internet, so it doesn't trust its peers:

- **Only host and clients talk.** A client's packets go only to its session's
  host, and the host's only to its clients. A client can't reach another client,
  or anyone in another session; everything between players goes through the
  host, which runs the game's checks (including anti-cheat).
- **Nobody can take a session code over.** The first host of a code gets a
  secret key in its welcome and sends it back in every hello. While the session
  exists, a host hello without that key is refused with
  `RelayRefused{already hosted}`, and that host's `NetworkManager::RelayError()`
  says so. The real host can move to a new address (its router changed ports)
  with the key and keeps its slot.
- **Clients notice the host leaving.** When the host goes, the relay sends its
  clients a welcome with host slot 0. A client disconnects when its host slot
  changes at all, so it's never re-pointed at a new host; `RelayError()` says
  "the host left the session".
- **Rates are capped per peer:** 1000 packets and 1 MiB per second, with twice
  that as a burst. A host's caps are multiplied by its client count.
- **Versions.** Hellos and welcomes carry the relay protocol version (2 now).
  The relay refuses a version older than it supports with
  `RelayRefused{version}`.

Peers without a version byte (version 1: older games, and the launcher's voice
calls, where every member announces the host role) still work. They get the
forwarding rules and caps. A version-1 session gets no key, so it keeps the old
hand-over: when its host leaves, the next host-role hello takes over. A
version-2 game host won't join a session a version-1 peer is hosting.

One gap follows from that: a version-1 peer that registers a game's code as
host before the game's own (version-2) host does holds the code. The real host
is refused with "already hosted" until that peer leaves. It can't see or join
the game, so this is squatting, not a takeover. Use random session codes
(the launcher makes them) so a code can't be guessed in advance.

This is a relay (TURN-style), not hole-punching (STUN): it always works,
including under symmetric NATs, at the cost of routing traffic through the relay
host.

## Voice calls in the launcher

The launcher's voice calls (DM calls and each server's **Lounge**) use the same
relay. Point it at yours in **Settings > Chat > Voice server** (`host` or
`host:port`, default port 45100), or bake a default into builds with
`-DOKAY_DEFAULT_VOICE_RELAY=voice.example.com:45100`.

Each call is a relay session: everyone registers as host, so the first one in
becomes the room's hub and fans each 20 ms audio frame out to the others; if
the hub leaves, the next hello makes someone else the hub. Audio is 16 kHz mono
IMA ADPCM (~64 kbit/s per speaker), only sent while you're talking, and
encrypted with ChaCha20 under a key that never reaches the relay: a random
per-call key carried in the private "[Call]" DM, or derived from a server's
invite code for its Lounge.
