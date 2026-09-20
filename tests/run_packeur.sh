#!/usr/bin/env bash
# packeur, driven headless: keys ride the same stdin as the program
# (the pause precedent). Three scenarios: load via arrows, clear via
# toggle-off, cancel at EOF.
set -u
fail=0
out=$(printf 'packeur\n\033[B\033[B \nnorm.cdf(0, 0, 1)\n' | ./vmtest 2>&1)
echo "$out" | grep -q 'packeur: loaded dist.nu' || { echo "FAIL packeur load summary"; fail=1; }
echo "$out" | grep -q '^0.5$' || { echo "FAIL packeur load effect (norm.cdf)"; fail=1; }
out=$(printf 'load("dist")\npackeur\n\033[B\033[B \nwho\n' | ./vmtest 2>&1)
echo "$out" | grep -q 'packeur: cleared dist.nu' || { echo "FAIL packeur clear summary"; fail=1; }
echo "$out" | grep -q '(no variables defined)' || { echo "FAIL packeur clear effect (who)"; fail=1; }
out=$(printf 'packeur\n' | ./vmtest 2>&1)
echo "$out" | grep -q 'packeur: cancelled' || { echo "FAIL packeur EOF cancel"; fail=1; }
out=$(printf 'packeur\njk \nwho("astro")\n' | ./vmtest 2>&1)
echo "$out" | grep -q 'packeur: loaded astro.nu' || { echo "FAIL packeur jk keys"; fail=1; }
[ $fail -eq 0 ] && echo "packeur: 4 scenarios clean"
exit $fail
