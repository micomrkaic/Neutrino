#!/usr/bin/env python3
"""Regenerate (or --check) the builtin-name list in editors/neutrino-mode.el
from eval.c's documentation table, so highlighting cannot drift from the
interpreter."""
import re, sys

def builtin_names():
    src = open("eval.c").read()
    names = re.findall(
        r'\{\s*"([a-z_0-9]+)",\s*"(?:[^"\\]|\\.)*",\s*"(?:[^"\\]|\\.)*",\s*"[a-z]+"\s*,', src)
    return sorted(set(names))

def main():
    check = "--check" in sys.argv
    names = builtin_names()
    lisp = "\n    ".join('"' + n + '"' for n in names)
    s = open("editors/neutrino-mode.el").read()
    pat = re.compile(r"(\(defconst neutrino--builtins\n  '\()[^)]*(\)\n  \"Builtin names)", re.S)
    m = pat.search(s)
    if not m:
        raise SystemExit("gen_emacs_mode: builtin defconst not found")
    s2 = pat.sub(lambda mo: mo.group(1) + lisp + mo.group(2), s)
    if check:
        if s2 != s:
            raise SystemExit(f"gen_emacs_mode: DRIFT — {len(names)} builtins in eval.c, list is stale")
        print(f"emacs-mode builtins: current ({len(names)} names)")
    elif s2 != s:
        open("editors/neutrino-mode.el", "w").write(s2)
        print(f"emacs-mode builtins: regenerated ({len(names)} names)")
    else:
        print(f"emacs-mode builtins: already current ({len(names)} names)")

if __name__ == "__main__":
    main()
