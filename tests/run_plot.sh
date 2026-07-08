#!/usr/bin/env bash
# Plot smoke test: render a PNG through gnuplot and check it is non-trivial.
# Skips (successfully) when gnuplot is not installed — it is a soft dependency.
set -u
cd "$(dirname "$0")/.."
command -v gnuplot >/dev/null 2>&1 || { echo "plot: gnuplot not installed — skipped"; exit 0; }
[[ -x ./vmtest ]] || { echo "run_plot: ./vmtest not built" >&2; exit 2; }
out=$(mktemp /tmp/neutrino_plot.XXXXXX.png)
trap 'rm -f "$out"' EXIT
NEUTRINO_PLOT_TERM="pngcairo size 480,320" NEUTRINO_PLOT_OUT="$out" ./vmtest > /dev/null 2>&1 << 'EOF'
let x = linspace(0, 6.28, 100)
plot(x, map(sin, x), {title = "smoke", grid = true})
EOF
size=$(stat -c %s "$out" 2>/dev/null || stat -f %z "$out" 2>/dev/null || echo 0)
if [[ "$size" -gt 2000 ]]; then echo "plot: PNG smoke test OK ($size bytes)"; exit 0; fi
echo "plot: FAILED (png $size bytes)"; exit 1
