#!/usr/bin/env bash
# Verify that every '%=' help example matches the interpreter.
set -u
cd "$(dirname "$0")/.."
[[ -x ./vmtest ]] || { echo "run_examples: ./vmtest not built" >&2; exit 2; }
exec python3 tests/verify_examples.py
