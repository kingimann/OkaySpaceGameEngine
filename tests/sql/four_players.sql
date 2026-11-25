-- Shared start of the LOCAL SQL check files that need four players (loaded with
-- \ir four_players.sql): alice, bob, carol and dave in auth.users with matching
-- usernames, the check helpers (test_helpers.sql), and :A :B :C :D for their ids.
\set QUIET on
set client_min_messages = warning;
reset role;

insert into auth.users (id, email) values
  ('00000000-0000-0000-0000-0000000000a1', 'alice@x'), ('00000000-0000-0000-0000-0000000000b2', 'bob@x'),
  ('00000000-0000-0000-0000-0000000000c3', 'carol@x'), ('00000000-0000-0000-0000-0000000000d4', 'dave@x');
update public.profiles set username = split_part((select email from auth.users u where u.id = profiles.id), '@', 1);

\ir test_helpers.sql

\set A '00000000-0000-0000-0000-0000000000a1'
\set B '00000000-0000-0000-0000-0000000000b2'
\set C '00000000-0000-0000-0000-0000000000c3'
\set D '00000000-0000-0000-0000-0000000000d4'
