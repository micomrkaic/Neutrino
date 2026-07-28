#!/usr/bin/env bash
# The tour must always play: no errors, and the closing line intact.
set -e
cd "$(dirname "$0")/.."
out=$(printf 'load("packages/demo.nu")\n' | NEUTRINO_PLOT_TERM=ascii ./vmtest 2>&1)
echo "$out" | grep -q 'error' && { echo "demo: errors in the tour"; echo "$out" | grep error; exit 1; }
echo "$out" | grep -q 'Tour complete' || { echo "demo: closing line missing"; exit 1; }
echo "$out" | grep -q '=> 23 people' || { echo "demo: birthday act broken"; exit 1; }
echo "demo: the tour plays clean, end to end"
