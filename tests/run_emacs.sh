#!/usr/bin/env bash
# Emacs-mode batch tests; skips politely when emacs is unavailable.
cd "$(dirname "$0")/.."
command -v emacs >/dev/null 2>&1 || { echo "emacs-mode: emacs not installed, skipping"; exit 0; }
python3 tools/gen_emacs_mode.py --check || exit 1
emacs --batch -Q -l tests/run_emacs.el 2>&1 | grep '^emacs-mode' || exit 1
