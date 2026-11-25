#!/usr/bin/env bash
# docs/sql/chat_invites.sql (chat client ids, limits, friends-only invites, the
# game_invites table) and the friends-only join link of docs/sql/blocks.sql,
# against a LOCAL, throwaway Postgres: never Supabase or any live database.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql bash tests/scripts/test_chat_invites_sql.sh
#
# Skipped (exit 77) unless OKAY_SQL_TEST_PGHOST names a local Postgres. The
# file is loaded twice: it must be idempotent. Shared steps: sql_test_lib.sh.
set -uo pipefail
source "$(dirname "$0")/sql_test_lib.sh"
sqltest_begin chat_invites_sql okay_ci_sqltest
for f in tests/sql/supabase_stub.sql docs/sql/social.sql docs/sql/group_chats.sql \
         docs/sql/blocks.sql docs/sql/chat_invites.sql docs/sql/chat_invites.sql; do
    sqltest_load -f "$REPO/$f"
done
sqltest_checks 60 -f "$REPO/tests/sql/test_chat_invites.sql"
