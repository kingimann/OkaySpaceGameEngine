-- LOCAL test of docs/sql/crash_reports.sql. Each check prints "PASS <name>" or "FAIL <name>: <why>" as a WARNING.
-- The launcher is "anon" (the public key every copy ships with); the GitHub workflow is "service_role".
\set QUIET on
set client_min_messages = warning;
reset role;

create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
-- Run a statement: 'ok' or '<sqlstate>:<message>'.
create or replace function t.try(q text) returns text language plpgsql as $$
begin execute q; return 'ok'; exception when others then return sqlstate || ':' || sqlerrm; end $$;
-- First column of the first row as text ('<none>' if no rows, 'ERR:..' on error).
create or replace function t.val(q text) returns text language plpgsql as $$
declare v text;
begin execute 'select (' || q || ')::text' into v; return coalesce(v, '<null>');
exception when no_data_found then return '<none>'; when others then return 'ERR:' || sqlstate; end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;

-- A valid row, with one field changed by the caller (a jsonb of overrides).
create or replace function t.ins(over jsonb default '{}') returns text language plpgsql as $$
declare r jsonb := jsonb_build_object(
    'report_id', substr(md5(random()::text), 1, 16), 'signature', 'abcdef0123456789', 'version', '2.874.0', 'build', 'pe-1-2',
    'os', 'Windows 10.0.19045 (x64)', 'exception', '0xC0000005', 'last_step', 'Render3D:gl', 'top_frame', 'OkayEngine.exe!Foo',
    'report', E'OkaySpace crash report\nVersion: 2.874.0\n') || over;
begin
  return t.try(format($f$insert into public.crash_reports (report_id, signature, version, build, os, exception, last_step, top_frame, report)
    values (%L, %L, %L, %L, %L, %L, %L, %L, %L)$f$, r->>'report_id', r->>'signature', r->>'version', r->>'build', r->>'os', r->>'exception',
    r->>'last_step', r->>'top_frame', r->>'report'));
end $$;
grant execute on function t.ins(jsonb) to public;

-- ---- the table and its policies exist the way the file says --------------------------------------------------------------------
select t.ok('table exists', to_regclass('public.crash_reports') is not null);
select t.ok('row level security is on', (select relrowsecurity from pg_class where oid = 'public.crash_reports'::regclass));
select t.ok('exactly one policy, insert only',
  (select count(*) from pg_policies where tablename = 'crash_reports') = 1 and
  (select cmd from pg_policies where tablename = 'crash_reports') = 'INSERT');
select t.ok('the policy is for anon and authenticated only',
  (select roles::text from pg_policies where tablename = 'crash_reports') = '{anon,authenticated}');
select t.ok('anon has no table-level select / update / delete',
  not has_table_privilege('anon', 'public.crash_reports', 'select') and not has_table_privilege('anon', 'public.crash_reports', 'update') and
  not has_table_privilege('anon', 'public.crash_reports', 'delete') and not has_table_privilege('authenticated', 'public.crash_reports', 'select'));
select t.ok('anon may insert exactly the nine columns',
  (select bool_and(has_column_privilege('anon', 'public.crash_reports', c, 'insert'))
     from unnest(array['report_id','signature','version','build','os','exception','last_step','top_frame','report']) c) and
  (select bool_and(not has_column_privilege('anon', 'public.crash_reports', c, 'insert'))
     from unnest(array['id','created_at','processed_at','issue_number']) c));
select t.ok('anon has no column select',
  (select bool_and(not has_column_privilege('anon', 'public.crash_reports', attname, 'select')) from pg_attribute where attrelid = 'public.crash_reports'::regclass and attnum > 0 and not attisdropped));

-- ---- the launcher (anon) sends ---------------------------------------------------------------------------------------------------
set role anon;
select t.ok('anon can insert a valid report', t.ins() = 'ok');
select t.ok('same report id twice is refused (23505)', t.try($$insert into public.crash_reports (report_id, signature, version, exception, report)
  values ('1111111111111111', 'abcdef0123456789', '1', 'x', 'OkaySpace crash report ........')$$) = 'ok'
  and t.try($$insert into public.crash_reports (report_id, signature, version, exception, report)
  values ('1111111111111111', 'abcdef0123456789', '1', 'x', 'OkaySpace crash report ........')$$) like '23505%');
select t.ok('minimal insert (only the required columns) works',
  t.try($$insert into public.crash_reports (report_id, signature, version, exception, report) values ('2222222222222222', '0123456789abcdef', '1.0', '0xC0000005', 'OkaySpace crash report cut')$$) = 'ok');

-- ---- ... and can do nothing else ----------------------------------------------------------------------------------------------------
select t.ok('anon cannot read rows (42501)', t.try($$select * from public.crash_reports$$) like '42501%');
select t.ok('anon cannot count rows', t.try($$select count(*) from public.crash_reports$$) like '42501%');
select t.ok('anon cannot read one column', t.try($$select report from public.crash_reports$$) like '42501%');
select t.ok('anon cannot update', t.try($$update public.crash_reports set version = 'x'$$) like '42501%');
select t.ok('anon cannot delete', t.try($$delete from public.crash_reports$$) like '42501%');
select t.ok('anon cannot truncate', t.try($$truncate public.crash_reports$$) like '42501%');
select t.ok('anon cannot read the row back with RETURNING', t.try($$insert into public.crash_reports (report_id, signature, version, exception, report)
  values ('3333333333333333', '0123456789abcdef', '1.0', 'x', 'OkaySpace crash report cut') returning id$$) like '42501%');
select t.ok('anon cannot set the id', t.try($$insert into public.crash_reports (id, report_id, signature, version, exception, report)
  values (gen_random_uuid(), '4444444444444444', '0123456789abcdef', '1.0', 'x', 'OkaySpace crash report cut')$$) like '42501%');
select t.ok('anon cannot set the time', t.try($$insert into public.crash_reports (created_at, report_id, signature, version, exception, report)
  values ('2020-01-01', '4444444444444444', '0123456789abcdef', '1.0', 'x', 'OkaySpace crash report cut')$$) like '42501%');
select t.ok('anon cannot mark a row processed', t.try($$insert into public.crash_reports (processed_at, report_id, signature, version, exception, report)
  values (now(), '4444444444444444', '0123456789abcdef', '1.0', 'x', 'OkaySpace crash report cut')$$) like '42501%');
select t.ok('anon cannot set an issue number', t.try($$insert into public.crash_reports (issue_number, report_id, signature, version, exception, report)
  values (1, '4444444444444444', '0123456789abcdef', '1.0', 'x', 'OkaySpace crash report cut')$$) like '42501%');

-- ---- the shape of a row is enforced here (23514 = check violation) ----------------------------------------------------------------
select t.ok('report id must be 16 lower-case hex', t.ins('{"report_id":"ABCDEF0123456789"}') like '23514%' and t.ins('{"report_id":"abc"}') like '23514%' and t.ins('{"report_id":"zzzzzzzzzzzzzzzz"}') like '23514%');
select t.ok('signature must be 16 hex', t.ins('{"signature":"nothex"}') like '23514%' and t.ins('{"signature":"abcdef01234567890"}') like '23514%');
select t.ok('version 1 to 40 characters', t.ins('{"version":""}') like '23514%' and t.ins(jsonb_build_object('version', repeat('v', 41))) like '23514%' and t.ins(jsonb_build_object('version', repeat('v', 40))) = 'ok');
select t.ok('column length limits', t.ins(jsonb_build_object('build', repeat('b', 121))) like '23514%' and t.ins(jsonb_build_object('os', repeat('o', 201))) like '23514%'
  and t.ins(jsonb_build_object('exception', repeat('e', 121))) like '23514%' and t.ins(jsonb_build_object('last_step', repeat('s', 201))) like '23514%'
  and t.ins(jsonb_build_object('top_frame', repeat('f', 301))) like '23514%' and t.ins('{"exception":""}') like '23514%');
select t.ok('the report must start with the report title', t.ins('{"report":"hello, this is not a crash report at all"}') like '23514%');
select t.ok('the report is at most 100 KB', t.ins(jsonb_build_object('report', 'OkaySpace crash report' || repeat('x', 102400 - 22 + 1))) like '23514%'
  and t.ins(jsonb_build_object('report', 'OkaySpace crash report' || repeat('x', 102400 - 22))) = 'ok');
select t.ok('multi-byte text counts in bytes, not characters', t.ins(jsonb_build_object('report', 'OkaySpace crash report' || repeat('é', 51200))) like '23514%');
select t.ok('the report cannot be tiny', t.ins('{"report":"OkaySpace"}') like '23514%');
select t.ok('null columns are refused where required', t.try($$insert into public.crash_reports (report_id, signature, version, exception, report) values (null, 'abcdef0123456789', '1', 'x', 'OkaySpace crash report cut')$$) like '23502%'
  and t.try($$insert into public.crash_reports (report_id, signature, exception, report) values ('5555555555555555', 'abcdef0123456789', 'x', 'OkaySpace crash report cut')$$) like '23502%');
select t.ok('hostile text is just text', t.ins(jsonb_build_object('last_step', E'x''); drop table public.crash_reports; --', 'report', E'OkaySpace crash report\n<script>alert(1)</script> @everyone [x](javascript:1) ``` \\0')) = 'ok'
  and to_regclass('public.crash_reports') is not null);

-- ---- authenticated (a signed-in launcher) is the same as anon -----------------------------------------------------------------------
reset role;
set role authenticated;
select t.ok('authenticated can insert', t.ins() = 'ok');
select t.ok('authenticated cannot read', t.try($$select * from public.crash_reports$$) like '42501%');
select t.ok('authenticated cannot update or delete', t.try($$update public.crash_reports set version = 'x'$$) like '42501%' and t.try($$delete from public.crash_reports$$) like '42501%');

-- ---- the workflow (service_role) reads, marks, prunes --------------------------------------------------------------------------------
reset role;
set role service_role;
select t.ok('service role reads every row', (select count(*) from public.crash_reports) >= 6);
select t.ok('rows start unprocessed with a time', (select bool_and(processed_at is null and issue_number is null and created_at is not null) from public.crash_reports));
select t.try($$update public.crash_reports set processed_at = now(), issue_number = 42 where report_id = '1111111111111111'$$) as upd \gset
select t.ok('service role marks rows processed', :'upd' = 'ok' and (select issue_number from public.crash_reports where report_id = '1111111111111111') = 42);
select t.ok('service role selects what is waiting', (select count(*) from public.crash_reports where processed_at is null) = (select count(*) from public.crash_reports) - 1);
select t.try($$delete from public.crash_reports where processed_at is not null$$) as del \gset
select t.ok('service role deletes old rows', :'del' = 'ok' and (select count(*) from public.crash_reports where report_id = '1111111111111111') = 0);

-- ---- the flood cap: 300 an hour ---------------------------------------------------------------------------------------------------------
reset role;
delete from public.crash_reports;
set session_replication_role = replica;   -- (superuser, local test only) seed 300 rows without running the cap on them
insert into public.crash_reports (report_id, signature, version, exception, report)
  select lpad(to_hex(g), 16, '0'), 'abcdef0123456789', '1', 'x', 'OkaySpace crash report seeded' from generate_series(1, 300) g;
set session_replication_role = origin;
set role anon;
select t.ok('the 301st report in an hour is refused with crash_rate_limit', t.ins() like '%crash_rate_limit%');
reset role;
update public.crash_reports set created_at = now() - interval '2 hours' where report_id = lpad(to_hex(1), 16, '0');
select t.ok('the cap is per hour: one row aged out, one more fits', (select count(*) from public.crash_reports where created_at > now() - interval '1 hour') = 299);
set role anon;
select t.ok('... and now it is accepted', t.ins() = 'ok');
select t.ok('... then full again', t.ins() like '%crash_rate_limit%');
reset role;
