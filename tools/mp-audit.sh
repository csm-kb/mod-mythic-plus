#!/usr/bin/env bash
# Static audit snapshot for before/after diffs (spec §3.3d). Usage: tools/mp-audit.sh <label>
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
LABEL="${1:?usage: $0 <label>}"; SRC="$MP_MOD_DIR/src"; OUT="$MP_OUT/audit-$LABEL.txt"
{
  echo "## registered script names"
  grep -rhoE '[A-Za-z]+Script\("[A-Za-z0-9_]+"\)' "$SRC" | sort -u
  echo "## registration calls"
  grep -rhoE '\b(Add_MP_[A-Za-z]+|AddSC_[A-Za-z]+|MP_Register_[A-Za-z]+)\(\);' "$SRC" | sort | uniq -c
  echo "## hook definitions (name: count)"
  grep -rhoE '\b(On[A-Z][A-Za-z]+|Modify[A-Za-z]+|modifyIncomingDmgHeal)\(' "$SRC/Scripts" \
    | sed -E 's/\($//' | sort | uniq -c
} > "$OUT"
mp_say "audit written: $OUT"
