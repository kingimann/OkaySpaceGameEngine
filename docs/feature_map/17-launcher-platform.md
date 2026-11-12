# 17. Launcher & platform

The OkaySpace launcher (`launcher/`, one 24,802-line `main.cpp` plus `VoiceChat.hpp`) and the online platform it talks to:
accounts, friends, chat, forums, marketplace, profiles, admin, the official game, Steam and PlayFab. The client code for the
online services is header-only in `engine/include/okay/Platform`; the server side is Supabase (SQL in `docs/sql`, one Edge
function in `supabase/functions`). Anything in `shared/` that the launcher, editor and player all use is mapped here too.
Line numbers below are for `launcher/main.cpp` at version 2.848.0 and drift. Nobody is touching it now.

## 17. Launcher & platform

### 17.1 Launcher shell and updater
- **What:** The launcher program players double-click (`OkaySpace.exe`): window and sidebar, the pages, settings, notifications, the loading screen, and the self-updater that keeps the launcher, editor and player current.
- **Parts:**
  - 17.1.1 Window, sidebar and pages - `launcher/main.cpp` - SDL2 plus Dear ImGui; tabs Create, Play, Community, Account, Settings, Achievements, Friends, Market, Servers, Forums, Downloads, plus Multiplayer, What's New, Tips and Admin; `main()` starts near L16377, the tab pages from L17892; number keys switch pages
  - 17.1.2 Self-updater - `launcher/main.cpp` (L136-326) - compares its baked version with the published `VERSION.txt` and downloads the launcher, editor and player; every file must have a SHA-256 in `versions.json` (`launcher/UpdaterCore.hpp`: no checksum, mismatch, failed download or failed swap = refused with a reason, nothing replaced); `ShellSafeUrl` refuses URLs that could break out of the shell command; `versions.json` itself is unsigned (see 17.11)
  - 17.1.3 Supabase mirror and periodic check - `launcher/main.cpp` (L810-1031) - uses whichever of the Supabase `releases` bucket and GitHub Releases has the newer version; `OKAY_UPDATE_SERVER` overrides; "newer version" badge
  - 17.1.4 What's New and Tips - `launcher/main.cpp` (L4050, tabs 12 and 13) - release history cards and a searchable tips list with a tip of the day
  - 17.1.5 Settings page - `launcher/main.cpp` (tab 4, L20333) - Appearance, Chat & Notifications, Loading screen, Behavior, Game libraries, Account, About, Privacy & Safety, Voice & Audio, Crash reports; saved in `launcher.cfg` next to the exe
  - 17.1.6 Tray, single instance, notifications - `launcher/main.cpp` (L9614, L9696, L1682, L14945) - minimize to the Windows tray, one launcher per machine, a notification bell and toast pop-ups
  - 17.1.7 Shared look and startup frame - `shared/OkayTheme.hpp`, `shared/Splash.hpp`, `shared/AppIcon.hpp`, `shared/RobotoFont.h`, `shared/JetBrainsMonoFont.h` - the Slate theme shared with the editor, a branded splash drawn before ImGui starts, a procedural window icon, embedded fonts
  - 17.1.8 Windows resources and build - `launcher/CMakeLists.txt`, `launcher/okayspace.rc`, `launcher/okayspace.ico` - target `okayspace-launcher` builds `OkaySpace.exe`; it does not link the engine library (only engine headers plus `ServerQuery.cpp` and `UdpSocket.cpp`)
  - 17.1.9 Script UI bridge and test panel - `shared/OkayScriptUIBridge.hpp`, `shared/OkayTestPanel.hpp` - not launcher code: OkayUI widgets for script `ui_*` calls and the F1 "Test UI" panel, used by the editor and the player
- **Code:** `launcher/main.cpp`, `launcher/UpdaterCore.hpp`, `launcher/CMakeLists.txt`, `launcher/okayspace.rc`, `launcher/okayspace.ico`, `shared/OkayTheme.hpp`, `shared/Splash.hpp`, `shared/AppIcon.hpp`, `shared/RobotoFont.h`, `shared/JetBrainsMonoFont.h`, `shared/OkayScriptUIBridge.hpp`, `shared/OkayTestPanel.hpp`
- **API:** `OkaySpace.exe`, `launcher.cfg`, env `OKAY_UPDATE_SERVER`, `OKAY_ACCOUNT_SERVER`, dev hooks `OKAY_UI_DEMO_TAB`, `OKAY_CHAT_DEMO`
- **Editor:** none (the launcher starts `OkayEngine.exe` and `OkaySpacePlayer.exe` from `Tools/`)
- **Docs:** `docs/packaging.md`, `docs/releasing.md`, `docs/launcher-multiplayer.md`
- **Tests:** none for the shell itself; the updater rules and helpers have tests (`launcher_versions` updater suite, `versions_index`, `crash_reports`, `project_delete`, `official_game`, `chat_json`, `friends_json`)
- **Status:** Works - the shipping app, structure read from `main.cpp`; no automated UI test
- **Open:** split `launcher/main.cpp` (1.5 MB, see 19.4 and ROADMAP "Later"); the update index (`versions.json`) is not signed (17.11); the updater itself requires and verifies checksums
- **Related:** 18.1, 18.5, 17.12, 17.13, 41.1
- **Words:** launcher, OkaySpace.exe, updater, update, new version, sidebar, tray, splash, loading screen, Settings, launcher.cfg, What's New, Tips, notifications, toast, Slate theme
- **Seen as:** launcher won't update or says "new version" forever; black frame at startup; wrong theme or text size; the sidebar is collapsed; two launchers open
- **Ask me like:** "add a Tips card for the new feature", "the launcher says up to date but a newer release exists"

### 17.2 Accounts and sign-in
- **What:** One account service shared by the launcher, editor, player and game scripts: sign up and in, session tokens that refresh, cloud saves and leaderboards. It works offline with a local on-disk account, with a custom server, or with Supabase.
- **Parts:**
  - 17.2.1 Account service - `Platform/Account/AccountService` - providers Local (salted, stretched SHA-256 file), Remote (your server, see 42.4) and Supabase (email sign-up, password reset (emails a link to the hosted page `supabase/auth-pages/reset-password`, see `docs/password-reset.md`), change email or username, token refresh); `Api()` adds the key and bearer token
  - 17.2.2 Account facade - `Platform/Account/Account` - process-wide `okay::Account` used by editor, player and the `account_*` script builtins; `OKAY_ACCOUNT_SERVER`
  - 17.2.3 Cloud saves and leaderboards - `Platform/Account/AccountService` - `CloudSave`, `CloudLoad`, `CloudList`, `LeaderboardSubmit`, `LeaderboardTop` on two Supabase tables
  - 17.2.4 Native HTTPS - `Platform/NativeHttp` - pooled WinHTTP connections on Windows (no curl process per request); elsewhere, or if WinHTTP can't reach, the code falls back to curl with secrets passed in files
  - 17.2.5 Account page - `launcher/main.cpp` (tab 3, L19634) - sign in or create, profile, sign-in and security, privacy, more
  - 17.2.6 Baked default server - `CMakeLists.txt` - `OKAY_DEFAULT_ACCOUNT_URL` and `OKAY_DEFAULT_ACCOUNT_KEY` compiled into every binary
- **Code:** `Platform/Account/Account`, `Platform/Account/AccountService`, `Platform/NativeHttp`
- **API:** `okay::Account::Get`, `Login`, `Logout`, `Token`, `IsLoggedIn`, `AccountService::Register`, `Login`, `Api`, `CloudSave`, `LeaderboardTop`, scripts `account_login`, `account_register`, `account_logout`, `account_is_logged_in`, `account_token`, `account_get`, `account_post`
- **Editor:** none (the sign-in page is the launcher's Account tab; the player and game scripts use the same service)
- **Docs:** `docs/accounts.md`, `docs/supabase-backend.md`
- **Tests:** `account`, `account_script`
- **Status:** Works - local backend, session persistence and the script facade are tested; the remote and Supabase paths need a live server and are not exercised in CI
- **Open:** the default Supabase project URL and publishable key are hardcoded in `CMakeLists.txt` (old map: remove them; the release workflow deliberately no longer overrides them); possible risk: `AccountService` shells out to curl on non-Windows
- **Related:** 17.9, 17.11, 42.4, 41.6
- **Words:** account, sign in, login, register, username, password, session, token, Supabase, cloud save, leaderboard, AccountService, NativeHttp, WinHTTP, curl, JWT
- **Seen as:** "can't sign in"; signed out after a restart; sign-in works in the launcher but not in a game; the launcher is slow to fill in (curl per request)
- **Ask me like:** "let players log in from my game's menu", "point the build at my own account server"

### 17.3 Friends, following, parties
- **What:** Friend lists with online status and what they play, one-way follows, and parties of friends who follow their leader into a match.
- **Parts:**
  - 17.3.1 Friends - `Platform/Friends` - add by username or short friend code, accept or decline, presence heartbeat ("playing X"), join a friend's relay session, a small activity feed; no-ops when signed out
  - 17.3.2 Follows - `docs/sql/follows.sql` - one-way follow graph; the launcher hides Follow buttons until it is run
  - 17.3.3 Parties - `Platform/Parties` - one party at a time, up to 8, the leader's session stored on the party so members follow it; create, join, leave, kick, promote through SECURITY DEFINER functions
  - 17.3.4 Friends page and party card - `launcher/main.cpp` (tab 6, L21452; party card L10356-10940) - join buttons, invites from menus, ready checks, crown on the leader
- **Code:** `Platform/Friends`, `Platform/Parties`, `docs/sql/follows.sql`, `docs/sql/parties.sql`
- **API:** `okay::Friends`, `okay::Parties`, `net_invite_friends`, `/invite`, `/party`
- **Editor:** none
- **Docs:** `docs/launcher-multiplayer.md`
- **Tests:** `friends_json`, `launcher_join`
- **Status:** Works - shipping pages; tests cover the profile JSON and party parsing, not the live tables
- **Open:** no test for follows or the SQL; `parties.sql` and `follows.sql` must be run once or the features hide
- **Related:** 17.4, 17.7, 17.15, 16.4
- **Words:** friends, friend code, follow, party, presence, online, invite, join game, Ask to join, friendships, activity feed
- **Seen as:** friend shows offline when online; Join button missing; a party member doesn't follow the leader; friend requests don't arrive
- **Ask me like:** "friends can't see what I'm playing", "raise the party size limit"

### 17.4 Chat: DMs, groups, voice notes, calls, polls, GIFs, emoji
- **What:** The Discord- and Messenger-style chat: direct messages, group chats, reactions, replies, pins, edit history, scheduled messages, custom status, polls, files, GIFs, emoji, stickers, voice notes and voice calls.
- **Parts:**
  - 17.4.1 Direct messages - `Platform/Messages` - send, history, typing, read marks, reactions, reply, search across chats, forward; rows limited to the two parties by row-level security; typing and read marks need `docs/sql/chat_receipts.sql` (`typing_state`, `message_reads`) and Settings shows "Not set up on the server yet" instead of the two toggles when the server says the tables are missing
  - 17.4.2 Group chats - `docs/sql/group_chats.sql`, `docs/sql/member_moderation.sql` - membership-gated conversations kept apart from the 1:1 `messages` table; owner/admin can remove, ban, unban and promote from the Members list (17.8.4)
  - 17.4.3 Pins, edit history, scheduled messages, status - `docs/sql/chat_extras.sql` - `message_pins`, `message_edits`; `launcher/main.cpp` (L5112-5640); pins stay on this device until the SQL is run
  - 17.4.4 Media - `docs/sql/chat_media.sql` - emoji from a Twemoji sheet (L3144), GIFs (Tenor key), custom emoji and stickers, file cards, voice notes (L4573); the public `chat` bucket
  - 17.4.5 Text formatting and link previews - `launcher/main.cpp` (L2346-2974) - bold, italic, strike, code, spoiler; preview cards for links
  - 17.4.6 Polls, game invites, join requests - `launcher/main.cpp` (L5925, L4444, L6098) - invite cards with Join game, "Ask to join", `/invite`
  - 17.4.7 Voice calls UI - `launcher/main.cpp` (L7062) - call cards, incoming call, voice panel; the audio engine is 16.7
  - 17.4.8 Chat window - `launcher/main.cpp` (L14982, polls L9726 and L14532) - docked or pop-out window with an outbox so a sent message appears instantly
  - 17.4.9 Blocks and enforced privacy - `docs/sql/blocks.sql`, `Platform/Privacy`, `Platform/BlockList`, `launcher/main.cpp` (`SetBlocked`, `PumpBlocks`, `PumpPrivacy`; Settings > Privacy & Safety) - server-side block list and privacy settings (messages, calls, friend requests, online status, profile details); blocked people cannot DM, call, friend-request or read presence and private profile fields; works offline (queued) and says when a server rule refused or the SQL has not been run
- **Code:** `Platform/Messages`, `Platform/Privacy`, `Platform/BlockList`, `docs/sql/group_chats.sql`, `docs/sql/chat_extras.sql`, `docs/sql/chat_media.sql`, `docs/sql/blocks.sql`
- **API:** `okay::Messages`, `okay::BlockList`, `okay::privacy`, chat slash commands `/invite`, `/party`, launcher Chat window
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`, `docs/launcher-multiplayer.md`
- **Tests:** `chat_json`, `chat_receipts` (missing-table detection), `chat_receipts_sql` (local Postgres, skipped unless `OKAY_SQL_TEST_PGHOST`), `blocks_privacy` (rule parsing, missing-table detection, settings, pending block changes), `blocks_sql` (local Postgres, 99 checks), `member_moderation`
- **Status:** Works - shipping; typing and read marks work once the owner has run `chat_receipts.sql` (not applied to any live project by us); the JSON splitter, the missing-table check and the block/privacy logic are tested. Server-side blocks need `docs/sql/blocks.sql` run by the owner (until then blocking is PC-only and Settings says so)
- **Open:** most of the 24.8k lines of `main.cpp` are chat code and untested (split, see 19.4); `chat_extras.sql` and `group_chats.sql` are optional and degrade silently; the live typing / read-mark round trip against a real Supabase project is untested
- **Related:** 16.7, 17.3, 17.6, 17.9
- **Words:** kick, ban group member, chat, DM, direct message, group chat, reactions, emoji, GIF, Tenor, sticker, poll, pin, edit history, scheduled message, voice note, call, /invite, typing indicator, read receipt, "Seen", chat_receipts.sql
- **Seen as:** messages missing or duplicated; a message with braces vanishes; GIF search empty; no emoji colour; call button does nothing
- **Ask me like:** "add message threads", "GIF search needs a key, where is it set?"

### 17.5 Forums
- **What:** Reddit-style community forums: threads, posts, votes, and sections (o/name) with their own moderators and bans.
- **Parts:**
  - 17.5.1 Threads, posts, votes - `Platform/Forums` - world-readable, writing needs sign-in; `docs/sql/social.sql` tables
  - 17.5.2 Sections and moderation - `docs/sql/forum_sections.sql` - anyone creates a section (creator becomes admin), join or leave, member, moderator, section bans
  - 17.5.3 Forums page - `launcher/main.cpp` (tab 9, L23674; background fetch L9795)
- **Code:** `Platform/Forums`, `docs/sql/forum_sections.sql`
- **API:** `okay::Forums`
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`
- **Tests:** none: only `chat_json` touches the class
- **Status:** Works - shipping page; untested
- **Open:** no test; staff tools live in 17.9
- **Related:** 17.9, 17.4
- **Words:** forums, thread, post, vote, section, o/, subforum, moderator
- **Seen as:** forum list empty; can't post; sections missing until `forum_sections.sql` is run
- **Ask me like:** "add a pinned announcements section"

### 17.6 Marketplace and Workshop
- **What:** Browse and publish shared games, scenes and templates; each item points to a hosted build and players press Get to download it through the launcher's download manager.
- **Parts:**
  - 17.6.1 Workshop client - `Platform/Workshop` - items (title, author, kind, download count, URL), reviews, publish; gracefully empty when the table is missing
  - 17.6.2 Market page - `launcher/main.cpp` (tab 7, L22068) - browse, search, Get, publish form (review in Admin 17.9)
  - 17.6.3 Download manager - `launcher/main.cpp` (L327-542, tab 10 L24597) - background downloads into `downloads/`, progress, clear finished
- **Code:** `Platform/Workshop`
- **API:** `okay::Workshop`, `WorkshopItem`
- **Editor:** none (the editor's Project panel marketplace asset manager is 15.4)
- **Docs:** `docs/supabase-backend.md`
- **Tests:** none for this class (`steam` tests the Steam Workshop calls)
- **Status:** Works - shipping page, untested
- **Open:** possible bug: two different structs named `okay::WorkshopItem` exist (`Platform/Workshop.hpp` and `Platform/Steam/SteamService.hpp`); including both headers in one file will not compile, and different layouts in two files is an ODR risk
- **Related:** 17.10, 15.4, 17.13
- **Words:** marketplace, workshop, market, items, publish, download, Get, review, WorkshopItem, templates, community games
- **Seen as:** an item won't download; published item not showing (needs review); duplicate WorkshopItem compile error
- **Ask me like:** "publish my game to the Marketplace", "why is my item pending?"

### 17.7 Profiles, badges, achievements
- **What:** Steam-style public profiles (me, friends, forum authors) with a showcase of achievements and games, playtime, social links and badges, plus achievements stored per user on this machine so games and the launcher read the same list.
- **Parts:**
  - 17.7.1 Achievements store - `Platform/Achievements` - local-first list per user (id, label, when unlocked), shared by running games and the launcher, no server needed
  - 17.7.2 Achievements page - `launcher/main.cpp` (tab 5, L21189; medals L13550)
  - 17.7.3 Public profile - `Platform/Profiles` - profile row, showcase (up to 6 achievements, 4 games), banner accent, friend count; page at `launcher/main.cpp` (L13590)
  - 17.7.4 Profile extras - `docs/sql/profile_showcase.sql`, `docs/sql/profile_extras.sql` - mirrored achievements, playtime (`user_playtime`), social links, verified tag, staff badges, achievement points
  - 17.7.5 Playtime tracking - `launcher/main.cpp` (L1032) - time in each game and in the editor
- **Code:** `Platform/Achievements`, `Platform/Profiles`, `docs/sql/profile_showcase.sql`, `docs/sql/profile_extras.sql`
- **API:** `okay::Achievements`, `okay::Profiles`, `steam_unlock` (Steam side, 17.10)
- **Editor:** none
- **Docs:** `docs/accounts.md`
- **Tests:** `friends_json` (profile rows in `Friends.hpp`); none for `Achievements` or `Profiles`
- **Status:** Works - shipping; the SQL degrades when not run (showcases hidden, points kept locally)
- **Open:** achievement cloud sync is "layered on later" per the header; no tests for `Achievements` or `Profiles`
- **Related:** 17.3, 17.10, 17.13
- **Words:** profile, badge, achievements, medal, showcase, verified, playtime, banner, points, public profile
- **Seen as:** achievement doesn't unlock or show; others can't see my showcase; playtime stays zero
- **Ask me like:** "unlock an achievement from a script", "show playtime on the profile"

### 17.8 Servers page (Discord-style community servers)
- **What:** Community servers with channels and invite codes (not game servers: the game server browser is 16.4 and the Multiplayer page 17.15). Resizable Discord-like layout with messages, edits, likes and voice rooms.
- **Parts:**
  - 17.8.1 Servers client - `Platform/Servers` - `create_server` and `join_server` are SECURITY DEFINER calls so a non-member can make or join one; channels, members, messages
  - 17.8.2 Servers page - `launcher/main.cpp` (tab 8, L22622; background poll L14858) - server rail, channels, member list
  - 17.8.3 Server voice rooms - `launcher/VoiceChat.hpp` - room key from the server's invite code (16.7)
  - 17.8.4 Member moderation (servers and group chats) - `shared/MemberModeration.hpp`, `docs/sql/member_moderation.sql` - Remove, Ban (reason, length), Unban, Banned list and Make/Remove admin on the existing members lists; owner > admin > member enforced by the database; needs the SQL applied (see `docs/member_moderation.md`)
- **Code:** `Platform/Servers`, `shared/MemberModeration.hpp`, `docs/sql/member_moderation.sql`
- **API:** `okay::Servers`, `Server`, `Channel`, `ChannelMessage`
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`, `docs/member_moderation.md`
- **Tests:** `member_moderation`, `member_moderation_sql` (local Postgres, opt-in); `chat_json` reads the channel JSON shape
- **Status:** Works - shipping page, untested
- **Open:** no mute, no invite expiry or revoke; moderation needs `member_moderation.sql` applied by the owner (until then the buttons answer "not set up"); the page itself is untested; the name clashes with game "servers" everywhere else (browser, dedicated)
- **Related:** 17.4, 16.7, 16.4
- **Words:** kick, ban, unban, remove member, admin role, banned list, servers, channels, invite code, community, Discord-style, server members, create_server, join_server
- **Seen as:** can't join with an invite code; channel messages missing
- **Ask me like:** "add a channel for announcements"

### 17.9 Admin
- **What:** Staff tools in the launcher: roles (admin above staff above moderator, stored as profile badges), site bans, the audit log and dashboard numbers, marketplace and forum moderation. The server enforces every permission; the page only hides what a rank can't do.
- **Parts:**
  - 17.9.1 Admin client - `Platform/Admin` - users, bans, log, stats, `MyBanInfo` for the ban banner; all RLS-enforced
  - 17.9.2 Admin page - `launcher/main.cpp` (tab 11, L12860) - dashboard, players, marketplace review, forum moderation, bans, audit log
  - 17.9.3 Ban banner - `launcher/main.cpp` (L13440) - shows a site ban and its end date
  - 17.9.4 Roles and audit SQL - `docs/sql/admin.sql` - roles, `site_bans`, `admin_log`, `is_admin`; run after `social.sql`
  - 17.9.5 Sub-pages owned elsewhere - Admin > Anti-cheat (39.6) and Admin > Official game (17.14)
  - 17.9.6 User reports - `shared/UserReports.hpp`, `docs/sql/reports.sql`, `launcher/main.cpp` (`OpenReport`, `DrawReportPopup`, `DrawReportsAdmin`) - players report a player, DM, group message, forum post or comment, listing or community server (fixed reasons plus a note); moderators review under Admin > Reports (open, resolved, dismissed). "Report sent" only after the server accepted it; clear messages for signed out, table missing, duplicate and rate limit. Owner must run `reports.sql`; "Report cheating" stays separate (39.6)
- **Code:** `Platform/Admin`, `docs/sql/admin.sql`, `shared/UserReports.hpp`, `docs/sql/reports.sql`
- **API:** `okay::Admin`, `AdminUser`, `AdminBan`
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`
- **Tests:** none for `Admin`; the anti-cheat parts of `admin.sql` are exercised by `anticheat_sql`; user reports by `user_reports` (requests, outcomes) and `reports_sql` (RLS on a local Postgres)
- **Status:** Works - shipping page; untested client
- **Open:** create test servers without a game (old map)
- **Related:** 39.6, 17.14, 17.5, 17.6
- **Words:** admin, moderator, staff, role, badge, ban, unban, audit log, admin_log, site_bans, dashboard
- **Seen as:** Admin tab missing (rank 0); ban banner on sign-in; a moderation action is refused
- **Ask me like:** "make a player a moderator", "ban someone for a week"

### 17.10 Steam and PlayFab
- **What:** Steam features behind one interface (achievements, stats, leaderboards, cloud files, workshop, friends, lobbies, overlay) with a built-in simulation so everything runs without the SDK, plus a PlayFab REST client.
- **Parts:**
  - 17.10.1 Steam interface and simulation - `Platform/Steam/SteamService`, `Platform/Steam/NullSteamService` - `ISteamService` and an in-memory backend (default)
  - 17.10.2 Real Steamworks backend - `Platform/Steam/SteamworksService` - compiled only with `-DOKAY_WITH_STEAM=ON` and `STEAMWORKS_SDK_PATH`
  - 17.10.3 Accessor and component - `Platform/Steam/Steam`, `Components/SteamManager` - `Steam::Get()`, `RunCallbacks()`; the component inits, pumps and shuts down; default app id 480 (test), release builds bake 1172560
  - 17.10.4 Script builtins - `steam_unlock`, `steam_set_stat`, `steam_cloud_write`, `steam_lobby_create`, `steam_workshop_publish` and about 40 more
  - 17.10.5 PlayFab client - `Platform/PlayFab/PlayFab` - Client REST API through curl: `Configure(TitleId)`, `LoginWithCustomID`, stats, leaderboard; visual script `playfab_*` instructions
- **Code:** `Platform/Steam/SteamService`, `Platform/Steam/Steam`, `Platform/Steam/NullSteamService`, `Platform/Steam/SteamworksService`, `Platform/PlayFab/PlayFab`, `Components/SteamManager`
- **API:** `ISteamService`, `Steam::Get`, `SteamManager`, scripts `steam_*`, `PlayFab::Get().Configure`, `playfab_*`
- **Editor:** View > Panels > Services (Steam: backend live or simulation, achievements, stats, leaderboard, cloud, overlay; PlayFab test); Add Component > Steam Manager
- **Docs:** `docs/steam_release.md`, `docs/photon.md` (provider seams)
- **Tests:** `steam`, `steam_script`
- **Status:** Works - the simulation backend and the script builtins are tested; the real Steamworks backend can't be built here and is untested
- **Open:** `release.yml` does not set `STEAMWORKS_SDK_PATH`, so the GitHub release exes use the simulation backend; PlayFab client has no test; Steam upload kit is manual (18.8)
- **Related:** 18.8, 16.10, 17.6, 17.7
- **Words:** Steam, Steamworks, ISteamService, SteamManager, achievements, stats, cloud saves, workshop, lobby, overlay, PlayFab, app id 480, simulation backend
- **Seen as:** achievements don't appear on Steam; Steam overlay missing; "Steam not running" falls back silently
- **Ask me like:** "unlock a Steam achievement when the boss dies", "log in with PlayFab at game start"

### 17.11 Security plan SEC1
- **What:** A four-item security plan from the old map: relay isolation, script file-path safety, updater signing and a reactions privacy fix. Three are done on main; updater signing is not found in the code. The cross-cutting security page is area 39.
- **Parts:**
  - 17.11.1 Relay session isolation - `Net/RelayServer` - done in #254 and hardened since (see 39.8)
  - 17.11.2 Script file paths - `Core/SaveData` - `Save::IsSafeRelative` refuses absolute paths and `..` for save, prefs, data, build and scene calls; test `script_paths`
  - 17.11.3 Reactions privacy - `docs/sql/social.sql` - `can_see_reaction_target` limits who can read or add a reaction (read in the SQL)
  - 17.11.4 Updater signing - `launcher/main.cpp` (L136-326) - not implemented: the updater requires and checks SHA-256 from `versions.json` but that index is unsigned (trusts HTTPS plus the bucket)
- **Code:** `Core/SaveData`, `docs/sql/social.sql`, `Net/RelayServer`
- **API:** `Save::IsSafeRelative`, `ShellSafeUrl`
- **Editor:** none
- **Docs:** `docs/relay.md`, `docs/anticheat.md`
- **Tests:** `script_paths`, `relay_security`
- **Status:** Partial - items 1 to 3 are in main (commit "Security: relay session isolation, script file paths, reactions privacy", #254); item 4 not found
- **Open:** sign the launcher, editor and player downloads (ed25519 as the anti-cheat manifest does) and verify before swapping; the SEC1 plan text is not in the repo, so item wording is from the old map
- **Related:** 39.8, 39.9, 39.11, 17.1
- **Words:** SEC1, security plan, updater signing, relay isolation, script paths, reactions privacy
- **Seen as:** a script reads files outside the game folder (should be refused); update downloaded from a tampered server would be accepted
- **Ask me like:** "sign the launcher updates", "what is left of the security plan?"

### 17.12 Create page: projects, templates and Git
- **What:** The launcher's Create page: list the projects on disk, make a new one from a template, open it in the editor, delete or remove one safely, and put a project on GitHub.
- **Parts:**
  - 17.12.1 Project list and New Project - `launcher/main.cpp` (tab 0, L17892) - every `<Location>/<Name>/Assets` folder holding a `.okayscene`, plus projects opened from elsewhere; the template choice is passed to the editor as `--template`
  - 17.12.2 Safe delete - `shared/ProjectDelete.hpp` - what counts as a project folder, and the folders never deleted (drive root, home, launcher folder, projects folder)
  - 17.12.3 Git for projects - `launcher/main.cpp` (L543-809) - clone, pull, commit and push, publish to GitHub (right-click a project)
- **Code:** `shared/ProjectDelete.hpp`, `launcher/main.cpp`
- **API:** launcher Create page; right-click menu: Delete project, Publish to GitHub, Commit & Push
- **Editor:** the project opens in the editor; the starter templates are 1.6
- **Docs:** `docs/making_a_game.md`
- **Tests:** `project_delete`
- **Status:** Works - the safety rules are unit tested (added in 2.829.0, #334); Git calls shell out and are untested
- **Open:** project templates are static scene templates; project migration across engine versions is planned (area 25)
- **Related:** 1.6, 25.1, 15.4
- **Words:** Create page, projects, new project, template, delete project, remove, Projects folder, Git, GitHub, commit, push
- **Seen as:** a project is missing from the list; "setting up..." never finishes; deleting is refused
- **Ask me like:** "why can't I delete this project from the launcher?"

### 17.13 Play page: library, web games, collections, playtime
- **What:** The Play page: the official game card, the games you built or downloaded, HTML games that open in the browser, Steam-style collections, cover art and playtime.
- **Parts:**
  - 17.13.1 Library and Play - `launcher/main.cpp` (tab 1, L18720) - finds games in the launcher's games and community folders and extra Game libraries and runs them through `OkaySpacePlayer.exe` or their own exe
  - 17.13.2 Web games - `launcher/main.cpp` (L18728) - a folder with `index.html` or a loose `.html` plays in the default browser
  - 17.13.3 Collections and cover art - `launcher/main.cpp` (L1922, L2974) - library categories; optional per-game cover image
  - 17.13.4 Pre-launch integrity check - `launcher/main.cpp` (L1181) - checks games that carry a `game.okayconfig` against `Data/manifest.json` when anti-cheat is on (39.4)
  - 17.13.5 Community page and Downloads - `launcher/main.cpp` (tab 2 L19208, tab 10 L24597) - the community folder's shared scenes and games with a hero card and Publish to Marketplace; the download list
- **Code:** `launcher/main.cpp`
- **API:** launcher Play page; `Settings > Game libraries`
- **Editor:** none
- **Docs:** `docs/packaging.md`
- **Tests:** none
- **Status:** Works - shipping page; untested
- **Open:** none recorded
- **Related:** 17.14, 17.6, 39.4, 18.6
- **Words:** Play page, library, games, collections, cover art, web games, index.html, Game libraries, playtime
- **Seen as:** my built game doesn't show in Play; a game starts the wrong player; HTML game opens in the browser
- **Ask me like:** "show my exported game in the Play list"

### 17.14 Official game: install, update, publish
- **What:** The one official OkaySpace game shown first on the Play page: install, play, auto-update, uninstall. Admins publish new versions from the launcher with their own login; a script does the same with a service key. Every download is checked by size and SHA-256 and swapped in with a journal.
- **Parts:**
  - 17.14.1 Manifest, diff and safe swap - `shared/OfficialGame.hpp` - parses `manifest.json` as untrusted input, compares versions, works out downloads and deletes, swaps a verified update in with a journal (`.okayofficial/staging`, `old`, `apply.txt`)
  - 17.14.2 Play-page card - `launcher/main.cpp` (L15151) - Install, Play, Update, Uninstall; hidden until one is published
  - 17.14.3 Admin > Official game - `launcher/main.cpp` (L15664, L15993) - publish, make live, delete, clean up; rules in `okayofficial::CheckPublish`
  - 17.14.4 Storage permissions - `docs/sql/official_game_admin.sql` - lets admins (only) write under `official/` in the `releases` bucket
  - 17.14.5 Publish script - `scripts/publish-official-game.sh` - same layout and checks from a shell; `KEEP`, `FORCE`, `DRY_RUN`
- **Code:** `shared/OfficialGame.hpp`, `docs/sql/official_game_admin.sql`, `scripts/publish-official-game.sh`
- **API:** `okayofficial::Verify`, `CheckPublish`, `scripts/publish-official-game.sh <folder> [version]`
- **Editor:** File > Build Settings (Windows) builds the folder that gets published
- **Docs:** `docs/releasing.md` (Publishing the official game)
- **Tests:** `official_game`, `official_publish`, `publish_official_cleanup`
- **Status:** Works - parsing, diff, swap journal, publish rules and the script's clean-up are tested (script against a stub curl)
- **Open:** keep `publish-official-game.sh` rules in step with `CheckPublish` by hand (the script header says so); the release workflow does not publish the official game
- **Related:** 18.5, 39.4, 17.9, 17.13
- **Words:** official game, install, update, manifest.json, okayofficial, staging, journal, publish, releases bucket, official/VERSION.txt
- **Seen as:** the official game card is missing; an update never finishes; "publishing is refused" before the SQL is run
- **Ask me like:** "publish version 2 of the official game", "roll back the official game"

### 17.15 Multiplayer page and game link
- **What:** The launcher's Steam-style multiplayer layer: what a running game reports (presence, invite requests), joining a friend or server by starting the game with `--join`, the server browser tabs, Quick Play, invites, recent players and leaderboards.
- **Parts:**
  - 17.15.1 Game IPC files - `Platform/GameIpc` - small files in `<config>/OkaySpace/ipc` keyed by process id: presence, invite request, join request, `played-with.tsv`; no sockets, stale files ignored by age
  - 17.15.2 Join and invite flow - `launcher/main.cpp` (L9994-10260) - finds which library game a link is for, starts it with `--join`, or hands the link to a running game; presence pumped once a second
  - 17.15.3 Server browser tabs - `launcher/main.cpp` (L10259, page L11320) - Internet, Friends, LAN, Favorites, History; filters; Connect to server; `servers.cfg`
  - 17.15.4 Server info and Quick Play - `launcher/main.cpp` (L10909) - who is on a server; join the best open server for a game
  - 17.15.5 Recent players and leaderboards - `launcher/main.cpp` (L11043) - from match results (16.9)
  - 17.15.6 Invite pop-ups and dialogs - `launcher/main.cpp` (L10533-10810)
- **Code:** `Platform/GameIpc`, `launcher/main.cpp`
- **API:** `GameIpc::WritePresence`, `RequestInvite`, `PostJoin`, `TakeJoin`, `GamePresence`, flags `--join LINK`, `--connect HOST:PORT`
- **Editor:** none (in a game: Shift+Tab opens the invite window)
- **Docs:** `docs/launcher-multiplayer.md`
- **Tests:** `launcher_join`
- **Status:** Works - `launcher_join` covers links, queries, presence, the IPC files and a game launched to join
- **Open:** none recorded; invites need the friends and chat tables (17.3, 17.4)
- **Related:** 16.4, 16.9, 17.3, 17.4
- **Words:** Multiplayer page, join, invite, rich presence, GameIpc, server browser, Quick Play, Favorites, History, Recent players, Leaderboards, --join
- **Seen as:** Join does nothing; friend's game isn't joinable; presence stuck on "In the menus"; invite pop-up missing
- **Ask me like:** "add a Join button to my game's friend list", "why is the Internet tab empty?"

### 17.16 Crash reports (launcher side)
- **What:** The launcher's Settings > Crash reports page that reads what the editor wrote when it closed unexpectedly (the latest report and a history), in one parser shared with the tests.
- **Parts:**
  - 17.16.1 Report parser - `shared/CrashReports.hpp` - reads `okay_crash.txt` and `okay_crashlog.txt` (Windows SEH and Linux or macOS signal format): when, version, last breadcrumb, exception, module
  - 17.16.2 Crash page - `launcher/main.cpp` (L1793 data, L20945 page) - newest first, one notification per new crash, a Clear button; `OKAY_CRASH_DIR` points it at another folder
- **Code:** `shared/CrashReports.hpp`
- **API:** `okaycrash::Report`, parse functions
- **Editor:** the editor writes the reports (15.9)
- **Docs:** `docs/editor.md`
- **Tests:** `crash_reports`
- **Status:** Works - the parser is tested for both formats
- **Open:** none
- **Related:** 15.9, 22.3
- **Words:** crash report, okay_crash.txt, okay_crashlog.txt, last step, exception, SIGSEGV
- **Seen as:** the editor closed by itself; Crash reports page empty
- **Ask me like:** "show me the latest editor crash"
