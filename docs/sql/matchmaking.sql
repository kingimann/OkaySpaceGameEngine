-- Matchmaking queues ("Find match"). A player (or a party leader, for the whole
-- party) asks for a match in a game + mode. Whoever polls runs the matcher, which
-- groups waiting tickets by game, mode, size, skill and region:
--   - the skill window starts at +-100 (skill 0-1000, from player_stats wins) and
--     widens by 25 a second, so nobody waits forever;
--   - after 30 s any region is fine; after 45 s a match may start with fewer
--     players (down to the ticket's minimum).
-- A formed match goes to a dedicated server listed in the browser for that game
-- with room for everyone (each match in its own room). With no free server, one
-- player hosts: through the site relay when app_config 'game_relay' is set
-- (everyone gets the relay + a fresh code), otherwise the host's game posts its
-- join link and the others connect to it.
-- Players never touch the tables; everything goes through the functions below.
-- Uses game_servers.sql, matches.sql (skill), parties.sql and app_config.sql when
-- present. Safe to run more than once.

create table if not exists public.match_tickets (
  id             uuid        primary key default gen_random_uuid(),
  owner          uuid        not null references auth.users on delete cascade,
  members        uuid[]      not null,
  game           text        not null,           -- lower-case
  mode           text        not null default '',
  region         text        not null default '',
  players_needed int         not null,
  min_players    int         not null,
  skill          int         not null default 500,
  status         text        not null default 'searching' check (status in ('searching', 'matched')),
  assignment     uuid,
  created_at     timestamptz not null default now(),
  heartbeat      timestamptz not null default now()
);
create unique index if not exists match_tickets_owner on public.match_tickets (owner);
create index if not exists match_tickets_queue on public.match_tickets (game, mode, status, created_at);

create table if not exists public.match_assignments (
  id          uuid        primary key default gen_random_uuid(),
  game        text        not null,
  mode        text        not null default '',
  players     int         not null default 0,
  server_addr text        not null default '',   -- a listed dedicated server ("host:port")
  server_name text        not null default '',
  room        text        not null default '',   -- this match's room on that server
  relay_addr  text        not null default '',   -- or: the relay + code everyone uses
  relay_code  text        not null default '',
  host_user   uuid,                               -- or: the player who hosts
  host_link   text        not null default '',   -- (their join link, once they're hosting)
  created_at  timestamptz not null default now()
);

alter table public.match_tickets     enable row level security;
alter table public.match_assignments enable row level security;
-- No policies: only the functions below (security definer) read or write these.

-- 0-1000 from wins / matches (500 = new player). 500 when matches.sql isn't set up.
create or replace function public.mm_skill(p_user uuid, p_game text) returns int
language plpgsql stable security definer set search_path = public as $$
declare w int; m int;
begin
  if to_regclass('public.player_stats') is null then return 500; end if;
  execute 'select wins, matches from public.player_stats where user_id = $1 and game = $2'
    into w, m using p_user, lower(p_game);
  if m is null then return 500; end if;
  return round(1000.0 * (w + 1) / (m + 2));
end;
$$;

-- Try to form matches in one queue. Returns how many it formed.
create or replace function public.mm_match(p_game text, p_mode text) returns int
language plpgsql security definer set search_path = public as $$
declare
  a       record;
  t       record;
  waited  double precision;
  win     double precision;
  total   int;
  picked  uuid[];
  formed  int := 0;
  aid     uuid;
  s_addr  text;
  s_name  text;
  relay   text;
begin
  perform pg_advisory_xact_lock(hashtext('okay-mm:' || p_game || ':' || p_mode));
  for a in
    select * from match_tickets
    where game = p_game and mode = p_mode and status = 'searching' and heartbeat > now() - interval '15 seconds'
    order by created_at limit 50
  loop
    if (select status from match_tickets where id = a.id) <> 'searching' then continue; end if;
    waited := extract(epoch from now() - a.created_at);
    win := 100 + 25 * waited;
    total := 0; picked := '{}';
    for t in
      select * from match_tickets
      where game = p_game and mode = p_mode and status = 'searching'
        and heartbeat > now() - interval '15 seconds'
        and players_needed = a.players_needed
        and abs(skill - a.skill) <= win
        and (waited >= 30 or region = '' or a.region = '' or region = a.region)
      order by (id = a.id) desc, created_at
    loop
      if total + cardinality(t.members) <= a.players_needed then
        picked := picked || t.id;
        total := total + cardinality(t.members);
      end if;
      exit when total = a.players_needed;
    end loop;
    if total < a.players_needed and not (waited >= 45 and total >= a.min_players and cardinality(picked) > 1) then
      continue;
    end if;

    -- Where to play: a listed dedicated server with room (counting matches just sent there).
    s_addr := null; s_name := ''; relay := '';
    if to_regclass('public.game_sessions') is not null then
      execute $q$
        select s.host_addr || ':' || s.port, s.name from public.game_sessions s
        where lower(s.game) = $1 and s.dedicated and not s.has_password and s.host_addr <> ''
          and s.updated_at > now() - interval '2 minutes'
          and s.players + $2 + coalesce((select sum(m.players) from public.match_assignments m
                                         where m.server_addr = s.host_addr || ':' || s.port
                                           and m.created_at > now() - interval '60 seconds'), 0) <= s.max_players
        order by s.players desc, s.updated_at desc limit 1 $q$
        into s_addr, s_name using p_game, total;
    end if;
    if s_addr is null and to_regclass('public.app_config') is not null then
      execute 'select value from public.app_config where key = ''game_relay''' into relay;
      relay := coalesce(btrim(relay), '');
    end if;

    aid := gen_random_uuid();
    insert into match_assignments (id, game, mode, players, server_addr, server_name, room, relay_addr, relay_code, host_user)
    values (aid, p_game, p_mode, total,
            coalesce(s_addr, ''), coalesce(s_name, ''),
            case when s_addr is null then '' else 'match-' || left(aid::text, 8) end,
            case when s_addr is null then relay else '' end,
            case when s_addr is null and relay <> '' then upper(substr(md5(random()::text || aid::text), 1, 6)) else '' end,
            case when s_addr is null then a.owner else null end);
    update match_tickets set status = 'matched', assignment = aid where id = any(picked);
    formed := formed + 1;
  end loop;
  return formed;
end;
$$;

-- Start searching (replaces any search I had). With p_party, a party leader
-- searches for the whole party (members follow the leader into the match).
create or replace function public.queue_join(p_game text, p_mode text default '', p_region text default '',
                                             p_players int default 2, p_min_players int default 0,
                                             p_party boolean default true)
returns uuid language plpgsql security definer set search_path = public as $$
declare
  me   uuid := auth.uid();
  g    text := lower(left(btrim(coalesce(p_game, '')), 120));
  m    text := left(btrim(coalesce(p_mode, '')), 40);
  mem  uuid[] := array[auth.uid()];
  pid  uuid;
  ldr  uuid;
  n    int;
  mn   int;
  sk   int := 0;
  u    uuid;
  tid  uuid;
begin
  if me is null then raise exception 'Sign in to find a match.'; end if;
  if g = '' then raise exception 'Which game?'; end if;
  n  := greatest(2, least(coalesce(p_players, 2), 64));
  mn := case when coalesce(p_min_players, 0) <= 0 then n else greatest(2, least(p_min_players, n)) end;

  if p_party and to_regclass('public.party_members') is not null then
    execute 'select party_id from public.party_members where member = $1' into pid using me;
    if pid is not null then
      execute 'select leader from public.parties where id = $1' into ldr using pid;
      if ldr is distinct from me then raise exception 'Only your party leader can start matchmaking.'; end if;
      execute 'select array_agg(member order by joined_at) from public.party_members where party_id = $1' into mem using pid;
    end if;
  end if;
  if cardinality(mem) > n then raise exception 'Your party is bigger than a match (% players).', n; end if;
  if exists (select 1 from match_tickets where owner <> me and status = 'searching' and members && mem) then
    raise exception 'Someone in your party is already searching.';
  end if;

  foreach u in array mem loop sk := sk + mm_skill(u, g); end loop;
  sk := sk / cardinality(mem);

  delete from match_tickets where owner = me;
  insert into match_tickets (owner, members, game, mode, region, players_needed, min_players, skill)
  values (me, mem, g, m, left(coalesce(p_region, ''), 32), n, mn, sk)
  returning id into tid;
  perform mm_match(g, m);
  return tid;
end;
$$;

-- Keep searching (call every couple of seconds) and see where I stand.
create or replace function public.queue_poll(p_ticket uuid)
returns table (status text, waited int, searching int, assignment uuid, players int,
               server_addr text, server_name text, room text, relay_addr text, relay_code text,
               is_host boolean, host_link text)
language plpgsql security definer set search_path = public as $$
#variable_conflict use_column
declare
  me uuid := auth.uid();
  t  record;
  a  record;
begin
  if me is null then raise exception 'Sign in to find a match.'; end if;
  -- Tidy up: abandoned searches, and old matches.
  delete from match_tickets where match_tickets.status = 'searching' and heartbeat < now() - interval '45 seconds';
  delete from match_tickets where match_tickets.status = 'matched' and created_at < now() - interval '30 minutes';
  delete from match_assignments where created_at < now() - interval '2 hours';

  select * into t from match_tickets where id = p_ticket and owner = me;
  if not found then
    return query select 'gone'::text, 0, 0, null::uuid, 0, ''::text, ''::text, ''::text, ''::text, ''::text, false, ''::text;
    return;
  end if;
  if t.status = 'searching' then
    update match_tickets set heartbeat = now() where id = t.id;
    perform mm_match(t.game, t.mode);
    select * into t from match_tickets where id = p_ticket;
  end if;
  if t.status = 'matched' then
    select * into a from match_assignments where id = t.assignment;
    return query select 'matched'::text, extract(epoch from now() - t.created_at)::int, 0, a.id, a.players,
                        a.server_addr, a.server_name, a.room, a.relay_addr, a.relay_code,
                        coalesce(a.host_user = me, false), a.host_link;
    return;
  end if;
  return query select 'searching'::text, extract(epoch from now() - t.created_at)::int,
                      coalesce((select sum(cardinality(q.members)) from match_tickets q
                                where q.game = t.game and q.mode = t.mode and q.status = 'searching'
                                  and q.heartbeat > now() - interval '15 seconds'), 0)::int,
                      null::uuid, 0, ''::text, ''::text, ''::text, ''::text, ''::text, false, ''::text;
end;
$$;

-- Stop searching (or forget a finished ticket).
create or replace function public.queue_leave(p_ticket uuid default null) returns void
language sql security definer set search_path = public as $$
  delete from match_tickets where owner = auth.uid() and (p_ticket is null or id = p_ticket);
$$;

-- The hosting player's game: here's how to join me.
create or replace function public.queue_host_ready(p_assignment uuid, p_link text) returns void
language plpgsql security definer set search_path = public as $$
begin
  update match_assignments set host_link = left(coalesce(p_link, ''), 600)
  where id = p_assignment and host_user = auth.uid();
  if not found then raise exception 'You are not hosting that match.'; end if;
end;
$$;

revoke all on function public.mm_skill(uuid, text)                              from public, anon, authenticated;
revoke all on function public.mm_match(text, text)                              from public, anon, authenticated;
revoke all on function public.queue_join(text, text, text, int, int, boolean)   from public, anon;
revoke all on function public.queue_poll(uuid)                                  from public, anon;
revoke all on function public.queue_leave(uuid)                                 from public, anon;
revoke all on function public.queue_host_ready(uuid, text)                      from public, anon;
grant execute on function public.queue_join(text, text, text, int, int, boolean) to authenticated;
grant execute on function public.queue_poll(uuid)                                to authenticated;
grant execute on function public.queue_leave(uuid)                               to authenticated;
grant execute on function public.queue_host_ready(uuid, text)                    to authenticated;

-- Players host through this relay when no dedicated server has room
-- (okayspace-relay, "host:port"). Leave empty to have the host post its own address.
do $$ begin
  if to_regclass('public.app_config') is not null then
    insert into public.app_config (key, value) values ('game_relay', '') on conflict (key) do nothing;
  end if;
end $$;
