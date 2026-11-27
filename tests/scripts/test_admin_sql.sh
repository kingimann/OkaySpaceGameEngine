#!/usr/bin/env bash
# docs/sql/admin.sql against a LOCAL, throwaway Postgres: never Supabase or any
# live database. tests/sql/supabase_stub.sql stands in for what Supabase
# provides, then the prerequisites (social.sql ... group_chats.sql), admin.sql
# anticheat.sql and app_config.sql twice each (they must be idempotent), and tests/sql/test_admin.sql.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_admin_sql.sh
#
# PGHOST must be a unix-socket directory or localhost; the script creates and
# drops its own database (okay_admin_sqltest). Skipped (exit 77) when unset.
# Shared steps: tests/scripts/sql_test_lib.sh.
set -uo pipefail
source "$(dirname "$0")/sql_test_lib.sh"
sqltest_begin admin_sql okay_admin_sqltest
for f in tests/sql/supabase_stub.sql docs/sql/social.sql docs/sql/profile_showcase.sql \
         docs/sql/profile_extras.sql docs/sql/forum_sections.sql docs/sql/follows.sql \
         docs/sql/group_chats.sql docs/sql/admin.sql docs/sql/admin.sql \
         docs/sql/anticheat.sql docs/sql/anticheat.sql docs/sql/app_config.sql docs/sql/app_config.sql; do
    sqltest_load -f "$REPO/$f"
done
sqltest_checks 150 -f "$REPO/tests/sql/test_admin.sql"
