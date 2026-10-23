-- OkaySpace chat extras: edit history and shared pinned messages. Run once in
-- your Supabase project's SQL editor (after social.sql and group_chats.sql).
-- Idempotent — re-running is safe. Until it has run, the launcher keeps pins
-- on this device only and shows no edit history.
--
-- Both tables point at a message by (scope, target_id): scope 'dm' is
-- public.messages, 'group' is public.group_messages. Visibility follows the
-- message itself: the subqueries below run with the reader's own row-level
-- security, so you only see edits / pins of messages you can read.

-- ---------------------------------------------------------------------------
-- message_edits: the text a message had before each edit. The client records
-- the old body right before it saves an edit; only the message's sender can
-- add rows. Everyone in the chat can read them ("edited" -> earlier versions).
-- ---------------------------------------------------------------------------
create table if not exists public.message_edits (
  id         bigint generated always as identity primary key,
  scope      text not null check (scope in ('dm','group')),
  target_id  bigint not null,
  editor     uuid not null default auth.uid() references auth.users on delete cascade,
  old_body   text not null,
  edited_at  timestamptz default now()
);
create index if not exists message_edits_target_idx on public.message_edits (scope, target_id, id);
alter table public.message_edits enable row level security;

drop policy if exists "see edits of visible messages" on public.message_edits;
drop policy if exists "record own edits"              on public.message_edits;
create policy "see edits of visible messages" on public.message_edits for select using (
  (scope = 'dm'    and exists (select 1 from public.messages m       where m.id = target_id))
  or (scope = 'group' and exists (select 1 from public.group_messages g where g.id = target_id))
);
create policy "record own edits" on public.message_edits for insert with check (
  editor = auth.uid() and (
    (scope = 'dm'    and exists (select 1 from public.messages m       where m.id = target_id and m.sender = auth.uid()))
    or (scope = 'group' and exists (select 1 from public.group_messages g where g.id = target_id and g.sender = auth.uid()))
  )
);

-- ---------------------------------------------------------------------------
-- message_pins: messages pinned in a chat, seen by everyone in it. Anyone who
-- can read the message may pin or unpin it (as in Messenger / Telegram DMs).
-- ---------------------------------------------------------------------------
create table if not exists public.message_pins (
  scope      text not null check (scope in ('dm','group')),
  target_id  bigint not null,
  pinned_by  uuid not null default auth.uid() references auth.users on delete cascade,
  created_at timestamptz default now(),
  primary key (scope, target_id)
);
alter table public.message_pins enable row level security;

drop policy if exists "see pins of visible messages" on public.message_pins;
drop policy if exists "pin visible messages"         on public.message_pins;
drop policy if exists "unpin visible messages"       on public.message_pins;
create policy "see pins of visible messages" on public.message_pins for select using (
  (scope = 'dm'    and exists (select 1 from public.messages m       where m.id = target_id))
  or (scope = 'group' and exists (select 1 from public.group_messages g where g.id = target_id))
);
create policy "pin visible messages" on public.message_pins for insert with check (
  pinned_by = auth.uid() and (
    (scope = 'dm'    and exists (select 1 from public.messages m       where m.id = target_id))
    or (scope = 'group' and exists (select 1 from public.group_messages g where g.id = target_id))
  )
);
create policy "unpin visible messages" on public.message_pins for delete using (
  (scope = 'dm'    and exists (select 1 from public.messages m       where m.id = target_id))
  or (scope = 'group' and exists (select 1 from public.group_messages g where g.id = target_id))
);
