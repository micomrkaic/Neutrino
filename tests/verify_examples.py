#!/usr/bin/env python3
"""Verify help examples: every doc-table example line whose comment uses the
'%=' marker is executed through vmtest and its output compared to the text
after the marker. Plain '%' comments are prose and skipped."""
import re, subprocess, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
src = open('eval.c').read()
rows = re.findall(r'\{\s*"([a-z_0-9]+)",\s*"(?:[^"\\]|\\.)*",\s*"(?:[^"\\]|\\.)*",\s*"[a-z]+"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\},', src)
ok = bad = checked = 0
for name, ex in rows:
    ex = ex.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')
    for line in ex.split('\n'):
        if '%=' not in line:
            continue
        checked += 1
        expr, _, expected = line.partition('%=')
        # a trailing prose comment may follow the expected value
        expected = expected.split('%')[0].strip()
        r = subprocess.run(['./vmtest'], input=expr.rstrip() + '\n',
                           capture_output=True, text=True, timeout=20)
        got = (r.stdout + r.stderr).strip()
        # multi-statement example lines echo intermediates ending with the value
        last = got.split('\n')[-1].strip()
        if last == expected:
            ok += 1
        else:
            bad += 1
            print(f"EXAMPLE MISMATCH ({name}): {expr.strip()}\n  claimed: {expected!r}\n  actual : {last!r}")
print(f"examples: {ok} of {checked} verified")
sys.exit(1 if bad else 0)
