-- LOCAL test of docs/sql/chat_receipts.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING. Users: a, b are DM partners, c is a bystander.
\set QUIET on
set client_min_messages = warning;
reset role;
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-00000000000a', 'a@x'),
  ('00000000-0000-0000-0000-00000000000b', 'b@x'),
  ('00000000-0000-0000-0000-00000000000c', 'c@x');
insert into public.messages (sender, recipient, body) values
  ('00000000-0000-0000-0000-00000000000a', '00000000-0000-0000-0000-00000000000b', 'a to b'),
  ('00000000-0000-0000-0000-00000000000b', '00000000-0000-0000-0000-00000000000a', 'b to a'),
  ('00000000-0000-0000-0000-00000000000c', '00000000-0000-0000-0000-00000000000c', 'c note to self');

create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false);
end $$;
grant usage on schema t to public;
-- message ids looked up as the owner: a bystander cannot see them through RLS.
create table t.ids as select
  (select id from public.messages where body = 'a to b') as a_to_b,
  (select id from public.messages where body = 'b to a') as b_to_a,
  (select id from public.messages where body = 'c note to self') as c_note;
grant select on t.ids to public;
grant execute on all functions in schema t to public;

-- ---- typing_state -----------------------------------------------------------
select t.as_user('00000000-0000-0000-0000-00000000000a');
set role authenticated;
-- the client sends only peer + at; sender defaults to auth.uid()
insert into public.typing_state (peer, at) values ('00000000-0000-0000-0000-00000000000b', now())
  on conflict (sender) do update set peer = excluded.peer, at = excluded.at;
insert into public.typing_state (peer, at) values ('00000000-0000-0000-0000-00000000000b', now())
  on conflict (sender) do update set peer = excluded.peer, at = excluded.at;   -- upsert twice: still one row
reset role;
do $$ begin
  perform t.ok('typing upsert keeps one row per sender', (select count(*) from public.typing_state) = 1, 'rows duplicated');
end $$;

do $$ declare denied boolean := false; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a');
  set local role authenticated;
  begin
    insert into public.typing_state (sender, peer) values ('00000000-0000-0000-0000-00000000000c', '00000000-0000-0000-0000-00000000000b');
  exception when others then denied := (sqlstate = '42501'); end;
  reset role;
  perform t.ok('cannot write typing as someone else', denied, 'forged sender accepted');
end $$;

do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000b'); set local role authenticated;
  select count(*) into n from public.typing_state where sender = '00000000-0000-0000-0000-00000000000a';
  reset role;
  perform t.ok('recipient sees who is typing to them', n = 1, 'n=' || n);
end $$;
do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000c'); set local role authenticated;
  select count(*) into n from public.typing_state;
  reset role;
  perform t.ok('bystander sees no typing rows', n = 0, 'n=' || n);
end $$;
do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000b'); set local role authenticated;
  update public.typing_state set peer = '00000000-0000-0000-0000-00000000000c' where sender = '00000000-0000-0000-0000-00000000000a';
  get diagnostics n = row_count;
  delete from public.typing_state where sender = '00000000-0000-0000-0000-00000000000a';
  declare d int; begin get diagnostics d = row_count; n := n + d; end;
  reset role;
  perform t.ok('recipient cannot edit or delete the sender''s row', n = 0, 'changed ' || n);
end $$;
-- ---- message_reads ----------------------------------------------------------
select t.as_user('00000000-0000-0000-0000-00000000000b');
set role authenticated;
insert into public.message_reads (peer, last_read_id, updated_at)
  select '00000000-0000-0000-0000-00000000000a', min(id), now() from public.messages where body = 'a to b'
  on conflict (reader, peer) do update set last_read_id = excluded.last_read_id, updated_at = excluded.updated_at;
insert into public.message_reads (peer, last_read_id, updated_at)
  select '00000000-0000-0000-0000-00000000000a', min(id), now() from public.messages where body = 'b to a'
  on conflict (reader, peer) do update set last_read_id = excluded.last_read_id, updated_at = excluded.updated_at;
reset role;
do $$ begin
  perform t.ok('read mark upserts to the newest id (one row per pair)',
    (select count(*) from public.message_reads) = 1
    and (select last_read_id from public.message_reads) = (select id from public.messages where body = 'b to a'), 'wrong row state');
end $$;

do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a'); set local role authenticated;
  select count(*) into n from public.message_reads where reader = '00000000-0000-0000-0000-00000000000b';
  reset role;
  perform t.ok('the other party can read my read mark', n = 1, 'n=' || n);
end $$;
do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000c'); set local role authenticated;
  select count(*) into n from public.message_reads;
  reset role;
  perform t.ok('bystander sees no read marks', n = 0, 'n=' || n);
end $$;
do $$ declare denied boolean := false; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a'); set local role authenticated;
  begin
    insert into public.message_reads (reader, peer, last_read_id)
      values ('00000000-0000-0000-0000-00000000000b', '00000000-0000-0000-0000-00000000000a', 1);
  exception when others then denied := (sqlstate = '42501'); end;
  reset role;
  perform t.ok('cannot write a read mark as someone else', denied, 'forged reader accepted');
end $$;
do $$ declare denied boolean := false; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000c'); set local role authenticated;
  begin
    insert into public.message_reads (peer, last_read_id)
      values ('00000000-0000-0000-0000-00000000000a', (select a_to_b from t.ids));
  exception when others then denied := (sqlstate = '42501'); end;
  reset role;
  perform t.ok('cannot mark a message from a conversation I am not in', denied, 'claimed foreign message');
end $$;
do $$ declare denied boolean := false; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000b'); set local role authenticated;
  begin
    update public.message_reads set last_read_id = (select c_note from t.ids);
  exception when others then denied := (sqlstate = '42501'); end;
  reset role;
  perform t.ok('cannot move my mark onto a foreign message', denied, 'update accepted');
end $$;
do $$ declare n int; begin
  perform t.as_user('00000000-0000-0000-0000-00000000000a'); set local role authenticated;
  delete from public.message_reads;
  get diagnostics n = row_count;
  reset role;
  perform t.ok('the other party cannot delete my read mark', n = 0, 'deleted ' || n);
end $$;
do $$ begin
  perform t.ok('re-running chat_receipts.sql left both tables', to_regclass('public.typing_state') is not null and to_regclass('public.message_reads') is not null);
end $$;
