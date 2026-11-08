#!/usr/bin/env bash
# Docstring examples are documentation, so they are executed: every
# "# e.g.  EXPR %= EXPECTED" line in packages/*.nu is run with its
# package loaded and the output must match EXPECTED exactly.
set -u
cd "$(dirname "$0")/.."
python3 - << 'PY'
import re, subprocess, sys, glob, os
fails = total = 0
for path in sorted(glob.glob('packages/*.nu')):
    pkg = os.path.basename(path)[:-3]
    for ln in open(path):
        m = re.match(r'#\s*e\.g\.\s+(.*?)\s+%=\s+(.*?)\s*$', ln)
        if not m: continue
        expr, want = m.groups()
        total += 1
        prog = f'load("{pkg}")\n{expr}\n'
        r = subprocess.run(['./vmtest'], input=prog, capture_output=True, text=True,
                           env=dict(os.environ, NEUTRINO_PLOT_TERM='ascii'))
        got = r.stdout.strip().split('\n')[-1] if r.stdout.strip() else '(no output)'
        if got != want:
            print(f"FAIL pkgdoc {pkg}: {expr}\n      want: {want}\n      got:  {got}")
            fails += 1
# and the rendering: help(f) must serve sig, description, provenance, example
checks = [
  ('finance', 'help(npv)', ['  npv(r, cfs)', 'net present value', 'package function (packages/finance.nu), 2 arguments', 'e.g.  npv(0.10, [-100, 30, 40, 50]) % -2.10368']),
  ('dist',    'help(norm)', ['  norm', 'the normal distribution', 'from packages/dist.nu', 'e.g.  norm.cdf(1.96, 0, 1) % 0.975002']),
]
for pkg, expr, wants in checks:
    r = subprocess.run(['./vmtest'], input=f'load("{pkg}")\n{expr}\n', capture_output=True, text=True,
                       env=dict(os.environ, NEUTRINO_PLOT_TERM='ascii'))
    for w in wants:
        total += 1
        if w not in r.stdout:
            print(f"FAIL pkgdoc render {pkg}: missing {w!r}"); fails += 1
print(f"pkgdoc: {total - fails} of {total} docstring examples and renderings verified" if not fails
      else f"pkgdoc: {fails} of {total} FAILED")
sys.exit(1 if fails else 0)
PY
