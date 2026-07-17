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
python3 tests/verify_manual.py PACKAGES.md || exit 1

# stray-escape guard: the REPL renderer must consume markdown \| escapes
if ./neutrino 2>/dev/null <<< manual | grep -q 'on\\|off'; then
  echo "manual render: stray backslash-pipe leaked"; exit 1
fi
echo "manual render: escapes clean"

# worked-example tables in PACKAGES.md must match the interpreter
python3 tools/gen_package_tables.py --check || exit 1
