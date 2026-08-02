#define _POSIX_C_SOURCE 200809L
/* wasm_api.c — Neutrino in the browser: a persistent session exposed as
 * string-in / string-out calls. Mirrors vmtest.c's loop, but output (echo,
 * print, help, errors) is captured into a buffer returned to JavaScript. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include "arena.h"
#include "parser.h"
#include "value.h"
#include "eval.h"
#include "vm.h"
#include "version.h"

/* vmtest.c's Keep, duplicated: accepted (arena, source) pairs stay alive for
 * the session because identifiers point into their source text. */
typedef struct { Arena **arenas; char **srcs; size_t len, cap; } Keep;
static void keep_push(Keep *k, Arena *a, char *s)
{
    if (k->len == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->arenas = realloc(k->arenas, k->cap * sizeof *k->arenas);
        k->srcs   = realloc(k->srcs,   k->cap * sizeof *k->srcs);
        if (!k->arenas || !k->srcs) abort();
    }
    k->arenas[k->len] = a; k->srcs[k->len] = s; k->len++;
}

static Interp  I;
static EnvObj *globals;
static Keep    keep;
static char   *g_buf;      /* last result, owned here, freed on next call */
static size_t  g_cap_len;  /* memstream size, updated on fflush */
static FILE   *g_cap;      /* live capture stream during nu_eval (pause streams from it) */
static size_t  g_streamed; /* bytes already streamed to the page mid-eval */

/* Real pause in the browser (called from bi_pause under __EMSCRIPTEN__).
 * Output accumulates in the memstream and normally reaches the page only
 * when nu_eval returns — so a blocking pause must first flush and stream
 * the pending text (or the user waits at a blank screen, the v2.25.2
 * lesson), then yield to the event loop via Asyncify until the page's
 * Enter latch fires. Stale pages without the latch skip the wait. */
void nu_wasm_pause(const char *msg, uint32_t mlen)
{
    if (!g_cap) return;
    fwrite(msg, 1, mlen, g_cap); fputc('\n', g_cap);
    fflush(g_cap);             /* POSIX: buffer and size now current */
    EM_ASM({
        var t = UTF8ToString($0);
        if (window.__nuStream) { window.__nuStream(t);
                                 window.__nuPauseWaiting = 1; window.__nuPauseDone = 0; }
        else                   { window.__nuPauseDone = 1; }
    }, g_buf + g_streamed);
    g_streamed = g_cap_len;
    while (!EM_ASM_INT({ return (window.__nuPauseDone | 0); }))
        emscripten_sleep(60);
    EM_ASM({ window.__nuPauseWaiting = 0; });
}

EMSCRIPTEN_KEEPALIVE
const char *nu_version(void) { return NEUTRINO_VERSION " (wasm, built " NEUTRINO_BUILT ")"; }

EMSCRIPTEN_KEEPALIVE
void nu_init(void)
{
    interp_init(&I);
    globals = globals_new();
    value_set_multiline(true);    /* aligned multi-line matrices, as the native REPL defaults */
}

/* REPL-level commands (handled in repl.c natively, which the wasm build
 * bypasses). Skip leading blanks; return the argument tail, or nullptr if the
 * line is not this command. Mirrors repl.c's match_command. */
static const char *wasm_command(const char *line, const char *word)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t wl = strlen(word);
    if (strncmp(line, word, wl) != 0) return NULL;
    const char *after = line + wl;
    if (*after != '\0' && *after != ' ' && *after != '\t') return NULL;
    while (*after == ' ' || *after == '\t') after++;
    return after;
}

EMSCRIPTEN_KEEPALIVE
const char *nu_eval(const char *line)
{
    free(g_buf); g_buf = NULL;
    g_cap_len = 0; g_streamed = 0;
    FILE *cap = open_memstream(&g_buf, &g_cap_len);
    g_cap = cap;
    if (!cap) return "internal: out of memory\n";
    value_set_out(cap);

    const char *arg;
    if ((arg = wasm_command(line ? line : "", "pretty"))) {   /* REPL command, not a builtin */
        if (!strcmp(arg, "on") || !strcmp(arg, "1"))       value_set_multiline(true);
        else if (!strcmp(arg, "off") || !strcmp(arg, "0")) value_set_multiline(false);
        else if (*arg == '\0') fprintf(cap, "pretty is %s\n", value_multiline() ? "on" : "off");
        else fprintf(cap, "usage: pretty on | off\n");
        fclose(cap); g_cap = NULL;
        value_set_out(NULL);
        return g_buf ? g_buf : "";
    }

    char *src = strdup(line ? line : "");
    Arena *a = arena_new();
    Parser p;
    parser_init(&p, src, a);
    AstNode *prog = parser_parse(&p);
    if (p.had_error) {
        fprintf(cap, "  parse error at %u:%u: %s\n", p.err_tok.line, p.err_tok.col, p.err_msg);
        arena_free(a); free(src);
    } else {
        keep_push(&keep, a, src);
        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error)
            fprintf(cap, "  error at %u:%u: %s\n", I.cur_line, I.cur_col, I.err);
        value_release(r);
    }
    fclose(cap); g_cap = NULL; /* finalizes g_buf */
    value_set_out(NULL);
    return (g_buf ? g_buf + g_streamed : "");   /* page gets the unstreamed tail */
}
