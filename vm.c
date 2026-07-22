/* vm.c — Neutrino bytecode VM (call-frame stack, lexical addressing).
 *
 * One operand stack and one CallFrame stack per vm_run invocation, with a single
 * setjmp at the top. A function's parameters and self live in stack slots, so a
 * direct call (OP_CALL on a compiled closure) pushes a frame over the callee and
 * its arguments already on the stack — no per-call environment, malloc, setjmp,
 * or C recursion. Slot 0 of a frame is the closure itself (the self-slot, for
 * direct recursion); slots 1..nparams are the arguments. Free variables are
 * value-snapshot upvalues carried on the closure, so a closure never points back
 * at the scope that created it. Globals and loop-body `let`s remain name-keyed in
 * the env (which every frame roots at the session globals), preserving late
 * binding for recursion and mutual recursion.
 *
 * Builtins that invoke a closure (e.g. map) go through call_value ->
 * vm_run_closure, which nests a fresh vm_run (saving/restoring I->jmp); that is
 * the only place C recursion happens, and it is shallow.
 *
 * Error handling stays total: on runtime_error the handler sweeps the whole
 * operand stack (one contiguous live-set across all frames) and unwinds every
 * frame's open loop scopes back to globals. The discipline that makes the sweep
 * complete: a fallible op PEEKs its operands (they stay on the stack), calls a
 * non-consuming runtime helper, and only on success pops them and pushes a result.
 */
#include "vm.h"
#include "chunk.h"
#include "compile.h"
#include "nrt.h"
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

typedef struct {
    Chunk   *chunk;
    uint32_t ip;
    uint32_t slot_base;  /* slot 0 = closure (self); slots 1.. = params; then temps */
    uint32_t mark_base;  /* marks stack height at frame entry (restored on return)   */
    CloObj  *closure;    /* the executing closure (for upvalues); null at top level  */
    EnvObj  *env;        /* loop/global scope; starts = globals, loops push children */
} CallFrame;

typedef struct {
    Value     *stack;  uint32_t sp, cap;
    CallFrame *frames; uint32_t nframe, fcap;
    EnvObj    *globals;  /* floor for scope unwind; never released by a frame */
    int64_t   *end_dims; uint32_t end_sp, end_cap;  /* 'end' context: 2 dims per frame */
    uint32_t  *marks;  uint32_t nmark, mcap;        /* loop-body sp bases (break/continue reset) */
} VM;

#define VM_MAX_FRAMES 200000   /* direct recursion: frame-stack depth   */
#define VM_MAX_DEPTH  1800     /* builtin->closure nesting: C-stack depth */
static int vm_depth = 0;

/* Chunks that define closures are retained for the session (an escaping
 * closure's proto lives inside its statement chunk). Freed by vm_session_end. */
static Chunk  **g_kept;
static uint32_t g_nkept, g_ckept;

static void keep_chunk(Chunk *c)
{
    if (g_nkept == g_ckept) {
        g_ckept = g_ckept ? g_ckept * 2 : 16;
        g_kept = realloc(g_kept, g_ckept * sizeof *g_kept);
        if (!g_kept) abort();
    }
    g_kept[g_nkept++] = c;
}

void vm_session_end(void)
{
    for (uint32_t i = 0; i < g_nkept; i++) { chunk_free(g_kept[i]); free(g_kept[i]); }
    free(g_kept); g_kept = nullptr; g_nkept = g_ckept = 0;
}

static void stack_grow(VM *st)
{
    st->cap = st->cap ? st->cap * 2 : 256;
    st->stack = realloc(st->stack, st->cap * sizeof *st->stack);
    if (!st->stack) abort();
}

/* Each frame owns exactly one ref to fr->env unless it is globals (borrowed). */
static void scope_push(VM *st, CallFrame *fr)
{
    EnvObj *child = env_new(fr->env);
    if (fr->env != st->globals) env_release(fr->env);
    fr->env = child;
}
static void scope_pop(VM *st, CallFrame *fr)
{
    EnvObj *parent = fr->env->parent;
    if (parent != st->globals) env_retain(parent);
    env_release(fr->env);
    fr->env = parent;
}

static bool vm_is_num(Value v)
{ return v.kind == VAL_INT || v.kind == VAL_FLOAT || v.kind == VAL_COMPLEX; }

static CallFrame *push_frame(VM *st, Chunk *chunk, CloObj *closure, uint32_t slot_base)
{
    if (st->nframe == st->fcap) {
        st->fcap = st->fcap ? st->fcap * 2 : 64;
        st->frames = realloc(st->frames, st->fcap * sizeof *st->frames);
        if (!st->frames) abort();
    }
    CallFrame *fr = &st->frames[st->nframe++];
    fr->chunk = chunk; fr->ip = 0; fr->slot_base = slot_base;
    fr->mark_base = st->nmark;
    fr->closure = closure; fr->env = st->globals;
    return fr;
}

/* closure: VAL_CLOSURE -> run it with callee at slot 0 and args at slots 1..argc;
 * VAL_NULL -> top-level statement chunk (no slots). */
static Value vm_run(Interp *I, Chunk *ch, Value closure, Value *args, uint32_t argc, bool *ok)
{
    if (vm_depth >= VM_MAX_DEPTH) runtime_error(I, "maximum call depth exceeded");
    vm_depth++;
    jmp_buf saved;
    memcpy(saved, I->jmp, sizeof(jmp_buf));

    VM *st = malloc(sizeof *st);
    st->stack = nullptr; st->sp = 0; st->cap = 0;
    st->frames = nullptr; st->nframe = 0; st->fcap = 0;
    st->globals = I->globals;
    st->end_dims = nullptr; st->end_sp = 0; st->end_cap = 0;
    st->marks = nullptr; st->nmark = 0; st->mcap = 0;
    stack_grow(st);

    CloObj *clo = closure.kind == VAL_CLOSURE ? as_clo(closure) : nullptr;
    if (clo) {                                          /* set up initial slots */
        st->stack[st->sp++] = value_retain(closure);    /* slot 0 = self        */
        for (uint32_t i = 0; i < argc; i++) {
            if (st->sp >= st->cap) stack_grow(st);
            st->stack[st->sp++] = value_retain(args[i]); /* slots 1..argc        */
        }
    }
    push_frame(st, ch, clo, /*slot_base=*/0);

    if (setjmp(I->jmp)) {                                /* runtime_error landed here */
        for (uint32_t k = 0; k < st->sp; k++) value_release(st->stack[k]);
        for (uint32_t f = 0; f < st->nframe; f++) {
            CallFrame *fr = &st->frames[f];
            while (fr->env != st->globals) scope_pop(st, fr);
        }
        free(st->stack); free(st->frames); free(st->end_dims); free(st->marks); free(st);
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        vm_depth--;
        *ok = false;
        return val_null();
    }

    CallFrame *fr = &st->frames[st->nframe - 1];

#define PUSH(v)   do { if (st->sp >= st->cap) stack_grow(st); st->stack[st->sp++] = (v); } while (0)
#define PEEK(d)   (st->stack[st->sp - 1 - (d)])
#define RD_U8()   (fr->chunk->code[fr->ip++])
#define RD_U16()  (fr->ip += 2, (uint16_t)(fr->chunk->code[fr->ip - 2] | (fr->chunk->code[fr->ip - 1] << 8)))

    for (;;) {
        if (fr->ip >= fr->chunk->code_len) {            /* end of chunk == return */
            Value result = st->sp > fr->slot_base ? st->stack[--st->sp] : val_null();
            while (fr->env != st->globals) scope_pop(st, fr);
            while (st->sp > fr->slot_base) value_release(st->stack[--st->sp]);
            st->nmark = fr->mark_base;                  /* discard this frame's loop marks */
            st->nframe--;
            if (st->nframe == 0) {
                free(st->stack); free(st->frames); free(st->end_dims); free(st->marks); free(st);
                memcpy(I->jmp, saved, sizeof(jmp_buf));
                vm_depth--;
                *ok = true;
                return result;
            }
            fr = &st->frames[st->nframe - 1];
            PUSH(result);
            continue;
        }

        Chunk *ch_ = fr->chunk;
        I->cur_line = ch_->lines[fr->ip];
        switch ((OpCode)ch_->code[fr->ip++]) {

        case OP_CONST: { uint16_t k = RD_U16(); PUSH(value_retain(ch_->consts[k])); break; }
        case OP_NULL:  PUSH(val_null()); break;

        case OP_GET_LOCAL: { uint8_t s = RD_U8(); PUSH(value_retain(st->stack[fr->slot_base + s])); break; }
        case OP_SET_LOCAL: {
            uint8_t s = RD_U8(); uint32_t idx = fr->slot_base + s;
            value_release(st->stack[idx]); st->stack[idx] = value_retain(PEEK(0));
            break;
        }
        case OP_GET_UPVALUE: { uint8_t i = RD_U8(); PUSH(value_retain(fr->closure->upvalues[i])); break; }
        case OP_SET_UPVALUE: {
            uint8_t i = RD_U8();
            value_release(fr->closure->upvalues[i]); fr->closure->upvalues[i] = value_retain(PEEK(0));
            break;
        }

        case OP_GET_VAR: {
            uint16_t k = RD_U16();
            Value out;
            if (!env_lookup(fr->env, ch_->names[k], ch_->namelens[k], &out))
                runtime_error(I, "undefined name '%.*s'", (int)ch_->namelens[k], ch_->names[k]);
            PUSH(out);
            break;
        }
        case OP_DEFINE: {
            uint16_t k = RD_U16();
            env_define(fr->env, ch_->names[k], ch_->namelens[k], PEEK(0));
            break;
        }
        case OP_SET_VAR: {
            uint16_t k = RD_U16();
            if (!env_assign(fr->env, ch_->names[k], ch_->namelens[k], PEEK(0)))
                runtime_error(I, "assignment to undefined name '%.*s' (declare it with 'let' first)",
                              (int)ch_->namelens[k], ch_->names[k]);
            break;
        }
        case OP_GET_AT: {
            Value out;
            if (!env_lookup(fr->env, "@", 1, &out))
                runtime_error(I, "'@' is only meaningful on the right-hand side of '|>'");
            PUSH(out);
            break;
        }

        case OP_BINOP: {
            enum TokenKind op = (enum TokenKind)RD_U8();
            Value r = apply_binop(I, op, PEEK(1), PEEK(0));
            value_release(PEEK(0)); value_release(PEEK(1)); st->sp -= 2;
            PUSH(r);
            break;
        }
        case OP_UNARY: {
            enum TokenKind op = (enum TokenKind)RD_U8();
            Value r = apply_unary(I, op, PEEK(0));
            value_release(PEEK(0)); st->sp -= 1;
            PUSH(r);
            break;
        }
        case OP_TRANSPOSE: {
            bool conj = RD_U8() != 0;
            Value r = transpose(I, PEEK(0), conj);
            value_release(PEEK(0)); st->sp -= 1;
            PUSH(r);
            break;
        }
        case OP_RANGE: {
            Value r = make_range(I, PEEK(2), PEEK(0), PEEK(1));
            value_release(PEEK(0)); value_release(PEEK(1)); value_release(PEEK(2)); st->sp -= 3;
            PUSH(r);
            break;
        }

        case OP_CALL: {
            uint8_t argc = RD_U8();
            Value  callee = PEEK(argc);
            if (callee.kind == VAL_CLOSURE) {            /* direct call: push a frame */
                CloObj *cl = as_clo(callee);
                if (cl->chunk->nparams != argc)
                    runtime_error(I, "function expects %u argument(s), got %u", cl->chunk->nparams, argc);
                if (st->nframe >= VM_MAX_FRAMES) runtime_error(I, "call stack overflow");
                uint32_t sb = st->sp - argc - 1;          /* callee + args become slots */
                fr = push_frame(st, cl->chunk, cl, sb);
            } else {                                      /* builtin (or error): in place */
                Value *args2 = &st->stack[st->sp - argc];
                Value r = call_value(I, callee, args2, argc);
                for (uint32_t i = 0; i < argc; i++) value_release(args2[i]);
                value_release(callee);
                st->sp -= (uint32_t)argc + 1;
                PUSH(r);
            }
            break;
        }

        case OP_AUTOCALL: {                              /* bare callable name -> call with 0 args */
            Value callee = PEEK(0);
            if (callee.kind == VAL_CLOSURE && as_clo(callee)->chunk->nparams == 0) {
                if (st->nframe >= VM_MAX_FRAMES) runtime_error(I, "call stack overflow");
                fr = push_frame(st, as_clo(callee)->chunk, as_clo(callee), st->sp - 1);
            } else if (callee.kind == VAL_BUILTIN && as_blt(callee)->min_arity == 0) {
                Value r = call_value(I, callee, nullptr, 0);
                value_release(callee);
                st->sp -= 1;
                PUSH(r);
            }                                            /* otherwise leave the value untouched */
            break;
        }

        case OP_RETURN: {
            Value result = st->sp > fr->slot_base ? st->stack[--st->sp] : val_null();
            while (fr->env != st->globals) scope_pop(st, fr);
            while (st->sp > fr->slot_base) value_release(st->stack[--st->sp]);
            st->nmark = fr->mark_base;                  /* discard this frame's loop marks */
            st->nframe--;
            if (st->nframe == 0) {
                free(st->stack); free(st->frames); free(st->end_dims); free(st->marks); free(st);
                memcpy(I->jmp, saved, sizeof(jmp_buf));
                vm_depth--;
                *ok = true;
                return result;
            }
            fr = &st->frames[st->nframe - 1];
            PUSH(result);
            break;
        }

        case OP_FIELD: {
            uint16_t k = RD_U16();
            Value t = PEEK(0);
            if (t.kind != VAL_RECORD) runtime_error(I, "field access on a non-record value");
            RecObj *rec = as_rec(t);
            Value out; bool found = false;
            for (uint32_t i = 0; i < rec->count; i++)
                if (rec->keylens[i] == ch_->namelens[k] &&
                    memcmp(rec->keys[i], ch_->names[k], ch_->namelens[k]) == 0) {
                    out = value_retain(rec->vals[i]); found = true; break;
                }
            if (!found) runtime_error(I, "record has no field '%.*s'", (int)ch_->namelens[k], ch_->names[k]);
            value_release(t); st->sp -= 1;
            PUSH(out);
            break;
        }

        case OP_ASSERT_BOOL:
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "operand of '&&'/'||' must be Bool, got %s", value_type_name(PEEK(0)));
            break;

        case OP_JUMP: { uint16_t off = RD_U16(); fr->ip += off; break; }
        case OP_LOOP: { uint16_t off = RD_U16(); fr->ip -= off; break; }
        case OP_JUMP_IF_FALSE: {
            uint16_t off = RD_U16();
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "condition must be Bool, got %s", value_type_name(PEEK(0)));
            if (!PEEK(0).as.b) fr->ip += off;
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t off = RD_U16();
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "condition must be Bool, got %s", value_type_name(PEEK(0)));
            if (PEEK(0).as.b) fr->ip += off;
            break;
        }
        case OP_POP: st->sp -= 1; value_release(st->stack[st->sp]); break;
        case OP_TEE: {
            value_print(vout(), PEEK(0));
            fputc('\n', vout());
            break;
        }

        case OP_SCOPE_PUSH: scope_push(st, fr); break;
        case OP_SCOPE_POP:  scope_pop(st, fr);  break;

        case OP_FOR_BEGIN: {
            Value it = PEEK(0);
            if (it.kind != VAL_ARRAY && !vm_is_num(it))
                runtime_error(I, "for: can only iterate a number or array, got %s", value_type_name(it));
            PUSH(val_int(0));
            break;
        }
        case OP_FOR_NEXT: {
            uint16_t name = RD_U16();
            uint16_t endoff = RD_U16();
            Value iter = PEEK(1);
            int64_t idx = PEEK(0).as.i;
            int64_t count = iter.kind == VAL_ARRAY
                          ? (int64_t)as_arr(iter)->rows * as_arr(iter)->cols : 1;
            if (idx >= count) { fr->ip += endoff; break; }
            Value elem = iter.kind == VAL_ARRAY ? arr_get(as_arr(iter), (size_t)idx)
                                                : value_retain(iter);
            scope_push(st, fr);
            env_define(fr->env, ch_->names[name], ch_->namelens[name], elem);
            value_release(elem);
            PEEK(0) = val_int(idx + 1);
            break;
        }
        case OP_FOR_END:
            value_release(PEEK(1)); st->sp -= 2;
            break;

        case OP_MARK_PUSH:
            if (st->nmark == st->mcap) {
                st->mcap = st->mcap ? st->mcap * 2 : 16;
                st->marks = realloc(st->marks, st->mcap * sizeof *st->marks);
                if (!st->marks) abort();
            }
            st->marks[st->nmark++] = st->sp;
            break;
        case OP_MARK_POP:
            st->nmark--;
            break;
        case OP_MARK_RESET:                              /* break/continue: drop mid-expression temporaries */
            while (st->sp > st->marks[st->nmark - 1]) { st->sp--; value_release(st->stack[st->sp]); }
            break;

        case OP_CLOSURE: {
            uint16_t pidx = RD_U16();
            uint8_t  nup  = RD_U8();
            Value *ups = nup ? malloc(nup * sizeof *ups) : nullptr;
            for (uint8_t i = 0; i < nup; i++) ups[i] = st->stack[st->sp - nup + i];  /* move */
            st->sp -= nup;
            PUSH(val_closure_vm(ch_->protos[pidx], ups, nup));
            break;
        }

        case OP_MATRIX: {
            uint16_t k = RD_U16();
            ArrObj *shape = as_arr(ch_->consts[k]);
            uint32_t nrows = shape->rows * shape->cols;
            const int64_t *rc = (const int64_t *)shape->data;
            uint32_t ntot = 0;
            for (uint32_t r = 0; r < nrows; r++) ntot += (uint32_t)rc[r];
            Value *ev = &st->stack[st->sp - ntot];
            Value result = build_matrix(I, ev, nrows, rc);
            for (uint32_t r = 0; r < ntot; r++) value_release(ev[r]);
            st->sp -= ntot;
            PUSH(result);
            break;
        }
        case OP_INDEX: {
            uint8_t argc = RD_U8();
            uint8_t colonmask = RD_U8();
            Value  target = PEEK(argc);
            Value *idx    = &st->stack[st->sp - argc];
            Value result = do_index(I, target, idx, argc, colonmask);
            for (uint32_t k = 0; k < argc; k++) value_release(idx[k]);
            value_release(target);
            st->sp -= (uint32_t)argc + 1;
            PUSH(result);
            break;
        }

        case OP_END_PUSH: {
            uint8_t argc = RD_U8();
            Value tgt = PEEK(0);                       /* the just-compiled target */
            int64_t d0 = 1, d1 = 1;
            if (tgt.kind == VAL_ARRAY) {
                ArrObj *ta = as_arr(tgt);
                if (argc == 1) { d0 = d1 = (int64_t)ta->rows * ta->cols; }
                else           { d0 = ta->rows; d1 = ta->cols; }
            } else if (tgt.kind == VAL_STRING) {
                d0 = d1 = (int64_t)((StrObj *)tgt.as.obj)->len;   /* s[end] */
            }
            if (st->end_sp == st->end_cap) {
                st->end_cap = st->end_cap ? st->end_cap * 2 : 8;
                st->end_dims = realloc(st->end_dims, st->end_cap * 2 * sizeof *st->end_dims);
                if (!st->end_dims) abort();
            }
            st->end_dims[2 * st->end_sp]     = d0;
            st->end_dims[2 * st->end_sp + 1] = d1;
            st->end_sp++;
            break;
        }
        case OP_END_GET: {
            uint8_t dim = RD_U8();
            PUSH(val_int(st->end_dims[2 * (st->end_sp - 1) + dim]));
            break;
        }
        case OP_END_POP:
            st->end_sp--;
            break;

        case OP_INDEX_SET: {
            uint8_t argc = RD_U8();
            uint8_t colonmask = RD_U8();
            Value  value  = PEEK(0);                       /* top: the value */
            Value *idx    = &st->stack[st->sp - 1 - argc]; /* below it: the indices */
            Value  target = PEEK(argc + 1);                /* below those: the array */
            Value  result = do_index_set(I, target, idx, argc, colonmask, value);
            value_release(value);
            for (uint32_t k = 0; k < argc; k++) value_release(idx[k]);
            value_release(target);
            st->sp -= (uint32_t)argc + 2;
            PUSH(result);
            break;
        }
        case OP_RECORD: {
            uint16_t cnt = RD_U16();
            Value rec = val_record(cnt);
            RecObj *rrec = as_rec(rec);
            for (uint32_t k = 0; k < cnt; k++) {
                uint16_t ni = RD_U16();
                rrec->keys[k]    = ch_->names[ni];
                rrec->keylens[k] = ch_->namelens[ni];
            }
            Value *vals = &st->stack[st->sp - cnt];
            for (uint32_t k = 0; k < cnt; k++) rrec->vals[k] = vals[k];   /* move ownership */
            st->sp -= cnt;
            PUSH(rec);
            break;
        }
        }
    }

#undef PUSH
#undef PEEK
#undef RD_U8
#undef RD_U16
}

/* Builtin -> closure path: nests a fresh vm_run with the callee as slot 0. */
Value vm_run_closure(Interp *I, Value callee, Value *args, uint32_t n)
{
    CloObj *c = as_clo(callee);
    if (c->chunk->nparams != n)
        runtime_error(I, "function expects %u argument(s), got %u", c->chunk->nparams, n);
    bool ok;
    Value r = vm_run(I, c->chunk, callee, args, n, &ok);
    if (!ok) longjmp(I->jmp, 1);
    return r;
}

Value vm_eval_program(Interp *I, AstNode *block, EnvObj *globals, bool echo)
{
    I->globals   = globals;
    I->had_error = false;

    AstNode  *single = block;
    AstNode **items  = &single;
    uint32_t  count  = 1;
    if (block->kind == AST_BLOCK) { items = block->as.list.items; count = block->as.list.count; }

    Value last = val_null();
    for (uint32_t i = 0; i < count; i++) {
        AstNode *s = items[i];
        Chunk *ch = malloc(sizeof *ch);
        if (!vm_compile(I, s, ch)) { free(ch); value_release(last); return val_null(); }
        bool ok;
        Value r = vm_run(I, ch, val_null(), nullptr, 0, &ok);
        if (ch->nproto > 0) keep_chunk(ch);
        else { chunk_free(ch); free(ch); }
        if (!ok) { value_release(last); return val_null(); }
        value_release(last);
        last = r;
        if (echo && !s->silent && last.kind != VAL_NULL) {
            value_print(vout(), last);
            fputc('\n', vout());
            /* ans: the last value you saw and didn't name. Echo-coupled by
             * design — suppressed statements and load()ed scripts (echo off)
             * never touch it, so ans always matches the screen. */
            if (s->kind != AST_LET)
                env_define(globals, "ans", 3, last);
        }
    }
    return last;
}
