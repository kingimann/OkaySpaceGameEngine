# OkayBackend: one backend layer, Supabase today, our own backend beside it

> **What this is:** the design of OkayBackend, the layer every program will use to reach online services, and the plan to
> build it in rounds. Owner request, 2026-11-26: "Create a centralized backend abstraction layer called OkayBackend. All our
> applications should communicate through OkayBackend rather than directly with Supabase-specific APIs", keep Supabase as
> production, no extra hosting costs, audit first. The audit is [okay_backend_audit.md](okay_backend_audit.md).
>
> **Status:** Design only (round 1). No program code exists for it yet. Every "will" below is a plan; nothing here is built.
> Owner decisions that change the plan are in section 13, each with the default this plan uses until the owner says otherwise.
>
> **Last checked:** 2026-11-26 (branch `claude/okay-backend-1`).

## Contents

- [1. The short version](#1-the-short-version)
- [2. Names and homes](#2-names-and-homes)
- [3. How the layers fit](#3-how-the-layers-fit)
- [4. The C++ interfaces](#4-the-c-interfaces)
- [5. Providers and configuration](#5-providers-and-configuration)
- [6. Moving each existing integration](#6-moving-each-existing-integration)
- [7. The Local backend](#7-the-local-backend)
- [8. Migration tools](#8-migration-tools)
- [9. Costs](#9-costs)
- [10. Testing](#10-testing)
- [11. Where the code comes from, licences and IP](#11-where-the-code-comes-from-licences-and-ip)
- [12. Risks](#12-risks)
- [13. Owner decisions](#13-owner-decisions)
- [14. Implementation rounds](#14-implementation-rounds)

## 1. The short version

- **Today** every program already sends all its Supabase traffic through one class, `AccountService`, but the 330 call sites
  write Supabase's routes by hand (audit section 1). So the door is already single; what goes through it is Supabase-shaped.
- **OkayBackend** is a family of C++ interfaces (`Auth`, `Database` with RPC, `Storage`, `Functions`, `Realtime`) that the
  programs call instead of writing routes. A **provider** turns those calls into requests. Two providers:
  - **Supabase**: production, the default, sends exactly the requests the programs send today.
  - **Local**: our own backend, written by us. It runs inside the program (for tests and offline use) or as a small server
    program on a developer's machine, on 127.0.0.1 only. It is never hosted unless the owner decides to host it.
- The provider is picked by **configuration** (one environment variable or one file), never by editing the programs.
- **It grows on what exists** (rule 19d): `AccountService` becomes OkayBackend's first user and keeps its public functions;
  its on-device "Local" accounts become the in-program mode of the Local backend; the Python reference server and the
  `Custom` provider are retired once Local does their job; `INetBackend` stays the game-facing facade on top.
- **Supabase stays.** Releases, the updater and every player's data stay on it. Moving a call site to OkayBackend must send
  the same bytes to Supabase as before, checked by a test that records both.
- **No new costs.** Nothing new is hosted. The Local backend runs on the developer's computer or inside a test.

## 2. Names and homes

Rule 19e: one system, one name, one home. Registered in the feature map (17.19).

| Thing | Name | Home (all planned) |
|---|---|---|
| The system | **OkayBackend** | `engine/include/okay/Backend/`, `engine/src/Backend/`, `backend/` |
| Entry header | `okay/Backend/Backend.hpp` | `engine/include/okay/Backend/Backend.hpp` |
| C++ namespace | `okay::backend` | |
| The two providers | `supabase`, `local` | `engine/src/Backend/Supabase*.cpp`, `engine/src/Backend/Local*.cpp` |
| The Local server program | `okay-backend` (`OkayBackend.exe` on Windows) | `backend/main.cpp` (like `relay/`) |
| The migration commands | `okay backend export`, `okay backend import` | `tools/okay-cli/` (the existing `okay` command) |
| Old names kept as aliases | `AccountService`, `okay::Account`, `OKAY_ACCOUNT_SERVER`, `OKAY_ACCOUNT_API_KEY`, `account_server.txt` | frozen names stay working |

"Local" already means "on-device accounts" inside `AccountService`. The design keeps one meaning: the Local backend, which
can run in the program (that is what on-device accounts become) or as `okay-backend`. There is no second "local" thing.

## 3. How the layers fit

```
 launcher / editor / player / game scripts
        |                     |
  community headers      okay::Account, INetBackend (game-facing, unchanged API)
  (Messages, Forums...)       |
        +----------+----------+
                   |
           okay::backend::Backend  (Auth, Database + RPC, Storage, Functions, Realtime)
                   |
        provider: supabase | local
                   |
           Transport: HTTP (okay::http::Send) | in-process (Local's router, no sockets) | recorder (tests)
                   |
      Supabase project (production)   or   okay-backend on 127.0.0.1 / inside the program
```

- **Transport** is a small interface: send one request, get one response. Today's `AccountService::HttpRequest` and
  `SetTransportForTest` become its HTTP and recorder forms; the in-process form calls the Local backend's request router
  directly, so tests and offline use need no network and no sockets.
- **The same request format both ways.** The Local backend answers the documented route shapes the programs already use
  (a subset of the PostgREST query syntax, the GoTrue auth routes, the Storage object routes, `/functions/v1/<name>`). So one
  request builder serves both providers, the reset page and the scripts work against Local unchanged, and the contract tests
  can compare answers request by request. The routes are public, documented interfaces; we copy their behaviour, never
  Supabase's code (section 11). Owner decision 13.5 can change this later without touching the programs, because the
  programs only see the C++ interfaces.
- One useful side effect: because Local answers the same routes, today's launcher can already be pointed at a running
  `okay-backend` with the two existing settings (`OKAY_ACCOUNT_SERVER=http://127.0.0.1:45300`, any `OKAY_ACCOUNT_API_KEY`),
  before any call site is moved.

## 4. The C++ interfaces

Sketch (C++17, header-only where it is small; no `<mutex>` or `<thread>` in anything the launcher, editor or player can
include, rule 21a). Final names are settled in round 2; the shape is what matters.

```cpp
namespace okay::backend {

enum class Key { None, Anon, User, Service };          // which credential a request carries
struct Request  { std::string method, path, body, contentType, bodyFile; std::vector<std::string> headers; Key key = Key::User; int timeoutSec = 0; };
struct Response { bool ok = false, reached = false, maybeSent = false; long status = 0; std::string body, error; };  // today's ApiResponse

class Transport { public: virtual ~Transport() = default; virtual Response Send(const Request&) = 0; };

template <class T> struct Result { bool ok = false; std::string error; T value; };   // like today's account::Result
struct Session { std::string userId, email, username, accessToken, refreshToken; long expiresAt = 0; };

class Auth {                                            // GoTrue-shaped on both providers
public:
    virtual Result<Session> SignUp(const std::string& email, const std::string& password, const std::string& username) = 0;
    virtual Result<Session> SignIn(const std::string& emailOrUsername, const std::string& password) = 0;
    virtual Result<Session> Refresh(const std::string& refreshToken) = 0;
    virtual Response User() = 0;                         // who am I (also checks a token)
    virtual Response UpdateUser(const std::string& json) = 0;   // email, password, metadata
    virtual Response Recover(const std::string& email, const std::string& redirect) = 0;
    virtual Response VerifyRecovery(const std::string& email, const std::string& code) = 0;
    virtual Response Resend(const std::string& email) = 0;
    virtual bool VerifyToken(const std::string& token, std::string& outUserId) = 0;
};

class Query {                                           // builds the PostgREST query text; one builder for both providers
public:
    Query& Select(std::string cols); Query& Eq(std::string col, std::string v); Query& Neq(...); Query& Lt(...); Query& Gt(...);
    Query& In(std::string col, std::vector<std::string> vs); Query& Is(std::string col, std::string v); Query& Ilike(...);
    Query& Or(std::string expr); Query& Order(std::string col, bool desc = false); Query& Limit(int n); Query& Offset(int n);
    Query& Prefer(std::string p);                       // return=minimal, resolution=merge-duplicates, count=exact
    Response Get(); Response Insert(const std::string& json, std::string onConflict = {}); Response Update(const std::string& json); Response Delete();
    std::string PathForTest() const;                    // the exact text sent, for golden tests
};
class Database { public: virtual Query From(const std::string& table) = 0; virtual Response Rpc(const std::string& fn, const std::string& json) = 0; };

class Storage {
public:
    virtual Response Upload(const std::string& bucket, const std::string& path, const std::string& localFile, const std::string& type, bool upsert) = 0;
    virtual Response Download(const std::string& bucket, const std::string& path, const std::string& toFile) = 0;
    virtual Response List(const std::string& bucket, const std::string& prefix, int limit, int offset) = 0;
    virtual Response Remove(const std::string& bucket, const std::vector<std::string>& paths) = 0;
    virtual std::string PublicUrl(const std::string& bucket, const std::string& path) const = 0;
};
class Functions { public: virtual Response Invoke(const std::string& name, const std::string& json, int timeoutSec = 0) = 0; };

class Realtime {                                        // section 7.6: polling first, a push channel later
public:
    using Handler = std::function<void(const std::string& table, const std::string& rowJson)>;
    virtual int  Subscribe(const std::string& table, const std::string& filter, Handler h) = 0;
    virtual void Unsubscribe(int id) = 0;
    virtual void Pump() = 0;                            // called once a frame by the program; never blocks
};

struct Capabilities { bool emailConfirm, passwordResetEmail, realtimePush, functions; };

class Backend {                                         // one per process, like okay::Account
public:
    virtual Auth& auth() = 0; virtual Database& db() = 0; virtual Storage& storage() = 0;
    virtual Functions& functions() = 0; virtual Realtime& realtime() = 0;
    virtual const char* ProviderName() const = 0;       // "supabase" | "local"
    virtual Capabilities Caps() const = 0;
};
Backend& Get();                                         // built from configuration on first use
}
```

Rules the interfaces keep, because the programs rely on them today:

- Never send a request twice when it may have arrived (`maybeSent`); a 401 re-reads the saved token and retries once;
  only the launcher refreshes tokens (Supabase rotates refresh tokens).
- `Response` is today's `ApiResponse`, so moved code changes as little as possible.
- A missing table answers the same "not set up" codes on both providers, so the "not set up" screens keep working.

## 5. Providers and configuration

| Setting | Where | Values | Default |
|---|---|---|---|
| `OKAY_BACKEND` | environment variable, or `"provider"` in `backend.json` | `supabase`, `local`, `auto` | `auto` |
| Backend address | `OKAY_ACCOUNT_SERVER`, `account_server.txt`, or `"url"` in `backend.json` (existing names kept) | an `http(s)` address | the built-in Supabase address (`OKAY_DEFAULT_ACCOUNT_URL`) |
| Publishable key | `OKAY_ACCOUNT_API_KEY`, `account_apikey.txt`, or `"key"` in `backend.json` | text | the built-in key (`OKAY_DEFAULT_ACCOUNT_KEY`) |
| Local data folder | `OKAY_BACKEND_DATA`, or `"data"` in `backend.json` | a folder | `<config folder>/backend-local/` |
| Local server port | `OKAY_BACKEND_PORT`, or `okay-backend --port N` | 1-65535 | 45300, on 127.0.0.1 only |

- `backend.json` sits in the config folder next to `session.txt` (`%APPDATA%\OkaySpace` on Windows). Environment variables
  win over the file; the file wins over the built-in values. The two old `.txt` files keep working.
- `auto` keeps today's rule (`AccountService::Resolve`): a built-in or configured address with a key means `supabase`;
  no address means `local` in-process (today's on-device accounts). The old `Custom` provider goes away when Local replaces
  the reference server (round 10).
- **Production stays Supabase.** Release builds keep the Supabase address built in, so a player who changes nothing talks to
  Supabase exactly as now. `local` is in every build (rule 17: nothing compiled out), but a person has to choose it.
- Switching providers signs the person out (a token from one backend means nothing to the other). The launcher says so.
- **Secrets never go in `backend.json`.** The service role key is only ever an environment variable on the owner's machine
  or a GitHub secret (rule 15). The Local backend has no service key that leaves the machine: its admin key is generated in
  its data folder and only works on 127.0.0.1.

## 6. Moving each existing integration

Order: smallest risk first. Each move is part of one round's PR (rule 9: fewer, bigger PRs), and each moved file gets a
**golden request test**: the old code path and the new one run through the recorder transport and must send byte-for-byte
the same method, path, headers and body to the Supabase provider.

| Integration (audit section 3) | Moves to | Round | Why this order |
|---|---|---|---|
| `AccountService` transport (`Api`, `ApiAnon`, `ApiUpload`, `ApiTimed`, `HttpRequest`) | `Transport` + `Backend` underneath; public API unchanged | 2 | Everything else already goes through it; changing what is underneath moves all 330 sites to the new transport at once with no route change |
| Auth in `AccountService` (sign-up, sign-in, refresh, recover, verify, resend, update user, username check) | `Auth` | 2 | One file, well tested (`account`, `editor_signin_gate`) |
| Cloud saves, leaderboards (`AccountService`) | `Database` | 3 | Used by games; two tables |
| `Matchmaking.hpp`, `MatchResults.hpp`, `Matchmaker.cpp` | `Database`, `Rpc` | 3 | Engine side (editor, player), tested |
| `TicketClient` / `AntiCheatBackend` session ticket, `AccountData` delete | `Functions` | 4 | Already request structs; tested |
| `AntiCheatBackend` tables, `UserReports`, `CrashUpload`, `MemberModeration`, `AntiCheatAdminTools` | `Database`, `Rpc` | 4 | Already request structs; tested |
| Chat attachments (`Messages.hpp` Storage part), workshop uploads (`Workshop.hpp`), official game publish (`launcher/main.cpp`) | `Storage` | 4 | Few sites; public links must stay the same text |
| Updater (`ReleasesBucketBase`, `editor/Updater.cpp`) | `Storage::PublicUrl` of the Supabase provider only | 4 | The address is built into installed launchers; it never follows a provider switch (section 12) |
| `Profiles`, `Friends`, `BlockList`, `Privacy`, `Invites`, `Parties` | `Database`, `Rpc` | 5 | Medium size; add the missing request tests for Profiles first |
| `Messages`, `Servers` | `Database`, `Rpc` | 6 | Biggest file (55 sites) |
| `Forums`, `Admin`, `Workshop` tables, launcher leftovers | `Database`, `Rpc` | 7 | Admin has no test today: write it first |
| Publish scripts, `crash_to_issue.py`, CI checks | `okay backend storage ...` commands (round 15) or left as they are | 15 | Release path: last, and only if it stays as fast (rule 11a) |
| Edge Functions, reset page | stay on Supabase; Local gets its own C++ versions (round 11) | 11 | They are server code, not clients |

After round 7 a check fails any new Supabase route outside `engine/src/Backend/` (a `--check` mode of the backend scan, run by
`branch-lint`), so no call site goes back to writing routes by hand.

## 7. The Local backend

All of it is our own code (rule 19a), written from the HTTP RFCs, the published route documentation of the services we
answer like, and our own SQL in `docs/sql/` (which is the specification of the rules). Cryptography only through libsodium
(rule 18).

### 7.1 Built from existing pieces

| Need | Existing piece | Work |
|---|---|---|
| HTTP server | `editor/WebRun.cpp` `PreviewServer` (our own, RFC 9112, non-blocking, 127.0.0.1) | Move its socket and request-head code into the Platform Layer (`engine/include/okay/Platform/HttpServer.hpp`, planned) and extend it: all methods, request bodies with a size cap, several open connections, header limits; `PreviewServer` then uses the moved code (no second copy) |
| Request routing and JSON | `okay::json` (`Core/Json.hpp`), `AccountService::detail::JsonEscape` | A small router: route table, path parameters |
| Password hashing | libsodium `crypto_pwhash_str` (Argon2id) through `NetCrypto.hpp` | Add the wrapper; existing PBKDF2 records from on-device accounts are checked once and re-hashed with Argon2id on that sign-in |
| Access tokens | libsodium `crypto_sign` (Ed25519), already wrapped in `NetCrypto.hpp` | JSON web tokens with `alg` EdDSA and the claims the programs read (`sub`, `exp`) plus `role`; refresh tokens from `randombytes_buf`, stored only as a `crypto_generichash` hash, rotated on every use, a reused one ends the session |
| Session ticket | the ticket format in `shared/TicketClient.hpp` and `supabase/functions/session-ticket/ticket.ts` (ours) | A C++ handler that signs with libsodium Ed25519 |
| Rate limits, lock-out, body caps, token expiry | `examples/account-server/server.py` (ours) | Same behaviour in C++, then the Python server is deleted |
| Random ids | `AccountService::detail::OsRandom` | UUID v4 from it |
| Path safety for files | `webrun::MapTarget` | Reused for Storage paths |

### 7.2 Auth

Accounts (id, email, username, Argon2id hash, created, confirmed, metadata) in the store. Routes: sign-up, password sign-in,
refresh, user, update user, recover, verify, resend, admin delete. **Email:** Local sends no mail (no mail server, no cost);
the confirmation and reset codes are written to the `okay-backend` window and its log on the developer's machine, and a
sign-up is confirmed at once unless `--confirm-email` is given.

### 7.3 Database

- **Store:** our own. Tables are rows of JSON values with a primary key, kept in memory and saved as an append-only journal
  (one line per change, flushed per request) plus a snapshot written on clean exit and every N changes. One process owns the
  data folder (a lock file says so).
- **Queries:** a parser for the PostgREST subset the programs use (select with columns and simple embeds, `eq`, `neq`, `lt`,
  `lte`, `gt`, `gte`, `in`, `is`, `like`, `ilike`, `or`, `order`, `limit`, `offset`, `on_conflict`, the `Prefer` options,
  `count=exact`). The list is fixed by the golden tests from rounds 2-7: anything a program sends must parse.
- **Rules:** each table's row-level policies are written as C++ rules (who may select, insert, update, delete which rows),
  translated by hand from `docs/sql/`. Each RPC is a C++ function; each trigger is a hook. A "service" caller skips the rules,
  like `service_role`.
- **One place for the schema:** `cloud_saves` and `leaderboards` move from a doc into `docs/sql/` (round 3) so every table has
  one definition.
- **Jobs:** the two nightly `pg_cron` jobs run when the server starts and every 24 hours.
- **How we know the rules match:** section 10.3.

This is the largest part of the work: 52 tables, 189 policies, 117 functions, 44 triggers. It is done in the order the
programs need it (games first, community later), and a table that Local does not have yet answers "not set up", which the
programs already handle. Owner decision 13.1 offers a faster way.

### 7.4 Storage

Files in `<data>/storage/<bucket>/<path>`, with the bucket list (`releases`, `chat`, `workshop`, `game-content`), a public flag
per bucket, size caps, and the rules from `chat_media.sql`, `social.sql` and `official_game_admin.sql` (your own id folder;
admins for `releases/official/`). Routes: upload (with upsert), download, public download, list, remove, make bucket.

### 7.5 Functions

The two functions become C++ handlers inside `okay-backend` with the same contract: `delete-account` (the step list in
`delete_account.ts` moves to one data file that both the TypeScript and the C++ read, so the two lists cannot drift) and
`session-ticket`. A function Local does not have answers 404, which the programs show as "not deployed" today.

### 7.6 Realtime

Nothing uses Realtime today; the programs poll. Plan: round 16 adds the `Realtime` interface with a polling form for both
providers (the same requests the community headers send now, moved behind `Subscribe` and `Pump`). A push channel needs a
decision first (13.2): a WebSocket handshake (RFC 6455) uses SHA-1, which libsodium does not have, so Local would use
Server-Sent Events (plain HTTP, no hashing) and Supabase Realtime would stay unused.

### 7.7 Where it runs

- **Inside a program:** the Local backend's router is called through the in-process transport, with the data folder in the
  config folder. This is what on-device accounts become, and what the tests use. Single-threaded: the program calls it.
- **As `okay-backend`:** a small console program like the relay, listening on 127.0.0.1:45300 only. Binding any other address
  needs `--listen <address> --i-know-this-is-not-hosted-safely` and prints a warning: hosting it is an owner decision (13.7).
  No TLS (it never leaves the machine), so no crypto of our own is needed for transport.

## 8. Migration tools

Commands of the existing `okay` tool (`tools/okay-cli/`), all run on the owner's or a developer's own machine:

```
okay backend export --from supabase --out <folder> [--tables a,b] [--dry-run]
okay backend export --from local    --out <folder>
okay backend import --to local      --in <folder>
okay backend import --to supabase   --in <folder>      (only into a project the owner names; asks to confirm)
okay backend verify --in <folder> --against <provider> (counts and checksums)
```

- **What is read from Supabase, only through documented APIs:** accounts from the Auth admin API (`GET /auth/v1/admin/users`,
  paged): id, email, username metadata, created and confirmed times. Every table through PostgREST with the service key,
  paged in primary-key order. Every Storage object through list and download. The service key comes from an environment
  variable on the owner's machine (rule 15), never from a file in the repository.
- **The export folder:** one JSON-lines file per table, the files, a `manifest.json` with counts and a SHA-256 per file
  (libsodium `crypto_hash_sha256`), and the export time. Ids stay the same, so links between tables survive.
- **Import** checks the manifest, then writes in dependency order (accounts, then tables, then files), and can resume.
- **Passwords cannot be exported:** Supabase's public APIs never return password hashes. Sign-in moves this way:
  1. While both run, Local marks every imported account "password not here yet". The first time a person signs in to Local,
     Local checks the password once against Supabase's documented sign-in route; if Supabase accepts it, Local stores its own
     Argon2id hash and never asks Supabase again for that account.
  2. Anyone who never signs in during that time uses "Forgot password" (Local's reset).
  3. Going back to Supabase works the same way: imported accounts need a password reset, unless the owner decides otherwise.
  The plan does not read hashes from the Supabase database directly: that is not a public API, and checking those hashes
  would need a hash function libsodium does not provide (rule 18).
- **Cost and care:** an export reads every row and file once, which counts toward the Supabase project's bandwidth quota, so
  `--dry-run` first prints counts and sizes. An export holds people's emails and messages: it stays on the owner's machine, is
  never committed, never uploaded as a CI artifact, and the folder gets a `README` saying so (13.6).

## 9. Costs

| | Runs where | Cost |
|---|---|---|
| Supabase (database, auth, storage, functions) | the owner's existing project | unchanged; OkayBackend sends the same requests |
| GitHub Actions | GitHub's runners | the contract tests against Local add a few minutes to the full run on main and the nightly run (not to PR checks, rule 6) |
| `okay-backend` | a developer's own computer, or inside a test | nothing |
| Migration tools | the owner's own computer | nothing; Supabase bandwidth only when an export is run |
| Contract tests against Supabase | a separate Supabase test project, only if the owner makes one (13.3) | the owner's choice; skipped otherwise |

Nothing in this plan rents a server, a database or storage. Hosting the Local backend anywhere is not in the plan.

## 10. Testing

### 10.1 Golden request tests (rounds 2-7)

For every moved file: record the requests the old code sends (today's `SetTransportForTest`) and the new code sends (the
recorder transport), for the same calls, and require them to be identical. They are the proof that moving to OkayBackend
changes nothing for Supabase. They run in the normal test build.

### 10.2 Contract tests (round 9 on)

One suite, `tests/test_okay_backend_contract.cpp`, that every provider must pass: sign-up, sign-in, wrong password, refresh
and reuse of an old refresh token, cloud save round trip, leaderboard keeps the best score, one player cannot read or change
another's rows, storage folder rules, each RPC the programs call, delete account removes everything, a missing table answers
"not set up".

- Against **Local in-process**: in every test build and CI full run (no network, no sockets).
- Against **`okay-backend` over 127.0.0.1**: in the full run, to cover the HTTP server.
- Against **Supabase**: only when `OKAY_BACKEND_TEST_URL`, `OKAY_BACKEND_TEST_KEY` and `OKAY_BACKEND_TEST_SERVICE_KEY` name a
  separate test project the owner made. Skipped otherwise, and it refuses the production address built into the programs.

### 10.3 Rules oracle

The rules in Local are a hand translation of `docs/sql/`, so they are checked against the SQL itself: when
`OKAY_SQL_TEST_PGHOST` points at a throw-away local Postgres (as the SQL tests already do, with `tests/sql/supabase_stub.sql`),
each rules scenario also runs as SQL with the same caller id, and the rows each side returns must match. The existing
`tests/sql/test_*.sql` checks are ported into this form table by table.

### 10.4 Hostile input and the rest

The HTTP server and the query parser get the same treatment as our HTTP client (`tests/test_own_http.cpp`): truncated and
huge input, bad framing, deep nesting, fuzzing under ASan and UBSan. Migration gets a round-trip test (Local, export, import
into a fresh Local, compare). Every option-on case is tested with the option on (bug class 5 of the brief).

## 11. Where the code comes from, licences and IP

Owner rule, 2026-11-26: implement from public standards and specifications, do not copy source code, name the source and
licence of anything third-party, flag possible IP conflicts.

- **Our code is written from:** RFC 9110 and 9112 (HTTP), RFC 3986 (URLs), RFC 8259 (JSON), RFC 7519 (JSON web tokens),
  RFC 8037 (EdDSA in JOSE), RFC 4122 (UUIDs), RFC 6455 or the WHATWG Server-Sent Events text (realtime, if chosen), the
  publicly documented HTTP routes of PostgREST, Supabase Auth (GoTrue), Supabase Storage and Edge Functions (behaviour only),
  and our own SQL in `docs/sql/`. Each new file says which.
- **Cryptography** only through libsodium (ISC licence), as today (rule 18).
- **Supabase's own software is not used and not copied.** For the record, its open-source parts are published under
  permissive licences: PostgREST (MIT), Supabase Auth / GoTrue (MIT), Supabase Storage (Apache-2.0), Supabase Realtime
  (Apache-2.0), the Supabase CLI and dashboard (Apache-2.0), PostgreSQL (the PostgreSQL Licence). We read none of their
  source for this work. (Licence names from memory of their published repositories; not re-checked on 2026-11-26.)
- **Possible IP points to flag:**
  1. Answering the same routes as PostgREST and GoTrue copies an interface, not code. Public API shapes are widely
     re-implemented; this design treats it as fine, as the coordinator's brief does. Owner may prefer our own route shapes
     (13.5).
  2. "Supabase" is a trademark. OkayBackend's code and docs name it only to say what it talks to; the Local backend is never
     presented as Supabase or a Supabase product.
  3. The name "OkayBackend" was chosen by the owner. No trademark search was done (unverified).
  4. The audit found no code copied from Supabase in the repository (the SQL, functions and reset page are ours), but a text
     scan cannot prove that (`scripts/provenance_scan.py` says the same).

## 12. Risks

| Risk | What protects against it |
|---|---|
| Installed launchers have the Supabase releases address built in | The updater address never follows a provider switch; releases stay on Supabase; `ReleasesBucketBase` keeps its exact output (golden test) |
| A moved call sends a slightly different request and breaks a feature for every player | Golden request tests for every moved file; moves land in the full run on main before a release (rule 6) |
| Old launchers and editors keep calling Supabase routes forever | Supabase stays production; routes, tables and functions keep their names |
| Local's rules differ from the SQL and leak data on a developer machine | Rules oracle (10.3); Local holds test data unless a migration import puts real data there (13.6) |
| Real people's data on a developer machine after an export | Exports stay on the owner's machine, never in git or CI; owner decides who may run them |
| Token refresh races sign people out | Only the launcher refreshes (as today); the contract tests cover reuse of an old refresh token |
| Argon2id makes `okay-backend` slow while it hashes (single-threaded) | libsodium's "interactive" limits; it is a developer tool |
| The Local database is a big piece of work (section 7.3) | Built in the order the programs need it; "not set up" answers until then; owner decision 13.1 |
| Windows build rules (no `<mutex>` / `<thread>` in reachable headers) | Single-threaded server loop like `PreviewServer`; `debugutil::SpinLock` where needed |
| The service role key in CI can read and change the whole database | Owner decision 13.6 (a narrower key or signed uploads); not changed by OkayBackend |

## 13. Owner decisions

Each has the default this plan uses until the owner decides.

1. **How Local stores data and applies the rules.** (a) Our own store with the rules translated into C++ (all ours, the big
   job in 7.3). (b) Run the existing `docs/sql/` in a PostgreSQL the developer installs, with our own HTTP front in front of
   it (exact rules at once, but our backend then depends on PostgreSQL as a tool, the way the SQL tests do today).
   **Default: (a)**, because of rules 17 and 19a; (b) only if the owner wants full parity quickly.
2. **Realtime push.** A WebSocket needs SHA-1 for its handshake (not used for security, but libsodium has none). **Default:
   no WebSockets; polling, plus Server-Sent Events on Local.** The owner can allow SHA-1 for the handshake only.
3. **A Supabase test project** for the contract tests. **Default: none; the Supabase leg of the contract tests is skipped.**
4. **Moving sign-ins between backends.** **Default: check the password against the old backend on first sign-in, then
   "Forgot password" for the rest** (section 8).
5. **Route shapes for Local.** **Default: answer the documented PostgREST / GoTrue / Storage route shapes the programs use**,
   so one request builder serves both and today's programs can talk to Local unchanged. The alternative is our own routes
   behind the same C++ interfaces.
6. **Security clean-ups found by the audit:** replace the real publishable key in `tests/test_crash_scrub.cpp:79` with a
   made-up one (**default: yes, in round 2**); confirm the `sb_secret_` test values are not real (owner check, audit 9.3); use
   a narrower credential than the service role key for release uploads (**default: unchanged until the owner decides**); who
   may run a data export (**default: only the owner**).
7. **Hosting the Local backend anywhere.** **Default: never; 127.0.0.1 only.**
8. **Retiring the Python reference server** (`examples/account-server/`) and the `Custom` provider once Local covers their
   routes. **Default: yes, in round 10** (rule 19d: no second copy).

## 14. Implementation rounds

Each round is one PR from `claude/ready` (rule 9), precheck clean before the push (rules 6 and 7a). "Exists" and "Gap" follow
the claim format of rule 26.

1. **Audit and design (this round).** Exists: `AccountService`, `INetBackend`, the HTTP seam, the reference server, SQL tests.
   Gap: no inventory, no design. Files: `docs/platform/okay_backend_audit.md`, `docs/platform/okay_backend.md`, the feature map
   entry 17.19, a roadmap row, `scripts/provenance_scan.py backend`. Tests: none (docs and a read-only scan).
2. **Core and Auth.** Exists: `AccountService::HttpRequest`, `SetTransportForTest`, `ApiResponse`. Gap: no `okay::backend`
   types. Files: `engine/include/okay/Backend/Backend.hpp`, `Transport.hpp`, `Auth.hpp`, `engine/src/Backend/Supabase*.cpp`,
   `AccountService.hpp` (delegates; public API unchanged), `tests/test_crash_scrub.cpp` (made-up key). Tests:
   `okay_backend_golden_auth` (every auth flow: same requests before and after), `account`, `editor_signin_gate` unchanged.
3. **Database builder and the game-facing tables.** Exists: hand-written PostgREST strings. Gap: no builder. Files:
   `Backend/Database.hpp`, `Query`, `AccountService` cloud saves and leaderboards, `Matchmaking.hpp`, `MatchResults.hpp`,
   `Matchmaker.cpp`, `docs/sql/cloud_saves.sql` and `docs/sql/leaderboards.sql` (moved from the doc). Tests:
   `okay_backend_query` (every operator, encoding, hostile values), golden tests for each moved file, `matchmaking`,
   `match_results`, `matchmaker`.
4. **Storage and Functions, and the request-struct files.** Exists: `ApiUpload`, the request structs in `shared/`. Gap: no
   Storage or Functions interface. Files: `Backend/Storage.hpp`, `Backend/Functions.hpp`, `Messages.hpp` (storage part),
   `Workshop.hpp` (storage part), `launcher/main.cpp` (official publish), `AccountData.hpp`, `AntiCheatBackend.hpp`,
   `TicketClient.hpp`, `UserReports.hpp`, `CrashUpload.hpp`, `MemberModeration.hpp`, `AntiCheatAdminTools.hpp`,
   `VersionsManager.cpp` / `editor/Updater.cpp` (public URL only). Tests: golden tests per file, plus the existing
   `anticheat_backend`, `ticket_client`, `account_data`, `crash_upload`, `member_moderation`, `user_reports`, `launcher_versions`.
5. **Community, part 1.** Files: `Profiles.hpp`, `Friends.hpp`, `BlockList.hpp`, `Privacy.hpp`, `Invites.hpp`, `Parties.hpp`.
   Gap: `Profiles` has no request test (write it first). Tests: golden tests per file.
6. **Community, part 2.** Files: `Messages.hpp` (tables), `Servers.hpp`. Tests: golden tests (55 + 19 sites).
7. **Community, part 3, and the lock.** Files: `Forums.hpp`, `Admin.hpp` (no test today: write it first), `Workshop.hpp`
   (tables), launcher leftovers; `scripts/provenance_scan.py backend --check` in `branch_lint.py`. Tests: golden tests; the
   check fails on a route outside `engine/src/Backend/`.
8. **HTTP server in the Platform Layer.** Exists: `webrun::PreviewServer`. Gap: GET/HEAD only, in `editor/`. Files:
   `engine/include/okay/Platform/HttpServer.hpp`, `engine/src/Platform/HttpServer.cpp`, `editor/WebRun.cpp` (uses it). Tests:
   `http_server` (framing, limits, hostile input, ASan fuzz), `web_run` unchanged.
9. **Local backend: store, Auth, in-process transport, `okay-backend`.** Exists: on-device accounts in `AccountService`, the
   reference server's behaviour, libsodium wrappers. Gap: no server-side auth of our own in C++. Files:
   `engine/src/Backend/Local*.cpp`, `Store`, `NetCrypto.hpp` (pwhash wrapper), `backend/main.cpp`, `backend/CMakeLists.txt`.
   Tests: `okay_backend_contract` (auth part) against Local in-process and over 127.0.0.1; PBKDF2 record upgrade.
10. **Local database for the game tables.** Files: Local query parser, rules and RPCs for `cloud_saves`, `leaderboards`,
    `game_sessions`, `profiles`, matchmaking and match tables. Delete `examples/account-server/` and the `Custom` provider
    (13.8). Tests: contract (data part), rules oracle for those tables, query parser fuzz.
11. **Local Storage and Functions.** Files: Local storage, `delete-account` and `session-ticket` in C++, the shared step list.
    Tests: contract (storage, functions), `delete_account_function` reads the same step list.
12. **Local community tables, part 1** (profiles extras, friends, follows, blocks, privacy, invites, parties). Tests: rules
    oracle ported from `tests/sql/test_blocks.sql`, `test_chat_invites.sql`, `test_public_names.sql`.
13. **Local community tables, part 2** (messages, groups, receipts, reactions, servers, channels). Tests: oracle from
    `test_chat_receipts.sql`, `test_reactions_privacy.sql`.
14. **Local community tables, part 3** (forums, workshop, admin, anti-cheat, reports, crash reports, member moderation,
    app config, nightly jobs). Tests: oracle from `test_anticheat.sql`, `test_reports.sql`, `test_member_moderation.sql`,
    `test_crash_reports.sql`.
15. **Migration tools.** Exists: `tools/okay-cli`. Gap: no export or import. Files: `tools/okay-cli/Backend*.cpp`. Tests:
    Local round trip; Supabase export only against the owner's test project (13.3); first-sign-in password move.
16. **Realtime.** Files: `Backend/Realtime.hpp`, polling for both providers, Server-Sent Events on Local (or WebSockets if 13.2
    allows). Tests: contract (realtime), chat typing and read marks through `Subscribe`.
17. **Scripts and CI on OkayBackend (optional).** Only if the release path stays as fast (rule 11a): publish uploads through
    `okay backend storage upload`. Releases stay on Supabase. Tests: the existing publish script tests.

After round 14 the owner can run every program against Local with one setting, and Supabase is optional rather than
required, while it stays the production backend for as long as the owner wants.
