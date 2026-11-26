# OkayBackend audit: every place we talk to Supabase today

> **What this is:** the audit the owner asked for before any change (2026-11-26: "Audit all existing Supabase integrations
> before making changes"). It lists every place the code, the scripts, CI and the server functions talk to Supabase, what each
> one needs, and what already exists that OkayBackend can build on. The design is in [okay_backend.md](okay_backend.md).
>
> **Status:** Internal record. Read only: nothing in the programs was changed by this audit. Facts are from origin/main at
> `ab3b88647` and re-checked unchanged at `3a1135e5f` (2026-11-26). Re-run the call-site part with `python3 scripts/provenance_scan.py backend` (add `--summary` for
> the counts, `--tests` to include the tests).
>
> **Last checked:** 2026-11-26 (branch `claude/okay-backend-1`). Nothing was sent to the live Supabase project; the audit reads
> the code only.

## Contents

- [1. Summary](#1-summary)
- [2. What runs where today, and what it costs](#2-what-runs-where-today-and-what-it-costs)
- [3. Every call site, file by file](#3-every-call-site-file-by-file)
- [4. Tables, functions and buckets](#4-tables-functions-and-buckets)
- [5. The schema in docs/sql](#5-the-schema-in-docssql)
- [6. Server functions and the web page](#6-server-functions-and-the-web-page)
- [7. Scripts, CI, secrets and configuration names](#7-scripts-ci-secrets-and-configuration-names)
- [8. What already exists that OkayBackend builds on](#8-what-already-exists-that-okaybackend-builds-on)
- [9. Security findings](#9-security-findings)
- [10. Riskiest integrations](#10-riskiest-integrations)
- [11. Limits of this audit](#11-limits-of-this-audit)

## 1. Summary

- **330 call sites in 39 files** outside the tests (another 64 inside tests). A "call site" is one line that names a Supabase
  route (`/rest/v1`, `/auth/v1`, `/storage/v1`, `/functions/v1`, `/realtime/v1`, `api.supabase.com`) or calls the one helper
  that builds the releases address. Comments and made-up sample addresses are not counted.
- **Everything in the programs goes through one door.** Every C++ call site sends through `okay::account::AccountService`
  (`Api`, `ApiAnon`, `ApiUpload`, `ApiTimed`, all ending in `HttpRequest`, which calls `okay::http::Send`: WinHTTP on Windows,
  our own client elsewhere). No program has a second HTTP path to Supabase. That is the seam OkayBackend replaces.
- **But the routes are written by hand at each call site.** 236 database requests are PostgREST query strings typed out in
  20 headers (`"/rest/v1/messages?select=...&to=eq." + id`). The routes are Supabase's, so moving to another backend today
  means editing every one of them. That is the main gap.
- **Realtime is not used at all.** Chat typing, read marks and presence poll tables. Nothing opens a WebSocket.
- **The launcher is the big user** (287 of 330 sites reach it). The editor and the player use only sign-in, cloud saves,
  leaderboards, matchmaking, match results, the session ticket and the updater.
- **The schema is large and rule-heavy:** 24 SQL files, 52 tables, 189 row-level security policies, 117 SQL functions (103
  `security definer`), 44 triggers and 2 nightly `pg_cron` jobs. Any backend of our own has to give the same answers.

### Call sites per service

| Service (Supabase part) | Call sites | Names |
|---|---:|---|
| Database (PostgREST tables) | 236 | 47 tables and 1 view (section 4) |
| Storage | 40 | buckets `chat`, `workshop`, `releases`, `game-content` (writes and public reads) |
| Database functions (PostgREST RPC) | 30 | 26 named functions plus 4 built from a variable (section 4) |
| Auth (GoTrue) | 17 | `signup`, `token` (password and refresh), `user`, `recover`, `resend`, `verify`, `admin/users` |
| Management API (`api.supabase.com`) | 4 | `projects/<ref>/api-keys?reveal=true` only, in four publish scripts |
| Edge Functions | 3 | `delete-account`, `session-ticket` |
| Realtime | 0 | not used |
| **Total** | **330** | |

### Call sites per program

A site in a shared header counts once for every program that includes it, so the column adds up to more than 330.

| Program | Call sites | What it uses |
|---|---:|---|
| launcher (`OkaySpace.exe`) | 287 | everything: accounts, community (friends, chat, forums, servers, parties, profiles, workshop, admin), anti-cheat, crash upload, official game publish, updater |
| engine (linked into editor, player, sandbox) | 53 | matchmaking, match results, the queue matchmaker, account service |
| player (`OkaySpacePlayer.exe`) | 45 | account service, matchmaking, anti-cheat session ticket |
| editor (`OkayEngine.exe`) | 30 | account service (sign-in gate), matchmaking, updater |
| examples, sandbox | 29 each | the same headers, reached through the `Okay.hpp` umbrella include |
| scripts (release and crash tooling) | 24 | Storage uploads, crash reports, the management API |
| server functions (`supabase/functions`) | 9 | Auth, tables, RPC, Storage with the service role key |
| CI (`.github/workflows`) | 4 | public reads of the releases bucket (checksum checks) |
| tools | 1 | crash upload (through `shared/CrashUpload.hpp`) |
| web page (`supabase/auth-pages`) | 1 | Auth `PUT /auth/v1/user` (password reset) |
| relay, browser, web player | 0 | do not talk to Supabase |

### Which key each call needs

| Key | Where | Call sites |
|---|---|---:|
| None (public Storage URL) | updater, CI checks, chat and workshop download links, publish scripts' read-back | 17 |
| Anon (publishable) key only | sign-up, sign-in, password reset, resend, verify, username check, crash upload | 7 |
| Anon key + the player's access token (JWT) | everything else in the programs (every request also carries the anon key as `apikey`) | 276 |
| Anon key + a token, inside a server function or the reset page | the Edge Functions' "who is calling" check, the reset page | 4 |
| Service role key | publish scripts, `crash_to_issue.py`, both Edge Functions | 22 |
| Management token (`sbp_...`) | four publish scripts, only to fetch the service key when it is not given | 4 |

The key column in section 3 is worked out per line by the scan. One correction by hand: `shared/CrashUpload.hpp` builds a
request that the launcher sends with `ApiAnon` (`launcher/main.cpp:4956`), so crash upload needs only the anon key.

## 2. What runs where today, and what it costs

| Part | Runs on | Paid by |
|---|---|---|
| Database, Auth, Storage, Edge Functions | the owner's Supabase project (`config/public-account.json` names its address) | the owner's Supabase plan, as today |
| Password-reset page (`supabase/auth-pages/reset-password`) | a static page the owner hosts (`docs/online/password-reset.md`) | as today |
| Release publishing, crash-report sweep | GitHub Actions runners (`release.yml`, `dev-build.yml`, `crash-issues.yml`) and the owner's own machine (`scripts/release-local.sh`) | GitHub Actions minutes, as today |
| Relay (`relay/`) | wherever the owner runs it (not Supabase) | as today |
| SQL tests | a throw-away local Postgres, only when `OKAY_SQL_TEST_PGHOST` is set | nothing |

Nothing in this audit or in the design adds a hosted service. See [okay_backend.md, section 9](okay_backend.md#9-costs).

## 3. Every call site, file by file

Programs are worked out from the `#include` graph (who includes the header, directly or through other headers). Line
numbers are where the route text is; "..." means more lines, listed by the scan. Tests are the test files in `tests/` that
cover the file (`match_results` = `tests/test_match_results.cpp`); "parsers only" means the test checks how replies are read,
not which request is sent.

Error handling is the same for every C++ site: `ApiResponse` says `ok` (2xx), `reached` (false when the network failed),
`status`, and `maybeSent` (the request may have arrived, so it is never sent twice). A 401 makes `ApiTimed` re-read the
token the launcher saved and try once more. Community headers note a missing table (PostgREST codes `PGRST205`, `42P01`) and
show "not set up" instead of an error (`Messages::NoteTable`). Scripts use `curl -fsS` and stop on the first error;
the Edge Functions skip a step whose table does not exist.

| File | Lines | Sites | Programs | Service: names | Key | HTTP client | Tests |
|---|---|---:|---|---|---|---|---|
| `engine/include/okay/Net/MatchResults.hpp` | 124, 143, 159, 173 | 4 | engine, launcher | RPC: game_leaderboard, my_game_stats, recent_players, report_match | user JWT | okay::http::Send | match_results |
| `engine/include/okay/Net/Matchmaking.hpp` | 55, 75, 78, 95, 99, 121, 128, 136 | 8 | editor, engine, examples, launcher, player, sandbox | Database: game_sessions | user JWT | okay::http::Send | matchmaking |
| `engine/include/okay/Platform/Account/AccountData.hpp` | 26, 28, 182 | 3 | launcher | Database: (every table, from a list); Functions: delete-account | user JWT | okay::http::Send | account_data |
| `engine/include/okay/Platform/Account/AccountService.hpp` | 615, 626, 667, 671, 687, 719, 737, 888 ... | 21 | editor, engine, examples, launcher, player, sandbox | Auth: recover, resend, signup, token, user, verify; Database: cloud_saves, leaderboards, profiles; RPC: email_for_login | anon, user JWT | okay::http::Send | account, editor_signin_gate, own_http_net |
| `engine/include/okay/Platform/Admin.hpp` | 43, 57, 70, 73, 89, 91, 94, 100 ... | 18 | launcher | RPC: admin_stats, my_role_rank; Database: admin_log, app_config, profile_badges, profiles, site_bans | user JWT | okay::http::Send | none |
| `engine/include/okay/Platform/BlockList.hpp` | 36, 52, 59, 67, 75, 95 | 6 | launcher | Database: blocks, privacy_settings, profiles | user JWT | okay::http::Send | blocks_privacy |
| `engine/include/okay/Platform/Forums.hpp` | 49, 51, 77, 93, 96, 104, 115, 125 ... | 37 | launcher | Database: forum_posts, forum_section_bans, forum_section_members, forum_sections, forum_threads, forum_votes, profile_badges, profiles; RPC: forum_section_counts, my_role_rank | user JWT | okay::http::Send | chat_json, public_name (parsers only) |
| `engine/include/okay/Platform/Friends.hpp` | 95, 99, 113, 120, 127, 158, 162, 170 ... | 19 | launcher | Database: activity, friendships, profiles | user JWT | okay::http::Send | friends_json, public_name (parsers only) |
| `engine/include/okay/Platform/Invites.hpp` | 253, 320, 328 | 3 | launcher | Database: game_invites; RPC: (from a variable) | user JWT | okay::http::Send | chat_invites |
| `engine/include/okay/Platform/Messages.hpp` | 62, 83, 109, 121, 135, 154, 177, 178 ... | 55 | launcher | Database: group_convos, group_members, group_messages, message_edits, message_pins, message_reactions, message_reads, messages, profiles, typing_state; RPC: add_group_member, create_group, my_groups; Storage: chat (upload, list, delete, public links) | none (public URL), user JWT | okay::http::Send | chat_invites, chat_json, chat_receipts |
| `engine/include/okay/Platform/Parties.hpp` | 53, 58, 65, 107, 113, 124, 149 | 7 | launcher | Database: parties, party_members, profiles; RPC: (from a variable) | user JWT | okay::http::Send | launcher_join |
| `engine/include/okay/Platform/Privacy.hpp` | 182, 187 | 2 | launcher | Database: profiles, profiles_visible | user JWT | okay::http::Send | chat_invites |
| `engine/include/okay/Platform/Profiles.hpp` | 89, 143, 145, 159, 172, 174, 183, 189 ... | 27 | launcher | Database: activity, follows, friendships, profile_badges, profiles, user_achievements, user_playtime, workshop_items; RPC: follow_counts, profile_friend_count | user JWT | okay::http::Send | none |
| `engine/include/okay/Platform/Servers.hpp` | 35, 52, 61, 71, 81, 96, 134, 141 ... | 19 | launcher | Database: channel_messages, channels, message_reactions, profiles, server_members, servers; RPC: create_server, join_server | user JWT | okay::http::Send | chat_json (parsers only) |
| `engine/include/okay/Platform/Workshop.hpp` | 69, 73, 78, 83, 93, 134, 148, 156 ... | 18 | launcher | Database: profiles, workshop_items, workshop_reviews; Storage: workshop (upload, public links) | none (public URL), user JWT | okay::http::Send | chat_json, public_name (parsers only) |
| `engine/src/Net/Matchmaker.cpp` | 60, 72, 91, 94 | 4 | engine | RPC: queue_host_ready, queue_join, queue_leave, queue_poll | user JWT | okay::http::Send | matchmaker, net_races |
| `shared/AntiCheatAdminTools.hpp` | 26, 65, 86 | 3 | launcher | RPC: ac_ban_with_devices, ac_linked_accounts; Database: ac_reports | user JWT | okay::http::Send | anticheat_wiring |
| `shared/AntiCheatBackend.hpp` | 21, 156, 210, 221, 232, 242, 253, 270 ... | 16 | engine, launcher, player | Database: ac_appeals, ac_bans, ac_reports, profiles; RPC: ac_file_appeal, ac_my_bans; Functions: session-ticket | user JWT | okay::http::Send | anticheat_backend, ticket_client |
| `shared/CrashUpload.hpp` | 49 | 1 | launcher, tools | Database: crash_reports (insert only) | anon (sent with `ApiAnon`) | okay::http::Send | crash_upload |
| `shared/MemberModeration.hpp` | 87, 110 | 2 | launcher | RPC: ban_member, kick_member, set_member_role, unban_member (from a variable); Database: community_bans | user JWT | okay::http::Send | member_moderation |
| `shared/UserReports.hpp` | 82, 152, 164 | 3 | launcher | Database: reports | user JWT | okay::http::Send | user_reports |
| `editor/Updater.cpp` | 77 | 1 | editor | Storage: public releases bucket | none (public URL) | okay::http::Send | startup_update |
| `launcher/AdminPage.cpp` | 803 | 1 | launcher | Storage: public workshop links (recognises them) | none (public URL) | okay::http::Send | none |
| `launcher/ChatText.cpp` | 461 | 1 | launcher | Storage: recognises chat file links | none | - | none |
| `launcher/MemberModerationUI.cpp` | 40 | 1 | launcher | Database: profiles | user JWT | okay::http::Send | member_moderation (model only) |
| `launcher/VersionsManager.cpp` | 201 | 1 | launcher, editor | Storage: public releases bucket (`ReleasesBucketBase`, the one builder of the updater address) | none (public URL) | okay::http::Send | launcher_versions, okayui_port_versions |
| `launcher/main.cpp` | 818, 6426, 10171, 10175, 11768, 11773, 11790, 11827 ... | 11 | launcher | Storage: releases (official game publish: upload, list, delete, public read), workshop links; Database: profiles, workshop_items; RPC: log_official_publish | none (public URL), user JWT | okay::http::Send | none (launcher UI) |
| `supabase/auth-pages/reset-password/index.html` | 102 | 1 | web page | Auth: user (`PUT`, new password) | anon + recovery JWT | fetch | test_password_reset_page.js, test_password_reset_page_e2e.js |
| `supabase/functions/delete-account/index.ts` | 28, 34, 43, 61, 80, 88 | 6 | server function | Auth: user, token (password check), admin/users (delete); Database: every table in its step list; Storage: chat, workshop | anon + user JWT, service role | fetch | delete_account_test.ts (logic, with fakes) |
| `supabase/functions/session-ticket/index.ts` | 53, 68, 88 | 3 | server function | Auth: user; RPC: ac_is_banned, ac_note_device | anon + user JWT, service role | fetch | ticket_test.ts (signing only) |
| `.github/workflows/dev-build.yml` | 147 | 1 | CI | Storage: public releases bucket (read back) | none (public URL) | curl | test_dev_workflow.py |
| `.github/workflows/fast-release.yml` | 100, 282 | 2 | CI | Storage: public releases bucket (read back) | none (public URL) | curl | test_fast_release.py |
| `.github/workflows/release.yml` | 410 | 1 | CI | Storage: public releases bucket (checksum check) | none (public URL) | curl | none |
| `scripts/crash_to_issue.py` | 728, 738, 746, 750, 765 | 5 | script (CI `crash-issues.yml`) | Database: crash_reports (read, mark processed, delete) | service role | urllib | test_crash_to_issue.py, test_crash_to_issue_http.py |
| `scripts/publish-content.sh` | 76, 91, 98, 106 | 4 | script | Storage: game-content (make bucket, upload, read back); Management API: api-keys | service role, management token, none | curl | test_publish_content.sh |
| `scripts/publish-official-game.sh` | 84, 163, 177, 194, 209, 229 | 6 | script | Storage: releases (make bucket, upload, list, delete, read back); Management API: api-keys | service role, management token, none | curl | test_publish_official_cleanup.sh |
| `scripts/publish-supabase-dev.sh` | 176 | 1 | script (CI `dev-build.yml`) | Management API: api-keys (its uploads reuse `publish-supabase.sh`) | management token | curl | test_dev_publish.sh |
| `scripts/publish-supabase-downloads.sh` | 45, 53 | 2 | script (CI `release.yml`) | Storage: releases (upload, read back) | service role, none | curl | test_publish_supabase_checks.sh |
| `scripts/publish-supabase.sh` | 96, 125, 144, 153, 175, 248 | 6 | script (CI `release.yml`) | Storage: releases (make bucket, upload, list, read back); Management API: api-keys | service role, management token, none | curl | test_publish_supabase_checks.sh |

The full line-by-line list (330 rows, with the route each line names) is the output of `python3 scripts/provenance_scan.py
backend`. It is not pasted here because it goes stale with every edit; the scan is the record.

## 4. Tables, functions and buckets

**Tables the programs read or write (47, plus the view `profiles_visible`):** ac_appeals, ac_bans, ac_reports, activity,
admin_log, app_config, blocks, channel_messages, channels, cloud_saves, community_bans, crash_reports, follows, forum_posts,
forum_section_bans, forum_section_members, forum_sections, forum_threads, forum_votes, friendships, game_invites,
game_sessions, group_convos, group_members, group_messages, leaderboards, message_edits, message_pins, message_reactions,
message_reads, messages, parties, party_members, privacy_settings, profile_badges, profiles, reports, server_members,
servers, site_bans, typing_state, user_achievements, user_playtime, workshop_items, workshop_reviews.

- `cloud_saves` and `leaderboards` are the only two not created in `docs/sql/`: their SQL is in
  `docs/online/supabase-backend.md`. A backend of our own needs one place for every table.
- Tables in `docs/sql/` that no program calls directly (server-side only, filled by triggers or functions): ac_audit,
  ac_devices, ac_rate_events, ac_report_servers, match_assignments, match_players, match_tickets, matches, player_stats.

**Database functions called over RPC (26 named):** ac_ban_with_devices, ac_file_appeal, ac_is_banned, ac_linked_accounts,
ac_my_bans, ac_note_device, add_group_member, admin_stats, create_group, create_server, email_for_login, follow_counts,
forum_section_counts, game_leaderboard, join_server, log_official_publish, my_game_stats, my_groups, my_role_rank,
profile_friend_count, queue_host_ready, queue_join, queue_leave, queue_poll, recent_players, report_match. Built from a
variable: ban_member, kick_member, set_member_role, unban_member (`MemberModeration`), and the invite and party helpers.
Every RPC the code calls is defined in `docs/sql/`.

**Storage buckets:**

| Bucket | Public read | Written by | Rules in |
|---|---|---|---|
| `releases` | yes | publish scripts (service role); `official/` by admins from the launcher (user JWT) | `docs/sql/official_game_admin.sql` (admin-only list, upload, replace, delete under `official/`) |
| `chat` | yes | players (attachments, voice notes), under their own id folder | `docs/sql/chat_media.sql` |
| `workshop` | yes | players (listing files), under their own id folder | `docs/sql/social.sql` |
| `game-content` | yes | `scripts/publish-content.sh` (service role) | made by the script; no SQL rules |

**Auth endpoints:** `POST /auth/v1/signup`, `POST /auth/v1/token?grant_type=password`, `POST /auth/v1/token?grant_type=refresh_token`,
`GET /auth/v1/user`, `PUT /auth/v1/user` (change email, username in metadata, password), `POST /auth/v1/recover`,
`POST /auth/v1/resend`, `POST /auth/v1/verify` (recovery code), `DELETE /auth/v1/admin/users/<id>` (Edge Function only).
The programs read two claims from the access token: `sub` (the account id) and `exp` (when it ends).
Sign-in by username first calls the RPC `email_for_login`.

## 5. The schema in docs/sql

| File | Tables | RLS on | Policies | Functions | Triggers | Buckets |
|---|---:|---:|---:|---:|---:|---:|
| admin.sql | 2 | 2 | 15 | 14 | 4 | 0 |
| anticheat.sql | 7 | 7 | 14 | 17 | 13 | 0 |
| app_config.sql | 1 | 1 | 4 | 1 | 1 | 0 |
| blocks.sql | 2 | 2 | 10 | 9 | 3 | 0 |
| chat_extras.sql | 2 | 2 | 5 | 0 | 0 | 0 |
| chat_invites.sql | 1 | 1 | 5 | 7 | 4 | 0 |
| chat_media.sql | 0 | 0 | 4 | 0 | 0 | 2 |
| chat_receipts.sql | 2 | 2 | 8 | 1 | 0 | 0 |
| crash_reports.sql | 1 | 1 | 1 | 1 | 1 | 0 |
| follows.sql | 1 | 1 | 4 | 1 | 0 | 0 |
| forum_sections.sql | 3 | 3 | 19 | 9 | 2 | 0 |
| game_servers.sql | 1 | 1 | 4 | 1 | 1 | 0 |
| group_chats.sql | 3 | 3 | 11 | 4 | 0 | 0 |
| matches.sql | 3 | 3 | 3 | 4 | 0 | 0 |
| matchmaking.sql | 2 | 2 | 0 | 6 | 0 | 0 |
| member_moderation.sql | 1 | 1 | 9 | 17 | 8 | 0 |
| official_game_admin.sql | 0 | 0 | 4 | 2 | 0 | 0 |
| parties.sql | 2 | 2 | 4 | 7 | 1 | 0 |
| profile_extras.sql | 2 | 2 | 5 | 1 | 1 | 0 |
| profile_showcase.sql | 1 | 1 | 4 | 1 | 0 | 0 |
| public_names.sql | 0 | 0 | 0 | 3 | 2 | 0 |
| reactions_privacy.sql | 0 | 0 | 2 | 3 | 1 | 0 |
| reports.sql | 1 | 1 | 4 | 1 | 1 | 0 |
| social.sql | 14 | 14 | 50 | 7 | 1 | 3 |
| **total** | **52** | **52** | **189** | **117** | **44** | **5** |

(Counts are text matches of `create table`, `enable row level security`, `create policy`, `create function`, `create trigger`
and `storage.buckets`; a function that is re-created in two files counts twice.)

What the schema relies on from Supabase, which any other backend must provide:

- `auth.uid()`, `auth.role()`, `auth.jwt()` inside policies and functions (the caller's id and role from the token), and the
  `auth.users` table (foreign keys `references auth.users on delete cascade`).
- Three roles: `anon`, `authenticated`, `service_role` (which skips row-level security).
- `security definer` functions (run with the owner's rights; 103 matches), so a caller can do one checked thing it could not
  do on the table itself.
- Triggers that keep the data honest (best score only, `updated_at`, public names, moderation logs).
- `pg_cron` for two nightly jobs: `okay-ac-retention` (`ac_purge_old()`) and `okay-ac-audit-retention` (anticheat.sql:720).
  When `pg_cron` is missing they are run by hand.
- Storage rules written as policies on `storage.objects` (bucket id plus the first folder of the path being the caller's id,
  or an admin check).

How it is tested today: `tests/scripts/test_*_sql.sh` load the SQL into a throw-away local Postgres with
`tests/sql/supabase_stub.sql` (our own stand-in for the `auth` schema and the three roles) and run check files
(`tests/sql/test_*.sql`). They are skipped when `OKAY_SQL_TEST_PGHOST` is not set. Nine such tests load 16 of the 24 files and check nine areas
(anti-cheat, blocks, chat invites, read receipts, crash reports, member moderation, public names, reactions and privacy,
reports).

## 6. Server functions and the web page

| Part | What it does | Supabase it uses | Settings (names only) | Tests |
|---|---|---|---|---|
| `supabase/functions/delete-account` | Deletes or anonymises the caller's rows in every table, empties their `chat/` and `workshop/` folders, deletes the auth user. Checks the password first. | Auth (`user`, `token`, `admin/users`), PostgREST with the service key, Storage list and delete | `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `SUPABASE_SERVICE_ROLE_KEY` (given by the Edge runtime) | `delete_account_test.ts` (all steps, with fakes; CI `delete_account_function`) |
| `supabase/functions/session-ticket` | Gives a signed-in, not-banned player a short Ed25519-signed ticket that a game server checks on join. | Auth (`user`), RPC `ac_is_banned`, `ac_note_device` with the service key | `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `SUPABASE_SERVICE_ROLE_KEY`, `OKAY_TICKET_SEED`, `OKAY_TICKET_PUBLIC_KEY`, `OKAY_TICKET_TTL` | `ticket_test.ts` (signing, checked against Web Crypto in CI) |
| `supabase/auth-pages/reset-password` | The page the reset email links to: reads the recovery token from the link and sets the new password. | Auth `PUT /auth/v1/user` | `OKAY_RESET_CONFIG` (project address and anon key, filled in when deployed) | `test_password_reset_page.js`, `test_password_reset_page_e2e.js` |

Both functions are written in TypeScript for Deno by us. Neither is deployed or run against the live project by CI.

## 7. Scripts, CI, secrets and configuration names

Names only. No value was read, printed or copied.

**GitHub secrets the workflows use:** `SUPABASE_SERVICE_KEY` (the service role key: `release.yml`, `dev-build.yml`,
`crash-issues.yml`), `OKAY_UPDATE_SIGNING_SEED` (signs `versions.json`; not a Supabase key), `WINDOWS_SIGN_PFX_BASE64` and
`WINDOWS_SIGN_PFX_PASSWORD` (code signing; not Supabase). **GitHub variable:** `OKAY_DEFAULT_ACCOUNT_URL` (the project
address; falls back to `config/public-account.json`).

**Environment the scripts read:** `SUPABASE_URL`, `SUPABASE_SERVICE_KEY`, `SUPABASE_ACCESS_TOKEN` (a management token; only used
when the service key is not given, to fetch it from `api.supabase.com`), `BUCKET`, `OKAY_UPDATE_SIGNING_SEED`,
`OKAY_UPDATE_TRUSTED_KEYS`, `OKAY_CONTENT_SIGNING_SEED`, `OKAY_PUBLIC_ACCOUNT_FILE`.

**What the programs read (client configuration):**

| Name | Kind | Meaning |
|---|---|---|
| `OKAY_ACCOUNT_SERVER` | environment variable, or `account_server.txt` next to the program | the backend address (overrides the built-in one) |
| `OKAY_ACCOUNT_API_KEY` | environment variable, or `account_apikey.txt` | the publishable (anon) key |
| `OKAY_DEFAULT_ACCOUNT_URL`, `OKAY_DEFAULT_ACCOUNT_KEY` | CMake values, from `config/public-account.json` | the address and publishable key built into every program |
| `OKAY_UPDATE_SERVER` | environment variable | overrides the releases bucket address (testing) |
| `session.txt` in the config folder | file | the saved session, shared by the launcher and the editor |

The account service picks its provider from these (`Provider::Auto`): no address means on-device accounts; an address and a
key means Supabase; an address and no key means a "custom" server (the Python reference server
`examples/account-server/server.py`).

**Publish flow (unchanged by OkayBackend for now):** `release.yml` runs `scripts/publish-supabase.sh` and
`scripts/publish-supabase-downloads.sh` with the service key; `dev-build.yml` runs `scripts/publish-supabase-dev.sh`;
`crash-issues.yml` runs `scripts/crash_to_issue.py` daily; `fast-release.yml` only reads the public bucket. A release is
"published" when `versions.json` in the `releases` bucket has its checksums (CLAUDE.md rule 13).

## 8. What already exists that OkayBackend builds on

| Existing piece | Where | What it already does | Gap for OkayBackend |
|---|---|---|---|
| Account service | `engine/include/okay/Platform/Account/AccountService.hpp` | One client for every program; providers `Local` (on-device accounts, PBKDF2), `Custom` (reference server), `Supabase`; `Api`/`ApiAnon`/`ApiUpload`/`ApiTimed`; token refresh in one place (the launcher); `SetTransportForTest` to record requests | The provider only changes sign-in; every other call writes Supabase routes by hand. No typed database, storage or function calls |
| Account facade | `engine/include/okay/Platform/Account/Account.hpp`, `engine/src/Platform/Account/Account.cpp` | Process-wide `okay::Account` for editor, player and scripts | none (stays; it sits on top) |
| Online services interface | `engine/include/okay/Net/NetBackend.hpp` (`INetBackend`, `NetBackend`) | Game-facing calls (sign-in, cloud saves, leaderboards, sessions) with providers Native, PlayFab (stub), Custom (stub) | Game level only; stays the game-facing facade and moves onto OkayBackend underneath |
| HTTP seam | `engine/include/okay/Platform/NativeFetch.hpp` (`okay::http::Send`), `OwnHttp*.hpp`, `NativeHttp.hpp` | One client: WinHTTP on Windows, our own HTTP/1.1 client elsewhere | none (OkayBackend's HTTP transport) |
| Request builders as data | `shared/TicketClient.hpp`, `shared/AntiCheatBackend.hpp`, `shared/CrashUpload.hpp`, `AccountData.hpp`, `shared/MemberModeration.hpp` | Build a request struct (method, path, body, header) that tests can compare | Already the right shape: OkayBackend makes every call site work this way |
| Reference account server | `examples/account-server/server.py` (Python) | `/register`, `/login`, cloud saves and leaderboards, rate limits, token expiry, versioned password hashing | Python and partial; the Local backend replaces it (then it is deleted) |
| Local web server | `editor/WebRun.hpp` / `.cpp` (`PreviewServer`) | Our own non-blocking HTTP/1.1 server on 127.0.0.1, written from RFC 9112, GET and HEAD | Only GET/HEAD, one answer per connection, lives in `editor/`. The Local backend's server starts from it |
| Relay | `relay/main.cpp`, `engine/include/okay/Net/RelayServer.hpp` | A small server program of our own (UDP) | Shows the shape for a second small server program; not reused for HTTP |
| JSON | `engine/include/okay/Core/Json.hpp` (`okay::json`, RFC 8259 reader) | Strict reader with limits | The community headers still use their own small parsers |
| libsodium wrapper | `engine/include/okay/Net/NetCrypto.hpp` | Key exchange, AEAD, Ed25519 signatures through libsodium | Needs password hashing (`crypto_pwhash`) and an HMAC or signature for tokens, through libsodium (rule 18) |
| Fakes for tests | `tests/scripts/fake_storage.py`, `tests/sql/supabase_stub.sql`, `tests/scripts/sql_test_lib.sh` | A fake Storage server, a local `auth` schema stand-in, a local Postgres harness | Become the oracle for the contract tests |

## 9. Security findings

1. **The publishable key is committed** in `config/public-account.json:3` and built into every program. It is a
   `sb_publishable_` key, which Supabase designs to be public; the data is protected by row-level security. Not a leak.
2. **The same real publishable key is copied into a test**, `tests/test_crash_scrub.cpp:79` (a fixture for the crash-report
   scrubber). Low risk because the key is public, but a test should use a made-up value so a key change does not touch
   tests and so the real key is in one file only. Not changed here (docs-only round).
3. **Values shaped like secret keys (`sb_secret_...`) are in three tests:** `tests/scripts/test_crash_to_issue.py:701`,
   `tests/test_crash_handler.cpp:67`, `tests/scripts/test_crash_to_issue_http.py:31`. They sit in test setup and look made up,
   but this audit cannot prove that without the real key, which it did not read. **Owner: please confirm none of them is the
   project's real service key; if one is, rotate the key.** Values were not printed.
4. **The service role key does release uploads.** CI uses `SUPABASE_SERVICE_KEY`, which skips every row rule in the whole
   database, just to upload files to two buckets. A leak of that one secret is a full-database leak.
5. **Four publish scripts can fetch the service key** with a management token (`api-keys?reveal=true`). A management token
   controls the whole Supabase account, so it should only ever be typed on the owner's own machine, never stored in CI (it is
   not today).
6. **Printing keys:** `scripts/publish-supabase.sh` passes the key only in curl header arguments and does not echo it;
   the crash scrubber removes `sb_` keys and JWTs from reports (`tests/test_crash_scrub.cpp`); `crash_to_issue.py` has a test
   that it never prints the key. The other scripts were not read line by line for this.
7. Git history was not scanned for old secrets (too slow on this machine); only the current tree was.

## 10. Riskiest integrations

| Rank | Integration | Why it is risky to move |
|---|---|---|
| 1 | Updater and release publishing (`ReleasesBucketBase`, `publish-supabase*.sh`, `release.yml`) | Every installed launcher has the releases address built in. If it moves, old launchers stop updating. Checksums must keep working (rule 13). Leave it on Supabase; only wrap it. |
| 2 | Sign-in and token refresh (`AccountService`, launcher, editor gate) | A mistake signs every player out or locks the editor. Supabase rotates refresh tokens, so two refreshers can revoke a session (the code already refreshes in one place only). |
| 3 | Delete account and export data (`delete-account`, `AccountData`) | Must reach every table; a table added later and missed means data kept after "delete". |
| 4 | Row-level rules (189 policies, 103 security-definer matches) | A backend of our own that gets one rule wrong shows private messages or lets someone edit another's data. |
| 5 | Admin and moderation (`Admin.hpp`, anti-cheat admin tools, member moderation, `official/` uploads) | Power actions; their checks live in SQL functions. |
| 6 | Session ticket (anti-cheat join gate) | Game servers trust its signature; the signing key lives only in the Edge Function settings. |
| 7 | Community headers (Messages, Forums, Profiles, Servers, Friends) | Lowest risk per call but the most calls (180+), and three of them have no test that checks the request sent. |

## 11. Limits of this audit

- The scan finds route text. A route built from pieces held in different variables would be missed; the five "from a variable"
  RPC sites above were checked by hand. Programs come from the `#include` graph, not from what each program's CMake links.
- The SQL counts are text matches, not a parse.
- Nothing was run against Supabase, so whether the live project matches `docs/sql/` is not known. `cloud_saves` and
  `leaderboards` exist only in a doc, so the live project may differ from the repository.
- Licence names in the design page are from memory of the projects' published licences and were not re-checked today.
