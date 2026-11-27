# Security (plan SEC1)

> **What this is:** Five protections, one section each: what was wrong, what changed, how you see it working, what is not done.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `online/`, paths and links checked; the text itself was not re-checked against the code).

Five protections, one section each: what was wrong, what changed, how you see it working, what is not done. Every claim
about a result says what was run. Nothing here ran on Windows, and nothing ran against the real Supabase project: those
two are unverified and are listed under "Your steps" and at the end.

Related pages: [relay](relay.md), [updater](../release/updater.md), [anticheat](anticheat.md#anti-cheat-1), [crypto](../platform/crypto.md).

## 1. Relay isolation

The relay (`relay/main.cpp`, `engine/src/Net/RelayServer.cpp`) forwards game packets between a session's host and its other
peers. It was already strict: packets only travel host to client inside one session, slots are random, hellos need a cookie,
the host key protects a code, and peers, sessions, codes, hello rate and per-peer bytes are capped. A read of the code for the
remaining gaps found two, each reproduced by a test that **failed before the fix** (`relay_security`):

| Gap | Test that failed first | Fix |
|---|---|---|
| The table that rate-limits hellos by address holds 65,536 rows. About 70,000 forged hellos (one datagram each, from made-up source addresses) filled it, and then **every new address was refused**, so nobody could join until the rows aged out. Each hello while full also re-scanned the whole table (a CPU cost for the attacker's price of one packet). | `relay.SlotOf(real) != 0` after 70,000 forged hellos | Rows that are back to a full allowance are forgotten exactly (no loss), the table is pruned at most four times a second, and addresses it has no room for share one global allowance (`kUntrackedHellosPerSecond` = 2,000/s) instead of all being refused. |
| Forwarding was capped per peer and per receiver only. One address could hold 64 peers in 16 sessions and push about 60 MB at once; one session of 31 clients could move 54 MB at once. Nothing capped an address or a room as a whole. | `relay.DroppedRate() > 0` (twice) | `kAddressBytesPerSecond` = 8 MiB/s per IP address (all its peers and sessions together; loopback exempt) and `kSessionBytesPerSecond` = 16 MiB/s per session, each with a two-second burst. A LAN party or a 32-player game is far below both. |

Already fine (read, and covered by existing tests): another session's slot cannot be reached (`Packets only travel inside a session`),
slots are random, a spoofed source cannot fill anyone's quota (cookie), oversize and malformed datagrams (10,000 random ones),
log forging (`RelayLogSafe`), memory bounds (sessions, peers, per-address peers and sessions).

**How to see it:** `ctest -R relay_security` (Linux build). The three new blocks are at the end of `tests/test_relay_security.cpp`.
The relay is a separate Linux program (`okayspace-relay`), not part of the Windows zip, so there is nothing to click.

**Not done, on purpose or for lack of a fix that does not break old games:**
- Session codes are chosen by the game, not the relay. A short code (`"1"`) is guessable, and a client that guesses a live code
  is welcomed into it (the host decides whether to admit it). The relay cannot tell a good code from a bad one. Use long random
  codes in your game (the launcher's voice calls and the matchmaker already do). A minimum length on the relay would refuse
  old games, so it is not enforced.
- Relay data frames carry no per-packet signature. An attacker who knows a peer's address and port and can forge source addresses
  can inject datagrams that look like that peer's. Game packets are end-to-end encrypted (libsodium), so injected ones are
  rejected by the receiver; the relay itself does not check.

## 2. Script file-path safety

One function now decides every file name a script, visual-script graph, action list, plugin or network peer supplies:
`okay::pathsafety::Check` in `engine/include/okay/Core/PathSafety.hpp` (written from the Windows naming rules and POSIX rules).
A name must be relative and stay inside the game folder (and the save folder). It is refused for: empty or over 240 bytes, NUL
or any control character, a leading `/` or `\` (absolute, UNC, `\\?\`, `\\.\`), any `..` part, and, **on every system**
because games move between Linux and Windows: any `:` (drive letter, or an NTFS hidden stream such as `file.txt:secret`), the
characters `< > " | ? *`, a part ending in `.` or space (Windows silently strips it, so `...` means `..`), and the device names
CON, PRN, AUX, NUL, COM1-9, LPT1-9, CONIN$, CONOUT$ with or without an extension, in any letter case.
`okay::pathsafety::StaysInside` adds the check a string rule cannot make: an existing symbolic link (or junction) inside the
game folder that points outside it is refused for reads and writes.

`Save::IsSafeRelative`, `ScriptPathOk` (the one gate the OkayScript builtins, action lists and visual-script nodes call, with the
log line that tells the author why) and the visual-script sub-graph node all use it now. Modes: `Portable` (default, what scripts use),
`Windows` and `Posix` (only the rules every system shares), so the table test shows the verdict in each.

What was vulnerable (failing first; the old `Save::IsSafeRelative` was run on each name, output in the report): `NUL`, `con.txt`,
`dir/aux`, `COM1`, `x.`, `x `, `file.txt:hidden`, `a|b`, `what?`, `CONOUT$`, `nul .txt`, `...` and a 300-character name were all
**accepted**. Besides the weak rule, three places did not use it at all:

- the visual-script **Save Creation / Load Creation** nodes wrote `../../anything.okaycreation` (the OkayScript `build_save` was
  guarded, the node was not);
- the **spawn / spawn3 / shoot_at / spawn_wave** script builtins, the action-list spawn instructions and the visual-script
  Spawn Prefab / Shoot nodes opened any path as a prefab (an absolute path read any file the process could, as a parse attempt);
- the sub-graph node had its own, different rule.

**Tests:** `path_safety` (`tests/test_path_safety.cpp`: a table of 60 hostile and fine names with the expected verdict in Portable,
Windows and POSIX mode, plus symbolic-link escapes on a real temp folder, run on Linux; the Windows mode is the same rules applied
to strings, **not run on Windows**) and `script_paths` (extended: hostile names through `save`, `data_save`, `prefs_save`,
`build_save`, `spawn`, `spawn3`, a visual-script Save Creation node, and a link pointing out of the game folder).

**How to see it (Windows release):** open `examples\security_paths\script_paths.okayscene` (File > Open, or File > Examples if it is
listed), press Play, open the Console. Seven lines print: the six hostile names say `false`, `slot_ok.okaysave` says `true`, and the
Console shows `[script] save file: refusing file 'NUL': a reserved device name (CON, NUL, COM1 ...)` and similar for each.

**Not done:** engine readers that open a file from a path stored in a scene or model (textures, meshes) are not script input and
are not under this gate (they have their own checks, e.g. `TexturePath.hpp`). A project that deliberately symlinks its asset
folder outside the game folder will have script file calls refused for those paths; copy the files in, or tell us.

## 3. Updater signing

What already existed (before this work): `versions.json`, the list of every release file with its size and SHA-256, is signed with
ed25519 (`scripts/sign_index.py`, verified by `shared/UpdateSignature.hpp` inside the launcher and the editor); the updater
refuses a file whose size or SHA-256 differs, stages every file as `<name>.new` and swaps only when all passed, rolls back a
launcher that does not start, and never installs without a checksum. So one signature over the list covers every file, because
each file's SHA-256 is inside the signed list; per-file signatures would add nothing. (The SEC1 text asked for "a signature
over the manifest and each file": this is that, with the file's hash inside the signed manifest.)

What was missing, and what changed:

1. **Replay of an old signed list** (the "known gap" in updater.md). An attacker who can only serve an old, really signed
   `versions.json` could offer a version that was later withdrawn for a security fix. New: a program that accepted a validly
   signed list remembers the newest `latest` it saw in `okay_update_floor.txt` beside it (`shared/UpdateGuard.hpp`), and refuses a
   later signed list that names an older one: "Update refused: the release list is older than one this program already
   accepted...". Only a list that verified under a trusted key can raise the floor, so an unsigned or forged list can never set it
   high; a damaged floor file is ignored and rewritten. Failing first: with the floor not yet written, the replayed list was
   accepted (`update_security`).
2. **Silence when the check is off.** With no public key built in, the check is OFF and an unsigned feed is accepted, as before,
   but nothing said so. Now every start of the launcher and the editor prints and logs
   `update signature check: OFF (no key configured)` (or `ON (1 key built in)`), the launcher writes it to `okay_update.log`, and
   Settings > About in the launcher and Help > About in the editor show it (amber for OFF, green for ON).
3. **Tests against a hostile server** (`tests/test_update_security.cpp`; a map of address to bytes is the local test server, no
   network): a good release installs; a flipped byte, a truncated, extended, empty or missing file, and another real version's file
   served under this version's address are all refused and leave nothing installed; a list changed after signing, truncated and
   over-long signatures, a signature by another key, a signature of another list, an all-zero signature and a missing signature are
   all refused; a replayed older signed list is refused and does not lower the floor; the self-update path (`Stage`) refuses a
   damaged launcher download and leaves the live file alone. Run: `ctest -R "update_security|update_signature"` (83 + 49 checks).
4. **Release wiring.** `scripts/sign-release.sh` (`sign`, `verify`, `check-key`, `status`; a thin wrapper over `sign_index.py`;
   its test is `tests/scripts/test_sign_release.sh`) and two things in `.github/workflows/release.yml`: the Publish step now gets
   the secret `OKAY_UPDATE_SIGNING_SEED` (so `publish-supabase.sh` signs `versions.json` and verifies the published pair), and a
   step "Check the update signing key" confirms the seed makes a public key that `OKAY_UPDATE_TRUSTED_KEYS` lists. Both are
   **skipped when the secret is absent**. The workflow was **not run** and not dispatched.

Version rule (already in the launcher, now documented): the automatic update only installs a version strictly newer than the
running one (`newer` in `RunUpdateCheck`), apart from the deliberate way back from a test build. The Versions page installs an
older version only when the person picks it.

**Your steps (once; the key is yours, never paste it in chat):**
1. On a machine you trust, not CI: `python3 scripts/sign_index.py genkey`. It prints a PRIVATE seed and a PUBLIC key and writes nothing.
2. GitHub > Settings > Secrets and variables > Actions > **Secrets** > New repository secret: name `OKAY_UPDATE_SIGNING_SEED`, value the
   private seed. Keep a copy in a password manager.
3. Same page > **Variables** > New repository variable: name `OKAY_UPDATE_TRUSTED_KEYS`, value the public key.
4. Release as usual. The release job's summary shows the "Check the update signing key" step passing. The launcher built then trusts
   that key; its start-up line and Settings > About say `ON (1 key built in)`.
5. Never republish an older version as the newest to withdraw a release: publish a newer fixed one. (A program refuses an older
   list for good. To undo that on one machine, delete `okay_update_floor.txt` beside the program.)

**How to see it (until you finish the steps, the check is OFF, and that is what you see):** start the launcher from a terminal
(or open `okay_update.log` beside `OkaySpace.exe`) and read `update signature check: OFF (no key configured)`; the same line is in
Settings > About. To see a bad signature refused without a key of your own, run `ctest -R update_security` and read
`tests/test_update_security.cpp`; the messages it checks are the ones a person would see ("Update refused: ... was not signed by
the publisher's key...").

**Not done:** a signed timestamp or expiry (an attacker can still serve the *current* signed list forever and so hide a newer
release: a "freeze"); signing the executables for Windows itself (that is the Authenticode work in docs/release/packaging.md); older
launchers in the field never verify anything.

Cryptography note (CLAUDE.md rule 18): no new cryptography was written. Verification and signing go through the existing
`engine/include/okay/AntiCheat/AntiCheatCrypto.hpp` wrapper (libsodium Ed25519 where libsodium is built in) and the existing `scripts/sign_index.py`;
no private key is in the repository, and the scripts never print the seed.

## 4. Reactions privacy (SQL)

`docs/sql/social.sql` already had row-level security on `message_reactions`: you can read or add a reaction only if you can see the
message under it, and remove only your own. A read of it, and a run against a throwaway local Postgres (`tests/scripts/test_reactions_sql.sh`,
**not** the real Supabase), found the gaps below. `docs/sql/reactions_privacy.sql` closes them; without it the new test fails 9 checks.

| Gap | Fix in `reactions_privacy.sql` |
|---|---|
| `can_see_reaction_target` (a SECURITY DEFINER function) could be called by signed-out visitors. | Only signed-in users. |
| A reaction's text was unbounded (a 5,000-character "emoji" was accepted, and an empty one). | 1 to 40 characters (the longest key the app writes is `poll:<n>`); existing rows are not re-checked. |
| The table grants allowed UPDATE (RLS had no policy, so it did nothing, but a policy added later would have opened it). | UPDATE revoked. A reaction is added or removed, never edited. |
| A person you blocked could still react to your messages in a shared server or group and you still saw their reactions (and their user id). | Blocks (from `blocks.sql`) hide reactions in both directions and stop new ones on the blocker's messages. |
| Deleting a message left its reactions (and reactor ids) in the table for ever. | A trigger on `messages`, `channel_messages` and `group_messages` deletes them; old orphans are cleaned once. |
| The app reads every reaction row (with reactor ids) only to count them. | `reaction_counts(scope, ids)` returns counts and "did I react", never a user id or e-mail; it runs with the caller's own permissions, so the same row rules apply. |

Not changed: the raw table is still readable by everyone who can see the message, because the current app counts rows itself. Move the
app to `reaction_counts` and the raw SELECT can then be limited to your own rows. Reactor ids are user ids (UUIDs), never e-mails.

**Your steps:** Supabase dashboard > SQL Editor > New query > paste all of `docs/sql/reactions_privacy.sql` > Run. It is safe to run
twice. Run it after `social.sql`, `group_chats.sql`, `admin.sql` and `blocks.sql` (it prints a NOTICE for any part whose prerequisite
is missing; run it again once that is in). **Nothing here was applied to your project.**

**How to see it:** the launcher's chat is unchanged. In the SQL Editor, `select * from public.reaction_counts('dm', array[1]);`
returns rows of (target_id, emoji, n, mine) and no user column. To run the check yourself on any machine with Postgres:
`OKAY_SQL_TEST_PGHOST=/var/run/postgresql bash tests/scripts/test_reactions_sql.sh` (24 checks; CI runs it in the `sql-tests` job);
`python3 tests/scripts/test_reactions_sql_static.py` is the cheap version that needs no database.

## 5. Public names (SQL)

Several tables keep a copy of the author's name next to the author's user id (`forum_posts.author_name` and so on). The client
wrote that text, and an older launcher wrote the sign-in e-mail there, so other players saw an e-mail as a forum author. The
launcher now cleans names when it shows them (`okay::account::PublicName`); `docs/sql/public_names.sql` makes the database safe too:
`public_name_for(uid)` (display name, else username, cut at any '@', else 'Player') is the only source of those columns, a trigger
on each table replaces whatever the client sends, `profiles.username` and `display_name` are cut at '@', and old rows are cleaned once.
Covered: `activity`, `forum_threads`, `forum_posts`, `workshop_items`, `workshop_reviews`, `channel_messages`, `group_messages`,
`forum_section_members`, `forum_section_bans`, `admin_log` (actor and target), `blocks`. Checked by `tests/scripts/test_public_names_sql.sh`
on a throwaway local Postgres (**not** the real Supabase).

**Your steps:** SQL Editor > paste all of `docs/sql/public_names.sql` > Run, after `social.sql` and the optional files you use
(`profile_extras.sql`, `forum_sections.sql`, `group_chats.sql`, `admin.sql`, `blocks.sql`). Safe to run twice; run it again after
adding one of those files later. **Nothing here was applied to your project.**
