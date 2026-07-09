/* compile.c — Neutrino AST -> bytecode, with lexical addressing.
 *
 * A resolver pass (the Compiler stack) classifies every identifier as a frame
 * slot (a function's self or parameters), an upvalue (a free variable captured
 * from an enclosing function by value-snapshot), or a name (a loop-body `let`
 * or a global, resolved through the runtime env at execution time). Function
 * calls thus need no per-call environment — parameters live in stack slots —
 * and closures capture only the values they use, so a closure never holds a
 * reference back to the scope that made it (the env/closure cycle is gone).
 * Globals stay late-bound by name, which is what keeps recursion and mutual
 * recursion working. A function's own bound name resolves to slot 0 (the
 * self-slot), so direct recursion is a slot read, not a global lookup. */
#include "compile.h"
#include "nrt.h"
#include "lexer.h"
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

[[noreturn]] static void compile_error(Interp *I, AstNode *n, const char *fmt, ...)
{
    char msg[200];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    I->cur_line = n->line; I->cur_col = n->col;
    snprintf(I->err, sizeof I->err, "%s", msg);
    I->had_error = true;
    longjmp(I->jmp, 1);
}

/* ---- resolver state ------------------------------------------------------ */

typedef enum { LK_SLOT, LK_ENV } LKind;

typedef struct {
    const char *name; uint32_t len;
    LKind   kind;                 /* SLOT: self/params; ENV: loop var / loop-body let */
    uint8_t slot;                 /* if SLOT                                           */
    int     depth;                /* lexical depth where declared                      */
} CLocal;

typedef struct {
    bool    from_local;           /* captured from the enclosing function's local ...  */
    bool    is_slot;              /*   ... which is a SLOT (else an ENV local, by name) */
    uint8_t slot;                 /* if from_local && is_slot                          */
    uint8_t up_index;             /* if !from_local: enclosing upvalue index           */
    const char *name; uint32_t len;
} CUpval;

typedef struct Compiler {
    struct Compiler *enclosing;
    Chunk  *chunk;
    CLocal  locals[256];  int nlocals; int nslots;
    CUpval  upvals[256];  int nupvals;
    int     depth;                /* 0 = global (top-level); >0 = inside a scope        */
    int     index_dim;           /* dimension being compiled inside an index, else -1   */
    int     scope_count;         /* live runtime scopes (SCOPE_PUSH / FOR_NEXT), for break */
    bool    in_function;         /* compiling a lambda body? (enables 'return')         */
    struct { int scope_base; uint32_t cont_target; bool is_for;
             uint32_t breaks[32]; int nbreaks; } loops[8];
    int     nloops;
} Compiler;

static void compile_node(Interp *I, Compiler *cc, AstNode *n);
static void compile_lambda(Interp *I, Compiler *cc, AstNode *n,
                           const char *selfname, uint32_t selflen);
static void emit_name_op_g(Chunk *c, uint8_t op, const char *s, uint32_t len, uint32_t line);

static int resolve_local(Compiler *cc, const char *name, uint32_t len)
{
    for (int i = cc->nlocals - 1; i >= 0; i--)
        if (cc->locals[i].len == len && memcmp(cc->locals[i].name, name, len) == 0)
            return i;
    return -1;
}

static int add_upval(Compiler *cc, CUpval uv)
{
    for (int i = 0; i < cc->nupvals; i++) {           /* dedup */
        CUpval *e = &cc->upvals[i];
        if (e->from_local == uv.from_local && e->is_slot == uv.is_slot &&
            e->slot == uv.slot && e->up_index == uv.up_index &&
            e->len == uv.len && (uv.len == 0 || memcmp(e->name, uv.name, uv.len) == 0))
            return i;
    }
    cc->upvals[cc->nupvals] = uv;
    return cc->nupvals++;
}

/* Is `name` a local (any kind) of some enclosing function? If so register an
 * upvalue chain down to `cc` and return its index here; else -1 (it's global). */
static int resolve_upvalue(Compiler *cc, const char *name, uint32_t len)
{
    if (!cc->enclosing) return -1;
    int li = resolve_local(cc->enclosing, name, len);
    if (li >= 0) {
        CLocal *l = &cc->enclosing->locals[li];
        CUpval uv = { .from_local = true, .is_slot = (l->kind == LK_SLOT),
                      .slot = l->slot, .up_index = 0, .name = name, .len = len };
        return add_upval(cc, uv);
    }
    int ui = resolve_upvalue(cc->enclosing, name, len);
    if (ui >= 0) {
        CUpval uv = { .from_local = false, .is_slot = false, .slot = 0,
                      .up_index = (uint8_t)ui, .name = name, .len = len };
        return add_upval(cc, uv);
    }
    return -1;
}

static void emit_get_named(Compiler *cc, const char *name, uint32_t len, uint32_t L)
{
    int li = resolve_local(cc, name, len);
    if (li >= 0) {
        if (cc->locals[li].kind == LK_SLOT) {
            chunk_emit(cc->chunk, OP_GET_LOCAL, L);
            chunk_emit(cc->chunk, cc->locals[li].slot, L);
        } else emit_name_op_g(cc->chunk, OP_GET_VAR, name, len, L);
        return;
    }
    int ui = resolve_upvalue(cc, name, len);
    if (ui >= 0) { chunk_emit(cc->chunk, OP_GET_UPVALUE, L); chunk_emit(cc->chunk, (uint8_t)ui, L); return; }
    emit_name_op_g(cc->chunk, OP_GET_VAR, name, len, L);     /* global */
}

static void emit_set_named(Compiler *cc, const char *name, uint32_t len, uint32_t L)
{
    int li = resolve_local(cc, name, len);
    if (li >= 0) {
        if (cc->locals[li].kind == LK_SLOT) {
            chunk_emit(cc->chunk, OP_SET_LOCAL, L);
            chunk_emit(cc->chunk, cc->locals[li].slot, L);
        } else emit_name_op_g(cc->chunk, OP_SET_VAR, name, len, L);
        return;
    }
    int ui = resolve_upvalue(cc, name, len);
    if (ui >= 0) { chunk_emit(cc->chunk, OP_SET_UPVALUE, L); chunk_emit(cc->chunk, (uint8_t)ui, L); return; }
    emit_name_op_g(cc->chunk, OP_SET_VAR, name, len, L);     /* global */
}

static void declare_slot(Compiler *cc, const char *name, uint32_t len)
{
    cc->locals[cc->nlocals++] = (CLocal){ name, len, LK_SLOT, (uint8_t)cc->nslots, cc->depth };
    cc->nslots++;
}
static void declare_env_local(Compiler *cc, const char *name, uint32_t len)
{
    cc->locals[cc->nlocals++] = (CLocal){ name, len, LK_ENV, 0, cc->depth };
}
static void begin_scope(Compiler *cc) { cc->depth++; }
static void end_scope(Compiler *cc)
{
    cc->depth--;
    while (cc->nlocals > 0 && cc->locals[cc->nlocals - 1].depth > cc->depth) cc->nlocals--;
}

/* ---- emit helpers -------------------------------------------------------- */

static uint32_t emit_jump(Chunk *c, uint8_t op, uint32_t line)
{
    chunk_emit(c, op, line);
    chunk_emit_u16(c, 0xffff, line);
    return c->code_len - 2;
}
static void patch_jump(Interp *I, AstNode *n, Chunk *c, uint32_t at)
{
    uint32_t off = c->code_len - (at + 2);
    if (off > 0xffff) compile_error(I, n, "branch too large for the VM");
    c->code[at] = (uint8_t)(off & 0xff); c->code[at + 1] = (uint8_t)(off >> 8);
}
static void emit_loop(Interp *I, AstNode *n, Chunk *c, uint32_t loop_start, uint32_t line)
{
    chunk_emit(c, OP_LOOP, line);
    uint32_t off = c->code_len + 2 - loop_start;
    if (off > 0xffff) compile_error(I, n, "loop body too large for the VM");
    chunk_emit_u16(c, (uint16_t)off, line);
}
static void emit_const(Chunk *c, Value v, uint32_t line)
{
    uint32_t idx = chunk_add_const(c, v);
    chunk_emit(c, OP_CONST, line);
    chunk_emit_u16(c, (uint16_t)idx, line);
}
static void emit_name_op_g(Chunk *c, uint8_t op, const char *s, uint32_t len, uint32_t line)
{
    uint32_t idx = chunk_add_name(c, s, len);
    chunk_emit(c, op, line);
    chunk_emit_u16(c, (uint16_t)idx, line);
}

/* ---- the walk ------------------------------------------------------------ */

/* Does this index argument reference 'end'? Stops at a nested index (whose
 * 'end' belongs to it). Conservative: unusual node types return true so the
 * end-context is always available when in doubt. */
static bool node_has_end(AstNode *n)
{
    if (!n) return false;
    switch (n->kind) {
    case AST_END:   return true;
    case AST_INDEX: return false;
    case AST_INT: case AST_FLOAT: case AST_IMAG: case AST_STRING:
    case AST_BOOL: case AST_NULL: case AST_IDENT: case AST_AT: case AST_COLON:
        return false;
    case AST_BINARY: case AST_ASSIGN:
        return node_has_end(n->as.binary.lhs) || node_has_end(n->as.binary.rhs);
    case AST_UNARY: case AST_POSTFIX:
        return node_has_end(n->as.unary.operand);
    case AST_RANGE:
        return node_has_end(n->as.range.start) || node_has_end(n->as.range.step)
            || node_has_end(n->as.range.stop);
    case AST_FIELD:
        return node_has_end(n->as.field.target);
    case AST_CALL:
        if (node_has_end(n->as.call.callee)) return true;
        for (uint32_t i = 0; i < n->as.call.args.count; i++)
            if (node_has_end(n->as.call.args.items[i])) return true;
        return false;
    case AST_MATRIX:
        for (uint32_t r = 0; r < n->as.list.count; r++) {
            AstNode *row = n->as.list.items[r];
            for (uint32_t k = 0; k < row->as.list.count; k++)
                if (node_has_end(row->as.list.items[k])) return true;
        }
        return false;
    case AST_IF:
        return node_has_end(n->as.iff.cond) || node_has_end(n->as.iff.then_e)
            || node_has_end(n->as.iff.else_e);
    default:                                        /* lambda/record/let/block/loops: rare here */
        return true;
    }
}

/* Does this subtree reference '@' at its own pipe level? Used to decide whether
 * `x |> rhs` substitutes @ (rhs uses @) or applies a bare callable (rhs(x)).
 * Descends into lambdas (a lambda body can capture the enclosing @) but not into
 * a nested |>'s right side, which rebinds @ for itself. */
static bool contains_at(const AstNode *n)
{
    if (!n) return false;
    switch (n->kind) {
    case AST_AT: return true;
    case AST_UNARY: case AST_POSTFIX: case AST_RETURN: return contains_at(n->as.unary.operand);
    case AST_BINARY:
        if (n->as.binary.op == TOK_PIPE_GT) return contains_at(n->as.binary.lhs);  /* rhs rebinds @ */
        return contains_at(n->as.binary.lhs) || contains_at(n->as.binary.rhs);
    case AST_ASSIGN: return contains_at(n->as.binary.rhs);
    case AST_RANGE:
        return contains_at(n->as.range.start) || contains_at(n->as.range.step) || contains_at(n->as.range.stop);
    case AST_CALL: case AST_INDEX:
        if (contains_at(n->as.call.callee)) return true;
        for (uint32_t i = 0; i < n->as.call.args.count; i++) if (contains_at(n->as.call.args.items[i])) return true;
        return false;
    case AST_FIELD: return contains_at(n->as.field.target);
    case AST_IF:
        return contains_at(n->as.iff.cond) || contains_at(n->as.iff.then_e) || contains_at(n->as.iff.else_e);
    case AST_RECORD: case AST_MATRIX: case AST_ROW: case AST_BLOCK: case AST_BLOCK_EXPR:
        for (uint32_t i = 0; i < n->as.list.count; i++) if (contains_at(n->as.list.items[i])) return true;
        return false;
    case AST_RECORD_FIELD: return contains_at(n->as.recfield.value);
    case AST_LET: return contains_at(n->as.let.value) || contains_at(n->as.let.body);
    case AST_LAMBDA: return contains_at(n->as.lambda.body);
    case AST_FOR: return contains_at(n->as.forloop.iter) || contains_at(n->as.forloop.body);
    case AST_WHILE: return contains_at(n->as.whileloop.cond) || contains_at(n->as.whileloop.body);
    default: return false;
    }
}

static void compile_node(Interp *I, Compiler *cc, AstNode *n)
{
    Chunk *c = cc->chunk;
    uint32_t L = n->line;
    switch (n->kind) {
    case AST_INT:    emit_const(c, val_int(parse_int_lit(n->as.lit.text, n->as.lit.len)), L); break;
    case AST_FLOAT:  emit_const(c, val_float(parse_float_lit(n->as.lit.text, n->as.lit.len)), L); break;
    case AST_IMAG:   emit_const(c, val_complex(0.0, parse_float_lit(n->as.lit.text, n->as.lit.len - 1)), L); break;
    case AST_STRING: emit_const(c, decode_string(n->as.lit.text, n->as.lit.len), L); break;
    case AST_BOOL:   emit_const(c, val_bool(n->as.boolean), L); break;
    case AST_NULL:   chunk_emit(c, OP_NULL, L); break;

    case AST_IDENT:  emit_get_named(cc, n->as.lit.text, n->as.lit.len, L); break;
    case AST_AT:     chunk_emit(c, OP_GET_AT, L); break;

    case AST_UNARY:
        compile_node(I, cc, n->as.unary.operand);
        chunk_emit(c, OP_UNARY, L); chunk_emit(c, (uint8_t)n->as.unary.op, L);
        break;
    case AST_POSTFIX:
        compile_node(I, cc, n->as.unary.operand);
        chunk_emit(c, OP_TRANSPOSE, L); chunk_emit(c, n->as.unary.op == TOK_CTRANSPOSE ? 1 : 0, L);
        break;

    case AST_BINARY: {
        enum TokenKind op = n->as.binary.op;
        if (op == TOK_AND || op == TOK_OR) {
            compile_node(I, cc, n->as.binary.lhs);
            chunk_emit(c, OP_ASSERT_BOOL, L);
            uint32_t sc = emit_jump(c, op == TOK_AND ? OP_JUMP_IF_FALSE : OP_JUMP_IF_TRUE, L);
            chunk_emit(c, OP_POP, L);
            compile_node(I, cc, n->as.binary.rhs);
            chunk_emit(c, OP_ASSERT_BOOL, L);
            patch_jump(I, n, c, sc);
        } else if (op == TOK_PIPE_GT) {
            if (!contains_at(n->as.binary.rhs)) {       /* bare callable: x |> f  ==>  f(x) */
                compile_node(I, cc, n->as.binary.rhs);
                compile_node(I, cc, n->as.binary.lhs);
                chunk_emit(c, OP_CALL, L); chunk_emit(c, 1, L);
            } else {
                compile_node(I, cc, n->as.binary.lhs);
                chunk_emit(c, OP_SCOPE_PUSH, L); cc->scope_count++;
                emit_name_op_g(c, OP_DEFINE, "@", 1, L);
                chunk_emit(c, OP_POP, L);
                compile_node(I, cc, n->as.binary.rhs);
                chunk_emit(c, OP_SCOPE_POP, L); cc->scope_count--;
            }
        } else {
            compile_node(I, cc, n->as.binary.lhs);
            compile_node(I, cc, n->as.binary.rhs);
            chunk_emit(c, OP_BINOP, L); chunk_emit(c, (uint8_t)op, L);
        }
        break;
    }

    case AST_RANGE:
        compile_node(I, cc, n->as.range.start);
        if (n->as.range.step) compile_node(I, cc, n->as.range.step);
        else emit_const(c, val_int(1), L);
        compile_node(I, cc, n->as.range.stop);
        chunk_emit(c, OP_RANGE, L);
        break;

    case AST_CALL: {
        uint32_t argc = n->as.call.args.count;
        compile_node(I, cc, n->as.call.callee);
        for (uint32_t i = 0; i < argc; i++) compile_node(I, cc, n->as.call.args.items[i]);
        chunk_emit(c, OP_CALL, L); chunk_emit(c, (uint8_t)argc, L);
        break;
    }

    case AST_FIELD:
        compile_node(I, cc, n->as.field.target);
        emit_name_op_g(c, OP_FIELD, n->as.field.name, n->as.field.namelen, L);
        break;

    case AST_IF: {
        compile_node(I, cc, n->as.iff.cond);
        uint32_t else_j = emit_jump(c, OP_JUMP_IF_FALSE, L);
        chunk_emit(c, OP_POP, L);
        compile_node(I, cc, n->as.iff.then_e);
        uint32_t end_j = emit_jump(c, OP_JUMP, L);
        patch_jump(I, n, c, else_j);
        chunk_emit(c, OP_POP, L);
        if (n->as.iff.else_e) compile_node(I, cc, n->as.iff.else_e);
        else chunk_emit(c, OP_NULL, L);
        patch_jump(I, n, c, end_j);
        break;
    }

    case AST_LET:
        if (n->as.let.body) {                         /* let x = v in body  (expression) */
            chunk_emit(c, OP_SCOPE_PUSH, L);
            begin_scope(cc); cc->scope_count++;
            if (n->as.let.value->kind == AST_LAMBDA)
                compile_lambda(I, cc, n->as.let.value, n->as.let.name, n->as.let.namelen);
            else
                compile_node(I, cc, n->as.let.value);
            emit_name_op_g(c, OP_DEFINE, n->as.let.name, n->as.let.namelen, L);
            chunk_emit(c, OP_POP, L);                  /* binding holds it; clear the stack copy */
            declare_env_local(cc, n->as.let.name, n->as.let.namelen);
            compile_node(I, cc, n->as.let.body);       /* leaves the body's value on the stack */
            end_scope(cc); cc->scope_count--;
            chunk_emit(c, OP_SCOPE_POP, L);            /* drops the binding; value stays on stack */
            break;
        }
        if (n->as.let.value->kind == AST_LAMBDA)
            compile_lambda(I, cc, n->as.let.value, n->as.let.name, n->as.let.namelen);
        else
            compile_node(I, cc, n->as.let.value);
        emit_name_op_g(c, OP_DEFINE, n->as.let.name, n->as.let.namelen, L);
        if (cc->depth > 0) declare_env_local(cc, n->as.let.name, n->as.let.namelen);
        break;

    case AST_ASSIGN: {
        AstNode *tgt = n->as.binary.lhs;
        if (tgt->kind == AST_INDEX) {
            AstNode *name = tgt->as.call.callee;          /* parser guarantees AST_IDENT */
            uint32_t argc = tgt->as.call.args.count;
            if (argc != 1 && argc != 2) compile_error(I, tgt, "arrays take 1 or 2 indices");
            compile_node(I, cc, name);                    /* load the array */
            bool uses_end = false;
            for (uint32_t k = 0; k < argc; k++)
                if (node_has_end(tgt->as.call.args.items[k])) { uses_end = true; break; }
            if (uses_end) { chunk_emit(c, OP_END_PUSH, L); chunk_emit(c, (uint8_t)argc, L); }
            uint8_t colonmask = 0;
            int saved_dim = cc->index_dim;
            for (uint32_t k = 0; k < argc; k++) {
                AstNode *arg = tgt->as.call.args.items[k];
                if (arg->kind == AST_COLON) { colonmask |= (uint8_t)(1u << k); chunk_emit(c, OP_NULL, L); }
                else { cc->index_dim = (int)k; compile_node(I, cc, arg); }
            }
            cc->index_dim = saved_dim;
            if (uses_end) chunk_emit(c, OP_END_POP, L);
            compile_node(I, cc, n->as.binary.rhs);        /* the value */
            chunk_emit(c, OP_INDEX_SET, L); chunk_emit(c, (uint8_t)argc, L); chunk_emit(c, colonmask, L);
            emit_set_named(cc, name->as.lit.text, name->as.lit.len, L);  /* rebind the name */
        } else {
            compile_node(I, cc, n->as.binary.rhs);
            emit_set_named(cc, tgt->as.lit.text, tgt->as.lit.len, L);
        }
        break;
    }

    case AST_BLOCK:
        for (uint32_t i = 0; i < n->as.list.count; i++) {
            if (i) chunk_emit(c, OP_POP, L);
            compile_node(I, cc, n->as.list.items[i]);
        }
        if (n->as.list.count == 0) chunk_emit(c, OP_NULL, L);
        break;

    case AST_BLOCK_EXPR:                               /* scoped block as an expression */
        chunk_emit(c, OP_SCOPE_PUSH, L);
        begin_scope(cc); cc->scope_count++;
        for (uint32_t i = 0; i < n->as.list.count; i++) {
            if (i) chunk_emit(c, OP_POP, L);
            compile_node(I, cc, n->as.list.items[i]);
        }
        if (n->as.list.count == 0) chunk_emit(c, OP_NULL, L);
        end_scope(cc); cc->scope_count--;
        chunk_emit(c, OP_SCOPE_POP, L);                /* drops the block's locals; value stays */
        break;

    case AST_WHILE: {
        if (cc->nloops >= 8) compile_error(I, n, "loops nested too deeply");
        chunk_emit(c, OP_MARK_PUSH, L);                 /* record the body's stack base */
        uint32_t loop_start = c->code_len;
        compile_node(I, cc, n->as.whileloop.cond);
        uint32_t exit = emit_jump(c, OP_JUMP_IF_FALSE, L);
        chunk_emit(c, OP_POP, L);
        int li = cc->nloops++;
        cc->loops[li].scope_base = cc->scope_count;     /* unwind target for break/continue */
        cc->loops[li].cont_target = loop_start;         /* continue re-evaluates the condition */
        cc->loops[li].is_for = false;
        cc->loops[li].nbreaks = 0;
        chunk_emit(c, OP_SCOPE_PUSH, L);
        begin_scope(cc); cc->scope_count++;
        compile_node(I, cc, n->as.whileloop.body);
        end_scope(cc); cc->scope_count--;
        chunk_emit(c, OP_POP, L);
        chunk_emit(c, OP_SCOPE_POP, L);
        emit_loop(I, n, c, loop_start, L);
        patch_jump(I, n, c, exit);
        chunk_emit(c, OP_POP, L);                        /* drop the false condition */
        for (int b = 0; b < cc->loops[li].nbreaks; b++)  /* break lands on the MARK_POP below */
            patch_jump(I, n, c, cc->loops[li].breaks[b]);
        cc->nloops--;
        chunk_emit(c, OP_MARK_POP, L);
        chunk_emit(c, OP_NULL, L);
        break;
    }

    case AST_FOR: {
        if (cc->nloops >= 8) compile_error(I, n, "loops nested too deeply");
        compile_node(I, cc, n->as.forloop.iter);
        chunk_emit(c, OP_FOR_BEGIN, L);
        chunk_emit(c, OP_MARK_PUSH, L);                 /* base above [iterable, index] */
        uint32_t loop_start = c->code_len;
        chunk_emit(c, OP_FOR_NEXT, L);
        uint32_t name = chunk_add_name(c, n->as.forloop.var, n->as.forloop.varlen);
        chunk_emit_u16(c, (uint16_t)name, L);
        uint32_t exit = c->code_len; chunk_emit_u16(c, 0xffff, L);
        int li = cc->nloops++;
        cc->loops[li].scope_base = cc->scope_count;     /* before FOR_NEXT's per-iteration scope */
        cc->loops[li].cont_target = loop_start;         /* continue goes to FOR_NEXT */
        cc->loops[li].is_for = true;
        cc->loops[li].nbreaks = 0;
        begin_scope(cc); cc->scope_count++;             /* FOR_NEXT pushes the body scope at runtime */
        declare_env_local(cc, n->as.forloop.var, n->as.forloop.varlen);
        compile_node(I, cc, n->as.forloop.body);
        end_scope(cc); cc->scope_count--;
        chunk_emit(c, OP_POP, L);
        chunk_emit(c, OP_SCOPE_POP, L);
        emit_loop(I, n, c, loop_start, L);
        patch_jump(I, n, c, exit);
        for (int b = 0; b < cc->loops[li].nbreaks; b++)  /* break lands on the MARK_POP below */
            patch_jump(I, n, c, cc->loops[li].breaks[b]);
        cc->nloops--;
        chunk_emit(c, OP_MARK_POP, L);
        chunk_emit(c, OP_FOR_END, L);
        chunk_emit(c, OP_NULL, L);
        break;
    }

    case AST_BREAK: {
        if (cc->nloops == 0) compile_error(I, n, "'break' is only valid inside a loop");
        int li = cc->nloops - 1;
        chunk_emit(c, OP_MARK_RESET, L);                /* drop mid-expression temporaries */
        for (int s = cc->loops[li].scope_base; s < cc->scope_count; s++) chunk_emit(c, OP_SCOPE_POP, L);
        if (cc->loops[li].nbreaks >= 32) compile_error(I, n, "too many 'break' statements in one loop");
        cc->loops[li].breaks[cc->loops[li].nbreaks++] = emit_jump(c, OP_JUMP, L);
        break;
    }

    case AST_CONTINUE: {
        if (cc->nloops == 0) compile_error(I, n, "'continue' is only valid inside a loop");
        int li = cc->nloops - 1;
        chunk_emit(c, OP_MARK_RESET, L);                /* drop mid-expression temporaries */
        for (int s = cc->loops[li].scope_base; s < cc->scope_count; s++) chunk_emit(c, OP_SCOPE_POP, L);
        emit_loop(I, n, c, cc->loops[li].cont_target, L);
        break;
    }

    case AST_RETURN:
        if (!cc->in_function) compile_error(I, n, "'return' is only valid inside a function");
        if (n->as.unary.operand) compile_node(I, cc, n->as.unary.operand);
        else chunk_emit(c, OP_NULL, L);
        chunk_emit(c, OP_RETURN, L);
        break;

    case AST_LAMBDA: compile_lambda(I, cc, n, "", 0); break;

    case AST_MATRIX: {
        uint32_t nrows = n->as.list.count;
        Value shape = val_array(ELT_INT, 1, nrows);
        int64_t *sc = (int64_t *)as_arr(shape)->data;
        for (uint32_t ri = 0; ri < nrows; ri++) {
            AstNode *row = n->as.list.items[ri];
            sc[ri] = row->as.list.count;
            for (uint32_t ci = 0; ci < row->as.list.count; ci++)
                compile_node(I, cc, row->as.list.items[ci]);
        }
        uint32_t k = chunk_add_const(c, shape);
        chunk_emit(c, OP_MATRIX, L); chunk_emit_u16(c, (uint16_t)k, L);
        break;
    }

    case AST_INDEX: {
        uint32_t argc = n->as.call.args.count;
        if (argc != 1 && argc != 2) compile_error(I, n, "arrays take 1 or 2 indices");
        compile_node(I, cc, n->as.call.callee);
        bool uses_end = false;
        for (uint32_t k = 0; k < argc; k++)
            if (node_has_end(n->as.call.args.items[k])) { uses_end = true; break; }
        if (uses_end) { chunk_emit(c, OP_END_PUSH, L); chunk_emit(c, (uint8_t)argc, L); }
        uint8_t colonmask = 0;
        int saved_dim = cc->index_dim;
        for (uint32_t k = 0; k < argc; k++) {
            AstNode *arg = n->as.call.args.items[k];
            if (arg->kind == AST_COLON) { colonmask |= (uint8_t)(1u << k); chunk_emit(c, OP_NULL, L); }
            else { cc->index_dim = (int)k; compile_node(I, cc, arg); }
        }
        cc->index_dim = saved_dim;
        if (uses_end) chunk_emit(c, OP_END_POP, L);
        chunk_emit(c, OP_INDEX, L); chunk_emit(c, (uint8_t)argc, L); chunk_emit(c, colonmask, L);
        break;
    }

    case AST_COLON: compile_error(I, n, "':' is only valid inside an index, e.g. a[:, 2]");

    case AST_END:
        if (cc->index_dim < 0) compile_error(I, n, "'end' is only valid inside an index");
        chunk_emit(c, OP_END_GET, L); chunk_emit(c, (uint8_t)cc->index_dim, L);
        break;

    case AST_RECORD: {
        uint32_t cnt = n->as.list.count;
        for (uint32_t k = 0; k < cnt; k++)
            compile_node(I, cc, n->as.list.items[k]->as.recfield.value);
        chunk_emit(c, OP_RECORD, L); chunk_emit_u16(c, (uint16_t)cnt, L);
        for (uint32_t k = 0; k < cnt; k++) {
            AstNode *f = n->as.list.items[k];
            uint32_t ni = chunk_add_name(c, f->as.recfield.name, f->as.recfield.namelen);
            chunk_emit_u16(c, (uint16_t)ni, L);
        }
        break;
    }

    case AST_ROW:
    case AST_RECORD_FIELD: compile_error(I, n, "VM: malformed AST node");
    }
}

/* Compile a lambda into a proto chunk, then in the enclosing chunk push the
 * captured upvalue values (resolved in the enclosing frame) and OP_CLOSURE. */
static void compile_lambda(Interp *I, Compiler *cc, AstNode *n,
                           const char *selfname, uint32_t selflen)
{
    Chunk *proto = malloc(sizeof *proto);
    if (!proto) abort();
    chunk_init(proto);
    proto->src = n->as.lambda.src; proto->srclen = n->as.lambda.srclen;
    uint32_t np = n->as.lambda.params.count;
    proto->nparams = np;                                  /* arity only; params are slots */
    uint32_t idx = chunk_add_proto(cc->chunk, proto);     /* enclosing owns proto */

    Compiler fc = (Compiler){ .enclosing = cc, .chunk = proto, .depth = 1, .index_dim = -1, .in_function = true };
    declare_slot(&fc, selfname, selflen);                 /* slot 0 = self (the closure) */
    for (uint32_t i = 0; i < np; i++) {
        AstNode *p = n->as.lambda.params.items[i];
        declare_slot(&fc, p->as.lit.text, p->as.lit.len); /* slots 1..np = params */
    }
    compile_node(I, &fc, n->as.lambda.body);              /* body leaves one value */

    for (int i = 0; i < fc.nupvals; i++) {                /* push captures, in order */
        CUpval *uv = &fc.upvals[i];
        if (uv->from_local && uv->is_slot) {
            chunk_emit(cc->chunk, OP_GET_LOCAL, n->line); chunk_emit(cc->chunk, uv->slot, n->line);
        } else if (uv->from_local) {
            emit_name_op_g(cc->chunk, OP_GET_VAR, uv->name, uv->len, n->line);
        } else {
            chunk_emit(cc->chunk, OP_GET_UPVALUE, n->line); chunk_emit(cc->chunk, uv->up_index, n->line);
        }
    }
    chunk_emit(cc->chunk, OP_CLOSURE, n->line);
    chunk_emit_u16(cc->chunk, (uint16_t)idx, n->line);
    chunk_emit(cc->chunk, (uint8_t)fc.nupvals, n->line);
}

bool vm_compile(Interp *I, AstNode *stmt, Chunk *out)
{
    chunk_init(out);
    /* Save and restore the caller's unwind target on every exit. Without
     * this, I->jmp is left pointing at this (returned) frame; any later
     * longjmp — e.g. load() re-raising after an inner vm_eval_program —
     * jumps into a dead frame. Found by the first builtin to raise after
     * running a nested program. */
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp)) {
        chunk_free(out);
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        return false;
    }
    Compiler cc = (Compiler){ .enclosing = nullptr, .chunk = out, .depth = 0, .index_dim = -1 };
    compile_node(I, &cc, stmt);
    if (stmt->kind == AST_IDENT)        /* bare 'who' / 'rand' / a 0-arg fn name: call it */
        chunk_emit(out, OP_AUTOCALL, stmt->line);
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    return true;
}
