#!/usr/bin/env bash
# Live checklist host (spec §3.3e): stops your normal auth+world, then runs auth (master image; no modules) and
# worldserver:$DOCKER_IMAGE_TAG against ac-mpcheck-db. Requires: MP_KEEP_DB=1 tools/mp-preflight.sh
# Baseline run: DOCKER_IMAGE_TAG=mpcheck-baseline tools/mp-live.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
mp_require_docker
docker ps --format '{{.Names}}' | grep -qx "$MP_DB_CONTAINER" \
  || { echo "mp: $MP_DB_CONTAINER not running — run: MP_KEEP_DB=1 tools/mp-preflight.sh" >&2; exit 2; }
cd "$MP_ROOT"
export DOCKER_VOL_ETC=./var/mpcheck/etc DOCKER_VOL_LOGS=./var/mpcheck/logs
ENVS=(-e "AC_LOGIN_DATABASE_INFO=$(mp_dbinfo acore_auth)"
      -e "AC_WORLD_DATABASE_INFO=$(mp_dbinfo acore_world)"
      -e "AC_CHARACTER_DATABASE_INFO=$(mp_dbinfo acore_characters)"
      -e "AC_PLAYERBOTS_DATABASE_INFO=$(mp_dbinfo acore_playerbots)")
mp_guard_env "${ENVS[@]}"
restore()
{
  docker rm -f mp-live-auth >/dev/null 2>&1 || true
  echo "mp: back to normal: docker compose up -d ac-authserver ac-worldserver"
}
trap restore EXIT
docker compose stop ac-worldserver ac-authserver
# The authserver must use the same auth DB copy so session keys match this worldserver.
DOCKER_IMAGE_TAG=master docker compose run -d --name mp-live-auth --service-ports --no-deps "${ENVS[@]}" ac-authserver
mp_say "worldserver:$DOCKER_IMAGE_TAG starting — log in normally; type 'server shutdown 1' in this console to end"
docker compose run --rm --service-ports --no-deps "${ENVS[@]}" ac-worldserver
