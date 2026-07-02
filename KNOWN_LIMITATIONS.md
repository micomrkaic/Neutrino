# Neutrino — Known Limitations

A deliberate, documented list. Most of these are conscious design choices or
deferred work, not bugs. Kept here so they live somewhere durable.

## Memory model (refcounting)

Neutrino reclaims memory by reference counting. This is the intended design, not
a stopgap: the workload allocates large matrices, and refcounting frees them at
last use (low high-water mark), which also makes the VM's error-path sweep total
and teardown deterministic. We are **not** adding a tracing GC.

- **The dominant closure/scope cycle is gone (value-capture upvalues).** Lexical
  addressing makes a closure capture *snapshots of its free variables*, not the
  enclosing environment, so the old leaking pattern —

      for i = 1:N do let cap = fn x -> x + i; ... end

  no longer forms a cycle: `cap` holds a copied value of `i`, with no edge back to
  the iteration scope. The `E_i ⇄ cap` island that leaked under both the
  tree-walker and the early VM (measured ~38–41 KB / ~500 allocs at N=100) is now
  reclaimed immediately; the case is ASan-clean. A function's own name resolves to
  a non-owning self-slot (slot 0 = the closure at call time), so direct recursion
  doesn't create a self-cycle either.
  - **Residual cyclic garbage is still uncollected**, as refcounting cannot
    reclaim isolated cycles in general. Building one requires a *local* closure
    that captures *itself* by value, which does not arise from ordinary code. A
    Bacon–Rajan trial-deletion collector remains in reserve *only if* measurement
    ever justifies it. Everything is reclaimed at teardown.
  - **Tradeoff of value-capture + self-slot:** *local* mutual recursion (two
    closures in the same local scope referring to each other) is unsupported.
    `let … in` now lets you sequence local bindings, but it is non-recursive
    between siblings and a called function roots its environment at the globals,
    not at the caller's scope — so in `let even = fn n -> … odd … in let odd =
    fn n -> … even … in even(4)`, `even`'s reference to `odd` resolves as a global
    and is undefined at call time. Self-recursion works (the self-slot), and
    **global** mutual recursion is unaffected (globals are late-bound by name).

- **The evaluator is leak-free on the error path.** When a runtime error unwinds
  via `longjmp`, the VM sweeps its operand stack and unwinds any open scopes and
  nested call frames, so in-flight temporaries are released rather than stranded.
  (The original tree-walker leaked here — temporaries lived in scattered C locals
  with nothing to enumerate on unwind; replacing it with the stack VM is what
  fixed it. The tree-walker has since been removed at cutover.)

- **Higher-order / elementwise builtins free their own C-level scratch on error.**
  A builtin that allocates an intermediate buffer and then calls back into a
  closure or a scalar kernel must release that buffer if the call raises (the
  `longjmp` skips ordinary cleanup). `map_unary`, `map_binary`, `map` (HOF), and
  the axis-reduction core `reduce_dim` each wrap their build loop in a local
  `setjmp` and, on unwind, hand the partial result to one shared helper
  (`array_build_abort`) that releases the already-built elements, frees the
  buffer, and re-raises — so the transcendentals, rounding, binary math, and
  axis reductions built on them are leak-free on the error path even if a kernel
  raises mid-array. Any future array-builder should reuse `array_build_abort`.
  (The matrix decompositions and least-squares paths likewise free their C
  buffers before any raise.)

## Coverage

The bytecode VM is the only evaluator: every top-level statement is compiled to
a chunk and executed. There is no interpreter fallback. The full surface —
literals, variables, unary/binop with short-circuit, postfix transpose, ranges,
calls, field access, `if`, `let`, assignment, blocks, child scopes, `while`,
`for`, the `|>` pipe, closures, matrix literals, indexing, and records — is
compiled.

## Numerical

- **`eig` eigenvectors use inverse iteration.** The general (non-symmetric) path
  computes eigenvalues by complex shifted QR and then one eigenvector per value
  by inverse iteration. This is accurate for well-separated spectra; for badly
  defective or tightly clustered eigenvalues the returned vectors can lose
  precision (no Schur-vector accumulation). The Hermitian path (Jacobi) returns
  fully orthonormal vectors.
- **Underdetermined `\` uses normal-equation-free QR of Aᴴ.** Stable for
  full-row-rank `A`; rank-deficient wide systems raise rather than returning a
  pseudoinverse solution.

## Language features

- **Block expressions sequence statements.** `( s1; s2; … ; expr )` is a scoped
  statement sequence whose value is the final expression — usable as a function
  body, a pipe stage, or anywhere an expression is expected, with `let` bindings
  local to the block. (Outside a block, parentheses still group a single
  expression.) `break`, `continue`, and `return` are safe anywhere inside one,
  including mid-expression: each loop records its value-stack base and a
  non-local exit releases any in-flight temporaries back to it.
- **Indexed assignment is name-targeted and bounded.** `a[i] = x` (and slices,
  gather, mask, `:`, `end`, 2-D) works with copy-on-write, but the target must be
  a plain name (`a`, not `rec.field[i]` or `a[i][j]`), indices must be in range
  (no auto-growing an array by assigning past its end), and the value is either a
  scalar (broadcast) or an array whose element count matches the selection.
- **`_` sections cannot reuse a hole.** Each `_` is a distinct positional slot;
  a single section cannot reference the same hole twice.

- **The RNG is reproducible by default.** Unlike Octave (which seeds from
  entropy), Neutrino starts every session from a fixed seed, so `rand`/`randn`/
  `randi` produce the same stream until you call `rng(seed)`. This is intentional
  — reproducibility by default — but means you must reseed (e.g. `rng(time)` once
  a time source exists, or any distinct integer) if you want different runs.
