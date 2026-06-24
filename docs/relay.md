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

- Each peer sends `RelayHello{role, code}` to the relay (re-sent ~1/s, which also
  keeps the NAT mapping open).
- The relay assigns each peer a **slot** and tells it (and, for clients, the
  host's slot) in a `RelayWelcome`.
- App traffic is wrapped as `RelayData{destSlot}` to the relay, which forwards it
  as `RelayData{srcSlot}` to the destination.
- Inside `NetworkManager`, a relayed peer is keyed by a synthetic
  `Endpoint{address = slot}`, so the entire server/client code path — handshake,
  snapshots, fragmentation, encryption — is unchanged between direct and relayed
  play.

This is a relay (TURN-style), not hole-punching (STUN): it always works,
including under symmetric NATs, at the cost of routing traffic through the relay
host.
