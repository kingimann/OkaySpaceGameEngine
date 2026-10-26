-- LOCAL test of docs/sql/anticheat.sql (review items #270.1-4). Each check prints
-- "PASS <name>" or "FAIL <name>: <why>" as a WARNING.
\set QUIET on
set client_min_messages = warning;

-- Users: admin, two players (cheater, and the cheater's new account), one bystander.
reset role;
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-00000000000a', 'admin@x'),
  ('00000000-0000-0000-0000-0000000000c1', 'cheater@x'),
  ('00000000-0000-0000-0000-0000000000c2', 'newacct@x'),
  ('00000000-0000-0000-0000-0000000000b1', 'bystander@x');
insert into public.profile_badges (owner, badge) values ('00000000-0000-0000-0000-00000000000a', 'admin');

create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false);
end $$;
create or replace function t.as_service() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'service_role')::text, false);
end $$;
create or replace function t.as_anon() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'anon')::text, false);
end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;

-- Admin bans the cheater: account + machine, and a separate account-only ban; a report and an appeal.
select t.as_user('00000000-0000-0000-0000-00000000000a');
set role authenticated;
insert into public.ac_bans (user_id, hw_hash, reason) values
  ('00000000-0000-0000-0000-0000000000c1', repeat('ab', 32), 'speedhack (machine)');
insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000c1', 'speedhack (account)');
insert into public.ac_bans (user_id, reason) values ('00000000-0000-0000-0000-0000000000b1', 'to edit');
reset role;
select t.as_user('00000000-0000-0000-0000-0000000000b1');
set role authenticated;
insert into public.ac_reports (reported, reason) values ('00000000-0000-0000-0000-0000000000c1', 'flying');
select public.ac_file_appeal((select id from public.ac_my_bans() limit 1), 'please');
reset role;

-- ---- #270.2 ac_is_banned: admins and service_role only ----------------------------
do $$
declare v boolean; denied boolean := false;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000b1');
  set local role authenticated;
  begin
    v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  exception when insufficient_privilege then denied := true;
  end;
  perform t.ok('ac_is_banned refused to a plain player', denied, 'a player could look up anyone''s ban status');
end $$;
do $$
declare v boolean; denied boolean := false;
begin
  perform t.as_anon();
  set local role anon;
  begin v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  exception when insufficient_privilege then denied := true; end;
  perform t.ok('ac_is_banned refused to anon', denied);
end $$;
do $$
declare v boolean;
begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  v := public.ac_is_banned('00000000-0000-0000-0000-0000000000c1', null);
  perform t.ok('ac_is_banned works for an admin', v);
exception when others then perform t.ok('ac_is_banned works for an admin', false, sqlerrm);
end $$;
do $$
declare v boolean;
begin
  perform t.as_service();
  set local role service_role;
  v := public.ac_is_banned(null, repeat('ab', 32));
  perform t.ok('ac_is_banned works for service_role', v);
exception when others then perform t.ok('ac_is_banned works for service_role', false, sqlerrm);
end $$;
do $$
declare n int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000b1');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans();
  perform t.ok('players still see their own bans (ac_my_bans)', n = 1, n::text);
end $$;

-- ---- #270.1 an empty hash never matches a machine ban ------------------------------
do $$
declare v boolean; n int; refused boolean := false;
begin
  perform t.as_service();
  v := public.ac_is_banned(null, '');
  perform t.ok('ac_is_banned(null, '''') is false', not v);
  perform t.as_user('00000000-0000-0000-0000-0000000000c2');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans('');
  perform t.ok('ac_my_bans('''') finds no machine ban', n = 0, n::text);
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  begin insert into public.ac_bans (hw_hash, reason) values ('', 'empty');
  exception when check_violation then refused := true; end;
  perform t.ok('a ban on an empty hash is refused', refused);
end $$;

-- ---- #270.3 every admin action is logged -------------------------------------------
do $$
declare before_n int; b bigint; r bigint; a bigint;
begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select id into b from public.ac_bans where reason = 'to edit';
  reset role;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set until = now() + interval '3 days' where id = b;
  reset role;
  perform t.ok('changing a ban''s until is logged', (select count(*) from public.admin_log) = before_n + 1
                     and exists (select 1 from public.admin_log where detail like '%ban #' || b || '%' and action like 'ac_ban%' and detail like '%until%'),
                     (select string_agg(action || ':' || coalesce(detail, ''), ' | ') from public.admin_log));
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set reason = 'edited reason' where id = b;
  reset role;
  perform t.ok('changing a ban''s reason is logged', (select count(*) from public.admin_log) = before_n + 1);
  set local role authenticated;
  update public.ac_bans set lifted = true, lift_reason = 'first' where id = b;
  reset role;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set lift_reason = 'second thoughts' where id = b;
  reset role;
  perform t.ok('changing a lifted ban''s lift_reason is logged', (select count(*) from public.admin_log) = before_n + 1);
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  update public.ac_bans set reason = reason where id = b;   -- no change: nothing to log
  reset role;
  perform t.ok('a no-op update logs nothing', (select count(*) from public.admin_log) = before_n);

  select id into r from public.ac_reports limit 1;
  select count(*) into before_n from public.admin_log;
  set local role authenticated;
  delete from public.ac_reports where id = r;
  reset role;
  perform t.ok('an admin deleting a report is logged',
                     exists (select 1 from public.admin_log where action = 'ac_report_delete' and detail like '%#' || r || '%'));

  select id into a from public.ac_appeals limit 1;
  set local role authenticated;
  delete from public.ac_appeals where id = a;
  reset role;
  perform t.ok('an admin deleting an appeal is logged',
                     exists (select 1 from public.admin_log where action = 'ac_appeal_delete' and detail like '%#' || a || '%'));

  set local role authenticated;
  delete from public.ac_bans where id = b;
  reset role;
  perform t.ok('an admin deleting a ban is logged',
                     exists (select 1 from public.admin_log where action = 'ac_ban_delete' and detail like '%#' || b || '%'));
end $$;
-- Only admins can delete at all (RLS): a player's delete touches nothing.
do $$
declare n int;
begin
  perform t.as_user('00000000-0000-0000-0000-0000000000c1');
  set local role authenticated;
  delete from public.ac_bans;
  reset role;
  select count(*) into n from public.ac_bans where user_id = '00000000-0000-0000-0000-0000000000c1';
  perform t.ok('a player can''t delete bans', n = 2, n::text);
end $$;

-- ---- #270.4 machine bans survive account deletion ----------------------------------
reset role;
select set_config('request.jwt.claims', '', false);
do $$
declare n_machine int; n_account int; ok boolean;
begin
  delete from auth.users where id = '00000000-0000-0000-0000-0000000000c1';
  select count(*) into n_machine from public.ac_bans where hw_hash = repeat('ab', 32);
  select count(*) into n_account from public.ac_bans where reason = 'speedhack (account)';
  perform t.ok('deleting the account keeps its machine ban', n_machine = 1, n_machine::text);
  perform t.ok('...with user_id set to null',
                     (select user_id is null from public.ac_bans where hw_hash = repeat('ab', 32)));
  perform t.ok('...and drops its account-only ban', n_account = 0, n_account::text);
exception when others then perform t.ok('deleting a banned account', false, sqlerrm);
end $$;
do $$
declare n int; v boolean; ap bigint;
begin
  -- The cheater's new account on the same machine still sees (and can appeal) the machine ban.
  perform t.as_user('00000000-0000-0000-0000-0000000000c2');
  set local role authenticated;
  select count(*) into n from public.ac_my_bans(repeat('ab', 32));
  perform t.ok('the machine ban (null user_id) shows in ac_my_bans by hash', n = 1, n::text);
  select count(*) into n from public.ac_bans;
  perform t.ok('RLS: a null-user ban isn''t readable by other players', n = 0, n::text);
  ap := public.ac_file_appeal((select id from public.ac_my_bans(repeat('ab', 32)) limit 1), 'new me', repeat('ab', 32));
  perform t.ok('the machine ban can be appealed from the new account', ap is not null);
  reset role;
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  v := public.ac_is_banned(null, repeat('ab', 32));
  perform t.ok('the machine is still banned', v);
  select count(*) into n from public.ac_bans where user_id is null;
  perform t.ok('admins see the null-user ban', n = 1, n::text);
  update public.ac_appeals set status = 'accepted', decision_note = 'ok' where id = ap;
  reset role;
  perform t.ok('accepting its appeal lifts the null-user ban',
                     (select lifted from public.ac_bans where hw_hash = repeat('ab', 32)));
exception when others then perform t.ok('null-user machine ban', false, sqlerrm);
end $$;
-- A purge still works with null users and isn't blocked by the delete logging.
do $$
declare r record;
begin
  select * into r from public.ac_purge_old();
  perform t.ok('ac_purge_old runs', true);
exception when others then perform t.ok('ac_purge_old runs', false, sqlerrm);
end $$;
-- An admin's purge: one summary line, no line per purged row.
reset role;
select set_config('request.jwt.claims', '', false);
insert into public.ac_reports (reporter, reported, reason, created_at)
values ('00000000-0000-0000-0000-0000000000b1', '00000000-0000-0000-0000-0000000000c2', 'old one', now() - interval '200 days');
do $$
declare r record; before_del int;
begin
  select count(*) into before_del from public.admin_log where action = 'ac_report_delete';
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  select * into r from public.ac_purge_old();
  reset role;
  perform t.ok('an admin purge removes the old report', r.reports = 1, r.reports::text);
  perform t.ok('...logs one summary line', exists (select 1 from public.admin_log where action = 'ac_purge'));
  perform t.ok('...and no per-row delete lines',
               (select count(*) from public.admin_log where action = 'ac_report_delete') = before_del);
  perform t.ok('...and the purge flag is cleared', coalesce(current_setting('okay.ac_purge', true), '') <> 'on');
end $$;
