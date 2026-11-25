-- LOCAL test of docs/sql/chat_invites.sql (and the friends-only join link in
-- docs/sql/blocks.sql). Each check prints "PASS <name>" or "FAIL <name>: <why>"
-- as a WARNING. alice and carol are friends; bob is a stranger to alice; dave
-- is alice's friend until he blocks her.
\set QUIET on
set client_min_messages = warning;
reset role;

insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-0000000000a1', 'alice@x'), ('00000000-0000-0000-0000-0000000000b2', 'bob@x'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol@x'), ('00000000-0000-0000-0000-0000000000d4', 'dave@x');
update public.profiles set username = split_part((select email from auth.users u where u.id = profiles.id), '@', 1);
insert into public.friendships (requester, addressee, status) values
  ('00000000-0000-0000-0000-0000000000a1', '00000000-0000-0000-0000-0000000000c3', 'accepted'),
  ('00000000-0000-0000-0000-0000000000d4', '00000000-0000-0000-0000-0000000000a1', 'accepted');
update public.profiles set last_seen = now(), playing = 'Space Game',
  session_code = 'okay://join?game=Space&relay=r:1&code=K7Q' where username = 'alice';

\ir test_helpers.sql

\set A '00000000-0000-0000-0000-0000000000a1'
\set B '00000000-0000-0000-0000-0000000000b2'
\set C '00000000-0000-0000-0000-0000000000c3'
\set D '00000000-0000-0000-0000-0000000000d4'

-- ======================= presence: the join link is for friends only ========
select t.as_user(:'B'); set role authenticated;
select t.ok('stranger still sees that alice is online', t.val($$select last_seen from public.profiles_visible where username = 'alice'$$) <> '<null>');
select t.ok('stranger does NOT see alice''s join link', t.val($$select session_code from public.profiles_visible where username = 'alice'$$) = '<null>');
select t.ok('stranger cannot read session_code from the table', t.try($$select session_code from public.profiles$$) like '42501:%');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('friend sees alice''s join link', t.val($$select session_code from public.profiles_visible where username = 'alice'$$) like 'okay://join?%');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('alice sees her own join link', t.val($$select session_code from public.profiles_visible where username = 'alice'$$) like 'okay://join?%');
select t.ok('alice''s update of carol''s presence changes nothing', t.try($$update public.profiles set playing = 'fake' where username = 'carol'$$) = 'ok');
reset role;
select t.ok('carol''s presence was not forged', (select playing from public.profiles where username = 'carol') is null);

-- ======================= game_invites: insert rules ==========================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice invites carol (friend)', t.try($$insert into public.game_invites (recipient, kind, title, link, game_version, target)
  values ('00000000-0000-0000-0000-0000000000c3', 'game', 'Space', 'okay://join?game=Space&relay=r:1&code=K7Q', '1.4.2', 'exported')$$) = 'ok');
select t.ok('sender is filled from the JWT', t.val($$select sender from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000c3'$$) = :'A');
select t.ok('a new invite starts pending', t.val($$select status from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000c3'$$) = 'pending');
select t.ok('default expiry is about 2 hours', t.val($$select (expires_at - created_at) between interval '119 minutes' and interval '121 minutes' from public.game_invites limit 1$$) = 'true');
select t.ok('alice cannot invite bob (not friends)', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000b2', 'okay://join?game=Space&addr=1.2.3.4:5')$$) = 'PT403:okay_rule:invite_friends');
select t.ok('cannot invite yourself', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000a1', 'okay://join?game=x&addr=1:2')$$) like '23514:%');
select t.ok('cannot send an invite in someone else''s name', t.try($$insert into public.game_invites (sender, recipient, link) values ('00000000-0000-0000-0000-0000000000c3', '00000000-0000-0000-0000-0000000000d4', 'okay://join?game=x&addr=1:2')$$) like '42501:%');
select t.ok('an unknown kind is refused', t.try($$insert into public.game_invites (recipient, kind, link) values ('00000000-0000-0000-0000-0000000000c3', 'virus', 'okay://join?game=x&addr=1:2')$$) like '23514:%');
select t.ok('an empty link is refused', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000c3', '')$$) like '23514:%');
select t.ok('a far-future expiry is cut to 24 hours', t.try($$insert into public.game_invites (recipient, kind, link, expires_at) values ('00000000-0000-0000-0000-0000000000d4', 'party', 'P1', now() + interval '30 days')$$) = 'ok'
  and t.val($$select expires_at <= now() + interval '24 hours 1 minute' from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000d4'$$) = 'true');
select t.ok('a forged created_at is replaced by the server time', t.try($$insert into public.game_invites (recipient, kind, link, created_at) values ('00000000-0000-0000-0000-0000000000d4', 'room', 'R1', now() - interval '5 days')$$) = 'ok'
  and t.val($$select created_at > now() - interval '1 minute' from public.game_invites where link = 'R1'$$) = 'true');
select t.ok('a forged accepted status is replaced by pending', t.try($$insert into public.game_invites (recipient, kind, link, status) values ('00000000-0000-0000-0000-0000000000d4', 'server', 'S1', 'accepted')$$) = 'ok'
  and t.val($$select status from public.game_invites where link = 'S1'$$) = 'pending');
select t.ok('a second game invite to carol replaces the first', t.try($$insert into public.game_invites (recipient, kind, title, link) values ('00000000-0000-0000-0000-0000000000c3', 'game', 'Space', 'okay://join?game=Space&relay=r:1&code=NEW')$$) = 'ok'
  and t.cnt($$select 1 from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000c3' and status = 'pending'$$) = 1
  and t.cnt($$select 1 from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000c3' and status = 'cancelled'$$) = 1);
select t.ok('alice cannot change status directly', (select t.try($$update public.game_invites set status = 'accepted'$$) in ('ok', '42501:permission denied for table game_invites'))
  and t.cnt($$select 1 from public.game_invites where status = 'accepted'$$) = 0);
reset role;

-- ======================= reading ============================================
select t.as_user(:'C'); set role authenticated;
select t.ok('carol reads the invites sent to her', t.cnt($$select 1 from public.game_invites$$) = 2);
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('bob reads none of them', t.cnt($$select 1 from public.game_invites$$) = 0);
select t.ok('bob cannot delete them', (select t.try($$delete from public.game_invites$$) = 'ok'));
reset role;
select t.ok('bob''s delete removed nothing', (select count(*) from public.game_invites) = 5);
select t.as_anon(); set role anon;
select t.ok('anon cannot read invites', t.try($$select 1 from public.game_invites$$) like '42501:%');
reset role;

-- ======================= respond / cancel ====================================
select t.as_user(:'B'); set role authenticated;
select t.ok('bob cannot accept carol''s invite', t.val($$select public.respond_invite((select max(id) from public.game_invites), true)$$) = 'gone');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('the sender cannot accept their own invite', t.val($$select public.respond_invite((select max(id) from public.game_invites where recipient = '00000000-0000-0000-0000-0000000000c3' and status = 'pending'), true)$$) = 'gone');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('carol accepts', t.val($$select public.respond_invite((select id from public.game_invites where status = 'pending'), true)$$) = 'accepted');
select t.ok('accepted is stored with a time', t.val($$select status || ':' || (responded_at is not null) from public.game_invites where link like '%NEW'$$) = 'accepted:true');
select t.ok('accepting twice says so', t.val($$select public.respond_invite((select id from public.game_invites where link like '%NEW'), true)$$) = 'already_accepted');
select t.ok('a cancelled invite cannot be accepted', t.val($$select public.respond_invite((select id from public.game_invites where status = 'cancelled'), true)$$) = 'already_cancelled');
reset role;
-- An expired invite.
select t.as_service();
update public.game_invites set expires_at = now() - interval '1 minute' where link = 'R1';
select t.as_user(:'D'); set role authenticated;
select t.ok('an expired invite says expired', t.val($$select public.respond_invite((select id from public.game_invites where link = 'R1'), true)$$) = 'expired');
select t.ok('dave declines the party invite', t.val($$select public.respond_invite((select id from public.game_invites where link = 'P1'), false)$$) = 'declined');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('alice cancels her server invite', t.val($$select public.cancel_invite((select id from public.game_invites where link = 'S1'))$$) = 'cancelled');
select t.ok('alice cannot cancel an invite she did not send', t.val($$select public.cancel_invite(-1)$$) = 'gone');
reset role;

-- ======================= blocks ==============================================
select t.as_user(:'A'); set role authenticated;
select t.ok('alice invites dave again', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000d4', 'okay://join?game=x&addr=1:2')$$) = 'ok');
reset role;
select t.as_user(:'D'); set role authenticated;
select t.ok('dave blocks alice', t.try($$insert into public.blocks (blocked) values ('00000000-0000-0000-0000-0000000000a1')$$) = 'ok');
select t.ok('the block withdrew alice''s pending invite', t.cnt($$select 1 from public.game_invites where status = 'pending'$$) = 0);
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('alice cannot invite dave across his block (generic text)', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000d4', 'okay://join?game=x&addr=1:2')$$) = 'PT403:okay_rule:dm_nobody');
reset role;

-- ======================= legacy chat invites are friends-only ================
select t.as_user(:'B'); set role authenticated;
select t.ok('stranger cannot send an [Invite] DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', E'[Invite] Space\nokay://join?game=Space&addr=6.6.6.6:1')$$) = 'PT403:okay_rule:invite_friends');
select t.ok('stranger cannot send a [Party] DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', E'[Party] p1\nbob')$$) = 'PT403:okay_rule:invite_friends');
select t.ok('stranger cannot send a [JoinRequest] DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', '[JoinRequest] Space')$$) = 'PT403:okay_rule:invite_friends');
select t.ok('stranger can still say hi', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'hi')$$) = 'ok');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('a friend can send an [Invite] DM', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', E'[Invite] Space\nokay://join?game=Space&addr=1.2.3.4:5')$$) = 'ok');
reset role;

-- ======================= delivery: client ids, no duplicates =================
select t.as_user(:'C'); set role authenticated;
select t.ok('a message with a client id is stored', t.try($$insert into public.messages (recipient, body, client_id) values ('00000000-0000-0000-0000-0000000000a1', 'once', '11111111-2222-3333-4444-555555555555')$$) = 'ok');
select t.ok('the same client id again is a conflict, not a second row', t.try($$insert into public.messages (recipient, body, client_id) values ('00000000-0000-0000-0000-0000000000a1', 'once', '11111111-2222-3333-4444-555555555555')$$) like '23505:%');
select t.ok('resend with on conflict do nothing is accepted (PostgREST ignore-duplicates)',
  t.try($$insert into public.messages (recipient, body, client_id) values ('00000000-0000-0000-0000-0000000000a1', 'once', '11111111-2222-3333-4444-555555555555') on conflict (sender, client_id) do nothing$$) = 'ok'
  and t.cnt($$select 1 from public.messages where body = 'once'$$) = 1);
select t.ok('messages without a client id still work', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'old client')$$) = 'ok'
  and t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', 'old client')$$) = 'ok');
reset role;
select t.as_user(:'A'); set role authenticated;
select t.ok('another sender may reuse the same client id', t.try($$insert into public.messages (recipient, body, client_id) values ('00000000-0000-0000-0000-0000000000c3', 'mine', '11111111-2222-3333-4444-555555555555')$$) = 'ok');
reset role;

-- ======================= limits =============================================
select t.as_user(:'C'); set role authenticated;
select t.ok('a 4000-character message is fine', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', repeat('x', 4000))$$) = 'ok');
select t.ok('a 4001-character message is too long', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000a1', repeat('x', 4001))$$) = 'PT403:okay_rule:too_long');
select t.ok('an edit cannot make a message too long', t.try($$update public.messages set body = repeat('y', 5000) where body = 'once'$$) = 'PT403:okay_rule:too_long');
reset role;
-- 30 messages from bob in the last minute (the service role is not limited).
select t.as_service();
insert into public.messages (sender, recipient, body) select :'B', :'C', 'spam ' || g from generate_series(1, 30) g;
select t.ok('the service role is not rate limited', (select count(*) from public.messages where body like 'spam %') = 30);
select t.as_user(:'B'); set role authenticated;
select t.ok('the 31st message in a minute is refused', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000c3', 'one more')$$) = 'PT403:okay_rule:rate_limited');
reset role;
select t.as_service();
update public.messages set created_at = now() - interval '2 minutes' where body like 'spam %';
select t.as_user(:'B'); set role authenticated;
select t.ok('after a minute bob can send again', t.try($$insert into public.messages (recipient, body) values ('00000000-0000-0000-0000-0000000000c3', 'one more')$$) = 'ok');
reset role;

-- group messages get the same client id and limits
select t.as_user(:'A'); set role authenticated;
select t.ok('alice makes a group with carol', t.try($$select public.create_group('crew', array['00000000-0000-0000-0000-0000000000c3']::uuid[])$$) = 'ok');
select t.ok('group message with a client id', t.try($$insert into public.group_messages (convo_id, body, client_id) values ((select max(id) from public.group_convos), 'g1', 'aaaaaaaa-2222-3333-4444-555555555555')$$) = 'ok');
select t.ok('group resend is a conflict, not a second row', t.try($$insert into public.group_messages (convo_id, body, client_id) values ((select max(id) from public.group_convos), 'g1', 'aaaaaaaa-2222-3333-4444-555555555555')$$) like '23505:%');
select t.ok('too long in a group', t.try($$insert into public.group_messages (convo_id, body) values ((select max(id) from public.group_convos), repeat('z', 4001))$$) = 'PT403:okay_rule:too_long');
reset role;

-- invite rate limit: 20 in 10 minutes
select t.as_service();
insert into public.game_invites (sender, recipient, kind, link) select :'C', :'A', 'room', 'spam' || g from generate_series(1, 20) g;
select t.as_user(:'C'); set role authenticated;
select t.ok('the 21st invite in 10 minutes is refused', t.try($$insert into public.game_invites (recipient, link) values ('00000000-0000-0000-0000-0000000000a1', 'okay://join?game=x&addr=1:2')$$) = 'PT403:okay_rule:rate_limited');
reset role;
