/* parser.c */
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* token cursor                                                        */
/* ------------------------------------------------------------------ */
[[noreturn]] static void parse_error(Parser *p, const char *msg)
{
    p->had_error = true;
    p->err_msg   = msg;
    p->err_tok   = p->cur;
    longjmp(p->jmp, 1);
}

static void advance(Parser *p)
{
    p->prev = p->cur;
    p->cur  = lexer_next(&p->lex);
    if (p->cur.kind == TOK_ERROR)
        parse_error(p, p->lex.error ? p->lex.error : "lex error");
}

static bool check(Parser *p, enum TokenKind k) { return p->cur.kind == k; }

static bool accept(Parser *p, enum TokenKind k)
{
    if (p->cur.kind == k) { advance(p); return true; }
    return false;
}

static Token expect(Parser *p, enum TokenKind k, const char *what)
{
    if (p->cur.kind != k) {
        snprintf(p->msgbuf, sizeof p->msgbuf, "expected %s", what);
        parse_error(p, p->msgbuf);
    }
    Token t = p->cur;
    advance(p);
    return t;
}

static void skip_newlines(Parser *p) { while (p->cur.kind == TOK_NEWLINE) advance(p); }

/* ------------------------------------------------------------------ */
/* node helpers                                                        */
/* ------------------------------------------------------------------ */
static AstNode *node(Parser *p, AstKind k, Token at)
{
    return ast_alloc(p->arena, k, at.line, at.col);
}

/* growable scratch vector of node pointers, copied into the arena at close */
typedef struct { AstNode **data; uint32_t len, cap; } Vec;

static void vec_push(Vec *v, AstNode *n)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, v->cap * sizeof *v->data);
        if (!v->data) abort();
    }
    v->data[v->len++] = n;
}

static AstList vec_seal(Parser *p, Vec *v)
{
    AstList list = { .items = nullptr, .count = v->len };
    if (v->len) {
        list.items = arena_alloc(p->arena, v->len * sizeof *list.items);
        memcpy(list.items, v->data, v->len * sizeof *list.items);
    }
    free(v->data);
    return list;
}

/* ------------------------------------------------------------------ */
/* precedence                                                          */
/* ------------------------------------------------------------------ */
enum {
    BP_PIPE = 10, BP_OR = 20, BP_AND = 30, BP_BITOR = 40, BP_BITAND = 50,
    BP_CMP = 60, BP_RANGE = 70, BP_ADD = 80, BP_MUL = 90,
    BP_UNARY = 100, BP_POW = 110, BP_POSTFIX = 120, BP_CALL = 130,
};

static int infix_bp(enum TokenKind k)
{
    switch (k) {
    case TOK_PIPE_GT:   return BP_PIPE;
    case TOK_OR:        return BP_OR;
    case TOK_AND:       return BP_AND;
    case TOK_PIPE:      return BP_BITOR;
    case TOK_AMP:       return BP_BITAND;
    case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
                        return BP_CMP;
    case TOK_COLON:     return BP_RANGE;
    case TOK_PLUS: case TOK_MINUS: return BP_ADD;
    case TOK_STAR: case TOK_SLASH: case TOK_BACKSLASH:
    case TOK_DOT_STAR: case TOK_DOT_SLASH: case TOK_DOT_BACKSLASH:
                        return BP_MUL;
    case TOK_CARET: case TOK_DOT_CARET: return BP_POW;
    case TOK_CTRANSPOSE: case TOK_TRANSPOSE: return BP_POSTFIX;
    case TOK_LPAREN: case TOK_LBRACK: case TOK_DOT: return BP_CALL;
    default:            return 0;
    }
}

static AstNode *parse_expr(Parser *p, int min_bp);
static AstNode *parse_statement(Parser *p);

/* literal / ident node from the current token, then advance */
static AstNode *lit_node(Parser *p, AstKind k)
{
    AstNode *n = node(p, k, p->cur);
    n->as.lit.text = p->cur.start;
    n->as.lit.len  = p->cur.len;
    advance(p);
    return n;
}

/* ------------------------------------------------------------------ */
/* nud handlers (prefix position)                                      */
/* ------------------------------------------------------------------ */
static AstNode *parse_matrix(Parser *p)
{
    Token open = p->cur;
    expect(p, TOK_LBRACK, "'['");
    AstNode *m = node(p, AST_MATRIX, open);
    Vec rows = {0};
    skip_newlines(p);

    while (!check(p, TOK_RBRACK)) {
        AstNode *row = node(p, AST_ROW, p->cur);
        Vec elems = {0};
        vec_push(&elems, parse_expr(p, 0));
        while (accept(p, TOK_COMMA)) {        /* comma continues the row (absorbs newlines) */
            skip_newlines(p);
            vec_push(&elems, parse_expr(p, 0));
        }
        row->as.list = vec_seal(p, &elems);
        vec_push(&rows, row);

        if (check(p, TOK_SEMI) || check(p, TOK_NEWLINE)) {
            while (accept(p, TOK_SEMI) || accept(p, TOK_NEWLINE)) { }
        } else if (!check(p, TOK_RBRACK)) {
            parse_error(p, "expected ',' or ';' between matrix elements");
        }
    }
    expect(p, TOK_RBRACK, "']' to close matrix");
    m->as.list = vec_seal(p, &rows);
    return m;
}

static AstNode *parse_lambda(Parser *p)
{
    Token kw = p->cur;
    expect(p, TOK_KW_FN, "'fn'");
    AstNode *lam = node(p, AST_LAMBDA, kw);
    Vec params = {0};
    if (!check(p, TOK_ARROW)) {
        do {
            Token id = expect(p, TOK_IDENT, "parameter name");
            AstNode *pn = node(p, AST_IDENT, id);
            pn->as.lit.text = id.start;
            pn->as.lit.len  = id.len;
            vec_push(&params, pn);
        } while (accept(p, TOK_COMMA));
    }
    expect(p, TOK_ARROW, "'->' after lambda parameters");
    lam->as.lambda.params = vec_seal(p, &params);
    lam->as.lambda.body   = parse_expr(p, 0);  /* full expression, bounded by enclosing terminators */
    return lam;
}

static AstNode *parse_if(Parser *p)
{
    Token kw = p->cur;
    expect(p, TOK_KW_IF, "'if'");
    AstNode *n = node(p, AST_IF, kw);
    n->as.iff.cond = parse_expr(p, 0);
    skip_newlines(p);
    expect(p, TOK_KW_THEN, "'then'");
    skip_newlines(p);
    n->as.iff.then_e = parse_expr(p, 0);
    skip_newlines(p);
    if (accept(p, TOK_KW_ELSE)) {
        skip_newlines(p);
        n->as.iff.else_e = parse_expr(p, 0);
        skip_newlines(p);
    }
    expect(p, TOK_KW_END, "'end' to close if");
    return n;
}

static AstNode *parse_record(Parser *p)
{
    Token open = p->cur;
    expect(p, TOK_LBRACE, "'{'");
    AstNode *rec = node(p, AST_RECORD, open);
    Vec fields = {0};
    skip_newlines(p);

    while (!check(p, TOK_RBRACE)) {
        /* keyed fields only: IDENT '=' value. Anything else is the
         * reserved positional form, which we deliberately do not accept yet. */
        Token fstart = p->cur;
        if (!check(p, TOK_IDENT))
            parse_error(p, "positional records are reserved — use named fields, e.g. {name = value}");
        Token name = p->cur;
        advance(p);
        if (!check(p, TOK_ASSIGN)) {
            if (check(p, TOK_COLON))
                parse_error(p, "record fields use '=', not ':' (':' is the range operator)");
            parse_error(p, "positional records are reserved — use named fields, e.g. {name = value}");
        }
        advance(p);                       /* consume '=' */
        skip_newlines(p);

        AstNode *f = node(p, AST_RECORD_FIELD, fstart);
        f->as.recfield.name    = name.start;
        f->as.recfield.namelen = name.len;
        f->as.recfield.value   = parse_expr(p, 0);
        vec_push(&fields, f);

        skip_newlines(p);
        if (!accept(p, TOK_COMMA)) break;  /* no comma ⇒ record must end */
        skip_newlines(p);                  /* trailing comma allowed */
    }
    expect(p, TOK_RBRACE, "'}' to close record");
    rec->as.list = vec_seal(p, &fields);
    return rec;
}

/* ------------------------------------------------------------------ */
/* operator sections: '_' inside grouping parens becomes a lambda param */
/* ------------------------------------------------------------------ */
static bool is_hole(const AstNode *n)
{
    return n->kind == AST_IDENT && n->as.lit.len == 1 && n->as.lit.text[0] == '_';
}

/* collect '_' holes in evaluation (left-to-right) order, not descending into a
 * nested lambda (which includes already-built inner sections), so each '_'
 * binds to its own innermost grouping. */
static void collect_holes(AstNode *n, Vec *out)
{
    if (!n) return;
    if (is_hole(n)) { vec_push(out, n); return; }
    switch (n->kind) {
    case AST_LAMBDA: return;
    case AST_UNARY: case AST_POSTFIX: collect_holes(n->as.unary.operand, out); return;
    case AST_BINARY: collect_holes(n->as.binary.lhs, out); collect_holes(n->as.binary.rhs, out); return;
    case AST_RANGE:
        collect_holes(n->as.range.start, out); collect_holes(n->as.range.step, out); collect_holes(n->as.range.stop, out); return;
    case AST_CALL: case AST_INDEX:
        collect_holes(n->as.call.callee, out);
        for (uint32_t i = 0; i < n->as.call.args.count; i++) collect_holes(n->as.call.args.items[i], out);
        return;
    case AST_FIELD: collect_holes(n->as.field.target, out); return;
    case AST_IF:
        collect_holes(n->as.iff.cond, out); collect_holes(n->as.iff.then_e, out); collect_holes(n->as.iff.else_e, out); return;
    case AST_RECORD: case AST_MATRIX: case AST_ROW: case AST_BLOCK:
        for (uint32_t i = 0; i < n->as.list.count; i++) collect_holes(n->as.list.items[i], out);
        return;
    case AST_RECORD_FIELD: collect_holes(n->as.recfield.value, out); return;
    case AST_LET: collect_holes(n->as.let.value, out); return;
    case AST_ASSIGN: collect_holes(n->as.binary.rhs, out); return;
    case AST_FOR: collect_holes(n->as.forloop.iter, out); collect_holes(n->as.forloop.body, out); return;
    case AST_WHILE: collect_holes(n->as.whileloop.cond, out); collect_holes(n->as.whileloop.body, out); return;
    default: return;
    }
}

/* If e contains holes, rewrite it into  fn _@0, _@1, .. -> e  (each '_' becomes
 * the next fresh parameter). '@' can't appear in an identifier, so the generated
 * names can never collide with a user binding. */
static AstNode *maybe_section(Parser *p, AstNode *e)
{
    Vec holes = {0};
    collect_holes(e, &holes);
    if (holes.len == 0) { free(holes.data); return e; }

    AstNode *lam = ast_alloc(p->arena, AST_LAMBDA, e->line, e->col);
    Vec params = {0};
    for (uint32_t i = 0; i < holes.len; i++) {
        char *nm = arena_alloc(p->arena, 16);
        int len = snprintf(nm, 16, "_@%u", i);
        holes.data[i]->as.lit.text = nm;          /* the hole now references the param */
        holes.data[i]->as.lit.len  = (uint32_t)len;
        AstNode *pn = ast_alloc(p->arena, AST_IDENT, e->line, e->col);
        pn->as.lit.text = nm; pn->as.lit.len = (uint32_t)len;
        vec_push(&params, pn);
    }
    lam->as.lambda.params = vec_seal(p, &params);
    lam->as.lambda.body   = e;
    free(holes.data);
    return lam;
}

/* parse a statement sequence up to (but not consuming) 'end' or EOF */
static AstNode *parse_block_until_end(Parser *p)
{
    AstNode *block = node(p, AST_BLOCK, p->cur);
    Vec stmts = {0};
    while (accept(p, TOK_NEWLINE) || accept(p, TOK_SEMI)) { }
    while (!check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
        AstNode *s = parse_statement(p);
        if (check(p, TOK_SEMI)) s->silent = true;
        vec_push(&stmts, s);
        if (check(p, TOK_KW_END) || check(p, TOK_EOF)) break;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_SEMI))
            parse_error(p, "expected newline or ';' in loop body");
        while (accept(p, TOK_NEWLINE) || accept(p, TOK_SEMI)) { }
    }
    block->as.list = vec_seal(p, &stmts);
    return block;
}

static AstNode *parse_for(Parser *p)
{
    Token kw = p->cur;
    expect(p, TOK_KW_FOR, "'for'");
    Token var = expect(p, TOK_IDENT, "loop variable after 'for'");
    expect(p, TOK_ASSIGN, "'=' after the loop variable");
    AstNode *n = node(p, AST_FOR, kw);
    n->as.forloop.var    = var.start;
    n->as.forloop.varlen = var.len;
    n->as.forloop.iter   = parse_expr(p, 0);
    skip_newlines(p);
    expect(p, TOK_KW_DO, "'do' before the loop body");
    n->as.forloop.body = parse_block_until_end(p);
    expect(p, TOK_KW_END, "'end' to close for");
    return n;
}

static AstNode *parse_while(Parser *p)
{
    Token kw = p->cur;
    expect(p, TOK_KW_WHILE, "'while'");
    AstNode *n = node(p, AST_WHILE, kw);
    n->as.whileloop.cond = parse_expr(p, 0);
    skip_newlines(p);
    expect(p, TOK_KW_DO, "'do' before the loop body");
    n->as.whileloop.body = parse_block_until_end(p);
    expect(p, TOK_KW_END, "'end' to close while");
    return n;
}

static AstNode *parse_nud(Parser *p)
{
    Token t = p->cur;
    switch (t.kind) {
    case TOK_INT:      return lit_node(p, AST_INT);
    case TOK_FLOAT:    return lit_node(p, AST_FLOAT);
    case TOK_IMAG:     return lit_node(p, AST_IMAG);
    case TOK_STRING:   return lit_node(p, AST_STRING);
    case TOK_IDENT:    return lit_node(p, AST_IDENT);
    case TOK_KW_TRUE:  { AstNode *n = node(p, AST_BOOL, t); n->as.boolean = true;  advance(p); return n; }
    case TOK_KW_FALSE: { AstNode *n = node(p, AST_BOOL, t); n->as.boolean = false; advance(p); return n; }
    case TOK_KW_NULL:  { AstNode *n = node(p, AST_NULL, t); advance(p); return n; }

    case TOK_MINUS: case TOK_PLUS: case TOK_TILDE: case TOK_BANG: {
        advance(p);
        AstNode *n = node(p, AST_UNARY, t);
        n->as.unary.op = t.kind;
        n->as.unary.operand = parse_expr(p, BP_UNARY);
        return n;
    }
    case TOK_LPAREN: {
        Token open = p->cur;
        advance(p);
        skip_newlines(p);
        AstNode *first = parse_statement(p);
        skip_newlines(p);
        if (check(p, TOK_SEMI)) {                  /* block-expression: ( s; s; expr ) */
            AstNode *blk = node(p, AST_BLOCK_EXPR, open);
            Vec stmts = {0};
            vec_push(&stmts, first);
            while (accept(p, TOK_SEMI)) {
                skip_newlines(p);
                if (check(p, TOK_RPAREN)) break;   /* a trailing ';' is fine */
                vec_push(&stmts, parse_statement(p));
                skip_newlines(p);
            }
            blk->as.list = vec_seal(p, &stmts);
            expect(p, TOK_RPAREN, "')'");
            return blk;
        }
        expect(p, TOK_RPAREN, "')'");
        if (first->kind == AST_ASSIGN || first->kind == AST_LET)
            return first;                          /* a lone binding/assignment in parens */
        return maybe_section(p, first);            /* '(_ + 1)' etc. becomes a lambda */
    }
    case TOK_LBRACK:   return parse_matrix(p);
    case TOK_KW_FN:    return parse_lambda(p);
    case TOK_KW_IF:    return parse_if(p);
    case TOK_KW_FOR:   return parse_for(p);
    case TOK_KW_WHILE: return parse_while(p);

    case TOK_LBRACE:   return parse_record(p);
    case TOK_KW_LET: {                       /* let x = v in body  (expression) */
        Token kw = p->cur; advance(p);
        Token name = expect(p, TOK_IDENT, "name after 'let'");
        expect(p, TOK_ASSIGN, "'=' in let binding");
        AstNode *n = node(p, AST_LET, kw);
        n->as.let.name    = name.start;
        n->as.let.namelen = name.len;
        n->as.let.value   = parse_expr(p, 0);
        expect(p, TOK_KW_IN, "'in' (a 'let' expression is 'let x = .. in ..')");
        n->as.let.body    = parse_expr(p, 0);
        return n;
    }
    case TOK_AT:       { AstNode *n = node(p, AST_AT, t); advance(p); return n; }
    case TOK_KW_END:
        if (p->in_index == 0) parse_error(p, "'end' is only valid inside an index, e.g. a[end]");
        { AstNode *n = node(p, AST_END, t); advance(p); return n; }
    case TOK_KW_BREAK:    { AstNode *n = node(p, AST_BREAK, t);    advance(p); return n; }
    case TOK_KW_CONTINUE: { AstNode *n = node(p, AST_CONTINUE, t); advance(p); return n; }
    case TOK_KW_RETURN: {
        AstNode *n = node(p, AST_RETURN, t);
        advance(p);
        n->as.unary.operand = nullptr;                 /* bare 'return' -> null */
        switch (p->cur.kind) {                          /* a value follows unless a terminator is next */
        case TOK_NEWLINE: case TOK_SEMI: case TOK_EOF: case TOK_KW_END:
        case TOK_KW_ELSE: case TOK_RPAREN: case TOK_RBRACK: case TOK_RBRACE: case TOK_COMMA:
            break;
        default:
            n->as.unary.operand = parse_expr(p, 0);
        }
        return n;
    }
    default:           parse_error(p, "expected an expression");
    }
}

/* ------------------------------------------------------------------ */
/* led handlers (infix / postfix position)                             */
/* ------------------------------------------------------------------ */
static AstList parse_arglist(Parser *p, enum TokenKind close, const char *what)
{
    Vec args = {0};
    skip_newlines(p);
    if (!check(p, close)) {
        vec_push(&args, parse_expr(p, 0));
        while (accept(p, TOK_COMMA)) {
            skip_newlines(p);
            vec_push(&args, parse_expr(p, 0));
        }
    }
    skip_newlines(p);
    expect(p, close, what);
    return vec_seal(p, &args);
}

static AstNode *parse_index_arg(Parser *p)
{
    if (check(p, TOK_COLON)) {                    /* bare ':' = the whole dimension */
        AstNode *n = node(p, AST_COLON, p->cur);
        advance(p);
        return n;
    }
    return parse_expr(p, 0);                       /* scalar, range a:b, vector, or mask */
}

static AstList parse_index_arglist(Parser *p)
{
    Vec args = {0};
    p->in_index++;                                /* enables 'end' within these args */
    skip_newlines(p);
    if (!check(p, TOK_RBRACK)) {
        vec_push(&args, parse_index_arg(p));
        while (accept(p, TOK_COMMA)) { skip_newlines(p); vec_push(&args, parse_index_arg(p)); }
    }
    skip_newlines(p);
    expect(p, TOK_RBRACK, "']'");
    p->in_index--;
    return vec_seal(p, &args);
}

static AstNode *parse_range(Parser *p, AstNode *left)
{
    AstNode *n = node(p, AST_RANGE, p->cur);
    expect(p, TOK_COLON, "':'");
    n->as.range.start = left;
    AstNode *mid = parse_expr(p, BP_RANGE);   /* additive binds in; another ':' does not */
    if (accept(p, TOK_COLON)) {
        n->as.range.step = mid;
        n->as.range.stop = parse_expr(p, BP_RANGE);
    } else {
        n->as.range.step = nullptr;           /* default step 1 */
        n->as.range.stop = mid;
    }
    return n;
}

static AstNode *parse_led(Parser *p, AstNode *left, int lbp)
{
    Token t = p->cur;
    switch (t.kind) {
    case TOK_CARET: case TOK_DOT_CARET: {        /* right-associative */
        advance(p);
        AstNode *n = node(p, AST_BINARY, t);
        n->as.binary.op = t.kind;
        n->as.binary.lhs = left;
        n->as.binary.rhs = parse_expr(p, lbp - 1);
        return n;
    }
    case TOK_CTRANSPOSE: case TOK_TRANSPOSE: {   /* postfix */
        advance(p);
        AstNode *n = node(p, AST_POSTFIX, t);
        n->as.unary.op = t.kind;
        n->as.unary.operand = left;
        return n;
    }
    case TOK_LPAREN: {
        AstNode *n = node(p, AST_CALL, t);
        advance(p);
        n->as.call.callee = left;
        n->as.call.args = parse_arglist(p, TOK_RPAREN, "')'");
        return n;
    }
    case TOK_LBRACK: {
        AstNode *n = node(p, AST_INDEX, t);
        advance(p);
        n->as.call.callee = left;
        n->as.call.args = parse_index_arglist(p);
        return n;
    }
    case TOK_DOT: {
        advance(p);
        Token name = expect(p, TOK_IDENT, "field name after '.'");
        AstNode *n = node(p, AST_FIELD, t);
        n->as.field.target  = left;
        n->as.field.name    = name.start;
        n->as.field.namelen = name.len;
        return n;
    }
    case TOK_COLON:
        return parse_range(p, left);

    default: {                                    /* left-associative binary */
        advance(p);
        AstNode *n = node(p, AST_BINARY, t);
        n->as.binary.op = t.kind;
        n->as.binary.lhs = left;
        n->as.binary.rhs = parse_expr(p, lbp);
        return n;
    }
    }
}

static AstNode *parse_expr(Parser *p, int min_bp)
{
    AstNode *left = parse_nud(p);
    for (;;) {
        int lbp = infix_bp(p->cur.kind);
        if (lbp <= min_bp) break;
        left = parse_led(p, left, lbp);
    }
    return left;
}

/* ------------------------------------------------------------------ */
/* statements / program                                                */
/* ------------------------------------------------------------------ */
static AstNode *parse_statement(Parser *p)
{
    if (check(p, TOK_KW_LET)) {
        Token kw = p->cur;
        advance(p);
        Token name = expect(p, TOK_IDENT, "name after 'let'");
        expect(p, TOK_ASSIGN, "'=' in let binding");
        AstNode *n = node(p, AST_LET, kw);
        n->as.let.name    = name.start;
        n->as.let.namelen = name.len;
        n->as.let.value   = parse_expr(p, 0);
        if (accept(p, TOK_KW_IN))            /* 'let x = v in body' used as a statement */
            n->as.let.body = parse_expr(p, 0);
        return n;
    }
    AstNode *e = parse_expr(p, 0);
    if (check(p, TOK_ASSIGN)) {                 /* name = value  or  name[idx] = value */
        if (e->kind == AST_INDEX) {
            if (e->as.call.callee->kind != AST_IDENT)
                parse_error(p, "indexed assignment needs a name target, e.g. a[i] = x");
        } else if (e->kind != AST_IDENT) {
            parse_error(p, "invalid assignment target (only a name or an index can be assigned)");
        }
        Token kw = p->cur;
        advance(p);
        AstNode *n = node(p, AST_ASSIGN, kw);
        n->as.binary.op  = TOK_ASSIGN;
        n->as.binary.lhs = e;
        n->as.binary.rhs = parse_expr(p, 0);
        return n;
    }
    return e;
}

static AstNode *parse_program(Parser *p)
{
    AstNode *block = node(p, AST_BLOCK, p->cur);
    Vec stmts = {0};
    while (accept(p, TOK_NEWLINE) || accept(p, TOK_SEMI)) { }

    while (!check(p, TOK_EOF)) {
        AstNode *s = parse_statement(p);
        if (check(p, TOK_SEMI)) s->silent = true;   /* ';' suppresses this statement's echo */
        vec_push(&stmts, s);
        if (check(p, TOK_EOF)) break;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_SEMI))
            parse_error(p, "expected newline or ';' after statement");
        while (accept(p, TOK_NEWLINE) || accept(p, TOK_SEMI)) { }
    }
    block->as.list = vec_seal(p, &stmts);
    return block;
}

/* ------------------------------------------------------------------ */
void parser_init(Parser *p, const char *src, Arena *arena)
{
    *p = (Parser){ .arena = arena };
    lexer_init(&p->lex, src);
}

AstNode *parser_parse(Parser *p)
{
    if (setjmp(p->jmp)) return nullptr;   /* a parse_error unwinds to here */
    advance(p);                           /* prime the lookahead */
    return parse_program(p);
}
