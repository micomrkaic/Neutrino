#!/usr/bin/env bash
# The -e flag and the XDG history home.
set -u
cd "$(dirname "$0")/.."
fail=0
[ "$(./neutrino -e '2 + 2')" = "4" ] || { echo "FAIL cli: -e arithmetic"; fail=1; }
out=$(./neutrino -e 'load("dist"); norm.cdf(1.96, 0, 1)')
[ "$out" = "0.975002" ] || { echo "FAIL cli: -e with package load"; fail=1; }
./neutrino -e 'let x = )' >/dev/null 2>&1; [ $? -eq 1 ] || { echo "FAIL cli: -e parse error exit"; fail=1; }
./neutrino -e >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL cli: -e missing arg exit"; fail=1; }
./neutrino -e '1' x.nu >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL cli: -e + file conflict exit"; fail=1; }
H=$(mktemp -d)
printf '1+1\nquit\n' | HOME="$H" ./neutrino >/dev/null 2>&1
[ -f "$H/.local/state/neutrino/history" ] || { echo "FAIL cli: history not in XDG state dir"; fail=1; }
grep -q '1+1' "$H/.local/state/neutrino/history" || { echo "FAIL cli: quit lost the history"; fail=1; }
echo "let legacy = 1" > "$H/.neutrino_history"; rm -rf "$H/.local"
printf '\n' | HOME="$H" ./neutrino >/dev/null 2>&1
grep -q 'legacy' "$H/.local/state/neutrino/history" || { echo "FAIL cli: old history not migrated"; fail=1; }
rm -rf "$H"
[ $fail -eq 0 ] && echo "cli: -e and history home clean"
exit $fail
