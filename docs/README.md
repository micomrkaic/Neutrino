# docs/ — the browser playground (GitHub Pages)

This folder is served by GitHub Pages at
`https://micomrkaic.github.io/Neutrino/`.

- `index.html` — the terminal page (custom minimal REPL, brand palette).
- `neutrino.js` — the whole interpreter as a single self-contained file: the
  WebAssembly binary is embedded as base64 (`-sSINGLE_FILE=1`), so there is no
  separate `.wasm` to fetch and no MIME/CORS/path concerns.
- `.nojekyll` — tells Pages to serve the files as-is (skip Jekyll).

## Enabling Pages (one time)
Repo → Settings → Pages → Build and deployment →
Source: "Deploy from a branch" → Branch: `main`, folder: `/docs` → Save.
Live in a minute or two. Then paste the URL into the repo's About field and
add a "Try it in your browser" link to the top-level README.

## Rebuilding neutrino.js after interpreter changes
With a current emsdk (`emcc` on PATH), from the repo root:

```
emcc -std=c2x -O2 -sMODULARIZE=1 -sEXPORT_NAME=Neutrino \
  -sALLOW_MEMORY_GROWTH=1 -sSUPPORT_LONGJMP=1 \
  -sENVIRONMENT=web -sSINGLE_FILE=1 \
  -sEXPORTED_FUNCTIONS=_nu_init,_nu_eval,_nu_version,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,UTF8ToString,stringToUTF8,lengthBytesUTF8,FS \
  lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c wasm_api.c \
  -o docs/neutrino.js
```

(Older Ubuntu emscripten 3.1.6 also needs
`-Dnullptr=NULL '-Dalignof(x)=_Alignof(x)' -Dstatic_assert=_Static_assert`
for its partial-C23 clang-15.)

## Local preview
`neutrino.js` is single-file, so a plain static server works — but browsers
allow WASM from `file://` inconsistently, so serve over http:

```
cd docs && python3 -m http.server 8000    # then open http://localhost:8000
```

## Known browser limitations
- **Plotting** (`plot`, `hist`) renders as **ASCII** in the browser (there is
  no gnuplot subprocess). The native build uses gnuplot by default and can
  also produce ASCII with `NEUTRINO_PLOT_TERM=ascii`.
- **File I/O** (`readcsv`/`readtable`/`writecsv`) works against the in-memory
  filesystem (MEMFS). Wiring drag-and-drop of a real CSV into MEMFS is a
  small follow-up.
- Entries are single-line; use `;` to sequence (for-loops work on one line).
