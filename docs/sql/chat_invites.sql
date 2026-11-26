-- OkaySpace chat delivery, chat limits and game invites (Supabase).
-- Run once in the SQL editor AFTER social.sql, group_chats.sql and blocks.sql (it
-- uses okay_private.are_friends and the blocks table). Idempotent: safe to re-run.
-- Also re-run blocks.sql once: it now shows a friend's join link
-- (profiles.session_code) only to friends.
--
-- Until this file runs, the launcher keeps working as before: messages are sent
-- without a client id, invites travel only as "[Invite]" chat messages, and the
-- limits are checked on the PC only (engine/include/okay/Platform/ChatDelivery.hpp,
-- engine/include/okay/Platform/Invites.hpp).
--
-- What it adds
--   * Delivery without duplicates: messages.client_id and group_messages.client_id
--     (a random id the sender's PC picks once per message). (sender, client_id) is
--     unique, so a message re-sent after a lost answer or on reconnect is stored
--     once. Old clients send no client id and are not affected.
--   * Limits the DATABASE enforces (a modified client gets the same answer):
--       - a message body is at most 4000 characters (okay_rule:too_long), on
--         insert and on edit;
--       - at most 30 messages a minute from one sender in DMs and at most 30 in
--         groups (okay_rule:rate_limited);
--       - chat invites ("[Invite] ", "[Party] ", "[JoinRequest] " DMs) only
--         between friends (okay_rule:invite_friends).
--   * game_invites: one record per invite to a game, party, community server or
--     room, with an expiry (default 2 hours, at most 24) and a status (pending,
--     accepted, declined, cancelled). Only friends can invite each other, blocks
--     are honoured both ways (a block also withdraws pending invites), at most 20
--     invites per 10 minutes per sender, and a new invite of the same kind to the
--     same friend replaces the older pending one. The sender and the recipient
--     can read it; nobody else. The status changes only through respond_invite
--     (the recipient) and cancel_invite (the sender), which say plainly why when
--     they cannot: 'expired', 'gone', 'already_<status>', 'invite_friends'.
--   Refusals are errors named "okay_rule:<reason>" (HTTP 403, code PT403), as in
--   blocks.sql, so the launcher can say exactly which rule said no.
--
-- Service role / SQL editor (auth.uid() is null) is not limited.

-- ---------------------------------------------------------------------------
-- Delivery: client ids
-- ---------------------------------------------------------------------------
alter table public.messages add column if not exists client_id uuid;
do $$ begin
  if not exists (select 1 from pg_constraint where conname = 'messages_sender_client_id_key') then
    alter table public.messages add constraint messages_sender_client_id_key unique (sender, client_id);
  end if;
end $$;
create index if not exists messages_sender_created_idx on public.messages (sender, created_at);

do $$ begin
  if to_regclass('public.group_messages') is not null then
    alter table public.group_messages add column if not exists client_id uuid;
    if not exists (select 1 from pg_constraint where conname = 'group_messages_sender_client_id_key') then
      alter table public.group_messages add constraint group_messages_sender_client_id_key unique (sender, client_id);
    end if;
    create index if not exists group_messages_sender_created_idx on public.group_messages (sender, created_at);
  end if;
end $$;

-- ---------------------------------------------------------------------------
-- Limits (BEFORE INSERT / UPDATE triggers on messages and group_messages)
-- ---------------------------------------------------------------------------
create or replace function okay_private.is_invite_body(p_body text)
  returns boolean language sql immutable as $$
  select left(coalesce(p_body, ''), 9) = '[Invite] ' or left(coalesce(p_body, ''), 8) = '[Party] '
      or left(coalesce(p_body, ''), 14) = '[JoinRequest] ';
$$;

create or replace function okay_private.chat_limits() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
declare n bigint;
begin
  if auth.uid() is null then return new; end if;
  if char_length(coalesce(new.body, '')) > 4000 then
    raise exception 'okay_rule:too_long' using errcode = 'PT403';
  end if;
  if tg_op <> 'INSERT' then return new; end if;
  if tg_table_name = 'messages' then   -- (nested: group_messages rows have no recipient field)
    if okay_private.is_invite_body(new.body) and not okay_private.are_friends(new.sender, new.recipient) then
      raise exception 'okay_rule:invite_friends' using errcode = 'PT403';
    end if;
  end if;
  execute format('select count(*) from public.%I where sender = $1 and created_at > now() - interval ''60 seconds''', tg_table_name)
    into n using new.sender;
  if n >= 30 then raise exception 'okay_rule:rate_limited' using errcode = 'PT403'; end if;
  return new;
end $$;

drop trigger if exists okay_messages_limits on public.messages;
create trigger okay_messages_limits before insert or update of body on public.messages
  for each row execute function okay_private.chat_limits();
do $$ begin
  if to_regclass('public.group_messages') is not null then
    drop trigger if exists okay_group_messages_limits on public.group_messages;
    create trigger okay_group_messages_limits before insert or update of body on public.group_messages
      for each row execute function okay_private.chat_limits();
  end if;
end $$;

-- Also as a RESTRICTIVE policy, so the friends-only invite rule holds even if a
-- trigger is dropped.
drop policy if exists "okay rules: invites between friends" on public.messages;
create policy "okay rules: invites between friends" on public.messages as restrictive for insert
  with check (not okay_private.is_invite_body(body) or okay_private.are_friends(sender, recipient));

-- ---------------------------------------------------------------------------
-- game_invites
-- ---------------------------------------------------------------------------
create table if not exists public.game_invites (
  id           bigint generated always as identity primary key,
  sender       uuid not null default auth.uid() references auth.users on delete cascade,
  recipient    uuid not null references auth.users on delete cascade,
  kind         text not null default 'game' check (kind in ('game', 'party', 'server', 'room')),
  title        text not null default '' check (char_length(title) <= 120),
  link         text not null check (char_length(link) between 1 and 2000),   -- okay://join link, party id, server code or room code
  game_version text not null default '' check (char_length(game_version) <= 40),
  target       text not null default '' check (target in ('', 'exported', 'project', 'dedicated')),
  status       text not null default 'pending' check (status in ('pending', 'accepted', 'declined', 'cancelled')),
  created_at   timestamptz not null default now(),
  expires_at   timestamptz not null default now() + interval '2 hours',
  responded_at timestamptz,
  constraint game_invites_not_self check (sender <> recipient)
);
create index if not exists game_invites_recipient_idx on public.game_invites (recipient, id);
create index if not exists game_invites_sender_idx on public.game_invites (sender, created_at);
alter table public.game_invites enable row level security;

drop policy if exists "see my invites"    on public.game_invites;
drop policy if exists "send own invites"  on public.game_invites;
drop policy if exists "clear my invites"  on public.game_invites;
create policy "see my invites"   on public.game_invites for select using (auth.uid() = sender or auth.uid() = recipient);
create policy "send own invites" on public.game_invites for insert with check (auth.uid() = sender);
create policy "clear my invites" on public.game_invites for delete using (auth.uid() = sender or auth.uid() = recipient);
-- No update policy: the status changes only through respond_invite / cancel_invite.

-- The reason p_sender may not invite p_recipient, or null.
create or replace function okay_private.invite_rule(p_sender uuid, p_recipient uuid)
  returns text language plpgsql stable security definer set search_path = public, pg_temp as $$
begin
  if p_sender is null or p_recipient is null or p_sender = p_recipient then return null; end if;
  if exists (select 1 from public.blocks where blocker = p_sender and blocked = p_recipient) then return 'you_blocked'; end if;
  if exists (select 1 from public.blocks where blocker = p_recipient and blocked = p_sender) then return 'dm_nobody'; end if;
  if not okay_private.are_friends(p_sender, p_recipient) then return 'invite_friends'; end if;
  return null;
end $$;

create or replace function okay_private.game_invites_rules() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
declare r text; n bigint;
begin
  if auth.uid() is null then return new; end if;
  if new.sender is distinct from auth.uid() then return new; end if;   -- the "send own invites" policy refuses it
  r := okay_private.invite_rule(new.sender, new.recipient);
  if r is not null then raise exception 'okay_rule:%', r using errcode = 'PT403'; end if;
  select count(*) into n from public.game_invites where sender = new.sender and created_at > now() - interval '10 minutes';
  if n >= 20 then raise exception 'okay_rule:rate_limited' using errcode = 'PT403'; end if;
  -- The server owns the clock and the status.
  new.created_at := now();
  new.status := 'pending';
  new.responded_at := null;
  new.expires_at := least(greatest(coalesce(new.expires_at, now() + interval '2 hours'), now() + interval '1 minute'),
                          now() + interval '24 hours');
  -- One live invite of a kind per friend: the newest replaces the older ones.
  update public.game_invites set status = 'cancelled', responded_at = now()
   where sender = new.sender and recipient = new.recipient and kind = new.kind and status = 'pending';
  return new;
end $$;
drop trigger if exists okay_game_invites_rules on public.game_invites;
create trigger okay_game_invites_rules before insert on public.game_invites
  for each row execute function okay_private.game_invites_rules();

drop policy if exists "okay rules: invite insert" on public.game_invites;
create policy "okay rules: invite insert" on public.game_invites as restrictive for insert
  with check (okay_private.invite_rule(sender, recipient) is null);

-- A block withdraws the pending invites between the two.
create or replace function okay_private.blocks_cancel_invites() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
begin
  update public.game_invites set status = 'cancelled', responded_at = now()
   where status = 'pending'
     and ((sender = new.blocker and recipient = new.blocked) or (sender = new.blocked and recipient = new.blocker));
  return new;
end $$;
drop trigger if exists okay_blocks_cancel_invites on public.blocks;
create trigger okay_blocks_cancel_invites after insert on public.blocks
  for each row execute function okay_private.blocks_cancel_invites();

-- The recipient accepts or declines. Returns 'accepted' | 'declined' | 'expired' |
-- 'gone' (no such invite for you) | 'already_<status>' | 'invite_friends' | 'dm_nobody' | 'you_blocked'.
create or replace function public.respond_invite(p_id bigint, p_accept boolean)
  returns text language plpgsql security definer set search_path = public, pg_temp as $$
declare g public.game_invites; r text;
begin
  select * into g from public.game_invites where id = p_id and recipient = auth.uid() for update;
  if not found or auth.uid() is null then return 'gone'; end if;
  if g.status <> 'pending' then return 'already_' || g.status; end if;
  if g.expires_at <= now() then return 'expired'; end if;
  if p_accept then
    r := okay_private.invite_rule(g.recipient, g.sender);
    if r is not null then return r; end if;
  end if;
  update public.game_invites set status = case when p_accept then 'accepted' else 'declined' end, responded_at = now()
   where id = p_id;
  return case when p_accept then 'accepted' else 'declined' end;
end $$;

-- The sender withdraws an invite. Returns 'cancelled' | 'gone' | 'already_<status>'.
create or replace function public.cancel_invite(p_id bigint)
  returns text language plpgsql security definer set search_path = public, pg_temp as $$
declare g public.game_invites;
begin
  select * into g from public.game_invites where id = p_id and sender = auth.uid() for update;
  if not found or auth.uid() is null then return 'gone'; end if;
  if g.status <> 'pending' then return 'already_' || g.status; end if;
  update public.game_invites set status = 'cancelled', responded_at = now() where id = p_id;
  return 'cancelled';
end $$;

revoke execute on function public.respond_invite(bigint, boolean) from public, anon;
revoke execute on function public.cancel_invite(bigint) from public, anon;
grant execute on function public.respond_invite(bigint, boolean) to authenticated, service_role;
grant execute on function public.cancel_invite(bigint) to authenticated, service_role;
revoke execute on all functions in schema okay_private from public;
grant execute on all functions in schema okay_private to anon, authenticated, service_role;
revoke update on public.game_invites from anon, authenticated;
revoke all on public.game_invites from anon;

notify pgrst, 'reload schema';
