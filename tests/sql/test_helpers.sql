-- Shared helpers for the LOCAL SQL check files (tests/sql/test_*.sql), loaded
-- with \ir test_helpers.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING; tests/scripts/sql_test_lib.sh counts them.
--   t.ok(name, cond, why)  report one check (a null cond is a FAIL)
--   t.as_user(uid) / t.as_anon() / t.as_service()
--                          act as that caller (then set role authenticated / anon)
--   t.try(sql)             'ok' or '<sqlstate>:<message>'
--   t.val(sql)             first column of the first row as text, '<null>', or 'ERR:..'
--   t.cnt(sql)             number of rows the query returns
create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if coalesce(cond, false) then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false);
end $$;
create or replace function t.as_anon() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'anon')::text, false);
end $$;
create or replace function t.as_service() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'service_role')::text, false);
end $$;
-- Run a statement: 'ok' or '<sqlstate>:<message>'.
create or replace function t.try(q text) returns text language plpgsql as $$
begin execute q; return 'ok'; exception when others then return sqlstate || ':' || sqlerrm; end $$;
-- First column of the first row as text ('<null>' if no rows or null, 'ERR:..' on error).
create or replace function t.val(q text) returns text language plpgsql as $$
declare v text;
begin execute 'select (' || q || ')::text' into v; return coalesce(v, '<null>');
exception when others then return 'ERR:' || sqlstate || ':' || sqlerrm; end $$;
create or replace function t.cnt(q text) returns bigint language plpgsql as $$
declare v bigint;
begin execute 'select count(*) from (' || q || ') x' into v; return v; end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;
