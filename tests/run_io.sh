#!/usr/bin/env bash
# input() and pause() read the keyboard; under a pipe they consume the
# next stdin line — which makes them shell-testable, if never golden-able.
set -e
cd "$(dirname "$0")/.."
out=$(printf 'let s = input("name? "); upper(s)\nmico\n' | ./vmtest 2>&1)
echo "$out" | grep -q '"MICO"' || { echo "io: input broken"; exit 1; }
echo "$out" | grep -q 'name? ' || { echo "io: prompt missing"; exit 1; }
out=$(printf 'pause("[w] ")\n\nprint("resumed")\n' | ./vmtest 2>&1)
echo "$out" | grep -q 'resumed' || { echo "io: pause did not resume"; exit 1; }
echo "io: input and pause behave at the pipe"
