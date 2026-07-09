#!/usr/bin/env bash
# Input lines are unbounded (getline): a >8 KB matrix literal must parse.
# Regression for the old fgets 8192-byte buffer that silently truncated.
set -u
cd "$(dirname "$0")/.."
VMTEST="${VMTEST:-./vmtest}"
[[ -x "$VMTEST" ]] || { echo "run_longline: $VMTEST not built" >&2; exit 2; }
out="$(python3 -c "
row = ', '.join(['1.234567890123456'] * 40)
print('let A = [' + '; '.join([row] * 25) + ']; size(A)')" | "$VMTEST" 2>&1)"
if [[ "$out" == "[25, 40]" ]]; then echo "longline: 20 KB literal parses"
else echo "longline FAIL: got: $out" >&2; exit 1; fi
