-- OkaySpace chat attachments (Supabase Storage). Run once in your Supabase
-- project's SQL editor (after social.sql). Idempotent — re-running is safe.
--
-- Creates a public Storage bucket "chat" for files and voice notes sent in
-- chats. A signed-in player may only write into their own folder
-- (chat/<their auth uuid>/...), which okay::Messages::UploadChatFile uses.
-- Objects are readable by URL (names carry a random prefix, so links aren't
-- guessable) — the same model as Messenger/WhatsApp media links.
do $$
begin
  if to_regclass('storage.buckets') is not null then
    insert into storage.buckets (id, name, public, file_size_limit)
      values ('chat', 'chat', true, 26214400)          -- 25 MB per file
      on conflict (id) do update set public = true, file_size_limit = 26214400;

    drop policy if exists "chat files readable"   on storage.objects;
    drop policy if exists "upload own chat file"  on storage.objects;
    drop policy if exists "replace own chat file" on storage.objects;
    drop policy if exists "delete own chat file"  on storage.objects;

    create policy "chat files readable" on storage.objects for select
      using (bucket_id = 'chat');
    create policy "upload own chat file" on storage.objects for insert
      with check (bucket_id = 'chat' and auth.uid()::text = (storage.foldername(name))[1]);
    create policy "replace own chat file" on storage.objects for update
      using (bucket_id = 'chat' and auth.uid()::text = (storage.foldername(name))[1]);
    create policy "delete own chat file" on storage.objects for delete
      using (bucket_id = 'chat' and auth.uid()::text = (storage.foldername(name))[1]);
  end if;
end $$;
