-- OkaySpace group chats (Supabase) — multi-person direct messages.
-- Run this once in your Supabase project's SQL editor (after social.sql). It is
-- idempotent and self-contained: re-running it is safe.
--
-- Design: a group chat is a membership-gated conversation, modelled on the
-- Servers feature (not the 1:1 `messages` table, which is left untouched so
-- existing DMs keep working). Membership drives every read. To avoid RLS
-- self-recursion, reads are gated on a SECURITY DEFINER helper and the initial
-- member set is written through a SECURITY DEFINER RPC.
--
-- Columns that identify "me" (group_convos.owner, group_members.member,
-- group_messages.sender) DEFAULT to auth.uid(), so the client never sends its
-- own user id — Postgres fills it from the signed-in JWT and RLS enforces it.

-- ---------------------------------------------------------------------------
-- group_convos: one row per group conversation.
-- ---------------------------------------------------------------------------
create table if not exists public.group_convos (
  id         bigint generated always as identity primary key,
  title      text,                       -- optional name; client derives one from members when null
  owner      uuid not null default auth.uid() references auth.users on delete cascade,
  created_at timestamptz default now()
);

-- ---------------------------------------------------------------------------
-- group_members: who is in each conversation. last_read is the newest
-- group_messages.id this member has seen (drives the unread badge).
-- ---------------------------------------------------------------------------
create table if not exists public.group_members (
  convo_id   bigint not null references public.group_convos on delete cascade,
  member     uuid not null default auth.uid() references auth.users on delete cascade,
  last_read  bigint default 0,
  created_at timestamptz default now(),
  primary key (convo_id, member)
);

-- ---------------------------------------------------------------------------
-- group_messages: the messages themselves. sender_name is denormalized so the
-- UI can show a name without a profiles join per row. reply_to quotes an
-- earlier message (same as 1:1 DMs); edited marks an edited body.
-- ---------------------------------------------------------------------------
create table if not exists public.group_messages (
  id          bigint generated always as identity primary key,
  convo_id    bigint not null references public.group_convos on delete cascade,
  sender      uuid not null default auth.uid() references auth.users on delete cascade,
  sender_name text,
  body        text not null,
  reply_to    bigint,
  edited      boolean default false,
  created_at  timestamptz default now()
);
create index if not exists group_messages_convo_idx on public.group_messages (convo_id, id);

-- SECURITY DEFINER membership check (bypasses RLS -> no recursion).
create or replace function public.is_group_member(cid bigint) returns boolean
  language sql security definer stable set search_path = public as $$
    select exists (select 1 from public.group_members m
                   where m.convo_id = cid and m.member = auth.uid());
  $$;

alter table public.group_convos   enable row level security;
alter table public.group_members  enable row level security;
alter table public.group_messages enable row level security;

-- group_convos: visible to members; the owner may rename / delete it.
drop policy if exists "see my groups"     on public.group_convos;
drop policy if exists "owner edits group" on public.group_convos;
drop policy if exists "owner drops group" on public.group_convos;
create policy "see my groups"     on public.group_convos for select
  using (owner = auth.uid() or public.is_group_member(id));
create policy "owner edits group" on public.group_convos for update using (owner = auth.uid());
create policy "owner drops group" on public.group_convos for delete using (owner = auth.uid());

-- group_members: visible to fellow members. The owner may add members directly
-- (the create/add RPCs also do, as SECURITY DEFINER). A member may update only
-- their own row (last_read) and may remove themselves (leave); the owner may
-- remove anyone (kick).
drop policy if exists "see group members"  on public.group_members;
drop policy if exists "owner adds member"  on public.group_members;
drop policy if exists "update own member"  on public.group_members;
drop policy if exists "leave or kick"      on public.group_members;
create policy "see group members" on public.group_members for select
  using (member = auth.uid() or public.is_group_member(convo_id));
create policy "owner adds member" on public.group_members for insert with check (
  exists (select 1 from public.group_convos c where c.id = convo_id and c.owner = auth.uid())
);
create policy "update own member" on public.group_members for update using (member = auth.uid());
create policy "leave or kick"     on public.group_members for delete using (
  member = auth.uid()
  or exists (select 1 from public.group_convos c where c.id = convo_id and c.owner = auth.uid())
);

-- group_messages: members read; a member sends/edits/deletes their own.
drop policy if exists "read group messages"   on public.group_messages;
drop policy if exists "send group messages"   on public.group_messages;
drop policy if exists "edit own group message" on public.group_messages;
drop policy if exists "delete own group message" on public.group_messages;
create policy "read group messages" on public.group_messages for select
  using (public.is_group_member(convo_id));
create policy "send group messages" on public.group_messages for insert
  with check (sender = auth.uid() and public.is_group_member(convo_id));
create policy "edit own group message"   on public.group_messages for update using (sender = auth.uid());
create policy "delete own group message" on public.group_messages for delete using (sender = auth.uid());

-- ---------------------------------------------------------------------------
-- Allow 'group'-scoped message reactions. social.sql constrains
-- message_reactions.scope to ('dm','channel'); widen it to include 'group' so
-- the existing reaction code path works on group messages too. Idempotent.
-- ---------------------------------------------------------------------------
do $$ begin
  if exists (select 1 from pg_constraint where conname = 'message_reactions_scope_check') then
    alter table public.message_reactions drop constraint message_reactions_scope_check;
  end if;
  alter table public.message_reactions
    add constraint message_reactions_scope_check check (scope in ('dm','channel','group'));
exception when undefined_table then
  null;  -- message_reactions not created yet (social.sql not run); skip.
end $$;

-- ---------------------------------------------------------------------------
-- create_group: start a conversation with an initial member set (plus the
-- creator, who becomes owner). Returns the new conversation row.
-- ---------------------------------------------------------------------------
create or replace function public.create_group(p_title text, p_members uuid[])
  returns public.group_convos language plpgsql security definer set search_path = public as $$
  declare g public.group_convos; uid uuid;
  begin
    insert into public.group_convos (title, owner) values (nullif(p_title,''), auth.uid())
      returning * into g;
    insert into public.group_members (convo_id, member) values (g.id, auth.uid())
      on conflict do nothing;
    if p_members is not null then
      foreach uid in array p_members loop
        if uid is not null and uid <> auth.uid() then
          insert into public.group_members (convo_id, member) values (g.id, uid)
            on conflict do nothing;
        end if;
      end loop;
    end if;
    return g;
  end; $$;

-- Add a member to a group (owner only). Returns true if added/already present.
create or replace function public.add_group_member(p_convo bigint, p_member uuid)
  returns boolean language plpgsql security definer set search_path = public as $$
  begin
    if not exists (select 1 from public.group_convos c where c.id = p_convo and c.owner = auth.uid()) then
      return false;
    end if;
    insert into public.group_members (convo_id, member) values (p_convo, p_member)
      on conflict do nothing;
    return true;
  end; $$;

-- my_groups: every group I'm in, with its last message and my unread marker,
-- most-recently-active first. One round-trip for the conversation list.
create or replace function public.my_groups()
  returns table (convo_id bigint, title text, owner uuid, member_count int,
                 last_id bigint, last_body text, last_sender text,
                 last_when timestamptz, last_read bigint)
  language sql security definer stable set search_path = public as $$
    select c.id, c.title, c.owner,
           (select count(*)::int from public.group_members m2 where m2.convo_id = c.id),
           lm.id, lm.body, lm.sender_name, lm.created_at,
           coalesce(gm.last_read, 0)
    from public.group_convos c
    join public.group_members gm on gm.convo_id = c.id and gm.member = auth.uid()
    left join lateral (
        select id, body, sender_name, created_at
        from public.group_messages m
        where m.convo_id = c.id
        order by m.id desc limit 1
    ) lm on true
    order by lm.created_at desc nulls last, c.id desc;
  $$;

grant execute on function public.is_group_member(bigint)          to anon, authenticated;
grant execute on function public.create_group(text, uuid[])       to anon, authenticated;
grant execute on function public.add_group_member(bigint, uuid)   to anon, authenticated;
grant execute on function public.my_groups()                      to anon, authenticated;
