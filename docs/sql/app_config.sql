-- Site-wide settings the launcher reads at startup (e.g. the default voice
-- server for calls). Anyone can read; only admins (my_role_rank() >= 3, see
-- admin.sql) can change them. Run after admin.sql.
create table if not exists public.app_config (
    key        text primary key,
    value      text not null default '',
    updated_at timestamptz not null default now()
);
alter table public.app_config enable row level security;

drop policy if exists "app_config read" on public.app_config;
create policy "app_config read" on public.app_config
    for select to anon, authenticated using (true);

drop policy if exists "app_config admin insert" on public.app_config;
create policy "app_config admin insert" on public.app_config
    for insert to authenticated with check (public.my_role_rank() >= 3);

drop policy if exists "app_config admin update" on public.app_config;
create policy "app_config admin update" on public.app_config
    for update to authenticated using (public.my_role_rank() >= 3) with check (public.my_role_rank() >= 3);

drop policy if exists "app_config admin delete" on public.app_config;
create policy "app_config admin delete" on public.app_config
    for delete to authenticated using (public.my_role_rank() >= 3);

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
