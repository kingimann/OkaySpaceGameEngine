-- LOCAL test of docs/sql/public_names.sql. Each check prints "PASS <name>" or
-- "FAIL <name>: <why>" as a WARNING. Made-up people only (example.test).
-- Run twice by tests/scripts/test_public_names_sql.sh:
--   -v seed=1  BEFORE public_names.sql: users, profiles and old rows with
--              emails / null names in every covered table (as an old launcher
--              left them);
--   -v seed=0  AFTER public_names.sql (loaded twice): the checks.
-- alice: username 'alice', no display name -> 'alice'
-- bob:   display_name 'Bobby'               -> 'Bobby'
-- carol: legacy username 'carol@example.test' -> 'carol'
-- dave:  no profile row                     -> 'Player'
-- erin:  display_name 'erin@example.test'   -> 'erin'
\set QUIET on
set client_min_messages = warning;
reset role;

\set A '00000000-0000-0000-0000-0000000000a1'
\set B '00000000-0000-0000-0000-0000000000b2'
\set C '00000000-0000-0000-0000-0000000000c3'
\set D '00000000-0000-0000-0000-0000000000d4'
\set E '00000000-0000-0000-0000-0000000000e5'

\if :seed
insert into auth.users (id, email, raw_user_meta_data) values
  (:'A', 'alice@example.test', '{"username":"alice"}'), (:'B', 'bob@example.test', '{"username":"bob"}'),
  (:'C', 'carol@example.test', '{}'), (:'D', 'dave@example.test', '{}'), (:'E', 'erin@example.test', '{"username":"erin_x"}');
update public.profiles set display_name = 'Bobby' where id = :'B';
update public.profiles set username = 'carol@example.test' where id = :'C';   -- what an old signup could leave
update public.profiles set display_name = 'erin@example.test' where id = :'E';
delete from public.profiles where id = :'D';
insert into auth.users (id, email) values ('00000000-0000-0000-0000-0000000000e6', 'other@example.test');
update public.profiles set username = 'alice@example.test' where id = '00000000-0000-0000-0000-0000000000e6';

-- A stand-in for Supabase Storage, which admin.sql's workshop_items_guard reads:
-- bob uploaded one file to his own folder.
create schema if not exists storage;
create table if not exists storage.objects (bucket_id text, name text);
grant usage on schema storage to authenticated; grant select on storage.objects to authenticated;
insert into storage.objects values ('workshop', '00000000-0000-0000-0000-0000000000b2/b2.zip');

-- Parents.
insert into public.forum_threads (id, author, author_name, title) overriding system value
  values (9001, :'A', 'alice@example.test', 'old thread'), (9002, :'B', null, 'b thread'),
         (9003, :'C', 'carol@example.test', 'c thread'), (9004, :'D', 'dave@example.test', 'd thread');
insert into public.workshop_items (id, author, author_name, title) overriding system value
  values (9001, :'A', 'alice@example.test', 'old item'), (9002, :'B', null, 'b item'),
         (9003, :'C', 'carol@example.test', 'c item'), (9004, :'D', 'dave@example.test', 'd item');
insert into public.servers (id, name, owner) overriding system value values (9001, 'srv', :'A');
insert into public.channels (id, server_id, name) overriding system value values (9001, 9001, 'general');
insert into public.group_convos (id, owner) overriding system value values (9001, :'A');
insert into public.forum_sections (id, slug, name, owner) overriding system value values (9001, 'oldsec', 'Old section', :'B');

-- Old rows with an email or no name, in every covered table.
insert into public.activity (actor, actor_name, kind) values
  (:'A', 'alice@example.test', 'online'), (:'B', null, 'online'), (:'C', 'carol@example.test', 'online'), (:'D', 'dave@example.test', 'online');
insert into public.forum_posts (thread_id, author, author_name, body) values
  (9001, :'A', 'alice@example.test', 'x'), (9001, :'B', null, 'x'), (9001, :'C', 'carol@example.test', 'x'), (9001, :'D', 'dave@example.test', 'x');
insert into public.workshop_reviews (item_id, reviewer, reviewer_name, rating) values
  (9001, :'A', 'alice@example.test', 5), (9001, :'B', null, 4), (9001, :'C', 'carol@example.test', 3), (9001, :'D', 'dave@example.test', 2);
insert into public.channel_messages (channel_id, author, author_name, body) values
  (9001, :'A', 'alice@example.test', 'x'), (9001, :'B', null, 'x'), (9001, :'C', 'carol@example.test', 'x'), (9001, :'D', 'dave@example.test', 'x');
insert into public.group_messages (convo_id, sender, sender_name, body) values
  (9001, :'A', 'alice@example.test', 'x'), (9001, :'B', null, 'x'), (9001, :'C', 'carol@example.test', 'x'), (9001, :'D', 'dave@example.test', 'x');
insert into public.forum_section_members (section_id, user_id, user_name) values
  (9001, :'A', 'alice@example.test'), (9001, :'C', 'carol@example.test'), (9001, :'D', 'dave@example.test')
  on conflict do nothing;
update public.forum_section_members set user_name = null where user_id = :'B';   -- the auto-added owner row
insert into public.forum_section_bans (section_id, user_id, user_name) values
  (9001, :'A', 'alice@example.test'), (9001, :'B', null), (9001, :'C', 'carol@example.test'), (9001, :'D', 'dave@example.test');
insert into public.admin_log (actor, actor_name, action, target, target_name) values
  (:'A', 'alice@example.test', 'note', :'C', 'carol@example.test'),
  (:'B', null, 'note', :'D', 'dave@example.test'),
  (null, null, 'note', null, null);                         -- written from the SQL editor: stays nameless
insert into public.blocks (blocker, blocked, blocked_name) values
  (:'B', :'A', 'alice@example.test'), (:'A', :'B', null), (:'A', :'C', 'carol@example.test'), (:'A', :'D', 'dave@example.test');
\else

\ir test_helpers.sql

-- The covered tables, written out again on purpose so the test does not trust the file it checks.
create table t.cov (tbl text, id_col text, name_col text);
insert into t.cov values
  ('activity', 'actor', 'actor_name'), ('forum_threads', 'author', 'author_name'),  -- dup-allow: the test keeps its own copy of public_names.sql's table list
  ('forum_posts', 'author', 'author_name'), ('workshop_items', 'author', 'author_name'),
  ('workshop_reviews', 'reviewer', 'reviewer_name'), ('channel_messages', 'author', 'author_name'),
  ('group_messages', 'sender', 'sender_name'), ('forum_section_members', 'user_id', 'user_name'),
  ('forum_section_bans', 'user_id', 'user_name'), ('admin_log', 'actor', 'actor_name'),
  ('admin_log', 'target', 'target_name'), ('blocks', 'blocked', 'blocked_name');
-- Every name column in public that sits next to a user id column must be covered.
create table t.expected_people (who text, name text);
insert into t.expected_people values
  ('00000000-0000-0000-0000-0000000000a1', 'alice'), ('00000000-0000-0000-0000-0000000000b2', 'Bobby'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol'), ('00000000-0000-0000-0000-0000000000d4', 'Player');

-- ======================= profiles never hold an email =========================
select t.ok('profiles: an old email username was cleaned', t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000c3'$$) = 'carol');
select t.ok('profiles: an old email display_name was cleaned', t.val($$select display_name from public.profiles where id = '00000000-0000-0000-0000-0000000000e5'$$) = 'erin');
select t.ok('profiles: a taken cut username gets an id suffix', t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000e6'$$) = 'alice_0000');
select t.as_user(:'A'); set role authenticated;
select t.ok('profiles: a client update to an email username is cut',
  t.try($$update public.profiles set username = 'alice2@example.test' where id = auth.uid()$$) = 'ok'
  and t.val($$select username from public.profiles where id = auth.uid()$$) = 'alice2');
select t.ok('profiles: a client update to an email display_name is cut',
  t.try($$update public.profiles set display_name = 'Al@example.test' where id = auth.uid()$$) = 'ok'
  and t.val($$select display_name from public.profiles where id = auth.uid()$$) = 'Al');
update public.profiles set username = 'alice', display_name = null where id = auth.uid();
reset role;
-- public_name_for must also be safe on rows written before the trigger existed.
alter table public.profiles disable trigger zz_profile_no_email;
update public.profiles set username = 'carol@example.test' where id = :'C';

-- ======================= public_name_for =====================================
select t.ok('name: username when there is no display name', public.public_name_for(:'A') = 'alice', public.public_name_for(:'A'));
select t.ok('name: display_name is preferred', public.public_name_for(:'B') = 'Bobby', public.public_name_for(:'B'));
select t.ok('name: an email username is cut at the @', public.public_name_for(:'C') = 'carol', public.public_name_for(:'C'));
select t.ok('name: missing profile gives Player', public.public_name_for(:'D') = 'Player', public.public_name_for(:'D'));
update public.profiles set display_name = 'erin@example.test' where id = :'E';
select t.ok('name: an email display_name is cut at the @', public.public_name_for(:'E') = 'erin', public.public_name_for(:'E'));
select t.ok('name: null id gives Player', public.public_name_for(null) = 'Player');
update public.profiles set display_name = '   ' where id = :'E';
select t.ok('name: a blank display_name falls back to username', public.public_name_for(:'E') = 'erin_x', public.public_name_for(:'E'));
update public.profiles set display_name = '@example.test' where id = :'E';
select t.ok('name: nothing before the @ gives Player', public.public_name_for(:'E') = 'Player', public.public_name_for(:'E'));
update public.profiles set display_name = 'erin@example.test' where id = :'E';
update public.profiles set username = 'carol' where id = :'C';
update public.profiles set display_name = 'erin' where id = :'E';
alter table public.profiles enable trigger zz_profile_no_email;
select t.ok('name: function is SECURITY DEFINER with search_path', exists (select 1 from pg_proc
  where oid = 'public.public_name_for(uuid)'::regprocedure and prosecdef and proconfig @> array['search_path=public']));
select t.as_user(:'C'); set role authenticated;
select t.ok('name: callable by a signed-in user', public.public_name_for(:'A') = 'alice');
reset role;

-- ======================= signup default (social.sql) ==========================
insert into auth.users (id, email, raw_user_meta_data) values
  ('00000000-0000-0000-0000-0000000000f6', 'frank@example.test', '{"username":"frank@example.test"}'),
  ('00000000-0000-0000-0000-0000000000f7', 'gina@example.test', '{}'),
  ('00000000-0000-0000-0000-0000000000f8', '@example.test', '{}');
select t.ok('signup: an email in the username metadata is cut', t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000f6'$$) = 'frank',
  t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000f6'$$));
select t.ok('signup: no metadata gives the email local-part', t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000f7'$$) = 'gina');
select t.ok('signup: an empty local-part still gives a name without @', t.val($$select username from public.profiles where id = '00000000-0000-0000-0000-0000000000f8'$$) = 'player_00000000');

-- ======================= triggers on every covered table =====================
select t.ok('cover: trigger on ' || c.tbl || '.' || c.name_col, exists (select 1 from pg_trigger g
  where g.tgrelid = ('public.' || c.tbl)::regclass and g.tgname = 'zz_public_name_' || c.name_col and not g.tgisinternal
    and g.tgfoid = 'public.okay_set_public_name()'::regprocedure))
  from t.cov c;
select t.ok('cover: exactly one name trigger per covered column, nothing extra',
  (select count(*) from pg_trigger where tgfoid = 'public.okay_set_public_name()'::regprocedure) = (select count(*) from t.cov),
  (select count(*) from pg_trigger where tgfoid = 'public.okay_set_public_name()'::regprocedure)::text);
-- Any other *_name column next to a uuid id column in public is a person name we missed.
select t.ok('cover: no uncovered <x>_name beside a <x> uuid column', not exists (
  select 1 from information_schema.columns n join information_schema.columns i
    on i.table_schema = n.table_schema and i.table_name = n.table_name and i.data_type = 'uuid'
   and (n.column_name = i.column_name || '_name' or (i.column_name = 'user_id' and n.column_name = 'user_name'))
  where n.table_schema = 'public' and n.data_type = 'text'
    and not exists (select 1 from t.cov c where c.tbl = n.table_name and c.name_col = n.column_name)),
  (select string_agg(n.table_name || '.' || n.column_name, ', ') from information_schema.columns n join information_schema.columns i
    on i.table_schema = n.table_schema and i.table_name = n.table_name and i.data_type = 'uuid'
   and (n.column_name = i.column_name || '_name' or (i.column_name = 'user_id' and n.column_name = 'user_name'))
  where n.table_schema = 'public' and n.data_type = 'text'
    and not exists (select 1 from t.cov c where c.tbl = n.table_name and c.name_col = n.column_name)));

-- ======================= the clean-up of old rows ============================
select t.ok('cleanup: ' || c.tbl || '.' || c.name_col || ' for ' || e.name,
  t.val(format('select string_agg(distinct coalesce(%I, ''<null>''), '','') from public.%I where %I = %L',
               c.name_col, c.tbl, c.id_col, e.who)) in (e.name, '<null>'),
  t.val(format('select string_agg(distinct coalesce(%I, ''<null>''), '','') from public.%I where %I = %L',
               c.name_col, c.tbl, c.id_col, e.who)))
  from t.cov c cross join t.expected_people e;
-- ...and at least one row per covered column was really rewritten from an email.
select t.ok('cleanup: ' || c.tbl || '.' || c.name_col || ' has alice''s cleaned row',
  t.val(format('select count(*) from public.%I where %I = %L and %I = ''alice''', c.tbl, c.id_col,
               '00000000-0000-0000-0000-0000000000a1', c.name_col))::int > 0
  or (c.tbl, c.name_col) in (('admin_log', 'target_name'), ('blocks', 'blocked_name')))
  from t.cov c;
select t.ok('cleanup: admin_log target carol cleaned', t.val($$select target_name from public.admin_log where target = '00000000-0000-0000-0000-0000000000c3'$$) = 'carol');
select t.ok('cleanup: blocks blocked alice cleaned', t.val($$select blocked_name from public.blocks where blocked = '00000000-0000-0000-0000-0000000000a1'$$) = 'alice');
select t.ok('cleanup: a null-name row of bob was filled', t.val($$select author_name from public.forum_posts where author = '00000000-0000-0000-0000-0000000000b2'$$) = 'Bobby');
select t.ok('cleanup: SQL-editor admin_log row stays nameless',
  t.val($$select count(*) from public.admin_log where actor is null and actor_name is null and target_name is null$$) = '1');

-- ======================= a client cannot choose the name =====================
select t.as_user(:'A'); set role authenticated;
select t.ok('client: insert thread with an email name works',
  t.try($$insert into public.forum_threads (id, author_name, title) overriding system value values (9101, 'alice@example.test', 'hi')$$) = 'ok');
select t.ok('client: an email author_name is stored as the username',
  t.val($$select author_name from public.forum_threads where id = 9101$$) = 'alice', t.val($$select author_name from public.forum_threads where id = 9101$$));
select t.ok('client: insert post with a fake name works',
  t.try($$insert into public.forum_posts (thread_id, author_name, body) values (9101, 'Admin', 'hello')$$) = 'ok');
select t.ok('client: a fake author_name is replaced by the real public name',
  t.val($$select author_name from public.forum_posts where body = 'hello'$$) = 'alice', t.val($$select author_name from public.forum_posts where body = 'hello'$$));
select t.ok('client: activity insert with an email',
  t.try($$insert into public.activity (actor_name, kind) values ('alice@example.test', 'playing')$$) = 'ok');
select t.ok('client: activity actor_name is the public name',
  t.val($$select actor_name from public.activity where kind = 'playing'$$) = 'alice');
reset role;
select t.as_user(:'B'); set role authenticated;
select t.ok('client: bob publishes an item under a fake name',
  t.try($$insert into public.workshop_items (id, author_name, title, storage_path) overriding system value values (9102, 'TotallyNotBob', 'b2',
         'https://x.example.test/storage/v1/object/public/workshop/00000000-0000-0000-0000-0000000000b2/b2.zip')$$) = 'ok',
  t.try($$insert into public.workshop_items (id, author_name, title, storage_path) overriding system value values (9102, 'TotallyNotBob', 'b2',
         'https://x.example.test/storage/v1/object/public/workshop/00000000-0000-0000-0000-0000000000b2/b2.zip')$$));
select t.ok('client: display_name is used, not the fake name',
  t.val($$select author_name from public.workshop_items where id = 9102$$) = 'Bobby', t.val($$select author_name from public.workshop_items where id = 9102$$));
select t.ok('client: bob edits his own item name column',
  t.try($$update public.workshop_items set author_name = 'bob@example.test' where id = 9102$$) = 'ok');
select t.ok('client: an update cannot change the name',
  t.val($$select author_name from public.workshop_items where id = 9102$$) = 'Bobby', t.val($$select author_name from public.workshop_items where id = 9102$$));
reset role;
select set_config('request.jwt.claims', '', false);   -- as the SQL editor
update public.workshop_items set status = 'approved' where id = 9002;   -- reviews need an approved item
select t.as_user(:'B'); set role authenticated;
select t.ok('client: review with an email name',
  t.try($$insert into public.workshop_reviews (item_id, reviewer_name, rating) values (9002, 'bob@example.test', 5)$$) = 'ok',
  t.try($$insert into public.workshop_reviews (item_id, reviewer_name, rating) values (9002, 'bob@example.test', 5)$$));
select t.ok('client: reviewer_name is the public name',
  t.val($$select reviewer_name from public.workshop_reviews where item_id = 9002$$) = 'Bobby');
reset role;

-- ======================= activity: who reads own rows =========================
-- The "see friends activity" policy deliberately keeps actor = auth.uid(): the
-- profile page (Profiles.hpp, viewing yourself), the account data export
-- (AccountData.hpp "activity") and pruning your own rows (DELETE ... WHERE
-- needs SELECT) read your own rows. The Friends feed filters actor=neq.<me>.
select t.as_user(:'A'); set role authenticated;
select t.ok('activity: I can read my own rows (profile page, data export)',
  t.val($$select count(*) from public.activity where actor = auth.uid()$$)::int > 0);
select t.ok('activity: the Friends feed query (actor=neq.me) has none of my rows',
  t.val($$select count(*) from public.activity where actor <> auth.uid() and actor = '00000000-0000-0000-0000-0000000000a1'$$) = '0');
select t.ok('activity: I can prune my own rows',
  t.try($$delete from public.activity where actor = auth.uid() and kind = 'playing'$$) = 'ok'
  and t.val($$select count(*) from public.activity where actor = auth.uid() and kind = 'playing'$$) = '0');
reset role;
select t.as_user(:'C'); set role authenticated;
select t.ok('activity: a stranger cannot read my rows',
  t.val($$select count(*) from public.activity where actor = '00000000-0000-0000-0000-0000000000a1'$$) = '0');
reset role;

-- ======================= updates on every covered table ======================
-- Even a direct (superuser) update of the name column gets the public name back
-- (admin_log is append-only since admin.sql round A2: there the update is refused instead).
select t.ok('update: ' || c.tbl || '.' || c.name_col || ' cannot be set to an email',
  (x.r = 'ok' or (c.tbl = 'admin_log' and x.r like '42501:%append-only%'))
  and t.val(format('select string_agg(distinct %I, '','') from public.%I where %I = %L', c.name_col, c.tbl, c.id_col,
                   '00000000-0000-0000-0000-0000000000a1')) in ('alice', '<null>'), x.r)
  from t.cov c, lateral (select t.try(format('update public.%I set %I = ''alice@example.test'' where %I = %L', c.tbl, c.name_col,
                                             c.id_col, '00000000-0000-0000-0000-0000000000a1')) as r) x;
select t.ok('update: changing the author id also changes the name',
  t.try($$update public.forum_posts set author = '00000000-0000-0000-0000-0000000000c3' where body = 'hello'$$) = 'ok'
  and t.val($$select author_name from public.forum_posts where body = 'hello'$$) = 'carol');
select t.ok('update: other columns can still be updated',
  t.try($$update public.workshop_items set downloads = downloads + 1 where id = 9102$$) = 'ok'
  and t.val($$select downloads from public.workshop_items where id = 9102$$) = '1');
select t.ok('update: mod-only trigger and name trigger agree on forum_threads',
  t.try($$update public.forum_threads set author_name = 'x@example.test' where id = 9101$$) = 'ok'
  and t.val($$select author_name from public.forum_threads where id = 9101$$) = 'alice');

-- A display-name change shows on new rows (old rows keep their snapshot).
update public.profiles set display_name = 'Ally' where id = :'A';
select t.as_user(:'A'); set role authenticated;
select t.try($$insert into public.forum_posts (thread_id, author_name, body) values (9101, 'whatever', 'after rename')$$);
reset role;
select t.ok('rename: a new post uses the new display name', t.val($$select author_name from public.forum_posts where body = 'after rename'$$) = 'Ally');

-- ======================= no '@' anywhere =====================================
select t.ok('final: no @ in ' || c.tbl || '.' || c.name_col,
  t.val(format('select count(*) from public.%I where position(''@'' in coalesce(%I, '''')) > 0', c.tbl, c.name_col)) = '0',
  t.val(format('select count(*) from public.%I where position(''@'' in coalesce(%I, '''')) > 0', c.tbl, c.name_col)))
  from t.cov c;
select t.ok('final: no @ in any profile username or display_name',
  not exists (select 1 from public.profiles where position('@' in username) > 0 or position('@' in coalesce(display_name, '')) > 0));
\endif
