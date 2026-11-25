#!/usr/bin/env bash
# docs/sql/blocks.sql against a LOCAL, throwaway Postgres: never Supabase or
# any live database. tests/sql/supabase_stub.sql stands in for what Supabase
# provides (auth.uid(), auth.users, the anon / authenticated / service_role
# roles), then the prerequisites (social.sql ... admin.sql), blocks.sql twice
# (it must be idempotent), and tests/sql/test_blocks.sql.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_blocks_sql.sh
#
# PGHOST must be a unix-socket directory or localhost; the script creates and
# drops its own database (okay_rp_sqltest). Skipped (exit 77) when unset.
# Shared steps: tests/scripts/sql_test_lib.sh.
set -uo pipefail
source "$(dirname "$0")/sql_test_lib.sh"
sqltest_begin blocks_sql okay_rp_sqltest
for f in tests/sql/supabase_stub.sql docs/sql/social.sql docs/sql/profile_showcase.sql \
         docs/sql/profile_extras.sql docs/sql/forum_sections.sql docs/sql/follows.sql \
         docs/sql/group_chats.sql docs/sql/admin.sql \
         docs/sql/blocks.sql docs/sql/blocks.sql; do
    sqltest_load -f "$REPO/$f"
done
sqltest_checks 70 -f "$REPO/tests/sql/test_blocks.sql"
