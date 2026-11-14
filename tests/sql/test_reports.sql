-- LOCAL test of docs/sql/reports.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING.
\set QUIET on
set client_min_messages = warning;

reset role;
-- admin, moderator, alice (reporter), bob (offender), carol (bystander)
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-00000000000a', 'admin@x'), ('00000000-0000-0000-0000-00000000000b', 'mod@x'),
  ('00000000-0000-0000-0000-0000000000a1', 'alice@x'), ('00000000-0000-0000-0000-0000000000b2', 'bob@x'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol@x');
-- (a trigger on auth.users creates the profiles; name them)
update public.profiles set username = split_part((select email from auth.users u where u.id = profiles.id), '@', 1);
insert into public.profile_badges (owner, badge) values
  ('00000000-0000-0000-0000-00000000000a', 'admin'), ('00000000-0000-0000-0000-00000000000b', 'moderator');
-- Content: bob -> alice DM, a group chat (bob + alice), bob's forum thread / post / listing / server (alice joined),
-- alice's own thread, and a DM between bob and carol that alice must not be able to report.
insert into public.messages (id, sender, recipient, body) overriding system value values
  (1, '00000000-0000-0000-0000-0000000000b2', '00000000-0000-0000-0000-0000000000a1', 'buy my coins'),
  (2, '00000000-0000-0000-0000-0000000000b2', '00000000-0000-0000-0000-0000000000c3', 'private to carol'),
  (3, '00000000-0000-0000-0000-0000000000a1', '00000000-0000-0000-0000-0000000000b2', 'alice own message');
insert into public.group_convos (id, title, owner) overriding system value values (1, 'g', '00000000-0000-0000-0000-0000000000b2');
insert into public.group_members (convo_id, member) values (1, '00000000-0000-0000-0000-0000000000b2'), (1, '00000000-0000-0000-0000-0000000000a1');
insert into public.group_messages (id, convo_id, sender, body) overriding system value values (1, 1, '00000000-0000-0000-0000-0000000000b2', 'group spam');
insert into public.forum_threads (id, author, author_name, title) overriding system value values
  (1, '00000000-0000-0000-0000-0000000000b2', 'bob', 'free robux'), (2, '00000000-0000-0000-0000-0000000000a1', 'alice', 'alice thread');
insert into public.forum_posts (id, thread_id, author, author_name, body) overriding system value values (1, 1, '00000000-0000-0000-0000-0000000000b2', 'bob', 'comment spam');
insert into public.workshop_items (id, author, author_name, title) overriding system value values (1, '00000000-0000-0000-0000-0000000000b2', 'bob', 'Totally safe tool');
insert into public.servers (id, name, owner) overriding system value values (1, 'bobs server', '00000000-0000-0000-0000-0000000000b2'), (2, 'hidden', '00000000-0000-0000-0000-0000000000c3');
insert into public.server_members (server_id, member) values (1, '00000000-0000-0000-0000-0000000000b2'), (1, '00000000-0000-0000-0000-0000000000a1'), (2, '00000000-0000-0000-0000-0000000000c3');

create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false);
end $$;
create or replace function t.as_anon() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'anon')::text, false);
end $$;
-- Runs a statement as the current role; returns 'ok' or the error text.
create or replace function t.try(q text) returns text language plpgsql as $$
begin execute q; return 'ok'; exception when others then return sqlerrm; end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;

-- ---- alice reports one of each target -------------------------------------
select t.as_user('00000000-0000-0000-0000-0000000000a1');
set role authenticated;
select t.ok('report a user', t.try($$insert into public.reports (target_type, target_id, reason) values ('user', '00000000-0000-0000-0000-0000000000b2', 'harassment')$$) = 'ok');
select t.ok('report a DM', t.try($$insert into public.reports (target_type, target_id, reason, note) values ('message', '1', 'scam', 'asks for money')$$) = 'ok');
select t.ok('report a group message', t.try($$insert into public.reports (target_type, target_id, reason) values ('group_message', '1', 'spam')$$) = 'ok');
select t.ok('report a forum post', t.try($$insert into public.reports (target_type, target_id, reason) values ('forum_thread', '1', 'scam')$$) = 'ok');
select t.ok('report a forum comment', t.try($$insert into public.reports (target_type, target_id, reason) values ('forum_post', '1', 'spam')$$) = 'ok');
select t.ok('report a marketplace item', t.try($$insert into public.reports (target_type, target_id, reason) values ('market_item', '1', 'illegal')$$) = 'ok');
select t.ok('report a server', t.try($$insert into public.reports (target_type, target_id, reason) values ('server', '1', 'hate')$$) = 'ok');
select t.ok('server fills in the owner and snapshot',
  (select target_user = '00000000-0000-0000-0000-0000000000b2' and snapshot = 'buy my coins' from public.reports where target_type = 'message'));
select t.ok('status starts open', (select bool_and(status = 'open') from public.reports));

-- ---- rules ------------------------------------------------------------------
select t.ok('cannot file as someone else',
  t.try($$insert into public.reports (reporter, target_type, target_id, reason) values ('00000000-0000-0000-0000-0000000000c3', 'user', '00000000-0000-0000-0000-0000000000b2', 'spam')$$) <> 'ok');
select t.ok('cannot report yourself',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('user', '00000000-0000-0000-0000-0000000000a1', 'spam')$$) like 'report_self%');
select t.ok('cannot report your own content',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('forum_thread', '2', 'spam')$$) like 'report_self%');
select t.ok('cannot report your own message',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('message', '3', 'spam')$$) like 'report_self%');
select t.ok('duplicate report blocked',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('user', '00000000-0000-0000-0000-0000000000b2', 'spam')$$) like 'report_duplicate%');
select t.ok('duplicate blocked even with another reason',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('server', '1', 'scam')$$) like 'report_duplicate%');
select t.ok('cannot report a DM you are not in',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('message', '2', 'spam')$$) like 'report_target_missing%');
select t.ok('cannot report a server you are not in',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('server', '2', 'spam')$$) like 'report_target_missing%');
select t.ok('unknown target refused',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('forum_post', '999', 'spam')$$) like 'report_target_missing%');
select t.ok('unknown reason refused',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('user', '00000000-0000-0000-0000-0000000000c3', 'dislike')$$) <> 'ok');
select t.ok('unknown target type refused',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('profile', '1', 'spam')$$) <> 'ok');
select t.ok('note longer than 500 refused',
  t.try($$insert into public.reports (target_type, target_id, reason, note) values ('user', '00000000-0000-0000-0000-0000000000c3', 'spam', repeat('x', 501))$$) <> 'ok');
select t.ok('cannot file a report as already resolved',
  t.try($$insert into public.reports (target_type, target_id, reason, status) values ('user', '00000000-0000-0000-0000-0000000000c3', 'spam', 'resolved')$$) <> 'ok');
select t.ok('cannot set the target owner yourself',
  t.try($$insert into public.reports (target_type, target_id, reason, target_user) values ('user', '00000000-0000-0000-0000-0000000000c3', 'spam', '00000000-0000-0000-0000-0000000000a1')$$) <> 'ok');
select t.ok('alice reads only her own', (select count(*) from public.reports) = 7);
select t.ok('reporter cannot update status', t.try($$update public.reports set status = 'dismissed'$$) is not null and (select count(*) from public.reports where status = 'open') = 7);
select t.ok('reporter cannot delete', t.try($$delete from public.reports$$) <> 'ok' and (select count(*) from public.reports) = 7);

-- ---- another player sees nothing, anon cannot touch the table -------------
select t.as_user('00000000-0000-0000-0000-0000000000c3');
select t.ok('carol cannot read others reports', (select count(*) from public.reports) = 0);
select t.ok('carol cannot update others reports', t.try($$update public.reports set status = 'resolved'$$) = 'ok' and (select count(*) from public.reports) = 0);
select t.as_user('00000000-0000-0000-0000-0000000000b2');
select t.ok('the reported player cannot read reports about them', (select count(*) from public.reports) = 0);
select t.as_anon();
set role anon;
select t.ok('signed-out cannot read', t.try($$select count(*) from public.reports$$) <> 'ok');
select t.ok('signed-out cannot report', t.try($$insert into public.reports (target_type, target_id, reason) values ('user', '00000000-0000-0000-0000-0000000000b2', 'spam')$$) <> 'ok');

-- ---- moderator --------------------------------------------------------------
reset role;
select t.as_user('00000000-0000-0000-0000-00000000000b');
set role authenticated;
select t.ok('moderator reads all reports', (select count(*) from public.reports) = 7);
select t.ok('moderator resolves a report', t.try($$update public.reports set status = 'resolved', review_note = 'removed' where target_type = 'user'$$) = 'ok');
select t.ok('status and reviewer stamped', (select status = 'resolved' and reviewed_by = '00000000-0000-0000-0000-00000000000b' and reviewed_at is not null from public.reports where target_type = 'user'));
select t.ok('moderator dismisses another', t.try($$update public.reports set status = 'dismissed' where target_type = 'server'$$) = 'ok');
select t.ok('moderator cannot rewrite what was filed',
  t.try($$update public.reports set reason = 'other' where target_type = 'user'$$) <> 'ok');
select t.ok('moderator cannot invalid status', t.try($$update public.reports set status = 'banana' where target_type = 'user'$$) <> 'ok');
select t.ok('moderator cannot delete', t.try($$delete from public.reports$$) <> 'ok' and (select count(*) from public.reports) = 7);
reset role;
select t.ok('review changes are in the audit log', (select count(*) from public.admin_log where action in ('report_resolved', 'report_dismissed')) = 2);
select t.ok('the filed text survived the review', (select reason = 'harassment' from public.reports where target_type = 'user'));
-- A moderator reporting something else is also subject to the rules.
select t.as_user('00000000-0000-0000-0000-00000000000a');
set role authenticated;
select t.ok('admin reads all too', (select count(*) from public.reports) = 7);
-- A plain player cannot become a moderator through the table.
reset role;
select t.as_user('00000000-0000-0000-0000-0000000000a1');
set role authenticated;
select t.ok('alice still sees her reports as open or reviewed',
  (select count(*) from public.reports where status <> 'open') = 2);

-- ---- hourly flood limit ------------------------------------------------------
reset role;
select t.as_user('00000000-0000-0000-0000-0000000000c3');
set role authenticated;
reset role;
insert into public.forum_threads (id, author, author_name, title) overriding system value
  select 100 + g, '00000000-0000-0000-0000-0000000000b2', 'bob', 'spam ' || g from generate_series(1, 11) g on conflict do nothing;
select t.as_user('00000000-0000-0000-0000-0000000000c3');
set role authenticated;
select t.ok('ten reports in an hour are fine',
  (select count(*) from generate_series(1, 10) g where t.try(format($$insert into public.reports (target_type, target_id, reason) values ('forum_thread', '%s', 'spam')$$, 100 + g)) = 'ok') = 10);
select t.ok('the eleventh is rate limited',
  t.try($$insert into public.reports (target_type, target_id, reason) values ('forum_thread', '111', 'spam')$$) like 'report_rate_limit%');
reset role;
