# Kick, ban and admins in community servers and group chats

The Members list of a community server (right side of the Servers page) and of a group chat
(group menu, Members) now has moderation. It works only after the owner applies one SQL file.
Until then the buttons answer "Moderation isn't set up on this project yet" instead of failing silently.

## What players see

| Who | Can do |
|---|---|
| Owner | Remove, Ban, Unban, Make admin / Remove admin on anyone except themselves |
| Admin | Remove, Ban, Unban on plain members only |
| Member | nothing (no buttons, the Banned list is hidden) |

- Each row an actor may act on has a "..." button (servers also: right-click the row). The menu shows only what is allowed on that person.
- **Remove** asks for confirmation. The person can come back with an invite (server) or be re-added (group).
- **Ban...** asks for an optional reason (300 characters, shown to the banned person when they try to rejoin) and a length: 1 hour, 1 day, 7 days, 30 days or permanent. A ban removes the person at once; they cannot rejoin, be added, read or post until it ends or is lifted.
- **Banned** list (owner and admins, bottom of the members list): who, why, when it ends, and an **Unban** button.
- Every action shows a toast with the real answer: done, "Refused by the server: <reason>", "Couldn't reach the server", or "Moderation isn't set up on this project yet: docs/sql/member_moderation.sql has not been applied".
- Not available in the offline demo data (there is no server to ask).

Rules, enforced by the database (the launcher only hides buttons): owner > admin > member; nobody acts on an equal or higher role or on themselves; the owner can never be removed or banned; only the owner changes roles. Kicks, bans, unbans and role changes are written to `admin_log` when `admin.sql` is applied (site staff read it on the Admin page).

## Owner apply steps (do this yourself; nothing here was run against a live project)

1. Make sure `social.sql` and `group_chats.sql` are already applied (and `admin.sql` if you want the audit rows).
2. Supabase dashboard > SQL Editor > New query.
3. Paste the whole of `docs/sql/member_moderation.sql` and Run. It is idempotent: running it again is safe.
4. Check it: `select count(*) from public.community_bans;` returns 0, and `select proname from pg_proc where proname in ('kick_member','ban_member','unban_member','set_member_role');` returns four rows.
5. Open the launcher, a server you own, Members: each member now has "..." and a "Banned" section appears. Try Ban on a test account, then Unban.
6. To undo: `drop table public.community_bans cascade;` and re-run `social.sql` and `group_chats.sql` (this restores the old owner-only kick policies and drops nothing else). Existing servers and groups are untouched.

What the script changes in existing tables: adds `group_members.role`, tightens the `server_members` / `group_members` delete and insert policies (a member can no longer write their own role), replaces `is_server_member`, `is_group_member`, `join_server` and `add_group_member` with ban-aware versions. Re-running `social.sql` or `group_chats.sql` afterwards puts the old versions back, so apply `member_moderation.sql` last.

## Tests

- `tests/test_member_moderation.cpp` (`member_moderation`): every role pair, request building, error mapping, banned list parsing.
- `tests/scripts/test_member_moderation_sql.sh` (`member_moderation_sql`): runs the SQL on a throwaway LOCAL Postgres (never Supabase), twice for idempotence, then 100+ checks: every role pair on both a server and a group chat, direct table writes, banned rejoin / post / read, expiry, bystander, audit rows, cleanup. Skipped unless `OKAY_SQL_TEST_PGHOST` names a local Postgres, e.g. `OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGUSER=postgres bash tests/scripts/test_member_moderation_sql.sh`.

## Not done

Mute, invite expiry or revoke, per-channel permissions, a moderator role between member and admin, admins adding group members (still owner only), and a ban that survives deleting your account elsewhere. Banned people are not told they were removed until they try to rejoin.
