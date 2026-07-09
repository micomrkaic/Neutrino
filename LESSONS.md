# Lessons — a retrospective on building Neutrino

*Written at v1.0, while the scars are fresh. Neutrino is a functional array
language with Octave-flavoured syntax: a lexer, arena, Pratt parser, AST
evaluator, bytecode compiler, and stack VM in portable C23, with 113 builtins
spanning linear algebra, special functions, statistics, solvers, plotting,
and data I/O. Final state: 588 golden tests, 3 codegen goldens, 56 verified
manual transcripts, 103 verified help examples, ~33,000 fuzzed inputs clean
under ASan+UBSan, zero dependencies beyond libm. This document records what
that cost and what it taught.*

---

## 1. Founding decisions that paid off

**Zero dependencies.** `make` works on a bare Ubuntu or Mac. Every algorithm
— LU, QR, SVD, Hessenberg-QR eigensolver, incomplete gamma/beta, Brent's
solvers — is code we wrote and sanitizers walked through. The cost is
performance (hand-rolled `inv` measured ~30x slower than LAPACK-backed
Octave); the payoff is that the whole system fits in one head and one
debugger. For a learning project this trade is correct without qualification.

**Records as the multi-value convention.** `eig(A) -> {values, vectors}`,
`lu -> {L, U, p}`, `fminbnd -> {x, fx}`. One idea, used everywhere, and it
composed into an unplanned bonus: a record of column vectors turned out to
*be* the data frame — `readtable` plus existing mask indexing gave
`d.cpi[d.year >= 2021]` with no new types at all. The best feature of the
data-frame story is that it required no features.

**Reproducible-by-default RNG.** Fixed seed at startup, `rng(k)` to reseed.
Made stochastic goldens possible, made bug reports deterministic, and cost
nothing. Any language with a test suite should do this.

**The pipe with a placeholder.** `x |> f(@) |> g` earned its keep daily.
Notably, the `@` placeholder also *prevented* a feature: matrix
multiplication never needed an operator beyond `*` because the pipe absorbed
the composition patterns that tempt people into operator zoos.

**Strict Bool discipline.** `1 == true` is an error; conditions must be
Bool. Caught real mistakes in real sessions and cost one paragraph of
documentation.

**Autocall for zero-argument callables, statement position only.** `who`,
`tic`, bare closures. The restriction to statement position is what kept it
sound — names in expression position stay values, so `map(sqrt, x)` works.
The lesson is the shape of the compromise: convenience at the top level,
purity in expressions.

## 2. Founding decisions we would change

**Strings should have been first-class from day one.** The single
load-bearing regret. "Inert strings" looked like admirable scope discipline
and the friction never stopped compounding: `readtable` must reject a
country-code column by name; plot legends needed the `label1..labelN`
contortion because there are no string arrays; `==` on strings is an error
that surprises everyone. The reason it is a *founding* decision: retrofitting
touches every builtin's type dispatch, the array element tower, comparison,
sorting, and display. Scope discipline applied to a foundation is not
discipline, it is deferral with interest.

**The error-handling strategy is the architecture.** The largest bug source
of the entire project, by an order of magnitude, was setjmp/longjmp
unwinding over manually managed memory:

- Four independent setjmp-clobber bugs (handlers reading register-cached
  locals — including one in the oldest indexing code, exposed only when the
  sanitizer build gained -O1).
- Error-path leaks in every generation of code: elementwise ops, slice
  assignment, `mrdivide`/`inv`/`mpow`, CSV readers, plotting, and parser
  scratch vectors on *every* parse error.
- A guard idiom (`saved`/`setjmp`/`volatile`/`array_build_abort`) that must
  be re-remembered at every allocation site, forever.

None of these are individually hard; the point is the *class* never closes.
A successor should make one foundational move that eliminates it: garbage
collection (error paths become trivially safe — the unreachable is
collected), an arena-per-evaluation ownership model, or a host language
where unwinding is safe by construction. This one decision is worth more
than any amount of syntax.

**Sizes were int-shaped when they should have been one guarded path.** The
fuzzer found uint32 truncations that amounted to a latent heap overflow
(`1:4294967306` allocated 10 slots and would write billions), signed-overflow
UB in the documented wraparound semantics, and an `ipow` that would "wrap"
astronomically large exponents sometime next century. The fix — one
`DIM_MAX`, one `as_dim`, one `check_cells`, unsigned arithmetic for
documented wrap — took an afternoon. It should have been the first
afternoon, not the last.

**Undecided, honestly: the Int/Float split.** Exact integers were pleasant;
they paid in wrap semantics, promotion rules, and the `4 / 2 -> 2.0`
explanation. Lua's doubles-only heresy deletes the entire category. For a
numerical language this is a genuine trade, not a mistake — but a successor
should decide it consciously rather than inherit it.

## 3. The methodology that worked — portable to any project

**Machine-verified documentation is the project's best invention.** Three
documents cannot drift from the implementation: the manual (every REPL
transcript executed and diffed by `make test`), the builtin reference
(generated from the interpreter's own doc table), and the help examples
(every `%=` line executed and compared). The system caught the *author*
confabulating four separate times: a matrix operator that did not exist, a
comment character misremembered as modulo, ten trailing-zero format errors,
and a random value invented outright. Write-from-memory is not a personal
failing to overcome; it is a constant to engineer around.

**Goldens run per-feature, sanitizers run per-commit, fuzzers run
per-milestone.** The division of labour was right. What was wrong until the
end: the sanitizer build at -O0, which hid every optimization-dependent bug
(the clobber class) while giving full confidence. **Sanitizers test the code
the compiler generated, not the code you wrote — build them at -O1.**

**Error paths are the primary attack surface.** Essentially every leak and
both memory-safety findings lived on paths where something had already gone
wrong. The habit that works: when writing any error check after an
allocation, stop and trace the unwind. The habit that works better:
architecture where the trace is unnecessary (see §2).

**The clean-room ritual.** Every deliverable was rebuilt from the packed
tarball in a fresh directory and the full suite run there. It caught missing
files, stale binaries, and untracked fixtures roughly once a week for the
life of the project. Cost: ~30 seconds per session.

**Fake the external tool, assert on what you send.** When gnuplot output
was unverifiable (occluded legend in a terminal render), substituting a
`gnuplot` that captured stdin settled in one command what rendering
speculation could not. Test the interface you control.

**Fix the message, not just the bug.** `plot: gnuplot failed (exit 127) —
is gnuplot installed?` exists because the failure was once a silent SIGPIPE
death. Every hard-won diagnostic came from actually running the failure.

## 4. Process lessons

**Wait-for-real-need survived contact with enthusiasm — barely.** The
policy ("features earn their way in through transcripts of friction") said
no to QZ, LAPACK, sparse matrices, date/time, an inline-assignment operator,
and, at the very end, an entire list of appealing syntax (recorded in
DESIGN_NOTES.md with named triggers). Each refusal looks obvious in
retrospect; none felt obvious at the time. The mechanism that made refusal
possible was writing the design down anyway — a parked idea does not nag.

**The predictable failure mode is forgetting your own codebase.** `kron`
was proposed and half-reimplemented while a tested version already existed
(silver lining: the new one fixed a real overflow bug in the old). Memory of
one's own project is as unreliable as memory of facts; grep before design.

**A false-positive warning is worse than no warning.** The staleness check
that cried wolf across binaries got scoped per-binary the day it first
misfired. Warnings survive only while they are always right.

**Benchmarks beat adjectives.** "Hand-rolled is slower" settled nothing;
`tic; inv(rand(400)); toc` -> 0.11 s settled everything, including the
decision *not* to act yet.

## 5. If there is a successor

Inherit unchanged: the verification-first documentation system, the golden
suite discipline, reproducible RNG, records for multi-returns, the
wait-for-need policy, fuzzing from week one, sanitizers at -O1, the
clean-room ritual, one central size guard.

Decide consciously at founding: memory/error architecture (the big one),
first-class strings, the numeric tower, and — before any of those — what the
language is *for*. Neutrino's answer was "recreation, learning, a personal
Octave," and every good decision above traces back to the clarity of that
answer. A successor built to "apply lessons" has no answer; a successor
built because building is the hobby should be maximally *different* — lazy,
stack-based, APL-family, anything that teaches new lessons rather than
re-teaching these.

And mind the second-system effect. This file is its antidote: the ambition
is written down, so it does not have to be built.


## Postscript (v1.3.0): the strings ledger, closed

The founding regret was repaid in three phased releases — scalar operations
(1.1.0), string arrays with refcounted elements (1.2.0), and the payoff
(1.3.0): `readtable` string columns, quote-aware CSV, `strsplit`/`strjoin`,
composable `fields`. The retrofit worked *because* of the other founding
decisions: the strictness doctrine meant every operation already had a
tested refusal to replace with a behavior, and the golden suite plus
sanitizer-and-fuzz discipline caught a use-after-free, a double-free, and
two silent-garbage paths before any shipped. strings ledger: closed.
