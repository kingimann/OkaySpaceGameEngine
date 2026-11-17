#!/usr/bin/env bash
# docs/sql/reactions_privacy.sql against a LOCAL, throwaway Postgres: never Supabase or any live database. Same
# stand-in as test_blocks_sql.sh (tests/sql/supabase_stub.sql), then social.sql ... blocks.sql, the migration twice
# (it must be idempotent), and tests/sql/test_reactions_privacy.sql.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_reactions_sql.sh
#
# OKAY_SQL_TEST_SKIP_MIGRATION=1 loads everything but the migration: the checks that only the migration can satisfy FAIL
# (this is how the "fails first" run is made). Skipped (exit 77) when PGHOST is unset.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${OKAY_SQL_TEST_PGHOST:-}"
PORT="${OKAY_SQL_TEST_PGPORT:-5432}"
USER_="${OKAY_SQL_TEST_PGUSER:-postgres}"
DB=okay_rx_sqltest
if [ -z "$HOST" ]; then echo "reactions_sql: skipped (set OKAY_SQL_TEST_PGHOST to a local Postgres)"; exit 77; fi
case "$HOST" in
    /*|localhost|127.0.0.1|::1) ;;
    *) echo "reactions_sql: refusing non-local host '$HOST'"; exit 1 ;;
esac
command -v psql >/dev/null || { echo "reactions_sql: skipped (no psql)"; exit 77; }
PSQL=(psql -h "$HOST" -p "$PORT" -U "$USER_" -X -q -v ON_ERROR_STOP=1)
T="$(mktemp -d)"; trap 'rm -rf "$T"; "${PSQL[@]}" -d postgres -c "drop database if exists $DB" >/dev/null 2>&1' EXIT

"${PSQL[@]}" -d postgres -c "set client_min_messages = warning" -c "drop database if exists $DB" -c "create database $DB" >/dev/null || exit 1
files=("$REPO/tests/sql/supabase_stub.sql" "$REPO/docs/sql/social.sql" "$REPO/docs/sql/profile_showcase.sql"
       "$REPO/docs/sql/profile_extras.sql" "$REPO/docs/sql/forum_sections.sql" "$REPO/docs/sql/follows.sql"
       "$REPO/docs/sql/group_chats.sql" "$REPO/docs/sql/admin.sql" "$REPO/docs/sql/blocks.sql")
if [ "${OKAY_SQL_TEST_SKIP_MIGRATION:-0}" != 1 ]; then files+=("$REPO/docs/sql/reactions_privacy.sql" "$REPO/docs/sql/reactions_privacy.sql"); fi
for f in "${files[@]}"; do
    "${PSQL[@]}" -d "$DB" -f "$f" >"$T/out" 2>&1 || { echo "FAIL: loading $(basename "$f")"; cat "$T/out"; exit 1; }
done
"${PSQL[@]}" -d "$DB" -f "$REPO/tests/sql/test_reactions_privacy.sql" >"$T/out" 2>&1
code=$?
pass=$(grep -c 'WARNING:  PASS' "$T/out")
fail=$(grep -c 'WARNING:  FAIL' "$T/out")
grep 'WARNING:  FAIL\|ERROR' "$T/out"
if [ "$code" -ne 0 ] || [ "$fail" -ne 0 ] || [ "$pass" -lt 20 ]; then
    echo "reactions_sql: $pass passed, $fail failed (psql exit $code)"; exit 1
fi
echo "reactions_sql: ok ($pass checks)"
