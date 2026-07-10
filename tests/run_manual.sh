#!/usr/bin/env bash
# Verify that every REPL transcript in MANUAL.md matches the interpreter.
set -u
cd "$(dirname "$0")/.."
[[ -x ./vmtest ]] || { echo "run_manual: ./vmtest not built" >&2; exit 2; }
for src in *.c *.h; do
  [[ "$src" == repl.c || "$src" == main.c ]] && continue
  [[ -e "$src" && "$src" -nt ./vmtest ]] && { echo "run_manual: WARNING: $src newer than ./vmtest (stale binary?)" >&2; break; }
done
python3 tests/verify_manual.py MANUAL.md || exit 1
exec python3 tests/verify_manual.py PACKAGES.md
