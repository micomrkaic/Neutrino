#!/usr/bin/env bash
# Verify ASCII plotting (NEUTRINO_PLOT_TERM=ascii): deterministic text output,
# so we can assert on exact structure. Native-independent of gnuplot.
set -u
cd "$(dirname "$0")/.."
VMTEST="${VMTEST:-./vmtest}"
[[ -x "$VMTEST" ]] || { echo "run_ascii_plot: $VMTEST not built" >&2; exit 2; }
fail=0

check() {  # desc | program | needle
    local desc=$1 prog=$2 needle=$3
    local out
    out="$(printf '%s\n' "$prog" | NEUTRINO_PLOT_TERM=ascii "$VMTEST" 2>&1)"
    if ! grep -qF -- "$needle" <<<"$out"; then
        echo "ASCII FAIL: $desc"
        echo "  program: $prog"
        echo "  wanted substring: $needle"
        fail=1
    fi
}

# A line plot renders an axis and the x-range endpoints.
check "axis"        'plot([0, 5, 10])'                              '+------'
check "xrange-hi"   'plot([0, 5, 10])'                              '3'
check "title"       'plot([1,2,3], {title = "hello"})'              '  hello'
# Histogram shows bar counts.
check "hist-count"  'hist([1, 1, 1, 2], 2)'                         '# '
# Multi-series legend.
check "legend"      'plot(1:3, [1,2;2,4;3,6], {label1 = "a"})'      '* series 1 (a)'
# Error paths.
check "nofinite"    'plot([0/0, 0/0])'                              'no finite data'
check "empty"       'plot([])'                                      'empty data'

if [[ $fail == 0 ]]; then echo "ascii-plot: all checks passed"; else exit 1; fi
