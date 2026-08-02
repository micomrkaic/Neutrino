# The Neutrino Playbook

*What this project proved, written down for the next one.*

Neutrino froze at 2.0.0 — the design docket clear, 887 goldens green, one
routine user. This document is the transfer artifact: the principles that
turned out to be load-bearing, the architecture worth lifting whole, the
process that kept quality monotone, and the traps we paid for so the
successor doesn't have to. Each principle cites the incident that proved it,
because a rule without its scar is just an opinion.

---

## I. Principles that were load-bearing

**The screen is the spec.** Couple observable state to what the user actually
saw, mechanically, at one site. `ans` is four lines *inside the echo branch*,
so it cannot desynchronize from the display — and when the where-clause
desugar later broke it, the user noticed within hours *because* the invariant
was legible. Design invariants users can feel; they become your best
detectors.

**No claim ships unexecuted.** Every transcript, help example, and worked
table cell in the documentation is run against the interpreter in `make
test`. This caught: a hard-coded version string, a transcript embedding
today's date (fails at midnight), a transcript embedding an absolute path
(fails on any other machine), and a package list that went stale the minute
a sixth package appeared. Prose drifts; executed prose cannot.

**Desugar first; opcodes are earned.** The entire six-feature campaign —
elementwise pipe, tee, fan-out, chained comparisons, where clauses, sigma —
taught the VM exactly one opcode (OP_TEE). Everything else compiles into the
existing kernel: `~>` is a map call, chains are a let and an `&`, `where` is
`let..in`, sigma is `R ~> (fn k -> E) |> f`. Consequences: the golden suite
survives radical surface change untouched, regression risk concentrates in
the parser where fuzzing is cheap, and features compose because they *are*
compositions. Measure elegance in features per opcode.

**Parked designs wait for friction transcripts.** DESIGN_NOTES held five
designs for months, each with a written trigger ("an actual session where
the lack was felt"). When we consciously overrode the discipline (the pipe
family shipped as a birthday), the notes say so. The docket-clearing at
2.0.0 was only meaningful because the docket was honest.

**The standard library is not the workspace.** Registration-time bindings
live below a protection boundary; `who` shows only the user's names,
shadowing appends rather than overwrites, and clearing a shadow resurrects
the original. We learned this the hard way: before the boundary, `clear()`
deleted `pi`, and clearing a shadowed builtin destroyed the builtin forever.
Build the boundary on day one.

**Any changed artifact is a new version.** A documentation-only refresh
reshipped under a released tag jammed the deploy pipeline — correctly. The
tarball is the release; docs are content; the version exists so two
different artifacts never share a name.

**Keep a catch ledger.** Seventeen-plus catches are recorded in LESSONS.md
with their mechanisms. Reviewing them is how the same bug class gets caught
in minutes the second time (stderr-before-stdout buffering was diagnosed
instantly on its second appearance). The ledger is the project's immune
memory.

## II. Architecture worth lifting whole

**The value model.** Immediates for scalars, refcounted heap objects for
strings/arrays/records/closures/envs, with two conventions stated *at the
declaration*: constructors return +1, `arr_get` returns borrowed. The one
serious VM bug of the late era (map over string arrays) was a caller
violating the stated convention — the model was right; enforcement is code
review of ownership comments. Lift value.c/value.h nearly verbatim.

**Arena + Pratt + parser-level desugar.** Arena-allocated AST (parse errors
longjmp, nothing leaks), a Pratt parser where precedence is one table, and
the desugar strategy above. The lexer is a pure value struct — checkpoint by
copy gave two-token lookahead for free when sigma needed it. Lift arena.c
and the parser skeleton; the grammar is the part you'll rewrite.

**One evaluation funnel.** REPL, batch driver, and browser all pass through
`vm_eval_program`. Every semantic feature (echo, `ans`, suppression) landed
once and appeared on three frontends. Never let a frontend grow its own
evaluation path.

**The environment boundary** (§I above): `n_protected` on the env, backward
lookup, append-over-protected. Small, and it defines the stdlib/workspace
distinction correctly forever.

**Reserved-name space.** User identifiers cannot contain `@`; therefore
`_@e`, `_@c1`, section parameters, and any future compiler temp are
collision-free by construction. Reserve an unwritable character on day one.

**Generated artifacts have generators in the repo.** The builtin reference,
the Emacs mode's name list, and the package worked-example tables are all
produced by `tools/*.py` with `--check` modes wired into `make test`. The
rule that made this necessary: *when generated output misrenders, audit the
generator before the renderer* — both documentation disasters originated in
generation, not rendering.

## III. The verification lattice

Layers, and what each one actually caught:

- **Goldens** (`tests/run.sh`, one fresh session per line): the bulk
  regression net; also the arbiter of "bug fix" in maintenance mode — the
  language is now *defined* as what passes them. Known limit: fresh sessions
  cannot express stateful chains; know this before designing stateful
  features.
- **Verified transcripts** (`verify_manual.py`, persistent session per
  block): the harness for exactly what goldens can't say — `ans` chains,
  working-directory flows. Documentation and stateful testing are the same
  artifact.
- **Executable help examples** (`%=` claims in the doc table): caught the
  hard-coded version string.
- **Worked-example tables** (`gen_package_tables.py --check`): every package
  function's example re-executed forever.
- **doclint**: table structure across all documents; found 55 shattered rows
  in one audit.
- **Rendered-output guards**: grep the *actually rendered* manual for leaked
  escapes — and verify the verifier runs (a stale `exec` made two guards
  dead code since birth; a guard that never fires certifies).
- **ASan on everything**, including fuzz batches: `-O1` matters (setjmp
  clobber bugs hide at `-O0`).
- **Targeted fuzz per grammar change**: 500–900 generated programs mixing
  the new construct with everything; assert clean errors, never crashes.
- **The clean room**: every release ends with tarball → extract → build →
  full suite in a fresh directory. It caught the absolute-path transcript
  and proved generator idempotency. Non-negotiable.
- **The user**: the echo-coupled design made a semantic seam visible at the
  calculator within hours. Instrument for legibility; your user is the top
  layer of the lattice.

## IV. Process

**The release rite** (in order): implement → exercise interactively →
goldens (positive and negative, teaching error messages tested) → fuzz if
grammar changed → suite + ASan → manual section with transcripts captured
*by execution* → regenerate reference/mode/tables via tools → CHANGELOG →
version bump → wasm rebuild (docs and packages are embedded — a stale
bundle serves stale docs) → PDFs → package tarball → clean room → present.
Skipping steps is how the dead-`exec` class of bug survives.

**Version discipline**: semver; any shipped delta bumps; `git config
tag.sort version:refname` once per clone (v1.10 sorts after v1.9).

**Grep before designing.** Every feature began by reading the code that
exists — and twice the recon changed the design materially (block-expr
scoping made chains a pure desugar; `bi_where`'s 1-arg form was already
`find`, collapsing the rename's cost).

## V. The trap almanac

Paid for once; free forever:

- **Stale binaries.** Rebuild before every test run; a passing stale binary
  is a lie. (Bitten repeatedly.)
- **Display-layer escaping.** repr, JSON tool output, and shell `-c` each
  multiply backslashes. When the bug is bytes, count bytes (`od -c`).
- **Buffered stdout dies with the process; stderr doesn't.** A segfault
  makes successes vanish and errors survive, reordering reality.
- **Make's default goal is the first target.** A mkdir pattern rule silently
  became `make`'s job. Pin `.DEFAULT_GOAL`.
- **Equal mtimes read as up-to-date.** Dependency tests need `sleep` before
  `touch`.
- **Borrowed values in owning pools.** Constant pools release on free;
  retain what you deposit.
- **Shadowing by environment lookup is not immunity.** `let map = 7`
  replaced the binding; `~>` had to mint the primitive, not find it.
- **Names vs slots.** Runtime name lookup cannot see slot-allocated
  parameters; know which regime each identifier class lives in.
- **Time- and path-dependent goldens.** `datestr(today())` fails at
  midnight; `cd` echoes absolute paths. Assert monotone properties;
  suppress environment-dependent echoes.
- **First-occurrence string replacement.** Patterns recur (bp switch vs
  led switch); anchor edits uniquely or verify placement.
- **A registration boundary set mid-registration** truncated half the
  stdlib — including `clear` itself. Boundaries go at the true end.
- **`exec` in test scripts** makes everything after it dead code.
- **Correlated capture and verification.** If the transcript recorder and
  the transcript checker share a harness defect, they certify each other's
  garbage — the manual shipped corrupted output for three releases under a
  green check. Sentinels must be inert (`print`, never an echoing
  expression); when observable state is added to the language, audit every
  instrument that drives a session.

## VI. What to lift, what to re-derive

**Lift nearly verbatim**: `arena.c`; `value.c/.h` (value model, refcounting,
env with boundary); the test-harness family (`tests/run.sh`,
`verify_manual.py`, `run_doclint.py`, the `tools/gen_*.py --check`
pattern); the Makefile (objects, `-MMD -MP`, shared core, separate ASan
tree, pinned default goal); `deploy.sh`'s tag discipline; this file and
LESSONS.md.

**Lift the skeleton, rewrite the flesh**: lexer and Pratt parser; the
chunk/VM shape; the doc-table-drives-everything pattern (help, reference,
completion, editor mode from one source of truth).

**Re-derive against new goals**: the grammar; the type story (a heavier
language likely wants real types — that's the point of building it); the
package/module system (Neutrino's load-into-session is right for a
calculator, wrong for a big language — the parked load-path design is the
starting sketch).

## VII. What the successor should do differently

Honest debts, so they're chosen consciously next time:

- The golden runner's fresh-session-per-line forced stateful tests into the
  manual harness. Design the golden format with an explicit session block
  from day one.
- `@` resolves by name at runtime while parameters live in slots — two
  binding regimes met awkwardly under `~>`. Pick one regime.
- The doc table lives in C string literals; escaping rules cost several
  bugs. Put it in a data file the C build embeds.
- The wasm toolchain is held together with `dpkg --force-depends`; budget
  for a pinned emscripten from the start.
- The catch ledger and design notes were maintained by discipline; the
  successor could lint for them (e.g., refuse release if CHANGELOG lacks
  the version).

---

*The methodology above is the actual product of the Neutrino project; the
language is its first application. Build the heavy one on it.*

### Trap: two-workstation deploys diverge

Deploying from machine A then machine B leaves B's remote ahead of A;
A's next push is rejected as a non-fast-forward. Because every release
is a FULL SNAPSHOT (the tarball is the complete intended state), the
correct resolution is never a content merge: `git merge -s ours
origin/main` records the remote history while keeping the local tree
byte-for-byte, then push. deploy.sh automates this on push failure,
printing the superseded commits. The one hazard: work unique to another
machine that never entered a tarball is discarded by `ours` — the rule
is therefore that nothing lands in the release repo except through a
deploy. Never resolve with `push --force`: it works, erases history, and
teaches a bad reflex.

### Trap: the grep that hid the verdict

v2.19.0 shipped with a red lattice line — "book index: STALE" — because
the release rite's suite step piped make test through a grep for
expected-looking lines and read the survivors as green. The failure
matched no pattern; the pipe swallowed the exit code; the owner's deploy
found it in one honest run. The law: THE SUITE'S VERDICT IS ITS EXIT
CODE. Filters may decorate a green run; they must never stand between a
red one and the eyes. Run make test to a log, test $?, and only then
summarize.

### Delivery names carry the version

Tarballs are named neutrino-vX.Y.Z.tar.gz (and cozy-vX.Y.Z.tar.gz),
never bare neutrino.tar.gz — the owner keeps the full record of
submissions by filename, and the two-workstation deploy taught what
same-named files in a Downloads folder can do. Session restore
therefore globs for the newest: 
    tar xzf "$(ls /mnt/user-data/outputs/neutrino-v*.tar.gz | sort -V | tail -1)" --strip-components=1
The version in the name must equal version.h inside — deploy.sh's
banner is the cross-check.

