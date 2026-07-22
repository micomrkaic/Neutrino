# Changelog

Notable changes to Neutrino. Newest first.

## Unreleased

### Fixed
- **v1.12.1: documentation refresh.** The README caught up with its own
  language: the showcase example now leads with the elementwise pipe
  (`[1,2,3,4] ~> (@ ^ 2) |> sum`), the intro covers the pipe family, `ans`,
  and the Emacs mode, and the Language tour gained the three new pipes —
  every example executed before being written down. The manual's table of
  contents lists the Editors section, the build instructions show
  `make -j$(nproc)`, and LESSONS.md gained §7 (the documentation lattice and
  the recent catch ledger). No code changes; the version exists because a
  changed tarball must not reuse a released tag.
- **v1.11.1: incremental, parallel builds.** The Makefile compiled every
  translation unit in one monolithic command per binary — any edit rebuilt
  everything, serially, three times over. Now each .c compiles once into
  build/obj/ with compiler-generated header dependencies (-MMD -MP), the
  core objects are shared between `neutrino` and `vmtest`, the sanitizer
  build lives in its own build/asan/ tree, and `make -jN` parallelizes.
  Measured on one core: full build 34.7s to 22.2s, a `repl.c` edit 7.9s to
  0.6s, an `eval.c` edit 30.9s to 14.6s; multicore machines gain the -j
  factor on top. One bug caught mid-review: the mkdir rule had silently
  become the default goal (`make` built a directory); `.DEFAULT_GOAL` now
  pins `neutrino`. All external interfaces unchanged: make, vmtest,
  vmtest-asan, test, test-asan, wasm, clean.
- **v1.9.2: documentation audit — generated tables.** The builtin
  reference's doc rows use ` | ` for signature alternatives, and the
  reference generator emitted them into markdown cells unescaped — 55
  structural violations across the reference (rows shattered into phantom
  columns in the Docs tab, exactly as reported). The generator is now a repo
  tool (`tools/gen_reference.py`) that escapes cell contents, and a new
  documentation linter (`tests/run_doclint.py`, in `make test`) audits every
  table in all five documents for column-count consistency, unescaped pipes
  in code spans, and unbalanced backticks — 55 problems found, zero remain.
- **v1.9.1: documentation renderer escapes.** Markdown table cells use `\|`
  for a literal pipe; both the REPL's ANSI renderer and the browser's Docs
  tab were passing the backslash through (and the browser split cells on the
  escaped pipe, mangling the REPL-commands table). Both renderers now honor
  `\|` — including inside code spans, where the first fix didn't reach —
  and render `[text](url)` links as their text. Permanent guards in both
  suites: `run_manual.sh` greps the rendered manual for leaked escapes, and
  the page test asserts `mdToHtml` keeps escaped-pipe cells whole.
- **v1.8.3: editor results echo in the terminal (browser).** Running the
  editor previously went through `load("/_editor.nu")` — script semantics,
  which are silent for bare expressions (natively too, by design). The
  editor now sends its buffer directly to the evaluator, which treats it as
  a program typed at the prompt: every statement's value echoes in the
  terminal, `let` bindings included, exactly like interactive use. The page
  test gained a second phase that drives the real wasm bundle through the
  real page and asserts the echoed values.
- **v1.8.1: editor focus fix (browser).** The page's "click anywhere to
  focus the terminal" handler was stealing keystrokes from the script
  editor — clicking into the editor bounced focus back to the REPL, so typed
  text appeared in the terminal. The handler now leaves clicks in the right
  panel and on any interactive element alone; clicking the terminal area
  still focuses the prompt. Verified with a headless DOM focus matrix
  (editor, docs picker, terminal).
- **REPL history recall included a trailing newline.** The accumulated input
  buffer (which carries a `\n` per physical line for the parser) was passed to
  `add_history` untrimmed, so an up-arrow recalled the command plus a newline
  that had to be backspaced away. History entries are now trimmed before being
  added; multi-line constructs remain single entries in-session.

### Added
- **v1.13.0: rmt.nu — random matrices, structured.** A fifth standard
  package: `randsym`, `randspd` (chol-safe by construction), `wishart`,
  `randorth` (Haar, QR sign-fixed), `randperm` (ranks of uniform draws — an
  oscillation-pipe one-liner), `permmat`, `randcorr`, `randstoch` (random
  Markov chains), and `goe` — the Gaussian orthogonal ensemble, scaled so
  the spectrum follows Wigner's semicircle. Ten property-based goldens
  (symmetry, chol roundtrip at 1e-16, Q'Q = I, permutation validity, unit
  diagonals, row sums, spectral radius), nine machine-verified worked
  examples, and a verified transcript demonstrating the semicircle edge on
  a 200×200 draw. All reproducible under rng(seed).
- **v1.12.0: the pipe family.** Three pipes from the design notes, shipped
  together. The **elementwise pipe `~>`**: `x ~> f` is `map(f, x)`, and `@`
  under `~>` binds the *element* — the whole-vs-elementwise distinction
  (`*` vs `.*`) extended to pipelines; in a language named Neutrino, the
  elementwise pipe oscillates. `~>` compiles the map primitive itself, so
  shadowing the name `map` cannot change the operator. The **tee pipe
  `|>>`**: exactly `|>` but the flowing value is printed before being passed
  on — pipeline debugging without dismantling the pipeline (one new opcode,
  OP_TEE). **Fan-out**: `x |> {n = length, mu = mean}` applies each record
  field to the piped value and returns a record of results — a `describe()`
  composed from syntax; one level, whole-value only, `@` in the record
  rejected, non-callable field a type error. Also: readline's
  blink-matching-paren is on in the REPL. 15 new goldens, 5 verified manual
  transcripts, a 700-program fuzz pass over the new operators under ASan.
- **v1.11.0: an Emacs mode.** `editors/neutrino-mode.el` — syntax
  highlighting with the builtin list machine-generated from eval.c
  (`tools/gen_emacs_mode.py`, drift-checked in `make test`), `%` comment
  syntax, a syntax table that knows `'` is transpose and not a string quote,
  bracket-and-block-aware indentation, and an inferior REPL over comint
  (`M-x run-neutrino`; `C-c C-r`/`C-c C-b`/`C-c C-l`/`C-c C-z`). Batch
  tests verify fontification at known positions, an indentation golden, and
  a live comint session against the interpreter — skipping politely where
  Emacs is absent.
- **v1.10.0: quality of life, round two.** (1) `version` returns the
  interpreter version as a string. (2) Workspace shorthands: `whov`, `whof`,
  `whor` (vars, functions, records; each takes an optional `"sorted"`) and
  `whos` (everything, sorted). (3) `now` — a new core builtin — returns the
  local date and time as `{y, m, d, h, mi, s}`; finance.nu builds `today()`
  on it, returning a serial day number so date arithmetic chains:
  `datestr(today() + 90)`. (4) finance.nu's date API split by return type:
  `datestr(jdn)` is now a zero-padded string ("2026-07-17"), `daterec(jdn)`
  the `{y, m, d}` record, `dateadd` returns the record. (5) PACKAGES.md's
  function tables carry a worked example with its actual result for every
  command — 48 examples, generated by execution via the new repo tool
  `tools/gen_package_tables.py`, whose `--check` mode re-runs them all in
  `make test`, giving the tables the same machine-verified status as the
  transcripts.
- **v1.9.0: `ans`.** The last value you saw and didn't name. Every echoed
  expression statement rebinds `ans`; `let` doesn't (named results aren't
  anonymous), semicolon-suppressed statements don't, and `load()`ed scripts
  don't (they don't echo) — one rule, echo-coupled by design, so `ans` can
  never hold something that didn't print. This deliberately fixes two Octave
  warts: suppressed statements silently mutating `ans`, and scripts
  clobbering it as a side effect. `ans` is an ordinary global (visible in
  `who`, removable with `clear`), implemented as four lines at the echo site
  in `vm_eval_program`, so the REPL, vmtest, and the browser inherit
  identical behavior. Negative clauses are goldens; positive chains are
  machine-verified manual transcripts (81 now); the browser page test chains
  `ans` through the live terminal.
- **v1.8.0: the whole project in one file.** The browser bundle now embeds
  the four standard packages and the five documents (manual, packages guide,
  changelog, lessons, design notes) — `load("packages/dist.nu")` works in the
  browser exactly as natively, every PACKAGES.md transcript is browser-valid,
  and nothing can version-skew because interpreter, library, and docs travel
  together (~650 KB total, offline-capable). A third **Docs tab** renders any
  of the five documents as clean HTML via a small JS markdown renderer (the
  C ANSI renderer's subset, ported); typing `manual` or `manual packages` in
  the browser terminal opens it directly. `make wasm` carries the
  `--embed-file` flags.
- **v1.7.0: the browser grows up (structure borrowed from tea).** The web
  page is now a two-pane workbench: the terminal on the left; on the right, a
  **Plots panel** and a **script Editor** in tabs. `plot` and `hist` gained a
  third backend that writes SVG (`NEUTRINO_PLOT_TERM=svg` natively; the
  browser default) — nice 1-2-5 ticks, gridlines, multi-series polylines with
  a palette, legends from `labels`, XML-escaped text — announced to the page
  through a `Module.neutrinoPlot` hook, tea's protocol. The editor persists
  in localStorage, runs with Ctrl+Enter (selection or whole buffer, via
  `load("/_editor.nu")` — multi-line definitions welcome), and opens/saves
  `.nu` files. **File exchange**: an upload button and whole-window drag &
  drop (`.nu` opens in the editor; data files land in MEMFS for `readtable`/
  `load`), and "download new files" fetches everything the session created —
  workspace saves, CSVs, and the SVG plots themselves. A native SVG smoke
  test joins `make test`; `make wasm` now uses `-std=gnu2x` (EM_ASM needs
  GNU extensions).
- **v1.6.2: REPL commands are first-class names.** `help(manual)`,
  `help(pretty)`, and `help(more)` now work, the commands appear in the help
  tour under "repl commands", and tab completion knows them — they are
  registered as builtins that print a pointer to the REPL when called from a
  script. The sweep also found that `exit` was never handled at all (it
  errored as an undefined name); `exit`/`quit` are now real builtins —
  `exit(code)` sets the process exit status — so they work in the REPL, in
  scripts, and in completion alike.
- **v1.6.1: rendered documentation in the REPL.** The `manual` command now
  renders markdown to formatted terminal text — colored headers, tinted code
  blocks, bullets, clean inline code — through `less -R` (plain formatted
  text when piped), and takes a document name: `manual`, `manual packages`,
  `manual changelog`, `manual lessons`, `manual design`, `manual readme`.
  The renderer is ~90 lines of C handling exactly the markdown subset the
  project's documents use; no external tools.
- **v1.6.0: the packages release.** Four standard packages ship in
  `packages/` — `dist.nu` (probability distributions), `poly.nu`
  (polynomials), `finance.nu` (HP-12C: TVM, cash flows, bonds, amortization,
  dates), and `astro.nu` (solar almanac and the daylight driving window) —
  documented in a new PACKAGES.md whose 61 transcripts are machine-verified
  by `make test` alongside the manual's, with a typeset PACKAGES.pdf. All
  package numerics are golden-tested against independent references (SciPy,
  NumPy, Python datetime, astral). 778 tests total.
- **`packages/astro.nu`** — solar and lunar almanac in pure Neutrino: the
  NOAA solar position algorithm (with one refinement pass) gives `sunrise`,
  `sunset`, civil/nautical/astronomical `dawn_*`/`dusk_*`, `solar_noon`,
  `day_length`, and `sun_position` (altitude/azimuth), all within about a
  minute of the astral reference library; `moon_age`/`moon_illum`; `hm` for
  "HH:MM" display; a `places` record of preloaded coordinates; and
  `drive_daylight(from, to, y, m, d, tz)` — the daylight driving window from
  civil dawn at the origin to civil dusk at the destination. Twenty-one
  goldens, including the physics identities (solar noon bisects the day, the
  sun due south at maximum altitude at local noon) and honest refusals for
  polar day. Fourth package; zero interpreter changes again.
- **`packages/finance.nu`** — the HP-12C's greatest hits, in pure Neutrino:
  TVM (`pmt`, `pv`, `fv`, `nper`, `rate` — the last with an adaptive fzero
  bracket so 360-period mortgages don't overflow the probe), cash flows
  (`npv`, `irr` with sign-change validation), bonds (`bond_price`,
  `bond_ytm`, Macaulay/modified `duration`, `convexity`, with the par-bond
  identity golden-tested), `amort` returning the full schedule as a record
  of columns (plot the balance!), and HP-12C date arithmetic (`datenum`/
  `datestr` via Julian day numbers, `days`, `dateadd`, `dow`, and the 30/360
  US day count `days360`). Twenty-nine goldens, cross-checked against
  SciPy and Python's datetime. Zero interpreter changes — third package of
  the era, second in a row to touch no C.
- **`packages/poly.nu`** — polynomials, written entirely in Neutrino:
  `companion`, `roots` (companion matrix + `eig`, the same algorithm Octave
  uses, on the LAPACK-verified eigensolver), `polyval` (Horner, scalar or
  elementwise), `polyfit` (Vandermonde + least-squares backslash,
  NumPy-checked to 8 digits), `polyder`/`polyint` (mutually inverse, golden-
  tested), and `conv`. Sixteen goldens. Zero interpreter changes — the first
  delivery of the packages era to touch no C at all.
- **v1.5.0: workspace filters and completion.** `who` takes selectors —
  `who("records")`, `who("functions")`, `who("vars")`, plus `"sorted"` for
  alphabetical order, combinable (`who("functions", "sorted")`); bare `who`
  is unchanged. Tab completion in the REPL now covers file paths: inside a
  double-quoted string, TAB completes filenames (`load("packages/di<TAB>`
  finishes the path and the closing quote), while outside quotes it completes
  builtins, your variables, and keywords as before — a pty-driven smoke test
  guards both modes in `make test`. If completion previously did nothing for
  builtins, your installed binary predates the completion wiring; this
  release includes it.
- **v1.4.0: `error`/`assert`, multi-line expressions, and release tooling.**
  Packages now validate inputs like builtins do: `error(tmpl, ...)` raises
  with fmt-style templating and `assert(cond, tmpl, ...)` is its guard form —
  `dist.nu` quantiles now say "p must be in (0,1)" instead of crashing.
  Newlines are plain whitespace inside any open bracket (DESIGN_NOTES entry 8,
  resolved): expressions, matrices, and records span lines in files, and the
  REPL reads continuation lines automatically; matrix rows still take an
  explicit `;`. The `help` tour now lists the strings, solvers, data-files,
  and plotting categories it was missing; a new `manual` REPL command pages
  MANUAL.md ($PAGER, with a plain-print fallback); and `deploy.sh` releases a
  tarball to GitHub tagged with the version baked into version.h.
- **Strings, phase 3 (v1.3.0): cashing the cheques.** `readtable` now loads
  non-numeric CSV columns as string arrays — the original motivating wound,
  healed (two-pass column classification; empty cells stay `nan` in numeric
  columns and `""` in string columns). CSV reading is quote-aware
  (RFC-4180-lite: delimiters inside quoted cells, doubled quotes) and
  `writecsv` writes string matrices with matching quoting, so string data
  round-trips. `strsplit`/`strjoin` convert between strings and string
  arrays; `fields(r)` now *returns* the field names as a composable string
  column (behavior change from the print-only form); plots accept
  `{labels = ["low", "high"]}` for legends. The strings ledger is closed.
- **Strings, phase 2 (v1.2.0): string arrays.** A new array element type
  whose cells are refcounted strings. Literals (`["a", "b"; "c", "d"]`,
  homogeneous — mixing with numbers errors), indexing/slicing/`end`/masks/
  permutations, transpose, `reshape`, `sort` and `unique` (lexicographic),
  and elementwise operations: `names == "si"` gives a Bool mask,
  `names[names == "si"]` filters, `["pre_", "un_"] + "fix"` broadcasts
  concatenation. Assignment respects kinds (String cells never silently
  become numeric or vice versa; copy-on-write preserved), and every numeric
  reduction (`min`, `max`, `norm`, `hist`, …) refuses string arrays instead
  of reinterpreting pointers as doubles — the review found and fixed one
  use-after-free (borrowed scalar escaping `min`) and one silent-garbage
  path (`norm` returning 0) before they could ship. Zero new builtins: the
  whole phase is semantics. 24 new goldens; 4,000 string-array fuzz
  programs, ASan-clean.
- **Strings, phase 1 (v1.1.0).** Scalar strings are no longer inert: `+`
  concatenates, comparisons are lexicographic byte-wise (shorter prefixes
  first), and indexing reuses the array machinery — `s[i]`, `s[a:b]`,
  `s[end]`, masks and permutations all work. Library: `upper`, `lower`,
  `trim`, `contains`, `startswith`, `endswith`, `strrep`, plus the bridges
  `str(x)` (display text of any value) and `num(s)` (Int if exact, else
  Float), and `fmt(tmpl, ...)` — print's template engine returning a string.
  Byte semantics throughout (UTF-8 passes through, is not interpreted); the
  strictness doctrine holds: string-number arithmetic is still an error, no
  implicit conversion in either direction. ASan-clean; 5,000 string-grammar
  fuzz programs, zero hits. Phase 2 (string arrays) and phase 3 (readtable
  string columns, composable fields) to follow.
- **`fields(r)`** lists a record's field names with type and shape,
  who-style — column names of a `readtable` frame, functions of a package
  module, parts of a decomposition — without printing the data. (The natural
  composable form would return an array of name strings; impossible while
  strings are inert — another entry in the strings-first ledger.)
- **First package: `packages/dist.nu`.** Probability distributions — pdf,
  cdf, quantile, and sampling for normal, Student t, chi-squared, F,
  exponential, and uniform — written in Neutrino itself on the special-function
  builtins; quantiles with no closed form use recursive bracket expansion plus
  `fzero`. Every value cross-checked against SciPy (16 goldens, plus identity
  checks: quantile inverts CDF, t symmetry, pdf integrates to CDF). Writing it
  fired the first DESIGN_NOTES trigger: multi-line expressions inside brackets
  (recorded there as entry 8).
- **Workspace save/restore and function introspection (v1.0.3).**
  `save("ws.nu")` serializes every variable and function as reloadable
  Neutrino source (restore with `load`); serialization is atomic — built in
  memory, written only on success, so a failing save never leaves a truncated
  file. Closures now retain their source text (a zero-copy span captured at
  parse time), which powers both `save` and the new `body(f)` that prints a
  function's definition. Closures with captured variables refuse to
  serialize, with a message naming the variable; functions referencing
  globals save fine (dynamic lookup, not capture). Version bumped to 1.0.3.
- **`load("file.nu")` — packages.** Runs a file in the current session;
  bindings persist, so a file of `let` definitions is a package and a record
  of closures is a namespace (`geo.hyp(v)`). Works in the REPL, scripts,
  vmtest, and the browser (reads MEMFS). Nested loads are capped at 16 with a
  clean circular-load error; parse and runtime errors report the file name.
  Implementing it exposed a latent core bug: `vm_compile` clobbered the
  interpreter's unwind target and never restored it, so any longjmp *after* a
  nested `vm_eval_program` returned jumped into a dead stack frame (glibc
  fortify abort). `load` was the first caller ever to do that; `vm_compile`
  now saves and restores the caller's `jmp_buf` on every exit — the
  setjmp-discipline bug class from LESSONS.md, sixth occurrence.
- **Conditioning stress campaign.** Head-to-head against LAPACK (NumPy) on
  identical matrices: backslash residuals match LAPACK's order (1e-16) on
  Hilbert matrices up to cond 1.6e16 and a graded matrix at cond 1.6e28;
  general eigenvalues agree to ~5e-15 relative through n=40; a defective
  Jordan block, repeated eigenvalues, and rank-deficient SVD are exact
  (pinned as goldens). Found along the way: input lines were silently capped
  at 8 KB by fgets buffers in vmtest and the REPL's non-readline fallback —
  both now use getline (unbounded), with a regression check in make test.
- **REPL quality of life.** `clear()` / `clear("a", ...)` removes user
  variables (builtin bindings are untouchable — `clear("sum")` refuses);
  `mem` prints workspace size (payload bytes of all variables) and peak
  process memory; the splash banner shows version, build timestamp, and
  session start; `neutrino --version` prints the same.
- **v1.0 hardening campaign.** ~33,000 fuzzed programs (grammar-aware and
  byte-garbage) under ASan+UBSan plus 108 property-based linear-algebra
  identity checks on random matrices. Found and fixed: signed-integer-overflow
  UB in Int arithmetic and `ipow` (wraparound is now implemented in unsigned
  arithmetic; `MAX .^ MAX` previously also effectively hung), uint32
  truncation of range and constructor sizes that could reach a heap overflow
  (`1:4294967306` allocated 10 slots and wrote billions — all ranges and
  constructors now capped at 1e8 elements with clean errors), error-path
  leaks in the elementwise family, `mrdivide`/`inv`/`mpow` on singular or
  nonconformable inputs, indexed assignment (a latent setjmp-clobber: cleanup
  handlers read register-cached pointers), and parser scratch vectors on every
  parse error (now registered and freed on unwind). 18 regression goldens pin
  the fixes; the golden suite additionally runs clean under UBSan.
- **Examples in `help`.** `help(f)` now shows one or two usage examples with
  their actual output for every builtin (113 of them). Examples marked `%=` in
  the doc table are executed and compared by `tests/run_examples.sh` on every
  `make test` — the initial authoring pass had 11 wrong claimed outputs, all
  caught by the verifier before shipping.
- **Solvers: `fzero`, `fminbnd`, `integral`.** The first builtins that call
  back into the language — `call_value` re-enters the VM per evaluation, so
  the function argument can be a closure (capturing data: `fzero(npv, ...)`)
  or a builtin (`fzero(cos, 1, 2)`). Brent's zeroin and localmin; adaptive
  Simpson with Richardson error estimate. All three are allocation-free, so an
  error raised inside `f` propagates cleanly (ASan-verified). Cross-checked
  against SciPy's brentq / fminbound / quad.
- **Legend labels in plots.** `plot` and `hist` options accept `label` (single
  series) and `label1..labelN` (per series); unlabeled series keep the
  `series k` default. Validated as strings before gnuplot is launched.
- **Data file I/O.** `readcsv(file[, opts])` reads numeric CSV into a Float
  matrix (empty cells become `nan`; CRLF tolerated; `{delim, skip}` options);
  `writecsv(file, A[, opts])` writes at full precision (`%.17g` — values
  round-trip bit-exactly, Int columns stay integral). `readtable(file)` reads a
  header-bearing CSV into a **record of column vectors** with keys sanitized
  from the header (duplicates deduped) — a mini data frame using only existing
  language machinery: `d.cpi[d.year >= 2021]` just works. String columns are
  rejected by name until strings are first-class. Records can now own their
  keys (`owns_keys`), enabling dynamically-named fields.
- **`tic` / `toc()` and `unique(A)`.** Monotonic wall-clock timing, and sorted
  distinct elements (vectors keep orientation; matrices flatten to a row; NaNs
  compare unequal to themselves so all are kept, sorted last — the sort
  comparator is now a total order, which also makes NaN placement in `sort`,
  `median`, and `quantile` deterministic across platforms).
- **`cov` and `corr`.** Covariance and Pearson correlation: matrix form
  (columns = variables, rows = observations) returns the p x p matrix; two
  vectors return the scalar. `cov` shares `var`'s `w` normalization. Constant
  columns yield `nan` correlations, and the float printer now displays NaN
  without a meaningless sign bit (`nan`, never `-nan`).
- **Descriptive statistics.** `var(A[, w][, dim])` and `std(...)` (sample N-1
  default, `w = 1` for population), `median(A[, dim])`, and `quantile(x, p)`
  (linear interpolation between order statistics, NumPy-compatible; `p` scalar
  or vector). All follow the reduction family's conventions, work elementwise
  on logical masks like `sum`, and are cross-checked against NumPy.
- **Axis ranges in plots.** `plot` and `hist` options records now accept
  `xrange` / `yrange` as `[lo, hi]` vectors (`hist(y, 20, {yrange = [0, 6000]})`
  anchors a histogram's baseline at zero — gnuplot's auto-range otherwise
  magnifies sampling noise into apparent structure). `hist` accepts the full
  options record (`title`, labels, `grid`, ...). Range/option validation runs
  before gnuplot is launched, so errors are clean and leak-free.
- **Plotting via gnuplot.** `plot(y)` / `plot(x, y)` / `plot(x, Y)` (matrix
  columns as series), with a trailing gnuplot style string or an options record
  `{title, xlabel, ylabel, style, logx, logy, grid}`; `hist(y[, nbins])` draws
  histograms. Out-of-process and a soft dependency — clean error when gnuplot
  is absent. `NEUTRINO_PLOT_TERM` / `NEUTRINO_PLOT_OUT` redirect output for
  scripted rendering (PNG, ASCII `dumb`, ...). SIGPIPE is now ignored
  process-wide so a missing gnuplot (or pager) reports an error instead of
  killing the interpreter. A PNG smoke test runs in `make test` and skips
  gracefully without gnuplot.
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
