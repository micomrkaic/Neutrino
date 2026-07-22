/* chunk.c — bytecode chunk storage. */
#include "chunk.h"
#include <stdlib.h>

void chunk_init(Chunk *c) { *c = (Chunk){0}; }

void chunk_free(Chunk *c)
{
    for (uint32_t i = 0; i < c->nconst; i++) value_release(c->consts[i]);
    for (uint32_t i = 0; i < c->nproto; i++) { chunk_free(c->protos[i]); free(c->protos[i]); }
    free(c->code); free(c->lines); free(c->consts);
    free(c->names); free(c->namelens);
    free(c->protos); free(c->params); free(c->paramlens);
    *c = (Chunk){0};
}

uint32_t chunk_add_proto(Chunk *c, Chunk *proto)
{
    if (c->nproto == c->cproto) {
        c->cproto = c->cproto ? c->cproto * 2 : 4;
        c->protos = realloc(c->protos, c->cproto * sizeof *c->protos);
        if (!c->protos) abort();
    }
    c->protos[c->nproto] = proto;
    return c->nproto++;
}

void chunk_emit(Chunk *c, uint8_t b, uint32_t line)
{
    if (c->code_len == c->code_cap) {
        c->code_cap = c->code_cap ? c->code_cap * 2 : 64;
        c->code  = realloc(c->code,  c->code_cap);
        c->lines = realloc(c->lines, c->code_cap * sizeof *c->lines);
        if (!c->code || !c->lines) abort();
    }
    c->lines[c->code_len] = line;
    c->code[c->code_len++] = b;
}

void chunk_emit_u16(Chunk *c, uint16_t w, uint32_t line)
{
    chunk_emit(c, (uint8_t)(w & 0xff), line);
    chunk_emit(c, (uint8_t)(w >> 8),   line);
}

uint32_t chunk_add_const(Chunk *c, Value v)
{
    if (c->nconst == c->cconst) {
        c->cconst = c->cconst ? c->cconst * 2 : 8;
        c->consts = realloc(c->consts, c->cconst * sizeof *c->consts);
        if (!c->consts) abort();
    }
    c->consts[c->nconst] = v;
    return c->nconst++;
}

uint32_t chunk_add_name(Chunk *c, const char *s, uint32_t len)
{
    for (uint32_t i = 0; i < c->nname; i++)          /* dedupe identical names */
        if (c->namelens[i] == len && c->names[i] == s) return i;
    if (c->nname == c->cname) {
        c->cname = c->cname ? c->cname * 2 : 8;
        c->names    = realloc(c->names,    c->cname * sizeof *c->names);
        c->namelens = realloc(c->namelens, c->cname * sizeof *c->namelens);
        if (!c->names || !c->namelens) abort();
    }
    c->names[c->nname]    = s;
    c->namelens[c->nname] = len;
    return c->nname++;
}

/* ------------------------------------------------------------------ */
/* disassembler                                                        */
/* ------------------------------------------------------------------ */
#include "lexer.h"
#include <stdio.h>

/* Display symbol for the token kinds OP_BINOP / OP_UNARY carry. */
static const char *tok_sym(uint8_t t)
{
    switch ((enum TokenKind)t) {
    case TOK_PLUS: return "+";    case TOK_MINUS: return "-";
    case TOK_STAR: return "*";    case TOK_SLASH: return "/";
    case TOK_CARET: return "^";   case TOK_BACKSLASH: return "\\";
    case TOK_DOT_STAR: return ".*";   case TOK_DOT_SLASH: return "./";
    case TOK_DOT_CARET: return ".^";  case TOK_DOT_BACKSLASH: return ".\\";
    case TOK_EQ: return "==";  case TOK_NE: return "~=";
    case TOK_LT: return "<";   case TOK_LE: return "<=";
    case TOK_GT: return ">";   case TOK_GE: return ">=";
    case TOK_AMP: return "&";  case TOK_PIPE: return "|";
    case TOK_AND: return "&&"; case TOK_OR: return "||";
    case TOK_BANG: return "!"; case TOK_TILDE: return "~";
    case TOK_AT: return "@";
    default: return "?";
    }
}

static void dis_name(FILE *out, const Chunk *c, uint16_t idx)
{
    if (idx < c->nname) fprintf(out, "'%.*s'", (int)c->namelens[idx], c->names[idx]);
    else                fprintf(out, "name#%u?", idx);
}

static void dis_const(FILE *out, const Chunk *c, uint16_t idx)
{
    fprintf(out, "#%-3u ; ", idx);
    if (idx < c->nconst) value_print(out, c->consts[idx]);
    else                 fputs("??", out);
}

void chunk_disassemble(FILE *out, const Chunk *c, const char *title)
{
    fprintf(out, "== %s ==  (%u bytes, %u consts, %u names, %u protos", title,
            c->code_len, c->nconst, c->nname, c->nproto);
    if (c->nparams) {
        if (c->params) {                                 /* names kept (not the usual case) */
            fputs("; params:", out);
            for (uint32_t i = 0; i < c->nparams; i++)
                fprintf(out, " %.*s", (int)c->paramlens[i], c->params[i]);
        } else {
            fprintf(out, "; arity %u", c->nparams);      /* params live in slots 1..arity */
        }
    }
    fputs(")\n", out);

    uint32_t prev_line = 0;
    uint32_t i = 0;
    while (i < c->code_len) {
        uint32_t at = i;
        uint8_t  op = c->code[i++];
        uint32_t line = c->lines[at];
        fprintf(out, "%04u  ", at);
        if (line != prev_line) { fprintf(out, "%4u  ", line); prev_line = line; }
        else                   fputs("   |  ", out);

        #define U8()  (c->code[i++])
        #define U16() (i += 2, (uint16_t)(c->code[i-2] | (c->code[i-1] << 8)))
        switch ((OpCode)op) {
        case OP_CONST:      { uint16_t k = U16(); fputs("CONST          ", out); dis_const(out, c, k); break; }
        case OP_NULL:         fputs("NULL", out); break;
        case OP_GET_VAR:    { uint16_t k = U16(); fputs("GET_VAR        ", out); dis_name(out, c, k); break; }
        case OP_DEFINE:     { uint16_t k = U16(); fputs("DEFINE         ", out); dis_name(out, c, k); break; }
        case OP_SET_VAR:    { uint16_t k = U16(); fputs("SET_VAR        ", out); dis_name(out, c, k); break; }
        case OP_GET_AT:       fputs("GET_AT", out); break;
        case OP_BINOP:      { uint8_t t = U8(); fprintf(out, "BINOP          %s", tok_sym(t)); break; }
        case OP_UNARY:      { uint8_t t = U8(); fprintf(out, "UNARY          %s", tok_sym(t)); break; }
        case OP_TRANSPOSE:  { uint8_t cj = U8(); fprintf(out, "TRANSPOSE      %s", cj ? "conj" : "plain"); break; }
        case OP_RANGE:        fputs("RANGE", out); break;
        case OP_TEE:          fputs("TEE", out); break;
        case OP_CALL:       { uint8_t a = U8(); fprintf(out, "CALL           argc=%u", a); break; }
        case OP_AUTOCALL:     fputs("AUTOCALL", out); break;
        case OP_RETURN:       fputs("RETURN", out); break;
        case OP_FIELD:      { uint16_t k = U16(); fputs("FIELD          .", out); dis_name(out, c, k); break; }
        case OP_ASSERT_BOOL:  fputs("ASSERT_BOOL", out); break;
        case OP_JUMP:          { uint16_t o = U16(); fprintf(out, "JUMP           -> %04u", i + o); break; }
        case OP_JUMP_IF_FALSE: { uint16_t o = U16(); fprintf(out, "JUMP_IF_FALSE  -> %04u", i + o); break; }
        case OP_JUMP_IF_TRUE:  { uint16_t o = U16(); fprintf(out, "JUMP_IF_TRUE   -> %04u", i + o); break; }
        case OP_POP:          fputs("POP", out); break;
        case OP_LOOP:       { uint16_t o = U16(); fprintf(out, "LOOP           -> %04u", i - o); break; }
        case OP_SCOPE_PUSH:   fputs("SCOPE_PUSH", out); break;
        case OP_SCOPE_POP:    fputs("SCOPE_POP", out); break;
        case OP_FOR_BEGIN:    fputs("FOR_BEGIN", out); break;
        case OP_FOR_NEXT:   { uint16_t k = U16(); uint16_t o = U16();
                              fputs("FOR_NEXT       ", out); dis_name(out, c, k);
                              fprintf(out, ", end -> %04u", i + o); break; }
        case OP_FOR_END:      fputs("FOR_END", out); break;
        case OP_MARK_PUSH:    fputs("MARK_PUSH", out); break;
        case OP_MARK_POP:     fputs("MARK_POP", out); break;
        case OP_MARK_RESET:   fputs("MARK_RESET", out); break;
        case OP_CLOSURE:    { uint16_t p = U16(); uint8_t nu = U8();
                              fprintf(out, "CLOSURE        fn#%u, nup=%u", p, nu); break; }
        case OP_GET_LOCAL:  { uint8_t s = U8(); fprintf(out, "GET_LOCAL      slot %u", s); break; }
        case OP_SET_LOCAL:  { uint8_t s = U8(); fprintf(out, "SET_LOCAL      slot %u", s); break; }
        case OP_GET_UPVALUE:{ uint8_t s = U8(); fprintf(out, "GET_UPVALUE    up %u", s); break; }
        case OP_SET_UPVALUE:{ uint8_t s = U8(); fprintf(out, "SET_UPVALUE    up %u", s); break; }
        case OP_MATRIX:     { uint16_t k = U16(); fputs("MATRIX         shape ", out); dis_const(out, c, k); break; }
        case OP_INDEX:      { uint8_t a = U8(); uint8_t m = U8();
                              fprintf(out, "INDEX          argc=%u colonmask=%u", a, m); break; }
        case OP_END_PUSH:   { uint8_t a = U8(); fprintf(out, "END_PUSH       argc=%u", a); break; }
        case OP_END_GET:    { uint8_t d = U8(); fprintf(out, "END_GET        dim %u", d); break; }
        case OP_END_POP:      fputs("END_POP", out); break;
        case OP_INDEX_SET:  { uint8_t a = U8(); uint8_t m = U8();
                              fprintf(out, "INDEX_SET      argc=%u colonmask=%u", a, m); break; }
        case OP_RECORD:     { uint16_t nf = U16();
                              fprintf(out, "RECORD         %u field(s):", nf);
                              for (uint16_t f = 0; f < nf; f++) { uint16_t k = U16(); fputc(' ', out); dis_name(out, c, k); }
                              break; }
        }
        #undef U8
        #undef U16
        fputc('\n', out);
    }
    for (uint32_t p = 0; p < c->nproto; p++) {
        char sub[128];
        snprintf(sub, sizeof sub, "%s.fn#%u", title, p);
        fputc('\n', out);
        chunk_disassemble(out, c->protos[p], sub);
    }
}
