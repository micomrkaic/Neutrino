#!/usr/bin/env bash
# Neutrino regression runner.
#
# Each .test file is a flat list of cases. A case is one input line followed by
# its expectation line:
#
#     <input>
#     => <exact stdout>        value test: stdout must equal this, stderr empty
#     <input>
#     => !<substring>          error test: stdout empty, stderr must contain this
#
# '#' lines and blank lines are ignored. Inputs are fed to ./vmtest, which reads
# a line, evaluates it, and prints the value of the last (non-';'-suppressed)
# statement — so one input line yields one output line.
#
# Modes:
#   run.sh                 golden comparison; exits nonzero if any test fails
#   run.sh --asan          run every input through ./vmtest-asan and fail on any
#                          AddressSanitizer / LeakSanitizer / UBSan diagnostic
set -u
cd "$(dirname "$0")/.."                      # project root

ASAN=0
[[ "${1:-}" == "--asan" ]] && ASAN=1
VMTEST="${VMTEST:-./vmtest}"
[[ $ASAN == 1 ]] && VMTEST="${VMTEST_ASAN:-./vmtest-asan}"

if [[ ! -x "$VMTEST" ]]; then echo "runner: $VMTEST not built" >&2; exit 2; fi
for src in *.c *.h; do
  [[ "$src" == repl.c || "$src" == main.c ]] && continue
  [[ -e "$src" && "$src" -nt "$VMTEST" ]] && { echo "runner: WARNING: $src is newer than $VMTEST (stale binary? run 'make test' / 'make test-asan')" >&2; break; }
done

total=0 passed=0 failed=0
RED=$'\033[31m'; GRN=$'\033[32m'; DIM=$'\033[2m'; RST=$'\033[0m'
[[ -t 1 ]] || { RED=""; GRN=""; DIM=""; RST=""; }
errf="$(mktemp)"; trap 'rm -f "$errf"' EXIT

rstrip() { printf '%s' "$1" | sed -e 's/[[:space:]]*$//'; }

run_one() {
    local input=$1 exp=$2 file=$3
    total=$((total+1))
    local out err
    out="$(printf '%s\n' "$input" | "$VMTEST" 2>"$errf")"
    err="$(cat "$errf")"

    if [[ $ASAN == 1 ]]; then                # memory-safety sweep only
        if grep -qE 'AddressSanitizer|LeakSanitizer|runtime error:' "$errf"; then
            failed=$((failed+1))
            printf '%sASAN%s %s\n      %s\n' "$RED" "$RST" "$file" "$input"
            grep -E 'SUMMARY|ERROR|runtime error:' "$errf" | head -2 | sed 's/^/      /'
        else passed=$((passed+1)); fi
        return
    fi

    out="$(rstrip "$out")"
    local ok=0
    if [[ "$exp" == '!'* ]]; then            # error expected
        local sub="${exp#!}"
        [[ -z "$out" && "$err" == *"$sub"* ]] && ok=1
    else                                     # value expected
        [[ "$out" == "$exp" && -z "$err" ]] && ok=1
    fi

    if [[ $ok == 1 ]]; then passed=$((passed+1))
    else
        failed=$((failed+1))
        printf '%sFAIL%s %s\n      in:   %s\n      want: %s\n      got:  stdout=[%s] stderr=[%s]\n' \
               "$RED" "$RST" "$file" "$input" "$exp" "$out" "$(rstrip "$err")"
    fi
}

run_file() {
    local f=$1 input="" have=0
    while IFS= read -r raw || [[ -n "$raw" ]]; do
        local line="${raw%$'\r'}"
        local t="${line#"${line%%[![:space:]]*}"}"      # ltrim
        [[ -z "$t" || "$t" == '#'* ]] && continue
        if [[ "$t" == '=>'* ]]; then
            local exp="${t#=>}"; exp="${exp# }"
            if [[ $have == 1 ]]; then run_one "$input" "$exp" "$(basename "$f")"; have=0
            else echo "runner: '=>' with no input in $f: $t" >&2; fi
        else
            input="$t"; have=1
        fi
    done < "$f"
}

files=( tests/*.test )
[[ -e "${files[0]}" ]] || { echo "runner: no tests/*.test files" >&2; exit 2; }
for f in "${files[@]}"; do run_file "$f"; done

echo
if [[ $ASAN == 1 ]]; then
    if [[ $failed == 0 ]]; then printf '%sasan: %d inputs clean%s\n' "$GRN" "$total" "$RST"
    else printf '%sasan: %d/%d inputs flagged%s\n' "$RED" "$failed" "$total" "$RST"; fi
else
    if [[ $failed == 0 ]]; then printf '%s%d passed%s, %d total\n' "$GRN" "$passed" "$RST" "$total"
    else printf '%s%d passed, %d FAILED%s, %d total\n' "$RED" "$passed" "$failed" "$RST" "$total"; fi
fi
[[ $failed == 0 ]]
