#!/usr/bin/env python3
"""Generate Appendix E of BOOK.md: an alphabetical index of every builtin
and constant, extracted from eval.c's documentation table (the single
source of truth). --check verifies the book is current without writing."""
import re, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))

TITLES = {
    "core": "core", "const": "constant", "math": "math", "trig": "trig",
    "complex": "complex", "make": "arrays", "array": "arrays",
    "linalg": "linear algebra", "reduce": "reductions", "hof": "functional",
    "string": "strings", "random": "random", "solve": "solvers",
    "io": "files", "num": "numeric", "fmt": "output", "repl": "repl",
}

def esc(s):
    s = s.replace('\\"', '"')
    return s.replace('|', '\\|')

def build():
    src = open('eval.c').read()
    rows = re.findall(
        r'\{\s*"([a-z_0-9]+)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",'
        r'\s*"([a-z]+)"\s*,\s*"(?:[^"\\]|\\.)*"\s*\},', src)
    rows.sort(key=lambda r: r[0])
    out = ["| Name | Signature | Description | Area |", "|---|---|---|---|"]
    for name, sig, desc, cat in rows:
        out.append(f"| `{name}` | `{esc(sig)}` | {esc(desc)} | {TITLES.get(cat, cat)} |")
    out.append("")
    out.append(f"*{len(rows)} names; the same table drives `help`, tab "
               "completion, the reference, and the Emacs mode.*")
    return "\n".join(out)

def main():
    check = '--check' in sys.argv
    body = build()
    m = open('BOOK.md').read()
    pat = r'(<!-- INDEX:BEGIN -->\n).*?(\n<!-- INDEX:END -->)'
    if not re.search(pat, m, flags=re.S):
        print("book index: markers not found in BOOK.md"); sys.exit(1)
    m2 = re.sub(pat, lambda mo: mo.group(1) + body + mo.group(2), m, flags=re.S)
    if check:
        if m2 != m:
            print("book index: STALE — run tools/gen_book_index.py"); sys.exit(1)
        print("book index: current")
    else:
        open('BOOK.md', 'w').write(m2)
        print("book index: regenerated" if m2 != m else "book index: already current")

if __name__ == "__main__":
    main()
