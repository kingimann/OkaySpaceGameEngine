-- OkaySpace chat receipts: the typing indicator ("typing...") and read receipts
-- ("Seen" / "In the chat"). Run once in your Supabase project's SQL editor,
-- AFTER social.sql (it needs public.messages). Idempotent: re-running is safe.
-- Until it has run, the launcher shows "Not set up on the server yet" next to the
-- Typing indicator and Read receipts settings and neither feature does anything.
--
-- Used by engine/include/okay/Platform/Messages.hpp (SetTyping, PeerTyping,
-- MarkRead, PeerStateOf, PeerReadId). Both tables are tiny: one row per sender
-- (typing) and one row per (reader, peer) pair (reads), always upserted.
--
-- Access rule (matches public.messages): you can only WRITE rows that are yours
-- (sender / reader = you), and only READ rows where you are one of the two
-- people (yours, or addressed to you). Nobody else can see who is typing to
-- whom or how far anyone has read.

-- ---------------------------------------------------------------------------
-- typing_state: "sender is typing to peer as of `at`". One row per sender; the
-- client upserts it every couple of seconds while the composer has text and the
-- reader treats a row older than ~6 seconds as "not typing".
-- ---------------------------------------------------------------------------
create table if not exists public.typing_state (
  sender uuid primary key default auth.uid() references auth.users on delete cascade,
  peer   uuid not null references auth.users on delete cascade,
  at     timestamptz not null default now()
);
create index if not exists typing_state_peer_idx on public.typing_state (peer);
alter table public.typing_state enable row level security;

drop policy if exists "see own typing"    on public.typing_state;
drop policy if exists "send own typing"   on public.typing_state;
drop policy if exists "update own typing" on public.typing_state;
drop policy if exists "delete own typing" on public.typing_state;
create policy "see own typing"    on public.typing_state for select using (auth.uid() = sender or auth.uid() = peer);
create policy "send own typing"   on public.typing_state for insert with check (auth.uid() = sender);
create policy "update own typing" on public.typing_state for update using (auth.uid() = sender) with check (auth.uid() = sender);
create policy "delete own typing" on public.typing_state for delete using (auth.uid() = sender);

-- ---------------------------------------------------------------------------
-- message_reads: "reader has read your conversation up to message last_read_id".
-- updated_at is refreshed every few seconds while the chat is open and focused,
-- which the other side shows as "In the chat". The id must be a message of the
-- conversation between reader and peer, so nobody can claim to have read a
-- message of somebody else's chat.
-- ---------------------------------------------------------------------------
create table if not exists public.message_reads (
  reader       uuid not null default auth.uid() references auth.users on delete cascade,
  peer         uuid not null references auth.users on delete cascade,
  last_read_id bigint not null,
  updated_at   timestamptz not null default now(),
  primary key (reader, peer)
);
create index if not exists message_reads_peer_idx on public.message_reads (peer);
alter table public.message_reads enable row level security;

-- True when message `p_id` was sent between `p_reader` and `p_peer` (either way).
-- SECURITY INVOKER (the default): the caller's own RLS on public.messages applies,
-- so it can only ever see messages the caller is a party to.
create or replace function public.message_in_conversation(p_id bigint, p_reader uuid, p_peer uuid)
returns boolean language sql stable as $$
  select exists (
    select 1 from public.messages m
    where m.id = p_id
      and ((m.sender = p_reader and m.recipient = p_peer) or (m.sender = p_peer and m.recipient = p_reader))
  )
$$;

drop policy if exists "see own reads"    on public.message_reads;
drop policy if exists "mark own reads"   on public.message_reads;
drop policy if exists "update own reads" on public.message_reads;
drop policy if exists "delete own reads" on public.message_reads;
create policy "see own reads"    on public.message_reads for select using (auth.uid() = reader or auth.uid() = peer);
create policy "mark own reads"   on public.message_reads for insert
  with check (auth.uid() = reader and public.message_in_conversation(last_read_id, reader, peer));
create policy "update own reads" on public.message_reads for update
  using (auth.uid() = reader)
  with check (auth.uid() = reader and public.message_in_conversation(last_read_id, reader, peer));
create policy "delete own reads" on public.message_reads for delete using (auth.uid() = reader);

grant select, insert, update, delete on public.typing_state, public.message_reads to authenticated;
grant execute on function public.message_in_conversation(bigint, uuid, uuid) to authenticated;
