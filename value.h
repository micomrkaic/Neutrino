/* value.h — Neutrino runtime values.
 *
 * Scalars (null, bool, int64, f64, complex) are immediate — stored inline in
 * Value, no allocation. Everything else (string, array, record, closure,
 * builtin, env) is a refcounted heap Obj. The numeric tower is int -> float
 * -> complex; arrays carry one EltType so an index array is provably integer. */
#ifndef NEUTRINO_VALUE_H
#define NEUTRINO_VALUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "ast.h"

typedef struct { double re, im; } Cplx;

typedef enum : uint8_t {
    VAL_NULL, VAL_BOOL, VAL_INT, VAL_FLOAT, VAL_COMPLEX,
    VAL_STRING, VAL_ARRAY, VAL_RECORD, VAL_CLOSURE, VAL_BUILTIN,
} ValueKind;

/* array element type — the numeric tower plus a logical (Bool) element */
typedef enum : uint8_t { ELT_INT, ELT_FLOAT, ELT_COMPLEX, ELT_BOOL } EltType;

typedef struct Obj Obj;

typedef struct {
    ValueKind kind;
    union {
        bool     b;
        int64_t  i;
        double   f;
        Cplx     z;
        Obj     *obj;   /* STRING, ARRAY, RECORD, CLOSURE, BUILTIN */
    } as;
} Value;

struct Obj { ValueKind kind; uint32_t rc; };

typedef struct { Obj obj; char *data; uint32_t len; } StrObj;
typedef struct { Obj obj; EltType elt; uint32_t rows, cols; void *data; } ArrObj;

typedef struct {
    Obj obj;
    uint32_t count;
    const char **keys;   /* non-owning: point into source */
    uint32_t    *keylens;
    Value       *vals;   /* owned refs */
} RecObj;

typedef struct EnvObj EnvObj;
struct Chunk;   /* chunk.h — a compiled function proto */

typedef struct {
    Obj obj;
    struct Chunk *chunk;   /* compiled proto (non-owning; lives in a retained chunk) */
    Value   *upvalues;     /* value-capture snapshots of free variables (owned)      */
    uint32_t nupvalues;
} CloObj;

struct Interp;          /* eval.h */
typedef Value (*BuiltinFn)(struct Interp *I, Value *args, uint32_t n);

typedef struct {
    Obj obj;
    const char *name;
    BuiltinFn   fn;
    uint32_t    min_arity, max_arity;   /* max_arity == UINT32_MAX ⇒ variadic */
} BuiltinObj;

struct EnvObj {
    Obj obj;
    EnvObj      *parent;     /* owned ref (nullptr at global) */
    uint32_t     count, cap;
    const char **names;      /* non-owning slices */
    uint32_t    *namelens;
    Value       *vals;       /* owned refs */
};

/* --- refcounting --- */
Value value_retain(Value v);
void  value_release(Value v);

/* --- scalar constructors (immediate) --- */
static inline Value val_null(void)        { return (Value){ .kind = VAL_NULL }; }
static inline Value val_bool(bool b)      { return (Value){ .kind = VAL_BOOL,  .as.b = b }; }
static inline Value val_int(int64_t i)    { return (Value){ .kind = VAL_INT,   .as.i = i }; }
static inline Value val_float(double f)   { return (Value){ .kind = VAL_FLOAT, .as.f = f }; }
static inline Value val_complex(double re, double im) { return (Value){ .kind = VAL_COMPLEX, .as.z = { re, im } }; }

/* --- heap constructors (return +1 ref) --- */
Value val_string(const char *bytes, uint32_t len);   /* copies */
Value val_array(EltType elt, uint32_t rows, uint32_t cols);   /* zeroed */
Value val_record(uint32_t count);                    /* keys/vals filled by caller */
Value val_closure_vm(struct Chunk *proto, Value *upvalues, uint32_t nup);  /* takes ownership of upvalues */
Value val_builtin(const char *name, BuiltinFn fn, uint32_t min_arity, uint32_t max_arity);

/* --- accessors --- */
static inline StrObj     *as_str(Value v) { return (StrObj *)v.as.obj; }
static inline ArrObj     *as_arr(Value v) { return (ArrObj *)v.as.obj; }
static inline RecObj     *as_rec(Value v) { return (RecObj *)v.as.obj; }
static inline CloObj     *as_clo(Value v) { return (CloObj *)v.as.obj; }
static inline BuiltinObj *as_blt(Value v) { return (BuiltinObj *)v.as.obj; }

size_t elt_size(EltType e);
Value  arr_get(const ArrObj *a, size_t flat);          /* element -> scalar Value */
void   arr_set(ArrObj *a, size_t flat, Value scalar);  /* scalar coerced to a->elt */

/* --- environment --- */
EnvObj *env_new(EnvObj *parent);                       /* returns +1 ref */
void    env_retain(EnvObj *e);
void    env_release(EnvObj *e);
void    env_define(EnvObj *e, const char *name, uint32_t len, Value v);  /* retains v */
bool    env_assign(EnvObj *e, const char *name, uint32_t len, Value v);  /* update existing up the chain; retains v */
bool    env_lookup(EnvObj *e, const char *name, uint32_t len, Value *out); /* out gets +1 */
void    env_clear(EnvObj *e);   /* release all bindings (breaks closure/env cycles at teardown) */

void value_print(FILE *out, Value v);
const char *value_type_name(Value v);   /* "Int", "Array", ... for diagnostics */

/* number display formatting (affects Float and Complex parts) */
typedef enum { NFMT_G, NFMT_F, NFMT_E } NumFmtStyle;
void        value_format_set(NumFmtStyle style, int prec);
bool        value_format_by_name(const char *name);  /* "short"/"long"/"short e"/... ; false if unknown */
const char *value_format_desc(void);                 /* human label of the current setting */

/* output stream indirection — lets the REPL capture output for paging */
FILE *vout(void);                /* current output stream (defaults to stdout) */
void  value_set_out(FILE *f);    /* nullptr restores stdout */

/* multi-line aligned matrix display (off by default; the REPL enables it) */
void value_set_multiline(bool on);
bool value_multiline(void);

#endif
