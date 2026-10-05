#!/usr/bin/env bash
# Pre-flight (spec §3.3b) on the isolated server: dbimport, then worldserver --dry-run.
# Usage: tools/mp-preflight.sh [--dbimport-only]    Env: MP_KEEP_DB=1 keeps ac-mpcheck-db for mp-live.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
ONLY_DBIMPORT=0; [[ "${1:-}" == "--dbimport-only" ]] && ONLY_DBIMPORT=1
mp_require_docker
for img in "acore/ac-wotlk-db-import:$DOCKER_IMAGE_TAG" "acore/ac-wotlk-worldserver:$DOCKER_IMAGE_TAG"; do
  docker image inspect "$img" >/dev/null 2>&1 || { echo "mp: $img missing — run tools/mp-gate.sh first" >&2; exit 2; }
done
[[ "${MP_KEEP_DB:-0}" == 1 ]] || trap '"$MP_MOD_DIR/tools/mp-db.sh" down' EXIT
"$MP_MOD_DIR/tools/mp-db.sh" up

# Scratch etc/logs: a copy of the user's confs minus this module's conf, so conf.dist defaults apply and the
# entrypoint's cp -n never writes into env/dist/etc.
rm -rf "$MP_ROOT/var/mpcheck/etc" "$MP_ROOT/var/mpcheck/logs"
mkdir -p "$MP_ROOT/var/mpcheck/etc" "$MP_ROOT/var/mpcheck/logs"
cp -r "$MP_ROOT/env/dist/etc/." "$MP_ROOT/var/mpcheck/etc/" 2>/dev/null || true
rm -f "$MP_ROOT/var/mpcheck/etc/modules/mod-mythic-plus.conf"
export DOCKER_VOL_ETC=./var/mpcheck/etc DOCKER_VOL_LOGS=./var/mpcheck/logs

ENVS=(-e "AC_LOGIN_DATABASE_INFO=$(mp_dbinfo acore_auth)"
      -e "AC_WORLD_DATABASE_INFO=$(mp_dbinfo acore_world)"
      -e "AC_CHARACTER_DATABASE_INFO=$(mp_dbinfo acore_characters)"
      -e "AC_PLAYERBOTS_DATABASE_INFO=$(mp_dbinfo acore_playerbots)")
mp_guard_env "${ENVS[@]}"
cd "$MP_ROOT"

T0=$SECONDS; LOG="$MP_OUT/preflight-dbimport.log"
mp_say "dbimport (module SQL) against $MP_DB_CONTAINER"
mp_run_logged "$LOG" docker compose run --rm --no-deps -T "${ENVS[@]}" ac-db-import \
  || mp_fail "dbimport" $? "$LOG"
mp_say "dbimport ok ($((SECONDS - T0))s)"
# --dry-run exits before any runtime spawn, so check the spawn-id caps (0xFFFFFF, TCE00007) here.
SPAWN_MAX=$(docker exec "$MP_DB_CONTAINER" mysql -uroot -p"$MP_DB_PW" -N -e \
  "SELECT GREATEST((SELECT COALESCE(MAX(guid), 0) FROM acore_world.creature),
                   (SELECT COALESCE(MAX(guid), 0) FROM acore_world.gameobject))" 2>/dev/null) \
  || mp_fail "spawn-id cap query" $? "$LOG"
if (( SPAWN_MAX >= 16777215 )); then
  mp_fail "creature/gameobject spawn guid $SPAWN_MAX exceeds 0xFFFFFF (core shuts down on first spawn)" 1 "$LOG"
fi
if [[ $ONLY_DBIMPORT == 1 ]]; then mp_say "PASS preflight --dbimport-only"; exit 0; fi

T0=$SECONDS; LOG="$MP_OUT/preflight-worldserver.log"
mp_say "worldserver --dry-run against $MP_DB_CONTAINER"
mp_run_logged "$LOG" docker compose run --rm --no-deps -T "${ENVS[@]}" -e "AC_APPENDER_CONSOLE=1,4,6" \
  ac-worldserver worldserver --dry-run || mp_fail "worldserver dry-run" $? "$LOG"
grep -qE '\[module\.MythicPlus\.config\].*event=module_loaded' "$LOG" \
  || mp_fail "no module_loaded line" 1 "$LOG"
if grep -E '^(ERROR|FATAL) +\[module\.MythicPlus' "$LOG"; then mp_fail "module ERROR/FATAL lines" 1 "$LOG"; fi
grep -qE 'event=module_loaded.*config_warnings=0' "$LOG" || mp_fail "config warnings with default conf" 1 "$LOG"
mp_say "PASS preflight (dry-run $((SECONDS - T0))s) — logs: $MP_OUT"
