-- OkaySpace public names: the server, not the client, fills every stored
-- "shown name" column, and an email address is never stored as one (Supabase).
-- Run once in the SQL editor AFTER social.sql and after any of these you use:
-- profile_extras.sql, forum_sections.sql, group_chats.sql, admin.sql,
-- blocks.sql. Tables that do not exist yet are skipped, so re-run this file
-- after adding one of them later. Idempotent: safe to re-run.
--
-- Why: several tables keep a copy of the author's name next to the author's
-- user id (forum_posts.author_name and so on) so the launcher can show it
-- without a profiles lookup per row. The CLIENT used to send that text, and an
-- older launcher sent the sign-in EMAIL, so other players saw an email address
-- as a forum author. The launcher now cleans names when it shows them
-- (okay::account::PublicName); this file makes the database itself safe.
--
-- What it adds
--   * public.public_name_for(uid): the one definition of a player's public
--     name: display_name, else username, from public.profiles. Anything with
--     an '@' is cut to the part before it; an empty or missing name is 'Player'.
--   * public.okay_set_public_name(): one BEFORE INSERT OR UPDATE trigger
--     function, attached to each table below with two arguments (the user id
--     column, the name column). Whatever the client sends in the name column is
--     replaced with public_name_for(id). A null id gives a null name (for
--     example admin_log rows written from the SQL editor).
--   * profiles.username and profiles.display_name (both public) are cut at the
--     first '@' by a trigger, and old profiles are cleaned once.
--   * A one-off clean-up of existing rows whose name holds an '@' or is null.
--
-- Covered (id column -> name column):
--   activity actor -> actor_name, forum_threads / forum_posts /
--   workshop_items / channel_messages author -> author_name, workshop_reviews
--   reviewer -> reviewer_name, group_messages sender -> sender_name,
--   forum_section_members / forum_section_bans user_id -> user_name,
--   admin_log actor -> actor_name and target -> target_name, blocks blocked ->
--   blocked_name.
--
-- Not changed: the names are still a snapshot taken when the row is written
-- (a later display-name change does not rewrite old posts), as before.
-- Tested on a local Postgres by tests/scripts/test_public_names_sql.sh.

-- ---------------------------------------------------------------------------
-- public_name_for
-- ---------------------------------------------------------------------------
create or replace function public.public_name_for(uid uuid) returns text
language plpgsql stable security definer set search_path = public as $$
declare
  n text;
begin
  select coalesce(nullif(btrim(p.display_name), ''), nullif(btrim(p.username), ''))
    into n from public.profiles p where p.id = uid;
  if n is not null and position('@' in n) > 0 then
    n := nullif(btrim(split_part(n, '@', 1)), '');
  end if;
  return coalesce(n, 'Player');
end $$;
grant execute on function public.public_name_for(uuid) to anon, authenticated, service_role;

-- ---------------------------------------------------------------------------
-- profiles: the username and display_name are public too (profiles is
-- readable by everyone), so neither may hold an email. A BEFORE trigger cuts
-- both at the first '@' (an empty username left over becomes
-- player_<first 8 of the id>); old rows are cleaned once below, adding
-- _<first 4 of the id> when the cut username is already taken.
-- ---------------------------------------------------------------------------
create or replace function public.okay_profile_no_email() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if position('@' in coalesce(new.username, '')) > 0 then
    new.username := coalesce(nullif(btrim(split_part(new.username, '@', 1)), ''), 'player_' || left(new.id::text, 8));
  end if;
  if position('@' in coalesce(new.display_name, '')) > 0 then
    new.display_name := nullif(btrim(split_part(new.display_name, '@', 1)), '');
  end if;
  return new;
end $$;
revoke execute on function public.okay_profile_no_email() from public, anon, authenticated;
drop trigger if exists zz_profile_no_email on public.profiles;
create trigger zz_profile_no_email before insert or update of username, display_name on public.profiles
  for each row execute function public.okay_profile_no_email();
do $$
declare
  p record;
  base text;
begin
  for p in select id, username from public.profiles where position('@' in username) > 0 loop
    base := coalesce(nullif(btrim(split_part(p.username, '@', 1)), ''), 'player_' || left(p.id::text, 8));
    if exists (select 1 from public.profiles where username = base and id <> p.id) then
      base := base || '_' || left(p.id::text, 4);
    end if;
    update public.profiles set username = base where id = p.id;
  end loop;
  update public.profiles set display_name = display_name where position('@' in display_name) > 0;  -- the trigger cuts it
end $$;

-- ---------------------------------------------------------------------------
-- The trigger function. TG_ARGV[0] = user id column, TG_ARGV[1] = name column.
-- ---------------------------------------------------------------------------
create or replace function public.okay_set_public_name() returns trigger
language plpgsql security definer set search_path = public as $$
declare
  uid uuid := (to_jsonb(new) ->> tg_argv[0])::uuid;
begin
  new := jsonb_populate_record(new, jsonb_build_object(tg_argv[1],
           case when uid is null then null else public.public_name_for(uid) end));
  return new;
end $$;
revoke execute on function public.okay_set_public_name() from public, anon, authenticated;

-- ---------------------------------------------------------------------------
-- Attach to every covered table that exists, then clean the existing rows.
-- The trigger is named zz_public_name_<name column> so it runs after the
-- table's other BEFORE triggers (Postgres runs them in name order), for
-- example after forum_threads_mod_only puts the old author back.
-- ---------------------------------------------------------------------------
do $$
declare
  t record;
  trg text;
begin
  for t in select * from (values
      ('activity',              'actor',    'actor_name'),   -- dup-allow: tests/sql/test_public_names.sql keeps its own copy to check this list
      ('forum_threads',         'author',   'author_name'),
      ('forum_posts',           'author',   'author_name'),
      ('workshop_items',        'author',   'author_name'),
      ('workshop_reviews',      'reviewer', 'reviewer_name'),
      ('channel_messages',      'author',   'author_name'),
      ('group_messages',        'sender',   'sender_name'),
      ('forum_section_members', 'user_id',  'user_name'),
      ('forum_section_bans',    'user_id',  'user_name'),
      ('admin_log',             'actor',    'actor_name'),
      ('admin_log',             'target',   'target_name'),
      ('blocks',                'blocked',  'blocked_name')
    ) as v(tbl, id_col, name_col)
  loop
    if to_regclass('public.' || t.tbl) is null then continue; end if;
    trg := 'zz_public_name_' || t.name_col;
    execute format('drop trigger if exists %I on public.%I', trg, t.tbl);
    execute format('create trigger %I before insert or update of %I, %I on public.%I '
                   'for each row execute function public.okay_set_public_name(%L, %L)',
                   trg, t.id_col, t.name_col, t.tbl, t.id_col, t.name_col);
    -- One-off clean-up (fires the trigger too, which sets the same value).
    -- admin_log is append-only (admin.sql); this rename of old names is the one
    -- change it lets through, for this transaction only.
    if t.tbl = 'admin_log' then perform set_config('okay.admin_log_maintenance', 'on', true); end if;
    execute format('update public.%I set %I = case when %I is null then null '
                   'else public.public_name_for(%I) end '
                   'where (%I is null and %I is not null) or position(''@'' in %I) > 0',
                   t.tbl, t.name_col, t.id_col, t.id_col, t.name_col, t.id_col, t.name_col);
    perform set_config('okay.admin_log_maintenance', 'off', true);
  end loop;
end $$;
