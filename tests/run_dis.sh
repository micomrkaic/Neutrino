#!/usr/bin/env bash
# Codegen goldens: disassemble each tests/dis/*.nu and diff against its .golden.
# These lock in the *emitted bytecode*, not just behaviour — e.g. the loop
# MARK_PUSH/RESET/POP sequence. Regenerate after an intentional codegen change:
#   for f in tests/dis/*.nu; do ./neutrino --dis "$f" > "${f%.nu}.golden"; done
set -u
cd "$(dirname "$0")/.."
BIN="${NEUTRINO:-./neutrino}"
[[ -x "$BIN" ]] || { echo "run_dis: $BIN not built" >&2; exit 2; }
for src in *.c *.h; do
  [[ -e "$src" && "$src" -nt "$BIN" ]] && { echo "run_dis: WARNING: $src newer than $BIN (stale binary?)" >&2; break; }
done
fails=0; total=0
for nu in tests/dis/*.nu; do
  total=$((total+1))
  gold="${nu%.nu}.golden"
  if ! diff -u "$gold" <("$BIN" --dis "$nu") > /tmp/dis_diff.$$ 2>&1; then
    echo "FAIL $(basename "$nu"): disassembly differs from golden:"
    sed 's/^/      /' /tmp/dis_diff.$$
    fails=$((fails+1))
  fi
done
rm -f /tmp/dis_diff.$$
echo "codegen: $((total-fails)) of $total disassembly goldens match"
exit $((fails > 0))
