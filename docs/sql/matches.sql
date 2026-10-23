-- Match results, per-game player stats, leaderboards and "Recently played with".
-- A match's host (a dedicated server, or the player hosting) reports it when it
-- ends (net_match_end() in scripts). Every signed-in player in it gets a stats row
-- for that game. Players never write these tables directly: reporting goes through
-- report_match(), and reads through the functions at the bottom.
-- Needs social.sql (profiles). Uses game_servers.sql (game_sessions) when present.
-- Safe to run more than once.

create table if not exists public.matches (
  id           uuid        primary key default gen_random_uuid(),
  reporter     uuid        references auth.users on delete set null,
  game         text        not null,
  map          text        not null default '',
  server       text        not null default '',
  mode         text        not null default '',
  dedicated    boolean     not null default false,
  duration_s   int         not null default 0,
  player_count int         not null default 0,
  ended_at     timestamptz not null default now()
);
create index if not exists matches_game     on public.matches (lower(game), ended_at desc);
create index if not exists matches_reporter on public.matches (reporter, ended_at desc);

create table if not exists public.match_players (
  id       bigint generated always as identity primary key,
  match_id uuid   not null references public.matches on delete cascade,
  user_id  uuid   references auth.users on delete cascade,   -- null: not signed in
  name     text   not null default '',
  score    int    not null default 0,
  place    int    not null default 0,
  won      boolean not null default false,
  seconds  int    not null default 0
);
create index if not exists match_players_user  on public.match_players (user_id, match_id);
create index if not exists match_players_match on public.match_players (match_id);

-- One row per player per game (game is stored lower-case).
create table if not exists public.player_stats (
  user_id     uuid        not null references auth.users on delete cascade,
  game        text        not null,
  matches     int         not null default 0,
  wins        int         not null default 0,
  total_score bigint      not null default 0,
  best_score  int         not null default 0,
  seconds     bigint      not null default 0,
  last_played timestamptz not null default now(),
  primary key (user_id, game)
);
create index if not exists player_stats_wins  on public.player_stats (game, wins desc);
create index if not exists player_stats_score on public.player_stats (game, total_score desc);
create index if not exists player_stats_best  on public.player_stats (game, best_score desc);

-- Signed-in players can read everything (match history is public, like Steam's);
-- nobody can write except through report_match().
alter table public.matches       enable row level security;
alter table public.match_players enable row level security;
alter table public.player_stats  enable row level security;
drop policy if exists "matches - read"       on public.matches;
drop policy if exists "match players - read" on public.match_players;
drop policy if exists "player stats - read"  on public.player_stats;
create policy "matches - read"       on public.matches       for select using (auth.uid() is not null);
create policy "match players - read" on public.match_players for select using (auth.uid() is not null);
create policy "player stats - read"  on public.player_stats  for select using (auth.uid() is not null);

-- report_match(p): p = {"game","map","server","mode","dedicated","duration",
--   "players":[{"user_id"|null,"name","score","place","won","self","seconds"}]}
-- Who may report: the player hosting (they're in it, marked "self"), or a server
-- listed in the browser (game_sessions) for this game in the last 10 minutes.
-- One report per reporter every 15 seconds. Returns the match id.
create or replace function public.report_match(p jsonb) returns uuid
language plpgsql security definer set search_path = public as $$
declare
  me      uuid := auth.uid();
  g       text := left(btrim(coalesce(p->>'game', '')), 120);
  n       int;
  mid     uuid;
  pl      jsonb;
  uid     uuid;
  sc      int;
  secs    int;
  won     boolean;
  host_ok boolean := false;
begin
  if me is null then raise exception 'Sign in to report matches.'; end if;
  if g = '' then raise exception 'A match needs a game.'; end if;
  if jsonb_typeof(p->'players') is distinct from 'array' then raise exception 'A match needs players.'; end if;
  n := jsonb_array_length(p->'players');
  if n < 1 or n > 128 then raise exception 'A match needs 1-128 players.'; end if;
  if exists (select 1 from matches where reporter = me and ended_at > now() - interval '15 seconds') then
    raise exception 'Too many match reports; try again in a few seconds.';
  end if;

  host_ok := exists (select 1 from jsonb_array_elements(p->'players') e where coalesce((e->>'self')::boolean, false));
  if not host_ok and to_regclass('public.game_sessions') is not null then
    execute 'select exists (select 1 from public.game_sessions where host_id = $1 and lower(game) = lower($2)
                            and updated_at > now() - interval ''10 minutes'')'
      into host_ok using me, g;
  end if;
  if not host_ok then raise exception 'Only the match host or a listed server can report results.'; end if;

  insert into matches (reporter, game, map, server, mode, dedicated, duration_s, player_count)
  values (me, g, left(coalesce(p->>'map', ''), 120), left(coalesce(p->>'server', ''), 120),
          left(coalesce(p->>'mode', ''), 60), coalesce((p->>'dedicated')::boolean, false),
          greatest(0, least(coalesce((p->>'duration')::int, 0), 86400)), n)
  returning id into mid;

  for pl in select value from jsonb_array_elements(p->'players') loop
    uid := null;
    if coalesce((pl->>'self')::boolean, false) then
      uid := me;
    elsif nullif(pl->>'user_id', '') is not null then
      begin uid := (pl->>'user_id')::uuid; exception when others then uid := null; end;
      if uid is not null and not exists (select 1 from auth.users where id = uid) then uid := null; end if;
    end if;
    if uid is not null and exists (select 1 from match_players where match_id = mid and user_id = uid) then
      uid := null;   -- the same account twice: count it once
    end if;
    sc   := coalesce((pl->>'score')::int, 0);
    secs := greatest(0, least(coalesce((pl->>'seconds')::int, 0), 86400));
    won  := coalesce((pl->>'won')::boolean, false);
    insert into match_players (match_id, user_id, name, score, place, won, seconds)
    values (mid, uid, left(coalesce(pl->>'name', ''), 64), sc, greatest(0, coalesce((pl->>'place')::int, 0)), won, secs);
    if uid is not null then
      insert into player_stats as s (user_id, game, matches, wins, total_score, best_score, seconds, last_played)
      values (uid, lower(g), 1, case when won then 1 else 0 end, sc, sc, secs, now())
      on conflict (user_id, game) do update set
        matches     = s.matches + 1,
        wins        = s.wins + excluded.wins,
        total_score = s.total_score + excluded.total_score,
        best_score  = greatest(s.best_score, excluded.best_score),
        seconds     = s.seconds + excluded.seconds,
        last_played = now();
    end if;
  end loop;
  return mid;
end;
$$;

-- Top players of a game. p_stat: wins | score | best | matches | time.
create or replace function public.game_leaderboard(p_game text, p_stat text default 'wins', p_limit int default 50)
returns table (user_id uuid, username text, display_name text, avatar_url text, matches int, wins int,
               total_score bigint, best_score int, seconds bigint, last_played timestamptz)
language sql stable set search_path = public as $$
  select s.user_id, pr.username, pr.display_name, pr.avatar_url, s.matches, s.wins,
         s.total_score, s.best_score, s.seconds, s.last_played
  from player_stats s left join profiles pr on pr.id = s.user_id
  where s.game = lower(p_game)
  order by case p_stat when 'score'   then s.total_score
                       when 'best'    then s.best_score
                       when 'matches' then s.matches
                       when 'time'    then s.seconds
                       else s.wins end desc,
           s.matches desc, s.last_played desc
  limit least(greatest(coalesce(p_limit, 50), 1), 200);
$$;

-- My stats in a game, with my rank by wins.
create or replace function public.my_game_stats(p_game text)
returns table (user_id uuid, username text, display_name text, avatar_url text, matches int, wins int,
               total_score bigint, best_score int, seconds bigint, last_played timestamptz, rank int)
language sql stable set search_path = public as $$
  select s.user_id, pr.username, pr.display_name, pr.avatar_url, s.matches, s.wins,
         s.total_score, s.best_score, s.seconds, s.last_played,
         (select count(*) + 1 from player_stats o where o.game = s.game and o.wins > s.wins)::int
  from player_stats s left join profiles pr on pr.id = s.user_id
  where s.user_id = auth.uid() and s.game = lower(p_game);
$$;

-- Everyone I shared a match with in the last 60 days, most recent first.
create or replace function public.recent_players(p_limit int default 40)
returns table (user_id uuid, username text, display_name text, avatar_url text,
               game text, last_played timestamptz, matches int)
language sql stable set search_path = public as $$
  with mine as (
    select mp.match_id from match_players mp join matches m on m.id = mp.match_id
    where mp.user_id = auth.uid() and m.ended_at > now() - interval '60 days'
  )
  select o.user_id, pr.username, pr.display_name, pr.avatar_url,
         (array_agg(m.game order by m.ended_at desc))[1], max(m.ended_at), count(*)::int
  from match_players o
  join mine on mine.match_id = o.match_id
  join matches m on m.id = o.match_id
  left join profiles pr on pr.id = o.user_id
  where o.user_id is not null and o.user_id <> auth.uid()
  group by o.user_id, pr.username, pr.display_name, pr.avatar_url
  order by max(m.ended_at) desc
  limit least(greatest(coalesce(p_limit, 40), 1), 100);
$$;

revoke all on function public.report_match(jsonb)                from public, anon;
revoke all on function public.game_leaderboard(text, text, int)   from public, anon;
revoke all on function public.my_game_stats(text)                 from public, anon;
revoke all on function public.recent_players(int)                 from public, anon;
grant execute on function public.report_match(jsonb)              to authenticated;
grant execute on function public.game_leaderboard(text, text, int) to authenticated;
grant execute on function public.my_game_stats(text)              to authenticated;
grant execute on function public.recent_players(int)              to authenticated;

-- Optional: drop old match detail after a year (stats rows stay). With pg_cron:
--   select cron.schedule('purge-old-matches', '0 4 * * *',
--     $$delete from public.matches where ended_at < now() - interval '365 days'$$);
