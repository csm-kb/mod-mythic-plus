#!/usr/bin/env bash
# Gate: build the real worldserver + db-import images under the mpcheck tag (master images untouched).
# Expect: tens of minutes the first time (ccache-backed afterwards).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
LOG="$MP_OUT/gate.log"
mp_require_docker
mp_say "gate: docker compose build ac-worldserver ac-db-import (tag $DOCKER_IMAGE_TAG)"
cd "$MP_ROOT"
mp_run_logged "$LOG" docker compose build --progress=plain ac-worldserver ac-db-import \
  || mp_fail "gate build" $? "$LOG"
mp_check_drift "$LOG"
mp_say "PASS gate — log: $LOG"
