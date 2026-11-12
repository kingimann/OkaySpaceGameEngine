# Download my data and Delete my account

Launcher: **Account > Sign-in & security > Your data** (online Supabase accounts only).

## Download my data

Saves one JSON file to the `exports/` folder next to the launcher
(`okayspace-data-<UTC time>.json`). The launcher reads each table with the
signed-in user's own token and a `...=eq.<my id>` filter, so Supabase row-level
security decides what comes back: only rows you may legitimately read.

Sections: profile, friendships, follows, messages you sent, group messages you
sent, server channel messages, reactions, forum threads, posts and votes,
marketplace listings and reviews, achievements, playtime, badges, activity,
servers and group chats you own or belong to, forum sections you own, cloud
saves, leaderboard entries, player stats, anti-cheat appeals, people you
blocked, user reports you filed, your privacy settings, and community bans placed on you.

Honest edges:

- A section whose table was never created on your server is listed under
  `not_set_up_on_this_server` (not an error).
- A section that could not be read is listed under `failed_sections` and the
  launcher says the export is INCOMPLETE.
- Anti-cheat **reports you filed** are readable only by admins (see
  `docs/sql/anticheat.sql`), so they are not in the export (they are deleted with
  the account).
- Your email lives in the sign-in service and is shown on the Account page.
  Launcher settings live in `launcher.cfg` on your PC.

## Delete my account

1. Press **Delete my account...**, type your username, type your password, tick
   the box. The button stays disabled until all three are right.
2. The launcher POSTs `{"confirm_username","password"}` to
   `/functions/v1/delete-account` with your access token. **The user id is taken
   only from the token**; a user id in the body is never read.
3. The function re-checks the password (a fresh sign-in against Supabase Auth),
   checks the typed name matches your profile, then runs the steps below in
   order, stopping at the first failure and reporting its name.
4. On success the launcher signs you out. On failure it says which step failed
   and that your account still exists (retry is safe: every step can repeat). If
   the function is not deployed it says so and that nothing was deleted.

An account that is site-banned cannot delete itself (that would lift the ban);
the function answers 403.

### Deleted vs anonymised

Every table in `docs/sql/*.sql` and `docs/supabase-backend.md` that references
`auth.users` is covered (a test fails if a new one is added without a step).

**Anonymised (row kept, link to you removed):**

| Table | What happens |
|---|---|
| `admin_log` | `actor`/`target` set to null, names set to `[deleted]` |
| `ac_bans` | `user_id` set to null; machine-hash bans stay. Account-only bans (no hash) are deleted |
| `matches` | `reporter` set to null |
| `community_bans`, `ac_bans`, `forum_section_bans` | `banned_by` set to null (the ban stays, the moderator link goes) |
| `ac_report_servers` | `added_by` set to null |
| `reports` | `reviewed_by` set to null (the moderator's review stays, the link to the reviewer goes) |

**Deleted:** `message_reactions`, `forum_votes`, `forum_posts`, `forum_threads`,
`forum_section_members`, `forum_section_bans`, `forum_sections`,
`workshop_reviews`, `workshop_items`, `channel_messages`, `server_members`,
`servers`, `group_messages`, `group_members`, `group_convos`, `message_edits`,
`community_bans` (bans placed on you), `blocks` (both directions),
`privacy_settings`, `typing_state`, `message_reads`, `message_pins`, `messages` (sent and received), `friendships`, `follows`,
`activity`, `user_achievements`, `user_playtime`, `profile_badges`,
`cloud_saves`, `leaderboards`, `player_stats`, `match_players`, `match_tickets`,
`party_members`, `parties`, `game_sessions`, `ac_reports` (filed by or about
you), `reports` (user reports you filed, and reports about you or your content;
their snapshot holds your words), `ac_appeals`, `ac_report_servers`, `profiles`; every file under
`<your id>/` in the `chat` and `workshop` storage buckets; then the auth user.

**Consequences the launcher warns about:** forum threads you started disappear
with the replies of others; servers and group chats you own disappear for every
member; DMs you sent disappear from the other person's history too.

**Not possible with the current schema:** replacing your messages with
`[deleted]` for other people's history. `messages.sender`, `group_messages.sender`,
`forum_*.author` and similar are `not null ... on delete cascade`. Keeping a
tombstone needs a schema change (make those columns nullable with
`on delete set null`, plus UI for a null author) and a Postgres test; it was
not done here.

Opaque ids that staff left in other rows (`banned_by`, `reviewed_by`, ...) are
not touched; once the account is gone they point at nobody.

## Owner: deploy steps (nothing here has been run)

1. Install the Supabase CLI and `supabase login`; `supabase link --project-ref <ref>`.
2. `supabase functions deploy delete-account` (keep JWT verification on, the
   default). `SUPABASE_URL`, `SUPABASE_ANON_KEY` and `SUPABASE_SERVICE_ROLE_KEY`
   are provided by the Edge runtime; set no secrets. The service key never
   leaves the function.
3. Run the SQL files in `docs/sql/` you actually use (tables that do not exist
   are skipped).
4. Test with a throwaway account first: create one, press Delete, check the
   rows and the auth user are gone. Check a wrong password gives 403 and no
   change.
5. Until step 2 is done the launcher shows "the delete-account function is not
   deployed" and deletes nothing.

Tests: `ctest -R account_data` (client logic), `ctest -R delete_account_function`
or `node --experimental-strip-types --test supabase/functions/delete-account/delete_account_test.ts`
(function logic against a fake client; also runs under `deno test`).
