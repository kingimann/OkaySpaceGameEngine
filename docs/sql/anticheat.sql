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
-- Retention: public.ac_purge_old() deletes reports and decided appeals older
-- than 90 days, and bans that ended (lifted or expired) more than 90 days ago.
-- With the pg_cron extension enabled, it runs every night (job "okay-ac-retention");
-- without it, run `select public.ac_purge_old();` yourself (or from a scheduled
-- Edge Function).

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
language plpgsql stable security definer set search_path = public as $$
begin
  if auth.uid() is null then return; end if;
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
  perform set_config('okay.ac_purge', 'off', true);
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
  end if;
end $$;
