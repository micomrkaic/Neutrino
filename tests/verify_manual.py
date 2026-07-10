#!/usr/bin/env python3
"""Verify MANUAL.md: run every `neutrino> ` transcript line through vmtest
(one session per fenced block) and check the shown output. Aligned multi-line
matrices in the manual are compared against vmtest's single-line form."""
import re, subprocess, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
doc = sys.argv[1] if len(sys.argv) > 1 else 'MANUAL.md'
text = open(doc).read()
blocks = re.findall(r'```[a-z]*\n(.*?)```', text, re.S)
ok = bad = total = 0
for b in blocks:
    lines = b.rstrip('\n').split('\n')
    entries, i = [], 0
    while i < len(lines):
        if lines[i].startswith('neutrino> '):
            inp = lines[i][len('neutrino> '):]
            exp = []
            i += 1
            while i < len(lines) and not lines[i].startswith('neutrino> '):
                exp.append(lines[i]); i += 1
            entries.append((inp, '\n'.join(exp).rstrip()))
        else:
            i += 1
    if not entries:
        continue
    prog = ''.join(inp + '\n"@@S@@"\n' for inp, _ in entries)
    r = subprocess.run(['./vmtest'], input=prog, capture_output=True, text=True, timeout=30)
    parts = r.stdout.split('"@@S@@"')
    err_lines = r.stderr.splitlines()
    ei = 0
    for k, (inp, exp) in enumerate(entries):
        total += 1
        got = parts[k].strip().strip('"').strip() if k < len(parts) else '<missing>'
        exp_clean = '\n'.join(re.sub(r'\s+#.*$', '', l).rstrip() for l in exp.split('\n')).rstrip()
        if exp_clean.startswith('error:'):
            got = (re.sub(r'^\s*(?:parse )?error at \d+:\d+: ', 'error: ', err_lines[ei])
                   if ei < len(err_lines) else got)
            ei += 1
        def canon(s):
            s = s.strip()
            if s.startswith('[') and '\n' in s:
                rows = [q.strip().strip('[]').strip() for q in s.split('\n')]
                return '[' + '; '.join(', '.join(q.split()) for q in rows) + ']'
            return s
        if exp_clean == '' or got == exp_clean or canon(exp_clean) == canon(got) \
           or got == exp_clean.strip('"'):
            ok += 1
        else:
            bad += 1
            print(f"MANUAL MISMATCH: {inp}\n  manual : {exp_clean!r}\n  actual : {got!r}")
print(f"{doc}: {ok} of {total} transcript examples verified")
sys.exit(1 if bad else 0)
