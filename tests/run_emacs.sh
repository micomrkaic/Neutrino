#!/usr/bin/env bash
# Emacs-mode batch tests; skips politely when emacs is unavailable.
cd "$(dirname "$0")/.."
# the drift lint needs only python — it gates EVERYWHERE; only the
# byte-compile below is allowed to skip when emacs is absent
python3 tools/gen_emacs_mode.py --check || exit 1
command -v emacs >/dev/null 2>&1 || { echo "emacs-mode: keyword list current; emacs not installed, byte-compile skipped"; exit 0; }
emacs --batch -Q -l tests/run_emacs.el 2>&1 | grep '^emacs-mode' || exit 1
