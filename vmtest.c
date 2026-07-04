/* vmtest.c — a minimal non-readline driver that runs each input line through
 * the bytecode VM (vm_eval_program). Lets us pipe test programs and run under
 * ASan/UBSan without touching the shipping readline REPL. Each line is parsed
 * and run as an independent program, so a runtime error in one line does not
 * abort the rest (matching the REPL). Accepted (arena, source) pairs are kept
 * and freed at exit so a clean run reports zero leaks. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"
#include "parser.h"
#include "eval.h"
#include "vm.h"

typedef struct { Arena **arenas; char **srcs; size_t count, cap; } Keep;

static void keep_push(Keep *k, Arena *a, char *src)
{
    if (k->count == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->arenas = realloc(k->arenas, k->cap * sizeof *k->arenas);
        k->srcs   = realloc(k->srcs,   k->cap * sizeof *k->srcs);
        if (!k->arenas || !k->srcs) abort();
    }
    k->arenas[k->count] = a;
    k->srcs[k->count]   = src;
    k->count++;
}

static void keep_free_all(Keep *k)
{
    for (size_t i = 0; i < k->count; i++) { arena_free(k->arenas[i]); free(k->srcs[i]); }
    free(k->arenas); free(k->srcs);
}

static bool is_blank(const char *s)
{
    for (; *s; s++) if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return false;
    return true;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);   /* broken pipes (gnuplot/pager absent) report via pclose, not death */
    Interp I;
    interp_init(&I);
    EnvObj *globals = globals_new();
    Keep keep = {0};

    char buf[8192];
    while (fgets(buf, sizeof buf, stdin)) {
        if (is_blank(buf)) continue;
        char *src = strdup(buf);
        Arena *a = arena_new();
        Parser p;
        parser_init(&p, src, a);
        AstNode *prog = parser_parse(&p);
        if (p.had_error) {
            fprintf(stderr, "  parse error at %u:%u: %s\n", p.err_tok.line, p.err_tok.col, p.err_msg);
            arena_free(a); free(src);
            continue;
        }
        keep_push(&keep, a, src);
        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error)
            fprintf(stderr, "  error at %u:%u: %s\n", I.cur_line, I.cur_col, I.err);
        value_release(r);
    }

    env_clear(globals);
    env_release(globals);
    vm_session_end();            /* free closure-bearing chunks (closures now gone) */
    keep_free_all(&keep);
    return 0;
}
