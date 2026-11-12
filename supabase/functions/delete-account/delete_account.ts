// Logic of the delete-account Edge Function, kept free of Deno APIs so it runs
// under Deno (the Edge runtime) and under Node (supabase/functions/delete-account/
// delete_account_test.ts, `node --experimental-strip-types`). index.ts wires it to
// real fetch calls; the tests wire it to a fake. Erasable TypeScript only.
//
// Contract (docs/account_data.md):
//   POST /functions/v1/delete-account
//   Authorization: Bearer <the caller's Supabase access token>
//   body: {"confirm_username": "<their username>", "password": "<their password>"}
//   200 {"ok":true,"steps":[...]}  401 no/invalid token  400 bad body / name mismatch
//   403 wrong password or account restricted  405 not POST
//   500 {"error","failed_step","completed":[...]}  a step failed; nothing after it ran
//
// The user id comes ONLY from the access token (Deps.userFromJwt). A user id,
// email or any other identity field in the request body is never read.

export interface AuthUser { id: string; email: string | null }
export type Rest = { ok: boolean; status: number; missing?: boolean; body?: unknown };

/** Everything the function needs from the outside world. Service-role access
 *  lives behind this interface, inside index.ts, and nowhere else. */
export interface Deps {
  /** Validate the bearer token with Supabase Auth; null when invalid. */
  userFromJwt(jwt: string): Promise<AuthUser | null>;
  /** Re-check the password (a fresh sign-in) for this email. */
  passwordOk(email: string, password: string): Promise<boolean>;
  /** PostgREST call as the service role. `missing` = table/column not installed. */
  rest(method: "GET" | "DELETE" | "PATCH", path: string, body?: unknown): Promise<Rest>;
  /** Delete every object under `<uid>/` in a storage bucket. */
  purgeBucket(bucket: string, uid: string): Promise<{ ok: boolean; missing?: boolean; removed: number; error?: string }>;
  /** auth.admin.deleteUser */
  deleteAuthUser(uid: string): Promise<{ ok: boolean; error?: string }>;
}

export type Kind = "anonymise" | "delete";
export interface Step { id: string; kind: Kind; table: string; filter: (uid: string) => string; patch?: unknown }

const eq = (col: string) => (uid: string) => `${col}=eq.${uid}`;

// Every table in docs/sql/*.sql and docs/supabase-backend.md that holds a user's
// data. Order: anonymise records that must outlive the account, then content
// that depends on other content (children before parents), then the profile.
// Tables a project never created are reported "skipped", not failed.
export const STEPS: Step[] = [
  // --- anonymised: the row stays, the link to the person goes ---
  { id: "admin_log.actor", kind: "anonymise", table: "admin_log", filter: eq("actor"), patch: { actor: null, actor_name: "[deleted]" } },
  { id: "admin_log.target", kind: "anonymise", table: "admin_log", filter: eq("target"), patch: { target: null, target_name: "[deleted]" } },
  // a ban row with no machine hash would break ac_bans' check once user_id is null: delete those
  { id: "ac_bans.account_only", kind: "delete", table: "ac_bans", filter: (u) => `user_id=eq.${u}&hw_hash=is.null` },
  { id: "ac_bans.user_id", kind: "anonymise", table: "ac_bans", filter: eq("user_id"), patch: { user_id: null } },
  { id: "matches.reporter", kind: "anonymise", table: "matches", filter: eq("reporter"), patch: { reporter: null } },
  // rows an admin/moderator created stay for the people they concern; only the link to the issuer goes
  { id: "ac_bans.banned_by", kind: "anonymise", table: "ac_bans", filter: eq("banned_by"), patch: { banned_by: null } },
  { id: "ac_report_servers.added_by", kind: "anonymise", table: "ac_report_servers", filter: eq("added_by"), patch: { added_by: null } },
  { id: "forum_section_bans.banned_by", kind: "anonymise", table: "forum_section_bans", filter: eq("banned_by"), patch: { banned_by: null } },
  // a ban outlives the moderator's account: keep the row, drop the link to who issued it
  { id: "community_bans.banned_by", kind: "anonymise", table: "community_bans", filter: eq("banned_by"), patch: { banned_by: null } },
  // --- deleted ---
  { id: "message_reactions", kind: "delete", table: "message_reactions", filter: eq("reactor") },
  { id: "forum_votes", kind: "delete", table: "forum_votes", filter: eq("voter") },
  { id: "forum_posts", kind: "delete", table: "forum_posts", filter: eq("author") },
  { id: "forum_threads", kind: "delete", table: "forum_threads", filter: eq("author") },
  { id: "forum_section_members", kind: "delete", table: "forum_section_members", filter: eq("user_id") },
  { id: "forum_section_bans", kind: "delete", table: "forum_section_bans", filter: eq("user_id") },
  { id: "forum_sections", kind: "delete", table: "forum_sections", filter: eq("owner") },
  { id: "workshop_reviews", kind: "delete", table: "workshop_reviews", filter: eq("reviewer") },
  { id: "workshop_items", kind: "delete", table: "workshop_items", filter: eq("author") },
  { id: "channel_messages", kind: "delete", table: "channel_messages", filter: eq("author") },
  { id: "server_members", kind: "delete", table: "server_members", filter: eq("member") },
  { id: "servers", kind: "delete", table: "servers", filter: eq("owner") },
  { id: "group_messages", kind: "delete", table: "group_messages", filter: eq("sender") },
  { id: "group_members", kind: "delete", table: "group_members", filter: eq("member") },
  { id: "group_convos", kind: "delete", table: "group_convos", filter: eq("owner") },
  { id: "message_edits", kind: "delete", table: "message_edits", filter: eq("editor") },
  { id: "typing_state.sender", kind: "delete", table: "typing_state", filter: eq("sender") },
  { id: "typing_state.peer", kind: "delete", table: "typing_state", filter: eq("peer") },
  { id: "message_reads.reader", kind: "delete", table: "message_reads", filter: eq("reader") },
  { id: "message_reads.peer", kind: "delete", table: "message_reads", filter: eq("peer") },
  { id: "community_bans.user_id", kind: "delete", table: "community_bans", filter: eq("user_id") },
  { id: "blocks.blocker", kind: "delete", table: "blocks", filter: eq("blocker") },
  { id: "blocks.blocked", kind: "delete", table: "blocks", filter: eq("blocked") },
  { id: "privacy_settings", kind: "delete", table: "privacy_settings", filter: eq("user_id") },
  { id: "message_pins", kind: "delete", table: "message_pins", filter: eq("pinned_by") },
  { id: "messages.sent", kind: "delete", table: "messages", filter: eq("sender") },
  { id: "messages.received", kind: "delete", table: "messages", filter: eq("recipient") },
  { id: "friendships.requested", kind: "delete", table: "friendships", filter: eq("requester") },
  { id: "friendships.received", kind: "delete", table: "friendships", filter: eq("addressee") },
  { id: "follows.following", kind: "delete", table: "follows", filter: eq("follower") },
  { id: "follows.followers", kind: "delete", table: "follows", filter: eq("followee") },
  { id: "activity", kind: "delete", table: "activity", filter: eq("actor") },
  { id: "user_achievements", kind: "delete", table: "user_achievements", filter: eq("owner") },
  { id: "user_playtime", kind: "delete", table: "user_playtime", filter: eq("owner") },
  { id: "profile_badges", kind: "delete", table: "profile_badges", filter: eq("owner") },
  { id: "cloud_saves", kind: "delete", table: "cloud_saves", filter: eq("user_id") },
  { id: "leaderboards", kind: "delete", table: "leaderboards", filter: eq("user_id") },
  { id: "player_stats", kind: "delete", table: "player_stats", filter: eq("user_id") },
  { id: "match_players", kind: "delete", table: "match_players", filter: eq("user_id") },
  { id: "match_tickets", kind: "delete", table: "match_tickets", filter: eq("owner") },
  { id: "party_members", kind: "delete", table: "party_members", filter: eq("member") },
  { id: "parties", kind: "delete", table: "parties", filter: eq("leader") },
  { id: "game_sessions", kind: "delete", table: "game_sessions", filter: eq("host_id") },
  { id: "ac_reports.filed", kind: "delete", table: "ac_reports", filter: eq("reporter") },
  { id: "ac_reports.about_me", kind: "delete", table: "ac_reports", filter: eq("reported") },
  { id: "ac_appeals", kind: "delete", table: "ac_appeals", filter: eq("user_id") },
  { id: "ac_report_servers", kind: "delete", table: "ac_report_servers", filter: eq("user_id") },
  { id: "profiles", kind: "delete", table: "profiles", filter: eq("id") },
];

export const BUCKETS = ["chat", "workshop"];

export interface StepResult { step: string; status: "done" | "skipped" }

function json(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export async function handleDeleteAccount(req: Request, deps: Deps): Promise<Response> {
  if (req.method === "OPTIONS") return new Response(null, { status: 204 });
  if (req.method !== "POST") return json(405, { error: "POST only" });

  // 1. Who is calling: the token, and nothing else.
  const auth = req.headers.get("authorization") ?? "";
  const m = /^Bearer\s+(\S+)$/i.exec(auth);
  if (!m) return json(401, { error: "sign in required" });
  const user = await deps.userFromJwt(m[1]).catch(() => null);
  if (!user || !UUID.test(user.id)) return json(401, { error: "invalid or expired session" });
  const uid = user.id;

  // 2. The body only carries confirmations, never identity.
  let body: { confirm_username?: unknown; password?: unknown } | null = null;
  try { body = await req.json(); } catch { body = null; }
  const confirm = typeof body?.confirm_username === "string" ? body.confirm_username.trim() : "";
  const password = typeof body?.password === "string" ? body.password : "";
  if (!confirm || !password) return json(400, { error: "confirm_username and password are required" });

  // 3. The typed name must be this account's username.
  const prof = await deps.rest("GET", `profiles?select=username&id=eq.${uid}`);
  if (!prof.ok && !prof.missing) return json(500, { error: "could not read the profile", failed_step: "profile_lookup", completed: [] });
  const rows = Array.isArray(prof.body) ? prof.body as Array<{ username?: string }> : [];
  const username = rows[0]?.username ?? "";
  if (!username || username.toLowerCase() !== confirm.toLowerCase())
    return json(400, { error: "the typed username does not match this account" });

  // 4. Fresh sign-in re-check.
  if (!user.email || !(await deps.passwordOk(user.email, password).catch(() => false)))
    return json(403, { error: "wrong password" });

  // 5. A site-banned account may not delete itself (that would lift the ban).
  const ban = await deps.rest("GET", `site_bans?select=user_id&user_id=eq.${uid}`);
  if (ban.ok && Array.isArray(ban.body) && ban.body.length > 0)
    return json(403, { error: "this account is restricted and cannot be deleted; contact support" });

  // 6. Run the steps in order; stop at the first real failure and say which.
  const done: StepResult[] = [];
  const fail = (step: string, why: string) =>
    json(500, { error: `step "${step}" failed: ${why}`, failed_step: step, completed: done.map((d) => d.step) });

  for (const s of STEPS) {
    const q = `${s.table}?${s.filter(uid)}`;
    const r = s.kind === "delete"
      ? await deps.rest("DELETE", q).catch((e) => ({ ok: false, status: 0, body: String(e) } as Rest))
      : await deps.rest("PATCH", q, s.patch).catch((e) => ({ ok: false, status: 0, body: String(e) } as Rest));
    if (r.missing) { done.push({ step: s.id, status: "skipped" }); continue; }
    if (!r.ok) return fail(s.id, `HTTP ${r.status}`);
    done.push({ step: s.id, status: "done" });
  }
  for (const b of BUCKETS) {
    const id = `storage.${b}`;
    const r = await deps.purgeBucket(b, uid).catch((e) => ({ ok: false, removed: 0, error: String(e) }));
    if (r.missing) { done.push({ step: id, status: "skipped" }); continue; }
    if (!r.ok) return fail(id, r.error ?? "storage error");
    done.push({ step: id, status: "done" });
  }
  const del = await deps.deleteAuthUser(uid).catch((e) => ({ ok: false, error: String(e) }));
  if (!del.ok) return fail("auth_user", del.error ?? "auth error");
  done.push({ step: "auth_user", status: "done" });

  return json(200, { ok: true, steps: done });
}
