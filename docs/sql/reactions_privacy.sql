-- =============================================================================
-- Reactions privacy (SEC1 item 4).  Run in the Supabase dashboard: SQL Editor > New query > paste this
-- whole file > Run.  Safe to run again (idempotent).  Run it AFTER social.sql, group_chats.sql, admin.sql and
-- blocks.sql (it uses their tables and the okay_private block helper; parts whose prerequisite is missing are
-- skipped with a NOTICE, run the file again once the prerequisite is in).  Docs: docs/online/security.md "Reactions privacy".
--
-- What it fixes (found by reading social.sql; each has a check in tests/sql/test_reactions_privacy.sql):
--  1. The visibility check (can_see_reaction_target) could be called by anyone, including signed-out visitors.
--     Only signed-in users may call it now.
--  2. A reaction's text was unbounded: a 1 MB "emoji" was accepted.  Now 1 to 40 characters (the longest key the
--     app writes is "poll:<n>").
--  3. Rows could be UPDATEd by the table grants (RLS had no UPDATE policy, so nothing changed, but the grant
--     was still there).  UPDATE is revoked: a reaction is added or removed, never edited.
--  4. A person you blocked could still react to your messages in a shared server or group, and you still saw
--     their reactions (and their user id) next to your messages.  Blocks now hide reactions in both directions
--     and stop new ones on the blocker's messages.
--  5. Deleting a message left its reactions behind for ever (orphans: storage growth, and the reactor ids stayed
--     in the table).  Deleting a message now deletes its reactions.
--  6. The app read every reaction row (with reactor ids) just to count them.  reaction_counts() returns counts
--     and "did I react", and never a user id or e-mail.  (The raw table is still readable by exactly the people
--     who can see the message, as before: the current app needs the rows.  Move the app to reaction_counts()
--     and the raw SELECT can be limited to your own rows.)
-- =============================================================================

-- 2. bounded text.  NOT VALID: rows already there are not re-checked, every new row is.
do $$ begin
  if to_regclass('public.message_reactions') is null then
    raise notice 'reactions_privacy: public.message_reactions is missing (run social.sql first); nothing to do';
    return;
  end if;
  if not exists (select 1 from pg_constraint where conname = 'message_reactions_emoji_len'
                   and conrelid = 'public.message_reactions'::regclass) then
    alter table public.message_reactions
      add constraint message_reactions_emoji_len check (char_length(emoji) between 1 and 40) not valid;
  end if;
end $$;

-- 3. never edited
revoke update on public.message_reactions from anon, authenticated;
revoke all on public.message_reactions from anon;
grant select on public.message_reactions to anon;   -- RLS decides (signed-out visitors can see nothing); keeps PostgREST schema discovery working

-- 1. only signed-in users may ask
revoke execute on function public.can_see_reaction_target(text, bigint) from public, anon;
grant execute on function public.can_see_reaction_target(text, bigint) to authenticated, service_role;

-- 4. blocks (needs blocks.sql: okay_private.is_blocked_between)
do $$ begin
  if to_regprocedure('okay_private.is_blocked_between(uuid,uuid)') is null then
    raise notice 'reactions_privacy: blocks.sql has not been run; block rules for reactions skipped (run it, then this file again)';
    return;
  end if;
  -- Is the message under this reaction written by (or, in a DM, addressed to) someone who has a block with the caller?
  execute $f$
    create or replace function okay_private.reaction_target_blocked(p_scope text, p_target bigint)
      returns boolean language plpgsql stable security definer set search_path = public, pg_temp as $b$
    begin
      if p_scope = 'dm' then
        return exists (select 1 from public.messages m where m.id = p_target
                         and okay_private.is_blocked_between(auth.uid(), case when m.sender = auth.uid() then m.recipient else m.sender end));
      elsif p_scope = 'channel' then
        return exists (select 1 from public.channel_messages c where c.id = p_target
                         and c.author <> auth.uid() and okay_private.is_blocked_between(auth.uid(), c.author));
      elsif p_scope = 'group' and to_regclass('public.group_messages') is not null then
        return exists (select 1 from public.group_messages g where g.id = p_target
                         and g.sender <> auth.uid() and okay_private.is_blocked_between(auth.uid(), g.sender));
      end if;
      return false;
    end $b$ $f$;
  revoke execute on function okay_private.reaction_target_blocked(text, bigint) from public;
  grant execute on function okay_private.reaction_target_blocked(text, bigint) to authenticated, service_role;

  drop policy if exists "okay rules: reaction insert" on public.message_reactions;
  create policy "okay rules: reaction insert" on public.message_reactions as restrictive for insert
    with check (not okay_private.reaction_target_blocked(scope, target_id));
  drop policy if exists "okay rules: reaction select" on public.message_reactions;
  create policy "okay rules: reaction select" on public.message_reactions as restrictive for select
    using (reactor = auth.uid() or not okay_private.is_blocked_between(reactor, auth.uid()));
end $$;

-- 5. a deleted message takes its reactions with it.  SECURITY DEFINER: the person deleting the message did not
-- make the other people's reactions, so RLS would not let them delete those rows.
create or replace function public.delete_reactions_of_message() returns trigger
  language plpgsql security definer set search_path = public, pg_temp as $$
begin
  delete from public.message_reactions where scope = tg_argv[0] and target_id = old.id;
  return old;
end $$;
revoke execute on function public.delete_reactions_of_message() from public, anon, authenticated;
do $$
declare r record;
begin
  if to_regclass('public.message_reactions') is null then return; end if;
  for r in select * from (values ('messages', 'dm'), ('channel_messages', 'channel'), ('group_messages', 'group')) v(tbl, sc) loop
    if to_regclass('public.' || r.tbl) is not null then
      execute format('drop trigger if exists okay_reactions_cleanup on public.%I', r.tbl);
      execute format('create trigger okay_reactions_cleanup after delete on public.%I for each row execute function public.delete_reactions_of_message(%L)', r.tbl, r.sc);
    end if;
  end loop;
  -- Reactions already orphaned by older deletes.
  delete from public.message_reactions x where
    (x.scope = 'dm' and not exists (select 1 from public.messages m where m.id = x.target_id))
    or (x.scope = 'channel' and not exists (select 1 from public.channel_messages c where c.id = x.target_id))
    or (x.scope = 'group' and to_regclass('public.group_messages') is not null
        and not exists (select 1 from public.group_messages g where g.id = x.target_id));
end $$;

-- 6. counts without who.  SECURITY INVOKER: the table's own RLS (can the caller see the message? blocks?) applies.
create or replace function public.reaction_counts(p_scope text, p_targets bigint[])
  returns table (target_id bigint, emoji text, n bigint, mine boolean)
  language plpgsql stable security invoker set search_path = public, pg_temp as $$
begin
  if p_scope not in ('dm', 'channel', 'group') then raise exception 'bad scope' using errcode = '22023'; end if;
  if coalesce(array_length(p_targets, 1), 0) > 500 then raise exception 'too many messages (500 at most)' using errcode = '54000'; end if;
  return query
    select r.target_id, r.emoji, count(*)::bigint, bool_or(r.reactor = auth.uid())
      from public.message_reactions r
     where r.scope = p_scope and r.target_id = any(p_targets)
     group by r.target_id, r.emoji;
end $$;
revoke execute on function public.reaction_counts(text, bigint[]) from public, anon;
grant execute on function public.reaction_counts(text, bigint[]) to authenticated, service_role;
