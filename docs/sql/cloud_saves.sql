-- Cloud saves: per-account key/value rows (AccountService::CloudSave / CloudLoad / CloudHas / CloudDelete / CloudList,
-- sent through OkayBackend's Database: okay/Backend/Database.hpp). One row per (player, key); each player reads and writes
-- only their own rows.
-- NOT YET CHECKED AGAINST THE LIVE PROJECT. These definitions were copied unchanged (OkayBackend round 3, 2026-11-26) from
-- docs/online/supabase-backend.md, the only place they were written down; nobody compared them with what the production
-- Supabase project has. Not safe to run twice as written: its policy statements fail when the policy already exists.

-- Per-account cloud saves: progress that follows the player across devices.
create table if not exists public.cloud_saves (
  user_id    uuid        not null default auth.uid() references auth.users on delete cascade,
  key        text        not null,
  data       text        not null,
  updated_at timestamptz not null default now(),
  primary key (user_id, key)
);
alter table public.cloud_saves enable row level security;

create policy "own cloud saves - read"   on public.cloud_saves for select using (auth.uid() = user_id);
create policy "own cloud saves - write"  on public.cloud_saves for insert with check (auth.uid() = user_id);
create policy "own cloud saves - update" on public.cloud_saves for update using (auth.uid() = user_id) with check (auth.uid() = user_id);
create policy "own cloud saves - delete" on public.cloud_saves for delete using (auth.uid() = user_id);
