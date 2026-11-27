-- LOCAL test of docs/sql/admin.sql (and the staff rule in forum_sections.sql).
-- Each check prints "PASS <name>" or "FAIL <name>: <why>" as a WARNING.
-- Players: alice, bob, carol, dave (four_players.sql). Only made-up values.
\ir four_players.sql

-- ---- Staff may verify a player, and change nothing else on their profile ------
reset role;
insert into public.profile_badges (owner, badge) values (:'C', 'staff');
update public.profiles set bio = 'bob bio', verified = false where id = :'B';

select t.as_user(:'C');
set role authenticated;
select t.ok('staff verifies a player', t.try(format($q$update public.profiles set verified = true where id = %L$q$, :'B')) = 'ok');
select t.ok('staff cannot change another player''s bio',
  t.try(format($q$update public.profiles set bio = 'hacked' where id = %L$q$, :'B')) <> 'ok');
select t.ok('staff cannot rename another player',
  t.try(format($q$update public.profiles set username = 'bob2' where id = %L$q$, :'B')) <> 'ok');
select t.ok('staff cannot sneak a bio change in with a verify',
  t.try(format($q$update public.profiles set verified = false, bio = 'x' where id = %L$q$, :'B')) <> 'ok');
reset role;
select t.ok('bob''s profile kept its text', (select bio = 'bob bio' and username = 'bob' and verified from public.profiles where id = :'B'));

select t.as_user(:'B');
set role authenticated;
select t.ok('a player still edits their own bio', t.try(format($q$update public.profiles set bio = 'mine' where id = %L$q$, :'B')) = 'ok');
reset role;
