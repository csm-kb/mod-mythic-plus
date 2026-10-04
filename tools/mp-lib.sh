# Shared helpers for tools/mp-*.sh. Sourced, not executed.
MP_MOD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MP_ROOT="$(cd "$MP_MOD_DIR/../.." && pwd)"
MP_OUT="$MP_ROOT/var/mpcheck/out"
MP_DB_CONTAINER="ac-mpcheck-db"
MP_DB_VOLUME="ac-mpcheck-db-data"
MP_DB_PW="mpcheck"
export DOCKER_IMAGE_TAG="${DOCKER_IMAGE_TAG:-mpcheck}"
if [[ "$DOCKER_IMAGE_TAG" == master ]]; then
  echo "mp: refusing DOCKER_IMAGE_TAG=master (would replace your normal images)" >&2
  exit 2
fi
mkdir -p "$MP_OUT"

mp_say() { printf 'mp: [%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

mp_require_docker()
{
  docker info >/dev/null 2>&1 || { echo "mp: Docker Desktop is not running. Start it, then rerun." >&2; exit 3; }
}

# Runs "$@", tees to $1, returns the command's exit code (safe under set -e/pipefail).
mp_run_logged()
{
  local log=$1; shift
  set +e
  "$@" 2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  return "$rc"
}

mp_fail()
{
  echo
  echo "mp: FAIL — $1 (exit $2)"
  echo "mp: log: $3"
  tail -n 25 "$3" | sed 's/^/  | /'
  exit 1
}

mp_check_drift()
{
  if grep -q 'mod-mythic-plus config drift' "$1"; then
    mp_fail "config drift between MpConfig.cpp and conf.dist" 1 "$1"
  fi
}

# DB info string for the isolated server only.
mp_dbinfo() { echo "$MP_DB_CONTAINER;3306;root;$MP_DB_PW;$1"; }

# Refuse any environment that points at the real server.
mp_guard_env()
{
  local arg
  for arg in "$@"; do
    if [[ "$arg" == *"ac-database;"* ]]; then
      echo "mp: REFUSING — would target the real ac-database: $arg" >&2
      exit 2
    fi
  done
}

mp_compose_network()
{
  docker inspect ac-database -f '{{range $k, $v := .NetworkSettings.Networks}}{{$k}}{{"\n"}}{{end}}' | head -n1
}
