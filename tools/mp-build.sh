#!/usr/bin/env bash
# Inner-loop build of worldserver in the ac-dev-server container (Linux toolchain, persistent build volume).
# Usage: tools/mp-build.sh [default|noproviders] [--full]
#   noproviders  compile the bot seam with no providers (-DMP_NO_BOT_PROVIDERS=ON)
#   --full       recompile every module TU with -k (baseline/error capture; ccache replays warnings)
# Expect: first run builds the dev image and does a cold full build (tens of minutes); later runs are minutes.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
VARIANT="default"; FULL=0
for a in "$@"; do
  case "$a" in
    default|noproviders) VARIANT="$a" ;;
    --full) FULL=1 ;;
    *) echo "usage: $0 [default|noproviders] [--full]" >&2; exit 2 ;;
  esac
done
EXTRA=""; [[ "$VARIANT" == noproviders ]] && EXTRA="-DMP_NO_BOT_PROVIDERS=ON"
BUILD_DIR="/azerothcore/var/build/mp-$VARIANT"
LOG="$MP_OUT/build-$VARIANT.log"
mp_require_docker
mp_say "building worldserver ($VARIANT$([[ $FULL == 1 ]] && echo ', full module rebuild'))"
cd "$MP_ROOT"
mp_run_logged "$LOG" env MSYS_NO_PATHCONV=1 docker compose --profile dev run --rm --no-deps -T \
  -e CCACHE_DIR=/azerothcore/var/build/ccache -e GIT_OPTIONAL_LOCKS=0 ac-dev-server bash -c "
    set -euo pipefail
    mkdir -p '$BUILD_DIR' && cd '$BUILD_DIR'
    cmake /azerothcore -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAPPS_BUILD=all -DTOOLS_BUILD=none \
      -DSCRIPTS=static -DMODULES=static -DWITH_WARNINGS=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache $EXTRA
    if [[ $FULL == 1 ]]; then
      find '$BUILD_DIR/modules' -path '*mod-mythic-plus*' -name '*.o' -delete 2>/dev/null || true
      cmake --build . --target worldserver -j \$(nproc) -- -k
    else
      cmake --build . --target worldserver -j \$(nproc)
    fi
  " || { rc=$?; grep -nE "mod-mythic-plus/.*error:|ld\.lld: error|undefined reference" "$LOG" \
           > "$MP_OUT/errors-$VARIANT.txt" || true; mp_fail "build ($VARIANT)" "$rc" "$LOG"; }
mp_check_drift "$LOG"
if [[ $FULL == 1 ]]; then
  grep -E "mod-mythic-plus/.*warning:" "$LOG" | sort -u > "$MP_OUT/warnings-$VARIANT.txt" || true
  mp_say "module warnings: $(wc -l < "$MP_OUT/warnings-$VARIANT.txt")"
fi
mp_say "PASS build ($VARIANT) — log: $LOG"
