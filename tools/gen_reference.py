#!/usr/bin/env python3
"""Regenerate MANUAL.md's builtin reference from eval.c's doc table.

Cell contents are markdown-escaped: a pipe inside a signature or description
becomes \\| so it cannot shatter the table (doclint guards this in make test).
Run from the repo root:  python3 tools/gen_reference.py
"""
import re
from collections import OrderedDict

TITLES = {
    "core": "Core & introspection", "io": "Data files", "solve": "Solvers",
    "plot": "Plotting", "make": "Array construction", "reduce": "Reductions",
    "array": "Array utilities", "math": "Mathematical functions",
    "linalg": "Linear algebra", "trig": "Trigonometric & hyperbolic",
    "complex": "Complex accessors", "random": "Random numbers",
    "test": "Predicates", "hof": "Higher-order functions",
    "string": "Strings", "repl": "REPL commands",
}

def md_cell(text):
    """Escape table-breaking characters for a markdown cell."""
    return text.replace("|", "\\|")

def main():
    src = open("eval.c").read()
    rows = re.findall(
        r'\{\s*"([a-z_0-9]+)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",'
        r'\s*"([a-z]+)"\s*,\s*"(?:[^"\\]|\\.)*"\s*\},', src)
    cats = OrderedDict()
    for name, sig, desc, cat in rows:
        sig = sig.replace('\\"', '"')
        desc = desc.replace('\\"', '"').replace("\\\\", "\\")
        cats.setdefault(cat, []).append((sig, desc))

    total = sum(len(v) for v in cats.values())
    ref = []
    for cat, items in cats.items():
        ref.append(f"### {TITLES.get(cat, cat)}\n")
        ref.append("| Signature | Description |\n|---|---|")
        for sig, desc in items:
            ref.append(f"| `{md_cell(sig)}` | {md_cell(desc)} |")
        ref.append("")

    m = open("MANUAL.md").read()
    pat = r"(## 17\. Builtin reference\n\n\*Generated[^*]*\*\n\n).*?(## 18\. Grammar summary)"
    if not re.search(pat, m, flags=re.S):
        raise SystemExit("gen_reference: reference section anchors not found")
    m2 = re.sub(pat, lambda mo: mo.group(1) + "\n".join(ref) + "\n" + mo.group(2), m, flags=re.S)
    if m2 == m:
        print(f"reference already current: {total} builtins, {len(cats)} sections")
    else:
        open("MANUAL.md", "w").write(m2)
        print(f"reference regenerated: {total} builtins, {len(cats)} sections, cells escaped")

if __name__ == "__main__":
    main()
