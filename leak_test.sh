#!/usr/bin/env bash

# Quitting if something go wrong

set -euo pipefail

#  Defaults
LST_HOST="127.0.0.1"
LST_PORT="5432"
DBS_HOST="192.168.31.2"
DBS_PORT="5432"

LOG_FILE="valgrind.log"
EXEC="./pg_proxy"

if [[ ! -x "${EXEC}" ]]; then
  echo "Can't find executable"
  echo "  ${EXEC}"
  exit 1
fi

sudo valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --num-callers=20 \
  --log-file="${LOG_FILE}" \
  "${EXEC}" \
  "${LST_HOST}" \
  "${LST_PORT}" \
  "${DBS_HOST}" \
  "${DBS_PORT}"
