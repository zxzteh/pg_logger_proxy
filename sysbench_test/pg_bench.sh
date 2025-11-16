#!/usr/bin/bash

# Quitting if something go wrong

set -euo pipefail

# Parameters

PG_HOST="127.0.0.1"
PG_PORT="5432"
PG_USER="sbtest"
PG_PASS="sbpass"  #  Not safe, but it's local server
PG_DB="sbtest"

SYSBENCH_CMD="sysbench"
SCENARIO="oltp_read_write"

# We don't want asking password, we enter it here
export PGPASSWORD="$PG_PASS"

# To disable SSL 
export PGSSLMODE=disable

# Utils

drop_sbtest_tables() {
  echo "Clean sbtest* tables . . ."
  local DROP_SQL
  DROP_SQL=$(psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -Atc \
    "SELECT 'DROP TABLE IF EXISTS ' || quote_ident(tablename) || ' CASCADE;'
     FROM pg_tables
     WHERE schemaname = 'public' AND tablename LIKE 'sbtest%';")

  if [[ -n "$DROP_SQL" ]]; then
    echo "$DROP_SQL" | psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" >/dev/null
    echo "Tables was cleaned"
  else
    echo "No need to clean tables"
  fi
}

prepare_tables() {
  local TABLES="$1"
  local TABLE_SIZE="$2"

  echo "Tables preparing: TABLES=${TABLES}, TABLE_SIZE=${TABLE_SIZE} . . ."
  $SYSBENCH_CMD \
    --db-driver=pgsql \
    --pgsql-host="$PG_HOST" \
    --pgsql-port="$PG_PORT" \
    --pgsql-user="$PG_USER" \
    --pgsql-password="$PG_PASS" \
    --pgsql-db="$PG_DB" \
    --tables="$TABLES" \
    --table-size="$TABLE_SIZE" \
    "$SCENARIO" \
    prepare
}

run_sysbench() {
  local MODE_DESC="$1"
  local TABLES="$2"
  local TABLE_SIZE="$3"
  local THREADS="$4"
  local TIME="$5"
  local EVENTS="$6"

  echo "Run test ${MODE_DESC}"
  echo "TABLES=${TABLES}, TABLE_SIZE=${TABLE_SIZE}, THREADS=${THREADS}, TIME=${TIME}, EVENTS=${EVENTS}"

  local ARGS=(
    --db-driver=pgsql
    --pgsql-host="$PG_HOST"
    --pgsql-port="$PG_PORT"
    --pgsql-user="$PG_USER"
    --pgsql-password="$PG_PASS"
    --pgsql-db="$PG_DB"
    --tables="$TABLES"
    --table-size="$TABLE_SIZE"
    --threads="$THREADS"
    --report-interval=10
  )

  if [[ "$TIME" -gt 0 ]]; then
    ARGS+=(--time="$TIME")
  else
    ARGS+=(--time=0)
  fi

  if [[ "$EVENTS" -gt 0 ]]; then
    ARGS+=(--events="$EVENTS")
  fi

  $SYSBENCH_CMD "${ARGS[@]}" "$SCENARIO" run
}

cleanup_sysbench() {
  local TABLES="$1"
  echo "Count of TABLES=${TABLES}"
  $SYSBENCH_CMD \
    --db-driver=pgsql \
    --pgsql-host="$PG_HOST" \
    --pgsql-port="$PG_PORT" \
    --pgsql-user="$PG_USER" \
    --pgsql-password="$PG_PASS" \
    --pgsql-db="$PG_DB" \
    --tables="$TABLES" \
    "$SCENARIO" \
    cleanup || true
}

# Usage helper

usage() {
  cat <<EOF
Usage: $0 <mode>

Modes:
  ls - light single
  hs - heavy single
  lt - light torture
  ht - heavy torture
EOF
}

# Main script

if [[ $# -ne 1 ]]; then
  usage
  exit 1
fi

MODE="$1"

case "$MODE" in
  ls)
    TABLES=4
    TABLE_SIZE=100
    THREADS=4
    TIME=30
    EVENTS=0
    ;;

  hs)
    TABLES=8
    TABLE_SIZE=500000
    THREADS=128
    TIME=0
    EVENTS=50000
    ;;

  lt)
    TABLES=4
    TABLE_SIZE=200000
    THREADS=8
    TIME=300
    EVENTS=0
    ;;

  ht)
    TABLES=16
    TABLE_SIZE=1000000
    THREADS=128
    TIME=600
    EVENTS=0
    ;;

  *)
    echo "Non existent mode : $MODE"
    usage
    exit 1
    ;;
esac

echo "1. Clean tables . . ."
drop_sbtest_tables

echo "2. Test table creation . . ."
prepare_tables "$TABLES" "$TABLE_SIZE"

echo "3. Bench start . . ."
run_sysbench "$MODE" "$TABLES" "$TABLE_SIZE" "$THREADS" "$TIME" "$EVENTS"

echo "4. Bench Cleanup . . ."
cleanup_sysbench "$TABLES"

echo "All done"
