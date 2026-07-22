/* ast.h — Neutrino abstract syntax tree. */
#ifndef NEUTRINO_AST_H
#define NEUTRINO_AST_H

#include <stdint.h>
#include <stdio.h>
#include "arena.h"
#include "lexer.h"   /* for enum TokenKind (operator tags) */

typedef enum : uint8_t {
    AST_INT, AST_FLOAT, AST_IMAG, AST_STRING, AST_BOOL, AST_NULL, AST_IDENT,
    AST_UNARY,    /* prefix - + ~ !            */
    AST_POSTFIX,  /* postfix ' .'              */
    AST_BINARY,
    AST_RANGE,    /* start : (step :)? stop    */
    AST_CALL,     /* callee ( args )           */
    AST_INDEX,    /* target [ args ]           */
    AST_FIELD,    /* target . name             */
    AST_ROW,      /* one matrix row (its .list holds the elements) */
    AST_MATRIX,   /* .list holds AST_ROW nodes */
    AST_LAMBDA,   /* fn params -> body         */
    AST_IF,       /* if cond then .. else .. end */
    AST_RECORD,       /* { name = value, ... }     */
    AST_RECORD_FIELD, /* name = value (inside a record) */
    AST_LET,      /* let name = value          */
    AST_BLOCK,    /* program / statement sequence (unscoped: top level, loop body) */
    AST_BLOCK_EXPR, /* ( stmt; stmt; expr ) — scoped statement sequence as an expression */
    AST_COLON,    /* ':' as a whole-dimension index */
    AST_END,      /* 'end' inside an index */
    AST_BREAK,    /* break out of the enclosing loop */
    AST_CONTINUE, /* skip to the next loop iteration */
    AST_RETURN,   /* return [expr] from the enclosing function */
    AST_AT,       /* '@' — placeholder for the left side of the enclosing |> */
    AST_ASSIGN,   /* name = value (reuses .binary: lhs ident, rhs value) */
    AST_FOR,      /* for v = iter do body end   */
    AST_WHILE,    /* while cond do body end     */
} AstKind;

typedef struct AstNode AstNode;

typedef struct { AstNode **items; uint32_t count; } AstList;

struct AstNode {
    AstKind  kind;
    bool     silent;       /* statement terminated by ';' — suppress its echo */
    uint32_t line, col;
    union {
        struct { const char *text; uint32_t len; } lit;   /* literals + idents (raw slice) */
        bool boolean;
        struct { enum TokenKind op; AstNode *operand; } unary;     /* + postfix */
        struct { enum TokenKind op; AstNode *lhs, *rhs; } binary;
        struct { AstNode *start, *step, *stop; } range;
        struct { AstNode *callee; AstList args; } call;            /* + index (kind distinguishes) */
        struct { AstNode *target; const char *name; uint32_t namelen; } field;
        struct { AstList params; AstNode *body; const char *src; uint32_t srclen; } lambda;
        struct { AstNode *cond, *then_e, *else_e; } iff;
        struct { const char *name; uint32_t namelen; AstNode *value, *body; } let;  /* body!=null: let..in expr */
        struct { const char *name; uint32_t namelen; AstNode *value; } recfield;
        struct { const char *var; uint32_t varlen; AstNode *iter, *body; } forloop;
        struct { AstNode *cond, *body; } whileloop;
        AstList list;   /* AST_ROW elements, AST_MATRIX rows, AST_BLOCK stmts */
    } as;
};

AstNode *ast_alloc(Arena *a, AstKind kind, uint32_t line, uint32_t col);
void     ast_print(FILE *out, const AstNode *n);

bool ast_contains_at(const AstNode *n);

#endif
