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
#   noproviders  compile the bot seam with no providers: temporarily flips `option(MP_NO_BOT_PROVIDERS ... OFF)`
#                to ON in mod-mythic-plus.cmake, runs one docker compose build of ac-worldserver under the tag
#                mpcheck-noprov, and restores the file (trap; verified with git diff --quiet). There is no
#                dev-server container. The warm ccache means only the module TUs recompile; the script requires
#                src/Bots/ files among the "Building CXX object" lines. If the layer was fully cached (rerun with no
#                source change) nothing compiled: it prints a NOTE and exits PASS without claiming verification,
#                like default's cached path (the earlier build already verified that source). Otherwise it writes
#                errors-/warnings-noproviders.txt like default. Then it removes the mpcheck-noprov image and the image ID it replaced (specific IDs only, never a prune).
#                Refuses to start when mod-mythic-plus.cmake already has uncommitted changes. --full is not
#                accepted here.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
VARIANT="default"; FULL=0
for a in "$@"; do
  case "$a" in
    default|noproviders) VARIANT="$a" ;;
    --full) FULL=1 ;;
    *) echo "usage: $0 [default|noproviders] [--full]  (--full = --no-cache; default only)" >&2; exit 2 ;;
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
  # noproviders: flip the option for one compose build, always restore the file.
  CMAKE_FILE="$MP_MOD_DIR/mod-mythic-plus.cmake"
  [[ $FULL == 0 ]] || { echo "mp: --full is not supported with noproviders" >&2; exit 2; }
  git -C "$MP_MOD_DIR" diff --quiet -- mod-mythic-plus.cmake \
    || { echo "mp: mod-mythic-plus.cmake has uncommitted changes; recover with: git checkout -- mod-mythic-plus.cmake (run in the module dir)" >&2; exit 2; }
  grep -qE '^option\(MP_NO_BOT_PROVIDERS .* OFF\)' "$CMAKE_FILE" \
    || { echo "mp: option(MP_NO_BOT_PROVIDERS ... OFF) not found in mod-mythic-plus.cmake" >&2; exit 2; }
  export DOCKER_IMAGE_TAG="mpcheck-noprov"
  NOPROV_IMAGE="acore/ac-wotlk-worldserver:$DOCKER_IMAGE_TAG"
  CMAKE_BACKUP="$(mktemp)"
  OLD_IMAGE_ID="$(docker image inspect -f '{{.Id}}' "$NOPROV_IMAGE" 2>/dev/null || true)"
  cp "$CMAKE_FILE" "$CMAKE_BACKUP"
  mp_noprov_cleanup()
  {
    local rc=$?
    trap - EXIT
    cp "$CMAKE_BACKUP" "$CMAKE_FILE"; rm -f "$CMAKE_BACKUP"
    if ! git -C "$MP_MOD_DIR" diff --quiet -- mod-mythic-plus.cmake; then
      echo "mp: ERROR mod-mythic-plus.cmake differs from HEAD after restore" >&2; rc=1
    fi
    # Remove only the image this run produced and the one it replaced, by ID. Never a prune.
    local new_id; new_id="$(docker image inspect -f '{{.Id}}' "$NOPROV_IMAGE" 2>/dev/null || true)"
    [[ -z "$new_id" ]] || docker rmi "$NOPROV_IMAGE" >/dev/null 2>&1 || true
    [[ -z "$new_id" ]] || docker rmi "$new_id" >/dev/null 2>&1 || true
    if [[ -n "$OLD_IMAGE_ID" && "$OLD_IMAGE_ID" != "$new_id" ]]; then
      docker rmi "$OLD_IMAGE_ID" >/dev/null 2>&1 || true
    fi
    exit "$rc"
  }
  trap mp_noprov_cleanup EXIT
  sed -i -E 's/^(option\(MP_NO_BOT_PROVIDERS .*) OFF\)/\1 ON)/' "$CMAKE_FILE"
  grep -qE '^option\(MP_NO_BOT_PROVIDERS .* ON\)' "$CMAKE_FILE" || mp_fail "flip MP_NO_BOT_PROVIDERS" 1 "$CMAKE_FILE"
  rm -f "$LOG"
  mp_run_logged "$LOG" docker compose build --progress=plain ac-worldserver \
    || { rc=$?; grep -nE "$ERR_RE" "$LOG" > "$MP_OUT/errors-$VARIANT.txt" || true
         mp_extract_warnings "$LOG" "$MP_OUT/warnings-$VARIANT.txt"
         mp_fail "build ($VARIANT)" "$rc" "$LOG"; }
  grep -nE "$ERR_RE" "$LOG" > "$MP_OUT/errors-$VARIANT.txt" || true
  NOPROV_CACHED=0
  if ! grep -qE "Building CXX object .*mod-mythic-plus/src/Bots/" "$LOG"; then
    NOPROV_CACHED=1
    mp_say "NOTE — build layer cached; no compile signal (noproviders unchanged since last verified build; change module source or rerun after a source edit)"
  else
    mp_say "src/Bots TUs compiled: $(grep -cE 'Building CXX object .*mod-mythic-plus/src/Bots/' "$LOG")"
  fi
fi
mp_check_drift "$LOG"
WARN_FILE="$MP_OUT/warnings-$VARIANT.txt"
if [[ "$VARIANT" == default && "$(grep -cE "$COMPILE_RE" "$LOG" || true)" == 0 ]]; then
  rm -f "$WARN_FILE" "$MP_OUT/warnings-new-$VARIANT.txt"
  mp_say "NOTE — build layer cached; no warning signal (rerun with --full)"
elif [[ "$VARIANT" == noproviders && "${NOPROV_CACHED:-0}" == 1 ]]; then
  rm -f "$WARN_FILE" "$MP_OUT/warnings-new-$VARIANT.txt"
else
  mp_extract_warnings "$LOG" "$WARN_FILE"
  mp_say "module warnings: $(wc -l < "$WARN_FILE")"
  if [[ -f "$WARN_BASE" ]]; then
    mp_compare_baseline "$WARN_FILE" "$WARN_BASE" "$MP_OUT/warnings-new-$VARIANT.txt"
  fi
fi
mp_say "PASS build ($VARIANT) — log: $LOG"
