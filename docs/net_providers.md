# Net providers: extension points only

The engine has two provider seams, `INetTransport` (realtime transport) and
`INetBackend` (online services). Each has a built-in **Native** provider that
the editor, the player, the launcher and every script call use. The other
providers named in the enums are **extension points for C++ programmers**, not
features.

| Seam | Provider | State |
| --- | --- | --- |
| `NetTransportProvider` | `Native` | Works. Direct UDP and the bundled relay. The only one anything in the editor, the player or the script API uses. |
| | `Photon` | Not a feature. A stub: every call fails, `LastError()` says it is not built in. It becomes real only in a C++ build you make yourself with `-DOKAY_WITH_PHOTON=ON` and Photon's own SDK (see [photon.md](photon.md)). That adapter is not part of the official builds or CI. |
| | `Custom` | A stub. Every call fails. It exists so you have a slot to fill. |
| `NetBackendProvider` | `Native` | Works. Account sign-in, cloud saves, leaderboards and the server browser (Supabase, or the local fallback). |
| | `PlayFab` | A stub. Every call fails. No PlayFab matchmaking or backend adapter exists. |
| | `Custom` | A stub. Every call fails. |

Nothing selects these providers outside C++. There is no editor menu, Build
Settings option, Project Settings field, Inspector field or script call for
them, and none will be added until a provider actually works. Game code that
never calls `NetTransport::Use(...)` or `NetBackend::Use(...)` always runs on
Native.

The separate **PlayFab section of the editor's Services panel** and the
`playfab_*` script calls are not these stubs. They talk straight to PlayFab's
REST API (title ID, custom-ID login, player stats, leaderboards) and work on
their own, without selecting a `NetBackend` provider.

## What you need to implement a provider yourself

1. An account with the service and its credentials (a Photon App ID, a PlayFab
   title ID and secret, or your own server). The engine cannot create these
   for you and ships none.
2. For Photon, Photon's closed-source per-platform C++ SDK, which the licence
   does not let the engine bundle. For PlayFab, an HTTPS client (the engine's
   `PlayFab` class is a starting point for the REST calls).
3. A class that implements `INetTransport` (see `okay/Net/NetTransport.hpp`) or
   `INetBackend` (see `okay/Net/NetBackend.hpp`), and a build of the engine that
   creates it in `CreateNetTransport` / `CreateNetBackend`.
4. Tests that connect to the real service. The existing tests only check that
   the stubs fail safely.
5. A switch in your game's startup code, before first use:
   `NetTransport::Use(...)` or `NetBackend::Use(...)`.

Until then, use Native, which is what ships and what is tested.
