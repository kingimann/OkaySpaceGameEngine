# online/: Online

> **What this is:** the list of pages in `docs/online/`, one line each. Online: multiplayer, servers, relay, accounts, the Supabase backend, anti-cheat, security, crash reports.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Download my data and Delete my account](account_data.md): Launcher: **Account > Sign-in & security > Your data** (online Supabase accounts only).
- [Player accounts](accounts.md): OkaySpace has a small account system for signing players in and out.
- [Anti-cheat](anticheat.md): Server-side, opt-in anti-cheat: the quick start, the full reference, and what hardening is done and not done.
- [Crash reports: what is saved, what is sent, how an issue is made](crash_reports.md): If the editor crashes, it writes a detailed report.
- [Dedicated servers](dedicated-server.md): Any OkaySpace game can run as its own **headless, authoritative game server**: the same player executable, with no window, GPU or audio.
- [Kick, ban and admins in community servers and group chats](member_moderation.md): The Members list of a community server (right side of the Servers page) and of a group chat (group menu, Members) now has moderation.
- [Multiplayer](multiplayer.md): Multiplayer in the launcher (Steam-style) and the multiplayer audit.
- [Net providers: extension points only](net_providers.md): The engine has two provider seams, `INetTransport` (realtime transport) and `INetBackend` (online services).
- [Password reset page (Supabase)](password-reset.md): "Forgot password?" in the launcher asks Supabase to email a reset link.
- [Photon Realtime transport (opt-in)](photon.md): Extension point only. It works only in a C++ build you make yourself with Photon's SDK, and nothing in the editor or the scripts selects it.
- [Relay & NAT traversal](relay.md): Most players are behind a home router (NAT): they can connect *out* to the internet, but nothing on the internet can connect *in* to them without port-forwarding.
- [Security (plan SEC1)](security.md): Four protections, one section each: what was wrong, what changed, how you see it working, what is not done.
- [Supabase backend for OkaySpace](supabase-backend.md): OkaySpace's online features — accounts, **cloud saves**, and **leaderboards** — run on a [Supabase](https://supabase.com) project.
