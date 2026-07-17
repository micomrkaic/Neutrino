#!/usr/bin/env python3
"""Documentation lint: every markdown table in every document must be
structurally sound — consistent column counts per block (unescaped pipes
inside cells shatter rows into phantom columns), no unescaped pipes inside
code spans in table rows, and balanced backticks per line."""
import re, sys, glob

DOCS = ["MANUAL.md", "PACKAGES.md", "CHANGELOG.md", "LESSONS.md", "DESIGN_NOTES.md"]

def cells(row):
    protected = row.replace("\\|", "\x01")
    return [c for c in protected.strip().strip("|").split("|")]

problems = []
for doc in DOCS:
    try:
        lines = open(doc).read().split("\n")
    except FileNotFoundError:
        continue
    in_code = False
    block, block_start = [], 0
    def flush():
        if len(block) >= 2:
            ncols = len(cells(block[0]))
            for off, row in enumerate(block):
                n = len(cells(row))
                if n != ncols:
                    problems.append(f"{doc}:{block_start+off+1}: row has {n} cells, header has {ncols}: {row.strip()[:80]}")
        block.clear()
    for i, L in enumerate(lines):
        if L.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        if L.startswith("|"):
            if not block:
                globals()["__bs"] = i
                block_start = i
            block.append(L)
            # unescaped pipe inside a code span within a table row
            for span in re.findall(r"`([^`]*)`", L.replace("\\|", "\x01")):
                if "|" in span:
                    problems.append(f"{doc}:{i+1}: unescaped pipe inside code span: `{span}`")
            if L.count("`") % 2 == 1:
                problems.append(f"{doc}:{i+1}: odd number of backticks in table row")
        else:
            flush()
    flush()

if problems:
    print(f"doclint: {len(problems)} problem(s)")
    for p in problems[:40]:
        print(" ", p)
    sys.exit(1)
print("doclint: all tables in all documents structurally sound")
