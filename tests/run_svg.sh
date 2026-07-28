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
# scatter.nu rides the frozen style=points path: circles + title, no C
rm -f plot_*.svg
printf 'load("packages/scatter.nu")\nrng(2); scatter_titled(rand(1, 25), rand(1, 25), "pkg scatter")\n' | NEUTRINO_PLOT_TERM=svg ./vmtest >/dev/null
python3 - << 'PY'
import xml.dom.minidom
d = xml.dom.minidom.parse('plot_1.svg')
assert len(d.getElementsByTagName('circle')) == 25, "want 25 scatter circles"
assert 'pkg scatter' in open('plot_1.svg').read(), "want title"
print("svg: scatter.nu renders circles via style=points")
PY
# marker-family unification: "circle" means circles on every backend
rm -f plot_*.svg
printf 'plot(1:6, (1:6) .^ 2, {style = "circle"})\n' | NEUTRINO_PLOT_TERM=svg ./vmtest >/dev/null
python3 - << 'PY'
import xml.dom.minidom
d = xml.dom.minidom.parse('plot_1.svg')
assert len(d.getElementsByTagName('circle')) == 6, "want circle-style markers"
print("svg: marker family (circle/points/dots) unified")
PY
rm -f plot_*.svg
