/* main.c — Neutrino front-end driver.
 *   neutrino [--tokens | --ast | --dis] [file.nu]
 * Default mode evaluates. With no file, runs a built-in sample. */
#include "lexer.h"
#include "parser.h"
#include "arena.h"
#include "ast.h"
#include "value.h"
#include "eval.h"
#include "vm.h"
#include "chunk.h"
#include "compile.h"
#include "repl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char SAMPLE[] =
    "% Neutrino evaluator smoke test\n"
    "let A = [1, 2, 3; 4, 5, 6]\n"
    "let s = sum(A)\n"                 /* int 21 */
    "let d = A[2, 3]\n"                /* exact index -> 6 */
    "let r = 1:2:9\n"                  /* int row vector */
    "let sq = fn x -> x .^ 2\n"
    "let q = map(sq, r)\n"             /* [1, 9, 25, 49, 81] */
    "let z = sqrt(-4)\n"               /* tower: 2i */
    "let rec = {name = \"neutrino\", dims = size(A)}\n"
    "let label = if s > 10 then \"big\" else \"small\" end\n"
    "print(A, s, d, q, z, rec.name, rec.dims, label)\n"
    "let total = s + sum(q)\n";        /* trailing let -> program value */

static char *read_all(FILE *f)
{
    if (fseek(f, 0, SEEK_END) != 0) return nullptr;
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) return nullptr;
    char *buf = malloc((size_t)n + 1);
    if (!buf) return nullptr;
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    return buf;
}

static void dump_tokens(const char *src)
{
    Lexer lx;
    lexer_init(&lx, src);
    for (;;) {
        Token t = lexer_next(&lx);
        const char *kn = token_kind_name(t.kind);
        if (t.kind == TOK_NEWLINE)      printf("%4u:%-3u %-13s \\n\n", t.line, t.col, kn);
        else if (t.kind == TOK_EOF)     printf("%4u:%-3u %-13s\n",     t.line, t.col, kn);
        else printf("%4u:%-3u %-13s '%.*s'\n", t.line, t.col, kn, (int)t.len, t.start);
        if (t.kind == TOK_EOF) break;
    }
}

static void print_diagnostic(const char *src, uint32_t line, uint32_t col, const char *msg)
{
    fprintf(stderr, "error at %u:%u: %s\n", line, col, msg);
    const char *p = src;
    for (uint32_t ln = 1; ln < line && *p; p++)
        if (*p == '\n') ln++;
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    fprintf(stderr, "  %.*s\n", (int)(eol - p), p);
    fprintf(stderr, "  %*s^\n", col ? (int)col - 1 : 0, "");
}

int main(int argc, char **argv)
{
    enum { M_EVAL, M_AST, M_TOKENS, M_DIS } mode = M_EVAL;
    const char *path = nullptr;
    bool want_sample = false;
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--tokens") == 0) mode = M_TOKENS;
        else if (strcmp(argv[i], "--ast")    == 0) mode = M_AST;
        else if (strcmp(argv[i], "--dis")    == 0) mode = M_DIS;
        else if (strcmp(argv[i], "--sample") == 0) want_sample = true;
        else path = argv[i];
    }

    /* no file, plain eval mode -> interactive REPL */
    if (!path && !want_sample && mode == M_EVAL)
        return repl_run();

    char *owned = nullptr;
    const char *src = SAMPLE;
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        owned = read_all(f);
        fclose(f);
        if (!owned) { fprintf(stderr, "read failed\n"); return 1; }
        src = owned;
    }

    if (mode == M_TOKENS) { dump_tokens(src); free(owned); return 0; }

    Arena *arena = arena_new();
    Parser p;
    parser_init(&p, src, arena);
    AstNode *prog = parser_parse(&p);
    int rc = 0;

    if (!prog || p.had_error) {
        print_diagnostic(src, p.err_tok.line, p.err_tok.col, p.err_msg ? p.err_msg : "parse error");
        rc = 1;
    } else if (mode == M_AST) {
        ast_print(stdout, prog);
    } else if (mode == M_DIS) {
        Interp I;
        interp_init(&I);
        AstNode  *single = prog;
        AstNode **items  = &single;
        uint32_t  count  = 1;
        if (prog->kind == AST_BLOCK) { items = prog->as.list.items; count = prog->as.list.count; }
        for (uint32_t i = 0; i < count; i++) {
            Chunk ch;
            if (!vm_compile(&I, items[i], &ch)) {
                print_diagnostic(src, I.cur_line, I.cur_col, I.err);
                rc = 1;
                break;
            }
            char title[32];
            snprintf(title, sizeof title, "stmt %u", i + 1);
            if (i) fputc('\n', stdout);
            chunk_disassemble(stdout, &ch, title);
            chunk_free(&ch);
        }
    } else {
        Interp I;
        interp_init(&I);
        EnvObj *globals = globals_new();
        Value result = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error) {
            print_diagnostic(src, I.cur_line, I.cur_col, I.err);
            rc = 1;
        }
        value_release(result);
        env_clear(globals);     /* break global-scope closure/env cycles */
        env_release(globals);
        vm_session_end();       /* free retained chunks before the arena they reference */
    }

    arena_free(arena);
    free(owned);
    return rc;
}
