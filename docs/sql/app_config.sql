-- Site-wide settings the launcher reads at startup (e.g. the default voice
-- server for calls). Anyone can read. Run after admin.sql. Idempotent.
--
-- Two kinds of keys:
--   free keys (voice_relay, game_relay, ...): config.write holders (admins)
--     change them through the API; each change is rate limited and logged.
--   controlled keys, listed in app_config_keys (maintenance mode, the
--     announcement banner, feature switches, minimum versions): only the
--     okay-admin server function changes them, through admin_set_config,
--     after the admin's second factor where the permission is 'danger'.
--     Each has its own permission and a pattern its value must match.
create table if not exists public.app_config (
    key        text primary key,
    value      text not null default '',
    updated_at timestamptz not null default now()
);
alter table public.app_config enable row level security;

drop policy if exists "app_config read" on public.app_config;
create policy "app_config read" on public.app_config
    for select to anon, authenticated using (true);

-- Controlled keys: the permission each needs and the pattern its value must match.
create table if not exists public.app_config_keys (
    key         text primary key,
    permission  text not null references public.admin_permissions,
    pattern     text not null,               -- the whole value must match this regular expression
    description text not null check (char_length(description) <= 200)
);
alter table public.app_config_keys enable row level security;
revoke insert, update, delete, truncate on public.app_config_keys from public, anon, authenticated;
drop policy if exists "app_config_keys read" on public.app_config_keys;
create policy "app_config_keys read" on public.app_config_keys for select to anon, authenticated using (true);

with keys (key, permission, pattern, description, initial) as (values
    ('maintenance',          'game.maintenance', '^(on|off)$',                    'Maintenance mode: the launcher shows the message and keeps players out of online play', 'off'),
    ('maintenance.message',  'game.maintenance', '^[^\r\n]*$',            'What players see during maintenance', ''),
    ('announcement',         'game.announce',    '^[^\r\n]*$',            'The launcher-wide banner; empty for none', ''),
    ('announcement.level',   'game.announce',    '^(info|warning|critical)$',     'How the banner looks', 'info'),
    ('announcement.until',   'game.announce',    '^(|\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2})?Z)$', 'When the banner goes away (UTC); empty for never', ''),
    ('min_version.launcher', 'game.min_version', '^(|\d{1,4}\.\d{1,4}\.\d{1,4})$', 'Oldest launcher allowed online; empty for any', ''),
    ('min_version.game',     'game.min_version', '^(|\d{1,4}\.\d{1,4}\.\d{1,4})$', 'Oldest game allowed online; empty for any', ''),
    ('feature.chat',         'game.features',    '^(on|off)$',                    'Chat', 'on'),
    ('feature.uploads',      'game.features',    '^(on|off)$',                    'Uploads (marketplace, chat pictures)', 'on'),
    ('feature.matchmaking',  'game.features',    '^(on|off)$',                    'Matchmaking', 'on'),
    ('feature.signups',      'game.features',    '^(on|off)$',                    'New accounts', 'on')
), added as (
    insert into public.app_config_keys (key, permission, pattern, description)
    select key, permission, pattern, description from keys
    on conflict (key) do update set permission = excluded.permission, pattern = excluded.pattern, description = excluded.description
    returning key
)
insert into public.app_config (key, value) select k.key, k.initial from keys k join added a on a.key = k.key
    on conflict (key) do nothing;

create or replace function public.app_config_is_controlled(p_key text) returns boolean
language sql stable security definer set search_path = public as $$
    select exists (select 1 from public.app_config_keys where key = p_key);
$$;

-- Free keys only, config.write holders only.
drop policy if exists "app_config admin insert" on public.app_config;
create policy "app_config admin insert" on public.app_config
    for insert to authenticated
    with check (public.admin_can(auth.uid(), 'config.write') and not public.app_config_is_controlled(key));

drop policy if exists "app_config admin update" on public.app_config;
create policy "app_config admin update" on public.app_config
    for update to authenticated
    using (public.admin_can(auth.uid(), 'config.write') and not public.app_config_is_controlled(key))
    with check (public.admin_can(auth.uid(), 'config.write') and not public.app_config_is_controlled(key));

drop policy if exists "app_config admin delete" on public.app_config;
create policy "app_config admin delete" on public.app_config
    for delete to authenticated
    using (public.admin_can(auth.uid(), 'config.write') and not public.app_config_is_controlled(key));

drop trigger if exists app_config_log on public.app_config;
create trigger app_config_log after insert or update or delete on public.app_config
    for each row execute function public.admin_row_log('config.write');

-- The okay-admin function (service role) sets a controlled key for an admin it
-- has signed in, and for a 'danger' permission after their second factor.
-- Checks here too: the key is controlled, the value matches its pattern, a
-- 'danger' change carries the key name typed back as p_confirm, and
-- admin_gate_as (permission, rate limit, refusals logged). Logs before / after.
-- Answers 'ok', 'confirm_needed', 'bad_value', 'unknown_key' or admin_gate's answer.
create or replace function public.admin_set_config(p_actor uuid, p_key text, p_value text, p_confirm text,
    p_source text, p_request_id uuid, p_ip inet, p_user_agent text) returns text
language plpgsql security definer set search_path = public as $$
declare k record; g text; old_value text;
begin
    select ck.permission, ck.pattern, p.kind into k
      from public.app_config_keys ck join public.admin_permissions p on p.permission = ck.permission
     where ck.key = p_key;
    if not found then return 'unknown_key'; end if;
    g := public.admin_gate_as(p_actor, k.permission, 'config_set', null, p_source, p_request_id, p_ip, p_user_agent);
    if g <> 'ok' then return g; end if;
    if p_value is null or char_length(p_value) > 300 or p_value !~ k.pattern then return 'bad_value'; end if;   -- every controlled value: 300 characters at most
    if k.kind = 'danger' and p_confirm is distinct from p_key then return 'confirm_needed'; end if;
    select value into old_value from public.app_config where key = p_key for update;
    insert into public.app_config (key, value) values (p_key, p_value)
        on conflict (key) do update set value = excluded.value;
    perform public.admin_log_record(p_actor, 'config_set', null, p_key,
        jsonb_build_object(p_key, old_value), jsonb_build_object(p_key, p_value),
        p_source, p_request_id, k.permission, 'ok', p_ip, p_user_agent);
    return 'ok';
end $$;
revoke execute on function public.admin_set_config(uuid, text, text, text, text, uuid, inet, text) from public, anon, authenticated;
grant execute on function public.admin_set_config(uuid, text, text, text, text, uuid, inet, text) to service_role;

-- Keep updated_at fresh.
create or replace function public.app_config_touch() returns trigger
language plpgsql as $$ begin new.updated_at := now(); return new; end $$;
drop trigger if exists app_config_touch on public.app_config;
create trigger app_config_touch before update on public.app_config
    for each row execute function public.app_config_touch();

-- Placeholder row: set the real value from the launcher's Admin page
-- (Admin > Settings > Voice server) once your voice server is running.
insert into public.app_config (key, value) values ('voice_relay', '')
    on conflict (key) do nothing;
