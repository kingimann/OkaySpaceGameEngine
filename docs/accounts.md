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
across launches until the player signs out. It does not yet send the token back
to the server for validation — treat the token as a session handle to build on
(e.g. add an `Authorization: Bearer <token>` header where you need it).

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
```
