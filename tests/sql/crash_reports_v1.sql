-- FIXTURE, DO NOT EDIT: the first version of docs/sql/crash_reports.sql (100 KB reports, 300 rows an hour, no attempts column, no
-- advisory lock, no pruning), kept so tests/scripts/test_crash_reports_sql.sh can check that loading the current file on top of a table
-- made by it brings it up to date. Never loaded anywhere but a throwaway test database.
-- OkaySpace crash reports: the launcher sends each editor crash report, with the person's name, folders, email and addresses taken out
-- first (docs/crash_reports.md), as one row in this table; a scheduled GitHub workflow (.github/workflows/crash-issues.yml) turns new
-- rows into issues. Run once in your Supabase project's SQL editor. Needs nothing else (no other docs/sql file). Idempotent: re-running
-- is safe. Until it has run, the launcher keeps the reports on the computer and says the server is not set up (it never says "sent"
-- unless the server accepted the row).
--
-- Who can do what (enforced here, not in the launcher):
--   * anyone holding the public anon key (that is every copy of the launcher) can INSERT a row, and only the nine columns below. They
--     cannot choose the id, the time, or the "processed" fields; they cannot read, change or delete any row, their own included.
--     The launcher therefore asks for the answer without the row ("Prefer: return=minimal"): INSERT ... RETURNING would need SELECT.
--   * the service role (the key the GitHub workflow holds as a secret) reads rows, marks them processed with the issue number, and
--     deletes old ones. It bypasses row-level security, so no policy is written for it.
--   * the shape of a row is checked here, whatever the sender claims: ids and signatures are 16 hex digits, the report starts with the
--     report title and is at most 100 KB, every text column has a length limit, the same report id can be stored only once.
--   * a flood is capped: at most 300 rows in any hour for the whole table (a normal day is a handful). Beyond that an insert fails with
--     "crash_rate_limit"; the launcher keeps the report and tries again later.
-- Nothing in the table is personal on purpose: the launcher scrubs first, and crash_to_issue.py scrubs again before writing an issue.

create table if not exists public.crash_reports (
  id           uuid primary key default gen_random_uuid(),
  created_at   timestamptz not null default now(),
  report_id    text not null,            -- 16 hex: a hash of the report, so a retry never stores it twice
  signature    text not null,            -- 16 hex: the same crash (same exception, same top frames) has the same signature
  version      text not null,
  build        text,
  os           text,
  exception    text not null,
  last_step    text,
  top_frame    text,
  report       text not null,            -- the scrubbed report text
  processed_at timestamptz,              -- set by the workflow when the row has been turned into an issue / a comment
  issue_number integer,                  -- the issue it went to
  constraint crash_reports_report_id_key unique (report_id),
  constraint crash_reports_report_id_check check (report_id ~ '^[0-9a-f]{16}$'),
  constraint crash_reports_signature_check check (signature ~ '^[0-9a-f]{16}$'),
  constraint crash_reports_version_check   check (char_length(version) between 1 and 40),
  constraint crash_reports_build_check     check (build is null or char_length(build) <= 120),
  constraint crash_reports_os_check        check (os is null or char_length(os) <= 200),
  constraint crash_reports_exception_check check (char_length(exception) between 1 and 120),
  constraint crash_reports_last_step_check check (last_step is null or char_length(last_step) <= 200),
  constraint crash_reports_top_frame_check check (top_frame is null or char_length(top_frame) <= 300),
  constraint crash_reports_report_check    check (octet_length(report) between 20 and 102400 and report like 'OkaySpace crash report%')
);

create index if not exists crash_reports_created_idx on public.crash_reports (created_at);
create index if not exists crash_reports_signature_idx on public.crash_reports (signature);
create index if not exists crash_reports_unprocessed_idx on public.crash_reports (created_at) where processed_at is null;

alter table public.crash_reports enable row level security;

-- One policy, insert only, for the two roles a launcher can have. There is deliberately no select, update or delete policy.
drop policy if exists "send crash reports" on public.crash_reports;
create policy "send crash reports" on public.crash_reports for insert to anon, authenticated with check (true);

-- Column grants: the sender supplies the nine columns and nothing else.
revoke all on public.crash_reports from anon, authenticated;
grant insert (report_id, signature, version, build, os, exception, last_step, top_frame, report) on public.crash_reports to anon, authenticated;
grant select, update, delete on public.crash_reports to service_role;

-- The flood cap. Security definer so it can count rows the sender is not allowed to read.
create or replace function public.crash_reports_guard() returns trigger language plpgsql security definer set search_path = public as $$
begin
  if (select count(*) from public.crash_reports where created_at > now() - interval '1 hour') >= 300 then
    raise exception 'crash_rate_limit' using hint = 'too many crash reports in the last hour; try again later';
  end if;
  return new;
end $$;
revoke all on function public.crash_reports_guard() from public, anon, authenticated;
drop trigger if exists crash_reports_guard on public.crash_reports;
create trigger crash_reports_guard before insert on public.crash_reports for each row execute function public.crash_reports_guard();
