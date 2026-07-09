/* lexer.h — Neutrino lexer, C23.
 *
 * Zero-copy tokenizer: every Token points back into the original source
 * buffer (no allocation, no string copying). The buffer must outlive the
 * tokens and must be NUL-terminated.
 */
#ifndef NEUTRINO_LEXER_H
#define NEUTRINO_LEXER_H

#include <stddef.h>
#include <stdint.h>

/* Fixed underlying type is a C23 feature — keeps the enum exactly one byte,
 * which matters because TokenKind sits in the hot Token struct. */
enum TokenKind : uint8_t {
    /* control / sentinels */
    TOK_EOF = 0,
    TOK_ERROR,
    TOK_NEWLINE,      /* statement / matrix-row separator (runs collapsed) */

    /* literals */
    TOK_INT,
    TOK_FLOAT,
    TOK_IMAG,         /* imaginary literal:  3i  2.5j */
    TOK_STRING,       /* "..." (escaped) or '...' (raw); slice keeps the quotes */
    TOK_IDENT,

    /* keywords — see KEYWORDS[] in lexer.c; that table is the single source. */
    TOK_KW_LET, TOK_KW_FN, TOK_KW_IF, TOK_KW_THEN, TOK_KW_ELSE,
    TOK_KW_FOR, TOK_KW_WHILE, TOK_KW_DO, TOK_KW_END, TOK_KW_RETURN,
    TOK_KW_TRUE, TOK_KW_FALSE, TOK_KW_NULL, TOK_KW_IN,
    TOK_KW_BREAK, TOK_KW_CONTINUE,

    /* grouping */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACK, TOK_RBRACK,
    TOK_LBRACE, TOK_RBRACE,

    /* arithmetic */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_CARET, TOK_BACKSLASH,

    /* elementwise (Octave dot-ops) */
    TOK_DOT_STAR, TOK_DOT_SLASH, TOK_DOT_CARET, TOK_DOT_BACKSLASH,

    /* comparison / logical / assignment */
    TOK_ASSIGN, TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_AND, TOK_OR, TOK_AMP, TOK_PIPE, TOK_BANG, TOK_TILDE,

    /* punctuation / misc */
    TOK_COLON, TOK_COMMA, TOK_SEMI, TOK_DOT, TOK_AT,
    TOK_ARROW,        /* ->  */
    TOK_FATARROW,     /* =>  */
    TOK_PIPE_GT,      /* |>  pipe-forward */
    TOK_TRANSPOSE,    /* .'  non-conjugate transpose */
    TOK_CTRANSPOSE,   /* '   conjugate transpose */
};

typedef struct {
    enum TokenKind kind;
    const char    *start;   /* into source buffer (no copy) */
    uint32_t       len;     /* lexeme length in bytes        */
    uint32_t       line;    /* 1-based, where the token starts */
    uint32_t       col;     /* 1-based byte column           */
} Token;

typedef struct {
    const char    *cur;        /* current read position */
    const char    *line_start; /* start of current line, for column math */
    uint32_t       line;       /* 1-based */
    enum TokenKind prev;       /* last emitted kind — drives '  disambiguation */
    const char    *error;      /* diagnostic for the most recent TOK_ERROR */
    int            depth;      /* open ( [ { nesting: newlines inside are whitespace */
} Lexer;

void  lexer_init(Lexer *lx, const char *src);
[[nodiscard]] Token lexer_next(Lexer *lx);
const char *token_kind_name(enum TokenKind k);

#endif /* NEUTRINO_LEXER_H */
