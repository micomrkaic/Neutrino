/* parser.h — recursive-descent + Pratt parser for Neutrino. */
#ifndef NEUTRINO_PARSER_H
#define NEUTRINO_PARSER_H

#include <setjmp.h>
#include "lexer.h"
#include "arena.h"
#include "ast.h"

typedef struct {
    Lexer   lex;
    Token   cur;          /* one-token lookahead */
    Token   prev;         /* last consumed       */
    Arena  *arena;
    bool    had_error;
    Token   err_tok;      /* where the error was reported */
    const char *err_msg;
    char    msgbuf[160];  /* backing store for formatted messages */
    jmp_buf jmp;          /* panic-unwind target */
    void   **scratch;     /* live parser scratch buffers, freed on parse error */
    uint32_t scr_len, scr_cap;
    int     in_index;     /* >0 while parsing index args (enables 'end') */
} Parser;

void     parser_init(Parser *p, const char *src, Arena *arena);
/* Returns the program (AST_BLOCK), or nullptr if had_error is set. */
AstNode *parser_parse(Parser *p);

#endif
