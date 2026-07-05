<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/logo-horizontal-dark.svg">
    <img alt="Neutrino — a small functional array language" src="brand/logo-horizontal.svg" width="520">
  </picture>
</p>

**[Read the manual](MANUAL.md)** ([PDF](MANUAL.pdf)) — the full guide to the language,
REPL, and tools, with every example verified against the interpreter.

A small functional **array language** with Octave-flavoured syntax, implemented
in C23. Neutrino has a strict numeric tower, first-class functions, immutable
refcounted values, complex numbers, logical (boolean) arrays, `for`/`while`
loops, and a linear algebra core — matrix multiply, left/right division,
least-squares, matrix power, `det`/`inv`, and `lu`/`qr`/`chol`/`eig`/`svd`
decompositions (all carried out in complex, so real inputs give real results).
It runs as a REPL with line editing, history, and tab completion, or executes
`.nu` scripts.

It is a recreational language project, built from scratch: a zero-copy pull
lexer, a Pratt parser into an arena, and a refcounting **bytecode VM**. Source
is compiled per top-level statement into a stack-machine chunk and executed; the
operand stack doubles as the live-set that makes error recovery leak-free. (An
earlier tree-walking evaluator was replaced by the VM; see *Implementation
notes*.)

```
neutrino> let A = [2, 1; 1, 3]
[2, 1; 1, 3]
neutrino> let x = A \ [5; 10]
[1; 3]
neutrino> A * x
[5; 10]
neutrino> [1, 2, 3, 4] |> map((_ .^ 2), @) |> sum(@)
30
```

---

## Build

Requires a C23-capable compiler (gcc 13+ / clang 16+) and `libm`.
GNU **readline** is optional but recommended — the Makefile auto-detects it and
falls back to a plain `fgets` REPL when it is absent.

```sh
make            # builds ./neutrino (readline if available)
make READLINE=0 # force the no-readline fallback
make clean
```

The build is `-std=c2x -Wall -Wextra -Werror -O2`. On gcc 14+, pass `STD=c23`:

```sh
make STD=c23
```

**macOS.** `cc` is Apple Clang; Xcode 15+ Command Line Tools (`xcode-select
--install`) provide the C23 features used here (`nullptr`, `[[noreturn]]`,
`enum : uint8_t`). The default `STD=c2x` is what to use — `STD=c23` needs a very
recent toolchain. Readline: macOS's `-lreadline` resolves to Apple's **libedit**
shim, which supplies line editing and history (the REPL builds and uses them)
but lacks GNU readline's signal helpers; the Makefile detects this and only
enables those extras for real GNU readline. For the full experience (cleaner
Ctrl-C handling) `brew install readline` — the Makefile adds
`$(brew --prefix)/{include,lib}` automatically. `make READLINE=0` forces the
plain `fgets` REPL. If Apple Clang's warning set flags something gcc didn't,
build with `make WERROR=` to drop `-Werror` for that build. The test harness
(`make test`) runs under the system bash 3.2 unchanged.

Convenience targets: `make run` / `make repl` (start the REPL), `make sample`
(run the built-in demo), `make ast` / `make tokens` (dump the demo's AST/tokens).

**Bytecode introspection.** `neutrino --dis file.nu` disassembles each top-level
statement's compiled chunk — mnemonics, resolved constants and names, jump
targets as absolute offsets, source-line annotations, and a recursive listing of
every nested function proto. Interactively, `dis(f)` disassembles a function to
the same format. The emitted code for the core constructs is itself golden-tested
(`tests/dis/`), so a codegen regression shows up as a diff, not a debugging
session.

### ASan build

```sh
gcc -std=c2x -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer lexer.c arena.c ast.c parser.c value.c eval.c \
    chunk.c compile.c vm.c repl.c main.c -lm -o neutrino-asan   # (READLINE=0 equivalent)
```

Both the clean *and* the error paths are leak-, use-after-free-, and UB-free
under ASan: when a runtime error unwinds via `longjmp`, the VM sweeps its
operand stack (and unwinds any open scopes / call frames), so in-flight
temporaries are released rather than stranded. The one residual leak is cyclic
garbage that refcounting cannot collect — see `KNOWN_LIMITATIONS.md`.

### Testing

```sh
make test        # run the golden-output regression suite
make test-asan   # run the same corpus under ASan/UBSan; fails on any leak
```

The suite lives in `tests/*.test`, one area per file (literals, arithmetic,
arrays, linear algebra, decompositions, control flow, functions/closures,
records, reductions, the math library, and error paths). Each case is an input
line and its expected output, checked against `vmtest` (which prints the value of
each line's last non-suppressed statement):

```
sin(1:10) .^ 2 + cos(1:10) .^ 2
=> [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
gamma(2 + 3i)
=> !expected a real number      # => !substr asserts a raised error, not a value
```

Adding a test is two lines; `tests/run.sh` discovers files automatically. The
`test-asan` target reruns every input (value *and* error cases) under the
sanitizers, so the error-unwind paths are leak-checked on every run. See
`tests/README.md` for the format.

## Run

```
neutrino                 # interactive REPL
neutrino script.nu       # run a file
neutrino --sample        # run the built-in demo program
neutrino --ast    file   # dump the AST and exit
neutrino --tokens file   # dump the token stream and exit
```

In the REPL: **Ctrl-D** exits, **Ctrl-C** cancels the current (possibly
multi-line) entry, **Tab** completes keywords and defined names. Unbalanced
brackets / `if…end` continue on a `...>` prompt. History persists in
`~/.neutrino_history`.

**Getting around.** `help` prints a grouped catalogue of every builtin plus a
language cheat-sheet; `help(f)` describes one function (its call forms, what it
does, how many arguments it takes). `who` lists your variables with their type
and shape. These are functions, but a bare name with no arguments is called for
you — so `help`, `who`, and `rand` work typed on their own, and so does any
0-parameter function you define (`let greet = fn -> "hi"`; then just `greet`).

**Shell.** A line beginning with `!` runs the rest as a shell command
(`!ls -l`, `!git status`), with your session state intact across it.
Programmatically, `system("cmd")` runs a command and returns its exit status
as an integer.

**Display.** `format` controls how numbers print: `format long` shows full
double precision, `format short` is terser, `format short e` switches to
scientific notation, and `format` on its own (or `format("...")` / `format(n)`
in a script) reports or sets it. Explicitly chosen formats keep trailing zeros
for consistent width (`format(4)` prints `2.000`, `1.414`, `3.162` — never a
bare `2` between decimalled neighbours); the startup default (`format default`)
uses the terse variable-width form. `more on` pages long output through
`$PAGER`; `more off` (the default) lets it scroll. The interactive shell prints
matrices as aligned, multi-line blocks; `pretty off` falls back to the compact
single-line `[a, b; c, d]` form (which round-trips as input), `pretty on`
restores alignment.

**Formatted printing.** When `print`'s first argument is a string containing
`{}` placeholders, each is filled by the following arguments in order; `{{` and
`}}` emit literal braces, string arguments print without quotes, and numbers
follow the current `format` setting. A placeholder can carry its own spec,
`{:[-][width][.prec][f|e|g]}`: width right-justifies (leading `-`
left-justifies), `.prec` and the conversion override the global format for that
one hole (and are restored afterwards) — applied element-wise if the argument is
an array. Placeholder/argument count mismatches and malformed specs are errors
caught before any output is written:

```
print("x = {}, |r| = {}", x, norm)       ->  x = 3.5, |r| = 1.414
print("pi = {:.3f}", 3.14159)            ->  pi = 3.142
print("[{:8.3f}]", sqrt(2))              ->  [   1.414]
print("[{:-8.3f}]", sqrt(2))             ->  [1.414   ]
print("{:.2f}", [1.234, 5.678])          ->  [1.23, 5.68]
print("a {{literal}} brace", ...)        ->  a {literal} brace
```

---

## Language tour

### Values and the numeric tower

Scalars are `Int` (int64), `Float` (f64), `Complex`, `Bool`, `String`, and
`null`. Arithmetic promotes along the tower **int → float → complex**:

```
2 + 3          -> 5
7 / 2          -> 3.5         # '/' always produces a float
2 ^ -1         -> 0.5         # int ^ negative int -> float
sqrt(-4)       -> 2i          # sqrt of a negative real is complex
2 + 3i         -> 2+3i
"hi" + "!"     -> error       # strings don't add; no implicit coercions
```

### Arrays and matrices

Arrays are 2-D, row-major, **1-based**, and carry a single element type
(`Int`/`Float`/`Complex`/`Bool`). Literals use `,` between columns and `;`
between rows; elements may themselves be matrices (concatenation):

```
[1, 2, 3]              # 1x3
[1, 2; 3, 4]           # 2x2
1:5                    # 1 2 3 4 5   (range)
1:2:9                  # 1 3 5 7 9   (start:step:stop)
let A = [1, 2; 3, 4]
[A, A]                 # 2x4  horizontal concat
[A; A]                 # 4x2  vertical concat
[A, [5; 6]]            # 2x3
```

Indexing reads an element, a slice, or a submatrix. An index can be a scalar, a
range, a vector of positions, a logical mask, or `:` (the whole dimension):

```
A[1, 2]                 -> 2            # element (1-based, row, column)
(1:10)[3]               -> 3
a[2:4]                  -> a 2..4       # range slice
a[[1, 3, 5]]            -> gather       # pick those positions, result follows the index shape
a[a > 25]               -> mask         # logical indexing
a[:]                    -> column       # every element, flattened to a column (row-major)
A[2, :]                 -> row 2        # ':' = the whole dimension
A[:, 2]                 -> column 2
A[1:2, 1:2]             -> top-left 2x2 submatrix
A[[1, 3], [1, 3]]       -> corner elements as a 2x2
a[end]                  -> last element       # 'end' = the current dimension's size
a[2:end]                -> from 2 to the last
A[end, :]               -> last row
A[end-1, end]           -> arithmetic on 'end' works in any position
```

Inside an index, `end` is the size of the dimension being indexed: the element
count for `a[i]`, the row count for the first subscript of `A[i, j]`, the column
count for the second. It nests correctly — in `a[b[end]]` the `end` refers to
`b`. Shape rules: a scalar in every position yields a scalar; otherwise a vector
or submatrix. A gather (`a[idx]`) takes the *shape of the index* (`[1;3;5]` →
column), `a[:]` flattens to a column, and a logical mask follows the target's
orientation.

Assignment writes through the same forms, with **copy-on-write** so aliases stay
independent:

```
a[i] = x                # element
A[i, j] = x
a[2:4] = [x, y, z]      # slice (value sizes must match the selection)
a[2:4] = 0              # scalar broadcasts to every selected position
a[mask] = 0             # masked
A[:, j] = v             # whole column; 'end' works here too
b = a; b[1] = 9         # a is unchanged — b got its own copy
```

A uniquely-owned array is mutated in place (so building one element-by-element in
a loop is linear); a shared one is copied first. Assigning a wider value promotes
the array (`a[i] = 2.5` turns an integer array into a float one). The index must
be in range — there is no auto-growing — and the target must be a plain name.

### Operators

| class        | operators                                               |
|--------------|---------------------------------------------------------|
| arithmetic   | `+  -  *  /  \  ^`  and elementwise `.*  ./  .\  .^`     |
| matrix       | `*` matmul, `\` left division, `/` right division, `^` power |
| comparison   | `==  !=  <  <=  >  >=`                                   |
| logical      | `&&  ||` (short-circuit, scalar), `&  |  ~`/`!` (eager / elementwise) |
| postfix      | `'` conjugate transpose, `.'` transpose                 |
| range / pipe | `:`  and  `\|>`                                          |

Linear algebra (square systems, partial-pivot Gaussian elimination, carried out
in complex so real inputs return real results):

```
[2, 0; 0, 2] \ [4; 6]   -> [2; 3]        # solve A x = b
[4, 6] / [2, 0; 0, 2]   -> [2, 3]        # solve x A = b
[1, 1; 0, 1] ^ 3        -> [1, 3; 0, 1]  # matrix power
[2, 0; 0, 2] ^ -1       -> [0.5, 0; 0, 0.5]   # inverse (A \ I)
det([1, 2; 3, 4])       -> -2
eig([2, 1; 1, 2]).values  -> [1; 3]      # eigenvalues (eig returns {values, vectors})
```

`eig(A)` returns a record `{values, vectors}` whose columns are the
eigenvectors matching `values`. Symmetric/Hermitian matrices take a Jacobi path
(real ascending eigenvalues, orthonormal vectors); general matrices use a
complex QR iteration with inverse iteration for the vectors, so non-symmetric
inputs and complex-conjugate eigenpairs work too:

```
let s = eig([0, -1; 1, 0])           # {values = [-1i; 1i], vectors = ..}
s.values                             -> [-1i; 1i]   # complex pair
let A = [1, 2, 3; 4, 5, 6; 7, 8, 10]
let e = eig(A)
A * e.vectors  ~  e.vectors * diag(e.values)        # A V = V diag(values)
```

A **non-square** `\` becomes a least-squares solve (Householder QR).
Overdetermined systems (`m > n`) give the least-squares fit; underdetermined
ones (`m < n`) give the minimum-norm solution:

```
[1, 1; 1, 2; 1, 3] \ [1; 2; 2]   -> [0.666667; 0.5]    # overdetermined: best-fit (intercept, slope)
[1, 1, 1; 1, 2, 3] \ [6; 14]     -> [1; 2; 3]        # underdetermined: least-norm solution
```

Decompositions return records:

```
let f = lu([4, 3; 6, 3])    # {L = .., U = .., p = ..}   with P*A = L*U
f.L * f.U                   -> [6, 3; 4, 3]
let g = qr([1, 2; 3, 4])    # {Q = .., R = ..}
g.Q * g.R                   -> [1, 2; 3, 4]
let L = chol([4, 2; 2, 3])  # lower Cholesky factor, SPD input
L * L'                      -> [4, 2; 2, 3]
let s = svd([1, 2; 3, 4; 5, 6])    # thin SVD {U = .., S = .., V = ..}
s.U * diag(s.S) * s.V'      -> [1, 2; 3, 4; 5, 6]   # A = U diag(S) V'
```

The decompositions run in complex throughout, so complex inputs work directly
and Hermitian structure is respected (`'` is the conjugate transpose):

```
eig([1, 1i; -1i, 1]).values  -> [0; 2]    # Hermitian eigenvalues (real)
let c = chol([2, 1i; -1i, 2])               # Hermitian positive-definite
c * c'                      -> [2+0i, 1i; -1i, 2+0i]   # round-trips (result is complex-typed)
let q = qr([1 + 1i, 2; 3, 4 - 1i])          # unitary Q, q.Q' q.Q = I
```

### Logical arrays

Comparisons and elementwise `&`/`|`/`~` over arrays yield **logical arrays**,
which drive masking and reductions:

```
let A = [1, 2, 3, 4, 5]
A > 2                   -> [false, false, true, true, true]
(A > 2) & (A < 5)       -> [false, false, true, true, false]
~(A > 2)                -> [true, true, false, false, false]
A[A > 2]                -> [3, 4, 5]      # logical indexing
sum(A > 2)              -> 3              # sum of a logical array counts trues
any(A > 10)             -> false
all(A > 0)              -> true
find(A > 2)             -> [3, 4, 5]      # 1-based positions of the trues
where(A > 2, A, 0)      -> [0, 0, 3, 4, 5]  # pick A where true, else 0
A[A > 3] = 0            -> A is now [1, 2, 3, 0, 0]   # masked assignment
```

Mixing `Bool` and numeric elements in a literal is an error (`[true, 1]`),
keeping the type discipline strict rather than silently coercing.

### Records

Keyed records use `{name = value}`; access with `.name`:

```
let p = {x = 3, y = 4}
sqrt(p.x .^ 2 + p.y .^ 2)   -> 5
```

### Control flow — `if` is an expression

```
if 3 > 2 then "yes" else "no" end   -> "yes"
```

The condition must be a `Bool` (no truthiness). `if` yields a value; a missing
`else` yields `null`.

### Loops and assignment

`for v = iter do … end` iterates over the elements of an array (row-major), or
runs once for a scalar; `while cond do … end` repeats while a `Bool` condition
holds. Both reuse the reserved `do`/`end` and yield `null`.

A bare `name = value` (no `let`) is an **assignment**: it walks the scope chain
and updates an existing binding, so a loop body can accumulate into an outer
variable. The name must already be declared with `let` — assigning an undeclared
name is an error. Each loop iteration runs in a fresh child scope, so `let`
inside the body is iteration-local while `=` reaches outward.

```
let s = 0
for i = 1:100 do s = s + i end          # s -> 5050
s

let n = 1; let p = 1
while n <= 5 do p = p * n; n = n + 1 end # p -> 120 (5!)

let a = 0; let b = 1                     # Fibonacci
for k = 1:10 do
  let t = a + b                          # t is local to the iteration
  a = b
  b = t
end
a                                        # -> 55
```

**Escapes** — `break` leaves the nearest enclosing loop, `continue` skips to its
next iteration, and `return [expr]` exits the enclosing function early (a bare
`return` yields `null`). They unwind any `let` scopes opened on the way out, so
they are safe inside nested `let … in` bodies and nested loops. Using `break` or
`continue` outside a loop, or `return` outside a function, is a compile error.

```
let s = 0
for i = 1:10 do
  if mod(i, 2) == 0 then continue end    # skip evens
  if i > 7 then break end                # stop past 7
  s = s + i
end
s                                        # -> 1 + 3 + 5 + 7 = 16

let first_big = fn xs ->
  for i = 1:numel(xs) do
    if xs[i] > 100 then return xs[i] end
  end                                    # falls through -> null if none
```



Lambdas are parens-free: `fn x -> body`, comma-separated parameters. Functions
are ordinary values:

```
let sq = fn x -> x .^ 2
map(sq, [1, 2, 3, 4])       -> [1, 4, 9, 16]
```

**Local bindings** — `let x = value in body` is an *expression*: it binds `x`
(scoped to `body`) and evaluates to `body`. This is how a function gives names to
intermediate results, since a lambda body is a single expression. The bindings
nest and shadow, and a closure created in `body` captures them by value like any
other variable:

```
let f = fn x -> let y = x + 1 in y * y;   f(4)        -> 25
let dist = fn x1 -> fn y1 -> fn x2 -> fn y2 ->
             let dx = x2 - x1 in
             let dy = y2 - y1 in sqrt(dx*dx + dy*dy)
dist(0)(0)(3)(4)                                       -> 5
let a = 1 in a + (let a = 100 in a) + a               -> 102   # inner a shadows
```

A top-level `let x = …` (no `in`) is the statement form and binds a global; the
`in` is what makes it a scoped expression instead.

**Block expressions** — `( s1; s2; … ; expr )` is a statement sequence as an
expression: the statements run in order and the block evaluates to its final
expression. It's the natural alternative to chained `let … in` when a function
body or pipe stage needs a few intermediate steps. Bindings are local to the
block (they don't leak out), later statements see earlier ones, and the usual
single-expression grouping `(a + b)` and sections `(_ + 1)` are unchanged (a
block is recognised by the `;`):

```
let hypot = fn a -> fn b -> (let s = a*a + b*b; sqrt(s));   hypot(3)(4)   -> 5
let f = fn n -> (let x = n + 1; let y = x * 2; y);          f(10)         -> 22
5 |> (let d = @ * 2; d + 1)                                                -> 11
(let a = (let b = 2; b * 3); a + 1)                                        -> 7   # nested
```

`return` from inside a block exits the enclosing function as usual, and `break`
/ `continue` work anywhere inside one — including mid-expression — with any
in-flight temporaries cleaned up on the way out.

**Operator sections** turn a grouping-parenthesised expression containing `_`
into a lambda. Each `_` becomes a fresh parameter, left to right:

```
let inc = (_ + 1)           # fn x -> x + 1
inc(5)                      -> 6
(2 * _)(7)                  -> 14
map((_ .^ 2), [1, 2, 3])    -> [1, 4, 9]
(_ + _)(3, 4)               -> 7          # two holes -> two parameters
(_ \ [4; 6])([2, 0; 0, 2])  -> [2; 3]     # section of left-division
```

Sections are delimited by **explicit grouping parentheses only** — call
argument lists are not section boundaries, which avoids the `g(_)` ambiguity
(identity-into-`g` vs. `fn x -> g(x)`). A `_` can't be reused within one
section; for that, write an explicit `fn`.

### Pipelines — `|>` and the `@` placeholder

`x |> expr` evaluates `expr` with `@` bound to `x`. A bare callable on the right
is applied directly; an `@`-free non-bare right-hand side is an error so the
piped value can't be silently dropped:

```
9  |> sqrt(@)               -> 3
9  |> sqrt                  -> 3          # bare callable: f(x)
5  |> @ + 1                 -> 6
[1, 2, 3, 4] |> sum(@) |> sqrt(@)         -> 3.16228
[1, 2, 3, 4] |> map((_ * 10), @) |> sum(@) -> 100
5  |> sqrt(2)              # error: piped value unused
```

`@` and `_` are independent placeholders: `@` is the dynamic piped value, `_`
is a lexical section parameter; they compose freely.

### Statements and echo suppression

Statements are separated by newlines or `;`. Ending a statement with `;`
**suppresses its echo** (Octave-style), in the REPL and in scripts:

```
let v = [3, 4];            # silent
let s = sum(v);            # silent
sqrt(s)                    -> ~2.6458
```

`let a = 1; let b = 2` runs both and echoes only `2`.

## Builtins

```
-- core ----------------------------------------------------------------
print(...)        print values separated by spaces
sum(A)            sum of elements; for a logical array, counts trues
size(x)           [rows, cols]
map(f, A)         apply f to each element of A
who               list your bindings with type and shape
help / help(f)    builtin catalogue, or describe one function/value
system(cmd)       run a shell command string; return its exit status
format / format(m) show or set number display: "short", "long", "short e", digits

-- elementwise math ----------------------------------------------------
abs  sqrt  exp                                      magnitude, root, e^x
log ln  log10  log2                                 logs (ln = natural; alias of log)
sin  cos  tan   asin  acos  atan   atan2(y,x)       trig + 2-arg arctangent
sinh cosh tanh  asinh acosh atanh                   hyperbolic + inverses
floor ceil round trunc  sign  cbrt                  rounding, sign, cube root
gamma  lgamma                                       gamma, log-gamma (real)
erf(x)  erfc(x)                                     error function and complement
beta(a,b)  lbeta(a,b)                               beta function and its log
gammainc(x,a)                                       regularized lower incomplete gamma P(a,x)
betainc(x,a,b)                                      regularized incomplete beta I_x(a,b)
norminv(p)                                          standard normal quantile
digamma(x)                                          psi(x) = d/dx log gamma(x)
besselj(n,x)  bessely(n,x)                          Bessel J_n, Y_n (integer order)
hypot(a,b)  mod(a,b)  rem(a,b)                       sqrt(a^2+b^2), modulo, remainder
real  imag  conj  angle (arg)                        complex part / conjugate / phase
                  Unary forms apply to a scalar or elementwise over an array;
                  the binary forms broadcast scalar-vs-array like the operators.
                  Transcendentals follow the numeric tower: real input outside
                  the real domain (log of a negative, asin of |x|>1) and complex
                  input return complex, exactly as sqrt does. mod takes the sign
                  of the divisor, rem the sign of the dividend.

-- reductions ----------------------------------------------------------
sum(A)            sum of elements
min(A) max(A)     smallest / largest element
mean(A) prod(A)   arithmetic mean / product
var(A[, w][, dim])  std(...)                        variance / std dev (w = 0: N-1, w = 1: N)
median(A) | median(A, dim)                          median overall or along dim
quantile(x, p)                                      quantiles, linear interpolation (p scalar or vector)
cov(X[, w]) | cov(x, y[, w])                        covariance matrix of columns / scalar pair cov
corr(X) | corr(x, y)                                Pearson correlation matrix / scalar
unique(A)                                           sorted distinct elements
tic  toc()                                          wall-clock timing (monotonic)
readcsv(f[, opts])  writecsv(f, A[, opts])          numeric CSV in/out (full-precision round trip)
readtable(f[, opts])                                CSV with header -> record of named column vectors
fzero(f, a, b)  fminbnd(f, a, b)                    Brent root / minimum ({x, fx}) of a function
integral(f, a, b[, tol])                            adaptive definite integral
any(mask) all(mask)   logical reductions
isnan(x) isinf(x) isfinite(x)   elementwise FP predicates -> logical

-- array utilities -----------------------------------------------------
length(A)         longest dimension (0 if empty)
numel(A)          number of elements
find(A)           1-based positions of nonzero/true elements (row-major)
sort(A)           ascending; a vector as a whole, a matrix by column
cumsum(A) cumprod(A)  cumulative sum / product (vector, or down columns)
diff(A)           consecutive differences (vector, or down columns)
repmat(A,m,n)     tile A into an m-by-n block grid
flipud(A) fliplr(A)   reverse row / column order
min(a,b) max(a,b)     elementwise min / max (broadcasting)

-- random --------------------------------------------------------------
rng(seed)         reseed the generator (xoshiro256**)
rand([r[,c]])     uniform [0,1) — scalar, n x n, or r x c
randn([r[,c]])    standard-normal draws (same shapes)
randi(imax[,r,c]) random integers in 1..imax (or randi([lo,hi], ...))

The generator is xoshiro256**, seeded by splitmix64. It starts from a fixed
seed, so a fresh session reproduces the same stream; call rng(seed) for a
different reproducible stream. Same seed, same draws — across sessions and
platforms.

-- constructors --------------------------------------------------------
zeros(r,c) ones(r,c)  filled arrays
eye(n)            n x n identity
diag(x)           vector -> diagonal matrix, or matrix -> its diagonal
linspace(a,b,n)   n evenly spaced points
reshape(A,r,c)    row-major reshape

-- linear algebra ------------------------------------------------------
det(A)            determinant
inv(A)            inverse (A \ I)
trace(A)          sum of the diagonal
dot(a,b)          inner product of two vectors
norm(x[,p])       vector p-norm (1 or 2) / matrix Frobenius
kron(A,B)         Kronecker product
lu(A)             LU with partial pivoting -> {L, U, p}   (P*A = L*U)
qr(A)             Householder QR -> {Q, R}                 (real or complex)
chol(A)           Cholesky factor L (lower), L*L' = A      (SPD / Hermitian PD)
eig(A)            eigendecomposition -> {values, vectors} (Hermitian or general)
kron(A, B)        Kronecker product: (m x n) kron (p x q) -> (mp x nq)
svd(A)            thin SVD -> {U, S, V},  A = U*diag(S)*V'  (S descending)
```

Decompositions return records, so destructure with field access:
`let f = qr(A); f.Q`, `lu(A).p`, `svd(A).S`, etc. Every factorization computes in
complex, so complex (and Hermitian) inputs are supported directly while real
inputs stay exactly real. `eig`/`svd` use cyclic / one-sided Jacobi; `qr` and
least-squares use Householder reflectors; `det` and `inv` reuse the complex
solver.

---

## Architecture

| file                | role                                                            |
|---------------------|-----------------------------------------------------------------|
| `lexer.{h,c}`       | zero-copy, pull-based lexer; tokens point into the source       |
| `arena.{h,c}`       | bump allocator for the AST (one arena per parsed program)       |
| `ast.{h,c}`         | AST node types and a pretty-printer (`--ast`)                   |
| `parser.{h,c}`      | recursive-descent + Pratt parser; `setjmp` panic unwinding      |
| `value.{h,c}`       | runtime values, refcounting, arrays, records, closures, environments |
| `eval.{h,c}`        | interpreter state, runtime helpers, numeric tower, linear algebra, builtins |
| `chunk.{h,c}`       | bytecode chunks: opcodes, constants, name pool, function protos |
| `compile.{h,c}`     | AST → bytecode compiler: lexical-addressing resolver, literal folding |
| `vm.{h,c}`          | call-frame stack machine: slots, upvalues, scopes, loops, error sweep |
| `nrt.{h}`           | shared runtime-helper declarations (VM ⇄ builtins)             |
| `repl.{h,c}`        | readline REPL: history, completion, multi-line continuation     |
| `main.c`            | CLI: REPL / file / `--ast` / `--tokens` / `--sample`            |
| `Makefile`          | build with readline auto-detection                              |

Roughly 4.7k lines of C.

## Implementation notes

**Lexer.** Pull-based and zero-copy: each `Token` is a `(start, len)` slice into
the source plus a kind; no string copies. Numbers, complex literals (`3i`),
strings with escapes, identifiers, and the operator set are recognised in a
single pass with one character of lookahead.

**Parser.** Recursive descent for statements and a Pratt (precedence-climbing)
core for expressions, allocating nodes into a per-program arena. Errors
`longjmp` to a single recovery point. Inside `()`, `[]`, and `if…end`, newlines
are insignificant — which is exactly what lets the REPL detect "incomplete
input": an unterminated construct fails at EOF, anything else is a real error.

**Values.** Scalars (`null`, `bool`, `int64`, `f64`, `complex`) are immediate —
stored inline, no allocation. Strings, arrays, records, closures, builtins, and
environments are refcounted heap objects (`retain`/`release`). Arrays store one
`EltType` so an index array is provably integer; logical arrays add `ELT_BOOL`
(one byte per element). Closures capture their free variables by value-snapshot
(*upvalues*), not by holding the enclosing environment, so a closure never points
back at the scope that made it — the closure/scope cycle that refcounting can't
reclaim simply doesn't form. The global binding cycle is cut by an explicit
`env_clear` at teardown, keeping ASan quiet.

**Evaluator.** A bytecode VM. Each top-level statement is compiled to a `Chunk`
(opcodes + folded constants + a name pool + function protos) and executed on a
stack machine. Values live on a single operand stack, which is the key to clean
error handling: a fallible op *peeks* its operands (leaving them on the stack),
calls a non-consuming runtime helper, and only on success pops and pushes the
result — so when a helper raises, every in-flight temporary is still on the
stack and gets released in one sweep. **Lexical addressing** classifies each name
at compile time: a function's parameters and self live in operand-stack *slots*
(slot 0 = the closure itself, the self-slot for direct recursion), free variables
are value-snapshot upvalues carried on the closure, and loop-body `let`s and
globals stay late-bound by name in the env (so recursion and mutual recursion
work). A direct call pushes a `CallFrame` over the callee and its arguments
already on the stack — no per-call environment, `malloc`, `setjmp`, or C
recursion — and the whole run loop is one frame stack under one `setjmp`. Child
scopes (`for`/`while`/`|>`) and call frames are unwound on error. Higher-order
builtins (`map`) drive closures through `call_value`, which nests a fresh
executor (the only, shallow, C recursion). The numeric tower lives in
`scalar_arith_k` (scalar promotion) and `elementwise` (broadcasting); matrix
multiply, `\`/`/` (Gaussian elimination with partial pivoting, in complex), and
`^` (exponentiation by squaring) build on those. These and the aggregate
builders (`build_matrix`, `do_index`) are shared runtime helpers, declared in
`nrt.h`.

**REPL lifetimes.** Environment names are non-owning slices into the source, and
compiled function protos live inside their statement's chunk. So a REPL that
freed each line would dangle binding names and escaped closures' code. Instead
each *accepted* entry's `(arena, source)` pair is retained for the session, and
chunks that define closures are likewise retained (freed by `vm_session_end` at
exit) — a closure defined at the prompt keeps its source, names, and bytecode
alive, which is semantically necessary anyway.

**Known limitation.** Reference counting cannot reclaim isolated reference
cycles. In practice this is a closure `let`-bound in a transient scope it
captures; it is reclaimed at teardown and is only unbounded under a long loop
minting non-escaping closures. The planned fix is upvalue capture (not a GC);
full detail in `KNOWN_LIMITATIONS.md`.

## Roadmap

- Auto-growing indexed assignment; assignment to `rec.field[i]`

Done since the last cut: `break`/`continue` are now balanced from anywhere,
including mid-expression (each loop records its value-stack base and a
non-local exit releases in-flight temporaries back to it); block expressions — `( s1; s2; expr )` as a scoped
statement sequence (function bodies, pipe stages, nested), with bindings local
to the block; a single shared error-path cleanup helper (`array_build_abort`) behind every array-builder (`map`, the elementwise
kernels, and axis reductions), so a kernel that raises mid-array never strands
scratch; `where(mask, a, b)` elementwise select (and
`where(mask)` / `find` for mask-to-index, plus logical-mask assignment
`A[mask] = v`); axis-aware reductions (`sum(A, dim)`, `mean`, `prod`,
`any`, `all`, and `min(A, [], dim)` / `max(A, [], dim)`); a full eigensolver —
`eig` returns `{values, vectors}` for symmetric/Hermitian (Jacobi) *and* general
matrices (complex QR + inverse iteration), with non-symmetric and complex
spectra supported; underdetermined least-squares (minimum-norm `m < n`);
number-display control (`format`) and REPL output paging (`more`).
Earlier: `for`/`while` loops with scope-walking assignment;
complex `lu`/`qr`/`chol`/`eig` and complex (and wide `m < n`) `svd`; a bytecode
VM with lexical addressing (slot locals, value-capture upvalues, self-slot);
the elementwise math library and complex accessors (`real`/`imag`/`conj`/
`angle`); `let … in` local bindings; and a golden-output regression suite
(`make test`).

## Reserved words

`let in fn if then else end true false null for while do break continue return`
are in use.

## License

GPL-3.0 — see [LICENSE](LICENSE). Neutrino is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License v3; derivative works must remain under the same terms.
