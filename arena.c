/* arena.c */
#include "arena.h"
#include <stdlib.h>

#define ARENA_BLOCK (64u * 1024u)

typedef struct Block {
    struct Block *next;
    size_t        used, cap;
    /* payload follows the header in the same allocation */
} Block;

struct Arena { Block *head; };

static size_t align_up(size_t n, size_t a) { return (n + (a - 1)) & ~(a - 1); }

static Block *block_new(size_t cap)
{
    Block *b = malloc(sizeof(Block) + cap);
    if (!b) { abort(); }
    b->next = nullptr;
    b->used = 0;
    b->cap  = cap;
    return b;
}

Arena *arena_new(void)
{
    Arena *a = malloc(sizeof *a);
    if (!a) { abort(); }
    a->head = block_new(ARENA_BLOCK);
    return a;
}

void *arena_alloc(Arena *a, size_t n)
{
    n = align_up(n, alignof(max_align_t));
    Block *b = a->head;
    if (b->used + n > b->cap) {
        size_t cap = n > ARENA_BLOCK ? n : ARENA_BLOCK;
        b = block_new(cap);
        b->next = a->head;
        a->head = b;
    }
    void *p = (char *)(b + 1) + b->used;
    b->used += n;
    return p;
}

void arena_free(Arena *a)
{
    if (!a) return;
    for (Block *b = a->head; b; ) {
        Block *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}
