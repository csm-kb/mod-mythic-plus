#!/usr/bin/env bash
# Inner-loop build of worldserver.
# Usage: tools/mp-build.sh [default|noproviders] [--full]
#   default      docker compose build of ac-worldserver (tag $DOCKER_IMAGE_TAG), reusing the warm BuildKit ccache
#                mount: takes minutes. Every image build compiles all module TUs (ccache replays warnings), so
#                --full is accepted but is a no-op here. Limitation: the image build stops at the first failing
#                batch (no -k; the root Dockerfile is off-limits), so errors-default.txt is not the complete list.
#   noproviders  compile the bot seam with no providers (-DMP_NO_BOT_PROVIDERS=ON) in the ac-dev-server container
#                (persistent build volume, -k, --full forces recompiling module TUs). This is a COLD build that
#                takes hours the first time; run it once, in the background.
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
LOG="$MP_OUT/build-$VARIANT.log"
# BuildKit plain progress prefixes lines with "#NN 12.34 ", so these greps are not anchored.
ERR_RE="mod-mythic-plus/.*error:|ld\.lld: error|undefined reference"
mp_require_docker
mp_say "building worldserver ($VARIANT$([[ $FULL == 1 ]] && echo ', full'))"
cd "$MP_ROOT"
if [[ "$VARIANT" == default ]]; then
  rm -f "$LOG"
  mp_run_logged "$LOG" docker compose build --progress=plain ac-worldserver \
    || { rc=$?; grep -nE "$ERR_RE" "$LOG" > "$MP_OUT/errors-$VARIANT.txt" || true
         grep -E "mod-mythic-plus/.*warning:" "$LOG" | sed -E 's/^#[0-9]+ [0-9.]+ //' | sort -u \
           > "$MP_OUT/warnings-$VARIANT.txt" || true
         mp_fail "build ($VARIANT)" "$rc" "$LOG"; }
else
  EXTRA="-DMP_NO_BOT_PROVIDERS=ON"
  BUILD_DIR="/azerothcore/var/build/mp-$VARIANT"
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
    " || { rc=$?; grep -nE "$ERR_RE" "$LOG" > "$MP_OUT/errors-$VARIANT.txt" || true
           mp_fail "build ($VARIANT)" "$rc" "$LOG"; }
fi
mp_check_drift "$LOG"
if [[ $FULL == 1 || "$VARIANT" == default ]]; then
  grep -E "mod-mythic-plus/.*warning:" "$LOG" | sed -E 's/^#[0-9]+ [0-9.]+ //' | sort -u \
    > "$MP_OUT/warnings-$VARIANT.txt" || true
  mp_say "module warnings: $(wc -l < "$MP_OUT/warnings-$VARIANT.txt")"
fi
mp_say "PASS build ($VARIANT) — log: $LOG"
