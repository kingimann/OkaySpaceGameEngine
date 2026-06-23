# Player accounts

OkaySpace has a small account system for signing players in and out. It is used
in two places that share the same code:

* the **launcher** — the **Account** tab (sign in / create account / sign out);
* the **engine** — a process-wide `okay::Account` plus `account_*` OkayScript
  builtins, so a game can sign players in from script.

It has two backends, chosen automatically at runtime:

| Backend | When | Where credentials live |
| --- | --- | --- |
| **Local** | no server configured (default) | a salted+hashed file on the device (`accounts.db`) |
| **Online** | a server URL is configured | your auth server; the client stores only the session token |

The local backend works out of the box and offline; the online backend is what
you use to connect to a real server.

## Connecting to a server

You point the client at a server in one of three ways. The first one found wins.

1. **Environment variable** (engine and launcher):

   ```bash
   export OKAY_ACCOUNT_SERVER=https://accounts.mygame.com
   ```

2. **A file next to the launcher** — `account_server.txt`, one line, the base
   URL. Handy for shipping a configured launcher:

   ```
   https://accounts.mygame.com
   ```

3. **From engine code**, before the first use of the account system:

   ```cpp
   okay::Account::Configure("/path/to/config-dir", "https://accounts.mygame.com");
   ```

   Passing an empty server URL (or not calling `Configure`) keeps the local
   backend; the per-user config directory is used by default.

Use **HTTPS** in production — the password is sent in the request body.

## The server contract

When a server URL is set, sign-in and registration are plain JSON over HTTPS:

```
POST <server>/register     {"username": "...", "password": "..."}
POST <server>/login        {"username": "...", "password": "..."}
```

The server must respond:

* **Success** — HTTP `200` with a JSON body containing a session token:

  ```json
  {"token": "<opaque session token>"}
  ```

* **Failure** — any non-2xx status with a JSON `error` the client shows to the
  player:

  ```json
  {"error": "Invalid username or password."}
  ```

The client stores the returned token (not the password) and stays signed in
across launches until the player signs out.

### Authenticated requests and session validation

Once signed in, the client can make **authenticated requests** to your server:
it attaches the session token as an `Authorization: Bearer <token>` header (sent
via a curl config file, so the token never appears in the process list). Use
this to build server features on top of accounts — cloud saves, profiles,
leaderboards, and so on. Your server returns `401`/`403` for an
invalid/expired token.

One endpoint is special:

```
GET <server>/verify        (Authorization: Bearer <token>)
```

* valid token → `200` (body optional, e.g. `{"username": "..."}`)
* invalid/expired/revoked → `401` or `403`

The launcher calls this on startup (and games can call it via
`account_verify()` / `okay::Account::VerifySession()`): a definitive rejection
signs the player out, while a network error leaves the session intact so being
offline doesn't log players out.

## Try it locally

A tiny reference server (Python standard library only) lives at
[`examples/account-server/server.py`](../examples/account-server/server.py). It
implements the contract above so you can exercise the online backend end to end:

```bash
python3 examples/account-server/server.py        # listens on :8080

export OKAY_ACCOUNT_SERVER=http://localhost:8080
./build/bin/OkaySpace                             # Account tab is now "online"
```

It stores accounts in a local `accounts.json` and issues random tokens — fine
for development, not for production. Put a real database, TLS, rate limiting,
and token validation in front of it before shipping.

## Cloud saves

Built on authenticated requests, cloud saves give each signed-in player
per-account key/value storage on the server, so progress follows them across
devices. A "key" names a save slot (`"save1"`, `"settings"`); the data is any
text you choose (your serialized save). It needs the online backend and a
signed-in player; on the local backend these no-op (`save`/`delete` return
false, `load` returns `""`, `list` is empty).

```javascript
function save_game() {
  cloud_save("slot1", "level=3;coins=120");
}
function load_game() {
  if (cloud_has("slot1")) { progress = cloud_load("slot1"); }
}
```

The reference server stores slots per account; sign in on another machine and
`cloud_load` returns the same data. The wire protocol (see
`okay/Platform/Account/AccountService.hpp`) is:

```
POST   <server>/cloud/<key>   {"data": "..."}   -> 200
GET    <server>/cloud/<key>                       -> 200 {"data": "..."} | 404
DELETE <server>/cloud/<key>                       -> 200
GET    <server>/cloud                             -> 200 {"keys": [...]}
```

Save-slot keys are limited to letters, digits, `_`, `-`, `.` (they live in a
URL path). From C++ the same is available as `okay::Account::CloudSave/
CloudLoad/CloudHas/CloudDelete/CloudList`.

## Leaderboards

Also built on authenticated requests: global high-score tables keyed by a board
name. Submitting keeps each player's best score. Like cloud saves, leaderboards
need the online backend and a signed-in player; on the local backend
`leaderboard_submit` returns false and `leaderboard_top` is empty.

```javascript
function game_over(score) {
  leaderboard_submit("high", score);
  // Each entry is "rank,name,score" (same shape as steam_leaderboard_top).
  for (e in leaderboard_top("high", 10)) { print(e); }
}
```

Wire protocol (see `okay/Platform/Account/AccountService.hpp`):

```
POST <server>/leaderboard/<board>          {"score": N}   -> 200
GET  <server>/leaderboard/<board>?count=N                 -> 200
     {"entries": ["1,alice,500", "2,bob,300", ...]}
```

From C++: `okay::Account::LeaderboardSubmit(board, score)` and
`okay::Account::LeaderboardTop(board, count)` (returns `account::ScoreEntry`
rows with `name`, `score`, `rank`).

## Using accounts from a game (OkayScript)

The engine exposes these builtins, backed by the shared `okay::Account` service:

| Builtin | Returns | Notes |
| --- | --- | --- |
| `account_register(user, pass)` | `1` / `0` | create account + sign in |
| `account_login(user, pass)` | `1` / `0` | sign in |
| `account_logout()` | — | sign out, forget saved session |
| `account_is_logged_in()` | `1` / `0` | |
| `account_is_online()` | `1` / `0` | whether a server is configured |
| `account_username()` | string | empty when signed out |
| `account_token()` | string | current session token |
| `account_error()` | string | reason for the last failed register/login |
| `account_verify()` | `1` / `0` | re-check the session with the server (signs out if rejected) |
| `account_get(path)` | string | authenticated GET; response body, or `""` if not 2xx |
| `account_post(path, json)` | string | authenticated POST; response body, or `""` if not 2xx |
| `cloud_save(key, data)` | `1` / `0` | store a save slot on the server |
| `cloud_load(key)` | string | read a save slot (`""` if missing / offline) |
| `cloud_has(key)` | `1` / `0` | whether a save slot exists |
| `cloud_delete(key)` | `1` / `0` | delete a save slot |
| `cloud_list()` | array | the player's save slot names |
| `leaderboard_submit(board, score)` | `1` / `0` | submit a score (server keeps the best) |
| `leaderboard_top(board, count)` | array | top entries as `"rank,name,score"` strings |

```javascript
function start() {
  if (!account_is_logged_in()) {
    if (!account_login("player1", "s3cret!")) {
      print("Sign-in failed: " + account_error());
    }
  }
}
```

From C++ the same thing is available through `okay::Account` (see
`engine/include/okay/Platform/Account/Account.hpp`):

```cpp
auto r = okay::Account::Login("player1", "s3cret!");
if (!r.ok) std::cerr << okay::Account::LastError() << "\n";

// Validate a resumed session on launch, then call a protected endpoint.
if (okay::Account::VerifySession()) {
    auto resp = okay::Account::Api("/profile");          // GET by default
    if (resp.ok) std::cout << resp.body << "\n";
    // POST JSON: okay::Account::Api("/save", "POST", "{\"slot\":1}");
}
```
