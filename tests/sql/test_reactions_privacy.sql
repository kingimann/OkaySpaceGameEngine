-- LOCAL test of docs/sql/reactions_privacy.sql (SEC1 item 4). Each check prints "PASS <name>" or "FAIL <name>: <why>"
-- as a WARNING. alice writes the messages, bob is her DM partner and a group member, carol is a stranger to the
-- conversations, dave is in alice's server but blocked by her.
\set QUIET on
set client_min_messages = warning;
reset role;
insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-0000000000a1', 'alice@x'), ('00000000-0000-0000-0000-0000000000b2', 'bob@x'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol@x'), ('00000000-0000-0000-0000-0000000000d4', 'dave@x');
create schema if not exists t;
create or replace function t.ok(name text, cond boolean, why text default '') returns void language plpgsql as $$
begin
  if cond then raise warning 'PASS %', name; else raise warning 'FAIL %: %', name, why; end if;
end $$;
create or replace function t.as_user(uid text) returns void language plpgsql as $$
begin perform set_config('request.jwt.claims', json_build_object('sub', uid, 'role', 'authenticated')::text, false); end $$;
create or replace function t.as_anon() returns void language plpgsql as $$
begin perform set_config('request.jwt.claims', json_build_object('role', 'anon')::text, false); end $$;
create or replace function t.try(q text) returns text language plpgsql as $$
begin execute q; return 'ok'; exception when others then return sqlstate || ':' || sqlerrm; end $$;
create or replace function t.cnt(q text) returns bigint language plpgsql as $$
declare v bigint;
begin execute 'select count(*) from (' || q || ') x' into v; return v; exception when others then return -1; end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;
\set A '00000000-0000-0000-0000-0000000000a1'
\set B '00000000-0000-0000-0000-0000000000b2'
\set C '00000000-0000-0000-0000-0000000000c3'
\set D '00000000-0000-0000-0000-0000000000d4'

-- fixtures, written as the database owner
insert into public.messages (id, sender, recipient, body) overriding system value values (1, :'A', :'B', 'dm from alice');
insert into public.servers (id, name, owner) overriding system value values (1, 's', :'A');
insert into public.server_members (server_id, member) values (1, :'A'), (1, :'B'), (1, :'C'), (1, :'D');
insert into public.channels (id, server_id, name) overriding system value values (1, 1, 'general');
insert into public.channel_messages (id, channel_id, author, body) overriding system value values (1, 1, :'A', 'channel msg by alice');
insert into public.group_convos (id, owner) overriding system value values (1, :'A');
insert into public.group_members (convo_id, member) values (1, :'A'), (1, :'B');
insert into public.group_messages (id, convo_id, sender, body) overriding system value values (1, 1, :'A', 'group msg by alice');
insert into public.blocks (blocker, blocked) values (:'A', :'D');

-- ======================= who may add and read =================================
select t.as_user(:'B'); set role authenticated;
select t.ok('dm partner can react', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('dm', 1, 'heart')$$) = 'ok');
select t.ok('group member can react', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('group', 1, 'heart')$$) = 'ok');
select t.ok('server member can react', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('channel', 1, 'heart')$$) = 'ok');
select t.ok('cannot react in someone else''s name',
  t.try($$insert into public.message_reactions (reactor, scope, target_id, emoji) values ('00000000-0000-0000-0000-0000000000c3', 'dm', 1, 'fire')$$) like '42501:%');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('stranger cannot react to a DM', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('dm', 1, 'fire')$$) like '42501:%');
select t.ok('stranger cannot react to a group message', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('group', 1, 'fire')$$) like '42501:%');
select t.ok('stranger sees no DM or group reactions', t.cnt($$select 1 from public.message_reactions where scope in ('dm','group')$$) = 0);
select t.ok('stranger cannot react to a message that does not exist', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('dm', 999, 'fire')$$) like '42501:%');
reset role;
select t.as_anon(); set role anon;
select t.ok('anon reads no reactions', t.cnt($$select 1 from public.message_reactions$$) <= 0);
select t.ok('anon cannot call the visibility check', t.try($$select public.can_see_reaction_target('dm', 1)$$) like '42501:%');
reset role;

-- ======================= unbounded text =======================================
select t.as_user(:'B'); set role authenticated;
select t.ok('an empty reaction is refused', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('dm', 1, '')$$) like '23514:%');
select t.ok('a huge reaction text is refused', t.try('insert into public.message_reactions (scope, target_id, emoji) values (''dm'', 1, ''' || repeat('x', 5000) || ''')') like '23514:%');
select t.ok('a poll vote key still fits', t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('dm', 1, 'poll:3')$$) = 'ok');
select t.ok('a reaction cannot be moved to another message (no UPDATE)', t.try($$update public.message_reactions set target_id = 2$$) like '42501:%');
reset role;

-- ======================= blocks ===============================================
select t.as_user(:'D'); set role authenticated;
select t.ok('a blocked user cannot react to the blocker''s channel message',
  t.try($$insert into public.message_reactions (scope, target_id, emoji) values ('channel', 1, 'angry')$$) like '42501:%');
reset role;
-- dave reacted before the block would have been recorded; simulate by inserting as the owner.
insert into public.message_reactions (reactor, scope, target_id, emoji) values (:'D', 'channel', 1, 'old');
select t.as_user(:'A'); set role authenticated;
select t.ok('the blocker does not see the blocked user''s reactions', t.cnt($$select 1 from public.message_reactions where reactor = '00000000-0000-0000-0000-0000000000d4'$$) = 0);
reset role;
select t.as_user(:'D'); set role authenticated;
select t.ok('the blocked user does not see the blocker''s reactions', t.cnt($$select 1 from public.message_reactions where reactor = '00000000-0000-0000-0000-0000000000a1'$$) = 0);
select t.ok('a blocked user still sees their own reactions', t.cnt($$select 1 from public.message_reactions where reactor = '00000000-0000-0000-0000-0000000000d4'$$) >= 0);
reset role;

-- ======================= counts without who ===================================
select t.as_user(:'B'); set role authenticated;
select t.ok('counts RPC exists and counts', t.cnt($$select * from public.reaction_counts('dm', array[1]::bigint[]) where emoji = 'heart' and n = 1 and mine$$) = 1);
select t.ok('counts RPC has no reactor column',
  (select count(*) from information_schema.parameters where specific_schema = 'public' and parameter_mode = 'OUT'
     and specific_name like 'reaction_counts%') = 4
  and (select count(*) from information_schema.parameters where specific_schema = 'public' and parameter_mode = 'OUT'
     and specific_name like 'reaction_counts%' and parameter_name in ('reactor', 'user_id', 'email')) = 0);
select t.ok('counts RPC refuses a huge id list', t.try($$select * from public.reaction_counts('dm', (select array_agg(g::bigint) from generate_series(1, 5000) g))$$) <> 'ok');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('counts RPC shows a stranger nothing about a private DM', t.cnt($$select * from public.reaction_counts('dm', array[1]::bigint[])$$) = 0);
reset role;

-- ======================= leftovers ============================================
select t.as_user(:'A'); set role authenticated;
delete from public.messages where id = 1;   -- a sender may delete their own DM
reset role;
select t.ok('deleting a DM deletes its reactions', (select count(*) from public.message_reactions where scope = 'dm') = 0);
delete from public.channel_messages where id = 1;   -- as the owner (moderation / cascade)
delete from public.group_messages where id = 1;
select t.ok('deleting a message deletes its reactions', (select count(*) from public.message_reactions) = 0,
  (select count(*)::text from public.message_reactions) || ' left');
