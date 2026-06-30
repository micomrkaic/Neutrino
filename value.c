/* value.c */
#include "value.h"
#include "chunk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- number display format (Float / Complex parts) ---------------- */
static NumFmtStyle g_fmt_style = NFMT_G;
static int         g_fmt_prec  = 6;          /* startup default == bare %g */

void value_format_set(NumFmtStyle style, int prec)
{
    if (prec < 0)  prec = 0;
    if (prec > 17) prec = 17;                /* a double carries ~17 sig digits */
    g_fmt_style = style; g_fmt_prec = prec;
}
bool value_format_by_name(const char *name)
{
    struct { const char *n; NumFmtStyle s; int p; } t[] = {
        { "short",   NFMT_G, 5 },  { "long",   NFMT_G, 16 },
        { "short g", NFMT_G, 5 },  { "long g", NFMT_G, 16 },
        { "short e", NFMT_E, 4 },  { "long e", NFMT_E, 15 },
        { "short f", NFMT_F, 4 },  { "long f", NFMT_F, 14 },
        { "default", NFMT_G, 6 },  { "reset",  NFMT_G, 6 },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++)
        if (strcmp(t[i].n, name) == 0) { value_format_set(t[i].s, t[i].p); return true; }
    return false;
}
const char *value_format_desc(void)
{
    static char buf[64];
    const char *style = g_fmt_style == NFMT_F ? "fixed"
                      : g_fmt_style == NFMT_E ? "scientific" : "auto";
    const char *unit  = g_fmt_style == NFMT_G ? "significant digits" : "decimals";
    snprintf(buf, sizeof buf, "%s, %d %s", style, g_fmt_prec, unit);
    return buf;
}
static void num_spec(char *spec, size_t n)
{
    char conv = g_fmt_style == NFMT_F ? 'f' : g_fmt_style == NFMT_E ? 'e' : 'g';
    snprintf(spec, n, "%%.%d%c", g_fmt_prec, conv);
}
static void fmt_double(FILE *out, double x)
{
    char spec[8]; num_spec(spec, sizeof spec);
    fprintf(out, spec, x);
}
static int fmt_double_str(char *buf, size_t cap, double x)
{
    char spec[8]; num_spec(spec, sizeof spec);
    return snprintf(buf, cap, spec, x);
}

/* ---- multi-line aligned matrix display (opt-in; REPL turns it on) ---- */
static bool g_multiline;
void value_set_multiline(bool on) { g_multiline = on; }
bool value_multiline(void)        { return g_multiline; }

/* ---- output stream indirection ------------------------------------ */
static FILE *g_out;                          /* nullptr means stdout */
FILE *vout(void)            { return g_out ? g_out : stdout; }
void  value_set_out(FILE *f) { g_out = f; }


size_t elt_size(EltType e)
{
    switch (e) {
    case ELT_INT:     return sizeof(int64_t);
    case ELT_FLOAT:   return sizeof(double);
    case ELT_COMPLEX: return sizeof(Cplx);
    case ELT_BOOL:    return sizeof(unsigned char);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* refcounting                                                         */
/* ------------------------------------------------------------------ */
static void env_free(EnvObj *e);

static void obj_free(Obj *o)
{
    switch (o->kind) {
    case VAL_STRING:
        free(((StrObj *)o)->data);
        break;
    case VAL_ARRAY:
        free(((ArrObj *)o)->data);
        break;
    case VAL_RECORD: {
        RecObj *r = (RecObj *)o;
        for (uint32_t i = 0; i < r->count; i++) value_release(r->vals[i]);
        free(r->keys); free(r->keylens); free(r->vals);
        break;
    }
    case VAL_CLOSURE: {
        CloObj *cl = (CloObj *)o;
        for (uint32_t i = 0; i < cl->nupvalues; i++) value_release(cl->upvalues[i]);
        free(cl->upvalues);
        break;
    }
    default:
        break;
    }
    free(o);
}

Value value_retain(Value v)
{
    if (v.kind >= VAL_STRING && v.as.obj) v.as.obj->rc++;
    return v;
}

void value_release(Value v)
{
    if (v.kind < VAL_STRING || !v.as.obj) return;
    Obj *o = v.as.obj;
    if (--o->rc == 0) obj_free(o);
}

/* closures hold an EnvObj*; route env retain/release through Obj rc */
void env_retain(EnvObj *e) { if (e) e->obj.rc++; }
void env_release(EnvObj *e)
{
    if (!e) return;
    if (--e->obj.rc == 0) env_free(e);
}
static Obj *alloc_obj(size_t size, ValueKind kind)
{
    Obj *o = calloc(1, size);
    if (!o) abort();
    o->kind = kind;
    o->rc = 1;
    return o;
}

/* ------------------------------------------------------------------ */
/* heap constructors                                                   */
/* ------------------------------------------------------------------ */
Value val_string(const char *bytes, uint32_t len)
{
    StrObj *s = (StrObj *)alloc_obj(sizeof *s, VAL_STRING);
    s->data = malloc(len + 1u);
    if (!s->data) abort();
    memcpy(s->data, bytes, len);
    s->data[len] = '\0';
    s->len = len;
    return (Value){ .kind = VAL_STRING, .as.obj = &s->obj };
}

Value val_array(EltType elt, uint32_t rows, uint32_t cols)
{
    ArrObj *a = (ArrObj *)alloc_obj(sizeof *a, VAL_ARRAY);
    a->elt = elt; a->rows = rows; a->cols = cols;
    size_t n = (size_t)rows * cols;
    a->data = n ? calloc(n, elt_size(elt)) : nullptr;
    if (n && !a->data) abort();
    return (Value){ .kind = VAL_ARRAY, .as.obj = &a->obj };
}

Value val_record(uint32_t count)
{
    RecObj *r = (RecObj *)alloc_obj(sizeof *r, VAL_RECORD);
    r->count = count;
    r->keys    = count ? calloc(count, sizeof *r->keys)    : nullptr;
    r->keylens = count ? calloc(count, sizeof *r->keylens) : nullptr;
    r->vals    = count ? calloc(count, sizeof *r->vals)    : nullptr;
    return (Value){ .kind = VAL_RECORD, .as.obj = &r->obj };
}

Value val_closure_vm(struct Chunk *proto, Value *upvalues, uint32_t nup)
{
    CloObj *c = (CloObj *)alloc_obj(sizeof *c, VAL_CLOSURE);
    c->chunk = proto; c->upvalues = upvalues; c->nupvalues = nup;
    return (Value){ .kind = VAL_CLOSURE, .as.obj = &c->obj };
}

Value val_builtin(const char *name, BuiltinFn fn, uint32_t min_arity, uint32_t max_arity)
{
    BuiltinObj *b = (BuiltinObj *)alloc_obj(sizeof *b, VAL_BUILTIN);
    b->name = name; b->fn = fn; b->min_arity = min_arity; b->max_arity = max_arity;
    return (Value){ .kind = VAL_BUILTIN, .as.obj = &b->obj };
}

/* ------------------------------------------------------------------ */
/* array element access                                                */
/* ------------------------------------------------------------------ */
Value arr_get(const ArrObj *a, size_t k)
{
    switch (a->elt) {
    case ELT_INT:     return val_int(((const int64_t *)a->data)[k]);
    case ELT_FLOAT:   return val_float(((const double *)a->data)[k]);
    case ELT_COMPLEX: { Cplx c = ((const Cplx *)a->data)[k]; return val_complex(c.re, c.im); }
    case ELT_BOOL:    return val_bool(((const unsigned char *)a->data)[k] != 0);
    }
    return val_null();
}

void arr_set(ArrObj *a, size_t k, Value v)
{
    switch (a->elt) {
    case ELT_INT:
        ((int64_t *)a->data)[k] = (v.kind == VAL_INT) ? v.as.i : (int64_t)v.as.f;
        break;
    case ELT_FLOAT:
        ((double *)a->data)[k] = (v.kind == VAL_INT) ? (double)v.as.i : v.as.f;
        break;
    case ELT_COMPLEX: {
        Cplx c;
        if      (v.kind == VAL_INT)     c = (Cplx){ (double)v.as.i, 0.0 };
        else if (v.kind == VAL_FLOAT)   c = (Cplx){ v.as.f, 0.0 };
        else                            c = v.as.z;
        ((Cplx *)a->data)[k] = c;
        break;
    }
    case ELT_BOOL:
        ((unsigned char *)a->data)[k] =
            (v.kind == VAL_BOOL) ? (v.as.b ? 1 : 0)
          : (v.kind == VAL_INT)  ? (v.as.i != 0)
          :                        (v.as.f != 0.0);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */
EnvObj *env_new(EnvObj *parent)
{
    EnvObj *e = (EnvObj *)alloc_obj(sizeof *e, VAL_NULL);  /* kind unused for env */
    e->parent = parent;
    env_retain(parent);
    return e;
}

static void env_free(EnvObj *e)
{
    for (uint32_t i = 0; i < e->count; i++) value_release(e->vals[i]);
    free(e->names); free(e->namelens); free(e->vals);
    env_release(e->parent);
    free(e);
}

void env_define(EnvObj *e, const char *name, uint32_t len, Value v)
{
    for (uint32_t i = 0; i < e->count; i++) {           /* replace in current frame */
        if (e->namelens[i] == len && memcmp(e->names[i], name, len) == 0) {
            value_release(e->vals[i]);
            e->vals[i] = value_retain(v);
            return;
        }
    }
    if (e->count == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->names    = realloc(e->names,    e->cap * sizeof *e->names);
        e->namelens = realloc(e->namelens, e->cap * sizeof *e->namelens);
        e->vals     = realloc(e->vals,     e->cap * sizeof *e->vals);
        if (!e->names || !e->namelens || !e->vals) abort();
    }
    e->names[e->count]    = name;
    e->namelens[e->count] = len;
    e->vals[e->count]     = value_retain(v);
    e->count++;
}

bool env_lookup(EnvObj *e, const char *name, uint32_t len, Value *out)
{
    for (; e; e = e->parent)
        for (uint32_t i = 0; i < e->count; i++)
            if (e->namelens[i] == len && memcmp(e->names[i], name, len) == 0) {
                *out = value_retain(e->vals[i]);
                return true;
            }
    return false;
}

bool env_assign(EnvObj *e, const char *name, uint32_t len, Value v)
{
    for (; e; e = e->parent)
        for (uint32_t i = 0; i < e->count; i++)
            if (e->namelens[i] == len && memcmp(e->names[i], name, len) == 0) {
                value_release(e->vals[i]);
                e->vals[i] = value_retain(v);
                return true;
            }
    return false;
}

/* Release every binding and empty the frame. A global-scope closure captures
 * the very env that owns it (env -> closure -> env), a cycle refcounting alone
 * can't reclaim. Clearing the frame at teardown drops the env->closure edge so
 * the closure frees, releases its captured env, and the env then frees too.
 * count is zeroed so the later env_free does not double-release. */
void env_clear(EnvObj *e)
{
    for (uint32_t i = 0; i < e->count; i++) value_release(e->vals[i]);
    e->count = 0;
}

/* ------------------------------------------------------------------ */
/* printing                                                            */
/* ------------------------------------------------------------------ */
static void print_complex(FILE *out, double re, double im)
{
    if (im == 0.0) im = 0.0;   /* fold -0.0 (conjugation of a real element) */
    if (re == 0.0) re = 0.0;
    if (re == 0.0 && im != 0.0) { fmt_double(out, im); fputc('i', out); return; }
    fmt_double(out, re);
    fputc(im < 0 ? '-' : '+', out);
    fmt_double(out, im < 0 ? -im : im);
    fputc('i', out);
}

static void print_scalar(FILE *out, Value v)
{
    switch (v.kind) {
    case VAL_NULL:    fputs("null", out); break;
    case VAL_BOOL:    fputs(v.as.b ? "true" : "false", out); break;
    case VAL_INT:     fprintf(out, "%lld", (long long)v.as.i); break;
    case VAL_FLOAT:   fmt_double(out, v.as.f); break;
    case VAL_COMPLEX: print_complex(out, v.as.z.re, v.as.z.im); break;
    default:          break;
    }
}

const char *value_type_name(Value v)
{
    switch (v.kind) {
    case VAL_NULL:    return "Null";
    case VAL_BOOL:    return "Bool";
    case VAL_INT:     return "Int";
    case VAL_FLOAT:   return "Float";
    case VAL_COMPLEX: return "Complex";
    case VAL_STRING:  return "String";
    case VAL_ARRAY:   return "Array";
    case VAL_RECORD:  return "Record";
    case VAL_CLOSURE: return "Closure";
    case VAL_BUILTIN: return "Builtin";
    }
    return "?";
}

/* Format one scalar element into buf (for width measurement + aligned print). */
static void scalar_str(char *buf, size_t cap, Value v)
{
    switch (v.kind) {
    case VAL_NULL:  snprintf(buf, cap, "null"); break;
    case VAL_BOOL:  snprintf(buf, cap, "%s", v.as.b ? "true" : "false"); break;
    case VAL_INT:   snprintf(buf, cap, "%lld", (long long)v.as.i); break;
    case VAL_FLOAT: fmt_double_str(buf, cap, v.as.f); break;
    case VAL_COMPLEX: {
        double re = v.as.z.re, im = v.as.z.im;
        if (im == 0.0) im = 0.0;
        if (re == 0.0) re = 0.0;
        char rb[32], ib[32];
        if (re == 0.0 && im != 0.0) {
            fmt_double_str(ib, sizeof ib, im);
            snprintf(buf, cap, "%si", ib);
        } else {
            fmt_double_str(rb, sizeof rb, re);
            fmt_double_str(ib, sizeof ib, im < 0 ? -im : im);
            snprintf(buf, cap, "%s%c%si", rb, im < 0 ? '-' : '+', ib);
        }
        break;
    }
    default: snprintf(buf, cap, "?"); break;
    }
}

/* Octave-style aligned block: columns right-justified to a common width. */
static void print_matrix_aligned(FILE *out, ArrObj *a)
{
    uint32_t R = a->rows, C = a->cols;
    size_t cells = (size_t)R * C;
    char (*buf)[80] = malloc((cells ? cells : 1) * sizeof *buf);
    int w = 0;
    for (size_t k = 0; k < cells; k++) {
        scalar_str(buf[k], sizeof buf[k], arr_get(a, k));
        int l = (int)strlen(buf[k]);
        if (l > w) w = l;
    }
    for (uint32_t r = 0; r < R; r++) {
        fputs(r == 0 ? "[ " : "  ", out);
        for (uint32_t c = 0; c < C; c++) {
            if (c) fputs("  ", out);
            fprintf(out, "%*s", w, buf[(size_t)r*C + c]);
        }
        if (r + 1 < R) fputc('\n', out);
        else           fputs(" ]", out);
    }
    free(buf);
}

void value_print(FILE *out, Value v)
{
    switch (v.kind) {
    case VAL_NULL: case VAL_BOOL: case VAL_INT: case VAL_FLOAT: case VAL_COMPLEX:
        print_scalar(out, v);
        break;
    case VAL_STRING:
        fprintf(out, "\"%.*s\"", (int)as_str(v)->len, as_str(v)->data);
        break;
    case VAL_ARRAY: {
        ArrObj *a = as_arr(v);
        if (g_multiline && a->rows > 1) { print_matrix_aligned(out, a); break; }
        fputc('[', out);
        for (uint32_t r = 0; r < a->rows; r++) {
            if (r) fputs("; ", out);
            for (uint32_t c = 0; c < a->cols; c++) {
                if (c) fputs(", ", out);
                Value e = arr_get(a, (size_t)r * a->cols + c);
                print_scalar(out, e);
            }
        }
        fputc(']', out);
        break;
    }
    case VAL_RECORD: {
        RecObj *r = as_rec(v);
        bool saved = g_multiline; g_multiline = false;   /* fields stay compact/single-line */
        fputc('{', out);
        for (uint32_t i = 0; i < r->count; i++) {
            if (i) fputs(", ", out);
            fprintf(out, "%.*s = ", (int)r->keylens[i], r->keys[i]);
            value_print(out, r->vals[i]);
        }
        fputc('}', out);
        g_multiline = saved;
        break;
    }
    case VAL_CLOSURE:
        fprintf(out, "<fn/%u>", as_clo(v)->chunk->nparams);
        break;
    case VAL_BUILTIN:
        fprintf(out, "<builtin %s>", as_blt(v)->name);
        break;
    }
}
