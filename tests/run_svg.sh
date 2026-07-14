#!/usr/bin/env bash
# SVG plot backend smoke: NEUTRINO_PLOT_TERM=svg writes well-formed SVG files.
set -e
cd "$(dirname "$0")/.."
rm -f plot_*.svg
printf 'plot(1:20, [sin(1:20); cos(1:20)]'"'"', {title = "t & <q>", labels = ["a", "b"]})\nhist(randn(200, 1), 10)\n' | NEUTRINO_PLOT_TERM=svg ./vmtest >/dev/null
python3 - << 'PY'
import xml.dom.minidom
d1 = xml.dom.minidom.parse('plot_1.svg')
assert len(d1.getElementsByTagName('polyline')) == 2, "want 2 series polylines"
s = open('plot_1.svg').read()
assert 't &amp; &lt;q&gt;' in s and '>a<' in s and '>b<' in s, "escape/legend"
d2 = xml.dom.minidom.parse('plot_2.svg')
assert len(d2.getElementsByTagName('rect')) >= 11, "want histogram bars"
print("svg: plot + hist backends well-formed, escaped, legended")
PY
rm -f plot_*.svg
