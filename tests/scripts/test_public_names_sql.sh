#!/usr/bin/env bash
# docs/sql/public_names.sql against a LOCAL, throwaway Postgres: never Supabase or
# any live database. tests/sql/supabase_stub.sql stands in for what Supabase
# provides, then the prerequisites (social.sql ... blocks.sql), the old rows an
# old launcher left (tests/sql/test_public_names.sql with seed=1),
# public_names.sql twice (it must be idempotent), and the checks (seed=0).
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_public_names_sql.sh
#
# Makes and drops its own database (okay_pn_sqltest). Skipped (exit 77) when
# OKAY_SQL_TEST_PGHOST is unset. Steps shared with the other SQL tests:
# tests/scripts/sql_test_lib.sh.
set -uo pipefail
source "$(dirname "$0")/sql_test_lib.sh"
sqltest_begin public_names_sql okay_pn_sqltest
for f in tests/sql/supabase_stub.sql docs/sql/social.sql docs/sql/profile_showcase.sql docs/sql/profile_extras.sql \
         docs/sql/forum_sections.sql docs/sql/follows.sql docs/sql/group_chats.sql docs/sql/admin.sql docs/sql/blocks.sql; do
    sqltest_load -f "$REPO/$f"
done
sqltest_load -v seed=1 -f "$REPO/tests/sql/test_public_names.sql"
sqltest_load -f "$REPO/docs/sql/public_names.sql"
sqltest_load -f "$REPO/docs/sql/public_names.sql"
sqltest_checks 135 -v seed=0 -f "$REPO/tests/sql/test_public_names.sql"
