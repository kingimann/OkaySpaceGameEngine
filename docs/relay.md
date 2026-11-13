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
okayspace-relay                       # listens on UDP 45100
okayspace-relay 50000                 # custom port
okayspace-relay --legacy-hello=off    # refuse version-1/2 hellos (see below)
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

- Each peer sends `RelayHello{role, code, version, hostKey, cookie}` to the
  relay (re-sent ~1/s, which also keeps the NAT mapping open).
- A new peer's first hello gets a `RelayCookie`: an HMAC of its address, port
  and the current minute under a secret only the relay knows. The peer sends
  the hello again with it, and only then is it given a slot. A cookie is good
  for the minute it was made and the next one.
- The relay assigns each peer a **slot** and tells it (and, for clients, the
  host's slot) in a `RelayWelcome`. The host's welcome also carries the
  session's **host key**.
- Each game picks a random 64-bit **nonce** per attempt and puts it in its hellos
  (after the cookie). The relay echoes it in every welcome for that peer, and the
  game ignores a welcome that doesn't carry it, so a welcome forged by someone who
  can't see the game's traffic can't pair it, point it at another slot, or end its
  session as "the host left". A host also ignores a welcome too short to carry its
  host key.
- If the relay doesn't answer within `NetworkManager::relayTimeoutSeconds` (15 by
  default, 0 waits forever) the attempt ends and `RelayError()` says so. A client
  whose code has no host yet hears nothing either, so it times out the same way.
- A hello from an address that is already registered is not proof of life by
  itself: it is rate-limited like any other, must carry the address's cookie (the
  relay answers one without it with a fresh cookie, which a real peer picks up and
  repeats), and a peer that proved its address can't be re-helloed at an older
  version. Spoofed hellos can't keep dead peers in the table. A bad port argument
  (`okayspace-relay 70000`, `abc`) is an error, not a wrapped number.
- A refusal is reported as what it is: a code taken, a protocol version the relay
  doesn't speak, or "refused for an unknown reason (code N)".
- All multi-byte numbers on the wire, in the relay frames and in the game's own
  packets, are little-endian, whatever the host's byte order.
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
- **Nothing is allocated for a spoofed address.** A slot needs a hello that
  echoes the address's cookie, which only the machine really at that address
  receives, so a forged source can't use up someone else's per-address quota.
  A version-3 hello is padded to 32 bytes and shorter ones are ignored, so the
  relay never answers with more bytes than it was sent.
- **Sessions are capped per address:** one IP address may open at most 16
  sessions at once (the peer that names a new code opens it), and hold at most
  64 peers. That fits a LAN party or a home network behind one NAT hosting a
  few games and calls, while everyone else joins those sessions; it takes at
  least 128 addresses to fill the relay's 2048 sessions. Loopback is exempt.
- **Hellos are rate limited:** 10 a second (20 as a burst) from each address
  that isn't a registered peer yet. Keepalive hellos from registered peers
  don't count, and loopback is exempt.
- **Rates are capped per peer:** 1000 packets and 1 MiB per second, with twice
  that as a burst. A host's caps are multiplied by its client count. Each peer
  also receives at most 4 MiB per second from its whole session, so a host's
  clients can't add up to a flood.
- **Versions.** Hellos and welcomes carry the relay protocol version (4 now).
  The relay refuses a version older than it supports with
  `RelayRefused{version}`. Version 4 only adds the nonce: a version-3 game
  still works against a version-4 relay (it gets the old, shorter welcome), and a
  version-4 game that meets an older relay drops its welcomes as unverifiable and
  gives up with "the relay's welcome could not be verified". To use an older relay
  anyway, set `allowUnverifiedRelay = true` on that game; its welcomes are then
  trusted as before, so a forged one can't be told from the real one. Update the
  relay instead where you can.

**Legacy hellos.** Version-1 and -2 hellos carry no cookie. The relay still
accepts them while `--legacy-hello` is on (the default for now; this is step 1 of
issue #733 and the default is not changed here), and once a minute, while any
arrive, logs how many ("N legacy (version 1/2) hellos in the last minute; M legacy
peers registered; hellos so far v1=.. v2=.. v3=.. v4=.."). `RelayServer` exposes the
same numbers: `LegacyHellos()` (v1/v2 hellos from new senders), `LegacyPeers()` (peers
registered right now that speak v1/v2) and `HellosByVersion(v)`. Switch to
`--legacy-hello=off` once `LegacyPeers()` stays 0 and the v1/v2 counts stop growing:
those hellos are refused with `RelayRefused{version}`.

Every client this repository ships now speaks version 4. The game's `NetworkManager`
and the launcher's voice calls build their hello with the one shared builder,
`okay/Net/RelayHello.hpp` (`BuildRelayHello`: cookie, nonce, padding) and check each
welcome with `ParseRelayWelcome` (it must echo the nonce). A voice call uses the
`RelayHub` role. It falls back to a version-1 hello only after six unanswered
version-4 hellos (a relay older than version 3 never answers one), and only then
accepts a welcome without the echo; a relay that refuses it ends the call with a
"update the launcher" message. Older launchers and games already in the field still
send version 1 or 2 hellos until players update, which is why the default stays on.
A version-3 game talks to a version-2 relay unchanged: the older relay ignores the
cookie and padding.

Peers without a version byte (version 1: older games, and older launchers'
voice calls, where every member announces the host role) still work. They get the
forwarding rules and caps. A version-1 session gets no key, so it keeps the old
hand-over: when its host leaves, the next host-role hello takes over. A
version-2 game host won't join a session a version-1 peer is hosting.

**A peer's address changes.** The relay knows a peer by its source address. If a host's
NAT rebinds, its next hello comes from a new address with the host key, and the relay moves
the host there and keeps its slot: clients notice nothing. If a client's address changes it
is a new peer with a new slot; the client sees the new slot in the next welcome and reports
the connection lost ("this connection's address changed on the relay"), and with
`autoReconnect` rejoins and resumes its state through the new address within a couple of seconds.

**Hub sessions.** Version 3 keeps that hand-over for sessions that want it with
the `RelayHub` role (the launcher's voice calls): every member says hub, the
first one in hosts, and the session never gets a host key, so when the hub
leaves the next hub hello takes over. A hub hello can't take a game's protected
session, and a game host won't join a hub session that has a hub.

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

Each call is a relay session: everyone registers as hub (`RelayHub`, behind
the relay's cookie), so the first one in becomes the room's hub and fans each 20 ms audio frame out to the others; if
the hub leaves, the next hello makes someone else the hub. Audio is 16 kHz mono
IMA ADPCM (~64 kbit/s per speaker), only sent while you're talking, and
encrypted with ChaCha20 under a key that never reaches the relay: a random
per-call key carried in the private "[Call]" DM, or derived from a server's
invite code for its Lounge.
