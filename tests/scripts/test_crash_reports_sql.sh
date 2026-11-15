#!/usr/bin/env bash
# docs/sql/crash_reports.sql against a LOCAL, throwaway Postgres: never Supabase or any live database. tests/sql/supabase_stub.sql stands
# in for what Supabase provides (the anon / authenticated / service_role roles), then crash_reports.sql twice (it must be idempotent), then
# tests/sql/test_crash_reports.sql: anon may insert nine columns and nothing else, never read, the table's checks, the flood cap, the
# self-pruning and the size limit. Then 18 senders at once (the cap must hold exactly: a plain count in a trigger lets rows past it), and a table
# made by the first version of the file (tests/sql/crash_reports_v1.sql) brought up to date by the current one.
#
#   OKAY_SQL_TEST_PGHOST=/var/run/postgresql OKAY_SQL_TEST_PGPORT=5432 bash tests/scripts/test_crash_reports_sql.sh
#
# PGHOST must be a unix-socket directory or localhost; the script creates and drops its own database (okay_cr_sqltest). Skipped (exit 77) when unset.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HOST="${OKAY_SQL_TEST_PGHOST:-}"
PORT="${OKAY_SQL_TEST_PGPORT:-5432}"
USER_="${OKAY_SQL_TEST_PGUSER:-postgres}"
DB=okay_cr_sqltest
if [ -z "$HOST" ]; then echo "crash_reports_sql: skipped (set OKAY_SQL_TEST_PGHOST to a local Postgres)"; exit 77; fi
case "$HOST" in
    /*|localhost|127.0.0.1|::1) ;;
    *) echo "crash_reports_sql: refusing non-local host '$HOST'"; exit 1 ;;
esac
command -v psql >/dev/null || { echo "crash_reports_sql: skipped (no psql)"; exit 77; }
PSQL=(psql -h "$HOST" -p "$PORT" -U "$USER_" -X -q -v ON_ERROR_STOP=1)
SENDER=(psql -h "$HOST" -p "$PORT" -U "$USER_" -X -q)   # (no ON_ERROR_STOP: a sender that is refused goes on with its next report)
UPDB=okay_cr_sqltest_up
T="$(mktemp -d)"
cleanup() {
    rm -rf "$T"
    "${PSQL[@]}" -d postgres -c "select pg_terminate_backend(pid) from pg_stat_activity where datname in ('$DB', '$UPDB') and pid <> pg_backend_pid()" >/dev/null 2>&1
    "${PSQL[@]}" -d postgres -c "drop database if exists $DB" -c "drop database if exists $UPDB" >/dev/null 2>&1
}
trap cleanup EXIT

"${PSQL[@]}" -d postgres -c "set client_min_messages = warning" -c "drop database if exists $DB" -c "create database $DB" >/dev/null || exit 1
for f in "$REPO/tests/sql/supabase_stub.sql" "$REPO/docs/sql/crash_reports.sql" "$REPO/docs/sql/crash_reports.sql"; do
    "${PSQL[@]}" -d "$DB" -f "$f" >"$T/out" 2>&1 || { echo "FAIL: loading $(basename "$f")"; cat "$T/out"; exit 1; }
done
"${PSQL[@]}" -d "$DB" -f "$REPO/tests/sql/test_crash_reports.sql" >"$T/out" 2>&1
code=$?
pass=$(grep -c 'WARNING:  PASS' "$T/out")
fail=$(grep -c 'WARNING:  FAIL' "$T/out")
grep 'WARNING:  FAIL\|ERROR' "$T/out"
if [ "$code" -ne 0 ] || [ "$fail" -ne 0 ] || [ "$pass" -lt 50 ]; then
    echo "crash_reports_sql: $pass passed, $fail failed (psql exit $code)"; exit 1
fi

# ---- 30 senders at the same moment: exactly 60 rows get in, never more ----------------------------------------------------------------
# A start gate makes them really overlap (psql processes started one after the other would mostly queue up on their own): the senders wait for a
# shared advisory lock that a controller session holds, and are let go together when the controller ends.
GATE=777001
"${PSQL[@]}" -d "$DB" -c "delete from public.crash_reports" >/dev/null || exit 1
count_gate() { "${PSQL[@]}" -d "$DB" -At -c "select count(*) from pg_locks where locktype = 'advisory' and objid = $GATE and granted = $1"; }
timeout 60 "${SENDER[@]}" -d "$DB" -c "select pg_advisory_lock($GATE), pg_sleep(30)" >/dev/null 2>&1 &   # (opens by itself after 30 s if the script is stopped)
for _ in $(seq 1 100); do [ "$(count_gate true)" = 1 ] && break; sleep 0.1; done
for n in $(seq 1 30); do
    {
        echo "select pg_advisory_lock_shared($GATE);"
        echo "set role anon;"
        for i in $(seq 1 8); do
            printf "insert into public.crash_reports (report_id, signature, version, exception, report) values ('%016x', 'abcdef0123456789', '1', 'x', 'OkaySpace crash report concurrent');\n" $((n * 1000 + i))
        done
    } > "$T/sender$n.sql"
    timeout 120 "${SENDER[@]}" -d "$DB" -f "$T/sender$n.sql" >/dev/null 2>&1 &
done
for _ in $(seq 1 150); do [ "$(count_gate false)" = 30 ] && break; sleep 0.1; done
waiting=$(count_gate false)
"${PSQL[@]}" -d "$DB" -At -c "select pg_terminate_backend(pid) from pg_locks where locktype = 'advisory' and objid = $GATE and granted and mode = 'ExclusiveLock'" >/dev/null   # the gate opens
wait 2>/dev/null
if [ "$waiting" != 30 ]; then echo "crash_reports_sql: FAIL: only $waiting of 30 senders reached the start gate"; exit 1; fi
rows=$("${PSQL[@]}" -d "$DB" -At -c "select count(*) from public.crash_reports")
if [ "$rows" != 60 ]; then
    echo "crash_reports_sql: FAIL: 30 senders at once stored $rows rows (the cap is 60: more means the guard does not hold, fewer means it refused too much)"; exit 1
fi

# ---- a table made by the first version of the file is brought up to date, and loading the new file again changes nothing -------------------
"${PSQL[@]}" -d postgres -c "set client_min_messages = warning" -c "drop database if exists $UPDB" -c "create database $UPDB" >/dev/null || exit 1
for f in "$REPO/tests/sql/supabase_stub.sql" "$REPO/tests/sql/crash_reports_v1.sql"; do
    "${PSQL[@]}" -d "$UPDB" -f "$f" >"$T/out" 2>&1 || { echo "FAIL: loading $(basename "$f") for the upgrade check"; cat "$T/out"; exit 1; }
done
"${PSQL[@]}" -d "$UPDB" -c "insert into public.crash_reports (report_id, signature, version, exception, report) values ('0123456789abcdef', 'abcdef0123456789', '1', 'x', 'OkaySpace crash report ' || repeat('x', 90000))" >/dev/null \
    || { echo "crash_reports_sql: FAIL: the old table did not take a 90 KB row"; exit 1; }
for f in "$REPO/docs/sql/crash_reports.sql" "$REPO/docs/sql/crash_reports.sql"; do
    "${PSQL[@]}" -d "$UPDB" -f "$f" >"$T/out" 2>&1 || { echo "FAIL: the upgrade ($(basename "$f") on the old table)"; cat "$T/out"; exit 1; }
done
up=$("${PSQL[@]}" -d "$UPDB" -At 2>"$T/uperr" <<'SQL'
select (select count(*) from information_schema.columns where table_name = 'crash_reports' and column_name = 'attempts') ||
       '|' || (select count(*) from public.crash_reports) ||
       '|' || (select pg_get_constraintdef(oid) like '%65536%' from pg_constraint where conname = 'crash_reports_report_check') ||
       '|' || (select count(*) from pg_proc where proname = 'crash_reports_guard' and prosrc like '%pg_advisory_xact_lock%');
set role anon;
insert into public.crash_reports (report_id, signature, version, exception, report) values ('fedcba9876543210', 'abcdef0123456789', '1', 'x', 'OkaySpace crash report ' || repeat('x', 70000));
SQL
)
if [ "$up" != "1|1|true|1" ] || ! grep -q "crash_reports_report_check" "$T/uperr"; then
    echo "crash_reports_sql: FAIL: the upgrade of an old table: got '$up' (want 1|1|true|1: attempts column, the old row kept, the 64 KB limit, the lock in the guard)"; cat "$T/uperr"; exit 1
fi
echo "crash_reports_sql: ok ($pass checks, 30 senders at once, upgrade of the first version)"
