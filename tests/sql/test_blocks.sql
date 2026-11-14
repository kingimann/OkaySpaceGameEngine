-- LOCAL test of docs/sql/blocks.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING. alice = the player with the rules,
-- bob = the person she blocks / restricts, carol = a friend of alice and a
-- bystander of the alice/bob block, dave = another stranger.
\set QUIET on
set client_min_messages = warning;
reset role;

insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-0000000000a1', 'alice@x'), ('00000000-0000-0000-0000-0000000000b2', 'bob@x'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol@x'), ('00000000-0000-0000-0000-0000000000d4', 'dave@x');
update public.profiles set username = split_part((select email from auth.users u where u.id = profiles.id), '@', 1);
-- alice and carol are friends.
insert into public.friendships (requester, addressee, status)
  values ('00000000-0000-0000-0000-0000000000a1', '00000000-0000-0000-0000-0000000000c3', 'accepted');
update public.profiles set last_seen = now(), playing = 'Space Game', session_code = 'ROOM1', bio = 'alice bio',
  status = 'busy', favorite_game = 'Chess', location = 'Mars'
  where username = 'alice';

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
create or replace function t.as_service() returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claims', json_build_object('role', 'service_role')::text, false);
end $$;
-- Run a statement: 'ok' or '<sqlstate>:<message>'.
create or replace function t.try(q text) returns text language plpgsql as $$
begin execute q; return 'ok'; exception when others then return sqlstate || ':' || sqlerrm; end $$;
-- First column of the first row as text ('<none>' if no rows, 'ERR:..' on error).
create or replace function t.val(q text) returns text language plpgsql as $$
declare v text;
begin execute 'select (' || q || ')::text' into v; return coalesce(v, '<null>');
exception when no_data_found then return '<none>'; when others then return 'ERR:' || sqlstate; end $$;
create or replace function t.cnt(q text) returns bigint language plpgsql as $$
declare v bigint;
begin execute 'select count(*) from (' || q || ') x' into v; return v; end $$;
grant usage on schema t to public; grant execute on all functions in schema t to public;

\set A '00000000-0000-0000-0000-0000000000a1'
\set B '00000000-0000-0000-0000-0000000000b2'
\set C '00000000-0000-0000-0000-0000000000c3'
\set D '00000000-0000-0000-0000-0000000000d4'

-- ======================= defaults: nothing is restricted =====================
select t.as_user(:'B'); set role authenticated;
select t.ok('default: stranger can DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'ok');
select t.ok('default: stranger can send a friend request', t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000a1')$$) = 'ok');
select t.ok('default: stranger sees presence', t.val($$select last_seen from public.profiles_visible where username = 'alice'$$) <> '<null>');
select t.ok('default: stranger sees bio', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = 'alice bio');
select t.ok('default: [Call] from a stranger is refused (calls default to friends)',
  t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] ROOM key')$$) like 'PT403:okay_rule:calls_friends');
reset role;
delete from public.friendships where requester = :'B'; delete from public.messages;

-- ======================= blocks table RLS ====================================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice blocks bob', t.try($$insert into public.blocks (blocked, blocked_name) values ('00000000-0000-0000-0000-0000000000b2', 'bob')$$) = 'ok');
select t.ok('blocker is filled from the JWT', t.val($$select blocker from public.blocks where blocked = '00000000-0000-0000-0000-0000000000b2'$$) = :'A');
select t.ok('blocking twice is a conflict, not a second row', t.try($$insert into public.blocks (blocked) values ('00000000-0000-0000-0000-0000000000b2')$$) like '23505:%');
select t.ok('on conflict do nothing is accepted (one-time migration is repeatable)',
  t.try($$insert into public.blocks (blocked) values ('00000000-0000-0000-0000-0000000000b2') on conflict do nothing$$) = 'ok');
select t.ok('cannot block yourself', t.try($$insert into public.blocks (blocked) values ('00000000-0000-0000-0000-0000000000a1')$$) like '23514:%');
select t.ok('cannot create a block in someone else''s name',
  t.try($$insert into public.blocks (blocker, blocked) values ('00000000-0000-0000-0000-0000000000c3', '00000000-0000-0000-0000-0000000000d4')$$) like '42501:%');
select t.ok('blocker reads own block', t.cnt($$select 1 from public.blocks$$) = 1);
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('blocked person can read the block they are part of', t.cnt($$select 1 from public.blocks$$) = 1);
select t.ok('blocked person cannot unblock', (select t.try($$delete from public.blocks$$) = 'ok') and t.cnt($$select 1 from public.blocks$$) = 1);
select t.ok('blocked person cannot edit the block', t.try($$update public.blocks set blocked_name = 'x'$$) like '42501:%' or t.val($$select blocked_name from public.blocks$$) = 'bob');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('bystander reads no blocks', t.cnt($$select 1 from public.blocks$$) = 0);
select t.ok('bystander delete is a no-op', t.try($$delete from public.blocks$$) = 'ok');
reset role;
select t.ok('block survived the bystander delete', (select count(*) from public.blocks) = 1);
select t.as_anon(); set role anon;
select t.ok('anon cannot read blocks', t.try($$select * from public.blocks$$) like '42501:%');
select t.ok('anon cannot create blocks', t.try($$insert into public.blocks (blocker, blocked) values ('00000000-0000-0000-0000-0000000000a1', '00000000-0000-0000-0000-0000000000d4')$$) like '42501:%');
reset role;

-- ======================= DMs across a block ==================================
select t.as_user(:'B'); set role authenticated;
select t.ok('blocked user cannot DM the blocker (generic "does not accept")',
  t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'let me in')$$) = 'PT403:okay_rule:dm_nobody');
select t.ok('blocked user cannot call the blocker',
  t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] R k')$$) = 'PT403:okay_rule:dm_nobody');
select t.ok('blocked user cannot send a friend request to the blocker',
  t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000a1')$$) = 'PT403:okay_rule:friend_requests_off');
select t.ok('blocked user can still DM a bystander', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000c3', 'hey carol')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('blocker cannot DM the person they blocked (must unblock)',
  t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000b2', 'hello')$$) = 'PT403:okay_rule:you_blocked');
select t.ok('blocker cannot friend-request the person they blocked',
  t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000b2')$$) = 'PT403:okay_rule:you_blocked');
select t.ok('blocker can still DM a bystander', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000c3', 'hi carol')$$) = 'ok');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('bystander can DM blocker', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'yo')$$) = 'ok');
select t.ok('bystander can DM the blocked person', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000b2', 'yo bob')$$) = 'ok');
reset role;
-- Defense in depth: with the trigger off, the RESTRICTIVE policy alone still refuses.
alter table public.messages disable trigger okay_messages_rules;
select t.as_user(:'B'); set role authenticated;
select t.ok('policy alone (trigger off) refuses a blocked DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'sneak')$$) like '42501:%');
reset role;
alter table public.messages enable trigger okay_messages_rules;
alter table public.friendships disable trigger okay_friendships_rules;
select t.as_user(:'B'); set role authenticated;
select t.ok('policy alone (trigger off) refuses a blocked friend request', t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000a1')$$) like '42501:%');
reset role;
alter table public.friendships enable trigger okay_friendships_rules;
select t.as_service();
set role service_role;
select t.ok('service role is not restricted', t.try($$insert into public.messages (sender, recipient, body) values ('00000000-0000-0000-0000-0000000000b2', '00000000-0000-0000-0000-0000000000a1', 'system')$$) = 'ok');
reset role;
-- Spoofing the sender is still refused (existing RLS).
select t.as_user(:'B'); set role authenticated;
select t.ok('cannot spoof the sender', t.try($$insert into public.messages (sender, recipient, body) values ('00000000-0000-0000-0000-0000000000c3', '00000000-0000-0000-0000-0000000000d4', 'x')$$) like '%:%' and
  t.try($$insert into public.messages (sender, recipient, body) values ('00000000-0000-0000-0000-0000000000c3', '00000000-0000-0000-0000-0000000000d4', 'x')$$) <> 'ok');
reset role;

-- ======================= presence / profile across a block ===================
select t.as_user(:'B'); set role authenticated;
select t.ok('blocked user: presence hidden', t.val($$select last_seen from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('blocked user: playing hidden', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('blocked user: session code hidden', t.val($$select session_code from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('blocked user: bio hidden', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('blocked user: status / favorite game / location hidden',
  t.val($$select coalesce(status, favorite_game, location) from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('blocked user still sees the public name (old chats keep rendering)', t.val($$select username from public.profiles_visible where username = 'alice'$$) = 'alice');
select t.ok('blocked user cannot read presence straight from the table', t.try($$select last_seen from public.profiles$$) like '42501:%');
select t.ok('blocked user cannot read the bio straight from the table', t.try($$select bio from public.profiles where username = 'alice'$$) like '42501:%');
select t.ok('public columns still readable from the table', t.val($$select username from public.profiles where username = 'alice'$$) = 'alice');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('bystander still sees alice presence', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = 'Space Game');
select t.ok('bystander still sees alice bio', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = 'alice bio');
select t.ok('bystander sees bob''s row (block does not hide bob from carol)', t.val($$select username from public.profiles_visible where username = 'bob'$$) = 'bob');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('owner sees own presence and bio', t.val($$select playing || '/' || bio from public.profiles_visible where username = 'alice'$$) = 'Space Game/alice bio');
select t.ok('owner can still update own presence', t.try($$update public.profiles set last_seen = now(), playing = 'Other' where id = auth.uid()$$) = 'ok');
select t.ok('EnsureProfile-style upsert still works under the column grants', t.try($$insert into public.profiles (username) values ('alice') on conflict (id) do update set username = excluded.username$$) = 'ok');
select t.ok('owner cannot update someone else''s profile', (select t.try($$update public.profiles set bio = 'pwn' where username = 'carol'$$)) = 'ok' and t.val($$select bio from public.profiles_visible where username = 'carol'$$) is distinct from 'pwn');
select t.ok('blocker does not see the blocked person''s presence (symmetric)', t.val($$select last_seen from public.profiles_visible where username = 'bob'$$) = '<null>');
reset role;
select t.as_anon(); set role anon;
select t.ok('anon with default visibility sees presence', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = 'Other');
reset role;

-- ======================= unblock restores everything =========================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice unblocks bob', t.try($$delete from public.blocks where blocked = '00000000-0000-0000-0000-0000000000b2'$$) = 'ok');
reset role;
select t.ok('block row is gone', (select count(*) from public.blocks) = 0);
select t.as_user(:'B'); set role authenticated;
select t.ok('after unblock: bob can DM alice', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'back')$$) = 'ok');
select t.ok('after unblock: bob sees presence again', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = 'Other');
reset role;

-- ======================= privacy_settings RLS ================================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice saves settings', t.try($$insert into public.privacy_settings (dm_from, profile_visibility, online_visibility, friend_requests, calls_from) values ('friends', 'everyone', 'everyone', 'everyone', 'friends')$$) = 'ok');
select t.ok('user_id is filled from the JWT', t.val($$select user_id from public.privacy_settings$$) = :'A');
select t.ok('invalid value rejected', t.try($$update public.privacy_settings set dm_from = 'anyone'$$) like '23514:%');
select t.ok('friend_requests cannot be friends-only value', t.try($$update public.privacy_settings set friend_requests = 'friends'$$) like '23514:%');
select t.ok('cannot save settings for someone else', t.try($$insert into public.privacy_settings (user_id, dm_from) values ('00000000-0000-0000-0000-0000000000b2', 'nobody')$$) like '42501:%');
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('others cannot read alice''s settings', t.cnt($$select 1 from public.privacy_settings$$) = 0);
select t.ok('others cannot change alice''s settings', (select t.try($$update public.privacy_settings set dm_from = 'everyone' where user_id = '00000000-0000-0000-0000-0000000000a1'$$)) = 'ok');
reset role;
select t.ok('alice''s setting unchanged by bob', (select dm_from from public.privacy_settings where user_id = :'A') = 'friends');
select t.as_anon(); set role anon;
select t.ok('anon cannot read settings', t.try($$select * from public.privacy_settings$$) like '42501:%');
reset role;

-- ======================= messages from: friends only =========================
select t.as_user(:'B'); set role authenticated;
select t.ok('friends-only: stranger refused with the friends reason', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'PT403:okay_rule:dm_friends');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('friends-only: friend allowed', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('friends-only is about incoming: alice can DM a stranger', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000b2', 'hi bob')$$) = 'ok');
update public.privacy_settings set dm_from = 'nobody';
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('nobody: even a friend is refused', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'PT403:okay_rule:dm_nobody');
select t.ok('nobody: a hang-up after a call is still delivered',
  t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[CallEnd] R 12')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('nobody: alice can still DM out', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000c3', 'hi carol')$$) = 'ok');
update public.privacy_settings set dm_from = 'everyone';
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('everyone: stranger allowed again', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'ok');
select t.ok('calls_from friends: stranger call refused', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] R k')$$) = 'PT403:okay_rule:calls_friends');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('calls_from friends: friend call allowed', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] R k')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set calls_from = 'nobody';
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('calls_from nobody: friend call refused', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] R k')$$) = 'PT403:okay_rule:calls_nobody');
select t.ok('calls_from nobody: plain DM still fine', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'text me')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set calls_from = 'everyone';
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('calls_from everyone: stranger call allowed', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[Call] R k')$$) = 'ok');
reset role;

-- ======================= friend requests =====================================
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set friend_requests = 'nobody';
reset role;
select t.as_user(:'D'); set role authenticated;
select t.ok('friend_requests nobody: refused', t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000a1')$$) = 'PT403:okay_rule:friend_requests_off');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('friend_requests nobody: alice can still send her own', t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000d4')$$) = 'ok');
update public.privacy_settings set friend_requests = 'everyone';
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('a request to someone else is unaffected', t.try($$insert into public.friendships (addressee) values ('00000000-0000-0000-0000-0000000000d4')$$) = 'ok');
reset role;

-- ======================= visibility settings (presence / profile) ============
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set online_visibility = 'friends';
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('online=friends: stranger sees no presence', t.val($$select last_seen from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('online=friends: profile fields still visible (separate setting)', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = 'alice bio');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('online=friends: friend sees presence', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = 'Other');
reset role;
select t.as_anon(); set role anon;
select t.ok('online=friends: anon sees no presence', t.val($$select playing from public.profiles_visible where username = 'alice'$$) = '<null>');
reset role;
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set online_visibility = 'nobody', profile_visibility = 'friends';
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('online=nobody: even a friend sees no presence', t.val($$select last_seen from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('profile=friends: friend sees bio', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = 'alice bio');
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('profile=friends: stranger sees no bio', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('profile=friends: stranger still sees the username', t.val($$select username from public.profiles_visible where username = 'alice'$$) = 'alice');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('owner always sees own presence and bio', t.val($$select playing || '/' || bio from public.profiles_visible where username = 'alice'$$) = 'Other/alice bio');
update public.privacy_settings set profile_visibility = 'nobody';
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('profile=nobody: even a friend sees no bio', t.val($$select bio from public.profiles_visible where username = 'alice'$$) = '<null>');
reset role;
select t.as_user(:'A'); set role authenticated;
update public.privacy_settings set online_visibility = 'everyone', profile_visibility = 'everyone';
reset role;

-- ======================= blocking removes the friendship =====================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice blocks her friend carol', t.try($$insert into public.blocks (blocked) values ('00000000-0000-0000-0000-0000000000c3')$$) = 'ok');
select t.ok('blocking ended the friendship', t.cnt($$select 1 from public.friendships where (requester = auth.uid() and addressee = '00000000-0000-0000-0000-0000000000c3') or (addressee = auth.uid() and requester = '00000000-0000-0000-0000-0000000000c3')$$) = 0);
reset role;
select t.ok('blocking carol did not touch other friendships', (select count(*) from public.friendships where requester = :'C' and addressee = :'D') = 1);
select t.as_user(:'C'); set role authenticated;
select t.ok('ex-friend cannot DM a blocker with friends-only DMs on either', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'x')$$) = 'PT403:okay_rule:dm_nobody');
reset role;

-- ======================= activity feed across a block ========================
reset role;
insert into public.activity (actor, actor_name, kind) values (:'A', 'alice', 'online');
insert into public.follows (follower, followee) values (:'B', :'A'), (:'C', :'A');
select t.as_user(:'B'); set role authenticated;
select t.ok('follower (not blocked) sees alice''s activity', t.cnt($$select 1 from public.activity where actor = '00000000-0000-0000-0000-0000000000a1'$$) = 1);
reset role;
insert into public.blocks (blocker, blocked) values (:'A', :'B');
select t.as_user(:'B'); set role authenticated;
select t.ok('blocked follower no longer sees alice''s activity', t.cnt($$select 1 from public.activity where actor = '00000000-0000-0000-0000-0000000000a1'$$) = 0);
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('blocked ex-friend (still a follower) is blocked too', t.cnt($$select 1 from public.activity where actor = '00000000-0000-0000-0000-0000000000a1'$$) = 0);
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('alice still sees her own activity', t.cnt($$select 1 from public.activity where actor = '00000000-0000-0000-0000-0000000000a1'$$) = 1);
reset role;

-- ======================= helper functions are not on the API schema ==========
select t.ok('helpers live outside public (not reachable as /rpc)',
  (select count(*) from pg_proc p join pg_namespace n on n.oid = p.pronamespace where n.nspname = 'public' and p.proname in ('is_blocked_between', 'dm_rule', 'friend_rule', 'can_see')) = 0);
select t.ok('re-running blocks.sql keeps a single view and policy set',
  (select count(*) from pg_policies where tablename = 'messages' and policyname = 'okay rules: dm insert') = 1);
