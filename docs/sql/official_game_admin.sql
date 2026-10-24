-- OkaySpace: admins publish the official game from the launcher (Admin >
-- Official game) with their own login, so no service key is ever needed on a
-- player's or admin's machine. Run once in your Supabase project's SQL editor
-- AFTER admin.sql (it uses public.is_admin and public.admin_log_write).
-- Idempotent: safe to re-run.
--
-- Storage, bucket "releases" (public, made by scripts/publish-supabase.sh):
--   admins may list, upload, replace and delete objects whose name is under
--   official/ (the layout is in shared/OfficialGame.hpp). Nobody else gains any
--   access, and nothing outside official/ (the engine's own VERSION.txt,
--   NOTES.txt and v<ver>/ files) becomes writable. Names with an empty, '.' or
--   '..' part, a backslash or a control character are refused, so a name like
--   official/../VERSION.txt can never reach the engine's release files.
--   Public downloads (/storage/v1/object/public/releases/...) don't go through
--   these policies and stay as they are. The admin-only "list" policy is there
--   because Storage needs SELECT to list a folder, upsert a file or delete one.
-- Audit log: public.log_official_publish() writes each publish, "make live"
-- (rollback) and delete to admin_log. Admins only.

-- True for a well-formed object name under official/.
create or replace function public.official_release_name(p_name text) returns boolean
language sql immutable set search_path = public as $$
  select p_name is not null
     and char_length(p_name) between 10 and 600
     and p_name like 'official/%'
     and p_name !~ '(^|/)\.\.?(/|$)'          -- no '.' or '..' part
     and position('//' in p_name) = 0         -- no empty part
     and right(p_name, 1) <> '/'
     and position(chr(92) in p_name) = 0      -- no backslash
     and p_name !~ '[[:cntrl:]]';
$$;

do $$
begin
  if to_regclass('storage.objects') is not null then
    drop policy if exists "admins list official game"    on storage.objects;
    drop policy if exists "admins upload official game"  on storage.objects;
    drop policy if exists "admins replace official game" on storage.objects;
    drop policy if exists "admins delete official game"  on storage.objects;

    create policy "admins list official game" on storage.objects for select to authenticated
      using (bucket_id = 'releases' and public.official_release_name(name) and public.is_admin(auth.uid()));
    create policy "admins upload official game" on storage.objects for insert to authenticated
      with check (bucket_id = 'releases' and public.official_release_name(name) and public.is_admin(auth.uid()));
    -- Both sides: an admin can't move an engine file in, or an official one out.
    create policy "admins replace official game" on storage.objects for update to authenticated
      using (bucket_id = 'releases' and public.official_release_name(name) and public.is_admin(auth.uid()))
      with check (bucket_id = 'releases' and public.official_release_name(name) and public.is_admin(auth.uid()));
    create policy "admins delete official game" on storage.objects for delete to authenticated
      using (bucket_id = 'releases' and public.official_release_name(name) and public.is_admin(auth.uid()));
  end if;
end $$;

-- ---------------------------------------------------------------------------
-- Audit log: action publish | make_live | delete, written as official_<action>.
-- ---------------------------------------------------------------------------
create or replace function public.log_official_publish(p_version text, p_files int default null,
                                                       p_bytes bigint default null, p_action text default 'publish')
returns void language plpgsql security definer set search_path = public as $$
begin
  if not public.is_admin(auth.uid()) then
    raise exception 'Only admins publish the official game' using errcode = 'insufficient_privilege';
  end if;
  if p_action is null or p_action not in ('publish', 'make_live', 'delete') then
    raise exception 'Unknown official game action' using errcode = 'invalid_parameter_value';
  end if;
  if p_version is null or p_version !~ '^[0-9]{1,12}(\.[0-9]{1,12}){1,7}$' then
    raise exception 'Bad version' using errcode = 'invalid_parameter_value';
  end if;
  perform public.admin_log_write('official_' || p_action, null,
    'v' || p_version || case when p_files is not null and p_bytes is not null
                             then ' (' || p_files || ' files, ' || pg_size_pretty(p_bytes) || ')' else '' end);
end $$;
revoke execute on function public.log_official_publish(text, int, bigint, text) from public, anon;
grant execute on function public.log_official_publish(text, int, bigint, text) to authenticated;
