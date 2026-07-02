# Neutrino regression suite

Run from the project root:

```sh
make test        # golden-output comparison; nonzero exit if anything fails
make test-asan   # rerun every input under ASan/UBSan; fails on any leak or UB
```

## Format

Each `*.test` file is a flat list of cases. A case is **one input line** followed
by **one expectation line**:

```
<input>
=> <exact stdout>      # value test:  stdout must equal this, stderr must be empty
<input>
=> !<substring>        # error test:  stdout empty, stderr must contain <substring>
```

`#` lines and blank lines are ignored, so group and comment freely.

Inputs are fed to `./vmtest`, which reads a line, evaluates it, and prints the
value of the **last statement that isn't suppressed by `;`** (Octave-style: a
trailing `;` silences a statement). So a case can set things up and assert in one
line:

```
let s = 0; for i = 1:10 do s = s + i end; s
=> 55
```

One input line therefore yields exactly one output line, which is what makes the
golden comparison trivial.

## Conventions

- Prefer inputs whose expected output is an obvious clean value (integers,
  identities that collapse to `1`/`0`, exact decimals). For decompositions, test
  an **invariant** rather than raw factor entries — e.g. `f.Q * f.R` reconstructs
  the input, `chol(A) * chol(A)'` returns `A`, `eig` of a matrix with an integer
  spectrum — so the golden is meaningful and not a float-noise snapshot. Wrap a
  reconstruction in `round(...)` when floating error would otherwise show.
- Error tests match a **stable substring** of the message (`!out of bounds`), not
  the whole line, so wording/position tweaks don't cause spurious failures.
- Avoid `inf`/`nan` goldens: their printed spelling varies across libc
  implementations (glibc vs. macOS) and would fail spuriously.

## Adding tests

Append two lines to the relevant file (or add a new `NN_area.test`);
`run.sh` globs `tests/*.test` in sorted order automatically.
