/* lexer.c — Neutrino lexer, C23. */
#include "lexer.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* character classes (ASCII; bytes >127 only ever appear inside string */
/* and comment bodies, which are scanned raw)                          */
/* ------------------------------------------------------------------ */
static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
static inline bool is_hex(char c)   { char l = c | 0x20; return is_digit(c) || (l >= 'a' && l <= 'f'); }
static inline bool is_bin(char c)   { return c == '0' || c == '1'; }
static inline bool is_alpha(char c) { return (c|0x20) >= 'a' && (c|0x20) <= 'z'; }
static inline bool is_ident0(char c){ return is_alpha(c) || c == '_'; }
static inline bool is_identc(char c){ return is_ident0(c) || is_digit(c); }

/* A '  is a transpose operator (not an opening quote) iff the previous
 * significant token closed a value. Everywhere else it opens a string. */
static bool ends_value(enum TokenKind k)
{
    switch (k) {
    case TOK_IDENT: case TOK_INT: case TOK_FLOAT: case TOK_IMAG:
    case TOK_STRING: case TOK_RPAREN: case TOK_RBRACK: case TOK_RBRACE:
    case TOK_CTRANSPOSE: case TOK_TRANSPOSE: case TOK_KW_END:
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* keyword table — THE place to edit Neutrino's reserved words         */
/* ------------------------------------------------------------------ */
static enum TokenKind keyword_lookup(const char *s, size_t n)
{
    static const struct { const char *kw; enum TokenKind kind; } KEYWORDS[] = {
        {"let",    TOK_KW_LET},   {"fn",    TOK_KW_FN},    {"if",     TOK_KW_IF},
        {"then",   TOK_KW_THEN},  {"else",  TOK_KW_ELSE},  {"for",    TOK_KW_FOR},
        {"while",  TOK_KW_WHILE}, {"where", TOK_KW_WHERE}, {"do",    TOK_KW_DO},    {"end",    TOK_KW_END},
        {"return", TOK_KW_RETURN},{"true",  TOK_KW_TRUE},  {"false",  TOK_KW_FALSE},
        {"null",   TOK_KW_NULL},  {"in",    TOK_KW_IN},
        {"break",  TOK_KW_BREAK}, {"continue", TOK_KW_CONTINUE},
    };
    for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++)
        if (strlen(KEYWORDS[i].kw) == n && memcmp(KEYWORDS[i].kw, s, n) == 0)
            return KEYWORDS[i].kind;
    return TOK_IDENT;
}

/* ------------------------------------------------------------------ */
/* low-level cursor helpers                                            */
/* ------------------------------------------------------------------ */
void lexer_init(Lexer *lx, const char *src)
{
    *lx = (Lexer){
        .cur = src, .line_start = src, .line = 1,
        .prev = TOK_NEWLINE,   /* so a leading '  is a string, not transpose */
        .error = nullptr,
    };
}

static inline bool match(Lexer *lx, char expected)
{
    if (*lx->cur == expected) { lx->cur++; return true; }
    return false;
}

static Token tok(Lexer *lx, enum TokenKind kind,
                 const char *start, uint32_t line, uint32_t col)
{
    Token t = { .kind = kind, .start = start,
                .len = (uint32_t)(lx->cur - start), .line = line, .col = col };
    lx->prev = kind;
    return t;
}

static Token error_token(Lexer *lx, const char *msg,
                         const char *start, uint32_t line, uint32_t col)
{
    lx->error = msg;
    return tok(lx, TOK_ERROR, start, line, col);
}

/* ------------------------------------------------------------------ */
/* trivia: spaces, tabs, CR, line + block comments, `...` continuation */
/* (plain newlines are NOT trivia — they are tokens)                   */
/* ------------------------------------------------------------------ */
static void skip_trivia(Lexer *lx)
{
    for (;;) {
        char c = *lx->cur;
        if (c == ' ' || c == '\t' || c == '\r') {
            lx->cur++;
        } else if (c == '%' || c == '#') {
            if (lx->cur[1] == '{') {                 /* block comment %{ … %} */
                char open = c;
                lx->cur += 2;
                while (*lx->cur) {
                    if (*lx->cur == open && lx->cur[1] == '}') { lx->cur += 2; break; }
                    if (*lx->cur == '\n') { lx->line++; lx->line_start = lx->cur + 1; }
                    lx->cur++;
                }
            } else {                                  /* line comment to EOL */
                while (*lx->cur && *lx->cur != '\n') lx->cur++;
            }
        } else if (c == '.' && lx->cur[1] == '.' && lx->cur[2] == '.') {
            lx->cur += 3;                             /* line continuation */
            while (*lx->cur && *lx->cur != '\n') lx->cur++;
            if (*lx->cur == '\n') { lx->cur++; lx->line++; lx->line_start = lx->cur; }
        } else {
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* literal scanners                                                    */
/* ------------------------------------------------------------------ */

/* number: int / float / imaginary, with 0x 0b prefixes and _ separators.
 * Decimal point requires a following digit — so `3.` is `3` then `.`,
 * which keeps `3.*x`, `3.'`, `3.foo` unambiguous. */
static Token number(Lexer *lx, const char *start, uint32_t line, uint32_t col)
{
    if (lx->cur[0] == '0' && (lx->cur[1] == 'x' || lx->cur[1] == 'X')) {
        lx->cur += 2;
        while (is_hex(*lx->cur) || *lx->cur == '_') lx->cur++;
        return tok(lx, TOK_INT, start, line, col);
    }
    if (lx->cur[0] == '0' && (lx->cur[1] == 'b' || lx->cur[1] == 'B')) {
        lx->cur += 2;
        while (is_bin(*lx->cur) || *lx->cur == '_') lx->cur++;
        return tok(lx, TOK_INT, start, line, col);
    }

    bool is_float = false;
    while (is_digit(*lx->cur) || *lx->cur == '_') lx->cur++;

    if (*lx->cur == '.' && is_digit(lx->cur[1])) {
        is_float = true;
        lx->cur++;
        while (is_digit(*lx->cur) || *lx->cur == '_') lx->cur++;
    }
    if (*lx->cur == 'e' || *lx->cur == 'E') {        /* exponent (backtracks if bare) */
        const char *save = lx->cur;
        lx->cur++;
        if (*lx->cur == '+' || *lx->cur == '-') lx->cur++;
        if (is_digit(*lx->cur)) {
            is_float = true;
            while (is_digit(*lx->cur) || *lx->cur == '_') lx->cur++;
        } else {
            lx->cur = save;
        }
    }
    if ((*lx->cur == 'i' || *lx->cur == 'j') && !is_identc(lx->cur[1])) {
        lx->cur++;
        return tok(lx, TOK_IMAG, start, line, col);
    }
    return tok(lx, is_float ? TOK_FLOAT : TOK_INT, start, line, col);
}

/* "..."  — C-style escapes; single line. Slice retains the quotes; the
 * lexer does not decode escapes (that is the parser/eval stage's job). */
static Token string_dq(Lexer *lx, const char *start, uint32_t line, uint32_t col)
{
    lx->cur++;                                       /* opening " */
    while (*lx->cur && *lx->cur != '"' && *lx->cur != '\n') {
        lx->cur += (*lx->cur == '\\' && lx->cur[1]) ? 2 : 1;
    }
    if (*lx->cur != '"')
        return error_token(lx, "unterminated string", start, line, col);
    lx->cur++;                                       /* closing " */
    return tok(lx, TOK_STRING, start, line, col);
}

/* '...'  — Octave raw string; the only escape is '' for a literal quote. */
static Token string_sq(Lexer *lx, const char *start, uint32_t line, uint32_t col)
{
    lx->cur++;                                       /* opening ' */
    for (;;) {
        char c = *lx->cur;
        if (c == '\0' || c == '\n')
            return error_token(lx, "unterminated string", start, line, col);
        if (c == '\'') {
            if (lx->cur[1] == '\'') { lx->cur += 2; continue; }   /* '' escape */
            lx->cur++;                               /* closing ' */
            return tok(lx, TOK_STRING, start, line, col);
        }
        lx->cur++;
    }
}

static Token identifier(Lexer *lx, const char *start, uint32_t line, uint32_t col)
{
    while (is_identc(*lx->cur)) lx->cur++;
    enum TokenKind k = keyword_lookup(start, (size_t)(lx->cur - start));
    return tok(lx, k, start, line, col);
}

/* ------------------------------------------------------------------ */
/* main dispatch                                                       */
/* ------------------------------------------------------------------ */
Token lexer_next(Lexer *lx)
{
    skip_trivia(lx);

    const char *start = lx->cur;
    uint32_t    line  = lx->line;
    uint32_t    col   = (uint32_t)(start - lx->line_start) + 1;
    char        c     = *lx->cur;

    if (c == '\0')
        return tok(lx, TOK_EOF, start, line, col);

    /* newline: consume the run (newlines + intervening trivia). Inside any
     * open bracket the newline is just whitespace — expressions may span
     * lines there (matrix rows still need ';'). At top level, emit one. */
    if (c == '\n') {
        do {
            lx->cur++; lx->line++; lx->line_start = lx->cur;
            skip_trivia(lx);
        } while (*lx->cur == '\n');
        if (lx->depth > 0) return lexer_next(lx);
        return tok(lx, TOK_NEWLINE, start, line, col);
    }

    if (is_digit(c))                          return number(lx, start, line, col);
    if (c == '.' && is_digit(lx->cur[1]))     return number(lx, start, line, col);
    if (is_ident0(c))                         return identifier(lx, start, line, col);
    if (c == '"')                             return string_dq(lx, start, line, col);
    if (c == '\'') {
        if (ends_value(lx->prev)) { lx->cur++; return tok(lx, TOK_CTRANSPOSE, start, line, col); }
        return string_sq(lx, start, line, col);
    }

    lx->cur++;   /* consume first char of an operator/punct token */
    switch (c) {
    case '(': lx->depth++; return tok(lx, TOK_LPAREN, start, line, col);
    case ')': if (lx->depth > 0) lx->depth--; return tok(lx, TOK_RPAREN, start, line, col);
    case '[': lx->depth++; return tok(lx, TOK_LBRACK, start, line, col);
    case ']': if (lx->depth > 0) lx->depth--; return tok(lx, TOK_RBRACK, start, line, col);
    case '{': lx->depth++; return tok(lx, TOK_LBRACE, start, line, col);
    case '}': if (lx->depth > 0) lx->depth--; return tok(lx, TOK_RBRACE, start, line, col);
    case ',': return tok(lx, TOK_COMMA,  start, line, col);
    case ';': return tok(lx, TOK_SEMI,   start, line, col);
    case ':': return tok(lx, TOK_COLON,  start, line, col);
    case '@': return tok(lx, TOK_AT,     start, line, col);
    case '+': return tok(lx, TOK_PLUS,   start, line, col);
    case '*': return tok(lx, TOK_STAR,   start, line, col);
    case '/': return tok(lx, TOK_SLASH,  start, line, col);
    case '^': return tok(lx, TOK_CARET,  start, line, col);
    case '\\':return tok(lx, TOK_BACKSLASH, start, line, col);

    case '-': return tok(lx, match(lx, '>') ? TOK_ARROW : TOK_MINUS, start, line, col);
    case '=': return tok(lx, match(lx, '=') ? TOK_EQ
                           : match(lx, '>') ? TOK_FATARROW : TOK_ASSIGN, start, line, col);
    case '!': return tok(lx, match(lx, '=') ? TOK_NE : TOK_BANG,   start, line, col);
    case '~': return tok(lx, match(lx, '=') ? TOK_NE : match(lx, '>') ? TOK_TILDE_GT : TOK_TILDE,  start, line, col);
    case '<': return tok(lx, match(lx, '=') ? TOK_LE : TOK_LT,     start, line, col);
    case '>': return tok(lx, match(lx, '=') ? TOK_GE : TOK_GT,     start, line, col);
    case '&': return tok(lx, match(lx, '&') ? TOK_AND : TOK_AMP,   start, line, col);
    case '|': return tok(lx, match(lx, '>') ? (match(lx, '>') ? TOK_PIPE_GTGT : TOK_PIPE_GT)
                           : match(lx, '|') ? TOK_OR : TOK_PIPE,   start, line, col);

    case '.':   /* not a number (that was handled above): dot-op, .', or field dot */
        switch (*lx->cur) {
        case '*':  lx->cur++; return tok(lx, TOK_DOT_STAR,      start, line, col);
        case '/':  lx->cur++; return tok(lx, TOK_DOT_SLASH,     start, line, col);
        case '^':  lx->cur++; return tok(lx, TOK_DOT_CARET,     start, line, col);
        case '\\': lx->cur++; return tok(lx, TOK_DOT_BACKSLASH, start, line, col);
        case '\'': lx->cur++; return tok(lx, TOK_TRANSPOSE,     start, line, col);
        default:              return tok(lx, TOK_DOT,           start, line, col);
        }

    default:
        return error_token(lx, "unexpected character", start, line, col);
    }
}

/* ------------------------------------------------------------------ */
const char *token_kind_name(enum TokenKind k)
{
    switch (k) {
    case TOK_EOF:        return "EOF";
    case TOK_ERROR:      return "ERROR";
    case TOK_NEWLINE:    return "NEWLINE";
    case TOK_INT:        return "INT";
    case TOK_FLOAT:      return "FLOAT";
    case TOK_IMAG:       return "IMAG";
    case TOK_STRING:     return "STRING";
    case TOK_IDENT:      return "IDENT";
    case TOK_KW_LET:     return "KW_LET";
    case TOK_KW_FN:      return "KW_FN";
    case TOK_KW_IF:      return "KW_IF";
    case TOK_KW_THEN:    return "KW_THEN";
    case TOK_KW_ELSE:    return "KW_ELSE";
    case TOK_KW_FOR:     return "KW_FOR";
    case TOK_KW_WHILE:   return "KW_WHILE";
    case TOK_KW_WHERE:   return "KW_WHERE";
    case TOK_KW_DO:      return "KW_DO";
    case TOK_KW_END:     return "KW_END";
    case TOK_KW_RETURN:  return "KW_RETURN";
    case TOK_KW_BREAK:   return "KW_BREAK";
    case TOK_KW_CONTINUE:return "KW_CONTINUE";
    case TOK_KW_TRUE:    return "KW_TRUE";
    case TOK_KW_FALSE:   return "KW_FALSE";
    case TOK_KW_NULL:    return "KW_NULL";
    case TOK_KW_IN:      return "KW_IN";
    case TOK_LPAREN:     return "LPAREN";
    case TOK_RPAREN:     return "RPAREN";
    case TOK_LBRACK:     return "LBRACK";
    case TOK_RBRACK:     return "RBRACK";
    case TOK_LBRACE:     return "LBRACE";
    case TOK_RBRACE:     return "RBRACE";
    case TOK_PLUS:       return "PLUS";
    case TOK_MINUS:      return "MINUS";
    case TOK_STAR:       return "STAR";
    case TOK_SLASH:      return "SLASH";
    case TOK_CARET:      return "CARET";
    case TOK_BACKSLASH:  return "BACKSLASH";
    case TOK_DOT_STAR:   return "DOT_STAR";
    case TOK_DOT_SLASH:  return "DOT_SLASH";
    case TOK_DOT_CARET:  return "DOT_CARET";
    case TOK_DOT_BACKSLASH: return "DOT_BACKSLASH";
    case TOK_ASSIGN:     return "ASSIGN";
    case TOK_EQ:         return "EQ";
    case TOK_NE:         return "NE";
    case TOK_LT:         return "LT";
    case TOK_LE:         return "LE";
    case TOK_GT:         return "GT";
    case TOK_GE:         return "GE";
    case TOK_AND:        return "AND";
    case TOK_OR:         return "OR";
    case TOK_AMP:        return "AMP";
    case TOK_PIPE:       return "PIPE";
    case TOK_BANG:       return "BANG";
    case TOK_TILDE:      return "TILDE";
    case TOK_COLON:      return "COLON";
    case TOK_COMMA:      return "COMMA";
    case TOK_SEMI:       return "SEMI";
    case TOK_DOT:        return "DOT";
    case TOK_AT:         return "AT";
    case TOK_ARROW:      return "ARROW";
    case TOK_FATARROW:   return "FATARROW";
    case TOK_PIPE_GT:    return "PIPE_GT";
    case TOK_PIPE_GTGT:  return "PIPE_GTGT";
    case TOK_TILDE_GT:   return "TILDE_GT";
    case TOK_TRANSPOSE:  return "TRANSPOSE";
    case TOK_CTRANSPOSE: return "CTRANSPOSE";
    }
    return "?";
}
