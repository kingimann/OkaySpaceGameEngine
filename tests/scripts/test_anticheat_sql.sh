#!/usr/bin/env bash
# docs/sql/anticheat.sql against a LOCAL, throwaway Postgres: never Supabase or
# any live database. tests/sql/supabase_stub.sql stands in for what Supabase
# provides (auth.uid(), auth.users, the anon / authenticated / service_role
# roles), then the prerequisites (social.sql ... admin.sql), anticheat.sql
# twice (it must be idempotent), and tests/sql/test_anticheat.sql.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_anticheat_sql.sh
#
# PGHOST must be a unix-socket directory or localhost; the script creates and
# drops its own database (okay_ac_sqltest). Skipped (exit 77) when unset.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${OKAY_SQL_TEST_PGHOST:-}"
PORT="${OKAY_SQL_TEST_PGPORT:-5432}"
USER_="${OKAY_SQL_TEST_PGUSER:-postgres}"
DB=okay_ac_sqltest
if [ -z "$HOST" ]; then echo "anticheat_sql: skipped (set OKAY_SQL_TEST_PGHOST to a local Postgres)"; exit 77; fi
case "$HOST" in
    /*|localhost|127.0.0.1|::1) ;;
    *) echo "anticheat_sql: refusing non-local host '$HOST'"; exit 1 ;;
esac
command -v psql >/dev/null || { echo "anticheat_sql: skipped (no psql)"; exit 77; }
PSQL=(psql -h "$HOST" -p "$PORT" -U "$USER_" -X -q -v ON_ERROR_STOP=1)
T="$(mktemp -d)"; trap 'rm -rf "$T"; "${PSQL[@]}" -d postgres -c "drop database if exists $DB" >/dev/null 2>&1' EXIT

"${PSQL[@]}" -d postgres -c "set client_min_messages = warning" -c "drop database if exists $DB" -c "create database $DB" >/dev/null || exit 1
for f in "$REPO/tests/sql/supabase_stub.sql" "$REPO/docs/sql/social.sql" "$REPO/docs/sql/profile_showcase.sql" \
         "$REPO/docs/sql/profile_extras.sql" "$REPO/docs/sql/forum_sections.sql" "$REPO/docs/sql/follows.sql" \
         "$REPO/docs/sql/group_chats.sql" "$REPO/docs/sql/admin.sql" \
         "$REPO/docs/sql/anticheat.sql" "$REPO/docs/sql/anticheat.sql"; do
    "${PSQL[@]}" -d "$DB" -f "$f" >"$T/out" 2>&1 || { echo "FAIL: loading $(basename "$f")"; cat "$T/out"; exit 1; }
done
"${PSQL[@]}" -d "$DB" -f "$REPO/tests/sql/test_anticheat.sql" >"$T/out" 2>&1
code=$?
pass=$(grep -c 'WARNING:  PASS' "$T/out")
fail=$(grep -c 'WARNING:  FAIL' "$T/out")
grep 'WARNING:  FAIL\|ERROR' "$T/out"
if [ "$code" -ne 0 ] || [ "$fail" -ne 0 ] || [ "$pass" -lt 30 ]; then
    echo "anticheat_sql: $pass passed, $fail failed (psql exit $code)"; exit 1
fi
echo "anticheat_sql: ok ($pass checks)"
