# OkayBackend Admin: audit and design of the administration dashboard

> **What this is:** the audit and the design for the OkayBackend administration dashboard. Owner request, 2026-11-26:
> "Create a standalone OkayBackend administration dashboard connected directly to our existing backend systems", to manage
> databases, authentication, users, storage, API endpoints, permissions, server functions, networking, logs, errors and
> external providers; show clearly what OkayBackend does natively and what still depends on Supabase; let providers be
> replaced with no change to the dashboard; responsive on desktop and mobile; secure admin sign-in, role-based permissions,
> audit logging; "Do not create placeholder functionality, simulated metrics or nonfunctional buttons."
>
> **Status:** Round A1 (audit and design) and round A2 (the SQL: roles, permissions, the extended audit log, the gate with
> rate limits, TOTP storage, and the storage and row rules for the game controls of section 18) are written. A2 is tested on a
> throw-away local Postgres (`admin_sql`, 152 checks) and has **not** been run on the live project: the owner runs the SQL
> (section 15). No dashboard, function or console exists yet. Every "will" is a plan.
> It is a part of OkayBackend (feature map 17.19.6), not a new system. The OkayBackend design is [okay_backend.md](okay_backend.md)
> and its audit [okay_backend_audit.md](okay_backend_audit.md).
>
> **Last checked:** 2026-11-26 (branch `claude/okay-backend-admin-1`, from main at `378f24f1e`). Read from the code only.
> Nothing was sent to the live Supabase project or to its management API. No key or token was read or printed.
> What this page says about Supabase's own APIs is **from memory of Supabase's public documentation**: supabase.com could not
> be reached from this session, so each such line is marked *(docs, not re-checked)* and is re-checked before round A3 codes it.

## Contents

- [1. The short version](#1-the-short-version)
- [2. Audit: what exists today](#2-audit-what-exists-today)
- [3. Audit: what each Supabase service lets an admin do](#3-audit-what-each-supabase-service-lets-an-admin-do)
- [4. Audit findings that shape the design](#4-audit-findings-that-shape-the-design)
- [5. Design: how the parts fit](#5-design-how-the-parts-fit)
- [6. Where the page is served, and what it costs](#6-where-the-page-is-served-and-what-it-costs)
- [7. The Admin API](#7-the-admin-api)
- [8. Native or Supabase: the provider view](#8-native-or-supabase-the-provider-view)
- [9. Admin sign-in and the second factor](#9-admin-sign-in-and-the-second-factor)
- [10. Roles and permissions](#10-roles-and-permissions)
- [11. Audit log and rate limits](#11-audit-log-and-rate-limits)
- [12. How the page is protected](#12-how-the-page-is-protected)
- [13. Each area: real operations and what is left out](#13-each-area-real-operations-and-what-is-left-out)
- [14. The launcher's Admin page: no second copy](#14-the-launchers-admin-page-no-second-copy)
- [15. Implementation rounds](#15-implementation-rounds)
- [16. Owner decisions](#16-owner-decisions)
- [17. Sources, licences and IP flags](#17-sources-licences-and-ip-flags)
- [18. Owner powers and game controls](#18-owner-powers-and-game-controls)

## 1. The short version

- **Much already exists.** The launcher has an Admin page (players, bans, roles, marketplace and forum review, reports,
  anti-cheat, the official game, the audit log). Its rules live in our SQL: roles as profile badges (admin > staff >
  moderator), `site_bans`, `admin_log`, `admin_stats`, `app_config`, reports, anti-cheat with an append-only `ac_audit`, and a
  rate-limit helper. The dashboard **extends these**; it does not make a second role table, a second log or a second ban list
  (rule 19d).
- **Three parts, all ours:**
  1. **The dashboard**: plain HTML, CSS and JavaScript written by us. No framework, UI kit, chart library, icon font or web
     font. Responsive from phone width to desktop.
  2. **The Admin API** (`/admin/v1/...`): our own documented contract. The dashboard talks only to it, never to a Supabase
     route, so swapping a provider changes the server side only.
  3. **Its servers**: first our own Supabase Edge Function `okay-admin` (TypeScript, ours), later the Local backend in C++ with
     the same contract. The service role key is used only inside the function, after the caller's session and permission are
     checked. The management token is never stored in the cloud (section 6.3).
- **Served at no cost by default**: from the owner's own computer, by our own program on 127.0.0.1 (section 6). Supabase
  cannot serve web pages from its own address (section 4, finding 1). Phone access needs a hosting choice by the owner.
- **Honest areas:** for each of the eleven areas, section 13 lists the operations that a real backend call can do, and the
  ones we will **not** show because no real call exists or it is unsafe. Nothing is shown as a button that does not work.
  A test (round A4) fails if any button has no Admin API route behind it.
- **Admin sign-in**: the person's normal account, an admin role from the existing badges, a password re-check for dangerous
  actions, and a second factor the owner picks (section 9). Every admin action writes one row to the extended `admin_log`,
  server-side, with before and after values.

## 2. Audit: what exists today

### 2.1 Client code

| Piece | Where | What it does | Tests |
|---|---|---|---|
| Admin client | `engine/include/okay/Platform/Admin.hpp` (`okay::Admin`) | Own rank (`my_role_rank`), dashboard numbers (`admin_stats`), find players, set and remove badges, verify, site ban and unban (`site_bans`), list bans, read `admin_log`, read and write `app_config`. 18 hand-written PostgREST routes; its own `Url`, `SplitObjects`, `Field`, `JStr`, `ParseIso` helpers | **none** (audit: "Admin has no test today") |
| Admin page | `launcher/AdminPage.cpp` (1028 lines) | Tabs Overview, Players, Marketplace, Forums, Bans, Audit log, Reports (every moderator); Official game and Anti-cheat (admins only). Work runs on a worker thread; the server enforces every rule, the page only hides what a rank cannot do | none (launcher UI) |
| Demo data in the Admin page | `launcher/AdminPage.cpp:109` `DemoAdminData()`, `:289` `DemoRepAdminData()` | Made-up players, numbers and log rows, shown only when `OKAY_UI_DEMO_USER` is set (screenshot hook). Inert without the variable, but it ships (`docs/status/unfinished_launcher.md` row "Chat sample data and 20+ demo flags") | - |
| Member moderation | `shared/MemberModeration.hpp`, `launcher/MemberModerationUI.cpp`, `docs/sql/member_moderation.sql` | Kick, ban, unban, admin role inside community servers and group chats (owner > admin > member), through four RPCs; logged to `admin_log` | `member_moderation`, `member_moderation_sql` |
| Anti-cheat admin tools | `engine/include/okay/AntiCheat/AntiCheatAdminTools.hpp`, `launcher/AntiCheatPages.cpp` | Review cheat reports, ban an account with its machines (`ac_ban_with_devices`), linked accounts (`ac_linked_accounts`), appeals | `anticheat_wiring`, `anticheat_sql` |
| User reports | `shared/UserReports.hpp`, `docs/sql/reports.sql` | Players report abuse; moderators set open / resolved / dismissed with a note; logged | `user_reports`, `reports_sql` |
| Official game publish | `launcher/main.cpp`, `docs/sql/official_game_admin.sql` | Admins upload the official game under `releases/official/` with their own login (no service key); `log_official_publish` logs it | none (launcher UI) |
| Crash upload | `shared/CrashUpload.hpp`, `docs/sql/crash_reports.sql` | Anyone with the publishable key inserts a scrubbed crash row; nobody but the service role reads it | `crash_upload`, `crash_reports_sql` |

### 2.2 Rules in `docs/sql/` the dashboard builds on

| Existing rule | File | Use in the dashboard |
|---|---|---|
| Roles as badges: `admin` (3), `staff` (2), `moderator` (1); `role_rank`, `is_admin`, `is_staff`, `is_global_mod`, `my_role_rank`, `can_manage_badge`; "you can only act on people ranked below you" | `admin.sql` | The role model (section 10) extends it |
| `admin_log` (actor, actor name, action, target, target name, detail up to 500 characters, time); written by triggers through `admin_log_write`; moderators read it; moderators insert only `note` rows | `admin.sql` | THE audit log, extended (section 11) |
| `ac_audit`: append-only (an `immutable` trigger refuses update, delete and truncate; purge only with a session flag), `actor_role`, `detail jsonb` | `anticheat.sql` | Its append-only trigger becomes the shared pattern for `admin_log` |
| `ac_rate_events` and `ac_rate_hit(key, limit, window)` (advisory lock, counts in a window) | `anticheat.sql` | The rate limiter (section 11.3) |
| `site_bans` with rank checks and a restrictive "not site banned" insert policy on 14 tables | `admin.sql` | Site bans area |
| `admin_stats()` (players, new this week, online now, banned, staff, threads this week, items, pending) | `admin.sql` | The overview numbers: real counts, no charts |
| `app_config` (key, value; everyone reads, rank 3 writes) | `app_config.sql` | Settings area (`voice_relay` and others) |
| `reports`, `ac_reports`, `ac_bans`, `ac_appeals`, `ac_devices` | `reports.sql`, `anticheat.sql` | Reports and anti-cheat areas |
| `crash_reports` (insert-only for players; service role reads, marks, deletes; empties itself after 7 or 14 days) | `crash_reports.sql` | Errors area |
| `game_sessions` (servers that advertise and heartbeat) | `game_servers.sql` | Networking area |
| Storage rules for `chat`, `workshop`, `releases/official/` | `chat_media.sql`, `social.sql`, `official_game_admin.sql` | Storage area |

### 2.3 Server functions and keys

- `supabase/functions/delete-account` and `session-ticket`: ours, TypeScript for Deno, each a **pure handler with injected
  dependencies** (`delete_account.ts`, `ticket.ts`) plus a thin `index.ts`, tested with fakes. They check the caller with
  `GET /auth/v1/user` and then use the service role key, which the Edge runtime provides (`SUPABASE_SERVICE_ROLE_KEY`); it
  never leaves the function. `delete-account` re-checks the password before acting. Both use Web Crypto (`ticket.ts` signs
  with Ed25519) and no imports. The Admin API function follows the same shape.
- **Service role key** (names only): `SUPABASE_SERVICE_KEY` in GitHub secrets for `release.yml`, `dev-build.yml`,
  `crash-issues.yml`; `SUPABASE_SERVICE_ROLE_KEY` inside the Edge runtime. It skips every row rule.
- **Management token** (`SUPABASE_ACCESS_TOKEN`, `sbp_...`): read only by four publish scripts, only on the owner's machine,
  only to fetch the service key when it is not given. It controls the whole Supabase account. It is not in CI.

### 2.4 Logs and errors today

- `admin_log` and `ac_audit`: admin actions (section 2.2).
- `crash_reports`: crash rows, turned into GitHub issues daily by `scripts/crash_to_issue.py`, then deleted after 7 days.
- `reports`, `ac_reports`: player reports.
- Supabase's own request, Auth, Storage, Postgres and function logs: only in Supabase's dashboard and its management API
  (section 3). Nothing of ours reads them today.
- No uptime or latency numbers are collected anywhere. Feature map 48.4.2 ("Dashboards", not built) is the same gap.

## 3. Audit: what each Supabase service lets an admin do

Three ways in: **service role** (the project's secret key; allowed only inside our server function), **management token**
(the owner's personal account token for `api.supabase.com`; whole account), and **not possible** through any public API.
All rows *(docs, not re-checked)*.

| Service | With the service role key (server-side) | Only with the management token | Not possible by public API |
|---|---|---|---|
| Auth (GoTrue) | List users (paged, `GET /auth/v1/admin/users`), get one, update one (`PUT /auth/v1/admin/users/<id>`: confirm email, change email, set a ban length `ban_duration`, metadata), delete one, invite, make a sign-in or recovery link (`/auth/v1/admin/generate_link`) | Read and change the Auth settings (providers, email templates, SMTP, redirect allow-list, rate limits) | Read password hashes; list another user's active sessions or end them one by one (a ban or a delete ends them) |
| Database (PostgREST) | Read and write any table in the exposed schemas, skipping row rules; count rows (`Prefer: count=exact`); call any function; the OpenAPI description at `/rest/v1/` lists tables and columns | Run SQL (`/v1/projects/<ref>/database/query`, marked beta), list backups | DDL (create or change tables, policies) through PostgREST; restore a backup (dashboard only, and point-in-time restore is a paid add-on) |
| Storage | List, create, change and delete buckets; list, upload, move, delete objects; signed links | - | Serve an HTML page from `*.supabase.co` (served as `text/plain`; see finding 1) |
| Edge Functions | Call them | List functions, their versions and status; deploy; set and list function secrets (names) | Read a function's source back from the cloud in a supported way |
| Realtime | Not used by us (audit) | Settings | - |
| Logs | - | Query the log store (`/v1/projects/<ref>/analytics/endpoints/logs.all`) and API request counts | Real-time streaming of logs to us |
| Project | - | Status, health of each service (`/v1/projects/<ref>/health`), API keys, network restrictions, SSL enforcement, custom domain | Billing and invoices (organisation dashboard only) |

## 4. Audit findings that shape the design

1. **Supabase will not host the page.** Our own doc (`docs/online/password-reset.md`) records that Supabase serves HTML from
   Storage and from Edge Functions on `*.supabase.co` as `text/plain`, and only a paid custom domain lifts that (marked
   unverified there; no upload was tried). So "a static page in our Supabase storage" is not a working option. Getting
   around the limit (for example an HTML page disguised as another file type) is rejected: it fights a documented provider
   rule.
2. **Service-role writes skip our logging and our checks.** Every logging trigger in `admin.sql` starts with
   `if auth.uid() is null then return` ("SQL editor: not logged"), and `workshop_items_guard` treats a null caller as trusted.
   A service-role request has no `auth.uid()`. So an Admin API that wrote with the service key would act unlogged and
   unchecked. **Design rule:** the Admin API sends each request **with the admin's own access token** whenever our SQL already
   has a rule for it (bans, roles, verify, reports, marketplace, forums, app_config, anti-cheat); then the same row rules and
   triggers as the launcher apply. The service role is used only where no row rule exists (Auth admin, bucket lists, crash
   reports, row counts) and then the function writes the audit row itself.
3. **`admin_log` is not tamper-proof and has no before/after.** It has no update or delete policy (so the API cannot change
   it), but the service role and the SQL editor can; it holds no before/after values, no source and no request id.
   `ac_audit` already shows the append-only pattern.
4. **Roles are a rank, not permissions.** Three badges map to ranks 1 to 3. There is no "owner" above admin and no read-only
   "support" role.
5. **The staff policy on `profiles` was wide.** `forum_sections.sql` "staff verify players" let staff update any column of
   any profile, not only `verified`. Confirmed in round A2 by a failing check in `admin_sql` (staff renamed a player and
   changed their bio) and fixed there: `profiles_keep_verified` now refuses any client change to someone else's profile
   other than `verified`.
6. **The Admin client has no test** and writes its own copies of helpers that OkayBackend now has (`Url`, JSON field reads).
7. **Demo data ships in the launcher's Admin page** (section 2.1). The dashboard has no demo mode at all: an empty answer is
   shown as empty, an error as an error.
8. **Crash rows become GitHub issues.** The dashboard may show the issue number as text but never links to github.com
   (rule 19b).

## 5. Design: how the parts fit

```
 Dashboard (our HTML/CSS/JS, in a browser)          launcher Admin page (later, same API; section 14)
        |  same-origin requests to /admin/v1/...
        v
 Admin console: okay-backend --admin on 127.0.0.1   (our C++; serves the page, forwards /admin/v1 to the provider)
        |                                   \
        |  Supabase provider                 \  owner-only management routes (section 6.3)
        v                                     v
 okay-admin Edge Function (our TypeScript)    api.supabase.com with the owner's token from the console's environment
   checks session, role, permission, rate
   then: admin's token -> PostgREST (RLS + triggers apply, logged by SQL)
         service role  -> Auth admin, Storage, counts, crash rows (logged by the function)
        |
 Supabase project (production)

 Later, Local provider: the console forwards /admin/v1 to the Local backend's own C++ handlers (same contract).
```

- **One contract, two servers.** The Admin API is a list of routes with request and reply shapes (section 7) written in one
  file, `backend/admin/api.json` (planned). The Edge Function and, later, the Local backend each implement it, and one contract
  test suite runs against both (as OkayBackend's `okay_backend_contract`).
- **The dashboard never names a provider route.** It knows `/admin/v1/...` only. A test (round A4) fails if the page's files
  contain `/rest/v1`, `/auth/v1`, `/storage/v1`, `/functions/v1`, `supabase.co` or `github.com`.
- **Homes** (rule 19e, all planned): the page in `backend/admin/` (the OkayBackend server folder from okay_backend.md section 2),
  the console mode in `backend/main.cpp` (`okay-backend --admin`; the same program the Local backend becomes, not a second
  one), the function in `supabase/functions/okay-admin/`, SQL in `docs/sql/admin.sql` (extended).

## 6. Where the page is served, and what it costs

### 6.1 Options

| Option | Cost | Phone | Real HTTP headers (CSP) | Management routes possible | Notes |
|---|---|---|---|---|---|
| **A. Our own program on the owner's computer, 127.0.0.1** (`okay-backend --admin`) | none | no (desktop only) | yes | yes, token stays on that computer | Uses the HTTP server OkayBackend round 8 moves out of `editor/WebRun.cpp` |
| B. Supabase Storage or an Edge Function | none | - | - | - | Does not work: HTML is served as `text/plain` (finding 1) |
| C. Supabase custom domain add-on (page from an Edge Function on our own domain) | paid add-on, monthly (price on Supabase's pricing page; not re-checked) plus a domain | yes | yes | no (token would have to be stored in the cloud) | Keeps everything with the provider we already use |
| D. A free static host (for example Cloudflare Pages or Netlify free plans) | none today, plus a domain if wanted | yes | yes (host's header file) | no | A new outside provider the page depends on; their free plans can change |
| E. Option A, also listening on the home network | none | yes, on the same Wi-Fi | yes | yes | No TLS without our own TLS server code; plain HTTP on a network leaks the admin token. **Not offered** unless the owner allows TLS work |
| F. The launcher's own Browser page opens option A | none | no | yes | yes | Needs our browser's JavaScript `fetch` (browser plan round 3); unverified |
| G. GitHub Pages | none | - | - | - | Forbidden (rule 19b) |

### 6.2 Default

**Option A** (zero cost, no new provider, real security headers, and the only option where management-token routes are safe).
The page is still built responsive, so options C or D only change where the same files are served; the owner picks one when
phone access is wanted (decision 16.1).

### 6.3 The management token

It never goes into a page, a Supabase function secret, CI, or the repository. Routes that need it (logs, function list and
status, Auth settings, project health) are served **only by the console on the owner's own computer**, which reads
`SUPABASE_ACCESS_TOKEN` from its own environment (the name the publish scripts already use). When the page is served any other
way those areas are not drawn at all, and one line of text says "Only on the owner's computer". Storing the token as a
function secret is rejected: one bug in the function would hand out the whole Supabase account.

## 7. The Admin API

All routes under `/admin/v1`. JSON in, JSON out. Every reply carries `request_id`. Errors: `{ "error": code, "message": text }`
with codes `signed_out`, `forbidden`, `reauth_needed`, `second_factor_needed`, `rate_limited`, `not_set_up` (a table or
function is missing on the provider), `not_supported` (this provider cannot do it), `bad_request`, `server`.

| Route | Permission | Supabase provider does it with |
|---|---|---|
| `POST /session` (email, password) / `POST /session/refresh` / `DELETE /session` | - | GoTrue password grant and refresh, server-side; the page holds the reply as an opaque token |
| `POST /session/reauth` (password, and a second-factor code when decision 16.2 says so) | - | GoTrue password grant; marks the session "re-checked" for 5 minutes |
| `GET /me` | `dashboard.view` | `/auth/v1/user` + `admin_permissions_of(uid)` |
| `GET /capabilities` | `dashboard.view` | Which routes this provider and this way of serving support (section 8) |
| `GET /providers` / `POST /providers/check` | `providers.read` / `providers.check` | Registry (section 8) and live probes |
| `GET /overview` | `dashboard.view` | `admin_stats()` |
| `GET /users?q=&page=` / `GET /users/<id>` | `users.read` | `profiles`, `profile_badges`, `site_bans` (admin's token) |
| `GET /users/<id>/account` | `users.read_private` | Auth admin get user (email, created, last sign-in, confirmed) |
| `POST /users/<id>/site-ban` / `DELETE ...` | `users.ban` | `site_bans` (admin's token: rank rules and log trigger apply) |
| `POST /users/<id>/roles` / `DELETE /users/<id>/roles/<role>` | `roles.manage` (+ `can_manage_badge`) | `profile_badges` (admin's token) |
| `POST /users/<id>/verified` | `users.verify` | `profiles.verified` (admin's token) |
| `POST /users/<id>/auth-ban` (length) | `auth.ban` + reauth | Auth admin `ban_duration` (service role; logged by the function) |
| `POST /users/<id>/confirm-email` | `auth.manage` | Auth admin update (service role) |
| `POST /users/<id>/password-reset` | `auth.reset` | GoTrue recover (sends the normal email; the admin never sees a password or link) |
| `DELETE /users/<id>` | `auth.delete` + reauth + typed username | The `delete-account` step list, run for that user (service role) |
| `GET /tables` / `GET /tables/<t>/rows?...` | `db.read` | PostgREST OpenAPI + counts; rows only from the allow-list (13.1) |
| `PUT /config/<key>` | `config.write` | `app_config` (admin's token) |
| `GET /storage/buckets` / `GET /storage/<b>?prefix=` / `DELETE /storage/<b>/<path>` | `storage.read` / `storage.delete` + reauth | Storage API (service role) |
| `GET /functions` / `POST /functions/<name>/check` | `functions.read` | Our known list + a health call (13.7); deployed list only on the console |
| `GET /network/servers` / `GET /network/relay` | `network.read` | `game_sessions`, `app_config.voice_relay`, a reachability probe from the console |
| `GET /audit?...` | `audit.read` | `admin_log` (+ `ac_audit` with `ac.review`) |
| `GET /errors/crashes` / `GET /errors/reports` / `PATCH /errors/reports/<id>` | `errors.read` / `reports.review` | `crash_reports` (service role, read only); `reports` (admin's token) |
| `GET/PATCH` marketplace, forums, anti-cheat routes | `market.review`, `forums.moderate`, `ac.review` | The same tables and RPCs the launcher uses, admin's token |
| `GET /logs?service=&since=` | `logs.read`, console only | Management API log query |

## 8. Native or Supabase: the provider view

- **The registry** is data, not code: for each service (Auth, Database, Storage, Functions, Realtime, Logs, Email, Hosting of
  the dashboard) the Admin API answers `GET /providers` with `provider` (`supabase`, `local`, `okaybackend`), `code_owner`
  (`ours` or `provider`), `runs_on`, and `status` measured now.
- **"Native"** means our own code doing the work. Today, honestly:

| Service | Code that does the work | Runs on | Shown as |
|---|---|---|---|
| Client layer (OkayBackend, `AccountService`) | ours | the person's computer | native |
| Row rules, functions, triggers (`docs/sql/`) | ours (SQL) | Supabase's PostgreSQL | ours, on Supabase |
| Server functions (`delete-account`, `session-ticket`, `okay-admin`) | ours (TypeScript) | Supabase Edge runtime | ours, on Supabase |
| Database engine, Auth, Storage, Edge runtime, email sending, logs | Supabase | Supabase | Supabase |
| Relay / voice server | ours | wherever the owner runs it | native |
| Dashboard and console | ours | the owner's computer | native |

- **Status is read live, never kept as a number:** `POST /providers/check` makes one real request per service (Auth: the
  settings route with the publishable key; Database: a one-row read of `app_config`; Storage: list of the `releases` bucket's
  first object; each function: its health route) and shows the answer, the HTTP status and the time it took, with the time it
  was measured. On the console, Supabase's own health route is added. No history, no graphs: nothing is stored that could be
  mistaken for monitoring we do not have.

## 9. Admin sign-in and the second factor

- **Who:** a normal account with an admin role (section 10). Signed in through `POST /admin/v1/session`; the function checks
  the role before answering, so a player gets `forbidden` and never a dashboard.
- **Session:** the access token is kept **in page memory only** (not `localStorage`, not cookies): closing the tab signs out.
  Refresh through `/session/refresh`. The dashboard signs out after 30 minutes without use.
- **Dangerous actions** (auth ban, delete a user, delete files, grant admin or owner, change settings) need `POST
  /session/reauth` within the last 5 minutes. The password check is done server-side by a GoTrue sign-in, as `delete-account`
  does today.
- **Second factor (owner decision 16.2):**
  - (a) **Our own TOTP** (RFC 6238 over RFC 4226) inside the function, using Web Crypto's HMAC (the platform's implementation,
    as `ticket.ts` uses Web Crypto for Ed25519; we write only the counter, the truncation and the code check). The secret is
    stored in a table only the service role can read. Works with common authenticator apps. On the Local backend libsodium has
    HMAC-SHA-256 but no SHA-1, so either both use SHA-256 (not every authenticator app honours it; unverified) or Local waits.
    Rule 18: this uses a vetted primitive and writes no cipher of our own, but it is security code: reported "not
    independently reviewed".
  - (b) **Supabase Auth's own multi-factor (TOTP factors)**: no code of ours for the factor, but admin sign-in then depends on
    Supabase and the Local backend needs (a) anyway.
  - (c) **Password re-check only.**
  - **Owner decision 2026-11-27: (a)**, our own TOTP, for dangerous actions. A2 built the storage: `admin_totp_secrets`
    (secret as opaque bytes, 16 to 256; algorithm SHA1 or SHA256; 6 or 8 digits; period; when confirmed; the last time step
    used) and `admin_totp_use_step(uid, step)`, which accepts a step only once (a code cannot be replayed, two requests with
    one code cannot both pass). Only the service role has any privilege on the table; the check itself is in the function
    (A5). The secret is **not** encrypted by SQL: the only database crypto we use is pgcrypto's `crypt` for a password
    compare, and a key kept in the same database would protect nothing. A5 may seal it with Web Crypto AES-GCM under a key
    held only in the function's secrets (rule 18: a vetted primitive, not our own cipher).

## 10. Roles and permissions

- **Roles extend the existing badges** (rule 19d): `owner` (new, rank 4, granted only in the SQL editor), `admin` (3), `staff`
  (2), `moderator` (1), and `support` (new, rank 0: it gives no power over the existing row rules, only dashboard reads).
  `role_rank`, `is_admin` and "act only on people ranked below you" keep working; `is_admin` includes `owner`.
- **Built in A2** (`docs/sql/admin.sql`): `admin_permissions` (permission, kind `read` / `write` / `danger`, description) and
  `admin_role_permissions`; `admin_can(uid, permission)` and `admin_permissions_of(uid)`. The owner holds every known
  permission whatever the table says, so the owner cannot be locked out. Re-running `admin.sql` seeds only permissions it has
  not seen before, so the owner's edits to the table stay. Nobody writes the table through the API; `permissions.manage`
  (owner) changes it through the function in A5, or the owner in the SQL editor.
- **Changed in A2:** handing out `admin` is now owner-only (it was any admin); `owner` is never handed out through the API;
  staff can hand out `support`. `can_manage_badge` also needs `roles.manage`; `site_bans`, `admin_log` reads and `admin_stats`
  use `admin_can` instead of the fixed rank. The launcher still shows admins a "give admin" button that the server now refuses
  (fixed when the launcher moves onto the Admin API, round A8).
- **Permissions** are rows in a new `admin_role_permissions (role, permission)` table, read through one function
  `admin_can(uid, permission)`, so a role can change without code changes. Default mapping (decision 16.3):

| Permission | owner | admin | staff | moderator | support |
|---|:-:|:-:|:-:|:-:|:-:|
| `dashboard.view`, `providers.read`, `users.read`, `network.read` | x | x | x | x | x |
| `audit.read`, `reports.review`, `market.review`, `forums.moderate`, `users.ban` (rank rule still applies) | x | x | x | x | |
| `users.verify`, `roles.manage` (moderator and cosmetic badges) | x | x | x | | |
| `users.read_private` (email, sign-in times), `auth.reset`, `auth.manage`, `ac.review`, `errors.read`, `db.read`, `storage.read`, `functions.read`, `providers.check`, `config.write`, `roles.manage` (staff, admin) | x | x | | | |
| `auth.ban`, `auth.delete`, `storage.delete`, `roles.manage` (admin), `logs.read` (console), `audit.read_ip`, `permissions.manage` | x | | | | |
| Section 18, owner console: `sql.read`, `sql.write`, `schema.manage`, `backups.manage`, `functions.deploy`, `functions.secrets`, `auth.settings`, `auth.end_sessions`, `storage.manage`, `network.manage`, `releases.pull` | x | | | | |
| Section 18, game: `game.maintenance`, `game.min_version`, `game.features`, `users.mass_action`, `servers.control` | x | x | | | |
| Section 18: `game.announce`, `chat.filter` | x | x | x | | |
| Section 18: `users.mute`, `chat.moderate`, `rooms.kick` | x | x | x | x | |
| Section 18: `profiles.edit` (allowed fields only), `users.view_as` (read only) | x | x | x | | x |

Kinds (they pick the rate limit, section 11.3): `danger` for `auth.ban`, `auth.delete`, `storage.delete`,
`permissions.manage`, every owner console write, `game.maintenance`, `game.min_version`, `game.features`,
`users.mass_action`, `servers.control`, `releases.pull`; `read` for the views; `write` for the rest. Every `danger`
permission also needs the second factor (A5) and, where it destroys or switches something off, a preview or the name typed
back (`admin_set_config` already refuses a `danger` key without it).

## 11. Audit log and rate limits

### 11.1 One log, extended

`admin_log` stays THE admin audit log (rule 19d). New columns: `before jsonb`, `after jsonb`, `source` (`dashboard`,
`launcher`, `console`, `sql`), `request_id uuid`, `permission text`, `outcome` (`ok`, `refused`, `failed`), `ip inet`,
`user_agent text` (300 characters). `detail` stays for old rows and the launcher.

**Built in A2:** the columns; `admin_log_record` (the one writer; service role only, for the function), `admin_log_write`
(as the signed-in caller, used by the triggers, now with before / after and the permission), `admin_log_context()` (reads
the request headers PostgREST passes: `x-okay-source`, `x-okay-request-id`, `x-okay-client-ip`, `x-okay-user-agent`, else
`User-Agent` and the first `X-Forwarded-For` address; values that do not parse are dropped; a client can never claim
`sql`). A note a moderator inserts gets the server's values for every field the server owns. These header values are what
the request claims: a signed-in admin talking to the API directly could set them; the `actor` column cannot be forged. Which
headers Supabase's gateway really passes is unverified until A3 runs against a project.

- **Who writes it:** only the server. Rows from RLS-covered actions come from the existing triggers (the function passes
  `source`, `request_id` and the address as request settings the triggers read). Rows for service-role actions are written by
  the function through `admin_log_write` (extended). The page cannot write a row.
- **Refused attempts are logged too** (`outcome = refused`), so probing shows up.
- **Append-only (built in A2):** `audit_append_only()` in `admin.sql` replaces `ac_audit_immutable` (deleted) and guards both
  logs. Update, delete and truncate are refused from every role, the SQL editor included. The one change allowed is
  emptying a link to a person (`actor`, `target`, their names, `ip`, `user_agent`, `before`, `after` set to null or
  `[deleted]`), which `delete-account` does; emptying `actor` also empties the address and agent, emptying `target` also
  empties before and after. A session setting (`okay.admin_log_maintenance`, `okay.ac_audit_purge`) lets one maintenance job
  through for one transaction: `ac_purge_audit` and `public_names.sql`'s one-off name clean-up.
- **Readers:** `audit.read` sees rows without `ip` and `user_agent`; `audit.read_ip` (owner) sees all.

### 11.2 "From where"

The function records the address from the request headers the Edge runtime passes (`x-forwarded-for`; unverified) and the
`User-Agent`. This is personal data: default retention 90 days for `ip` and `user_agent` (set to null by the retention job),
the rest kept (decision 16.5). Built in A2: `admin_log_purge_old()` (the SQL editor, pg_cron job
`okay-admin-log-retention` when pg_cron is on, or the owner) empties them and leaves one `log_retention` row.

### 11.3 Rate limits

Reuse `ac_rate_hit` (renamed only if the owner wants; no second limiter): sign-in 5 per 10 minutes per address and per
email; re-check 5 per 10 minutes per admin; reads 120 per minute per admin; writes 30 per minute per admin; dangerous actions
10 per hour per admin. A limit hit answers `rate_limited` and writes a `refused` log row.

**Built in A2:** `ac_rate_hit` and its table moved from `anticheat.sql` to `admin.sql` (one limiter; `anticheat.sql` runs
after `admin.sql` and uses it). `admin_gate(permission, action, target)` (the signed-in caller) and `admin_gate_as(actor, ...)`
(service role, for actions the function has checked a second factor for) answer `ok`, `forbidden`, `rate_limited` or
`signed_out`, use the permission's kind for the limit (read 120 a minute, write 30 a minute, danger 10 an hour), and log every
refusal, at most 20 refusal rows per account per 10 minutes so probing cannot flood the log. Changes made straight through
the API under a permission (the word filter, `app_config`'s free keys) go through `admin_row_log`, which counts them against
the same write limit and logs before and after. Sign-in limits per address and email are the function's (A3), with
`ac_rate_hit` (granted to the service role).

## 12. How the page is protected

- **No secret in any page.** The page holds the admin's own short-lived access token in memory. The service role key lives
  only in the Edge runtime; the management token only in the console's environment.
- **Same origin.** In option A the page calls only its own origin (`/admin/v1/...`), so the policy is `connect-src 'self'`.
  The Edge Function answers browsers only from an allow-list of origins (CORS, no `*`); with option A it needs none, because
  the console calls it server to server.
- **Headers sent by the console** (written into our server, not a meta tag, so all of them apply):
  `Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self';
  font-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`, `X-Content-Type-Options: nosniff`,
  `Referrer-Policy: no-referrer`, `Cache-Control: no-store` on API answers, `Cross-Origin-Opener-Policy: same-origin`.
- **No script injection paths:** no inline script or style, no `eval`, every value written with `textContent`, never
  `innerHTML`. A test checks the page's files for these.
- **No cross-site requests:** the token is sent in an `Authorization` header, never a cookie, so another site cannot make the
  browser act as the admin.
- **The local console:** listens on 127.0.0.1 only; refuses a request whose `Host` is not `127.0.0.1:<port>` or
  `localhost:<port>` (stops DNS rebinding) and any `POST`/`PUT`/`PATCH`/`DELETE` whose `Origin` is not its own; prints a
  one-time start link with a random code (from `OsRandom`) that the page must present once, so another local program cannot
  drive it unseen.

## 13. Each area: real operations and what is left out

"Round" is the round in section 15 that builds it. "Left out" items are not drawn: no button, no number, no placeholder.

### 13.1 Databases
- **Real:** list tables and columns (PostgREST description, service role); row count per table; browse rows of an
  allow-listed set of tables with filters and paging (read only), with private columns hidden (emails, hardware hashes,
  crash text bodies unless `errors.read`); edit only through the named actions of the other areas, never a generic cell edit.
  Round A6.
- **Moved to section 18 (owner console only, A10):** the SQL editor, table and policy changes, backups and restore.
- **Left out:** running SQL from any page other than the console on the owner's computer; database size, CPU and query
  statistics (no route we have checked; no made-up numbers).

### 13.2 Authentication
- **Real:** per account: email confirmed or not, created, last sign-in; confirm an email; send the normal password-reset email;
  auth-level ban for a length (blocks sign-in) and lift it; delete an account with all its data (the `delete-account` step
  list). Round A5.
- **Moved to section 18 (A10):** Auth settings changes and ending all sessions of an account (console only).
- **Left out:** showing or setting a password, or a reset link (the admin never sees one); ending ONE chosen session of
  another person (no route).

### 13.3 Users
- **Real:** search players; profile, roles, site-ban state; site ban and unban with reason and length; verify; anti-cheat
  bans, linked accounts and appeals (admins). Round A3 (read, bans, roles, verify), A6 (anti-cheat).
- **Moved to section 18 (A11):** editing a player's profile fields from an allowed list (`profiles.edit`), and a read-only
  "view as player".
- **Left out:** any session as a player (impersonation): unsafe, and never built.

### 13.4 Storage
- **Real:** list buckets with their public flag and limits; list objects in a folder with size and time; open a public file's
  link (our project address only); delete one file in `chat` or `workshop` (moderation, with reauth). Official game uploads
  stay where they are (Admin > Official game, admin's token). Round A6.
- **Moved to section 18 (A10):** creating buckets, changing their limits and rules, uploading and replacing files (console only).
- **Left out:** uploading from any page other than the console (the publish scripts and the launcher's official-game flow are the
  paths, with their checksums); changing bucket rules (SQL files); storage used per bucket as a total (no route checked;
  summing a full listing is slow and is not done).

### 13.5 API endpoints
- **Real:** the list of Admin API routes this provider supports and the list of our server functions, from `api.json` and
  `/capabilities`; for each, whether the provider answers now (13.7). Round A7.
- **Left out:** request counts and latency per endpoint (only the management API has counts: shown on the console only, as
  numbers it returns, round A7; nothing on other hosts); editing the exposed schemas or API settings (management API, not
  offered).

### 13.6 Permissions
- **Real:** who holds which role; grant and remove roles within `can_manage_badge`; the role-to-permission table, readable by
  all admins and changeable by the owner (with reauth). Round A3 (roles), A5 (permission table edits).
- **Left out:** editing row-level policies (SQL files, not a web form).

### 13.7 Server functions
- **Real:** our functions with a live health call each (round A7 adds a `GET` health answer to `delete-account`,
  `session-ticket` and `okay-admin`, without the service key, saying only "configured" or which setting is missing by name);
  on the console, the deployed list, versions and status from the management API. Round A7.
- **Moved to section 18 (A10):** deploy, roll back, secrets (console only).
- **Left out:** editing function code in a page, and deploying or setting secrets from any page other than the console (a page
  that deploys code is unsafe); invocation counts outside the console.

### 13.8 Networking
- **Real:** servers listed in the browser now (`game_sessions` rows with heartbeat time), the voice relay address
  (`app_config.voice_relay`) and change it; on the console, a UDP reachability probe of the relay address. Round A7.
- **Left out:** traffic, bandwidth and egress numbers (not available to us except Supabase's billing pages); network
  restrictions and SSL enforcement changes (management API; shown read-only on the console in round A7, never changed from
  the page).

### 13.9 Logs
- **Real:** `admin_log` with filters (who, action, target, time, outcome) and `ac_audit`; on the console, Supabase's request,
  Auth, Storage, Postgres and function logs for a chosen time window, as returned. Round A3 (audit), A7 (console logs).
- **Moved to section 18 (A10):** "live" logs on the console, by asking the logs route for the newest window every few seconds.
- **Left out:** true streaming (no public route); logs on any host other than the console.

### 13.10 Errors
- **Real:** crash reports still in the table (they empty after 7 or 14 days), with their GitHub issue number as text only;
  player reports with review; anti-cheat reports. Round A6.
- **Left out:** error rates and graphs (nothing collects them; feature map 48.4 is the place to build collection first);
  links to GitHub (rule 19b).

### 13.11 External providers
- **Real:** the registry and live checks (section 8). Round A3 (registry), A7 (checks).
- **Left out:** switching a provider from the page (a provider switch is configuration plus a migration, OkayBackend
  section 8, run on the owner's computer; a button that flips production is unsafe); provider costs (no billing API).

## 14. The launcher's Admin page: no second copy

Two screens that ban, verify and review would be a duplicate (rule 19d). So:
- **One server side.** Whatever the launcher page and the dashboard both do goes through the same Admin API, and `okay::Admin`
  becomes a client of it (it moves onto OkayBackend anyway in OkayBackend round 7; its missing test is written first).
- **One screen per job.** **Default:** the dashboard replaces the launcher's Admin tabs one by one; in the same change each
  replaced tab becomes a short line "Open the admin dashboard" and its code (and its demo data) is deleted. The ban banner and
  player-side reporting stay in the launcher (they are player features). Decision 16.6 can keep the launcher tabs instead,
  and then the dashboard does only what the launcher does not.

## 15. Implementation rounds

Each round: one PR from `claude/ready`, precheck clean before the push (rules 6, 7a, 9). Round letters keep them apart from
OkayBackend's numbered rounds.

1. **A1. Audit and design (this round).** Exists: everything in section 2. Gap: no audit, no design. Files: this page, feature
   map 17.19.6, a pointer in `okay_backend.md`, the docs index. Tests: none (docs).
2. **A2. SQL: roles, permissions, audit log, limits.** Exists: `admin.sql`, `ac_audit`'s append-only trigger, `ac_rate_hit`.
   Gap: no owner or support role, no permission table, `admin_log` without before/after, source or immutability. Files:
   `docs/sql/admin.sql`, `docs/sql/anticheat.sql` (shared immutable trigger, limiter moved), `docs/sql/app_config.sql`
   (controlled keys), `docs/sql/public_names.sql` (its name clean-up under the maintenance setting),
   `docs/sql/forum_sections.sql` (the staff profile bug, finding 5), `tests/sql/test_admin.sql`,
   `tests/scripts/test_admin_sql.sh`. Tests: `admin_sql`, 152 checks on a local Postgres (ranks, rank rule, permissions, log
   rows from each trigger, append-only refusal, account-deletion emptying, retention, the gate and its limits, TOTP privileges
   and replay, the word filter, the controlled config keys). **Written, tested locally; not run on the live project.**
   The CI job "SQL row-level-security tests (Postgres)" runs every `tests/scripts/test_*_sql.sh`, so it runs `admin_sql` too;
   a ctest entry in `tests/CMakeLists.txt` (a shared file) was not added.
3. **A3. The `okay-admin` function, first areas.** Exists: the `delete-account` handler shape. Gap: no Admin API. Files:
   `backend/admin/api.json` (the contract), `supabase/functions/okay-admin/{admin_api.ts,index.ts,admin_api_test.ts}`. Areas:
   session, reauth, me, capabilities, providers (registry), overview, users (read, site bans, roles, verify), audit read.
   Tests: `okay_admin_function` (Deno, fakes: every route's permission, refused callers, rate limits, the admin's token used
   for RLS routes, the log row for every write, no secret in any reply).
4. **A4. The dashboard and the console.** Exists: OkayBackend round 8's HTTP server (if not merged, its move is done here, not
   copied). Gap: no page, no server. Files: `backend/admin/{index.html,admin.css,admin.js}`, `backend/main.cpp`
   (`okay-backend --admin`), `backend/CMakeLists.txt`. Tests: `okay_admin_page` (Node, like `test_password_reset_page.js`:
   every button maps to a route in `api.json`; no provider route, no `github.com`, no `innerHTML`, no inline script; layouts at
   phone and desktop widths), `okay_admin_console` (headers, Host and Origin checks, start code, forwarding).
5. **A5. Users and Auth, second factor.** Gap: no Auth admin. Files: function routes, `delete-account` step list shared
   (one file both read). Tests: function tests; the second factor per decision 16.2 with RFC 6238's test vectors.
6. **A6. Databases, storage, errors, anti-cheat, marketplace, forums.** Tests: function tests with the allow-list and hidden
   columns.
7. **A7. Functions, networking, endpoints, providers checks, console management routes.** Tests: health routes, console with a
   fake management API.
8. **A8. The launcher on the Admin API.** `okay::Admin` gets its test first, then calls the Admin API; launcher tabs replaced
   per section 14. Tests: `admin_client` (golden requests), launcher build.
9. **A10. Owner console powers** (section 18.1). Console only, management token from the owner's environment, TOTP and a
   typed confirm for every write. Tests: console with a fake management API; the SQL editor's read-only transaction refuses
   a write; preview then commit; every route writes a log row with before and after.
10. **A11. Game and community controls** (section 18.2). The SQL storage and row rules were built in A2; A11 adds the
   function routes, the server-side enforcement and the launcher's side. Tests: function tests, SQL tests for each
   enforcement rule, launcher build.
11. **A9. Local provider.** After OkayBackend rounds 9 to 14: the Local backend answers `/admin/v1` with C++ handlers; the same
   contract suite runs against both. Tests: `okay_admin_contract`.

Nothing in rounds A2 to A9 runs against the live project in CI; the owner applies SQL and deploys the function (as today).

## 16. Owner decisions

Each with the default used until the owner decides.

1. **Where the page is served.** **Decided 2026-11-27: A, the owner's own computer only.** For phones: C (Supabase
   custom domain, paid) or D (a free static host, a new outside provider).
2. **Second factor.** **Decided 2026-11-27: (a) our own TOTP for dangerous actions.**
3. **Roles.** Default: add `owner` (only you) and `support`; the permission table in section 10. Who gets admin, staff,
   moderator and support is yours to say.
4. **Management token.** Default: **only on the console, from your environment; never stored in the cloud.**
5. **Addresses in the audit log.** Default: **kept 90 days, then removed; the rest kept.**
6. **The launcher's Admin tabs.** Default: **replaced by the dashboard tab by tab** (no two screens for one job).
7. **The function's name** (`okay-admin`) and the allowed origins for options C or D.

## 17. Sources, licences and IP flags

- **Written from:** RFC 9110 and 9112 (HTTP), RFC 6454 (origins), W3C Content Security Policy Level 3, the WHATWG Fetch
  standard (CORS), RFC 8259 (JSON), RFC 6238 and RFC 4226 (TOTP, HOTP) if decision 16.2 picks (a), Supabase's public API
  documentation (behaviour only), and our own SQL in `docs/sql/`. The page uses system fonts and icons drawn by us in SVG.
- **No third-party code:** no framework, UI kit, chart library, icon font, web font or Supabase client library. Web Crypto in
  the Edge runtime and libsodium on Local are the only crypto, as today (rule 18).
- **IP flags:**
  1. "Supabase" is a trademark; it appears only as the provider's name. The dashboard must not copy the look, layout, icons or
     names of Supabase's own dashboard (Supabase Studio); it follows our launcher's style.
  2. The Admin API is our own route shape; the provider routes it calls are public interfaces, used as documented.
  3. TOTP (RFC 6238) is an open IETF standard; no patent claim is known to us, but none was searched (unverified).
  4. The free static hosts named in 6.1 are only options, not used.
  5. Round A2's SQL was written from the PostgreSQL 16 manual (row security, triggers, privileges, advisory locks) and our
     own files in `docs/sql/`; no outside SQL was used.
  6. Pulling a release from "latest" (section 18.2) touches our own release index; it must keep CLAUDE.md rule 13 (never a
     "latest" without checksums). Not an IP matter, a safety one.
  7. None found beyond these.

## 18. Owner powers and game controls

Owner request, 2026-11-27: "can you add more admin powers, more control". The dashboard runs on the owner's computer only
(decision 16.1), so the console there may use the management token (section 6.3). Every power below has: a permission
(section 10), the second factor for `danger` ones (A5), a preview or the name typed back for destructive ones, the rate limit
of its kind (section 11.3), and an audit row with before and after. Nothing is unlimited and nothing is drawn without a real
call behind it. What Supabase's Management API offers is *(docs, not re-checked)*; A10 checks each route before coding it,
and a route that does not exist moves to "left out" with the reason.

### 18.1 Owner only, through the console (round A10)

| Power | Permission | Real call | Safety |
|---|---|---|---|
| SQL editor | `sql.read`; writes `sql.write` | Management API SQL route (beta) | Read-only by default: every query runs in `begin; set transaction read only; ... rollback`. A write runs first as a preview in a transaction that is rolled back (rows changed, shown), then again on the typed confirm and TOTP, then commits |
| Table and row-rule changes | `schema.manage` | The same SQL route | As a write; the console reminds that `docs/sql/` is the record and the change must go into the file too |
| Backups, restore | `backups.manage` | Backup list route; restore only where the plan offers it (point-in-time restore is a paid add-on) | Restore: preview of what is lost (the time span), typed project name, TOTP. Not drawn when the plan has no restore |
| Functions | `functions.deploy`, `functions.secrets`, `logs.read` | Deploy route; "roll back" = deploy an earlier build the console kept on the owner's computer (there is no route that returns old code); secrets set and delete by name | Secret values are typed in and never shown back; names only in the log |
| Auth settings | `auth.settings` | Auth config route: sign-up on/off, email templates, password rules, providers | Before / after in the log (template text and settings, never a secret such as an SMTP password: logged as "changed") |
| End all sessions of an account | `auth.end_sessions` | Through the SQL route: delete the account's rows in Auth's sessions and refresh-token tables (from memory of GoTrue's schema; unverified) | Access tokens already issued stay valid until they expire (up to an hour by default); the page says so |
| Buckets | `storage.manage` | Storage API (service role): create, update limits and public flag, upload, replace; bucket row rules through `schema.manage` | Replace shows the old size and time; uploads to `releases/` keep the checksum rules |
| Network | `network.manage` | Network restrictions, SSL enforcement and custom domain routes; Auth rate limits through the Auth config route | Preview of the new allow-list with the owner's own address checked, so the owner cannot lock themselves out |
| Live logs | `logs.read` | Logs query route, polled every few seconds for the newest window | Shown as returned; no stored copy |

Left out: PostgREST request rate limits (no route we know of); billing; true log streaming.

### 18.2 Game and community controls (storage and row rules built in A2; the rest round A11)

| Control | Permission | Built in A2 | A11 adds |
|---|---|---|---|
| Maintenance mode and its message | `game.maintenance` | `app_config` keys `maintenance` (`on`/`off`), `maintenance.message` | The launcher shows it and keeps players out of online play; server rules refuse new posts and matchmaking while on |
| Announcement banner | `game.announce` | Keys `announcement` (one line, 300 characters), `announcement.level` (`info`/`warning`/`critical`), `announcement.until` | The launcher draws the banner |
| Minimum launcher and game version | `game.min_version` | Keys `min_version.launcher`, `min_version.game` (`x.y.z` or empty) | The launcher asks to update; the ticket service refuses an older game |
| Pull a bad release from "latest" | `releases.pull` (owner) | the permission | Console: rewrite our release index in the `releases` bucket to point "latest" at the previous good version; refused unless that version has all three checksums (rule 13) |
| Feature switches | `game.features` | Keys `feature.chat`, `feature.uploads`, `feature.matchmaking`, `feature.signups` (`on`/`off`) | Restrictive insert rules on the chat tables and storage while off (like "not site banned"); matchmaking functions check it; sign-ups: the Auth setting through `auth.settings` (the key alone only hides the button, so A11 sets both) |
| Mass actions (mute, ban, unban a list) | `users.mass_action` | the permission (danger: 10 an hour) | Preview of every name and what changes, at most 50 players, typed confirm and TOTP; each player goes through the normal per-player rule and gets its own log row, plus one summary row |
| Mute | `users.mute` | the permission | A `chat_mutes` table and a restrictive insert rule on the chat tables (no mute exists today) |
| Chat word filter | `chat.filter` edits, `chat.moderate` reads | Table `chat_word_filter` (lower-case pattern, `mask` or `block`); rate limited and logged | A trigger on message inserts that masks or refuses |
| Lock or clear a chat room or forum thread | `chat.moderate` | the permission | Thread lock exists (`forum_threads.locked`); a `locked` flag for group chats and server channels; "clear" shows the count and needs the room name typed back |
| Edit a player's profile fields | `profiles.edit` (support and up) | the permission; finding 5's fix means a client cannot do it directly | A definer function with an allowed list (display name, bio, status, picture cleared) and before / after |
| View as player | `users.view_as` | the permission | Read only: the function runs the player's queries inside a read-only transaction with the player's id as the row-rule caller. No token, no session, no write is ever possible |
| Shut down or restart a listed game server; kick everyone from a room | `servers.control`, `rooms.kick` | the permissions | Needs our own server code: a command row the game server and the relay read on their heartbeat (no such channel today); until then not drawn |

**Row rules built in A2:** anyone (signed out too) reads `app_config` and `app_config_keys`; the controlled keys are changed
only by `admin_set_config(actor, key, value, confirm, ...)`, which only the service role can call (so the function checks the
second factor first): it refuses an unknown key, a value outside the key's pattern or over 300 characters, a `danger` key
without its name typed back, and anything `admin_gate_as` refuses; it logs before and after. Free keys (`voice_relay`,
`game_relay`) stay as before for `config.write` holders, now rate limited and logged. The word filter is readable by
`chat.moderate` / `chat.filter` holders only (players do not see the list).
