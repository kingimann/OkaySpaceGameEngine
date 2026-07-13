# Photon Realtime transport (opt-in)

OkaySpace multiplayer normally runs on its own transport (direct UDP or the
bundled relay — see `multiplayer` docs). For games that want to ride
**Photon Cloud** instead — no servers or relays of your own, rooms hosted on
Photon's infrastructure — the engine has a Photon adapter behind its
transport seam (`INetTransport`).

## Why it's opt-in

Photon's Realtime C++ SDK is a **closed-source, per-platform download** from
[dashboard.photonengine.com](https://dashboard.photonengine.com) and its
license doesn't allow us to bundle it. The engine therefore ships with the
Photon provider as a safe stub; the real adapter
(`engine/src/Net/PhotonTransport.cpp`) compiles only when you provide the SDK.

## Setup

1. Create a (free) Photon account and a **Realtime** app — note its **App ID**.
2. Download the *Photon Realtime C++ SDK* (v5.x) for your platform and unpack
   it. The folder should contain `LoadBalancing-cpp/`, `Photon-cpp/` and
   `Common-cpp/`.
3. Configure the engine build with the adapter enabled:

   ```
   cmake -DOKAY_WITH_PHOTON=ON -DOKAY_PHOTON_SDK_DIR=/path/to/photon-sdk ...
   ```

4. Provide the App ID at runtime — either the `OKAY_PHOTON_APPID` environment
   variable, or pass it explicitly in the host/join call (below).

> The adapter targets SDK v5.x. Photon's headers shift a little between
> minor versions; if yours differs, the fixes are usually confined to the
> includes and Listener signatures in `PhotonTransport.cpp`.

## Using it

Switch the transport provider, then host/join as usual:

```cpp
using namespace okay;
NetTransport::Use(NetTransportProvider::Photon);        // instead of Native
auto& t = NetTransport::Get();

// Room named by you; the App ID rides in the "host" argument (or comes from
// the OKAY_PHOTON_APPID environment variable if you pass "").
t.HostViaRelay("<your-app-id>", 0, "my-room-code");     // create the room
t.JoinViaRelay("<your-app-id>", 0, "my-room-code");     // join it elsewhere

t.SendReliable("chat", "hello");
t.OnRpc("chat", [](std::uint32_t from, const std::string& msg) { /* ... */ });
```

Call `NetTransport::Get().Update()` once per frame (the Photon client needs
regular servicing; the Native transport's `Update()` is a no-op, so calling it
unconditionally is fine).

### Mapping reference

| INetTransport call            | Photon behaviour                          |
| ----------------------------- | ----------------------------------------- |
| `StartServer(port)`           | connect + create room `okay-<port>`       |
| `StartClient(host, port)`     | connect + join room `okay-<port>`         |
| `HostViaRelay(appId,_,code)`  | connect + create room `<code>`            |
| `JoinViaRelay(appId,_,code)`  | connect + join room `<code>`              |
| `Send` / `SendReliable`       | `opRaiseEvent` (event code 1)             |
| `Rpc` / `OnRpc`               | `opRaiseEvent` (event code 2)             |
| `IsServer()`                  | you are the room's master client          |
| `PeerCount()`                 | players in the room, minus you            |

## Scope & alternatives

The adapter covers the transport verbs (rooms, messaging, RPC). Photon's
higher-level products (Fusion, Quantum, Photon Matchmaking/Party) are out of
scope — for a server browser, keep the Native services backend
(`NetBackend`), which also pairs with the PlayFab backend for auth, cloud
saves and leaderboards (see `visual_scripting.md` ▸ PlayFab).
