# Changelog

Notable changes to Neutrino. Newest first.

## Unreleased

### Added
- **The Neutrino Manual** (`MANUAL.md`, plus a typeset `MANUAL.pdf` — regenerate with `make manual`): a full user guide — REPL, types,
  operators with a precedence table, scope rules, control flow, functions and
  the pipe, arrays and indexing, linear algebra, complex, special functions,
  RNG, formatting, scripts/tools, a builtin reference *generated from the
  interpreter's own doc table*, and a grammar summary. Every REPL transcript in
  it is machine-verified against the interpreter by `tests/run_manual.sh`,
  which runs as part of `make test` — the manual cannot silently drift from
  the implementation.
- **Special functions.** `erf`/`erfc`, `beta`/`lbeta`, regularized incomplete
  gamma `gammainc(x, a)` (the chi-square CDF) and beta `betainc(x, a, b)` (the
  Student-t / F CDFs), the normal quantile `norminv(p)` (Acklam + one Halley
  refinement, ~1e-15), `digamma`, and integer-order Bessel `besselj`/`bessely`.
  All real-domain, elementwise over arrays, cross-checked against SciPy, and the
  build stays `-lm`-only (no GSL). With these primitives the classical CDFs and
  quantiles are one-liners in-language, e.g. `normcdf = fn x -> 0.5 * erfc(-x / sqrt(2))`.
- **`kron(A, B)`** Kronecker product, for all numeric element types (complex
  included), with scalar operands treated as scaling and an overflow guard on
  the result shape. (Replaces an earlier minimal implementation.)
- **Bytecode disassembler.** `neutrino --dis file.nu` lists each statement's
  compiled chunk (mnemonics, resolved constants/names, absolute jump targets,
  line annotations, recursive function protos); `dis(f)` does the same for a
  function interactively. The opcode switch is exhaustive under `-Wswitch`, so a
  new opcode can't be added without teaching the disassembler. Core codegen is
  golden-tested via `tests/run_dis.sh` (e.g. the loop MARK_PUSH/RESET/POP
  sequence), run as part of `make test`.
- **Formatted `print`.** `print("x = {}, y = {}", a, b)` fills each `{}` from the
  following arguments in order; `{{`/`}}` escape literal braces, string arguments
  print unquoted, numbers follow the current `format` setting, and count
  mismatches are errors caught before any output is written. Plain `print(...)`
  now also prints strings unquoted (output, not representation). A placeholder
  can carry its own spec — `{:[-][width][.prec][f|e|g]}` — for per-hole width
  justification, precision, and conversion (element-wise on arrays), with the
  global format state restored after each hole.
- **Consistent-width number formats.** Explicitly chosen formats (`format(4)`,
  `format short`, …) keep trailing zeros so a loop of values prints in uniform
  width (`2.000`, not a bare `2` amid `1.414`); the startup default keeps the
  terse variable-width form.
- **REPL splash banner.** The interactive shell now opens with an ASCII/Unicode
  rendering of the Neutrino mark beside a figlet wordmark, in brand colours via
  ANSI truecolor. Shown only on an interactive TTY (piped input and scripts get
  no banner); colour is suppressed off-TTY or when `NO_COLOR` is set.
- **Brand assets** (`brand/`): vector logo set (icon, horizontal/stacked/mono/
  reversed lockups, README header, favicon, plus a dark-mode lockup), an
  OpenGraph social card (`og-card.png`, 1200×630), and a brand guide. The
  project README now leads with the logo, theme-swapped for light/dark.
- **Block expressions.** `( s1; s2; … ; expr )` is a scoped statement sequence
  used as an expression — its value is the final expression, `let` bindings are
  local to the block, and later statements see earlier ones. Usable as a function
  body, a pipe stage, or anywhere an expression is expected; single-expression
  grouping `(a + b)` and `_` sections are unchanged. (`break`/`continue` buried
  inside a block's sub-expressions remain a pre-existing caveat — see
  KNOWN_LIMITATIONS.)
- **Axis-aware reductions.** `sum`, `prod`, `mean`, `any`, `all` take an optional
  dimension: `sum(A, 1)` reduces down columns (→ row), `sum(A, 2)` across rows
  (→ column). `min`/`max` use the unambiguous three-argument form
  `min(A, [], dim)` / `max(A, [], dim)`, leaving the two-argument elementwise
  `min(a, b)` intact. The no-argument forms still reduce every element to a
  scalar.
- **Full eigensolver.** `eig(A)` now returns a record `{values, vectors}`
  (consistent with `lu`/`qr`/`svd`/`chol`). Symmetric/Hermitian inputs use cyclic
  Jacobi with eigenvector accumulation (real ascending eigenvalues, orthonormal
  vectors); general matrices use complex Hessenberg reduction + shifted QR for the
  eigenvalues and inverse iteration for the vectors, so non-symmetric inputs and
  complex-conjugate pairs are supported.
- **Underdetermined least squares.** `A \ b` with `m < n` returns the
  minimum-norm solution (Householder QR of Aᴴ), complementing the existing
  overdetermined least-squares path.
- **`where`.** `where(mask)` gives the 1-based indices of the true elements (like
  `find`); `where(mask, a, b)` is an elementwise select (pick `a` where the mask
  is true, `b` where false), with scalar broadcasting.
- **Number-display control.** `format` sets precision/style — `format long`,
  `format short`, `format short e`, `format(n)` — as a REPL command or the
  `format("...")` / `format(n)` builtin. The startup default is unchanged.
- **Output paging.** `more on` / `more off` pages long REPL output through
  `$PAGER`.
- **Aligned matrix display.** The REPL prints matrices as multi-line,
  column-aligned blocks; `pretty off` restores the single-line `[a, b; c, d]`
  form (which round-trips as input). Off by default outside the REPL.

### Fixed
- **`break`/`continue` mid-expression.** A `break` or `continue` fired from
  inside a sub-expression with operands in flight (e.g. `acc + (… continue …)`)
  used to leave the value stack unbalanced and produce wrong results — in a
  `for` loop the stranded temporary was even read as the iteration index,
  ending the loop early. Each loop now records its value-stack base
  (`OP_MARK_PUSH`) and a non-local exit releases everything back to it
  (`OP_MARK_RESET`), with per-frame mark accounting so `return` through a loop
  is also clean. Escapes are now correct from any depth of expression, block,
  or nesting.
- **Bare-callable pipe.** `x |> f` now applies `f` to `x` (e.g. `9 |> sqrt` → `3`,
  `10 |> inc` → `11`) as documented; previously a pipe whose right side did not
  mention `@` evaluated to the callable itself without applying it. The
  `@`-substitution form (`x |> f(@)`) is unchanged.

### Performance
- **Scalar index fast path.** `a[i]` and `a[i, j]` with plain scalar indices no
  longer allocate a selection buffer per access (the colon / mask / gather paths
  are unchanged).

### Internal
- A single shared error-path helper (`array_build_abort`) backs every
  array-builder (`map`, the elementwise kernels, axis reductions), so a kernel
  that raises mid-array releases its scratch and re-raises with no leak. Verified
  with AddressSanitizer.
- Portable build: `CC` defaults to `cc` (gcc on Linux, Apple Clang on macOS),
  `-Werror` is overridable (`make WERROR=`), and readline detection handles the
  macOS libedit shim — the REPL builds with line editing + history against
  libedit and only enables GNU readline's signal helpers when real readline is
  present (`HAVE_GNU_READLINE`). Homebrew's prefix is added automatically.

## Earlier milestones

- Bytecode VM with lexical addressing (slot locals, value-capture upvalues,
  self-slot for recursion), replacing the original tree-walker.
- Control-flow escapes: `break`, `continue`, `return`.
- `for` / `while` loops with scope-walking assignment; `let … in` expressions;
  records; closures and `_` sections; the `|>` pipe.
- Complex throughout the linear algebra: `lu`, `qr`, `chol`, `eig`, and complex
  (and wide `m < n`) `svd`; left/right division; matrix power.
- Elementwise math library, complex accessors (`real`/`imag`/`conj`/`angle`),
  reproducible RNG, predicates, array utilities.
- REPL quality-of-life: rich `help` / `who`, `!cmd` and `system`, history.
- A golden-output regression suite run line-by-line in fresh processes, plus an
  AddressSanitizer build.
