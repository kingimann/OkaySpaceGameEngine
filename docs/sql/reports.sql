-- OkaySpace user reports: report a player, a chat message, a forum post or
-- comment, a marketplace listing or a community server. Run once in your
-- Supabase project's SQL editor AFTER social.sql, profile_extras.sql,
-- forum_sections.sql, group_chats.sql and admin.sql (it uses public.is_global_mod
-- and public.admin_log_write from admin.sql). Idempotent: re-running is safe.
-- Until it has run the launcher tells the person reporting that reports are not
-- set up on this server yet (it never says "Report sent" unless the server
-- accepted the row).
--
-- This is separate from docs/sql/anticheat.sql (ac_reports = "Report cheating",
-- reviewed under Admin > Anti-cheat). reports is for abuse, spam, scams and
-- illegal content, reviewed under Admin > Reports by every moderator.
--
-- Who can do what (enforced here by RLS, triggers and column grants):
--   * any signed-in player files a report as themselves, once per target per
--     7 days, at most 10 per hour, never about themselves or about something
--     they cannot see (a DM they are not in, a server they are not in);
--   * a player reads only their own reports;
--   * moderators / staff / admins (public.is_global_mod, rank >= 1) read every
--     report and change only its status (open / resolved / dismissed) and
--     review_note; each change is written to admin_log;
--   * nobody deletes reports through the API (the SQL editor can).
-- The server fills in target_user (who owns the thing) and snapshot (what it
-- said when it was reported, so removing it does not remove the evidence).

create table if not exists public.reports (
  id          bigint generated always as identity primary key,
  reporter    uuid not null default auth.uid() references auth.users on delete cascade,
  target_type text not null,
  target_id   text not null,                       -- uuid (user) or bigint as text (everything else)
  target_user uuid references auth.users on delete set null,   -- owner of the thing; set by the server
  reason      text not null,
  note        text check (note is null or char_length(note) <= 500),
  snapshot    text check (snapshot is null or char_length(snapshot) <= 400),   -- set by the server
  status      text not null default 'open',
  review_note text check (review_note is null or char_length(review_note) <= 500),
  reviewed_by uuid,
  reviewed_at timestamptz,
  created_at  timestamptz not null default now()
);
alter table public.reports drop constraint if exists reports_target_type_check;
alter table public.reports add constraint reports_target_type_check check (target_type in
  ('user', 'message', 'group_message', 'forum_thread', 'forum_post', 'market_item', 'server'));
alter table public.reports drop constraint if exists reports_reason_check;
alter table public.reports add constraint reports_reason_check check (reason in
  ('spam', 'harassment', 'hate', 'sexual', 'violence', 'scam', 'illegal', 'impersonation', 'cheating', 'other'));
alter table public.reports drop constraint if exists reports_status_check;
alter table public.reports add constraint reports_status_check check (status in ('open', 'resolved', 'dismissed'));
alter table public.reports drop constraint if exists reports_target_id_check;
alter table public.reports add constraint reports_target_id_check check (
  case when target_type = 'user'
       then target_id ~ '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
       else target_id ~ '^[0-9]{1,18}$' end);
create index if not exists reports_status_idx   on public.reports (status, created_at desc);
create index if not exists reports_reporter_idx on public.reports (reporter, created_at desc);
create index if not exists reports_target_idx   on public.reports (target_type, target_id);
alter table public.reports enable row level security;

-- Insert / update guard. SECURITY DEFINER so it can look the target up whatever
-- the reporter's own RLS lets them read; membership and party checks below keep
-- people from probing things they cannot see.
create or replace function public.reports_guard() returns trigger
language plpgsql security definer set search_path = public as $$
declare
  me uuid := auth.uid();
  owner_id uuid;
  snap text;
  found boolean := false;
begin
  if tg_op = 'INSERT' then
    if me is not null then
      new.reporter := me; new.status := 'open';
      new.review_note := null; new.reviewed_by := null; new.reviewed_at := null;
      new.created_at := now(); new.target_user := null; new.snapshot := null;
    end if;

    if new.target_type = 'user' then
      select p.id, p.username into owner_id, snap from public.profiles p where p.id = new.target_id::uuid;
      found := owner_id is not null;
    elsif new.target_type = 'message' then
      select m.sender, m.body into owner_id, snap from public.messages m
        where m.id = new.target_id::bigint and (me is null or me in (m.sender, m.recipient));
      found := owner_id is not null;
    elsif new.target_type = 'group_message' then
      select m.sender, m.body into owner_id, snap from public.group_messages m
        where m.id = new.target_id::bigint and (me is null or public.is_group_member(m.convo_id));
      found := owner_id is not null;
    elsif new.target_type = 'forum_thread' then
      select t.author, t.title into owner_id, snap from public.forum_threads t where t.id = new.target_id::bigint;
      found := owner_id is not null;
    elsif new.target_type = 'forum_post' then
      select p.author, p.body into owner_id, snap from public.forum_posts p where p.id = new.target_id::bigint;
      found := owner_id is not null;
    elsif new.target_type = 'market_item' then
      select w.author, w.title into owner_id, snap from public.workshop_items w where w.id = new.target_id::bigint;
      found := owner_id is not null;
    elsif new.target_type = 'server' then
      select s.owner, s.name into owner_id, snap from public.servers s
        where s.id = new.target_id::bigint and (me is null or public.is_server_member(s.id) or s.owner = me);
      found := owner_id is not null;
    end if;
    if not found then
      raise exception 'report_target_missing: that no longer exists, or you cannot see it' using errcode = 'no_data_found';
    end if;
    if owner_id = new.reporter then
      raise exception 'report_self: you cannot report yourself or your own content' using errcode = 'check_violation';
    end if;
    new.target_user := owner_id;
    new.snapshot := left(snap, 400);

    -- Duplicate and flood limits (per reporter).
    if exists (select 1 from public.reports r
               where r.reporter = new.reporter and r.target_type = new.target_type and r.target_id = new.target_id
                 and r.created_at > now() - interval '7 days') then
      raise exception 'report_duplicate: you already reported this' using errcode = 'unique_violation';
    end if;
    if (select count(*) from public.reports r
        where r.reporter = new.reporter and r.created_at > now() - interval '1 hour') >= 10 then
      raise exception 'report_rate_limit: too many reports, try again later' using errcode = 'check_violation';
    end if;
    return new;
  end if;

  -- UPDATE (moderators only, by RLS): what was filed stays as filed.
  new.reporter := old.reporter; new.target_type := old.target_type; new.target_id := old.target_id;
  new.target_user := old.target_user; new.reason := old.reason; new.note := old.note;
  new.snapshot := old.snapshot; new.created_at := old.created_at;
  if new.status is distinct from old.status or new.review_note is distinct from old.review_note then
    new.reviewed_by := coalesce(me, old.reviewed_by); new.reviewed_at := now();
    if me is not null then
      perform public.admin_log_write('report_' || new.status, coalesce(new.target_user, new.reporter),
                                     'report #' || new.id || ' (' || new.target_type || ' ' || new.target_id || ')'
                                     || coalesce(': ' || nullif(new.review_note, ''), ''));
    end if;
  else
    new.reviewed_by := old.reviewed_by; new.reviewed_at := old.reviewed_at;
  end if;
  return new;
end $$;
drop trigger if exists reports_guard on public.reports;
create trigger reports_guard before insert or update on public.reports
  for each row execute function public.reports_guard();

drop policy if exists "file reports"       on public.reports;
drop policy if exists "read own reports"   on public.reports;
drop policy if exists "mods read reports"  on public.reports;
drop policy if exists "mods review reports" on public.reports;
create policy "file reports" on public.reports for insert to authenticated
  with check (reporter = auth.uid() and status = 'open' and reviewed_by is null and review_note is null);
create policy "read own reports"  on public.reports for select to authenticated using (reporter = auth.uid());
create policy "mods read reports" on public.reports for select to authenticated using (public.is_global_mod(auth.uid()));
create policy "mods review reports" on public.reports for update to authenticated
  using (public.is_global_mod(auth.uid())) with check (public.is_global_mod(auth.uid()));

-- Column grants: a reporter can only supply what they are reporting and why.
revoke all on public.reports from anon, authenticated;
grant select on public.reports to authenticated;
grant insert (reporter, target_type, target_id, reason, note) on public.reports to authenticated;
grant update (status, review_note) on public.reports to authenticated;

-- Make the API see the new table right away.
notify pgrst, 'reload schema';
