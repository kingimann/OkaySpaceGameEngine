#!/usr/bin/env bash
# Shared steps of the tests/scripts/test_*_sql.sh scripts (sourced, not run):
# a LOCAL, throwaway Postgres only, never Supabase or any live database.
#   sqltest_begin <name> <db>    skip (exit 77) unless OKAY_SQL_TEST_PGHOST is a
#                                unix-socket directory or localhost; make a fresh
#                                <db> and drop it on exit. Sets PSQL, T, REPO.
#   sqltest_load <psql args...>  run psql on <db>; stop the test if it fails.
#   sqltest_checks <min> <psql args...>
#                                run a check file (PASS/FAIL warnings); exit 1
#                                on any FAIL, a psql error or fewer than <min>.
sqltest_begin() {
    SQLTEST_NAME="$1"; SQLTEST_DB="$2"
    REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    local host="${OKAY_SQL_TEST_PGHOST:-}" port="${OKAY_SQL_TEST_PGPORT:-5432}" user="${OKAY_SQL_TEST_PGUSER:-postgres}"
    if [ -z "$host" ]; then echo "$SQLTEST_NAME: skipped (set OKAY_SQL_TEST_PGHOST to a local Postgres)"; exit 77; fi
    case "$host" in
        /*|localhost|127.0.0.1|::1) ;;
        *) echo "$SQLTEST_NAME: refusing non-local host '$host'"; exit 1 ;;
    esac
    command -v psql >/dev/null || { echo "$SQLTEST_NAME: skipped (no psql)"; exit 77; }
    PSQL=(psql -h "$host" -p "$port" -U "$user" -X -q -v ON_ERROR_STOP=1)
    T="$(mktemp -d)"
    trap 'rm -rf "$T"; "${PSQL[@]}" -d postgres -c "drop database if exists $SQLTEST_DB" >/dev/null 2>&1' EXIT
    "${PSQL[@]}" -d postgres -c "set client_min_messages = warning" -c "drop database if exists $SQLTEST_DB" \
        -c "create database $SQLTEST_DB" >/dev/null || exit 1
}
sqltest_load() {
    "${PSQL[@]}" -d "$SQLTEST_DB" "$@" >"$T/out" 2>&1 || { echo "FAIL: loading $*"; cat "$T/out"; exit 1; }
}
sqltest_checks() {
    local min="$1"; shift
    "${PSQL[@]}" -d "$SQLTEST_DB" "$@" >"$T/out" 2>&1
    local code=$? pass fail
    pass=$(grep -c 'WARNING:  PASS' "$T/out")
    fail=$(grep -c 'WARNING:  FAIL' "$T/out")
    grep 'WARNING:  FAIL\|ERROR' "$T/out"
    if [ "$code" -ne 0 ] || [ "$fail" -ne 0 ] || [ "$pass" -lt "$min" ]; then
        echo "$SQLTEST_NAME: $pass passed, $fail failed (psql exit $code)"; exit 1
    fi
    echo "$SQLTEST_NAME: ok ($pass checks)"
}
