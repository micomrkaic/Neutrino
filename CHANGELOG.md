# Changelog

Notable changes to Neutrino. Newest first.

## Unreleased

### Added
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
