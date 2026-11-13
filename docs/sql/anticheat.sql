-- OkaySpace anti-cheat backend (opt-in; docs/anticheat.md): player reports,
-- anti-cheat bans and ban appeals. Run once in the SQL editor AFTER admin.sql
-- (it uses public.is_admin and public.admin_log_write). Idempotent: safe to re-run.
--
-- Who can do what (row-level security):
--   players  file reports (as themselves; a few an hour), see their own
--            anti-cheat bans (by account, or by this machine's hardware hash
--            through ac_my_bans) and appeal them (ac_file_appeal), and see their
--            own appeals
--   admins   (public.is_admin) see and review every report, ban / unban
--            accounts and machines, and accept or reject appeals; accepting an
--            appeal lifts its ban. Every admin action goes to admin_log
--            (triggers): reviews, bans, unbans, any change to a ban's until /
--            reason / lift reason, decisions, and deleting a report, ban or
--            appeal.
--
-- Hardware hashes are salted HMAC-SHA256 values made on the player's machine
-- (shared/AntiCheatHardware.hpp): the raw hardware IDs never reach this database.
-- A machine without a real machine id sends no hash at all, so a machine ban
-- always names one machine (never "every PC with this CPU").
--
-- Bans are separate from site_bans (admin.sql): an anti-cheat ban says "this
-- account / machine cheated". They're enforced when a player asks for a session
-- ticket: the session-ticket Edge function (supabase/functions/session-ticket)
-- calls public.ac_is_banned(user, hw_hash) with its own service key and signs
-- no ticket for a banned account or machine, so servers that require tickets
-- keep banned players out. Game servers must NOT be given the service_role key:
-- it can read and change the whole database. They need only the ticket service's
-- public key. Admins can call ac_is_banned too; players see only their own bans,
-- through ac_my_bans. Nothing else is blocked by this file.
--
-- Deleting an account deletes its account-only bans, but a ban that also names
-- a machine stays (user_id becomes null): a cheater can't clear a machine ban
-- by deleting the account and signing up again.
--
-- Abuse limits (all enforced here, in the database, so no client can skip them):
--   reports  20 an hour and 60 a day per player; the same player can't be
--            reported again within 10 minutes, nor more than 3 times a day, by
--            the same reporter; a reporter whose last 10 reports in 30 days were
--            all dismissed (none actioned) is paused until that clears.
--   appeals  one every hour and 5 a day per player, 3 per ban, one open per ban
--   lookups  ac_my_bans: 120 an hour per player
-- Ban evasion: ac_devices records which machine hashes an account used (written
-- by the ticket service through ac_note_device, readable by admins only);
-- ac_linked_accounts shows other accounts on the same machines;
-- ac_ban_with_devices bans an account and every machine it used in one step.
-- A ban that names a machine blocks a new account made on it (ac_is_banned), and
-- using a banned machine from another account is written to the audit log.
-- Shared machines exist (a household, a cafe): linking is a lead for an admin
-- to look at, never an automatic ban.
-- Audit: public.ac_audit is an append-only record of EVERY change to bans,
-- appeals, report reviews and the server-account list, including changes made
-- from the SQL editor or with the service key (admin_log only sees signed-in
-- admins). Nobody can edit or delete its rows through the API; only
-- ac_purge_audit() (admins / the SQL editor) removes entries older than a year.
--
-- Retention: public.ac_purge_old() deletes reports and decided appeals older
-- than 90 days, bans that ended (lifted or expired) more than 90 days ago, and
-- machine links not seen for 90 days.
-- With the pg_cron extension enabled, it runs every night (job "okay-ac-retention");
-- without it, run `select public.ac_purge_old();` yourself (or from a scheduled
-- Edge Function).

-- ---------------------------------------------------------------------------
-- Rate limiting (used by the functions below; nobody calls it directly)
-- ---------------------------------------------------------------------------
create table if not exists public.ac_rate_events (
  key text not null,
  at  timestamptz not null default now()
);
create index if not exists ac_rate_events_key on public.ac_rate_events (key, at desc);
alter table public.ac_rate_events enable row level security;   -- no policy: only the definer functions below touch it
revoke all on public.ac_rate_events from public, anon, authenticated;

-- Count one use of `p_key`; refuse when it was already used `p_limit` times in the last `p_window`
-- (at most a day is remembered). The advisory lock makes two simultaneous calls count one after the other.
create or replace function public.ac_rate_hit(p_key text, p_limit int, p_window interval) returns void
language plpgsql security definer set search_path = public as $$
begin
  perform pg_advisory_xact_lock(hashtext('ac_rate:' || p_key));
  delete from public.ac_rate_events where key = p_key and at < now() - interval '1 day';
  if (select count(*) from public.ac_rate_events where key = p_key and at > now() - p_window) >= p_limit then
    raise exception 'Too many requests; try again later' using errcode = 'check_violation';
  end if;
  insert into public.ac_rate_events (key) values (p_key);
end $$;
revoke execute on function public.ac_rate_hit(text, int, interval) from public, anon, authenticated;

-- ---------------------------------------------------------------------------
-- Reports
-- ---------------------------------------------------------------------------
create table if not exists public.ac_reports (
  id          bigint generated always as identity primary key,
  reporter    uuid not null default auth.uid() references auth.users on delete cascade,
  reported    uuid references auth.users on delete cascade,   -- null: a client check about the reporter's own game
  kind        text not null default 'player' check (kind in ('player', 'client_check', 'server_kick')),
  game        text check (char_length(game) <= 120),
  reason      text not null check (char_length(reason) between 1 and 500),
  details     text check (char_length(details) <= 4000),
  hw_hash     text check (hw_hash is null or hw_hash ~ '^[0-9a-f]{64}$'),
  status      text not null default 'open' check (status in ('open', 'reviewing', 'actioned', 'dismissed')),
  review_note text check (char_length(review_note) <= 500),
  reviewed_by uuid,
  reviewed_at timestamptz,
  created_at  timestamptz not null default now(),
  check (kind = 'client_check' or reported is not null),
  check (reported is null or reported <> reporter)
);
-- 'server_kick' came later: an older install's check doesn't allow it.
alter table public.ac_reports drop constraint if exists ac_reports_kind_check;
alter table public.ac_reports add constraint ac_reports_kind_check check (kind in ('player', 'client_check', 'server_kick'));
create index if not exists ac_reports_status  on public.ac_reports (status, created_at desc);
create index if not exists ac_reports_created on public.ac_reports (created_at);
create index if not exists ac_reports_reporter on public.ac_reports (reporter, created_at desc);
alter table public.ac_reports enable row level security;

drop policy if exists "file reports"   on public.ac_reports;
drop policy if exists "admins read reports"   on public.ac_reports;
drop policy if exists "admins review reports" on public.ac_reports;
drop policy if exists "admins delete reports" on public.ac_reports;
create policy "file reports" on public.ac_reports for insert to authenticated
  with check (reporter = auth.uid() and status = 'open' and reviewed_by is null and review_note is null);
create policy "admins read reports"   on public.ac_reports for select using (public.is_admin(auth.uid()));
create policy "admins review reports" on public.ac_reports for update using (public.is_admin(auth.uid()))
  with check (public.is_admin(auth.uid()));
create policy "admins delete reports" on public.ac_reports for delete using (public.is_admin(auth.uid()));

-- Accounts that game servers file their anti-cheat kicks with (a dedicated
-- server's --report-kicks account). Only they file 'server_kick' reports, or
-- reasons that start with "[server check]". Admins add and remove them.
create table if not exists public.ac_report_servers (
  user_id    uuid primary key references auth.users on delete cascade,
  note       text check (char_length(note) <= 200),
  added_by   uuid default auth.uid(),
  created_at timestamptz not null default now()
);
alter table public.ac_report_servers enable row level security;
drop policy if exists "admins manage report servers" on public.ac_report_servers;
create policy "admins manage report servers" on public.ac_report_servers for all
  using (public.is_admin(auth.uid())) with check (public.is_admin(auth.uid()));

-- New reports: from the caller, open, rate-limited. Reviews: stamped and logged.
create or replace function public.ac_reports_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if tg_op = 'INSERT' then
    if auth.uid() is not null and not public.is_admin(auth.uid()) then
      new.reporter := auth.uid(); new.status := 'open';
      new.review_note := null; new.reviewed_by := null; new.reviewed_at := null; new.created_at := now();
      if (select count(*) from public.ac_reports r
          where r.reporter = auth.uid() and r.created_at > now() - interval '1 hour') >= 20 then
        raise exception 'Too many reports; try again later' using errcode = 'check_violation';
      end if;
      if (new.kind = 'server_kick' or ltrim(new.reason) ilike '[server check]%')
         and not exists (select 1 from public.ac_report_servers s where s.user_id = auth.uid()) then
        raise exception 'Only a game server''s account files server reports' using errcode = 'insufficient_privilege';
      end if;
      perform public.ac_rate_hit('report-day:' || auth.uid(), 60, interval '1 day');
      -- Player reports: no repeats, and no reporting for a while when nearly all of them were thrown out.
      if new.kind = 'player' and new.reported is not null then
        if exists (select 1 from public.ac_reports r where r.reporter = auth.uid() and r.reported = new.reported
                   and r.created_at > now() - interval '10 minutes') then
          raise exception 'You just reported this player; an admin will look at it' using errcode = 'check_violation';
        end if;
        if (select count(*) from public.ac_reports r where r.reporter = auth.uid() and r.reported = new.reported
            and r.created_at > now() - interval '1 day') >= 3 then
          raise exception 'You have already reported this player today' using errcode = 'check_violation';
        end if;
        if (select count(*) from public.ac_reports r where r.reporter = auth.uid() and r.kind = 'player'
            and r.status = 'dismissed' and r.created_at > now() - interval '30 days') >= 10
           and not exists (select 1 from public.ac_reports r where r.reporter = auth.uid()
                           and r.status = 'actioned' and r.created_at > now() - interval '30 days') then
          raise exception 'Your recent reports were all dismissed, so reporting is paused for now' using errcode = 'check_violation';
        end if;
      end if;
    end if;
    return new;
  end if;
  -- UPDATE (admins only, by RLS): what a report says stays as filed.
  new.reporter := old.reporter; new.reported := old.reported; new.kind := old.kind; new.game := old.game;
  new.reason := old.reason; new.details := old.details; new.hw_hash := old.hw_hash; new.created_at := old.created_at;
  if new.status is distinct from old.status or new.review_note is distinct from old.review_note then
    new.reviewed_by := auth.uid(); new.reviewed_at := now();
    if auth.uid() is not null then
      perform public.admin_log_write('ac_report_' || new.status, coalesce(new.reported, new.reporter),
                                     'report #' || new.id || coalesce(': ' || nullif(new.review_note, ''), ''));
    end if;
  end if;
  return new;
end $$;
drop trigger if exists ac_reports_guard on public.ac_reports;
create trigger ac_reports_guard before insert or update on public.ac_reports
  for each row execute function public.ac_reports_guard();

-- Deletes by an admin (RLS lets no one else delete) are logged, for all three
-- tables. Not logged: the SQL editor / pg_cron (no auth.uid()), rows that go
-- with a deleted account (the caller isn't an admin), and ac_purge_old, which
-- logs one summary line instead (it sets okay.ac_purge for its own transaction).
create or replace function public.ac_log_delete() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null or not public.is_admin(auth.uid())
     or coalesce(current_setting('okay.ac_purge', true), '') = 'on' then
    return old;
  end if;
  if tg_table_name = 'ac_reports' then
    perform public.admin_log_write('ac_report_delete', coalesce(old.reported, old.reporter),
                                   'report #' || old.id || ': ' || old.reason);
  elsif tg_table_name = 'ac_bans' then
    perform public.admin_log_write('ac_ban_delete', old.user_id,
      'ban #' || old.id || ': ' || old.reason || case when old.hw_hash is not null then ' (+ machine)' else '' end);
  elsif tg_table_name = 'ac_appeals' then
    perform public.admin_log_write('ac_appeal_delete', old.user_id, 'appeal #' || old.id || ' on ban #' || old.ban_id);
  end if;
  return old;
end $$;
drop trigger if exists ac_reports_log_delete on public.ac_reports;
create trigger ac_reports_log_delete after delete on public.ac_reports
  for each row execute function public.ac_log_delete();

-- ---------------------------------------------------------------------------
-- Bans
-- ---------------------------------------------------------------------------
create table if not exists public.ac_bans (
  id          bigint generated always as identity primary key,
  user_id     uuid references auth.users on delete set null,   -- null: the account was deleted (machine bans stay)
  hw_hash     text check (hw_hash is null or hw_hash ~ '^[0-9a-f]{64}$'),
  reason      text not null check (char_length(reason) between 1 and 300),
  until       timestamptz,                    -- null = permanent
  report_id   bigint references public.ac_reports on delete set null,
  banned_by   uuid default auth.uid(),
  created_at  timestamptz not null default now(),
  lifted      boolean not null default false, -- unban keeps the row (appeals point at it)
  lifted_at   timestamptz,
  lifted_by   uuid,
  lift_reason text check (char_length(lift_reason) <= 300),
  check (user_id is not null or hw_hash is not null)
);
-- (An earlier version of this file cascaded: switch an existing table over.)
alter table public.ac_bans drop constraint if exists ac_bans_user_id_fkey;
alter table public.ac_bans add constraint ac_bans_user_id_fkey
  foreign key (user_id) references auth.users on delete set null;
create index if not exists ac_bans_user on public.ac_bans (user_id) where not lifted;
create index if not exists ac_bans_hw   on public.ac_bans (hw_hash) where not lifted;
alter table public.ac_bans enable row level security;

-- Deleting an account: its account-only bans go with it (nothing left to ban);
-- bans that name a machine too keep the machine (the foreign key nulls user_id).
create or replace function public.ac_bans_user_deleted() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  delete from public.ac_bans where user_id = old.id and hw_hash is null;
  return old;
end $$;
drop trigger if exists ac_bans_user_deleted on auth.users;
create trigger ac_bans_user_deleted before delete on auth.users
  for each row execute function public.ac_bans_user_deleted();

drop policy if exists "see own bans"  on public.ac_bans;
drop policy if exists "admins ban"    on public.ac_bans;
drop policy if exists "admins edit bans"   on public.ac_bans;
drop policy if exists "admins delete bans" on public.ac_bans;
create policy "see own bans" on public.ac_bans for select
  using (user_id = auth.uid() or public.is_admin(auth.uid()));
create policy "admins ban" on public.ac_bans for insert to authenticated
  with check (public.is_admin(auth.uid()) and (user_id is null or user_id <> auth.uid()));
create policy "admins edit bans" on public.ac_bans for update using (public.is_admin(auth.uid()))
  with check (public.is_admin(auth.uid()));
create policy "admins delete bans" on public.ac_bans for delete
  using (public.is_admin(auth.uid()) and (user_id is null or user_id <> auth.uid()));

create or replace function public.ac_bans_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if tg_op = 'INSERT' then
    new.banned_by := coalesce(auth.uid(), new.banned_by); new.created_at := now();
    new.lifted := false; new.lifted_at := null; new.lifted_by := null; new.lift_reason := null;
    if new.until is not null and new.until <= now() then
      raise exception 'The ban would already be over' using errcode = 'check_violation';
    end if;
    if auth.uid() is not null then
      perform public.admin_log_write('ac_ban', new.user_id,
        new.reason || case when new.hw_hash is not null then ' (+ machine)' else '' end
                   || case when new.until is null then ' (permanent)'
                           else ' (until ' || to_char(new.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC)' end);
    end if;
    return new;
  end if;
  -- UPDATE. The account was deleted (the foreign key's "set null"): only that.
  if new.user_id is null and old.user_id is not null
     and not exists (select 1 from auth.users u where u.id = old.user_id) then
    new := old;
    new.user_id := null;
    return new;
  end if;
  -- No one lifts, shortens or rewrites a ban on their own account (an admin
  -- included, also by accepting their own appeal).
  if auth.uid() is not null and old.user_id = auth.uid() then
    raise exception 'You can''t change a ban on your own account' using errcode = 'insufficient_privilege';
  end if;
  -- Otherwise who and what stay; lifting is stamped once, and a lifted ban stays lifted.
  new.user_id := old.user_id; new.hw_hash := old.hw_hash; new.banned_by := old.banned_by;
  new.created_at := old.created_at; new.report_id := old.report_id;
  if old.lifted then
    new.lifted := true; new.lifted_at := old.lifted_at; new.lifted_by := old.lifted_by;
  elsif new.lifted then
    new.lifted_at := now(); new.lifted_by := auth.uid();
    if auth.uid() is not null then
      perform public.admin_log_write('ac_unban', new.user_id, 'ban #' || new.id || coalesce(': ' || nullif(new.lift_reason, ''), ''));
    end if;
  end if;
  -- Any other change an admin makes to a ban is logged too.
  if auth.uid() is not null and (new.until is distinct from old.until or new.reason is distinct from old.reason
                                 or (old.lifted and new.lift_reason is distinct from old.lift_reason)) then
    perform public.admin_log_write('ac_ban_edit', new.user_id, 'ban #' || new.id || ':'
      || case when new.until is distinct from old.until then
           ' until ' || coalesce(to_char(old.until at time zone 'utc', 'YYYY-MM-DD HH24:MI'), 'permanent') || ' -> '
                     || coalesce(to_char(new.until at time zone 'utc', 'YYYY-MM-DD HH24:MI') || ' UTC', 'permanent') || ';'
         else '' end
      || case when new.reason is distinct from old.reason then ' reason "' || old.reason || '" -> "' || new.reason || '";' else '' end
      || case when old.lifted and new.lift_reason is distinct from old.lift_reason then
           ' lift reason "' || coalesce(old.lift_reason, '') || '" -> "' || coalesce(new.lift_reason, '') || '";'
         else '' end);
  end if;
  return new;
end $$;
drop trigger if exists ac_bans_guard on public.ac_bans;
create trigger ac_bans_guard before insert or update on public.ac_bans
  for each row execute function public.ac_bans_guard();

drop trigger if exists ac_bans_log_delete on public.ac_bans;
create trigger ac_bans_log_delete after delete on public.ac_bans
  for each row execute function public.ac_log_delete();

-- For the ticket service and admins: is this account or machine banned now?
-- (Players see their own bans with ac_my_bans, not anyone's.) Admins share the
-- `authenticated` role with every player, so the grant can't tell them apart:
-- the function refuses any signed-in caller who isn't an admin. A caller with
-- no auth.uid() is the service_role key (the session-ticket Edge function, never
-- a game server) or the SQL editor; anon has no execute right at all. An empty /
-- malformed hash never matches.
create or replace function public.ac_is_banned(p_user uuid, p_hw_hash text default null) returns boolean
language plpgsql stable security definer set search_path = public as $$
begin
  if auth.uid() is not null and not public.is_admin(auth.uid()) then
    raise exception 'Only admins and game servers can look up bans' using errcode = 'insufficient_privilege';
  end if;
  return exists (select 1 from public.ac_bans b
                 where not b.lifted and (b.until is null or b.until > now())
                   and ((p_user is not null and b.user_id = p_user)
                        or (p_hw_hash ~ '^[0-9a-f]{64}$' and b.hw_hash = p_hw_hash)));
end $$;
revoke execute on function public.ac_is_banned(uuid, text) from public, anon, authenticated;
grant execute on function public.ac_is_banned(uuid, text) to authenticated, service_role;

-- ---------------------------------------------------------------------------
-- Appeals
-- ---------------------------------------------------------------------------
create table if not exists public.ac_appeals (
  id            bigint generated always as identity primary key,
  ban_id        bigint not null references public.ac_bans on delete cascade,
  user_id       uuid not null default auth.uid() references auth.users on delete cascade,
  message       text not null check (char_length(message) between 1 and 2000),
  status        text not null default 'open' check (status in ('open', 'accepted', 'rejected')),
  decision_note text check (char_length(decision_note) <= 500),
  decided_by    uuid,
  decided_at    timestamptz,
  created_at    timestamptz not null default now()
);
create unique index if not exists ac_appeals_one_open on public.ac_appeals (ban_id) where status = 'open';
create index if not exists ac_appeals_status on public.ac_appeals (status, created_at desc);
alter table public.ac_appeals enable row level security;

drop policy if exists "see own appeals"       on public.ac_appeals;
drop policy if exists "admins decide appeals" on public.ac_appeals;
drop policy if exists "admins delete appeals" on public.ac_appeals;
-- Filing goes through ac_file_appeal (no insert policy: it checks the ban is yours).
create policy "see own appeals" on public.ac_appeals for select
  using (user_id = auth.uid() or public.is_admin(auth.uid()));
create policy "admins decide appeals" on public.ac_appeals for update using (public.is_admin(auth.uid()))
  with check (public.is_admin(auth.uid()));
create policy "admins delete appeals" on public.ac_appeals for delete using (public.is_admin(auth.uid()));

-- Deciding: stamped, logged, and an accepted appeal lifts the ban.
create or replace function public.ac_appeals_guard() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  new.ban_id := old.ban_id; new.user_id := old.user_id; new.message := old.message; new.created_at := old.created_at;
  if old.status <> 'open' then   -- decided once
    new.status := old.status; new.decision_note := old.decision_note;
    new.decided_by := old.decided_by; new.decided_at := old.decided_at;
    return new;
  end if;
  if new.status <> 'open' then
    new.decided_by := auth.uid(); new.decided_at := now();
    if new.status = 'accepted' then
      update public.ac_bans set lifted = true, lift_reason = coalesce(nullif(new.decision_note, ''), 'appeal accepted')
       where id = new.ban_id and not lifted;
    end if;
    if auth.uid() is not null then
      perform public.admin_log_write('ac_appeal_' || new.status, new.user_id,
                                     'ban #' || new.ban_id || coalesce(': ' || nullif(new.decision_note, ''), ''));
    end if;
  end if;
  return new;
end $$;
drop trigger if exists ac_appeals_guard on public.ac_appeals;
create trigger ac_appeals_guard before update on public.ac_appeals
  for each row execute function public.ac_appeals_guard();
drop trigger if exists ac_appeals_log_delete on public.ac_appeals;
create trigger ac_appeals_log_delete after delete on public.ac_appeals
  for each row execute function public.ac_log_delete();

-- My bans (by account, or this machine's hash), newest first, each with its
-- latest appeal and how many appeals are left (3 per ban). Ended bans from the
-- last 30 days are included so a player sees how their appeal went.
drop function if exists public.ac_my_bans(text);
create or replace function public.ac_my_bans(p_hw_hash text default null)
returns table (id bigint, reason text, until timestamptz, created_at timestamptz, active boolean,
               appeal_status text, appeal_note text, appeals_left int)
language plpgsql security definer set search_path = public as $$
begin
  if auth.uid() is null then return; end if;
  perform public.ac_rate_hit('mybans:' || auth.uid(), 120, interval '1 hour');
  return query
    select b.id, b.reason, b.until, b.created_at,
           (not b.lifted and (b.until is null or b.until > now())),
           a.status, a.decision_note,
           greatest(0, 3 - (select count(*) from public.ac_appeals x where x.ban_id = b.id))::int
      from public.ac_bans b
      left join lateral (select ap.status, ap.decision_note from public.ac_appeals ap
                          where ap.ban_id = b.id order by ap.created_at desc limit 1) a on true
     where (b.user_id = auth.uid() or (p_hw_hash is not null and p_hw_hash ~ '^[0-9a-f]{64}$' and b.hw_hash = p_hw_hash))
       and ((not b.lifted and (b.until is null or b.until > now()))
            or coalesce(b.lifted_at, b.until) > now() - interval '30 days')
     order by b.created_at desc
     limit 20;
end $$;
revoke execute on function public.ac_my_bans(text) from public, anon;
grant execute on function public.ac_my_bans(text) to authenticated;

-- Appeal one of my active bans: one open appeal at a time, 3 in all.
create or replace function public.ac_file_appeal(p_ban bigint, p_message text, p_hw_hash text default null)
returns bigint language plpgsql security definer set search_path = public as $$
declare v_id bigint;
begin
  if auth.uid() is null then
    raise exception 'Sign in to appeal' using errcode = 'insufficient_privilege';
  end if;
  if p_message is null or char_length(btrim(p_message)) = 0 or char_length(p_message) > 2000 then
    raise exception 'Write why the ban should be lifted (up to 2000 characters)' using errcode = 'invalid_parameter_value';
  end if;
  perform public.ac_rate_hit('appeal-day:' || auth.uid(), 5, interval '1 day');   -- also stops probing ban ids
  if exists (select 1 from public.ac_appeals a where a.user_id = auth.uid() and a.created_at > now() - interval '1 hour') then
    raise exception 'Wait an hour between appeals' using errcode = 'check_violation';
  end if;
  if not exists (select 1 from public.ac_bans b
                 where b.id = p_ban and not b.lifted and (b.until is null or b.until > now())
                   and (b.user_id = auth.uid()
                        or (p_hw_hash is not null and p_hw_hash ~ '^[0-9a-f]{64}$' and b.hw_hash = p_hw_hash))) then
    raise exception 'That isn''t one of your active bans' using errcode = 'insufficient_privilege';
  end if;
  if exists (select 1 from public.ac_appeals a where a.ban_id = p_ban and a.status = 'open') then
    raise exception 'Your appeal is already waiting for a decision' using errcode = 'unique_violation';
  end if;
  if (select count(*) from public.ac_appeals a where a.ban_id = p_ban) >= 3 then
    raise exception 'No appeals left for this ban' using errcode = 'check_violation';
  end if;
  insert into public.ac_appeals (ban_id, user_id, message) values (p_ban, auth.uid(), btrim(p_message))
  returning id into v_id;
  return v_id;
end $$;
revoke execute on function public.ac_file_appeal(bigint, text, text) from public, anon;
grant execute on function public.ac_file_appeal(bigint, text, text) to authenticated;

-- ---------------------------------------------------------------------------
-- Ban evasion: which machines an account used
-- ---------------------------------------------------------------------------
create table if not exists public.ac_devices (
  user_id    uuid not null references auth.users on delete cascade,
  hw_hash    text not null check (hw_hash ~ '^[0-9a-f]{64}$'),
  first_seen timestamptz not null default now(),
  last_seen  timestamptz not null default now(),
  primary key (user_id, hw_hash)
);
create index if not exists ac_devices_hw on public.ac_devices (hw_hash);
alter table public.ac_devices enable row level security;
drop policy if exists "admins read devices" on public.ac_devices;
create policy "admins read devices" on public.ac_devices for select using (public.is_admin(auth.uid()));
revoke insert, update, delete, truncate on public.ac_devices from public, anon, authenticated;   -- written only by ac_note_device

-- The ticket service (service key) or an admin: this account just used this machine. Keeps the 20 newest machines per account.
-- True when the machine has an active ban that belongs to another account (or to no account): that is written to the audit log.
create or replace function public.ac_note_device(p_user uuid, p_hw_hash text) returns boolean
language plpgsql security definer set search_path = public as $$
declare evasion boolean;
begin
  if auth.uid() is not null and not public.is_admin(auth.uid()) then
    raise exception 'Only the ticket service and admins record devices' using errcode = 'insufficient_privilege';
  end if;
  if p_user is null or p_hw_hash is null or p_hw_hash !~ '^[0-9a-f]{64}$' then return false; end if;
  insert into public.ac_devices (user_id, hw_hash) values (p_user, p_hw_hash)
  on conflict (user_id, hw_hash) do update set last_seen = now();
  delete from public.ac_devices d where d.user_id = p_user and d.hw_hash in (
    select x.hw_hash from public.ac_devices x where x.user_id = p_user order by x.last_seen desc offset 20);
  select exists (select 1 from public.ac_bans b
                 where not b.lifted and (b.until is null or b.until > now()) and b.hw_hash = p_hw_hash
                   and b.user_id is distinct from p_user) into evasion;
  if evasion then
    insert into public.ac_audit (actor, actor_role, action, table_name, target, detail)
    values (auth.uid(), 'ticket-service', 'evasion_signal', 'ac_devices', p_user,
            jsonb_build_object('hw_hash_prefix', left(p_hw_hash, 12)));
  end if;
  return evasion;
end $$;
revoke execute on function public.ac_note_device(uuid, text) from public, anon, authenticated;
grant execute on function public.ac_note_device(uuid, text) to authenticated, service_role;

-- Admins: other accounts that used any machine this account used (a lead, not proof: machines can be shared).
create or replace function public.ac_linked_accounts(p_user uuid)
returns table (other_user uuid, shared_machines int, last_seen timestamptz, other_banned boolean)
language plpgsql stable security definer set search_path = public as $$
begin
  if auth.uid() is not null and not public.is_admin(auth.uid()) then
    raise exception 'Only admins can look up linked accounts' using errcode = 'insufficient_privilege';
  end if;
  return query
    select o.user_id, count(*)::int, max(o.last_seen),
           exists (select 1 from public.ac_bans b where b.user_id = o.user_id and not b.lifted and (b.until is null or b.until > now()))
      from public.ac_devices m
      join public.ac_devices o on o.hw_hash = m.hw_hash and o.user_id <> m.user_id
     where m.user_id = p_user
     group by o.user_id
     order by max(o.last_seen) desc
     limit 50;
end $$;
revoke execute on function public.ac_linked_accounts(uuid) from public, anon;
grant execute on function public.ac_linked_accounts(uuid) to authenticated;

-- Admins: ban an account and every machine it used, in one step (runs with the caller's rights, so
-- the "admins ban" policy and the ban guard apply to every row). Returns how many ban rows were made.
create or replace function public.ac_ban_with_devices(p_user uuid, p_reason text, p_until timestamptz default null, p_report bigint default null)
returns int language plpgsql set search_path = public as $$
declare n int := 0; h record;
begin
  insert into public.ac_bans (user_id, reason, until, report_id) values (p_user, p_reason, p_until, p_report);
  n := 1;
  for h in select d.hw_hash from public.ac_devices d where d.user_id = p_user loop
    if not exists (select 1 from public.ac_bans b where b.hw_hash = h.hw_hash and not b.lifted and (b.until is null or b.until > now())) then
      insert into public.ac_bans (user_id, hw_hash, reason, until, report_id)
      values (p_user, h.hw_hash, left(p_reason || ' (machine of this account)', 300), p_until, p_report);
      n := n + 1;
    end if;
  end loop;
  return n;
end $$;
revoke execute on function public.ac_ban_with_devices(uuid, text, timestamptz, bigint) from public, anon;
grant execute on function public.ac_ban_with_devices(uuid, text, timestamptz, bigint) to authenticated;

-- ---------------------------------------------------------------------------
-- Audit: an append-only record of every change to bans, appeals, report reviews and the server accounts
-- ---------------------------------------------------------------------------
create table if not exists public.ac_audit (
  id         bigint generated always as identity primary key,
  at         timestamptz not null default now(),
  actor      uuid,            -- auth.uid(); null = the SQL editor, the service key, pg_cron or a cascade
  actor_role text,            -- the JWT role or database role that made the change
  action     text not null,   -- ban_insert, ban_update, ban_delete, appeal_decision, appeal_delete, report_review, report_delete, ...
  table_name text not null,
  row_id     bigint,
  target     uuid,            -- the account it is about, when there is one
  detail     jsonb not null default '{}'::jsonb
);
create index if not exists ac_audit_at on public.ac_audit (at desc);
create index if not exists ac_audit_target on public.ac_audit (target, at desc);
alter table public.ac_audit enable row level security;
drop policy if exists "admins read audit" on public.ac_audit;
create policy "admins read audit" on public.ac_audit for select using (public.is_admin(auth.uid()));
revoke insert, update, delete, truncate on public.ac_audit from public, anon, authenticated;

create or replace function public.ac_audit_immutable() returns trigger
language plpgsql set search_path = public as $$
begin
  if coalesce(current_setting('okay.ac_audit_purge', true), '') = 'on' then
    if tg_op = 'DELETE' then return old; end if;
    return null;
  end if;
  raise exception 'The anti-cheat audit log is append-only' using errcode = 'insufficient_privilege';
end $$;
drop trigger if exists ac_audit_immutable on public.ac_audit;
create trigger ac_audit_immutable before update or delete on public.ac_audit
  for each row execute function public.ac_audit_immutable();
drop trigger if exists ac_audit_no_truncate on public.ac_audit;
create trigger ac_audit_no_truncate before truncate on public.ac_audit
  for each statement execute function public.ac_audit_immutable();

create or replace function public.ac_audit_role() returns text
language plpgsql stable as $$
declare r text;
begin
  begin r := nullif(current_setting('request.jwt.claims', true), '')::jsonb ->> 'role';
  exception when others then r := null; end;
  return coalesce(r, current_user::text);
end $$;

create or replace function public.ac_audit_row() returns trigger
language plpgsql security definer set search_path = public as $$
declare act text; rid bigint; tgt uuid; snap jsonb;
begin
  if coalesce(current_setting('okay.ac_purge', true), '') = 'on' then return coalesce(new, old); end if;   -- the retention job writes one summary
  if tg_table_name = 'ac_bans' then
    act := 'ban_' || lower(tg_op);
    if tg_op = 'UPDATE' and to_jsonb(new) = to_jsonb(old) then return new; end if;
    rid := coalesce(new.id, old.id); tgt := coalesce(new.user_id, old.user_id);
  elsif tg_table_name = 'ac_appeals' then
    if tg_op = 'INSERT' then act := 'appeal_insert';
    elsif tg_op = 'UPDATE' then
      if new.status is not distinct from old.status and new.decision_note is not distinct from old.decision_note then return new; end if;
      act := 'appeal_decision';
    else act := 'appeal_delete'; end if;
    rid := coalesce(new.id, old.id); tgt := coalesce(new.user_id, old.user_id);
  elsif tg_table_name = 'ac_reports' then
    if tg_op = 'UPDATE' then
      if new.status is not distinct from old.status and new.review_note is not distinct from old.review_note then return new; end if;
      act := 'report_review';
    elsif tg_op = 'DELETE' then act := 'report_delete';
    else return new; end if;                       -- filing a report is not an admin action
    rid := coalesce(new.id, old.id); tgt := coalesce(new.reported, old.reported, new.reporter, old.reporter);
  else                                              -- ac_report_servers
    act := 'server_account_' || lower(tg_op);
    tgt := coalesce(new.user_id, old.user_id);
  end if;
  snap := case tg_op when 'INSERT' then jsonb_build_object('new', to_jsonb(new))
                     when 'DELETE' then jsonb_build_object('old', to_jsonb(old))
                     else jsonb_build_object('old', to_jsonb(old), 'new', to_jsonb(new)) end;
  insert into public.ac_audit (actor, actor_role, action, table_name, row_id, target, detail)
  values (auth.uid(), public.ac_audit_role(), act, tg_table_name::text, rid, tgt, snap);
  return coalesce(new, old);
end $$;
drop trigger if exists ac_bans_audit on public.ac_bans;
create trigger ac_bans_audit after insert or update or delete on public.ac_bans
  for each row execute function public.ac_audit_row();
drop trigger if exists ac_appeals_audit on public.ac_appeals;
create trigger ac_appeals_audit after insert or update or delete on public.ac_appeals
  for each row execute function public.ac_audit_row();
drop trigger if exists ac_reports_audit on public.ac_reports;
create trigger ac_reports_audit after update or delete on public.ac_reports
  for each row execute function public.ac_audit_row();
drop trigger if exists ac_report_servers_audit on public.ac_report_servers;
create trigger ac_report_servers_audit after insert or update or delete on public.ac_report_servers
  for each row execute function public.ac_audit_row();

-- Audit entries older than a year (the SQL editor, pg_cron or an admin). Leaves one line saying so.
create or replace function public.ac_purge_audit() returns bigint
language plpgsql security definer set search_path = public as $$
declare n bigint;
begin
  if auth.uid() is not null and not public.is_admin(auth.uid()) then
    raise exception 'Only admins purge the audit log' using errcode = 'insufficient_privilege';
  end if;
  perform set_config('okay.ac_audit_purge', 'on', true);
  delete from public.ac_audit where at < now() - interval '365 days';
  get diagnostics n = row_count;
  perform set_config('okay.ac_audit_purge', 'off', true);
  insert into public.ac_audit (actor, actor_role, action, table_name, detail)
  values (auth.uid(), public.ac_audit_role(), 'audit_purge', 'ac_audit', jsonb_build_object('removed', n));
  return n;
end $$;
revoke execute on function public.ac_purge_audit() from public, anon;
grant execute on function public.ac_purge_audit() to authenticated;

-- ---------------------------------------------------------------------------
-- Retention: 90 days
-- ---------------------------------------------------------------------------
create or replace function public.ac_purge_old() returns table (reports bigint, appeals bigint, bans bigint)
language plpgsql security definer set search_path = public as $$
declare r bigint; a bigint; b bigint;
begin
  -- The SQL editor / pg_cron (no auth.uid()) or an admin.
  if auth.uid() is not null and not public.is_admin(auth.uid()) then
    raise exception 'Only admins purge anti-cheat data' using errcode = 'insufficient_privilege';
  end if;
  perform set_config('okay.ac_purge', 'on', true);   -- one summary line, not a line per row
  delete from public.ac_appeals where status <> 'open' and coalesce(decided_at, created_at) < now() - interval '90 days';
  get diagnostics a = row_count;
  delete from public.ac_bans
   where (lifted and lifted_at < now() - interval '90 days')
      or (not lifted and until is not null and until < now() - interval '90 days');
  get diagnostics b = row_count;
  delete from public.ac_reports where created_at < now() - interval '90 days';
  get diagnostics r = row_count;
  delete from public.ac_devices where last_seen < now() - interval '90 days';
  delete from public.ac_rate_events where at < now() - interval '1 day';
  perform set_config('okay.ac_purge', 'off', true);
  insert into public.ac_audit (actor, actor_role, action, table_name, detail)
  values (auth.uid(), public.ac_audit_role(), 'purge', 'ac_*', jsonb_build_object('reports', r, 'appeals', a, 'bans', b));
  if auth.uid() is not null then
    perform public.admin_log_write('ac_purge', null, r || ' reports, ' || a || ' appeals, ' || b || ' bans older than 90 days');
  end if;
  return query select r, a, b;
end $$;
revoke execute on function public.ac_purge_old() from public, anon;
grant execute on function public.ac_purge_old() to authenticated;

do $$
begin
  if exists (select 1 from pg_extension where extname = 'pg_cron') then
    if exists (select 1 from cron.job where jobname = 'okay-ac-retention') then
      perform cron.unschedule('okay-ac-retention');
    end if;
    perform cron.schedule('okay-ac-retention', '17 3 * * *', 'select public.ac_purge_old()');
    if exists (select 1 from cron.job where jobname = 'okay-ac-audit-retention') then
      perform cron.unschedule('okay-ac-audit-retention');
    end if;
    perform cron.schedule('okay-ac-audit-retention', '41 3 1 * *', 'select public.ac_purge_audit()');
  end if;
end $$;
