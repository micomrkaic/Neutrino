# Changelog

Notable changes to Neutrino. Newest first.

## Unreleased

### Fixed
- **v2.19.1: the index catches up, and a rite lesson is paid for.**
  v2.19.0's four new builtins left the book index stale — caught not
  here but by the owner's deploy, because the release-side suite run was
  piped through a grep for expected lines and the STALE verdict matched
  no pattern while the pipe swallowed the exit code. Index regenerated;
  the PLAYBOOK gains the law: the suite's verdict is its exit code, and
  filters may decorate a green run but never stand between a red one and
  the eyes.
- **v2.16.2: deploy.sh survives two workstations.** Deploying from the
  Mac and then the X1 produced a rejected non-fast-forward push — the
  first real two-machine divergence. Since releases are full snapshots,
  the principled resolution is git merge -s ours: record the remote
  history, keep the local tree byte-for-byte (its content supersedes all
  earlier snapshots). deploy.sh now does this automatically on push
  failure, listing the superseded commits; the PLAYBOOK records the trap,
  its resolution, and the rule it implies — nothing enters the release
  repo except through a deploy.
- **v2.16.1: the art catches up with the appendices.** Appendix E
  (Symbolic differentiation) receives its author-drawn plate — a notepad
  passing through d/dx to the answered whiteboard — and the index
  card-file, redrawn with its F shield, follows the index to Appendix F.
  The book's title page is now the og-card (the navy social card with
  the atom mark), also installed at docs/og-card.png with og:image meta
  on the workbench page.
- **v2.13.1: the limitation that wasn't.** Preparing the Cozy fork, the
  conformance goldens impeached the v2.12.1 KNOWN_LIMITATIONS entry:
  string extraction has existed all along as INDEXING — s[4], s[4:8],
  s[end-3:end], fancy-index s[[3,2,1]], golden-tested since the strings
  phase. The wrong entry came from grepping the builtin table for a
  substr function while the grammar carried extraction as syntax; a
  substr builtin would be redundant, which is why none exists. The entry
  is rewritten: the genuine gap is only strfind (position of a pattern),
  and symb.nu's parser was feasible under the freeze after all. The
  goldens outrank the maintainer's memory — that is what they are for.
- **v2.12.1: substr stays out — on purpose, on record.** The string
  family's missing extraction (no substr/strfind/char indexing) is now a
  KNOWN_LIMITATIONS entry citing symb.nu's constructor API as its
  friction transcript, together with the reasoning: unlike keep, which
  merely complemented clear, extraction enables new program shapes
  (parsers), and that is successor territory. The owner declined his own
  sympathetic case; the freeze is the artifact.
- **v2.11.1: the page break that printed itself.** v2.11.0's per-section
  \newpage lost its backslash to shell quoting on the way into the
  pandoc preprocessor, so the PDF rendered a literal 'ewpage' before
  chapter 1 and broke no pages at all. The command is now assembled with
  chr(92) — immune to every quoting layer — and the rebuilt PDF is
  machine-checked two ways: zero stray 'ewpage' strings in its extracted
  text, and a page count that jumped from 36 to the properly-broken
  figure.
- **v2.10.3: "circle" means circles everywhere.** plot(x, y,
  {style = "circle"}) drew markers in the native REPL (gnuplot accepts its
  whole marker family plus abbreviations) but a line in the browser: the
  svg/ascii backends' point detection was an exact prefix match on
  "points". The detection is now a substring match over the marker family
  (point, circle, dot), verified across both backends and pinned by a new
  svg regression; the manual documents the rule. Reported from the
  author's Mac-vs-browser comparison — the two-frontend habit is itself a
  test harness.
- **v2.10.2: the Mac reads the book more strictly.** Four book transcripts
  failed on macOS, exposing two defects. (1) The fifth-roots-of-unity
  problem displayed the raw ~1e-16 residuals of mathematical zeros —
  asserting rounding noise, whose last digits differ between platforms'
  complex math libraries; both identities are now asserted below tolerance
  instead, and the discussion says why. (2) A genuine bug Linux's
  determinism had certified: the Markov problem took eigenvector column 1
  blindly — the 0.6 eigenvalue's vector, which sums to ~0 — so the
  'stationary distribution' printed ±1e15 garbage while the prose claimed
  75/25; deterministic garbage verifies, and only macOS's different
  garbage broke the spell. The transcript now selects the eigenvalue-1
  column with find (never assume eigenvalue ordering), prints [0.75; 0.25],
  and the discussion records the lesson with the first printing named.
- **v2.10.1: macOS build fix (first report from a real clone).** eval.c
  failed on Apple Clang: 'no member named ru_maxrss in struct rusage'.
  Cause: Darwin clamps header visibility to the requested standard, so our
  _XOPEN_SOURCE 700 *hid* the BSD extension fields of struct rusage that
  the mem builtin reads — glibc doesn't guard struct members, which is why
  Linux never noticed. Fix: define _DARWIN_C_SOURCE alongside, restoring
  full visibility on macOS and inert elsewhere. Reported by the author
  from his own Mac — the maintenance contract operating as designed.
- **v2.9.2: HP-handbook blue.** The plates' near-black background read as
  severity on paper; re-inked on deep navy blue (#1a3a6a) — the classic HP
  applications-handbook cover color, which is also now the background of
  brand/banner.svg and of the newly rendered brand/logo.png (detector and
  wordmark drawn at high resolution in the terminal palette). The book's
  frontispiece is the logo image rather than an ASCII code block, sized
  for the page.
- **v2.9.1: the plates, printed right.** The vignette sources carry an
  alpha channel with a soft halo; v2.9.0's grayscale conversion dropped
  the alpha instead of compositing, shipping murky near-unreadable plates
  in the PDF. Rebuilt from the originals: composited on white, then
  re-inked as light line art on workbench navy (#0d1117) — the same
  color as the new brand/banner.svg (the detector-and-wordmark banner,
  now a proper vector asset in the repo). Plates placed 30% smaller in
  the PDF (55% text width) and 62% in the browser, where the CSS
  inversion is retired since the art is now dark-native. Art total
  2.28 MB.
- **v2.3.1: a stale comment corrected.** The ASCII plot backend's header
  claimed it runs "always in the browser" — true before the SVG backend
  existed, false since: dispatch tries SVG first and the browser default
  is SVG (verified against the bundle: plot() writes plot_1.svg, dark
  palette, nothing in the terminal). ASCII remains the native
  NEUTRINO_PLOT_TERM=ascii mode and the no-gnuplot fallback. No behavior
  changed; comments are maintenance documentation and must not lie.
- **v1.17.1: the standard library is not the workspace.** The new constants
  appeared in `who` as ordinary variables — and worse, `clear()` deleted
  them (and always had a latent sibling: clearing a shadowed builtin
  destroyed the builtin forever). The environment now records a protection
  boundary: startup registrations — builtins and constants alike — are the
  standard library. `who` shows only your bindings; shadowing a stdlib name
  appends rather than overwrites (lookup takes the newest), so
  `clear("pi")` or a bare `clear()` removes the shadow and the original
  resurfaces; clearing an unshadowed stdlib name is an error, not a
  deletion; `save` writes only your workspace. Six new goldens including
  both resurrection paths.
- **v1.15.1: `ans` after a where clause.** A `where`-qualified expression
  echoed its value but did not set `ans` (first user-caught semantic bug —
  found at the calculator, naturally). Cause: where clauses desugar to
  `let..in`, and the v1.9.0 `ans` guard excluded the AST_LET kind wholesale,
  conflating let *statements* (named — correctly excluded) with let..in
  *expressions* (anonymous — must set `ans`, and now do). The same fix
  covers explicit `let x = e in body`, latent since v1.9.0. The manual's
  verified ans transcript now exercises the interaction.
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
- **v2.19.0: the owner gives in — five sanctioned conveniences, all
  additive, destined for Cozy.** (1) elseif: chains share one end,
  desugaring in the parser to nested ifs; no legal program changes.
  (2) eval("code"): a string runs in the current session and returns its
  last value — delivering, as a corollary, dynamic record access
  (eval("w." + col)), the v2.13.0 reflection wall's first door.
  (3) names()/names("vars")/names("funcs"): the programmatic who — a
  sorted string column — while the who family's printed tables stay
  exactly as they were (the owner asked whether who should return names;
  the answer shipped is no: display commands stay commands, reflection
  gets its own function). (4) input("prompt") reads the keyboard
  (window.prompt in the browser); (5) pause() waits for Enter (alert in
  the browser); both consume the next stdin line under a pipe, which
  tests/run_io.sh checks exactly. README's Status now states the honest
  contract: the 2.x surface is append-only — owner-sanctioned, strictly
  additive, individually listed. 905 goldens; 159 builtins; manual
  sections for all five.
- **v2.18.0: the tour gains its crown jewels.** A new act, Functions as
  values — anonymous application, Euclid's algorithm as bare recursion
  (gcd_(1071, 462) = 21), and iterate composing any function with itself
  n times, with forty-fold sqrt(1+t) converging on the golden ratio —
  and the pipelines act now opens with both jewels: the oscillation pipe
  (1:12 ~> (@ ^ 2) |> (fn v -> v[v > 50]) ~> sqrt |> mean, landing on
  exactly 10 as values flavor-change between element-wise and whole-value
  flow) and the fan-out dashboard (a seeded rand(1, 500) piped into
  {mu, sd, hi, lo} in one line). Six acts; run_demo.sh asserts the
  golden ratio and Euclid alongside the birthday.
- **v2.17.0: the tour, and a lit legend.** packages/demo.nu is the ninth
  package and the first performance: five acts of greatest hits — Basel
  in Euler's notation, the Gaussian integral's pi, the quadratic in
  blackboard word order, the seeded CLT mask, the birthday problem in one
  sentence, the fan-out thesis line, symb.nu's deriv("sin(x)/x") with
  fzero finding the tan x = x critical point, the HP-12C mortgage, and a
  plotted finale — every number computed live, deterministic, with make
  test asserting the tour plays clean (tests/run_demo.sh). Along the way
  print's template semantics met record braces (praw doubles them). And
  the browser's plot legend is finally readable: the svg legend text
  carried no fill attribute, defaulting to black on the dark theme —
  fixed to the axis color, with the svg suite now asserting every legend
  text carries a fill.
- **v2.16.0: symbols become functions again.** symb.nu gains the lift
  back to function-space: tofun(e) closes an expression tree into an
  ordinary fn x, with string conveniences ffun(src) and dfun(src) —
  dfun("sin(x)/x") is a callable derivative. The payoff is
  reunification, and book Problem E.4 stages it twice over: fzero on the
  symbolic derivative and fminbnd on the original function agree on
  4.49341 (the tan(x) = x critical point of sin(x)/x), and the
  Fundamental Theorem of Calculus verifies numerically —
  integral(dfun("x^3"), 1, 2) = 7 = the function's change. Two new table
  specs. 316 verified book transcripts; 98 in PACKAGES.
- **v2.15.0: the derivative reads like the textbook.** symb.nu's printer
  learns division and subtraction — mul(u, powc(v, -n)) prints u / v^n on
  either side, add(a, mul(-1, b)) prints a - b, and simp hoists buried
  negative constants so the patterns form — with the payoff
  deriv("sin(x)/x") = "((cos(x) / x) - (sin(x) / x^2))", the quotient
  rule as the blackboard writes it. En route the battery caught a real
  parser bug: unary minus bound tighter than ^, making -x^2 parse as
  (-x)^2; fixed to mathematical convention (minus parses a power), with
  evalx(parse("-x^2"), 3) = -9 and exp(-x^2) differentiating to
  -2x exp(-x^2), numerically cross-checked. 311 verified book
  transcripts; 98 in PACKAGES.
- **v2.14.0: the parser that was possible all along.** symb.nu completes
  its circle: a recursive-descent parser in pure Neutrino — legal all
  along, as v2.13.1's correction established — so the differentiator
  takes mathematics as typed: deriv("x^3 + 5*sin(x)") returns
  "((3 * x^2) + (5 * cos(x)))". Character classes are chained string
  comparisons ("0" <= c <= "9"), the grammar threads position through
  records, errors surface with positions via a num()-based fail, and
  general powers desugar as f^g = exp(g log f) — so x^x differentiates
  to x^x(ln x + 1) with no special case. Constructors remain (additive
  change); PACKAGES §8 extended, book Problem E.3 tells the story with
  the lesson attached, two new table specs. 309 verified book
  transcripts; 97 in PACKAGES.
- **v2.13.0: conversions mapped — one lesson each way.** Book Problem 3.5
  shows string-to-array needs no builtin: strsplit ~> trim ~> num is the
  idiom (and the reverse maps str through strjoin). The other direction is
  a wall worth recording: fields(r) can see a record's names but nothing
  can use a name dynamically, so generic k=v parsers, serializers, and
  record utilities are impossible in userland — KNOWN_LIMITATIONS gains
  the entry, and the Cozy seed gains design entry 5, the record
  reflection trio (getfield/setfield/construction), same family as
  ast(f). Milking maintenance for successor experience, per the owner's
  standing order.
- **v2.12.0: symb.nu — symbolic differentiation, pure showing off.** The
  eighth package: expression trees as nested records, constructors instead
  of a parser (the string builtins have no substring access — a recorded
  limit, worked with rather than around), and ddx by structural recursion.
  sub and divx desugar into add/mul/negative powers at construction, so
  product, power, and chain rules alone carry the calculus — the quotient
  rule falls out of d(b^-1) for free. simp folds constants and
  reassociates them leftward (the third derivative of x^5 prints 60x^2);
  subst composes; taylor extracts series by repeated ddx — sine's
  0, 1, 0, -1/6, 0, 1/120 recovered from record recursion. PACKAGES §8 and
  new book Appendix E cross-check the symbolic derivative against Chapter
  10's numeric operator to 1e-8 in one verified session; the index of
  builtins becomes Appendix F. Two spike lessons banked: the frozen
  grammar has no elseif (nested if/end chains are the dispatch tax, a
  motivated successor note), and & does not short-circuit (guards must
  nest). 299 verified book transcripts; 92 in PACKAGES.
- **v2.11.0: the book fully illustrated, and self-composition joins the
  idiom.** Problem 12.5 — selfcomp (fn f -> fn x -> f(f(x))) applied,
  bound, and mapped; the n-fold iterate built with if (pick evaluates
  both arms and would recurse forever); forty-fold composition of sqrt(1+t)
  converging to the golden ratio; and selfcomp(d) delivering the second
  derivative with the step-size caveat (nesting squares h). Nine new
  author-drawn plates complete the art program: chapters 11-14 (linear
  algebra, probability, plotting, the idiom) and appendices A-E (finance,
  astronomy, physics, random matrices, the index), re-inked on HP-handbook
  blue like the first ten. The PDF now starts every chapter and appendix
  on a fresh page. 288 verified transcripts.
- **v2.10.0: keep — the complement of clear, and the one sanctioned
  post-freeze builtin.** keep("a", "b") removes every user variable except
  the named ones. The pure-package route was probed first and is honestly
  impossible: whov/whos print but return nothing, so no .nu code can
  enumerate names — the composition of who's iteration and clear's removal
  exists only at the C level, where keep is twenty lines reusing both.
  Same contract as clear throughout: multiple names, strict about unknown
  ones, the standard library untouched (keep("pi") is an error), ans
  dropped unless kept, and dropping a shadow resurrects the original
  builtin. Recorded in the README as the explicit owner-sanctioned
  exception to the freeze; 8 goldens, manual, reference, Emacs mode, and
  the book's index all regenerated (155 names).
- **v2.9.0: the book gets its plates.** Ten hand-drawn vignettes in the
  old-HP-handbook style, one heading each of chapters 1-10 (the dinner
  bill, the type zoo, ribbon scissors, the Argand plane over an RLC
  circuit, block matrices, punch cards, the f(x) machine, the pipe works,
  the parts-bin card file, and tangent/area/root/deflection for calculus),
  drawn by the author and installed at docs/vignettes/ (optimized to
  ~2.8 MB total). The ASCII detector banner opens the book as its
  frontispiece. The browser's markdown renderer learned image syntax, and
  the Docs tab shows the plates CSS-inverted — white ink on the dark
  workbench, chalkboard style — while the PDF keeps the originals via
  pandoc's resource path. The verified transcripts are untouched: art and
  proof, side by side.
- **v2.8.0: calculus one-liners — the Fourier machine.** Three problems
  join the Calculus chapter: 10.5 defines Fourier coefficients for any
  function in two lines (the square wave's spectrum 4/pi(1, 0, 1/3, ...)
  recovered with even terms at quadrature-zero, then resynthesized from
  forty terms of its own spectrum by sigma, Gibbs wiggle honestly
  reported); 10.6 builds the derivative as an operator — d(f) returns a
  function — and composes it inside an integrand for arc length of
  arbitrary f; 10.7 is a gallery of famous integrals: Gamma(5) = 24, the
  Gaussian integral squared giving pi (the substitution that dodges the
  singularity gam(0.5) honestly fails on), the lazily-converging Wallis
  product (parenthesized past the loose-body where trap Chapter 14
  documents), and the quarter circle. 267 verified transcripts.
- **v2.7.0: the book finds its final shape.** Reordered for logical flow —
  types (2) and strings (3) now follow basic calculations, before complex
  numbers (4) — and two chapters join: 5 (Matrices — the construction kit
  with block notation, indexing and in-place assignment, mask selection,
  the matrix-vs-elementwise distinction that bit this book's own Monte
  Carlo draft, and display/shape mechanics) and 6 (Reading and writing
  data — the CSV round trip relocated from statistics, readtable on a
  shipped sample table in tests/data/weather.csv as a record of named
  columns, and save/clear/load proving workspace persistence). Fourteen
  chapters, five appendices, 50 numbered problems plus nine in the
  appendices, 271 verified transcripts.
- **v2.6.0: the book's reference half — types, strings, and the idiom
  chapter.** Three chapters join Neutrino by Example: 10 (Values and
  types — the type zoo via who, numeric promotion, Bool's deliberate
  refusal of arithmetic with pick as the bridge, floating-point honesty
  with eps/inf/nan), 11 (Strings — the twelve string builtins worked:
  cleanup, fmt reporting, CSV-line parsing with strsplit/num, predicates
  riding the pipes over ls), and 12 (The Neutrino idiom — the important
  one: lambdas as values, where as the blackboard's word order with the
  quadratic solved textbook-style, sigma including a dot product defined
  in notation, and the grand combinations: A |> {a = det, b = inv} where
  A = eig(rand(2)).vectors with det(A)*det(inv(A)) = 1 confirmed, the
  statistician's one-liner, and the birthday problem answered 23 in a
  single pipeline). 213 verified transcripts; 52 worked problems total.
- **v2.5.0: the book learns to draw, and the papers catch up.** Chapter 9
  (Plotting) joins Neutrino by Example with live figures: the transcript
  verifier now pins NEUTRINO_PLOT_TERM=ascii, and the ascii backend's
  fixed canvas makes seeded plots exact text — so the book verifies its
  own sine curve, randn histogram, and scatter.nu scatter, while browser
  users get the same commands as SVG. The manual's plotting section, which
  still claimed gnuplot was the only backend, now states the three-backend
  truth (native gnuplot default, TERM=ascii/svg overrides, browser SVG
  default) and points to scatter.nu; the README gains a Packages section
  naming all seven; KNOWN_LIMITATIONS records the per-point size/color
  boundary that separates package territory from the successor's.
- **v2.4.0: scatter.nu — the first post-freeze package.** Scatter plots
  without touching the core, proving the maintenance-mode contract: the
  frozen plot() already honors style = "points" in every backend (SVG
  circles in the browser, point markers in ascii and gnuplot), so
  scatter(x, y), scatter_titled(x, y, t), and a jitter(x, amount) helper
  are pure .nu. Per-point sizes and colors (bubble charts) would need
  backend changes and are deliberately declined — the package header says
  so. The SVG test suite asserts the circles and title; en route, the
  ascii backend's comment was corrected again (a missing gnuplot is an
  error, not a silent fallback — the code never promised what the comment
  claimed).
- **v2.3.0: the book gains its function index.** Appendix E of Neutrino by
  Example: every builtin and constant, alphabetical with signature,
  description, and area — machine-generated from the interpreter's own
  documentation table by tools/gen_book_index.py, whose --check runs in
  make test, so the index cannot drift from the language. The HP handbooks
  always ended with one; now so does ours.
- **v2.2.0: Neutrino by Example, the full applications handbook.** The
  booklet grew into the book it was always meant to be, in the tradition of
  the HP calculator applications handbooks: 36 worked word problems across
  8 chapters (basic calculations, complex numbers, user functions,
  anonymous functions and pipes, records, calculus, linear algebra,
  probability/statistics/data) and 4 package appendices (finance,
  astronomy, physics, random matrices), with circuit, beam, mixing-tank,
  and dartboard vignettes. Highlights: RLC impedance as one complex number;
  z^n via prod[k=1:n] z where complex power is absent; a progressive tax
  schedule as one function; Kepler's equation by fzero; Leontief
  input-output; a Markov chain's climate by eigenvector; the binomial
  built from gamma; CSV round-trips; Black-Scholes against its own Monte
  Carlo; Wigner's semicircle witnessed. 161 transcript examples, every one
  executed at authoring and re-executed by make test forever.
- **v2.1.0: Neutrino by Example — and the bug the book found.** A book of
  worked sessions joins the documentation: chapter 1 (the daily calculator —
  a mortgage end to end, early-payoff, date arithmetic), chapter 2 (arrays
  and pipelines — fan-out summaries, band counting, returns compounding by
  sigma, Basel and e as one-liners), chapter 3 (Monte Carlo — pi by darts,
  the CLT watched, Black-Scholes priced analytically and by 200,000-path
  simulation, agreeing within two cents), with chapters 4-9 planned. Every
  transcript is executed at capture and re-executed by make test; BOOK.md
  is covered by doclint and ships in the tarball with BOOK.pdf. Writing it
  exposed the project's gravest bug: the transcript capture harness and the
  verifier shared an echoing sentinel that poisoned `ans`, so ans-dependent
  manual transcripts were captured corrupted and certified green for three
  releases — correlated verification. The sentinel is now inert
  (print-based) in both; the manual's ans transcript is recaptured true;
  LESSONS.md §8 and the PLAYBOOK trap almanac record the principle:
  verifiers must not share machinery with what they check.
- **v2.0.1: the transfer document.** PLAYBOOK.md distills the project's
  engineering constitution for the successor language: the load-bearing
  principles (each cited to the incident that proved it), the architecture
  worth lifting whole, the verification-lattice inventory, the release
  rite, a trap almanac of every class of bug paid for once, a
  lift-vs-re-derive manifest, and an honest list of debts to choose
  differently next time. The README now records maintenance mode: Neutrino
  is feature-complete and frozen at 2.x; bug fixes only, where a bug is a
  divergence between behavior and the manual; packages remain open. The
  methodology was the product; the language is its first application.
- **v2.0.0: index-bound reductions — and the design docket closes.** Sigma
  notation, executable: `sum[k = 1:1000] 1 / k ^ 2` converges on `pi^2/6`
  at the prompt. `f[k = R] E` desugars to `R ~> (fn k -> E) |> f` — any
  callable reduces, the binder is scoped to the body, and the body binds
  loose like a fn body (parenthesize the reduction to operate on its
  result). Disambiguated from indexing by the peek `[ IDENT =`, a shape
  that was never legal, so no existing program changes meaning; pure parser
  desugar into the pipe machinery, no new AST, no VM changes; 16 goldens
  and a 500-program fuzz under ASan. With this, every design in
  DESIGN_NOTES has shipped or been formally rejected — the language its
  notes described now exists, and the version number says so.
- **v1.17.0: constants, and a consistently dark workbench.** Core
  mathematical constants join the language: `pi`, `e`, `eulergamma`, `phi`,
  `eps` (machine epsilon), `inf`, `nan` — ordinary shadowable values with a
  new Constants section in the reference. A sixth package, `phys.nu`,
  carries the CODATA 2018 physical constants as a record — exact where the
  2019 SI redefinition makes them exact — with goldens that recover the
  speed of light from Maxwell's relation 1/sqrt(eps0*mu0) and check
  hbar·2pi = h. The browser workbench is now uniformly dark: the Plots and
  Docs panes match the terminal and editor, and the SVG plot backend
  emits a dark-friendly palette (light axes and labels, brighter series
  colors).
- **v1.16.0: pwd, cd, ls.** The working directory as ordinary builtins —
  not Octave-style command syntax (that duality is a wart this language
  exists to avoid) but plain functions with bare autocall, so shell muscle
  memory works and the results are values: `ls` returns a string array
  (`ls("packages") ~> length`), globs supported, and `cd("dir")` actually
  persists — which `!cd` silently cannot, since the shell escape runs in a
  child process. En route, `ls` surfaced a latent use-after-free: `map` over
  string arrays over-released borrowed elements (arr_get returns strings
  borrowed; numeric immediates made the release a no-op for years) — fixed,
  with regression goldens covering `map`/`~>` over `fields()` and `ls()`.
- **v1.15.0: where clauses — definitions after use.** `expr where a = 1,
  b = -3` names an expression's constants after the fact, the way
  mathematics writes them. Bindings are sequential (later sees earlier, not
  vice versa — this is a let-chain, not Haskell's binding group), scoped to
  the single expression (never leak, shadow safely), and bind looser than
  pipes and chains, so a whole pipeline can be qualified at its end:
  `1:n ~> (@ ^ 2) |> sum where n = 5`. Pure desugar to let..in; no new AST,
  no VM changes. `where` is now a reserved word: the old builtin split along
  its natural seam into `find(mask)` (which already existed — the 1-arg form
  was always an alias) and `pick(mask, a, b)`; twelve call sites migrated.
  17 goldens, a 500-program fuzz under ASan, zero regressions.
- **v1.14.0: chained comparisons.** `a < b < c` now means what mathematics
  means: `a < b` and `b < c`, with the middle term evaluated exactly once
  (verified by rand-stream position). Chains run in one direction —
  `{<, <=}`, `{>, >=}`, or all `==`; mixing directions is a parse error with
  a teaching message, and `!=` never chains (`a != b != c` would not mean
  all-distinct). The conjunction is elementwise `&`, so chains over arrays
  are masks: `sum(0 < z < 1)` counts a band. Implemented as a pure parser
  desugar into a scoped block expression with reserved temps — no new AST
  node, no VM changes; previously `a < b < c` was a runtime type error, so
  the feature is purely additive. 17 goldens, a 900-program fuzz over
  comparison/operator mixtures under ASan, zero regressions on the existing
  811.
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
