/* ast.c — node allocation + S-expression pretty-printer. */
#include "ast.h"

AstNode *ast_alloc(Arena *a, AstKind kind, uint32_t line, uint32_t col)
{
    AstNode *n = arena_alloc(a, sizeof *n);
    *n = (AstNode){0};
    n->kind = kind;
    n->line = line;
    n->col  = col;
    return n;
}

static const char *op_symbol(enum TokenKind k)
{
    switch (k) {
    case TOK_PLUS:          return "+";
    case TOK_MINUS:         return "-";
    case TOK_STAR:          return "*";
    case TOK_SLASH:         return "/";
    case TOK_BACKSLASH:     return "\\";
    case TOK_CARET:         return "^";
    case TOK_DOT_STAR:      return ".*";
    case TOK_DOT_SLASH:     return "./";
    case TOK_DOT_CARET:     return ".^";
    case TOK_DOT_BACKSLASH: return ".\\";
    case TOK_EQ:            return "==";
    case TOK_NE:            return "!=";
    case TOK_LT:            return "<";
    case TOK_LE:            return "<=";
    case TOK_GT:            return ">";
    case TOK_GE:            return ">=";
    case TOK_AND:           return "&&";
    case TOK_OR:            return "||";
    case TOK_AMP:           return "&";
    case TOK_PIPE:          return "|";
    case TOK_PIPE_GT:       return "|>";
    case TOK_TILDE:         return "~";
    case TOK_BANG:          return "!";
    case TOK_CTRANSPOSE:    return "'";
    case TOK_TRANSPOSE:     return ".'";
    default:                return "?";
    }
}

static void indent(FILE *out, int depth)
{
    for (int i = 0; i < depth; i++) fputs("  ", out);
}

static void print(FILE *out, const AstNode *n, int depth);

static void print_list(FILE *out, const AstList *l, int depth)
{
    for (uint32_t i = 0; i < l->count; i++) {
        fputc('\n', out);
        print(out, l->items[i], depth);
    }
}

static void print(FILE *out, const AstNode *n, int depth)
{
    indent(out, depth);
    if (!n) { fputs("<null>", out); return; }

    switch (n->kind) {
    case AST_INT:    fprintf(out, "(int %.*s)",    (int)n->as.lit.len, n->as.lit.text); break;
    case AST_FLOAT:  fprintf(out, "(float %.*s)",  (int)n->as.lit.len, n->as.lit.text); break;
    case AST_IMAG:   fprintf(out, "(imag %.*s)",   (int)n->as.lit.len, n->as.lit.text); break;
    case AST_STRING: fprintf(out, "(str %.*s)",    (int)n->as.lit.len, n->as.lit.text); break;
    case AST_IDENT:  fprintf(out, "(id %.*s)",     (int)n->as.lit.len, n->as.lit.text); break;
    case AST_BOOL:   fprintf(out, "(bool %s)", n->as.boolean ? "true" : "false");       break;
    case AST_NULL:   fputs("(null)", out);                                              break;

    case AST_UNARY:
        fprintf(out, "(unary %s\n", op_symbol(n->as.unary.op));
        print(out, n->as.unary.operand, depth + 1);
        fputc(')', out);
        break;

    case AST_POSTFIX:
        fprintf(out, "(postfix %s\n", op_symbol(n->as.unary.op));
        print(out, n->as.unary.operand, depth + 1);
        fputc(')', out);
        break;

    case AST_BINARY:
        fprintf(out, "(%s\n", op_symbol(n->as.binary.op));
        print(out, n->as.binary.lhs, depth + 1); fputc('\n', out);
        print(out, n->as.binary.rhs, depth + 1);
        fputc(')', out);
        break;

    case AST_RANGE:
        fputs("(range\n", out);
        print(out, n->as.range.start, depth + 1); fputc('\n', out);
        if (n->as.range.step) { print(out, n->as.range.step, depth + 1); fputc('\n', out); }
        else { indent(out, depth + 1); fputs("<step 1>\n", out); }
        print(out, n->as.range.stop, depth + 1);
        fputc(')', out);
        break;

    case AST_CALL:
        fputs("(call\n", out);
        print(out, n->as.call.callee, depth + 1);
        print_list(out, &n->as.call.args, depth + 1);
        fputc(')', out);
        break;

    case AST_INDEX:
        fputs("(index\n", out);
        print(out, n->as.call.callee, depth + 1);
        print_list(out, &n->as.call.args, depth + 1);
        fputc(')', out);
        break;

    case AST_FIELD:
        fprintf(out, "(field .%.*s\n", (int)n->as.field.namelen, n->as.field.name);
        print(out, n->as.field.target, depth + 1);
        fputc(')', out);
        break;

    case AST_ROW:
        fputs("(row", out);
        print_list(out, &n->as.list, depth + 1);
        fputc(')', out);
        break;

    case AST_MATRIX:
        fputs("(matrix", out);
        print_list(out, &n->as.list, depth + 1);
        fputc(')', out);
        break;

    case AST_LAMBDA:
        fputs("(lambda (", out);
        for (uint32_t i = 0; i < n->as.lambda.params.count; i++) {
            const AstNode *p = n->as.lambda.params.items[i];
            fprintf(out, "%s%.*s", i ? " " : "", (int)p->as.lit.len, p->as.lit.text);
        }
        fputs(")\n", out);
        print(out, n->as.lambda.body, depth + 1);
        fputc(')', out);
        break;

    case AST_IF:
        fputs("(if\n", out);
        print(out, n->as.iff.cond, depth + 1);   fputc('\n', out);
        print(out, n->as.iff.then_e, depth + 1);
        if (n->as.iff.else_e) { fputc('\n', out); print(out, n->as.iff.else_e, depth + 1); }
        fputc(')', out);
        break;

    case AST_RECORD:
        fputs("(record", out);
        print_list(out, &n->as.list, depth + 1);
        fputc(')', out);
        break;

    case AST_RECORD_FIELD:
        fprintf(out, "(= %.*s\n", (int)n->as.recfield.namelen, n->as.recfield.name);
        print(out, n->as.recfield.value, depth + 1);
        fputc(')', out);
        break;

    case AST_LET:
        fprintf(out, n->as.let.body ? "(let-in %.*s\n" : "(let %.*s\n",
                (int)n->as.let.namelen, n->as.let.name);
        print(out, n->as.let.value, depth + 1);
        if (n->as.let.body) { fputc('\n', out); print(out, n->as.let.body, depth + 1); }
        fputc(')', out);
        break;

    case AST_BLOCK:
        fputs("(block", out);
        print_list(out, &n->as.list, depth + 1);
        fputc(')', out);
        break;
    case AST_BLOCK_EXPR:
        fputs("(block-expr", out);
        print_list(out, &n->as.list, depth + 1);
        fputc(')', out);
        break;
    case AST_AT:
        fputs("(@)", out);
        break;
    case AST_COLON:
        fputs("(:)", out);
        break;
    case AST_END:
        fputs("(end)", out);
        break;
    case AST_BREAK:
        fputs("(break)", out);
        break;
    case AST_CONTINUE:
        fputs("(continue)", out);
        break;
    case AST_RETURN:
        fputs("(return", out);
        if (n->as.unary.operand) { fputc(' ', out); print(out, n->as.unary.operand, depth); }
        fputc(')', out);
        break;
    case AST_ASSIGN:
        fprintf(out, "(assign %.*s", (int)n->as.binary.lhs->as.lit.len, n->as.binary.lhs->as.lit.text);
        print(out, n->as.binary.rhs, depth + 1);
        fputc(')', out);
        break;
    case AST_FOR:
        fprintf(out, "(for %.*s", (int)n->as.forloop.varlen, n->as.forloop.var);
        print(out, n->as.forloop.iter, depth + 1);
        print(out, n->as.forloop.body, depth + 1);
        fputc(')', out);
        break;
    case AST_WHILE:
        fputs("(while", out);
        print(out, n->as.whileloop.cond, depth + 1);
        print(out, n->as.whileloop.body, depth + 1);
        fputc(')', out);
        break;
    }
}

void ast_print(FILE *out, const AstNode *n)
{
    print(out, n, 0);
    fputc('\n', out);
}
