# Design notes — candidate syntax, deliberately not implemented

Ideas that survived a first design discussion but are **waiting for a real
transcript** — an actual session where one of them was missed — before any
implementation. House policy: features earn their way in through demonstrated
friction, not through being appealing. Each entry records the design so the
thinking is not lost, and names its trigger.

Status of everything in this file: **not implemented, not promised.**

---

## 1. Fan-out pipe: piping into a record of functions

```
data |> {n = length, mu = mean, sd = std, q = fn v -> quantile(v, [0.25, 0.75])}
   -->  {n = 120, mu = 2.31, sd = 0.94, q = [1.7, 2.9]}
```

**Semantics.** When the right-hand side of `|>` is a record literal whose
field values are callables, apply each to the piped value; the result is a
record of results, same keys. A non-callable field value is a type error (do
not silently pass it through — explicit beats clever).

**Why.** One value fanning into several named summaries is the shape of
descriptive statistics; this composes a `describe()` out of syntax instead of
hardcoding one. Not known from any mainstream language.

**Design questions.**
- Record *literal* only, or any record-valued expression? Literal-only is
  easier to compile and to read; start there.
- Nested fan-out (`|> {a = {b = f}}`)? No — one level, keep it primitive.
- Interaction with `@`: a record literal containing `@` should probably be a
  parse error rather than half fan-out, half placeholder.

**Trigger.** Wishing for a one-line multi-statistic summary during real data
work. Predicted to fire first.

**Effort.** Small: parser already has both pieces; one new compile case; the
VM applies fields in order. An afternoon plus goldens and a manual section.

---

## 2. Index-bound reductions: executable sigma notation

```
sum[k = 1:n] 1 / k^2
prod[j = 1:m] (1 - q[j])
max[i = 1:n] f(x[i])
```

**Semantics.** Sugar for the reduction of a mapped range:
`sum[k = R] E  ==  sum(map(fn k -> E, R))`, with `k` scoped to `E`.

**Why.** It is the whiteboard notation, executable. Reads with zero
explanation for anyone who writes mathematics.

**Design questions.**
- Grammar collision with indexing: `sum[...]` looks like `A[...]`. Resolvable
  by lookahead for `NAME =` immediately inside the bracket, but the manual
  must document that `sum` the *value* can no longer be indexed with a
  binding-shaped expression (harmless in practice; ugly in the spec).
- Which reducers? `sum`, `prod`, `min`, `max`, `all`, `any` — a fixed list,
  or any callable? Fixed list first; generality invites `plot[k = 1:3] ...`.
- Multiple binders `sum[i = .., j = ..]`? No. One binder; nest for more.

**Trigger.** Long-form mathematical scripts where the `map(fn k -> ..)`
spelling is demonstrably harming readability, more than once.

**Effort.** Medium: new AST node, scope handling in the compiler, careful
parser lookahead. A full session.

---

## 3. `where` clauses: definitions after use

```
let y = a * x^2 + b*x + c  where a = 1, b = -3, c = 2
```

**Semantics.** Exactly `let a = 1 in let b = -3 in let c = 2 in <expr>`, with
the bindings written after the expression. Bindings are sequential (later
ones may use earlier ones) and scoped to the expression only.

**Why.** Papers put the formula first and the symbol definitions below; this
matches how model equations are actually read. Nearly free semantically —
it is `let .. in` mirrored.

**Design questions.**
- Reserved word `where` collides with the existing `where(mask, a, b)`
  builtin. Options: rename the builtin (breaking), pick another keyword
  (`with`? also nice), or allow `where` as both keyword-in-expression-tail
  and callee (grammar-ambiguous, reject). Honest answer: `with` avoids the
  fight entirely and reads almost as well.
- Precedence: how far left does the clause reach? Proposal: to the start of
  the enclosing `let` initializer or bare expression statement, never across
  `;`.

**Trigger.** A real script where a long formula's `let` preamble is
obscuring the mathematics.

**Effort.** Small-medium: parser tail on expressions, desugars to existing
let-in nodes. No VM work.

---

## 4. Oscillation pipe `~>`: an elementwise pipe

```
x ~> sin ~> (_ ^ 2) |> sum        # map sin, map square, then sum
```

**Semantics.** `x ~> f  ==  map(f, x)`; same right-hand-side rules as `|>`
(bare callable applies; `@` form binds the *element*).

**Why.** Extends the language's existing whole-vs-elementwise distinction
(`*` vs `.*`) to pipelines. Also: flavor oscillation, in a language named
Neutrino. Pure sugar — the weakest "need" case on this list, the strongest
"personality" case.

**Design questions.** `@` inside `~>` binds the element, not the whole
array — must be crisply documented or it will surprise. Chaining `~>` then
`|>` is the intended idiom and needs a manual example.

**Trigger.** Honestly: none foreseeable — `map` is fine. This one is for the
day the language wants a birthday present.

**Effort.** Tiny: one token, desugars in the compiler to the map call.

---

## 5. Chained comparisons

```
0 <= p <= 1                 # (0 <= p) && (p <= 1), p evaluated once
sum(0 < z < 1.96)           # elementwise band count
```

**Semantics.** `a OP1 b OP2 c` desugars to `(a OP1 b) & (b OP2 c)` with `b`
evaluated once; elementwise when arrays are involved (hence `&`, not `&&`).
Mixed-direction chains (`a < b > c`) are a parse error — they are always a
bug.

**Why.** Standard mathematical notation, and currently a *silent trap*: the
grammar parses `0 <= p <= 1` today as `(0 <= p) <= 1` and errors on
Bool-vs-Int comparison. Of everything in this file this is the least funky
and the most defensible — arguably a usability bug fix rather than a
feature.

**Design questions.** Only the single-evaluation of `b` (needs a temp in
codegen) and the elementwise `&` choice above.

**Trigger.** The first time the trap fires in real work. Note: because the
current behavior is an *error* rather than a wrong answer, the trap is loud —
which lowers the urgency.

**Effort.** Small: parser collects the chain, compiler emits with a temp.

---

## 6. Tee pipe `|>>`: a pipe that shows its work

```
data |>> detrend |>> standardize |> regress
```

**Semantics.** Exactly `|>`, but the value flowing through is printed (echo
style) before being passed on. Zero cost when unused; pipeline debugging
without dismantling the pipeline.

**Design questions.** Print the value or the value plus a stage label?
Value only — labels invite options, options invite scope creep in what is
supposed to be a debugging aid.

**Trigger.** Repeatedly breaking pipelines apart just to see intermediates.

**Effort.** Tiny.

---

## 7. Rejected for the record: uncertainty literals

`9.81 ± 0.02` as a numeric type with error propagation through arithmetic.
Physicist catnip and genuinely useful — and a whole parallel numeric tower
touching every builtin, every promotion rule, and the display layer. That is
a different language. Recorded here so the idea is honored and the rejection
is remembered.

---

*Policy reminder: the trigger for any of these is a real transcript of real
friction, brought to a design discussion — not this file's existence.*
