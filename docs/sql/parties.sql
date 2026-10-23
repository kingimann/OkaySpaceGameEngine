-- Parties: a group of friends that plays together. The leader's game session
-- is stored on the party; members' launchers follow it into the same match.
-- Joining / leaving / kicking go through SECURITY DEFINER functions so the
-- rules (one party at a time, size limit, leader-only actions, hand the lead on
-- when the leader leaves) hold no matter what a client sends.
-- Safe to run more than once.

create table if not exists public.parties (
  id          uuid        primary key default gen_random_uuid(),
  leader      uuid        not null references auth.users on delete cascade,
  game        text        not null default '',   -- what the leader is playing
  join_link   text        not null default '',   -- the leader's session (okay://join?...); members follow it
  max_size    int         not null default 8,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now()
);

create table if not exists public.party_members (
  party_id   uuid        not null references public.parties on delete cascade,
  member     uuid        not null references auth.users on delete cascade,
  ready      boolean     not null default false,
  joined_at  timestamptz not null default now(),
  primary key (party_id, member)
);
create unique index if not exists party_members_one_party on public.party_members (member);   -- one party at a time

alter table public.parties       enable row level security;
alter table public.party_members enable row level security;

-- The party I'm in (null if none). SECURITY DEFINER so policies can use it
-- without recursing into party_members' own policy.
create or replace function public.my_party() returns uuid
language sql stable security definer set search_path = public as $$
  select party_id from public.party_members where member = auth.uid() limit 1;
$$;

drop policy if exists "parties - members read"  on public.parties;
drop policy if exists "parties - leader edits"  on public.parties;
drop policy if exists "party members - read"    on public.party_members;
drop policy if exists "party members - ready"   on public.party_members;
create policy "parties - members read" on public.parties for select using (id = public.my_party());
create policy "parties - leader edits" on public.parties for update using (leader = auth.uid()) with check (leader = auth.uid());
create policy "party members - read"   on public.party_members for select using (party_id = public.my_party());
create policy "party members - ready"  on public.party_members for update using (member = auth.uid()) with check (member = auth.uid());
-- (No insert/delete policies: those go through the functions below.)

create or replace function public.touch_party() returns trigger
language plpgsql set search_path = public as $$
begin new.updated_at := now(); return new; end;
$$;
drop trigger if exists touch on public.parties;
create trigger touch before update on public.parties
  for each row execute function public.touch_party();

-- Leave my party: hand the lead to the longest-standing member, or delete the
-- party when I was the last one in it.
create or replace function public.leave_party() returns void
language plpgsql security definer set search_path = public as $$
declare pid uuid; next_leader uuid;
begin
  select party_id into pid from party_members where member = auth.uid();
  if pid is null then return; end if;
  delete from party_members where party_id = pid and member = auth.uid();
  select member into next_leader from party_members where party_id = pid order by joined_at limit 1;
  if next_leader is null then
    delete from parties where id = pid;
  else
    update parties set leader = next_leader where id = pid and leader = auth.uid();
  end if;
end;
$$;

-- Start a new party with me as leader (leaving any party I'm in).
create or replace function public.create_party() returns public.parties
language plpgsql security definer set search_path = public as $$
declare p public.parties;
begin
  if auth.uid() is null then raise exception 'sign in first'; end if;
  perform public.leave_party();
  insert into parties (leader) values (auth.uid()) returning * into p;
  insert into party_members (party_id, member, ready) values (p.id, auth.uid(), true);
  return p;
end;
$$;

-- Join a party by id (from an invite), leaving any other party.
create or replace function public.join_party(p_party uuid) returns public.parties
language plpgsql security definer set search_path = public as $$
declare p public.parties; n int;
begin
  if auth.uid() is null then raise exception 'sign in first'; end if;
  select * into p from parties where id = p_party;
  if p.id is null then raise exception 'that party has ended'; end if;
  if exists (select 1 from party_members where party_id = p_party and member = auth.uid()) then return p; end if;
  select count(*) into n from party_members where party_id = p_party;
  if n >= p.max_size then raise exception 'the party is full'; end if;
  perform public.leave_party();
  insert into party_members (party_id, member) values (p_party, auth.uid());
  return p;
end;
$$;

-- Leader only: remove someone / hand over the lead.
create or replace function public.kick_from_party(p_member uuid) returns void
language plpgsql security definer set search_path = public as $$
begin
  if p_member = auth.uid() then return; end if;
  delete from party_members m using parties p
   where m.party_id = p.id and p.leader = auth.uid() and m.member = p_member;
end;
$$;
create or replace function public.promote_party_leader(p_member uuid) returns void
language plpgsql security definer set search_path = public as $$
begin
  update parties p set leader = p_member
   where p.leader = auth.uid()
     and exists (select 1 from party_members m where m.party_id = p.id and m.member = p_member);
end;
$$;

grant execute on function public.my_party()              to authenticated;
grant execute on function public.leave_party()           to authenticated;
grant execute on function public.create_party()          to authenticated;
grant execute on function public.join_party(uuid)        to authenticated;
grant execute on function public.kick_from_party(uuid)   to authenticated;
grant execute on function public.promote_party_leader(uuid) to authenticated;
