#!/usr/bin/env bash
# Inner-loop build of worldserver.
# Usage: tools/mp-build.sh [default|noproviders] [--full]
#   default      docker compose build of ac-worldserver (tag $DOCKER_IMAGE_TAG), reusing the warm BuildKit ccache
#                mount: takes minutes. When source changes invalidate the module layer, the module TUs are
#                recompiled (ccache replays warnings for the TUs that hit). When the layer is fully cached the log
#                has no compiler output, so there is no warning signal: warnings-default.txt is removed and a NOTE
#                is printed. --full passes --no-cache to docker compose build (slow, but always compiles every
#                module TU and so yields a real warning list). Limitation: the image build stops at the first
#                failing batch (no -k; the root Dockerfile is off-limits), so errors-default.txt is not the
#                complete list.
#   Warnings are compared with $MP_OUT/warnings-baseline.txt (when present) after stripping ":line:col:";
#   new ones are listed and written to warnings-new-<variant>.txt. Report only, the build does not fail.
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
COMPILE_RE="Building CXX object .*mod-mythic-plus"
WARN_BASE="$MP_OUT/warnings-baseline.txt"

# mp_extract_warnings <log> <out>: module warnings, BuildKit step prefix stripped, sorted unique.
mp_extract_warnings()
{
  grep -E "mod-mythic-plus/.*warning:" "$1" | sed -E 's/^#[0-9]+ [0-9.]+ //' | sort -u > "$2" || true
}

# mp_norm_warnings <file>: strip ":<line>:<col>:" so line shifts do not look like new warnings.
mp_norm_warnings()
{
  sed -E 's/:[0-9]+:[0-9]+: /: /' "$1" | sort -u
}

# mp_compare_baseline <warnings> <baseline> <new-out>: normalised comm -13; prints the count and the list.
mp_compare_baseline()
{
  comm -13 <(mp_norm_warnings "$2") <(mp_norm_warnings "$1") > "$3"
  mp_say "new warnings vs baseline: $(wc -l < "$3")"
  cat "$3"
}
[[ -n "${MP_BUILD_SOURCE_ONLY:-}" ]] && return 0
mp_require_docker
mp_say "building worldserver ($VARIANT$([[ $FULL == 1 ]] && echo ', full'))"
cd "$MP_ROOT"
if [[ "$VARIANT" == default ]]; then
  rm -f "$LOG"
  NOCACHE=(); [[ $FULL == 1 ]] && NOCACHE=(--no-cache)
  mp_run_logged "$LOG" docker compose build ${NOCACHE[@]+"${NOCACHE[@]}"} --progress=plain ac-worldserver \
    || { rc=$?; grep -nE "$ERR_RE" "$LOG" > "$MP_OUT/errors-$VARIANT.txt" || true
         mp_extract_warnings "$LOG" "$MP_OUT/warnings-$VARIANT.txt"
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
  WARN_FILE="$MP_OUT/warnings-$VARIANT.txt"
  if [[ "$VARIANT" == default && "$(grep -cE "$COMPILE_RE" "$LOG" || true)" == 0 ]]; then
    rm -f "$WARN_FILE" "$MP_OUT/warnings-new-$VARIANT.txt"
    mp_say "NOTE — build layer cached; no warning signal (rerun with --full)"
  else
    mp_extract_warnings "$LOG" "$WARN_FILE"
    mp_say "module warnings: $(wc -l < "$WARN_FILE")"
    if [[ "$VARIANT" == default && -f "$WARN_BASE" ]]; then
      mp_compare_baseline "$WARN_FILE" "$WARN_BASE" "$MP_OUT/warnings-new-$VARIANT.txt"
    fi
  fi
fi
mp_say "PASS build ($VARIANT) — log: $LOG"
