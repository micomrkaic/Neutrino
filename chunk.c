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
