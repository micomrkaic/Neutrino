/* eval.c — Neutrino tree-walking evaluator. */
#define _XOPEN_SOURCE 700         /* open_memstream + jn/yn Bessel (superset of POSIX.1-2008) */
#include <time.h>
#include "eval.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/wait.h>
#include "nrt.h"
#include "chunk.h"

/* ------------------------------------------------------------------ */
/* errors                                                              */
/* ------------------------------------------------------------------ */
[[noreturn]] void runtime_error(Interp *I, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(I->err, sizeof I->err, fmt, ap);
    va_end(ap);
    I->had_error = true;
    longjmp(I->jmp, 1);
}

static const char *type_name(ValueKind k)
{
    switch (k) {
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

static const char *elt_name(EltType e)
{
    switch (e) {
    case ELT_BOOL:    return "Bool";
    case ELT_INT:     return "Int";
    case ELT_FLOAT:   return "Float";
    case ELT_COMPLEX: return "Complex";
    }
    return "?";
}

static bool is_num(Value v)   { return v.kind == VAL_INT || v.kind == VAL_FLOAT || v.kind == VAL_COMPLEX; }
static bool is_array(Value v) { return v.kind == VAL_ARRAY; }

/* ------------------------------------------------------------------ */
/* numeric tower — scalar arithmetic                                   */
/* ------------------------------------------------------------------ */
typedef enum { AR_ADD, AR_SUB, AR_MUL, AR_DIV, AR_POW, AR_LDIV } Arith;

static int    num_rank(Value v) { return v.kind == VAL_INT ? 0 : v.kind == VAL_FLOAT ? 1 : 2; }
static double as_double(Value v){ return v.kind == VAL_INT ? (double)v.as.i : v.kind == VAL_FLOAT ? v.as.f : v.as.z.re; }
static Cplx   as_cplx(Value v)  { return v.kind == VAL_COMPLEX ? v.as.z
                                       : v.kind == VAL_FLOAT   ? (Cplx){ v.as.f, 0.0 }
                                                               : (Cplx){ (double)v.as.i, 0.0 }; }

static Cplx c_add(Cplx a, Cplx b){ return (Cplx){ a.re + b.re, a.im + b.im }; }
static Cplx c_sub(Cplx a, Cplx b){ return (Cplx){ a.re - b.re, a.im - b.im }; }
static Cplx c_mul(Cplx a, Cplx b){ return (Cplx){ a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re }; }
static Cplx c_div(Cplx a, Cplx b){ double d = b.re*b.re + b.im*b.im;
    return (Cplx){ (a.re*b.re + a.im*b.im)/d, (a.im*b.re - a.re*b.im)/d }; }

static int64_t ipow(int64_t base, int64_t e){ int64_t r = 1; while (e-- > 0) r *= base; return r; }

/* ---- PRNG: xoshiro256** seeded by splitmix64 (deterministic, reseed via rng()) ---- */
static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static void rng_seed(Interp *I, uint64_t seed)
{
    uint64_t sm = seed;
    for (int i = 0; i < 4; i++) I->rng_s[i] = splitmix64(&sm);
}
static inline uint64_t rotl64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
static uint64_t rng_next_u64(Interp *I)
{
    uint64_t *s = I->rng_s;
    uint64_t result = rotl64(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = rotl64(s[3], 45);
    return result;
}
static double rng_uniform(Interp *I)                      /* [0, 1) with 53 bits */
{
    return (double)(rng_next_u64(I) >> 11) * 0x1.0p-53;
}
static void rng_normal_pair(Interp *I, double *z0, double *z1)   /* Box-Muller */
{
    double u1 = rng_uniform(I), u2 = rng_uniform(I);
    if (u1 < 1e-300) u1 = 1e-300;                         /* guard log(0) */
    double r = sqrt(-2.0 * log(u1)), th = 6.283185307179586 * u2;   /* 2*pi */
    *z0 = r * cos(th); *z1 = r * sin(th);
}

static Arith arith_of(enum TokenKind op)
{
    switch (op) {
    case TOK_PLUS:                          return AR_ADD;
    case TOK_MINUS:                         return AR_SUB;
    case TOK_STAR:  case TOK_DOT_STAR:      return AR_MUL;
    case TOK_SLASH: case TOK_DOT_SLASH:     return AR_DIV;
    case TOK_CARET: case TOK_DOT_CARET:     return AR_POW;
    case TOK_BACKSLASH: case TOK_DOT_BACKSLASH: return AR_LDIV;
    default:                                return AR_ADD;   /* unreachable */
    }
}

static Value scalar_arith_k(Interp *I, Arith kind, Value a, Value b)
{
    if (!is_num(a) || !is_num(b))
        runtime_error(I, "arithmetic on non-numbers (%s, %s)", type_name(a.kind), type_name(b.kind));

    if (kind == AR_LDIV) { Value t = a; a = b; b = t; kind = AR_DIV; }   /* a\b == b/a */

    int rank = num_rank(a) > num_rank(b) ? num_rank(a) : num_rank(b);
    if (kind == AR_DIV && rank < 1) rank = 1;                            /* int/int -> float */
    if (kind == AR_POW) {
        if (rank == 2) runtime_error(I, "complex exponentiation is not supported yet");
        if (rank == 0 && b.as.i < 0) rank = 1;                          /* int^negint -> float */
    }

    switch (rank) {
    case 0: {
        int64_t x = a.as.i, y = b.as.i;
        switch (kind) {
        case AR_ADD: return val_int(x + y);
        case AR_SUB: return val_int(x - y);
        case AR_MUL: return val_int(x * y);
        case AR_POW: return val_int(ipow(x, y));
        default:     break;
        }
        break;
    }
    case 1: {
        double x = as_double(a), y = as_double(b);
        switch (kind) {
        case AR_ADD: return val_float(x + y);
        case AR_SUB: return val_float(x - y);
        case AR_MUL: return val_float(x * y);
        case AR_DIV: return val_float(x / y);
        case AR_POW: return val_float(pow(x, y));
        default:     break;
        }
        break;
    }
    case 2: {
        Cplx x = as_cplx(a), y = as_cplx(b);
        switch (kind) {
        case AR_ADD: { Cplx r = c_add(x, y); return val_complex(r.re, r.im); }
        case AR_SUB: { Cplx r = c_sub(x, y); return val_complex(r.re, r.im); }
        case AR_MUL: { Cplx r = c_mul(x, y); return val_complex(r.re, r.im); }
        case AR_DIV: { Cplx r = c_div(x, y); return val_complex(r.re, r.im); }
        default:     break;
        }
        break;
    }
    }
    runtime_error(I, "unsupported arithmetic");
}

static Value scalar_cmp(Interp *I, enum TokenKind op, Value a, Value b)
{
    if (!is_num(a) || !is_num(b))
        runtime_error(I, "comparison on non-numbers (%s, %s)", type_name(a.kind), type_name(b.kind));

    if (a.kind == VAL_COMPLEX || b.kind == VAL_COMPLEX) {
        if (op != TOK_EQ && op != TOK_NE)
            runtime_error(I, "ordering comparison is undefined for Complex");
        Cplx x = as_cplx(a), y = as_cplx(b);
        bool eq = x.re == y.re && x.im == y.im;
        return val_bool(op == TOK_EQ ? eq : !eq);
    }
    if (a.kind == VAL_INT && b.kind == VAL_INT) {
        int64_t x = a.as.i, y = b.as.i;
        switch (op) {
        case TOK_EQ: return val_bool(x == y); case TOK_NE: return val_bool(x != y);
        case TOK_LT: return val_bool(x <  y); case TOK_LE: return val_bool(x <= y);
        case TOK_GT: return val_bool(x >  y); case TOK_GE: return val_bool(x >= y);
        default: break;
        }
    }
    double x = as_double(a), y = as_double(b);
    switch (op) {
    case TOK_EQ: return val_bool(x == y); case TOK_NE: return val_bool(x != y);
    case TOK_LT: return val_bool(x <  y); case TOK_LE: return val_bool(x <= y);
    case TOK_GT: return val_bool(x >  y); case TOK_GE: return val_bool(x >= y);
    default: break;
    }
    runtime_error(I, "unsupported comparison");
}

/* ------------------------------------------------------------------ */
/* array helpers                                                       */
/* ------------------------------------------------------------------ */
static EltType vk_elt(ValueKind k)
{
    return k == VAL_BOOL ? ELT_BOOL : k == VAL_INT ? ELT_INT : k == VAL_FLOAT ? ELT_FLOAT : ELT_COMPLEX;
}
static EltType elt_max(EltType a, EltType b) { return a > b ? a : b; }   /* numeric tower only */

/* pack n temp scalar Values (owned) into a fresh rows×cols array; releases temps.
 * An all-Bool batch yields a logical array; bools never raise the numeric tower. */
static Value pack_array(Value *tmp, size_t n, uint32_t rows, uint32_t cols)
{
    EltType e = ELT_INT;
    bool all_bool = n > 0;
    for (size_t k = 0; k < n; k++) {
        if (tmp[k].kind == VAL_BOOL) continue;
        all_bool = false;
        e = elt_max(e, vk_elt(tmp[k].kind));
    }
    Value arr = val_array(all_bool ? ELT_BOOL : e, rows, cols);
    for (size_t k = 0; k < n; k++) { arr_set(as_arr(arr), k, tmp[k]); value_release(tmp[k]); }
    return arr;
}

static Value elementwise(Interp *I, Arith kind, Value a, Value b)
{
    bool aa = is_array(a), ba = is_array(b);
    uint32_t rows, cols;
    if (aa && ba) {
        if (as_arr(a)->rows != as_arr(b)->rows || as_arr(a)->cols != as_arr(b)->cols)
            runtime_error(I, "shape mismatch: %ux%u vs %ux%u",
                          as_arr(a)->rows, as_arr(a)->cols, as_arr(b)->rows, as_arr(b)->cols);
        rows = as_arr(a)->rows; cols = as_arr(a)->cols;
    } else if (aa) { rows = as_arr(a)->rows; cols = as_arr(a)->cols; }
    else           { rows = as_arr(b)->rows; cols = as_arr(b)->cols; }

    size_t n = (size_t)rows * cols;
    Value *tmp = n ? malloc(n * sizeof *tmp) : nullptr;
    for (size_t k = 0; k < n; k++) {
        Value av = aa ? arr_get(as_arr(a), k) : a;
        Value bv = ba ? arr_get(as_arr(b), k) : b;
        tmp[k] = scalar_arith_k(I, kind, av, bv);
    }
    Value r = pack_array(tmp, n, rows, cols);
    free(tmp);
    return r;
}

/* broadcast shape for elementwise ops: array∘array (equal shapes) or array∘scalar */
static void ew_dims(Interp *I, Value a, Value b, bool *aa, bool *ba, uint32_t *rows, uint32_t *cols)
{
    *aa = is_array(a); *ba = is_array(b);
    if (*aa && *ba) {
        if (as_arr(a)->rows != as_arr(b)->rows || as_arr(a)->cols != as_arr(b)->cols)
            runtime_error(I, "shape mismatch: %ux%u vs %ux%u",
                          as_arr(a)->rows, as_arr(a)->cols, as_arr(b)->rows, as_arr(b)->cols);
        *rows = as_arr(a)->rows; *cols = as_arr(a)->cols;
    } else if (*aa) { *rows = as_arr(a)->rows; *cols = as_arr(a)->cols; }
    else            { *rows = as_arr(b)->rows; *cols = as_arr(b)->cols; }
}

/* elementwise comparison -> logical (Bool) array */
static Value elementwise_cmp(Interp *I, enum TokenKind op, Value a, Value b)
{
    bool aa, ba; uint32_t rows, cols;
    ew_dims(I, a, b, &aa, &ba, &rows, &cols);
    Value out = val_array(ELT_BOOL, rows, cols);
    ArrObj *o = as_arr(out);
    size_t n = (size_t)rows * cols;
    for (size_t k = 0; k < n; k++) {
        Value av = aa ? arr_get(as_arr(a), k) : a;
        Value bv = ba ? arr_get(as_arr(b), k) : b;
        arr_set(o, k, scalar_cmp(I, op, av, bv));
    }
    return out;
}

/* elementwise & | on logical arrays -> logical array */
static Value elementwise_logical(Interp *I, enum TokenKind op, Value a, Value b)
{
    bool aa, ba; uint32_t rows, cols;
    ew_dims(I, a, b, &aa, &ba, &rows, &cols);
    Value out = val_array(ELT_BOOL, rows, cols);
    ArrObj *o = as_arr(out);
    size_t n = (size_t)rows * cols;
    for (size_t k = 0; k < n; k++) {
        Value av = aa ? arr_get(as_arr(a), k) : a;
        Value bv = ba ? arr_get(as_arr(b), k) : b;
        if (av.kind != VAL_BOOL || bv.kind != VAL_BOOL) {
            value_release(out);
            runtime_error(I, "'%s' requires Bool operands", op == TOK_AMP ? "&" : "|");
        }
        arr_set(o, k, val_bool(op == TOK_AMP ? (av.as.b && bv.as.b) : (av.as.b || bv.as.b)));
    }
    return out;
}

static Value matmul(Interp *I, Value a, Value b)
{
    ArrObj *x = as_arr(a), *y = as_arr(b);
    if (x->cols != y->rows)
        runtime_error(I, "matmul inner dimensions disagree: %ux%u * %ux%u",
                      x->rows, x->cols, y->rows, y->cols);
    uint32_t m = x->rows, k = x->cols, nn = y->cols;
    size_t cells = (size_t)m * nn;
    Value *tmp = cells ? malloc(cells * sizeof *tmp) : nullptr;
    for (uint32_t i = 0; i < m; i++)
        for (uint32_t j = 0; j < nn; j++) {
            Value acc = val_int(0);
            for (uint32_t t = 0; t < k; t++) {
                Value aik = arr_get(x, (size_t)i * k + t);
                Value bkj = arr_get(y, (size_t)t * nn + j);
                Value prod = scalar_arith_k(I, AR_MUL, aik, bkj);
                Value sum  = scalar_arith_k(I, AR_ADD, acc, prod);
                acc = sum;
            }
            tmp[(size_t)i * nn + j] = acc;
        }
    Value r = pack_array(tmp, cells, m, nn);
    free(tmp);
    return r;
}

static Value mldivide(Interp *I, Value A, Value B);
static Value mrdivide(Interp *I, Value num, Value den);
static Value mpow(Interp *I, Value base, Value e);
static Value lstsq(Interp *I, Value A, Value B);   /* non-square \ : least squares via QR */

static Value array_binop(Interp *I, enum TokenKind op, Value a, Value b)
{
    switch (op) {
    case TOK_STAR:
        if (is_array(a) && is_array(b)) return matmul(I, a, b);
        return elementwise(I, AR_MUL, a, b);                 /* scalar × array */
    case TOK_DOT_STAR:      return elementwise(I, AR_MUL, a, b);

    case TOK_BACKSLASH:                                       /* mldivide: solve A x = b */
        if (is_array(a) && is_array(b)) return mldivide(I, a, b);
        if (!is_array(a))               return elementwise(I, AR_LDIV, a, b);  /* scalar \ array */
        runtime_error(I, "left division: array \\ scalar is not conformable (use .\\ for elementwise)");
    case TOK_DOT_BACKSLASH: return elementwise(I, AR_LDIV, a, b);

    case TOK_SLASH:                                           /* mrdivide: solve x A = b */
        if (is_array(a) && is_array(b)) return mrdivide(I, a, b);
        if (!is_array(b))               return elementwise(I, AR_DIV, a, b);   /* array / scalar */
        runtime_error(I, "right division: scalar / array is not conformable (use ./ for elementwise)");
    case TOK_DOT_SLASH:     return elementwise(I, AR_DIV, a, b);

    case TOK_CARET:
        if (is_array(a)) return mpow(I, a, b);
        runtime_error(I, "'^' needs a matrix base; scalar ^ matrix is not supported");
    case TOK_DOT_CARET:     return elementwise(I, AR_POW, a, b);

    case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
        return elementwise_cmp(I, op, a, b);
    case TOK_AMP: case TOK_PIPE:
        return elementwise_logical(I, op, a, b);

    default:                                                 /* + and - */
        return elementwise(I, arith_of(op), a, b);
    }
}

Value transpose(Interp *I, Value v, bool conj)
{
    (void)I;
    if (!is_array(v)) return value_retain(v);   /* transpose of a scalar is itself */
    ArrObj *a = as_arr(v);
    Value out = val_array(a->elt, a->cols, a->rows);
    ArrObj *o = as_arr(out);
    for (uint32_t i = 0; i < a->rows; i++)
        for (uint32_t j = 0; j < a->cols; j++) {
            Value e = arr_get(a, (size_t)i * a->cols + j);
            if (conj && e.kind == VAL_COMPLEX) e.as.z.im = -e.as.z.im;
            arr_set(o, (size_t)j * a->rows + i, e);
        }
    return out;
}

/* Solve A X = B for X (A square n×n, B n×m), Gaussian elimination with partial
 * pivoting carried out in complex; returns Float when all inputs are real
 * (real arithmetic in Cplx keeps imaginary parts exactly zero), else Complex. */
static Value mldivide(Interp *I, Value A, Value B)
{
    ArrObj *a = as_arr(A), *b = as_arr(B);
    uint32_t n = a->rows, m = b->cols;
    if (a->cols != n)
        return lstsq(I, A, B);          /* non-square: overdetermined least squares (QR) */
    if (b->rows != n)
        runtime_error(I, "left division dimensions disagree: %ux%u \\ %ux%u",
                      a->rows, a->cols, b->rows, b->cols);

    Cplx *LU = malloc((size_t)n * n * sizeof *LU);
    Cplx *X  = malloc((size_t)n * m * sizeof *X);
    if ((!LU && n) || (!X && n && m)) abort();
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) LU[(size_t)i*n+j] = as_cplx(arr_get(a, (size_t)i*n+j));
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < m; j++) X[(size_t)i*m+j]  = as_cplx(arr_get(b, (size_t)i*m+j));

    for (uint32_t k = 0; k < n; k++) {
        uint32_t piv = k;
        double best = hypot(LU[(size_t)k*n+k].re, LU[(size_t)k*n+k].im);
        for (uint32_t i = k+1; i < n; i++) {
            double mag = hypot(LU[(size_t)i*n+k].re, LU[(size_t)i*n+k].im);
            if (mag > best) { best = mag; piv = i; }
        }
        if (best == 0.0) { free(LU); free(X); runtime_error(I, "left division: matrix is singular"); }
        if (piv != k) {
            for (uint32_t j = 0; j < n; j++) { Cplx t = LU[(size_t)k*n+j]; LU[(size_t)k*n+j] = LU[(size_t)piv*n+j]; LU[(size_t)piv*n+j] = t; }
            for (uint32_t j = 0; j < m; j++) { Cplx t = X[(size_t)k*m+j];  X[(size_t)k*m+j]  = X[(size_t)piv*m+j];  X[(size_t)piv*m+j]  = t; }
        }
        Cplx akk = LU[(size_t)k*n+k];
        for (uint32_t i = k+1; i < n; i++) {
            Cplx f = c_div(LU[(size_t)i*n+k], akk);
            for (uint32_t j = k; j < n; j++) LU[(size_t)i*n+j] = c_sub(LU[(size_t)i*n+j], c_mul(f, LU[(size_t)k*n+j]));
            for (uint32_t j = 0; j < m; j++) X[(size_t)i*m+j]  = c_sub(X[(size_t)i*m+j],  c_mul(f, X[(size_t)k*m+j]));
        }
    }
    for (uint32_t c = 0; c < m; c++)
        for (int64_t ii = (int64_t)n - 1; ii >= 0; ii--) {
            uint32_t i = (uint32_t)ii;
            Cplx s = X[(size_t)i*m+c];
            for (uint32_t j = i+1; j < n; j++) s = c_sub(s, c_mul(LU[(size_t)i*n+j], X[(size_t)j*m+c]));
            X[(size_t)i*m+c] = c_div(s, LU[(size_t)i*n+i]);
        }

    bool real_in = a->elt != ELT_COMPLEX && b->elt != ELT_COMPLEX;
    Value out = val_array(real_in ? ELT_FLOAT : ELT_COMPLEX, n, m);
    ArrObj *R = as_arr(out);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < m; j++) {
            Cplx z = X[(size_t)i*m+j];
            if (real_in) ((double *)R->data)[(size_t)i*m+j] = z.re;
            else         ((Cplx   *)R->data)[(size_t)i*m+j] = z;
        }
    free(LU); free(X);
    return out;
}

/* B / A solves X A = B, i.e. X = (A' \ B')' with plain transposes. */
static Value mrdivide(Interp *I, Value num, Value den)
{
    Value At = transpose(I, den, false);
    Value Bt = transpose(I, num, false);
    Value Xt = mldivide(I, At, Bt);
    Value X  = transpose(I, Xt, false);
    value_release(At); value_release(Bt); value_release(Xt);
    return X;
}

static Value identity(uint32_t n)
{
    Value m = val_array(ELT_INT, n, n);
    int64_t *d = (int64_t *)as_arr(m)->data;
    for (uint32_t i = 0; i < n; i++) d[(size_t)i * n + i] = 1;
    return m;
}

/* A^p for square A and integer p: exponentiation by squaring; p<0 inverts first
 * (A^-1 = A \ I), p==0 is the identity. */
static Value mpow(Interp *I, Value base, Value e)
{
    if (!is_array(base)) runtime_error(I, "matrix power: base must be a matrix");
    ArrObj *b = as_arr(base);
    if (b->rows != b->cols)
        runtime_error(I, "matrix power requires a square matrix (got %ux%u)", b->rows, b->cols);
    if (e.kind != VAL_INT)
        runtime_error(I, "matrix power exponent must be an integer");
    uint32_t n = b->rows;
    int64_t p = e.as.i;

    Value acc;
    if (p < 0) { Value id = identity(n); acc = mldivide(I, base, id); value_release(id); p = -p; }
    else       acc = value_retain(base);

    Value result = identity(n);
    while (p > 0) {
        if (p & 1) { Value t = matmul(I, result, acc); value_release(result); result = t; }
        p >>= 1;
        if (p > 0) { Value t = matmul(I, acc, acc); value_release(acc); acc = t; }
    }
    value_release(acc);
    return result;
}

/* ------------------------------------------------------------------ */
/* literal decoding                                                    */
/* ------------------------------------------------------------------ */
int64_t parse_int_lit(const char *s, uint32_t len)
{
    if (len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        int64_t v = 0;
        for (uint32_t k = 2; k < len; k++) {
            char c = s[k]; if (c == '_') continue;
            int d = (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10;
            v = v * 16 + d;
        }
        return v;
    }
    if (len > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        int64_t v = 0;
        for (uint32_t k = 2; k < len; k++) { if (s[k] == '_') continue; v = v * 2 + (s[k] - '0'); }
        return v;
    }
    int64_t v = 0;
    for (uint32_t k = 0; k < len; k++) { if (s[k] == '_') continue; v = v * 10 + (s[k] - '0'); }
    return v;
}

double parse_float_lit(const char *s, uint32_t len)
{
    char buf[64]; uint32_t j = 0;
    for (uint32_t k = 0; k < len && j < sizeof buf - 1; k++)
        if (s[k] != '_') buf[j++] = s[k];
    buf[j] = '\0';
    return strtod(buf, nullptr);
}

Value decode_string(const char *s, uint32_t len)
{
    /* s includes the surrounding quotes */
    char *buf = malloc(len);                 /* decoded is never longer than raw */
    uint32_t j = 0;
    if (s[0] == '"') {
        for (uint32_t k = 1; k < len - 1; k++) {
            if (s[k] == '\\' && k + 1 < len - 1) {
                char e = s[++k];
                switch (e) {
                case 'n': buf[j++] = '\n'; break;  case 't': buf[j++] = '\t'; break;
                case 'r': buf[j++] = '\r'; break;  case '0': buf[j++] = '\0'; break;
                case '\\': buf[j++] = '\\'; break; case '"': buf[j++] = '"';  break;
                default:  buf[j++] = e;    break;
                }
            } else buf[j++] = s[k];
        }
    } else {                                  /* single-quoted raw, '' -> ' */
        for (uint32_t k = 1; k < len - 1; k++) {
            if (s[k] == '\'' && k + 1 < len - 1 && s[k+1] == '\'') { buf[j++] = '\''; k++; }
            else buf[j++] = s[k];
        }
    }
    Value v = val_string(buf, j);
    free(buf);
    return v;
}

/* ------------------------------------------------------------------ */
/* evaluator                                                           */
/* ------------------------------------------------------------------ */

Value call_value(Interp *I, Value callee, Value *args, uint32_t n)
{
    if (callee.kind == VAL_CLOSURE)
        return vm_run_closure(I, callee, args, n);   /* all closures are compiled */
    if (callee.kind == VAL_BUILTIN) {
        BuiltinObj *b = as_blt(callee);
        if (n < b->min_arity || n > b->max_arity)
            runtime_error(I, "%s: wrong number of arguments (%u)", b->name, n);
        return b->fn(I, args, n);
    }
    runtime_error(I, "value of type %s is not callable", type_name(callee.kind));
}

static int64_t want_index(Interp *I, Value v, const char *which)
{
    if (v.kind != VAL_INT)
        runtime_error(I, "%s index must be an Int, got %s", which, type_name(v.kind));
    return v.as.i;
}

/* non-consuming: target and idx[0..argc) stay owned by the caller. Result +1. */
/* Resolve one index argument to a list of 0-based positions within [0,dim).
 * colon -> the whole dimension; scalar Int -> one position (sets *scalar);
 * Int range/vector -> gather; logical vector -> the true positions. The result
 * is malloc'd (caller frees). No malloc happens before a possible raise, so a
 * failure here leaks nothing of its own. */
static int64_t *resolve_index_dim(Interp *I, Value idx, bool colon, int64_t dim,
                                  size_t *count, bool *scalar, const char *what)
{
    *scalar = false;
    if (colon) {
        int64_t *p = malloc(((size_t)(dim > 0 ? dim : 1)) * sizeof *p);
        for (int64_t k = 0; k < dim; k++) p[k] = k;
        *count = (size_t)dim;
        return p;
    }
    if (is_array(idx)) {
        ArrObj *ix = as_arr(idx);
        size_t n = (size_t)ix->rows * ix->cols;
        if (ix->elt == ELT_BOOL) {
            if ((int64_t)n != dim)
                runtime_error(I, "logical %s index has %zu element(s) but dimension is %lld",
                              what, n, (long long)dim);
            size_t cnt = 0;
            for (size_t k = 0; k < n; k++) if (((unsigned char *)ix->data)[k]) cnt++;
            int64_t *p = malloc((cnt ? cnt : 1) * sizeof *p);
            size_t w = 0;
            for (size_t k = 0; k < n; k++) if (((unsigned char *)ix->data)[k]) p[w++] = (int64_t)k;
            *count = cnt;
            return p;
        }
        if (ix->elt != ELT_INT)
            runtime_error(I, "%s index array must be Int or logical, got %s",
                          what, ix->elt == ELT_FLOAT ? "Float" : "Complex");
        const int64_t *src = (const int64_t *)ix->data;
        int64_t *p = malloc((n ? n : 1) * sizeof *p);
        for (size_t k = 0; k < n; k++) {
            int64_t v = src[k];
            if (v < 1 || v > dim) { free(p);
                runtime_error(I, "%s index %lld out of bounds (1..%lld)", what, (long long)v, (long long)dim); }
            p[k] = v - 1;
        }
        *count = n;
        return p;
    }
    int64_t v = want_index(I, idx, what);          /* scalar Int (may raise; nothing malloc'd yet) */
    if (v < 1 || v > dim)
        runtime_error(I, "%s index %lld out of bounds (1..%lld)", what, (long long)v, (long long)dim);
    int64_t *p = malloc(sizeof *p);
    p[0] = v - 1; *count = 1; *scalar = true;
    return p;
}

/* Fast path for a plain scalar Int index: 0-based offset with bounds check,
 * no allocation. Caller guarantees the index is neither a colon nor an array. */
static int64_t scalar_ix(Interp *I, Value v, int64_t dim, const char *what)
{
    int64_t i = want_index(I, v, what);            /* raises on non-Int */
    if (i < 1 || i > dim)
        runtime_error(I, "%s index %lld out of bounds (1..%lld)", what, (long long)i, (long long)dim);
    return i - 1;
}

Value do_index(Interp *I, Value target, Value *idx, uint32_t argc, uint8_t colonmask)
{
    if (!is_array(target))
        runtime_error(I, "value of type %s is not indexable", type_name(target.kind));
    ArrObj *a = as_arr(target);

    int64_t *sel0 = nullptr, *sel1 = nullptr;
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp)) { free(sel0); free(sel1);
        memcpy(I->jmp, saved, sizeof(jmp_buf)); longjmp(I->jmp, 1); }

    Value result;
    if (argc == 1) {
        int64_t numel = (int64_t)a->rows * a->cols;
        if (!(colonmask & 1) && !is_array(idx[0])) {          /* fast path: a[i] */
            result = arr_get(a, (size_t)scalar_ix(I, idx[0], numel, ""));
        } else {
        size_t cnt; bool scalar;
        sel0 = resolve_index_dim(I, idx[0], colonmask & 1, numel, &cnt, &scalar, "");
        if (scalar) { result = arr_get(a, (size_t)sel0[0]); }
        else {
            uint32_t orows, ocols;
            if (colonmask & 1)                         { orows = (uint32_t)cnt; ocols = cnt ? 1 : 0; }      /* a[:] -> column */
            else if (is_array(idx[0]) && as_arr(idx[0])->elt == ELT_BOOL)                                   /* mask -> follow target */
                { orows = a->rows == 1 ? (cnt ? 1 : 0) : (uint32_t)cnt; ocols = a->rows == 1 ? (uint32_t)cnt : (cnt ? 1 : 0); }
            else if (is_array(idx[0]))                 { orows = as_arr(idx[0])->rows; ocols = as_arr(idx[0])->cols; } /* gather -> index shape */
            else                                       { orows = 1; ocols = (uint32_t)cnt; }
            result = val_array(a->elt, orows, ocols);
            for (size_t k = 0; k < cnt; k++) arr_set(as_arr(result), k, arr_get(a, (size_t)sel0[k]));
        }
        }
    } else if (argc == 2) {
        if (!(colonmask & 1) && !is_array(idx[0]) && !(colonmask & 2) && !is_array(idx[1])) {   /* fast path: a[i, j] */
            int64_t r0 = scalar_ix(I, idx[0], a->rows, "row");
            int64_t c0 = scalar_ix(I, idx[1], a->cols, "column");
            result = arr_get(a, (size_t)r0 * a->cols + (size_t)c0);
        } else {
        size_t rc, cc; bool rs, cs;
        sel0 = resolve_index_dim(I, idx[0], colonmask & 1, a->rows, &rc, &rs, "row");
        sel1 = resolve_index_dim(I, idx[1], colonmask & 2, a->cols, &cc, &cs, "column");
        if (rs && cs) { result = arr_get(a, (size_t)sel0[0] * a->cols + (size_t)sel1[0]); }
        else {
            result = val_array(a->elt, (uint32_t)rc, (uint32_t)cc);
            for (size_t r = 0; r < rc; r++)
                for (size_t cl = 0; cl < cc; cl++)
                    arr_set(as_arr(result), r * cc + cl, arr_get(a, (size_t)sel0[r] * a->cols + (size_t)sel1[cl]));
        }
        }
    } else {
        runtime_error(I, "arrays take 1 or 2 indices, got %u", argc);
    }

    memcpy(I->jmp, saved, sizeof(jmp_buf));
    free(sel0); free(sel1);
    return result;
}

static int elt_rank(EltType e)
{
    switch (e) { case ELT_BOOL: return 0; case ELT_INT: return 1;
                 case ELT_FLOAT: return 2; case ELT_COMPLEX: return 3; }
    return 0;
}

static EltType scalar_elt(Interp *I, Value v)
{
    switch (v.kind) {
    case VAL_BOOL:    return ELT_BOOL;
    case VAL_INT:     return ELT_INT;
    case VAL_FLOAT:   return ELT_FLOAT;
    case VAL_COMPLEX: return ELT_COMPLEX;
    default: runtime_error(I, "cannot assign a value of type %s into an array", type_name(v.kind));
    }
}

/* a[idx] = value, with copy-on-write. The result is the updated array. If the
 * target array is uniquely owned (rc == 2: its name binding plus the operand-
 * stack reference) and no element-type promotion is needed, it is mutated in
 * place; otherwise a fresh array is built so aliases (b = a; b[i] = x) are
 * unaffected. value may be a scalar (broadcast) or an array (element-wise). */
Value do_index_set(Interp *I, Value target, Value *idx, uint32_t argc,
                   uint8_t colonmask, Value value)
{
    if (!is_array(target))
        runtime_error(I, "value of type %s is not indexable", type_name(target.kind));
    ArrObj *a = as_arr(target);

    int64_t *sel0 = nullptr, *sel1 = nullptr;
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp)) { free(sel0); free(sel1);
        memcpy(I->jmp, saved, sizeof(jmp_buf)); longjmp(I->jmp, 1); }

    bool rhs_arr = is_array(value);
    ArrObj *vr   = rhs_arr ? as_arr(value) : nullptr;
    size_t  vcnt = rhs_arr ? (size_t)vr->rows * vr->cols : 1;
    EltType velt = rhs_arr ? vr->elt : scalar_elt(I, value);

    /* resolve selectors and count the addressed positions */
    size_t rc, cc; bool rs, cs;
    if (argc == 1) {
        sel0 = resolve_index_dim(I, idx[0], colonmask & 1, (int64_t)a->rows * a->cols, &rc, &rs, "");
        cc = 1; (void)cs;
    } else if (argc == 2) {
        sel0 = resolve_index_dim(I, idx[0], colonmask & 1, a->rows, &rc, &rs, "row");
        sel1 = resolve_index_dim(I, idx[1], colonmask & 2, a->cols, &cc, &cs, "column");
    } else {
        runtime_error(I, "arrays take 1 or 2 indices, got %u", argc);
    }
    size_t ntarget = (argc == 1) ? rc : rc * cc;

    if (rhs_arr && vcnt != ntarget)
        runtime_error(I, "assignment size mismatch: %zu position(s) but %zu value(s)", ntarget, vcnt);

    /* element type of the result; promotion forces a fresh array */
    EltType relt = elt_rank(velt) > elt_rank(a->elt) ? velt : a->elt;
    bool unique  = (a->obj.rc == 2);

    Value result; ArrObj *dst;
    if (unique && relt == a->elt) {
        result = value_retain(target);
        dst = a;
    } else {
        result = val_array(relt, a->rows, a->cols);
        dst = as_arr(result);
        size_t tot = (size_t)a->rows * a->cols;
        for (size_t k = 0; k < tot; k++) arr_set(dst, k, arr_get(a, k));   /* copy (coerced to relt) */
    }

    if (argc == 1) {
        for (size_t k = 0; k < rc; k++)
            arr_set(dst, (size_t)sel0[k], rhs_arr ? arr_get(vr, k) : value);
    } else {
        size_t k = 0;
        for (size_t r = 0; r < rc; r++)
            for (size_t cl = 0; cl < cc; cl++, k++)
                arr_set(dst, (size_t)sel0[r] * a->cols + (size_t)sel1[cl],
                        rhs_arr ? arr_get(vr, k) : value);
    }

    memcpy(I->jmp, saved, sizeof(jmp_buf));
    free(sel0); free(sel1);
    return result;
}

static uint32_t elem_rows(Value v) { return is_array(v) ? as_arr(v)->rows : 1; }
static uint32_t elem_cols(Value v) { return is_array(v) ? as_arr(v)->cols : 1; }
static Value    elem_at(Value v, uint32_t i, uint32_t j)
{
    if (is_array(v)) return arr_get(as_arr(v), (size_t)i * as_arr(v)->cols + j);
    return v;   /* scalar occupies (0,0) */
}

/* non-consuming: ev[0..sum(rowcounts)) stay owned by the caller; nrows rows,
 * rowcounts[r] elements in row r (row-major in ev). Result +1. Frees its own C
 * scratch before any raise and never touches ev. */
Value build_matrix(Interp *I, Value *ev, uint32_t nrows, const int64_t *rowcounts)
{
    if (nrows == 0) return val_array(ELT_INT, 0, 0);

    uint32_t ntot = 0;
    for (uint32_t r = 0; r < nrows; r++) ntot += (uint32_t)rowcounts[r];

    bool saw_bool = false, saw_num = false;
    EltType numelt = ELT_INT;
    for (uint32_t k = 0; k < ntot; k++) {
        Value e = ev[k];
        if (!is_num(e) && e.kind != VAL_BOOL && !is_array(e))
            runtime_error(I, "matrix elements must be numbers or matrices, got %s", type_name(e.kind));
        EltType ee = is_array(e) ? as_arr(e)->elt : vk_elt(e.kind);
        if (ee == ELT_BOOL) saw_bool = true;
        else { saw_num = true; numelt = elt_max(numelt, ee); }
    }
    if (saw_bool && saw_num)
        runtime_error(I, "cannot mix Bool and numeric elements in a matrix");
    EltType elt = saw_bool ? ELT_BOOL : numelt;

    uint32_t *rowh = malloc(nrows * sizeof *rowh);
    if (!rowh) abort();
    uint32_t out_h = 0, out_w = 0, idx = 0;
    bool have_w = false;
    for (uint32_t r = 0; r < nrows; r++) {
        uint32_t nc = (uint32_t)rowcounts[r];
        uint32_t rh = 0, rw = 0;
        for (uint32_t c = 0; c < nc; c++) {
            Value e = ev[idx + c];
            uint32_t er = elem_rows(e), ec = elem_cols(e);
            if (c == 0) rh = er;
            else if (er != rh) {
                free(rowh);
                runtime_error(I, "row %u: elements have mismatched heights (%u vs %u)", r + 1, rh, er);
            }
            rw += ec;
        }
        rowh[r] = rh;
        if (nc != 0) {
            if (!have_w) { out_w = rw; have_w = true; }
            else if (rw != out_w) {
                free(rowh);
                runtime_error(I, "row %u has width %u, expected %u", r + 1, rw, out_w);
            }
        }
        out_h += rh;
        idx += nc;
    }

    Value result = val_array(elt, out_h, out_w);
    ArrObj *R = as_arr(result);
    idx = 0;
    uint32_t out_row = 0;
    for (uint32_t r = 0; r < nrows; r++) {
        uint32_t nc = (uint32_t)rowcounts[r];
        uint32_t out_col = 0;
        for (uint32_t c = 0; c < nc; c++) {
            Value e = ev[idx + c];
            uint32_t er = elem_rows(e), ec = elem_cols(e);
            for (uint32_t i = 0; i < er; i++)
                for (uint32_t j = 0; j < ec; j++)
                    arr_set(R, (size_t)(out_row + i) * out_w + (out_col + j), elem_at(e, i, j));
            out_col += ec;
        }
        out_row += rowh[r];
        idx += nc;
    }
    free(rowh);
    return result;
}

Value make_range(Interp *I, Value sv, Value ev, Value stv)   /* non-consuming; result is +1 */
{
    if (!is_num(sv) || !is_num(ev) || !is_num(stv))
        runtime_error(I, "range bounds must be numbers");

    bool all_int = sv.kind == VAL_INT && ev.kind == VAL_INT && stv.kind == VAL_INT;
    Value out;
    if (all_int) {
        int64_t s = sv.as.i, st = stv.as.i, e = ev.as.i;
        if (st == 0) runtime_error(I, "range step cannot be zero");
        int64_t count = 0;
        if (st > 0 && e >= s) count = (e - s) / st + 1;
        else if (st < 0 && e <= s) count = (s - e) / (-st) + 1;
        out = val_array(ELT_INT, 1, (uint32_t)count);
        for (int64_t k = 0; k < count; k++) ((int64_t *)as_arr(out)->data)[k] = s + k * st;
    } else {
        double s = as_double(sv), st = as_double(stv), e = as_double(ev);
        if (st == 0.0) runtime_error(I, "range step cannot be zero");
        int64_t count = 0;
        double span = (e - s) / st;
        if (span >= 0) count = (int64_t)(span + 1e-9) + 1;
        out = val_array(ELT_FLOAT, 1, (uint32_t)count);
        for (int64_t k = 0; k < count; k++) ((double *)as_arr(out)->data)[k] = s + (double)k * st;
    }
    return out;
}

Value apply_binop(Interp *I, enum TokenKind op, Value a, Value b)
{
    if (is_array(a) || is_array(b)) return array_binop(I, op, a, b);
    switch (op) {
    case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
        return scalar_cmp(I, op, a, b);
    case TOK_AMP: case TOK_PIPE:
        if (a.kind != VAL_BOOL || b.kind != VAL_BOOL)
            runtime_error(I, "'%s' requires Bool operands", op == TOK_AMP ? "&" : "|");
        return val_bool(op == TOK_AMP ? (a.as.b && b.as.b) : (a.as.b || b.as.b));
    default:
        return scalar_arith_k(I, arith_of(op), a, b);
    }
}

Value apply_unary(Interp *I, enum TokenKind op, Value v)   /* non-consuming; result is +1 */
{
    switch (op) {
    case TOK_PLUS:
        if (!is_num(v) && !is_array(v)) runtime_error(I, "unary '+' on %s", type_name(v.kind));
        return value_retain(v);   /* already owned by caller; hand back a fresh ref */
    case TOK_MINUS: {
        Value z = val_int(0);
        if (is_array(v)) return elementwise(I, AR_SUB, z, v);
        if (is_num(v))   return scalar_arith_k(I, AR_SUB, z, v);
        runtime_error(I, "unary '-' on %s", type_name(v.kind));
    }
    case TOK_BANG: case TOK_TILDE:
        if (v.kind == VAL_BOOL) return val_bool(!v.as.b);
        if (is_array(v) && as_arr(v)->elt == ELT_BOOL) {
            ArrObj *a = as_arr(v);
            Value out = val_array(ELT_BOOL, a->rows, a->cols);
            ArrObj *o = as_arr(out);
            size_t nn = (size_t)a->rows * a->cols;
            for (size_t k = 0; k < nn; k++)
                ((unsigned char *)o->data)[k] = !((unsigned char *)a->data)[k];
            return out;
        }
        runtime_error(I, "logical-not requires Bool, got %s",
                      is_array(v) ? "a numeric array" : type_name(v.kind));
    default:
        runtime_error(I, "bad unary operator");
    }
}

/* Does this subtree reference '@'? Used to decide pipe semantics. Stops at a
 * nested '|>' RHS, which rebinds '@' to its own scope. */
/* x |> rhs : evaluate rhs with '@' bound to x.
 *   - bare callable (x |> f, x |> fn..)  ==>  f(x)
 *   - rhs mentions '@'                    ==>  '@' is x wherever it appears
 *   - rhs ignores '@'                     ==>  error (the piped value is dropped) */
/* ------------------------------------------------------------------ */
/* builtins                                                            */
/* ------------------------------------------------------------------ */
/* Shared error-path cleanup for builders holding a malloc'd Value scratch
 * array: on unwind, release the `done` already-built elements, free the
 * buffer, restore the saved jmp target, and re-raise. Never returns. */
[[noreturn]] static void array_build_abort(Interp *I, Value *tmp, size_t done, jmp_buf saved)
{
    for (size_t k = 0; k < done; k++) value_release(tmp[k]);
    free(tmp);
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    longjmp(I->jmp, 1);
}

static Value map_unary(Interp *I, Value v, Value (*f)(Interp *, Value))
{
    if (is_array(v)) {
        ArrObj *a = as_arr(v);
        size_t nn = (size_t)a->rows * a->cols;
        Value *tmp = nn ? malloc(nn * sizeof *tmp) : nullptr;
        jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
        volatile size_t done = 0;
        if (setjmp(I->jmp)) array_build_abort(I, tmp, done, saved);   /* an element raised */
        for (size_t k = 0; k < nn; k++) { Value e = arr_get(a, k); tmp[k] = f(I, e); done = k + 1; }
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        Value r = pack_array(tmp, nn, a->rows, a->cols);
        free(tmp);
        return r;
    }
    return f(I, v);
}

static void print_raw(Value v)
{
    if (v.kind == VAL_STRING) { StrObj *s = as_str(v); fwrite(s->data, 1, s->len, vout()); }
    else value_print(vout(), v);
}
static void print_raw_to(FILE *f, Value v)
{
    if (v.kind == VAL_STRING) { StrObj *s = as_str(v); fwrite(s->data, 1, s->len, f); }
    else value_print(f, v);
}

/* A placeholder spec: {:[-][width][.prec][f|e|g]} — all parts optional. */
typedef struct { int width; bool left; int prec; char conv; } HoleSpec;

/* If t[i..] starts a placeholder, parse it: fill *hs, return its total length.
 * Return 0 if not a placeholder, -1 if it opens like one but is malformed. */
static int parse_hole(const char *t, uint32_t len, uint32_t i, HoleSpec *hs)
{
    if (t[i] != '{') return 0;
    if (i + 1 < len && t[i+1] == '{') return 0;            /* '{{' escape, not a hole */
    uint32_t j = i + 1;
    *hs = (HoleSpec){ .width = 0, .left = false, .prec = -1, .conv = 0 };
    if (j < len && t[j] == '}') return 2;                  /* plain {} */
    if (j >= len || t[j] != ':') return -1;                /* '{x' — not a valid hole */
    j++;
    if (j < len && t[j] == '-') { hs->left = true; j++; }
    while (j < len && t[j] >= '0' && t[j] <= '9') { hs->width = hs->width * 10 + (t[j] - '0'); j++; }
    if (j < len && t[j] == '.') {
        j++; hs->prec = 0;
        while (j < len && t[j] >= '0' && t[j] <= '9') { hs->prec = hs->prec * 10 + (t[j] - '0'); j++; }
    }
    if (j < len && (t[j] == 'f' || t[j] == 'e' || t[j] == 'g')) { hs->conv = t[j]; j++; }
    if (j < len && t[j] == '}') return (int)(j - i + 1);
    return -1;
}

/* Emit one value under a spec: temporary format override, optional width pad. */
static void print_hole(Interp *I, Value v, const HoleSpec *hs)
{
    NumFmtStyle ss; int sp; bool st;
    value_format_get(&ss, &sp, &st);
    if (hs->conv || hs->prec >= 0) {
        NumFmtStyle style = hs->conv == 'f' ? NFMT_F : hs->conv == 'e' ? NFMT_E
                          : hs->conv == 'g' ? NFMT_G : ss;
        value_format_set(style, hs->prec >= 0 ? hs->prec : sp);   /* trailing on */
    }
    if (hs->width > 0) {                                   /* render, then justify */
        char *buf = nullptr; size_t sz = 0;
        FILE *ms = open_memstream(&buf, &sz);
        if (!ms) { value_format_restore(ss, sp, st); runtime_error(I, "print: out of memory"); }
        print_raw_to(ms, v);
        fclose(ms);
        fprintf(vout(), "%*s", hs->left ? -hs->width : hs->width, buf ? buf : "");
        free(buf);
    } else {
        print_raw(v);
    }
    value_format_restore(ss, sp, st);
}

static Value bi_print(Interp *I, Value *args, uint32_t n)
{
    if (n >= 1 && args[0].kind == VAL_STRING && memchr(as_str(args[0])->data, '{', as_str(args[0])->len)) {
        StrObj *t = as_str(args[0]);                    /* template mode: "a {} b {:.3f}" */
        uint32_t holes = 0;                             /* validate before any output */
        HoleSpec hs;
        for (uint32_t i = 0; i < t->len; i++) {
            if (t->data[i] == '{' && i + 1 < t->len && t->data[i+1] == '{') { i++; continue; }
            if (t->data[i] == '}' && i + 1 < t->len && t->data[i+1] == '}') { i++; continue; }
            int hl = parse_hole(t->data, t->len, i, &hs);
            if (hl < 0) runtime_error(I, "print: malformed placeholder (use {}, {:.3f}, {:8}, {:e}, ...)");
            if (hl > 0) { holes++; i += (uint32_t)hl - 1; }
        }
        if (holes > n - 1) runtime_error(I, "print: %u {} placeholder(s) but only %u argument(s)", holes, n - 1);
        if (holes < n - 1) runtime_error(I, "print: %u argument(s) without a {} placeholder", (n - 1) - holes);
        uint32_t next = 1;
        for (uint32_t i = 0; i < t->len; i++) {
            char ch = t->data[i];
            if (ch == '{' && i + 1 < t->len && t->data[i+1] == '{') { fputc('{', vout()); i++; continue; }
            if (ch == '}' && i + 1 < t->len && t->data[i+1] == '}') { fputc('}', vout()); i++; continue; }
            int hl = parse_hole(t->data, t->len, i, &hs);
            if (hl > 0) { print_hole(I, args[next++], &hs); i += (uint32_t)hl - 1; continue; }
            fputc(ch, vout());
        }
        fputc('\n', vout());
        return val_null();
    }
    for (uint32_t i = 0; i < n; i++) { if (i) fputc(' ', vout()); print_raw(args[i]); }
    fputc('\n', vout());
    return val_null();
}

/* ---- axis-aware reductions (sum/prod/mean/any/all/min/max with a dim) ---- */
static Value sc_min(Interp *I, Value a, Value b);   /* defined later */
static Value sc_max(Interp *I, Value a, Value b);
static Value numify(Value e);
static bool  elt_nonzero(Value e);
static int64_t as_count(Interp *I, Value v, const char *name);

static Value fold_add(Interp *I, Value a, Value x) { return scalar_arith_k(I, AR_ADD, a, numify(x)); }
static Value fold_mul(Interp *I, Value a, Value x) { return scalar_arith_k(I, AR_MUL, a, numify(x)); }
static Value fold_min(Interp *I, Value a, Value x) { return sc_min(I, a, numify(x)); }
static Value fold_max(Interp *I, Value a, Value x) { return sc_max(I, a, numify(x)); }
static Value fold_any(Interp *I, Value a, Value x) { (void)I; return val_bool(a.as.b || elt_nonzero(x)); }
static Value fold_all(Interp *I, Value a, Value x) { (void)I; return val_bool(a.as.b && elt_nonzero(x)); }

/* Reduce `a` along dim (1 = down columns -> 1xC, 2 = across rows -> Rx1).
 * If `init` is Null, each strip is seeded from its first element (for min/max). */
static Value reduce_dim(Interp *I, ArrObj *a, int dim, Value init,
                        Value (*fold)(Interp *, Value, Value))
{
    uint32_t R = a->rows, C = a->cols;
    bool seed = init.kind == VAL_NULL;
    if (seed && ((dim == 1 && R == 0) || (dim == 2 && C == 0)))
        runtime_error(I, "reduction over an empty dimension");
    uint32_t outn = (dim == 1) ? C : R;
    Value *tmp = malloc(sizeof(Value) * (outn ? outn : 1));
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    volatile size_t done = 0;
    if (setjmp(I->jmp)) array_build_abort(I, tmp, done, saved);   /* a fold raised */
    if (dim == 1) {                                   /* one result per column */
        for (uint32_t c = 0; c < C; c++) {
            Value acc; uint32_t r0 = 0;
            if (seed) { acc = numify(arr_get(a, c)); r0 = 1; } else acc = init;
            for (uint32_t r = r0; r < R; r++) acc = fold(I, acc, arr_get(a, (size_t)r * C + c));
            tmp[c] = acc; done = c + 1;
        }
    } else {                                          /* one result per row */
        for (uint32_t r = 0; r < R; r++) {
            Value acc; uint32_t c0 = 0;
            if (seed) { acc = numify(arr_get(a, (size_t)r * C)); c0 = 1; } else acc = init;
            for (uint32_t c = c0; c < C; c++) acc = fold(I, acc, arr_get(a, (size_t)r * C + c));
            tmp[r] = acc; done = r + 1;
        }
    }
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    Value out = (dim == 1) ? pack_array(tmp, C, 1, C) : pack_array(tmp, R, R, 1);
    free(tmp);
    return out;
}

static int dim_arg(Interp *I, Value v, const char *name)
{
    int64_t d = as_count(I, v, name);
    if (d != 1 && d != 2) runtime_error(I, "%s: dim must be 1 (columns) or 2 (rows)", name);
    return (int)d;
}

static Value bi_sum(Interp *I, Value *args, uint32_t n)
{
    Value a = args[0];
    if (n == 2) {
        if (!is_array(a)) runtime_error(I, "sum: the dim form needs an array");
        return reduce_dim(I, as_arr(a), dim_arg(I, args[1], "sum"), val_int(0), fold_add);
    }
    if (is_num(a)) return value_retain(a);
    if (!is_array(a)) runtime_error(I, "sum: expected Array, got %s", type_name(a.kind));
    ArrObj *arr = as_arr(a);
    size_t nn = (size_t)arr->rows * arr->cols;
    if (arr->elt == ELT_BOOL) {                       /* sum of a logical array = count of trues */
        int64_t cnt = 0;
        for (size_t k = 0; k < nn; k++) cnt += ((unsigned char *)arr->data)[k] != 0;
        return val_int(cnt);
    }
    Value acc = val_int(0);
    for (size_t k = 0; k < nn; k++) {
        Value e = arr_get(arr, k);
        Value s = scalar_arith_k(I, AR_ADD, acc, e);
        acc = s;
    }
    return acc;
}

static Value bi_size(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n;
    uint32_t rows = 1, cols = 1;
    if (is_array(args[0])) { rows = as_arr(args[0])->rows; cols = as_arr(args[0])->cols; }
    Value out = val_array(ELT_INT, 1, 2);
    ((int64_t *)as_arr(out)->data)[0] = rows;
    ((int64_t *)as_arr(out)->data)[1] = cols;
    return out;
}

static Value bi_map(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    Value f = args[0], a = args[1];
    if (!is_array(a)) runtime_error(I, "map: second argument must be an Array");
    ArrObj *arr = as_arr(a);
    size_t nn = (size_t)arr->rows * arr->cols;
    Value *tmp = nn ? malloc(nn * sizeof *tmp) : nullptr;

    /* If an element call raises, free the partial result and re-raise — array
     * elements are immediate scalars, so only the computed results leak. */
    jmp_buf saved;
    memcpy(saved, I->jmp, sizeof(jmp_buf));
    volatile size_t done = 0;
    if (setjmp(I->jmp)) array_build_abort(I, tmp, done, saved);
    for (size_t k = 0; k < nn; k++) {
        Value e = arr_get(arr, k);
        Value one[1] = { e };
        Value mapped = call_value(I, f, one, 1);
        value_release(e);
        tmp[k] = mapped;
        done = k + 1;
    }
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    Value r = pack_array(tmp, nn, arr->rows, arr->cols);
    free(tmp);
    return r;
}

static Value abs_scalar(Interp *I, Value v)
{
    switch (v.kind) {
    case VAL_INT:     return val_int(v.as.i < 0 ? -v.as.i : v.as.i);
    case VAL_FLOAT:   return val_float(fabs(v.as.f));
    case VAL_COMPLEX: return val_float(hypot(v.as.z.re, v.as.z.im));
    default:          runtime_error(I, "abs: expected a number, got %s", type_name(v.kind));
    }
}

static Value sqrt_scalar(Interp *I, Value v)
{
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT) {
        double d = as_double(v);
        if (d >= 0) return val_float(sqrt(d));
        return val_complex(0.0, sqrt(-d));        /* tower: sqrt of a negative real is complex */
    }
    if (v.kind == VAL_COMPLEX) {
        double x = v.as.z.re, y = v.as.z.im, m = hypot(x, y);
        double re = sqrt((m + x) / 2.0);
        double im = sqrt((m - x) / 2.0);
        if (y < 0) im = -im;
        return val_complex(re, im);
    }
    runtime_error(I, "sqrt: expected a number, got %s", type_name(v.kind));
}

static Value bi_abs(Interp *I, Value *args, uint32_t n)  { (void)n; return map_unary(I, args[0], abs_scalar); }
static Value bi_sqrt(Interp *I, Value *args, uint32_t n) { (void)n; return map_unary(I, args[0], sqrt_scalar); }

static Value fill_array(Interp *I, Value *args, double fill)
{
    if (args[0].kind != VAL_INT || args[1].kind != VAL_INT)
        runtime_error(I, "dimensions must be Int");
    int64_t r = args[0].as.i, c = args[1].as.i;
    if (r < 0 || c < 0) runtime_error(I, "dimensions must be non-negative");
    Value out = val_array(ELT_FLOAT, (uint32_t)r, (uint32_t)c);
    size_t nn = (size_t)r * c;
    for (size_t k = 0; k < nn; k++) ((double *)as_arr(out)->data)[k] = fill;
    return out;
}

static Value bi_zeros(Interp *I, Value *args, uint32_t n) { (void)n; return fill_array(I, args, 0.0); }
static Value bi_ones(Interp *I, Value *args, uint32_t n)  { (void)n; return fill_array(I, args, 1.0); }

static Value bi_any(Interp *I, Value *args, uint32_t n)
{
    Value a = args[0];
    if (n == 2) {
        if (!is_array(a)) runtime_error(I, "any: the dim form needs an array");
        return reduce_dim(I, as_arr(a), dim_arg(I, args[1], "any"), val_bool(false), fold_any);
    }
    if (a.kind == VAL_BOOL) return val_bool(a.as.b);
    if (is_array(a) && as_arr(a)->elt == ELT_BOOL) {
        ArrObj *x = as_arr(a); size_t t = (size_t)x->rows * x->cols;
        for (size_t k = 0; k < t; k++) if (((unsigned char *)x->data)[k]) return val_bool(true);
        return val_bool(false);
    }
    runtime_error(I, "any: expected a Bool or logical array, got %s", type_name(a.kind));
}

static Value bi_all(Interp *I, Value *args, uint32_t n)
{
    Value a = args[0];
    if (n == 2) {
        if (!is_array(a)) runtime_error(I, "all: the dim form needs an array");
        return reduce_dim(I, as_arr(a), dim_arg(I, args[1], "all"), val_bool(true), fold_all);
    }
    if (a.kind == VAL_BOOL) return val_bool(a.as.b);
    if (is_array(a) && as_arr(a)->elt == ELT_BOOL) {
        ArrObj *x = as_arr(a); size_t t = (size_t)x->rows * x->cols;
        for (size_t k = 0; k < t; k++) if (!((unsigned char *)x->data)[k]) return val_bool(false);
        return val_bool(true);
    }
    runtime_error(I, "all: expected a Bool or logical array, got %s", type_name(a.kind));
}

typedef struct { const char *name, *sig, *desc, *cat; } BuiltinDoc;

static const BuiltinDoc builtin_docs[] = {
    /* core ------------------------------------------------------------ */
    { "print", "print(...) | print(tmpl, ...)", "print values; template fills {} in order; {:[-][w][.p][f|e|g]} formats a hole ({{ }} literal)", "core" },
    { "who",   "who",               "list the variables you have defined (name, type, shape)", "core" },
    { "help",  "help / help(f)",    "help lists every builtin; help(f) describes one", "core" },
    { "system","system(cmd)",       "run a shell command string; return its exit status", "core" },
    { "dis",   "dis(f)",            "disassemble a function's bytecode (compiler/VM introspection)", "core" },
    { "plot",  "plot(y) | plot(x, y) | plot(x, Y, opts)", "line plot via gnuplot; Y columns are series; opts: style string or {title, xlabel, ylabel, style, logx, logy, grid, xrange, yrange}", "plot" },
    { "hist",  "hist(y[, nbins][, opts])", "histogram via gnuplot; opts as in plot (yrange = [0, m] to anchor the axis)", "plot" },
    { "format","format / format(m)", "show or set number display: \"short\", \"long\", \"short e\", or a digit count", "core" },
    { "size",  "size(x)",           "[rows, cols] of x (a scalar is 1x1)", "core" },
    { "length","length(x)",         "longest dimension of x (0 if empty)", "core" },
    { "numel", "numel(x)",          "number of elements (rows*cols)", "core" },
    /* constructors ---------------------------------------------------- */
    { "zeros", "zeros(r, c)",        "r-by-c matrix of zeros", "make" },
    { "ones",  "ones(r, c)",         "r-by-c matrix of ones", "make" },
    { "eye",   "eye(n)",             "n-by-n identity matrix", "make" },
    { "diag",  "diag(x)",            "vector -> diagonal matrix; matrix -> its diagonal as a column", "make" },
    { "linspace","linspace(a, b, n)","row of n points evenly spaced from a to b inclusive", "make" },
    { "reshape","reshape(A, r, c)",  "reinterpret A's elements as r-by-c (row-major), element count must match", "make" },
    { "repmat","repmat(A, m, n)",    "tile A into an m-by-n grid of copies", "make" },
    /* reductions ------------------------------------------------------ */
    { "sum",   "sum(A) | sum(A, dim)", "sum of all elements, or along dim (1 = columns, 2 = rows)", "reduce" },
    { "prod",  "prod(A) | prod(A, dim)","product of all elements, or along dim", "reduce" },
    { "tic",   "tic",               "start the wall-clock timer (monotonic)", "core" },
    { "toc",   "toc",               "seconds elapsed since tic", "core" },
    { "unique","unique(A)",         "sorted distinct elements; vectors keep orientation, matrices flatten to a row", "array" },
    { "cov",   "cov(X[, w]) | cov(x, y[, w])", "covariance matrix of X's columns (rows = observations), or scalar cov of two vectors; w as in var", "reduce" },
    { "corr",  "corr(X) | corr(x, y)", "Pearson correlation matrix of X's columns, or scalar correlation of two vectors", "reduce" },
    { "var",   "var(A) | var(A, w) | var(A, w, dim)", "variance; w = 0 divides by N-1 (default), w = 1 by N", "reduce" },
    { "std",   "std(A) | std(A, w) | std(A, w, dim)", "standard deviation (sqrt of var, same normalization)", "reduce" },
    { "median","median(A) | median(A, dim)", "median of all elements, or along dim", "reduce" },
    { "quantile","quantile(x, p)",   "quantile(s) of the data at probability p (scalar or vector); linear interpolation", "reduce" },
    { "mean",  "mean(A) | mean(A, dim)","mean of all elements, or along dim", "reduce" },
    { "min",   "min(A) | min(a, b) | min(A, [], dim)", "smallest element; elementwise min; or min along dim", "reduce" },
    { "max",   "max(A) | max(a, b) | max(A, [], dim)", "largest element; elementwise max; or max along dim", "reduce" },
    { "any",   "any(mask) | any(mask, dim)", "true if any element is nonzero/true (overall or along dim)", "reduce" },
    { "all",   "all(mask) | all(mask, dim)", "true if every element is nonzero/true (overall or along dim)", "reduce" },
    /* array shaping --------------------------------------------------- */
    { "sort",  "sort(A)",            "ascending sort: a vector as a whole, a matrix by column", "array" },
    { "find",  "find(mask)",         "1-based positions of nonzero/true elements (row-major)", "array" },
    { "where", "where(mask) | where(mask, a, b)", "indices of true, or pick a where true and b where false", "array" },
    { "cumsum","cumsum(A)",          "cumulative sum along a vector, or down each column", "array" },
    { "cumprod","cumprod(A)",        "cumulative product along a vector, or down each column", "array" },
    { "diff",  "diff(A)",            "consecutive differences along a vector, or down each column", "array" },
    { "flipud","flipud(A)",          "reverse row order (flip up-down)", "array" },
    { "fliplr","fliplr(A)",          "reverse column order (flip left-right)", "array" },
    /* elementwise math ------------------------------------------------ */
    { "abs",   "abs(x)",             "absolute value, or complex magnitude", "math" },
    { "sqrt",  "sqrt(x)",            "square root (complex result for negative reals)", "math" },
    { "cbrt",  "cbrt(x)",            "real cube root", "math" },
    { "exp",   "exp(x)",             "e raised to the x (complex-aware)", "math" },
    { "log",   "log(x)",             "natural logarithm (complex for negatives)", "math" },
    { "ln",    "ln(x)",              "natural logarithm (alias for log)", "math" },
    { "log10", "log10(x)",           "base-10 logarithm (complex for negatives)", "math" },
    { "log2",  "log2(x)",            "base-2 logarithm (complex for negatives)", "math" },
    { "sign",  "sign(x)",            "-1 / 0 / +1 by sign; z/|z| for complex", "math" },
    { "floor", "floor(x)",           "round toward -infinity (componentwise on complex)", "math" },
    { "ceil",  "ceil(x)",            "round toward +infinity (componentwise on complex)", "math" },
    { "round", "round(x)",           "round to nearest (componentwise on complex)", "math" },
    { "trunc", "trunc(x)",           "round toward zero", "math" },
    { "hypot", "hypot(a, b)",        "sqrt(a^2 + b^2) without overflow (elementwise)", "math" },
    { "mod",   "mod(a, b)",          "modulo, result takes the sign of b (elementwise)", "math" },
    { "rem",   "rem(a, b)",          "remainder, result takes the sign of a (elementwise)", "math" },
    { "gamma", "gamma(x)",           "gamma function (real, elementwise)", "math" },
    { "erf",   "erf(x)",             "error function (real, elementwise)", "math" },
    { "erfc",  "erfc(x)",            "complementary error function 1 - erf(x)", "math" },
    { "beta",  "beta(a, b)",         "beta function (a, b > 0, elementwise)", "math" },
    { "lbeta", "lbeta(a, b)",        "log of the beta function", "math" },
    { "gammainc","gammainc(x, a)",   "regularized lower incomplete gamma P(a, x) (the chi^2 CDF)", "math" },
    { "betainc","betainc(x, a, b)",  "regularized incomplete beta I_x(a, b) (Student-t / F CDFs)", "math" },
    { "norminv","norminv(p)",        "standard normal quantile (inverse CDF)", "math" },
    { "digamma","digamma(x)",        "digamma psi(x) = d/dx log gamma(x)", "math" },
    { "besselj","besselj(n, x)",     "Bessel function of the first kind, integer order n", "math" },
    { "bessely","bessely(n, x)",     "Bessel function of the second kind, integer order n (x > 0)", "math" },
    { "kron",  "kron(A, B)",         "Kronecker product: (m x n) kron (p x q) -> (mp x nq)", "linalg" },
    { "lgamma","lgamma(x)",          "log of |gamma(x)| (real, elementwise)", "math" },
    /* trig ------------------------------------------------------------ */
    { "sin",   "sin(x)",  "sine (complex-aware, elementwise)", "trig" },
    { "cos",   "cos(x)",  "cosine (complex-aware, elementwise)", "trig" },
    { "tan",   "tan(x)",  "tangent (complex-aware, elementwise)", "trig" },
    { "asin",  "asin(x)", "arcsine (complex outside [-1, 1])", "trig" },
    { "acos",  "acos(x)", "arccosine (complex outside [-1, 1])", "trig" },
    { "atan",  "atan(x)", "arctangent (complex-aware)", "trig" },
    { "atan2", "atan2(y, x)", "two-argument arctangent (elementwise)", "trig" },
    { "sinh",  "sinh(x)", "hyperbolic sine (complex-aware)", "trig" },
    { "cosh",  "cosh(x)", "hyperbolic cosine (complex-aware)", "trig" },
    { "tanh",  "tanh(x)", "hyperbolic tangent (complex-aware)", "trig" },
    { "asinh", "asinh(x)","inverse hyperbolic sine (complex-aware)", "trig" },
    { "acosh", "acosh(x)","inverse hyperbolic cosine (complex below 1)", "trig" },
    { "atanh", "atanh(x)","inverse hyperbolic tangent (complex outside (-1, 1))", "trig" },
    /* complex --------------------------------------------------------- */
    { "real",  "real(z)", "real part (elementwise)", "complex" },
    { "imag",  "imag(z)", "imaginary part (elementwise)", "complex" },
    { "conj",  "conj(z)", "complex conjugate (elementwise)", "complex" },
    { "angle", "angle(z)","argument atan2(im, re) (elementwise)", "complex" },
    { "arg",   "arg(z)",  "argument atan2(im, re) (alias for angle)", "complex" },
    /* linear algebra -------------------------------------------------- */
    { "dot",   "dot(a, b)",          "inner product of two vectors", "linalg" },
    { "norm",  "norm(x) | norm(x, p)","vector p-norm (p = 1 or 2, default 2); matrix Frobenius norm", "linalg" },
    { "trace", "trace(A)",           "sum of the diagonal", "linalg" },
    { "det",   "det(A)",             "determinant via LU", "linalg" },
    { "inv",   "inv(A)",             "matrix inverse (solves A \\ I)", "linalg" },
    { "lu",    "lu(A)",              "LU with partial pivoting -> {L, U, p}, so P*A = L*U", "linalg" },
    { "qr",    "qr(A)",              "Householder QR -> {Q, R} (real or complex)", "linalg" },
    { "chol",  "chol(A)",            "Cholesky factor L (lower), L*L' = A (SPD / Hermitian PD)", "linalg" },
    { "eig",   "eig(A)",             "eigendecomposition -> {values, vectors}; Hermitian (ascending real) or general (complex)", "linalg" },
    { "svd",   "svd(A)",             "thin SVD -> {U, S, V}, A = U*diag(S)*V' (S descending)", "linalg" },
    /* random ---------------------------------------------------------- */
    { "rng",   "rng(seed)",          "reseed the generator (xoshiro256**); same seed, same stream", "random" },
    { "rand",  "rand() | rand(n) | rand(r, c)", "uniform draws on [0, 1)", "random" },
    { "randn", "randn() | randn(n) | randn(r, c)", "standard-normal draws", "random" },
    { "randi", "randi(imax[, r, c]) | randi([lo, hi], ...)", "uniform random integers", "random" },
    /* predicates ------------------------------------------------------ */
    { "isnan",    "isnan(x)",    "elementwise test for NaN -> logical", "test" },
    { "isinf",    "isinf(x)",    "elementwise test for +/-Inf -> logical", "test" },
    { "isfinite", "isfinite(x)", "elementwise test for a finite value -> logical", "test" },
    /* higher-order ---------------------------------------------------- */
    { "map",   "map(f, A)",          "apply f to each element of A, returning an array of results", "hof" },
    };
static const size_t n_builtin_docs = sizeof builtin_docs / sizeof *builtin_docs;

static const BuiltinDoc *builtin_info(const char *name)
{
    for (size_t i = 0; i < n_builtin_docs; i++)
        if (strcmp(builtin_docs[i].name, name) == 0) return &builtin_docs[i];
    return nullptr;
}

static Value bi_format(Interp *I, Value *args, uint32_t n)
{
    if (n == 0) { fprintf(vout(), "format: %s\n", value_format_desc()); return val_null(); }
    Value a = args[0];
    if (a.kind == VAL_STRING) {
        StrObj *s = as_str(a);
        char name[32];
        if (s->len >= sizeof name || !value_format_by_name((memcpy(name, s->data, s->len), name[s->len] = '\0', name)))
            runtime_error(I, "format: unknown mode (try short, long, short e, long e, default)");
    } else if (a.kind == VAL_INT) {
        if (a.as.i < 1 || a.as.i > 17) runtime_error(I, "format: digit count must be 1..17");
        value_format_set(NFMT_G, (int)a.as.i);
    } else {
        runtime_error(I, "format: expected a mode string or a digit count, got %s", type_name(a.kind));
    }
    return val_null();
}

static Value bi_dis(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    Value v = args[0];
    if (v.kind == VAL_BUILTIN) { fprintf(vout(), "<builtin %s: native code, no bytecode>\n", as_blt(v)->name); return val_null(); }
    if (v.kind != VAL_CLOSURE) runtime_error(I, "dis: expected a function, got %s", type_name(v.kind));
    chunk_disassemble(vout(), as_clo(v)->chunk, "fn");
    return val_null();
}

/* ------------------------------------------------------------------ */
/* plotting (gnuplot, out of process)                                  */
/* ------------------------------------------------------------------ */

/* Look up a record field; null Value if absent. */
static Value rec_field(RecObj *r, const char *name)
{
    size_t len = strlen(name);
    for (uint32_t i = 0; i < r->count; i++)
        if (r->keylens[i] == len && memcmp(r->keys[i], name, len) == 0)
            return r->vals[i];
    return val_null();
}

/* Write s into gnuplot double quotes, escaping " and backslash. */
static void gp_qstr(FILE *g, const char *s, uint32_t len)
{
    fputc('"', g);
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') fputc('\\', g);
        if ((unsigned char)c >= 0x20 || c == '\t') fputc(c, g);
    }
    fputc('"', g);
}

static FILE *gp_open(Interp *I)
{
    FILE *g = popen("gnuplot -persist 2>/dev/null", "w");
    if (!g) runtime_error(I, "plot: could not start gnuplot");
    const char *term = getenv("NEUTRINO_PLOT_TERM");
    if (term && *term) {
        fprintf(g, "set terminal %s\n", term);
        const char *out = getenv("NEUTRINO_PLOT_OUT");
        if (out && *out) fprintf(g, "set output '%s'\n", out);
    }
    return g;
}

static void gp_close(Interp *I, FILE *g)
{
    int rc = pclose(g);
    if (rc != 0)
        runtime_error(I, "plot: gnuplot failed (exit %d) — is gnuplot installed?",
                      rc == -1 ? -1 : WEXITSTATUS(rc));
}

/* A vector argument for plotting: any 1 x n / n x 1 real array. */
static ArrObj *want_vec(Interp *I, Value v, const char *who)
{
    if (!is_array(v)) runtime_error(I, "%s: expected a vector, got %s", who, type_name(v.kind));
    ArrObj *a = as_arr(v);
    if (a->rows != 1 && a->cols != 1) runtime_error(I, "%s: expected a vector, got %ux%u", who, a->rows, a->cols);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "%s: complex data is not plottable directly (plot real/imag/abs)", who);
    if ((size_t)a->rows * a->cols == 0) runtime_error(I, "%s: empty data", who);
    return a;
}

/* Emit a "set xrange/yrange [lo:hi]" from a 2-element vector option. */
static void gp_range(Interp *I, FILE *g, const char *axis, Value v, const char *who)
{
    if (!is_array(v) || (size_t)as_arr(v)->rows * as_arr(v)->cols != 2 || as_arr(v)->elt == ELT_COMPLEX)
        runtime_error(I, "%s: %srange must be a 2-element vector [lo, hi]", who, axis);
    double lo = as_double(arr_get(as_arr(v), 0)), hi = as_double(arr_get(as_arr(v), 1));
    if (!(lo < hi)) runtime_error(I, "%s: %srange needs lo < hi", who, axis);
    fprintf(g, "set %srange [%.17g:%.17g]\n", axis, lo, hi);
}

/* Validate an options record before any gnuplot process exists, so option
 * errors cannot leak the popen stream. */
static void gp_check_range(Interp *I, Value v, const char *axis, const char *who)
{
    if (!is_array(v) || (size_t)as_arr(v)->rows * as_arr(v)->cols != 2 || as_arr(v)->elt == ELT_COMPLEX)
        runtime_error(I, "%s: %srange must be a 2-element vector [lo, hi]", who, axis);
    double lo = as_double(arr_get(as_arr(v), 0)), hi = as_double(arr_get(as_arr(v), 1));
    if (!(lo < hi)) runtime_error(I, "%s: %srange needs lo < hi", who, axis);
}
static void gp_check_opts(Interp *I, Value opts, const char *who)
{
    if (opts.kind != VAL_RECORD) return;
    RecObj *o = as_rec(opts);
    Value v;
    if ((v = rec_field(o, "xrange")).kind != VAL_NULL) gp_check_range(I, v, "x", who);
    if ((v = rec_field(o, "yrange")).kind != VAL_NULL) gp_check_range(I, v, "y", who);
}

/* Shared options record for plot/hist: title, xlabel, ylabel (strings);
 * logx, logy, grid (booleans); xrange, yrange ([lo, hi] vectors). */
static void gp_opts(Interp *I, FILE *g, Value opts)
{
    if (opts.kind != VAL_RECORD) return;
    RecObj *o = as_rec(opts);
    Value v;
    if ((v = rec_field(o, "title")).kind == VAL_STRING)  { fputs("set title ", g);  gp_qstr(g, as_str(v)->data, as_str(v)->len); fputc('\n', g); }
    if ((v = rec_field(o, "xlabel")).kind == VAL_STRING) { fputs("set xlabel ", g); gp_qstr(g, as_str(v)->data, as_str(v)->len); fputc('\n', g); }
    if ((v = rec_field(o, "ylabel")).kind == VAL_STRING) { fputs("set ylabel ", g); gp_qstr(g, as_str(v)->data, as_str(v)->len); fputc('\n', g); }
    if ((v = rec_field(o, "logx")).kind == VAL_BOOL && v.as.b) fputs("set logscale x\n", g);
    if ((v = rec_field(o, "logy")).kind == VAL_BOOL && v.as.b) fputs("set logscale y\n", g);
    if ((v = rec_field(o, "grid")).kind == VAL_BOOL && v.as.b) fputs("set grid\n", g);
    if ((v = rec_field(o, "xrange")).kind != VAL_NULL) gp_range(I, g, "x", v, "plot");
    if ((v = rec_field(o, "yrange")).kind != VAL_NULL) gp_range(I, g, "y", v, "plot");
}

/* plot(y) | plot(x, y) | plot(x, Y) — trailing string = style, trailing
 * record = {title, xlabel, ylabel, style, logx, logy, grid}. Y's columns
 * are separate series when Y is a matrix with matching rows. */
static Value bi_plot(Interp *I, Value *args, uint32_t n)
{
    Value opts = val_null(); const char *style = "lines"; uint32_t style_len = 5;
    if (n >= 2 && args[n-1].kind == VAL_STRING) {
        StrObj *s = as_str(args[n-1]);
        style = s->data; style_len = s->len; n--;
    } else if (n >= 2 && args[n-1].kind == VAL_RECORD) {
        opts = args[n-1]; n--;
        Value sv = rec_field(as_rec(opts), "style");
        if (sv.kind == VAL_STRING) { style = as_str(sv)->data; style_len = as_str(sv)->len; }
        else if (sv.kind != VAL_NULL) runtime_error(I, "plot: style must be a string");
    }
    if (n < 1 || n > 2) runtime_error(I, "plot: usage plot(y), plot(x, y), plot(x, Y[, style-or-opts])");

    ArrObj *X = nullptr, *Y;                      /* Y may be a matrix: columns are series */
    if (n == 2) X = want_vec(I, args[0], "plot");
    Value yv = args[n-1];
    if (!is_array(yv)) runtime_error(I, "plot: expected numeric data, got %s", type_name(yv.kind));
    Y = as_arr(yv);
    if (Y->elt == ELT_COMPLEX) runtime_error(I, "plot: complex data is not plottable directly");
    if ((size_t)Y->rows * Y->cols == 0) runtime_error(I, "plot: empty data");

    bool yvec = (Y->rows == 1 || Y->cols == 1);
    uint32_t npts   = yvec ? Y->rows * Y->cols : Y->rows;
    uint32_t nser   = yvec ? 1 : Y->cols;
    if (X) {
        uint32_t xn = X->rows * X->cols;
        if (xn != npts) runtime_error(I, "plot: x has %u points but y has %u", xn, npts);
    }

    gp_check_opts(I, opts, "plot");
    FILE *g = gp_open(I);
    gp_opts(I, g, opts);
    fputs("plot ", g);
    for (uint32_t s = 0; s < nser; s++) {
        if (s) fputs(", ", g);
        fprintf(g, "'-' using 1:2 with %.*s title \"series %u\"", (int)style_len, style, s + 1);
    }
    fputc('\n', g);
    for (uint32_t s = 0; s < nser; s++) {         /* one inline data block per series */
        for (uint32_t k = 0; k < npts; k++) {
            double x = X ? as_double(arr_get(X, k)) : (double)(k + 1);
            double y = yvec ? as_double(arr_get(Y, k))
                            : as_double(arr_get(Y, (size_t)k * Y->cols + s));
            fprintf(g, "%.17g %.17g\n", x, y);
        }
        fputs("e\n", g);
    }
    gp_close(I, g);
    return val_null();
}

/* hist(y[, nbins]) — histogram with boxes; default bin count by Sturges. */
static Value bi_hist(Interp *I, Value *args, uint32_t n)
{
    Value opts = val_null();
    if (n >= 2 && args[n-1].kind == VAL_RECORD) { opts = args[n-1]; n--; }
    gp_check_opts(I, opts, "hist");                    /* before any allocation */
    ArrObj *Y = want_vec(I, args[0], "hist");
    size_t nn = (size_t)Y->rows * Y->cols;
    int64_t nb = n >= 2 ? as_count(I, args[1], "hist") : (int64_t)(1.0 + log2((double)nn)) ;
    if (nb < 1 || nb > 100000) runtime_error(I, "hist: bin count out of range");
    double lo = as_double(arr_get(Y, 0)), hi = lo;
    for (size_t k = 1; k < nn; k++) {
        double v = as_double(arr_get(Y, k));
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (hi == lo) { lo -= 0.5; hi += 0.5; }
    double w = (hi - lo) / (double)nb;
    uint64_t *cnt = calloc((size_t)nb, sizeof *cnt);
    for (size_t k = 0; k < nn; k++) {
        double v = as_double(arr_get(Y, k));
        int64_t b = (int64_t)((v - lo) / w);
        if (b < 0) b = 0;
        if (b >= nb) b = nb - 1;
        cnt[b]++;
    }
    FILE *g = gp_open(I);
    gp_opts(I, g, opts);
    fprintf(g, "set boxwidth %.17g\nset style fill solid 0.6\n", w * 0.95);
    fputs("plot '-' using 1:2 with boxes title \"hist\"\n", g);
    for (int64_t b = 0; b < nb; b++)
        fprintf(g, "%.17g %llu\n", lo + (b + 0.5) * w, (unsigned long long)cnt[b]);
    fputs("e\n", g);
    free(cnt);
    gp_close(I, g);
    return val_null();
}

static Value bi_system(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (args[0].kind != VAL_STRING)
        runtime_error(I, "system: expected a command string, got %s", type_name(args[0].kind));
    StrObj *s = as_str(args[0]);
    char *cmd = malloc((size_t)s->len + 1);
    if (!cmd) runtime_error(I, "system: out of memory");
    memcpy(cmd, s->data, s->len);
    cmd[s->len] = '\0';
    fflush(stdout); fflush(vout()); fflush(stderr);  /* flush before the child writes to fd 1 */
    int rc = system(cmd);
    free(cmd);
    if (rc == -1) runtime_error(I, "system: could not start a shell");
    int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : WIFSIGNALED(rc) ? 128 + WTERMSIG(rc) : rc;
    return val_int(code);
}

static void who_describe(FILE *out, Value v)        /* compact type + shape/value column */
{
    switch (v.kind) {
    case VAL_BOOL:    fprintf(out, "bool       = %s", v.as.b ? "true" : "false"); break;
    case VAL_INT:     fprintf(out, "int        = %lld", (long long)v.as.i); break;
    case VAL_FLOAT:   fprintf(out, "float      = %g", v.as.f); break;
    case VAL_COMPLEX: fprintf(out, "complex    = %g%+gi", v.as.z.re, v.as.z.im); break;
    case VAL_STRING:  fprintf(out, "string     (%u chars)", as_str(v)->len); break;
    case VAL_ARRAY: {
        ArrObj *a = as_arr(v);
        fprintf(out, "array      %ux%u %s", a->rows, a->cols, elt_name(a->elt));
        break;
    }
    case VAL_RECORD:  fprintf(out, "record     (%u field%s)", as_rec(v)->count, as_rec(v)->count == 1 ? "" : "s"); break;
    case VAL_CLOSURE: fprintf(out, "function   (%u param%s)", as_clo(v)->chunk->nparams, as_clo(v)->chunk->nparams == 1 ? "" : "s"); break;
    case VAL_BUILTIN: fprintf(out, "builtin"); break;
    default:          fputs(type_name(v.kind), out); break;
    }
}

static Value bi_who(Interp *I, Value *args, uint32_t n)
{
    (void)args; (void)n;
    EnvObj *g = I->globals;
    uint32_t shown = 0;
    if (g)
        for (uint32_t i = 0; i < g->count; i++) {
            if (g->vals[i].kind == VAL_BUILTIN) continue;        /* user bindings only */
            fprintf(vout(), "  %-12.*s ", (int)g->namelens[i], g->names[i]);
            who_describe(vout(), g->vals[i]);
            fputc('\n', vout());
            shown++;
        }
    if (!shown) fputs("(no variables defined)\n", vout());
    return val_null();
}

static void help_arity(FILE *out, BuiltinObj *b)
{
    if (b->max_arity == UINT32_MAX)        fprintf(out, "%u or more arguments", b->min_arity);
    else if (b->min_arity == b->max_arity) fprintf(out, "%u argument%s", b->min_arity, b->min_arity == 1 ? "" : "s");
    else                                   fprintf(out, "%u to %u arguments", b->min_arity, b->max_arity);
}

static Value bi_help(Interp *I, Value *args, uint32_t n)
{
    (void)I;
    if (n == 1) {
        Value v = args[0];
        if (v.kind == VAL_BUILTIN) {
            BuiltinObj *b = as_blt(v);
            const BuiltinDoc *d = builtin_info(b->name);
            fprintf(vout(), "  %s\n", d ? d->sig : b->name);
            if (d) fprintf(vout(), "      %s\n", d->desc);
            fprintf(vout(), "      builtin, ");
            help_arity(vout(), b);
            fputc('\n', vout());
        } else if (v.kind == VAL_CLOSURE) {
            uint32_t np = as_clo(v)->chunk->nparams;
            fprintf(vout(), "  a function you defined, taking %u argument%s\n", np, np == 1 ? "" : "s");
        } else if (v.kind == VAL_ARRAY) {
            ArrObj *a = as_arr(v);
            fprintf(vout(), "  a %ux%u %s array\n", a->rows, a->cols, elt_name(a->elt));
        } else {
            fprintf(vout(), "  a %s value\n", type_name(v.kind));
        }
        return val_null();
    }

    /* help() — grouped catalogue of builtins, then a language cheat-sheet */
    static const struct { const char *cat, *label; } groups[] = {
        { "core",    "core" },        { "make",    "constructors" }, { "reduce",  "reductions" },
        { "array",   "arrays" },      { "math",    "math" },         { "trig",    "trigonometry" },
        { "complex", "complex" },     { "linalg",  "linear algebra" },{ "random",  "random" },
        { "test",    "predicates" },  { "hof",     "higher-order" },
    };
    fputs("Neutrino builtins  —  help(name) for detail, e.g. help(svd)\n\n", vout());
    for (size_t gi = 0; gi < sizeof groups / sizeof *groups; gi++) {
        fprintf(vout(), "  %-15s", groups[gi].label);
        int col = 0;
        for (size_t i = 0; i < n_builtin_docs; i++) {
            if (strcmp(builtin_docs[i].cat, groups[gi].cat) != 0) continue;
            if (col && col % 8 == 0) fprintf(vout(), "\n  %-15s", "");
            fprintf(vout(), " %s", builtin_docs[i].name);
            col++;
        }
        fputc('\n', vout());
    }
    fputs("\nLanguage\n", vout());
    fputs("  let x = v              bind a variable;  x = v reassigns an existing one\n", vout());
    fputs("  fn x -> expr           a function;  call f(x),  pipe  x |> f  or  x |> f(@)\n", vout());
    fputs("  if c then a else b end   for i = 1:n do .. end   while c do .. end\n", vout());
    fputs("  break  continue  return [v]    inside loops / functions\n", vout());
    fputs("  let x = v in expr      local binding (an expression)\n", vout());
    fputs("  A[i] / A[i, j] / A[:]  indexing (1-based; 'end' is the last index)\n", vout());
    fputs("  # comment    trailing ; hides a line's result\n", vout());
    fputs("  format short|long|\"short e\"   number display     more on|off   paging (REPL)\n", vout());
    fputs("  pretty on|off   aligned multi-line matrices (REPL)\n", vout());
    fputs("  !cmd   run a shell command (REPL)       system(\"cmd\")   run one, get its exit code\n", vout());
    return val_null();
}

/* ------------------------------------------------------------------ */
static void rng_seed(Interp *I, uint64_t seed);
void interp_init(Interp *I) { *I = (Interp){0}; rng_seed(I, 0x9E3779B97F4A7C15ULL); }

/* ------------------------------------------------------------------ */
/* linear algebra                                                      */
/* ------------------------------------------------------------------ */
/* extract a real matrix into a fresh double[rows*cols]; complex errors */
/* extract any real/complex array into a fresh Cplx[rows*cols]; the algorithms
 * below all compute in Cplx, so real inputs flow through unchanged (real
 * arithmetic keeps imaginary parts exactly zero) and complex inputs just work. */
static Cplx *to_cplx(Interp *I, Value v, uint32_t *rows, uint32_t *cols, const char *who)
{
    if (!is_array(v)) runtime_error(I, "%s: expected a matrix, got %s", who, type_name(v.kind));
    ArrObj *a = as_arr(v);
    *rows = a->rows; *cols = a->cols;
    size_t nn = (size_t)a->rows * a->cols;
    Cplx *d = malloc((nn ? nn : 1) * sizeof *d);
    for (size_t k = 0; k < nn; k++) {
        Value e = arr_get(a, k);
        d[k] = (e.kind == VAL_BOOL) ? (Cplx){ e.as.b ? 1.0 : 0.0, 0.0 } : as_cplx(e);
    }
    return d;
}
static Value from_cplx(const Cplx *d, uint32_t rows, uint32_t cols, bool real_out)
{
    Value out = val_array(real_out ? ELT_FLOAT : ELT_COMPLEX, rows, cols);
    size_t nn = (size_t)rows * cols;
    if (real_out) { double *o = (double *)as_arr(out)->data; for (size_t k = 0; k < nn; k++) o[k] = d[k].re; }
    else          { Cplx   *o = (Cplx   *)as_arr(out)->data; for (size_t k = 0; k < nn; k++) o[k] = d[k]; }
    return out;
}
static bool   arr_real(Value v)  { return as_arr(v)->elt != ELT_COMPLEX; }
static Cplx   c_conj(Cplx a)     { return (Cplx){ a.re, -a.im }; }
static double c_abs(Cplx a)      { return hypot(a.re, a.im); }
static Cplx   c_scale(double s, Cplx a) { return (Cplx){ s * a.re, s * a.im }; }
static Value record2(const char *k1, Value v1, const char *k2, Value v2)
{
    Value r = val_record(2); RecObj *o = as_rec(r);
    o->keys[0] = k1; o->keylens[0] = (uint32_t)strlen(k1); o->vals[0] = v1;
    o->keys[1] = k2; o->keylens[1] = (uint32_t)strlen(k2); o->vals[1] = v2;
    return r;
}
static Value record3(const char *k1, Value v1, const char *k2, Value v2, const char *k3, Value v3)
{
    Value r = val_record(3); RecObj *o = as_rec(r);
    o->keys[0] = k1; o->keylens[0] = (uint32_t)strlen(k1); o->vals[0] = v1;
    o->keys[1] = k2; o->keylens[1] = (uint32_t)strlen(k2); o->vals[1] = v2;
    o->keys[2] = k3; o->keylens[2] = (uint32_t)strlen(k3); o->vals[2] = v3;
    return r;
}
static double vmag(Value v)
{
    return v.kind == VAL_COMPLEX ? hypot(v.as.z.re, v.as.z.im) : fabs(as_double(v));
}

static Value bi_eye(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (args[0].kind != VAL_INT || args[0].as.i < 0) runtime_error(I, "eye: size must be a non-negative Int");
    return identity((uint32_t)args[0].as.i);
}

static Value bi_diag(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0])) runtime_error(I, "diag: expected an array");
    ArrObj *a = as_arr(args[0]);
    EltType et = a->elt == ELT_BOOL ? ELT_INT : a->elt;
    if (a->rows == 1 || a->cols == 1) {              /* vector -> diagonal matrix */
        uint32_t len = a->rows * a->cols;
        Value out = val_array(et, len, len);
        for (uint32_t k = 0; k < len; k++) arr_set(as_arr(out), (size_t)k * len + k, arr_get(a, k));
        return out;
    }
    uint32_t m = a->rows < a->cols ? a->rows : a->cols;  /* matrix -> diagonal vector */
    Value out = val_array(et, m, 1);
    for (uint32_t i = 0; i < m; i++) arr_set(as_arr(out), i, arr_get(a, (size_t)i * a->cols + i));
    return out;
}

static Value bi_trace(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0])) runtime_error(I, "trace: expected a matrix");
    ArrObj *a = as_arr(args[0]);
    uint32_t m = a->rows < a->cols ? a->rows : a->cols;
    Value acc = val_int(0);
    for (uint32_t i = 0; i < m; i++) {
        Value e = arr_get(a, (size_t)i * a->cols + i);
        acc = scalar_arith_k(I, AR_ADD, acc, e);
    }
    return acc;
}

static Value bi_det(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0])) runtime_error(I, "det: expected a square matrix");
    ArrObj *a = as_arr(args[0]);
    uint32_t N = a->rows;
    if (a->cols != N) runtime_error(I, "det: matrix must be square (got %ux%u)", a->rows, a->cols);
    bool real_in = a->elt != ELT_COMPLEX;
    if (N == 0) return val_int(1);
    Cplx *M = malloc((size_t)N * N * sizeof *M);
    for (size_t k = 0; k < (size_t)N * N; k++) M[k] = as_cplx(arr_get(a, k));
    Cplx det = { 1.0, 0.0 };
    int sign = 1;
    for (uint32_t k = 0; k < N; k++) {
        uint32_t p = k; double best = hypot(M[(size_t)k*N+k].re, M[(size_t)k*N+k].im);
        for (uint32_t i = k+1; i < N; i++) {
            double mg = hypot(M[(size_t)i*N+k].re, M[(size_t)i*N+k].im);
            if (mg > best) { best = mg; p = i; }
        }
        if (best == 0.0) { free(M); return real_in ? val_float(0.0) : val_complex(0.0, 0.0); }
        if (p != k) { for (uint32_t j = 0; j < N; j++) { Cplx t = M[(size_t)k*N+j]; M[(size_t)k*N+j] = M[(size_t)p*N+j]; M[(size_t)p*N+j] = t; } sign = -sign; }
        det = c_mul(det, M[(size_t)k*N+k]);
        for (uint32_t i = k+1; i < N; i++) {
            Cplx f = c_div(M[(size_t)i*N+k], M[(size_t)k*N+k]);
            for (uint32_t j = k+1; j < N; j++) M[(size_t)i*N+j] = c_sub(M[(size_t)i*N+j], c_mul(f, M[(size_t)k*N+j]));
        }
    }
    if (sign < 0) det = (Cplx){ -det.re, -det.im };
    free(M);
    return real_in ? val_float(det.re) : val_complex(det.re, det.im);
}

static Value bi_inv(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0])) runtime_error(I, "inv: expected a square matrix");
    ArrObj *a = as_arr(args[0]);
    if (a->rows != a->cols) runtime_error(I, "inv: matrix must be square (got %ux%u)", a->rows, a->cols);
    Value id = identity(a->rows);
    Value r = mldivide(I, args[0], id);     /* A \ I */
    value_release(id);
    return r;
}

static Value bi_dot(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0]) || !is_array(args[1])) runtime_error(I, "dot: expected two vectors");
    ArrObj *x = as_arr(args[0]), *y = as_arr(args[1]);
    size_t nx = (size_t)x->rows * x->cols, ny = (size_t)y->rows * y->cols;
    if (nx != ny) runtime_error(I, "dot: length mismatch (%zu vs %zu)", nx, ny);
    Value acc = val_int(0);
    for (size_t k = 0; k < nx; k++) {
        Value p = scalar_arith_k(I, AR_MUL, arr_get(x, k), arr_get(y, k));
        acc = scalar_arith_k(I, AR_ADD, acc, p);
    }
    return acc;
}

static Value bi_norm(Interp *I, Value *args, uint32_t n)
{
    Value v = args[0];
    if (is_num(v)) return val_float(vmag(v));
    if (!is_array(v)) runtime_error(I, "norm: expected a number or array");
    ArrObj *a = as_arr(v);
    size_t nn = (size_t)a->rows * a->cols;
    int p = 2;
    if (n == 2) {
        if (args[1].kind != VAL_INT) runtime_error(I, "norm: p must be 1 or 2");
        p = (int)args[1].as.i;
    }
    bool vec = a->rows == 1 || a->cols == 1;
    if (!vec || p == 2) {                            /* Frobenius for matrices; 2-norm for vectors */
        double s = 0; for (size_t k = 0; k < nn; k++) { double m = vmag(arr_get(a, k)); s += m * m; }
        return val_float(sqrt(s));
    }
    if (p == 1) { double s = 0; for (size_t k = 0; k < nn; k++) s += vmag(arr_get(a, k)); return val_float(s); }
    runtime_error(I, "norm: only p = 1 or 2 supported");
}

static Value bi_reshape(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_array(args[0])) runtime_error(I, "reshape: first argument must be an array");
    if (args[1].kind != VAL_INT || args[2].kind != VAL_INT) runtime_error(I, "reshape: dimensions must be Int");
    ArrObj *a = as_arr(args[0]);
    int64_t r = args[1].as.i, c = args[2].as.i;
    if (r < 0 || c < 0) runtime_error(I, "reshape: dimensions must be non-negative");
    size_t total = (size_t)a->rows * a->cols;
    if ((size_t)r * (size_t)c != total)
        runtime_error(I, "reshape: cannot fit %zu elements into %lldx%lld", total, (long long)r, (long long)c);
    Value out = val_array(a->elt, (uint32_t)r, (uint32_t)c);
    memcpy(as_arr(out)->data, a->data, total * elt_size(a->elt));   /* row-major reinterpretation */
    return out;
}

static Value bi_linspace(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    if (!is_num(args[0]) || args[0].kind == VAL_COMPLEX || !is_num(args[1]) || args[1].kind == VAL_COMPLEX)
        runtime_error(I, "linspace: endpoints must be real numbers");
    if (args[2].kind != VAL_INT || args[2].as.i < 1) runtime_error(I, "linspace: count must be a positive Int");
    double a = as_double(args[0]), b = as_double(args[1]);
    uint32_t cnt = (uint32_t)args[2].as.i;
    Value out = val_array(ELT_FLOAT, 1, cnt);
    double *d = (double *)as_arr(out)->data;
    if (cnt == 1) d[0] = b;
    else for (uint32_t k = 0; k < cnt; k++) d[k] = a + (b - a) * ((double)k / (double)(cnt - 1));
    return out;
}

/* complex Householder least squares: overdetermined A x ~= b (m>=n) via QR */
static Value lstsq(Interp *I, Value A, Value B)
{
    uint32_t m, nn;   Cplx *R  = to_cplx(I, A, &m, &nn, "left division");
    uint32_t bm, nrhs; Cplx *Bd = to_cplx(I, B, &bm, &nrhs, "left division");
    bool ro = arr_real(A) && arr_real(B);
    if (bm != m) { free(R); free(Bd); runtime_error(I, "left division dimensions disagree: %ux%u \\ %ux%u", m, nn, bm, nrhs); }
    if (m < nn) {
        /* Underdetermined: minimum-norm solution. With A^H = Q1 R1 (Q1 is n x m,
         * R1 is m x m upper-triangular), solve R1^H y = b, then x = Q1 y lies in
         * the row space of A, giving the least-norm solution of A x = b. */
        uint32_t P = nn;                                  /* C = A^H is P x m, P > m */
        Cplx   *C    = malloc((size_t)P * m * sizeof *C);
        Cplx   *Vv   = calloc((size_t)P * m, sizeof *Vv); /* stored reflectors */
        double *beta = calloc(m ? m : 1, sizeof *beta);
        Cplx   *vv   = malloc((P ? P : 1) * sizeof *vv);
        for (uint32_t i = 0; i < P; i++)
            for (uint32_t j = 0; j < m; j++) C[(size_t)i*m+j] = c_conj(R[(size_t)j*nn+i]);
        for (uint32_t k = 0; k < m; k++) {                /* Householder QR of C */
            double nrm = 0; for (uint32_t i = k; i < P; i++) { double a = c_abs(C[(size_t)i*m+k]); nrm += a*a; }
            nrm = sqrt(nrm);
            if (nrm == 0.0) continue;
            Cplx xk = C[(size_t)k*m+k]; double axk = c_abs(xk);
            Cplx alpha = axk > 0 ? c_scale(-nrm/axk, xk) : (Cplx){ -nrm, 0 };
            for (uint32_t i = 0; i < P; i++) vv[i] = (Cplx){0,0};
            for (uint32_t i = k; i < P; i++) vv[i] = C[(size_t)i*m+k];
            vv[k] = c_sub(vv[k], alpha);
            double vn2 = 0; for (uint32_t i = k; i < P; i++) { double a = c_abs(vv[i]); vn2 += a*a; }
            if (vn2 == 0.0) continue;
            beta[k] = 2.0 / vn2;
            for (uint32_t i = k; i < P; i++) Vv[(size_t)i*m+k] = vv[i];
            for (uint32_t j = k; j < m; j++) {
                Cplx w = {0,0}; for (uint32_t i = k; i < P; i++) w = c_add(w, c_mul(c_conj(vv[i]), C[(size_t)i*m+j]));
                w = c_scale(beta[k], w);
                for (uint32_t i = k; i < P; i++) C[(size_t)i*m+j] = c_sub(C[(size_t)i*m+j], c_mul(vv[i], w));
            }
        }
        Cplx *X = malloc((size_t)(nn ? nn : 1) * (nrhs ? nrhs : 1) * sizeof *X);
        Cplx *y = malloc((m ? m : 1) * sizeof *y);
        Cplx *xx = malloc((P ? P : 1) * sizeof *xx);
        for (uint32_t col = 0; col < nrhs; col++) {
            for (uint32_t i = 0; i < m; i++) {            /* forward subst: R1^H y = b */
                Cplx s = Bd[(size_t)i*nrhs+col];
                for (uint32_t j = 0; j < i; j++) s = c_sub(s, c_mul(c_conj(C[(size_t)j*m+i]), y[j]));
                Cplx rii = c_conj(C[(size_t)i*m+i]);
                if (c_abs(rii) == 0.0) { free(C); free(Vv); free(beta); free(vv); free(X); free(y); free(xx); free(R); free(Bd); runtime_error(I, "left division: matrix is rank-deficient"); }
                y[i] = c_div(s, rii);
            }
            for (uint32_t i = 0; i < P; i++) xx[i] = i < m ? y[i] : (Cplx){0,0};
            for (int kk = (int)m - 1; kk >= 0; kk--) {    /* x = Q1 y = Q [y; 0] */
                uint32_t k = (uint32_t)kk;
                if (beta[k] == 0.0) continue;
                Cplx w = {0,0}; for (uint32_t i = k; i < P; i++) w = c_add(w, c_mul(c_conj(Vv[(size_t)i*m+k]), xx[i]));
                w = c_scale(beta[k], w);
                for (uint32_t i = k; i < P; i++) xx[i] = c_sub(xx[i], c_mul(Vv[(size_t)i*m+k], w));
            }
            for (uint32_t i = 0; i < nn; i++) X[(size_t)i*nrhs+col] = xx[i];
        }
        Value out = from_cplx(X, nn, nrhs, ro);
        free(C); free(Vv); free(beta); free(vv); free(X); free(y); free(xx); free(R); free(Bd);
        return out;
    }
    Cplx *v = malloc((m ? m : 1) * sizeof *v);
    for (uint32_t k = 0; k < nn; k++) {
        double nrm = 0; for (uint32_t i = k; i < m; i++) { double a = c_abs(R[(size_t)i*nn+k]); nrm += a*a; }
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        Cplx xk = R[(size_t)k*nn+k]; double axk = c_abs(xk);
        Cplx alpha = axk > 0 ? c_scale(-nrm/axk, xk) : (Cplx){ -nrm, 0 };
        for (uint32_t i = 0; i < m; i++) v[i] = (Cplx){0,0};
        for (uint32_t i = k; i < m; i++) v[i] = R[(size_t)i*nn+k];
        v[k] = c_sub(v[k], alpha);
        double vn2 = 0; for (uint32_t i = k; i < m; i++) { double a = c_abs(v[i]); vn2 += a*a; }
        if (vn2 == 0.0) continue;
        double beta = 2.0 / vn2;
        for (uint32_t j = k; j < nn; j++) {           /* R <- (I - beta v v^H) R */
            Cplx w = {0,0}; for (uint32_t i = k; i < m; i++) w = c_add(w, c_mul(c_conj(v[i]), R[(size_t)i*nn+j]));
            w = c_scale(beta, w);
            for (uint32_t i = k; i < m; i++) R[(size_t)i*nn+j] = c_sub(R[(size_t)i*nn+j], c_mul(v[i], w));
        }
        for (uint32_t j = 0; j < nrhs; j++) {          /* same reflector applied to B */
            Cplx w = {0,0}; for (uint32_t i = k; i < m; i++) w = c_add(w, c_mul(c_conj(v[i]), Bd[(size_t)i*nrhs+j]));
            w = c_scale(beta, w);
            for (uint32_t i = k; i < m; i++) Bd[(size_t)i*nrhs+j] = c_sub(Bd[(size_t)i*nrhs+j], c_mul(v[i], w));
        }
    }
    Cplx *X = malloc((size_t)(nn ? nn : 1) * (nrhs ? nrhs : 1) * sizeof *X);
    for (uint32_t c = 0; c < nrhs; c++)
        for (int64_t ii = (int64_t)nn - 1; ii >= 0; ii--) {
            uint32_t i = (uint32_t)ii;
            Cplx s = Bd[(size_t)i*nrhs+c];
            for (uint32_t j = i+1; j < nn; j++) s = c_sub(s, c_mul(R[(size_t)i*nn+j], X[(size_t)j*nrhs+c]));
            Cplx rii = R[(size_t)i*nn+i];
            if (c_abs(rii) == 0.0) { free(R); free(Bd); free(v); free(X); runtime_error(I, "left division: matrix is rank-deficient"); }
            X[(size_t)i*nrhs+c] = c_div(s, rii);
        }
    Value out = from_cplx(X, nn, nrhs, ro);
    free(R); free(Bd); free(v); free(X);
    return out;
}

static Value bi_lu(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    uint32_t N, c; Cplx *M = to_cplx(I, args[0], &N, &c, "lu");
    bool ro = arr_real(args[0]);
    if (c != N) { free(M); runtime_error(I, "lu: matrix must be square (got %ux%u)", N, c); }
    uint32_t *piv = malloc((N ? N : 1) * sizeof *piv);
    for (uint32_t i = 0; i < N; i++) piv[i] = i;
    for (uint32_t k = 0; k < N; k++) {
        uint32_t p = k; double best = c_abs(M[(size_t)k*N+k]);
        for (uint32_t i = k+1; i < N; i++) { double mg = c_abs(M[(size_t)i*N+k]); if (mg > best) { best = mg; p = i; } }
        if (p != k) { for (uint32_t j = 0; j < N; j++) { Cplx t = M[(size_t)k*N+j]; M[(size_t)k*N+j] = M[(size_t)p*N+j]; M[(size_t)p*N+j] = t; } uint32_t t = piv[k]; piv[k] = piv[p]; piv[p] = t; }
        Cplx akk = M[(size_t)k*N+k];
        if (c_abs(akk) > 0)
            for (uint32_t i = k+1; i < N; i++) {
                Cplx f = c_div(M[(size_t)i*N+k], akk);
                M[(size_t)i*N+k] = f;
                for (uint32_t j = k+1; j < N; j++) M[(size_t)i*N+j] = c_sub(M[(size_t)i*N+j], c_mul(f, M[(size_t)k*N+j]));
            }
    }
    Cplx *Lb = calloc((size_t)(N ? N*N : 1), sizeof *Lb), *Ub = calloc((size_t)(N ? N*N : 1), sizeof *Ub);
    for (uint32_t i = 0; i < N; i++)
        for (uint32_t j = 0; j < N; j++) {
            if (i > j)       { Lb[(size_t)i*N+j] = M[(size_t)i*N+j]; Ub[(size_t)i*N+j] = (Cplx){0,0}; }
            else if (i == j) { Lb[(size_t)i*N+j] = (Cplx){1,0};     Ub[(size_t)i*N+j] = M[(size_t)i*N+j]; }
            else             { Lb[(size_t)i*N+j] = (Cplx){0,0};     Ub[(size_t)i*N+j] = M[(size_t)i*N+j]; }
        }
    Value L = from_cplx(Lb, N, N, ro), U = from_cplx(Ub, N, N, ro);
    Value P = val_array(ELT_INT, N, 1);
    for (uint32_t i = 0; i < N; i++) ((int64_t *)as_arr(P)->data)[i] = piv[i] + 1;
    free(M); free(Lb); free(Ub); free(piv);
    return record3("L", L, "U", U, "p", P);     /* P*A = L*U; p is the 1-based row permutation */
}

static Value bi_qr(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    uint32_t m, nn; Cplx *R = to_cplx(I, args[0], &m, &nn, "qr");
    bool ro = arr_real(args[0]);
    Cplx *Q = calloc((size_t)(m ? m*m : 1), sizeof *Q);
    for (uint32_t i = 0; i < m; i++) Q[(size_t)i*m+i] = (Cplx){1,0};
    Cplx *v = malloc((m ? m : 1) * sizeof *v);
    uint32_t steps = nn < m ? nn : m;
    for (uint32_t k = 0; k < steps; k++) {
        double nrm = 0; for (uint32_t i = k; i < m; i++) { double a = c_abs(R[(size_t)i*nn+k]); nrm += a*a; }
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        Cplx xk = R[(size_t)k*nn+k]; double axk = c_abs(xk);
        Cplx alpha = axk > 0 ? c_scale(-nrm/axk, xk) : (Cplx){ -nrm, 0 };
        for (uint32_t i = 0; i < m; i++) v[i] = (Cplx){0,0};
        for (uint32_t i = k; i < m; i++) v[i] = R[(size_t)i*nn+k];
        v[k] = c_sub(v[k], alpha);
        double vn2 = 0; for (uint32_t i = k; i < m; i++) { double a = c_abs(v[i]); vn2 += a*a; }
        if (vn2 == 0.0) continue;
        double beta = 2.0 / vn2;
        for (uint32_t j = k; j < nn; j++) {           /* R <- H R */
            Cplx w = {0,0}; for (uint32_t i = k; i < m; i++) w = c_add(w, c_mul(c_conj(v[i]), R[(size_t)i*nn+j]));
            w = c_scale(beta, w);
            for (uint32_t i = k; i < m; i++) R[(size_t)i*nn+j] = c_sub(R[(size_t)i*nn+j], c_mul(v[i], w));
        }
        for (uint32_t i = 0; i < m; i++) {            /* Q <- Q H */
            Cplx u = {0,0}; for (uint32_t l = k; l < m; l++) u = c_add(u, c_mul(Q[(size_t)i*m+l], v[l]));
            u = c_scale(beta, u);
            for (uint32_t l = k; l < m; l++) Q[(size_t)i*m+l] = c_sub(Q[(size_t)i*m+l], c_mul(u, c_conj(v[l])));
        }
    }
    Value Qv = from_cplx(Q, m, m, ro), Rv = from_cplx(R, m, nn, ro);
    free(R); free(Q); free(v);
    return record2("Q", Qv, "R", Rv);
}

static Value bi_chol(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    uint32_t N, c; Cplx *A = to_cplx(I, args[0], &N, &c, "chol");
    bool ro = arr_real(args[0]);
    if (c != N) { free(A); runtime_error(I, "chol: matrix must be square (got %ux%u)", N, c); }
    Cplx *L = calloc((size_t)(N ? N*N : 1), sizeof *L);
    for (uint32_t j = 0; j < N; j++) {
        double d = A[(size_t)j*N+j].re;
        for (uint32_t k = 0; k < j; k++) { double a = c_abs(L[(size_t)j*N+k]); d -= a*a; }
        if (d <= 0.0) { free(A); free(L); runtime_error(I, "chol: matrix is not positive definite"); }
        double Ljj = sqrt(d);
        L[(size_t)j*N+j] = (Cplx){ Ljj, 0 };
        for (uint32_t i = j+1; i < N; i++) {
            Cplx s = A[(size_t)i*N+j];
            for (uint32_t k = 0; k < j; k++) s = c_sub(s, c_mul(L[(size_t)i*N+k], c_conj(L[(size_t)j*N+k])));
            L[(size_t)i*N+j] = c_div(s, (Cplx){ Ljj, 0 });
        }
    }
    Value Lv = from_cplx(L, N, N, ro);     /* lower-triangular: L * L' = A  (Hermitian for complex) */
    free(A); free(L);
    return Lv;
}

/* complex Jacobi rotation that diagonalizes the Hermitian 2x2 [[app, apq],[apq*, aqq]];
 * yields a real cosine c and complex sine sp with |c|^2 + |sp|^2 = 1. */
static void jacobi_rot(double app, double aqq, Cplx apq, double *c_out, Cplx *sp_out)
{
    double a = c_abs(apq);
    if (a == 0.0) { *c_out = 1.0; *sp_out = (Cplx){0,0}; return; }
    double tau = (aqq - app) / (2.0 * a);
    double t = (tau >= 0 ? 1.0 : -1.0) / (fabs(tau) + sqrt(tau*tau + 1.0));
    double cc = 1.0 / sqrt(t*t + 1.0), s = t * cc;
    *c_out = cc;
    *sp_out = c_scale(s / a, apq);          /* s * e^{i arg(apq)} */
}

/* Complex Givens rotation: (c real, s) such that [[c,s],[-conj(s),c]] [a;b] = [r;0]. */
static Cplx c_sqrtz(Cplx z);                            /* defined later */
static void c_givens(Cplx a, Cplx b, double *c, Cplx *s)
{
    double ab = c_abs(b);
    if (ab == 0.0) { *c = 1.0; *s = (Cplx){0,0}; return; }
    double aa = c_abs(a);
    if (aa == 0.0) { *c = 0.0; *s = c_scale(1.0/ab, c_conj(b)); return; }
    double t = hypot(aa, ab);
    *c = aa / t;
    *s = c_scale(1.0/(aa*t), c_mul(a, c_conj(b)));      /* (a/|a|)*conj(b)/t */
}

/* Solve A x = b in place (A,b modified, x left in b); false if singular. */
static bool csolve_inplace(Cplx *A, uint32_t N, Cplx *b)
{
    for (uint32_t k = 0; k < N; k++) {
        uint32_t p = k; double best = c_abs(A[(size_t)k*N+k]);
        for (uint32_t i = k+1; i < N; i++) { double m = c_abs(A[(size_t)i*N+k]); if (m > best) { best = m; p = i; } }
        if (best < 1e-300) return false;
        if (p != k) {
            for (uint32_t j = 0; j < N; j++) { Cplx t = A[(size_t)k*N+j]; A[(size_t)k*N+j] = A[(size_t)p*N+j]; A[(size_t)p*N+j] = t; }
            Cplx t = b[k]; b[k] = b[p]; b[p] = t;
        }
        Cplx akk = A[(size_t)k*N+k];
        for (uint32_t i = k+1; i < N; i++) {
            Cplx f = c_div(A[(size_t)i*N+k], akk);
            for (uint32_t j = k+1; j < N; j++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(f, A[(size_t)k*N+j]));
            b[i] = c_sub(b[i], c_mul(f, b[k]));
        }
    }
    for (int64_t i = (int64_t)N-1; i >= 0; i--) {
        Cplx s = b[i];
        for (uint32_t j = (uint32_t)i+1; j < N; j++) s = c_sub(s, c_mul(A[(size_t)i*N+j], b[j]));
        b[i] = c_div(s, A[(size_t)i*N+i]);
    }
    return true;
}

/* Reduce A to upper Hessenberg form in place by Householder similarity. */
static void hessenberg(Cplx *A, uint32_t N)
{
    Cplx *v = malloc((N ? N : 1) * sizeof *v);
    for (uint32_t k = 0; k + 2 < N; k++) {
        double nrm = 0; for (uint32_t i = k+1; i < N; i++) { double a = c_abs(A[(size_t)i*N+k]); nrm += a*a; }
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        Cplx xk = A[(size_t)(k+1)*N+k]; double axk = c_abs(xk);
        Cplx alpha = axk > 0 ? c_scale(-nrm/axk, xk) : (Cplx){ -nrm, 0 };
        for (uint32_t i = 0; i < N; i++) v[i] = (Cplx){0,0};
        for (uint32_t i = k+1; i < N; i++) v[i] = A[(size_t)i*N+k];
        v[k+1] = c_sub(v[k+1], alpha);
        double vn2 = 0; for (uint32_t i = k+1; i < N; i++) { double a = c_abs(v[i]); vn2 += a*a; }
        if (vn2 == 0.0) continue;
        double beta = 2.0 / vn2;
        for (uint32_t j = 0; j < N; j++) {                 /* A <- (I - beta v v^H) A */
            Cplx w = {0,0}; for (uint32_t i = k+1; i < N; i++) w = c_add(w, c_mul(c_conj(v[i]), A[(size_t)i*N+j]));
            w = c_scale(beta, w);
            for (uint32_t i = k+1; i < N; i++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(v[i], w));
        }
        for (uint32_t i = 0; i < N; i++) {                 /* A <- A (I - beta v v^H) */
            Cplx w = {0,0}; for (uint32_t j = k+1; j < N; j++) w = c_add(w, c_mul(A[(size_t)i*N+j], v[j]));
            w = c_scale(beta, w);
            for (uint32_t j = k+1; j < N; j++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(w, c_conj(v[j])));
        }
    }
    free(v);
}

/* Eigenvalues of a (Hessenberg) matrix H by shifted complex QR; writes N eigenvalues to eig. */
static void qr_eig(Cplx *H, uint32_t N, Cplx *eig)
{
    hessenberg(H, N);
    double *cs = malloc((N ? N : 1) * sizeof *cs);
    Cplx   *sn = malloc((N ? N : 1) * sizeof *sn);
    uint32_t hi = N;
    int iters = 0;
    while (hi > 0) {
        uint32_t lo = hi - 1;                               /* find bottom block [lo, hi) */
        while (lo > 0) {
            double s = c_abs(H[(size_t)(lo-1)*N+(lo-1)]) + c_abs(H[(size_t)lo*N+lo]);
            if (s == 0.0) s = 1.0;
            if (c_abs(H[(size_t)lo*N+(lo-1)]) <= 1e-15 * s) break;
            lo--;
        }
        if (lo == hi - 1) { eig[hi-1] = H[(size_t)(hi-1)*N+(hi-1)]; hi--; iters = 0; continue; }
        if (++iters > 300) {                                /* give up: accept the diagonal */
            for (uint32_t i = lo; i < hi; i++) eig[i] = H[(size_t)i*N+i];
            hi = lo; iters = 0; continue;
        }
        Cplx a = H[(size_t)(hi-2)*N+(hi-2)], b = H[(size_t)(hi-2)*N+(hi-1)];
        Cplx cc2 = H[(size_t)(hi-1)*N+(hi-2)], d = H[(size_t)(hi-1)*N+(hi-1)];
        Cplx tr = c_add(a, d), det = c_sub(c_mul(a, d), c_mul(b, cc2));
        Cplx disc = c_sqrtz(c_sub(c_mul(tr, tr), c_scale(4.0, det)));
        Cplx mu1 = c_scale(0.5, c_add(tr, disc)), mu2 = c_scale(0.5, c_sub(tr, disc));
        Cplx mu = c_abs(c_sub(mu1, d)) < c_abs(c_sub(mu2, d)) ? mu1 : mu2;
        for (uint32_t i = lo; i < hi; i++) H[(size_t)i*N+i] = c_sub(H[(size_t)i*N+i], mu);   /* H - muI */
        for (uint32_t i = lo; i + 1 < hi; i++) {            /* QR: zero subdiagonals -> R */
            double c; Cplx s; c_givens(H[(size_t)i*N+i], H[(size_t)(i+1)*N+i], &c, &s);
            cs[i] = c; sn[i] = s;
            for (uint32_t j = i; j < hi; j++) {
                Cplx hi_ = H[(size_t)i*N+j], hj_ = H[(size_t)(i+1)*N+j];
                H[(size_t)i*N+j]     = c_add(c_scale(c, hi_), c_mul(s, hj_));
                H[(size_t)(i+1)*N+j] = c_sub(c_scale(c, hj_), c_mul(c_conj(s), hi_));
            }
        }
        for (uint32_t i = lo; i + 1 < hi; i++) {            /* H <- R Q (postmultiply by G^H) */
            double c = cs[i]; Cplx s = sn[i];
            uint32_t rmax = i + 2 < hi ? i + 2 : hi;
            for (uint32_t r = lo; r < rmax; r++) {
                Cplx mi = H[(size_t)r*N+i], mj = H[(size_t)r*N+(i+1)];
                H[(size_t)r*N+i]     = c_add(c_scale(c, mi), c_mul(c_conj(s), mj));
                H[(size_t)r*N+(i+1)] = c_sub(c_scale(c, mj), c_mul(s, mi));
            }
        }
        for (uint32_t i = lo; i < hi; i++) H[(size_t)i*N+i] = c_add(H[(size_t)i*N+i], mu);   /* + muI */
    }
    free(cs); free(sn);
}

/* eigenvalues of a symmetric/Hermitian matrix (always real) via cyclic Jacobi */
static Value bi_eig(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    uint32_t N, c; Cplx *A = to_cplx(I, args[0], &N, &c, "eig");
    if (c != N) { free(A); runtime_error(I, "eig: matrix must be square (got %ux%u)", N, c); }
    bool real_in = arr_real(args[0]);

    bool hermitian = true;                              /* detect symmetric/Hermitian */
    for (uint32_t i = 0; i < N && hermitian; i++)
        for (uint32_t j = i+1; j < N; j++) {
            Cplx d = c_sub(A[(size_t)i*N+j], c_conj(A[(size_t)j*N+i]));
            if (c_abs(d) > 1e-9 * (1.0 + c_abs(A[(size_t)i*N+j]))) { hermitian = false; break; }
        }

    if (hermitian) {                                   /* cyclic Jacobi, accumulating vectors in V */
        Cplx *V = calloc((size_t)(N ? N*N : 1), sizeof *V);
        for (uint32_t i = 0; i < N; i++) V[(size_t)i*N+i] = (Cplx){1,0};
        for (int sweep = 0; sweep < 100; sweep++) {
            double off = 0;
            for (uint32_t i = 0; i < N; i++) for (uint32_t j = i+1; j < N; j++) { double a = c_abs(A[(size_t)i*N+j]); off += a*a; }
            if (off < 1e-30) break;
            for (uint32_t p = 0; p < N; p++)
                for (uint32_t q = p+1; q < N; q++) {
                    Cplx apq = A[(size_t)p*N+q];
                    if (c_abs(apq) < 1e-300) continue;
                    double app = A[(size_t)p*N+p].re, aqq = A[(size_t)q*N+q].re;
                    double cc; Cplx sp; jacobi_rot(app, aqq, apq, &cc, &sp);
                    for (uint32_t k = 0; k < N; k++) {     /* A <- A R */
                        Cplx kp = A[(size_t)k*N+p], kq = A[(size_t)k*N+q];
                        A[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                        A[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                    }
                    for (uint32_t k = 0; k < N; k++) {     /* A <- R^H A */
                        Cplx pk = A[(size_t)p*N+k], qk = A[(size_t)q*N+k];
                        A[(size_t)p*N+k] = c_sub(c_scale(cc, pk), c_mul(sp, qk));
                        A[(size_t)q*N+k] = c_add(c_mul(c_conj(sp), pk), c_scale(cc, qk));
                    }
                    for (uint32_t k = 0; k < N; k++) {     /* V <- V R (eigenvectors) */
                        Cplx kp = V[(size_t)k*N+p], kq = V[(size_t)k*N+q];
                        V[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                        V[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                    }
                }
        }
        double *ev = malloc((N ? N : 1) * sizeof *ev);
        uint32_t *ord = malloc((N ? N : 1) * sizeof *ord);
        for (uint32_t i = 0; i < N; i++) { ev[i] = A[(size_t)i*N+i].re; ord[i] = i; }
        for (uint32_t i = 1; i < N; i++) {                 /* sort indices by ascending eigenvalue */
            uint32_t oi = ord[i]; double x = ev[oi]; uint32_t j = i;
            while (j > 0 && ev[ord[j-1]] > x) { ord[j] = ord[j-1]; j--; }
            ord[j] = oi;
        }
        Value vals = val_array(ELT_FLOAT, N, 1);
        Cplx *Vc = malloc((size_t)(N ? N*N : 1) * sizeof *Vc);
        for (uint32_t i = 0; i < N; i++) {
            ((double *)as_arr(vals)->data)[i] = ev[ord[i]];
            for (uint32_t r = 0; r < N; r++) Vc[(size_t)r*N+i] = V[(size_t)r*N+ord[i]];
        }
        Value vecs = from_cplx(Vc, N, N, real_in);
        free(A); free(V); free(Vc); free(ev); free(ord);
        return record2("values", vals, "vectors", vecs);
    }

    /* nonsymmetric: eigenvalues by complex QR, eigenvectors by inverse iteration */
    Cplx *A0 = malloc((size_t)(N ? N*N : 1) * sizeof *A0);   /* keep the original */
    memcpy(A0, A, (size_t)(N ? N*N : 1) * sizeof *A0);
    Cplx *eig = malloc((N ? N : 1) * sizeof *eig);
    qr_eig(A, N, eig);                                       /* A is consumed (Hessenberg/Schur) */

    for (uint32_t i = 1; i < N; i++) {                       /* sort by (re, im) for determinism */
        Cplx e = eig[i]; uint32_t j = i;
        while (j > 0 && (eig[j-1].re > e.re || (eig[j-1].re == e.re && eig[j-1].im > e.im))) { eig[j] = eig[j-1]; j--; }
        eig[j] = e;
    }

    Cplx *Vc = malloc((size_t)(N ? N*N : 1) * sizeof *Vc);
    Cplx *M = malloc((size_t)(N ? N*N : 1) * sizeof *M);
    Cplx *x = malloc((N ? N : 1) * sizeof *x);
    bool eig_real = real_in;
    for (uint32_t j = 0; j < N; j++) {
        Cplx lam = eig[j];
        if (fabs(lam.im) > 1e-12 * (1.0 + c_abs(lam))) eig_real = false;
        double shift = 1e-8 * (1.0 + c_abs(lam));            /* perturb off the exact eigenvalue */
        Cplx mu = { lam.re + shift, lam.im + shift };
        for (uint32_t i = 0; i < N; i++) x[i] = (Cplx){ 1.0 / (i + 1.0), 0 };   /* start vector */
        for (int it = 0; it < 6; it++) {
            memcpy(M, A0, (size_t)N*N * sizeof *M);
            for (uint32_t i = 0; i < N; i++) M[(size_t)i*N+i] = c_sub(M[(size_t)i*N+i], mu);
            if (!csolve_inplace(M, N, x)) break;             /* x <- (A - mu I)^{-1} x */
            double nrm = 0; for (uint32_t i = 0; i < N; i++) { double a = c_abs(x[i]); nrm += a*a; }
            nrm = sqrt(nrm);
            if (nrm == 0) break;
            for (uint32_t i = 0; i < N; i++) x[i] = c_scale(1.0/nrm, x[i]);
        }
        uint32_t pmax = 0; double bmax = 0;                  /* normalize phase: largest entry real>0 */
        for (uint32_t i = 0; i < N; i++) { double a = c_abs(x[i]); if (a > bmax) { bmax = a; pmax = i; } }
        if (bmax > 0) { Cplx ph = c_scale(1.0/c_abs(x[pmax]), x[pmax]); ph = c_conj(ph);
                        for (uint32_t i = 0; i < N; i++) x[i] = c_mul(x[i], ph); }
        for (uint32_t i = 0; i < N; i++) Vc[(size_t)i*N+j] = x[i];
    }

    Value vals = val_array(eig_real ? ELT_FLOAT : ELT_COMPLEX, N, 1);
    if (eig_real) for (uint32_t i = 0; i < N; i++) ((double *)as_arr(vals)->data)[i] = eig[i].re;
    else          for (uint32_t i = 0; i < N; i++) ((Cplx   *)as_arr(vals)->data)[i] = eig[i];
    Value vecs = from_cplx(Vc, N, N, eig_real && real_in);
    free(A); free(A0); free(eig); free(Vc); free(M); free(x);
    return record2("values", vals, "vectors", vecs);
}

/* thin SVD via one-sided Jacobi: A = U * diag(S) * V'  (S descending).
 * Works on the tall orientation; for m<n it factors A' and swaps U,V. */
static Value bi_svd(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    uint32_t m, nc; Cplx *Araw = to_cplx(I, args[0], &m, &nc, "svd");
    bool ro = arr_real(args[0]);
    bool tr = m < nc;
    uint32_t M = tr ? nc : m, N = tr ? m : nc;           /* work tall: M >= N */
    Cplx *U = malloc((size_t)(M ? M : 1) * (N ? N : 1) * sizeof *U);
    if (!tr) for (uint32_t i = 0; i < M; i++) for (uint32_t j = 0; j < N; j++) U[(size_t)i*N+j] = Araw[(size_t)i*nc+j];
    else     for (uint32_t i = 0; i < M; i++) for (uint32_t j = 0; j < N; j++) U[(size_t)i*N+j] = c_conj(Araw[(size_t)j*nc+i]);
    free(Araw);
    Cplx *V = calloc((size_t)(N ? N*N : 1), sizeof *V);
    for (uint32_t i = 0; i < N; i++) V[(size_t)i*N+i] = (Cplx){1,0};

    for (int sweep = 0; sweep < 60; sweep++) {
        double off = 0;
        for (uint32_t p = 0; p < N; p++)
            for (uint32_t q = p+1; q < N; q++) {
                double app = 0, aqq = 0; Cplx apq = {0,0};
                for (uint32_t k = 0; k < M; k++) {
                    Cplx up = U[(size_t)k*N+p], uq = U[(size_t)k*N+q];
                    app += up.re*up.re + up.im*up.im;
                    aqq += uq.re*uq.re + uq.im*uq.im;
                    apq = c_add(apq, c_mul(c_conj(up), uq));
                }
                double a = c_abs(apq); off += a*a;
                if (a < 1e-300) continue;
                double cc; Cplx sp; jacobi_rot(app, aqq, apq, &cc, &sp);
                for (uint32_t k = 0; k < M; k++) {
                    Cplx kp = U[(size_t)k*N+p], kq = U[(size_t)k*N+q];
                    U[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    U[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
                for (uint32_t k = 0; k < N; k++) {
                    Cplx kp = V[(size_t)k*N+p], kq = V[(size_t)k*N+q];
                    V[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    V[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
            }
        if (off < 1e-28) break;
    }
    double *sv = malloc((N ? N : 1) * sizeof *sv);
    for (uint32_t j = 0; j < N; j++) {
        double s = 0; for (uint32_t k = 0; k < M; k++) { Cplx u = U[(size_t)k*N+j]; s += u.re*u.re + u.im*u.im; }
        sv[j] = sqrt(s);
        if (sv[j] > 0) for (uint32_t k = 0; k < M; k++) U[(size_t)k*N+j] = c_div(U[(size_t)k*N+j], (Cplx){ sv[j], 0 });
    }
    uint32_t *ord = malloc((N ? N : 1) * sizeof *ord);
    for (uint32_t i = 0; i < N; i++) ord[i] = i;
    for (uint32_t i = 1; i < N; i++) { uint32_t o = ord[i]; double x = sv[o]; uint32_t j = i; while (j > 0 && sv[ord[j-1]] < x) { ord[j] = ord[j-1]; j--; } ord[j] = o; }
    Cplx *Uo = malloc((size_t)(M ? M : 1) * (N ? N : 1) * sizeof *Uo);
    Cplx *Vo = malloc((size_t)(N ? N*N : 1) * sizeof *Vo);
    double *so = malloc((N ? N : 1) * sizeof *so);
    for (uint32_t jj = 0; jj < N; jj++) {
        uint32_t src = ord[jj]; so[jj] = sv[src];
        for (uint32_t k = 0; k < M; k++) Uo[(size_t)k*N+jj] = U[(size_t)k*N+src];
        for (uint32_t k = 0; k < N; k++) Vo[(size_t)k*N+jj] = V[(size_t)k*N+src];
    }
    Value Uval, Vval;
    if (!tr) { Uval = from_cplx(Uo, M, N, ro); Vval = from_cplx(Vo, N, N, ro); }
    else     { Uval = from_cplx(Vo, N, N, ro); Vval = from_cplx(Uo, M, N, ro); }
    Value Sval = val_array(ELT_FLOAT, N, 1);
    for (uint32_t i = 0; i < N; i++) ((double *)as_arr(Sval)->data)[i] = so[i];
    free(U); free(V); free(sv); free(ord); free(Uo); free(Vo); free(so);
    return record3("U", Uval, "S", Sval, "V", Vval);
}

/* ---- elementwise math (scalar or array) --------------------------------- *
 * Transcendentals follow the numeric tower: real input that stays in the real
 * domain returns real; real input outside it (log of a negative, asin of |x|>1)
 * and complex input return complex, matching sqrt. Complex branches are hand-
 * rolled because <complex.h> would redefine `I`, which we use for Interp*. */
static const double NEU_PI = 3.14159265358979323846;

static Cplx c_sqrtz(Cplx z) {
    double m = hypot(z.re, z.im);
    double re = sqrt((m + z.re) * 0.5), im = sqrt((m - z.re) * 0.5);
    return (Cplx){ re, z.im < 0 ? -im : im };
}
static Cplx c_expz(Cplx z)  { double e = exp(z.re); return (Cplx){ e*cos(z.im), e*sin(z.im) }; }
static Cplx c_logz(Cplx z)  { return (Cplx){ log(hypot(z.re, z.im)), atan2(z.im, z.re) }; }
static Cplx c_sinz(Cplx z)  { return (Cplx){ sin(z.re)*cosh(z.im),  cos(z.re)*sinh(z.im) }; }
static Cplx c_cosz(Cplx z)  { return (Cplx){ cos(z.re)*cosh(z.im), -sin(z.re)*sinh(z.im) }; }
static Cplx c_tanz(Cplx z)  { return c_div(c_sinz(z), c_cosz(z)); }
static Cplx c_sinhz(Cplx z) { return (Cplx){ sinh(z.re)*cos(z.im), cosh(z.re)*sin(z.im) }; }
static Cplx c_coshz(Cplx z) { return (Cplx){ cosh(z.re)*cos(z.im), sinh(z.re)*sin(z.im) }; }
static Cplx c_tanhz(Cplx z) { return c_div(c_sinhz(z), c_coshz(z)); }
static Cplx c_imul(Cplx z)  { return (Cplx){ -z.im, z.re }; }   /* i * z */
static Cplx c_asinz(Cplx z) {
    Cplx w = c_sqrtz(c_sub((Cplx){1,0}, c_mul(z, z)));          /* sqrt(1 - z^2) */
    Cplx L = c_logz(c_add(c_imul(z), w));
    return (Cplx){ L.im, -L.re };                               /* -i * log(...) */
}
static Cplx c_acosz(Cplx z) { Cplx s = c_asinz(z); return (Cplx){ NEU_PI*0.5 - s.re, -s.im }; }
static Cplx c_atanz(Cplx z) {
    Cplx iz = c_imul(z);
    Cplx d  = c_sub(c_logz(c_sub((Cplx){1,0}, iz)), c_logz(c_add((Cplx){1,0}, iz)));
    return (Cplx){ -d.im*0.5, d.re*0.5 };                       /* (i/2) * (...) */
}
static Cplx c_asinhz(Cplx z) { return c_logz(c_add(z, c_sqrtz(c_add(c_mul(z,z), (Cplx){1,0})))); }
static Cplx c_acoshz(Cplx z) { return c_logz(c_add(z, c_sqrtz(c_sub(c_mul(z,z), (Cplx){1,0})))); }
static Cplx c_atanhz(Cplx z) { return c_scale(0.5, c_logz(c_div(c_add((Cplx){1,0}, z), c_sub((Cplx){1,0}, z)))); }

/* entire on the reals: real -> real, complex -> complex */
#define ENTIRE_UNARY(name, rfn, cfn)                                                  \
    static Value sc_##name(Interp *I, Value v) { (void)I;                             \
        if (v.kind == VAL_COMPLEX) { Cplx r = cfn(v.as.z); return val_complex(r.re, r.im); } \
        return val_float(rfn(as_double(v))); }                                        \
    static Value bi_##name(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_##name); }
ENTIRE_UNARY(exp,   exp,   c_expz)
ENTIRE_UNARY(sin,   sin,   c_sinz)
ENTIRE_UNARY(cos,   cos,   c_cosz)
ENTIRE_UNARY(tan,   tan,   c_tanz)
ENTIRE_UNARY(sinh,  sinh,  c_sinhz)
ENTIRE_UNARY(cosh,  cosh,  c_coshz)
ENTIRE_UNARY(tanh,  tanh,  c_tanhz)
ENTIRE_UNARY(atan,  atan,  c_atanz)
ENTIRE_UNARY(asinh, asinh, c_asinhz)
#undef ENTIRE_UNARY

/* tower: real in domain -> real; real out of domain or complex -> complex */
#define TOWER_UNARY(name, rfn, cfn, indomain)                                         \
    static Value sc_##name(Interp *I, Value v) { (void)I;                             \
        if (v.kind == VAL_COMPLEX) { Cplx r = cfn(v.as.z); return val_complex(r.re, r.im); } \
        double x = as_double(v);                                                      \
        if (indomain) return val_float(rfn(x));                                       \
        Cplx r = cfn((Cplx){ x, 0.0 }); return val_complex(r.re, r.im); }             \
    static Value bi_##name(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_##name); }
TOWER_UNARY(log,   log,   c_logz,   x >= 0.0)
TOWER_UNARY(asin,  asin,  c_asinz,  x >= -1.0 && x <= 1.0)
TOWER_UNARY(acos,  acos,  c_acosz,  x >= -1.0 && x <= 1.0)
TOWER_UNARY(acosh, acosh, c_acoshz, x >= 1.0)
TOWER_UNARY(atanh, atanh, c_atanhz, x > -1.0 && x < 1.0)
#undef TOWER_UNARY

/* log base b via the tower-aware natural log */
#define LOGB_UNARY(name, base)                                                        \
    static Value sc_##name(Interp *I, Value v) { (void)I; double l = log((double)base);\
        if (v.kind == VAL_COMPLEX) { Cplx r = c_logz(v.as.z); return val_complex(r.re/l, r.im/l); } \
        double x = as_double(v);                                                      \
        if (x >= 0.0) return val_float(log(x)/l);                                      \
        Cplx r = c_logz((Cplx){ x, 0.0 }); return val_complex(r.re/l, r.im/l); }       \
    static Value bi_##name(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_##name); }
LOGB_UNARY(log10, 10.0)
LOGB_UNARY(log2,   2.0)
#undef LOGB_UNARY

/* rounding: real or (componentwise) complex; Int passes through unchanged */
#define ROUND_UNARY(name, rfn)                                                        \
    static Value sc_##name(Interp *I, Value v) { (void)I;                             \
        if (v.kind == VAL_COMPLEX) return val_complex(rfn(v.as.z.re), rfn(v.as.z.im)); \
        if (v.kind == VAL_INT) return v;                                              \
        return val_float(rfn(as_double(v))); }                                        \
    static Value bi_##name(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_##name); }
ROUND_UNARY(floor, floor)
ROUND_UNARY(ceil,  ceil)
ROUND_UNARY(round, round)
ROUND_UNARY(trunc, trunc)
#undef ROUND_UNARY

/* real-domain only (error on complex) */
#define REAL_ONLY(name, rfn)                                                          \
    static Value sc_##name(Interp *I, Value v) {                                      \
        if (v.kind == VAL_INT || v.kind == VAL_FLOAT) return val_float(rfn(as_double(v))); \
        runtime_error(I, #name ": expected a real number, got %s", type_name(v.kind)); } \
    static Value bi_##name(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_##name); }
REAL_ONLY(cbrt,   cbrt)
REAL_ONLY(gamma,  tgamma)
REAL_ONLY(lgamma, lgamma)
REAL_ONLY(erf,    erf)
REAL_ONLY(erfc,   erfc)
#undef REAL_ONLY

/* ---- special functions (real domain, elementwise via map_unary/map_binary) ---- */
static Value map_binary(Interp *I, Value a, Value b, Value (*f)(Interp *, Value, Value));

static double want_real(Interp *I, Value v, const char *who)
{
    if (v.kind != VAL_INT && v.kind != VAL_FLOAT)
        runtime_error(I, "%s: expected a real number, got %s", who, type_name(v.kind));
    return as_double(v);
}

/* Regularized lower incomplete gamma P(a, x): series for x < a+1, else
 * 1 - continued fraction for Q (Lentz). Numerical Recipes structure. */
static double gammainc_P(Interp *I, double x, double a)
{
    if (x < 0.0 || a <= 0.0) runtime_error(I, "gammainc: requires x >= 0 and a > 0");
    if (x == 0.0) return 0.0;
    double lg = lgamma(a);
    if (x < a + 1.0) {                       /* series: P = e^{-x} x^a / Gamma(a) * sum */
        double ap = a, sum = 1.0 / a, del = sum;
        for (int i = 0; i < 500; i++) {
            ap += 1.0; del *= x / ap; sum += del;
            if (fabs(del) < fabs(sum) * 1e-16) break;
        }
        return sum * exp(-x + a * log(x) - lg);
    }
    double b = x + 1.0 - a, c = 1e300, d = 1.0 / b, h = d;   /* Lentz for Q */
    for (int i = 1; i < 500; i++) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b; if (fabs(d) < 1e-300) d = 1e-300;
        c = b + an / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d;
        double del = d * c; h *= del;
        if (fabs(del - 1.0) < 1e-16) break;
    }
    return 1.0 - exp(-x + a * log(x) - lg) * h;
}

/* Regularized incomplete beta I_x(a, b): Lentz continued fraction with the
 * symmetry I_x(a,b) = 1 - I_{1-x}(b,a) for convergence. */
static double betacf(double x, double a, double b)
{
    double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0, d = 1.0 - qab * x / qap;
    if (fabs(d) < 1e-300) d = 1e-300;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= 500; m++) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d; h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d; if (fabs(d) < 1e-300) d = 1e-300;
        c = 1.0 + aa / c; if (fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d;
        double del = d * c; h *= del;
        if (fabs(del - 1.0) < 1e-16) break;
    }
    return h;
}
static double betainc_I(Interp *I, double x, double a, double b)
{
    if (a <= 0.0 || b <= 0.0) runtime_error(I, "betainc: requires a > 0 and b > 0");
    if (x < 0.0 || x > 1.0)   runtime_error(I, "betainc: requires 0 <= x <= 1");
    if (x == 0.0) return 0.0;
    if (x == 1.0) return 1.0;
    double front = exp(lgamma(a + b) - lgamma(a) - lgamma(b)
                       + a * log(x) + b * log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) return front * betacf(x, a, b) / a;
    return 1.0 - front * betacf(1.0 - x, b, a) / b;
}

/* Normal quantile: Acklam's rational approximation refined by one Halley
 * step against erfc, giving ~1e-15 accuracy across (0, 1). */
static double norminv_d(Interp *I, double p)
{
    if (p < 0.0 || p > 1.0) runtime_error(I, "norminv: requires 0 <= p <= 1");
    if (p == 0.0) return -INFINITY;
    if (p == 1.0) return  INFINITY;
    static const double A[] = { -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02, -3.066479806614716e+01,
         2.506628277459239e+00 };
    static const double B[] = { -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01, -1.328068155288572e+01 };
    static const double C[] = { -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,  4.374664141464968e+00,
         2.938163982698783e+00 };
    static const double D[] = {  7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00 };
    double q, r, xx;
    if (p < 0.02425) {
        q = sqrt(-2.0 * log(p));
        xx = (((((C[0]*q+C[1])*q+C[2])*q+C[3])*q+C[4])*q+C[5]) /
             ((((D[0]*q+D[1])*q+D[2])*q+D[3])*q+1.0);
    } else if (p <= 0.97575) {
        q = p - 0.5; r = q * q;
        xx = (((((A[0]*r+A[1])*r+A[2])*r+A[3])*r+A[4])*r+A[5])*q /
             (((((B[0]*r+B[1])*r+B[2])*r+B[3])*r+B[4])*r+1.0);
    } else {
        q = sqrt(-2.0 * log(1.0 - p));
        xx = -(((((C[0]*q+C[1])*q+C[2])*q+C[3])*q+C[4])*q+C[5]) /
              ((((D[0]*q+D[1])*q+D[2])*q+D[3])*q+1.0);
    }
    double e = 0.5 * erfc(-xx / M_SQRT2) - p;                 /* one Halley step */
    double u = e * sqrt(2.0 * M_PI) * exp(xx * xx / 2.0);
    return xx - u / (1.0 + xx * u / 2.0);
}

/* Digamma psi(x): reflection for x < 0.5, recurrence up to x >= 6,
 * then the asymptotic series. */
static double digamma_d(Interp *I, double x)
{
    if (x <= 0.0 && x == floor(x)) runtime_error(I, "digamma: pole at non-positive integer");
    double result = 0.0;
    if (x < 0.5) {                            /* psi(x) = psi(1-x) - pi*cot(pi*x) */
        result -= M_PI / tan(M_PI * x);
        x = 1.0 - x;
    }
    while (x < 10.0) { result -= 1.0 / x; x += 1.0; }
    double inv = 1.0 / x, inv2 = inv * inv;
    result += log(x) - 0.5 * inv
            - inv2 * (1.0/12.0 - inv2 * (1.0/120.0 - inv2 * (1.0/252.0
              - inv2 * (1.0/240.0 - inv2 * (1.0/132.0)))));
    return result;
}

static Value sc_digamma(Interp *I, Value v) { return val_float(digamma_d(I, want_real(I, v, "digamma"))); }
static Value bi_digamma(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_digamma); }

static Value sc_norminv(Interp *I, Value v) { return val_float(norminv_d(I, want_real(I, v, "norminv"))); }
static Value bi_norminv(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_norminv); }

static Value sc_lbeta(Interp *I, Value a, Value b)
{
    double x = want_real(I, a, "lbeta"), y = want_real(I, b, "lbeta");
    if (x <= 0.0 || y <= 0.0) runtime_error(I, "lbeta: requires a > 0 and b > 0");
    return val_float(lgamma(x) + lgamma(y) - lgamma(x + y));
}
static Value bi_lbeta(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_lbeta); }
static Value sc_beta(Interp *I, Value a, Value b)
{
    Value l = sc_lbeta(I, a, b);
    return val_float(exp(l.as.f));
}
static Value bi_beta(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_beta); }

static Value sc_gammainc(Interp *I, Value xv, Value av)
{
    return val_float(gammainc_P(I, want_real(I, xv, "gammainc"), want_real(I, av, "gammainc")));
}
static Value bi_gammainc(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_gammainc); }

/* betainc(x, a, b): 3-arg — x may be an array; a, b are real scalars. */
static Value bi_betainc(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    double a = want_real(I, args[1], "betainc"), b = want_real(I, args[2], "betainc");
    Value xv = args[0];
    if (is_num(xv)) return val_float(betainc_I(I, want_real(I, xv, "betainc"), a, b));
    if (!is_array(xv)) runtime_error(I, "betainc: expected a real x, got %s", type_name(xv.kind));
    ArrObj *xa = as_arr(xv);
    if (xa->elt == ELT_COMPLEX) runtime_error(I, "betainc: expected real x");
    size_t nn = (size_t)xa->rows * xa->cols;
    Value out = val_array(ELT_FLOAT, xa->rows, xa->cols);
    for (size_t k = 0; k < nn; k++)
        ((double *)as_arr(out)->data)[k] = betainc_I(I, as_double(arr_get(xa, k)), a, b);
    return out;
}

/* Bessel J_n / Y_n: integer order n (scalar), elementwise over x. */
static Value sc_besselj(Interp *I, Value nv, Value xv)
{
    double nd = want_real(I, nv, "besselj");
    if (nd != floor(nd)) runtime_error(I, "besselj: order must be an integer");
    return val_float(jn((int)nd, want_real(I, xv, "besselj")));
}
static Value bi_besselj(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_besselj); }
static Value sc_bessely(Interp *I, Value nv, Value xv)
{
    double nd = want_real(I, nv, "bessely");
    if (nd != floor(nd)) runtime_error(I, "bessely: order must be an integer");
    double x = want_real(I, xv, "bessely");
    if (x <= 0.0) runtime_error(I, "bessely: requires x > 0");
    return val_float(yn((int)nd, x));
}
static Value bi_bessely(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_bessely); }

/* Kronecker product: (m x n) kron (p x q) -> (mp x nq), any numeric element types. */
static Value bi_kron(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    Value av = args[0], bv = args[1];
    if (is_num(av) && is_num(bv)) return scalar_arith_k(I, AR_MUL, numify(av), numify(bv));
    if (is_num(av)) return map_binary(I, av, bv, fold_mul);   /* scalar (x) B == scaling */
    if (is_num(bv)) return map_binary(I, av, bv, fold_mul);
    if (!is_array(av) || !is_array(bv))
        runtime_error(I, "kron: expected numeric arrays");
    ArrObj *A = as_arr(av), *B = as_arr(bv);
    uint64_t R = (uint64_t)A->rows * B->rows, C = (uint64_t)A->cols * B->cols;
    if (R > 100000000ULL || C > 100000000ULL || R * C > 100000000ULL)
        runtime_error(I, "kron: result too large (%llux%llu)", (unsigned long long)R, (unsigned long long)C);
    size_t cells = (size_t)(R * C);
    Value *tmp = malloc((cells ? cells : 1) * sizeof *tmp);
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    volatile size_t done = 0;
    if (setjmp(I->jmp)) array_build_abort(I, tmp, done, saved);
    for (size_t r = 0; r < R; r++) {                    /* output order: done stays exact */
        uint32_t i = (uint32_t)(r / B->rows), k = (uint32_t)(r % B->rows);
        for (size_t cc = 0; cc < C; cc++) {
            uint32_t j = (uint32_t)(cc / B->cols), l = (uint32_t)(cc % B->cols);
            Value aij = numify(arr_get(A, (size_t)i * A->cols + j));
            Value bkl = numify(arr_get(B, (size_t)k * B->cols + l));
            tmp[r * C + cc] = scalar_arith_k(I, AR_MUL, aij, bkl);
            done = r * C + cc + 1;
        }
    }
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    Value out = pack_array(tmp, cells, (uint32_t)R, (uint32_t)C);
    free(tmp);
    return out;
}

static Value sc_sign(Interp *I, Value v) { (void)I;
    if (v.kind == VAL_INT)   return val_int((v.as.i > 0) - (v.as.i < 0));
    if (v.kind == VAL_FLOAT) { double x = v.as.f; return val_float((double)((x > 0) - (x < 0))); }
    if (v.kind == VAL_COMPLEX) { double m = hypot(v.as.z.re, v.as.z.im);
        return m == 0.0 ? val_complex(0, 0) : val_complex(v.as.z.re/m, v.as.z.im/m); }
    runtime_error(I, "sign: expected a number, got %s", type_name(v.kind)); }
static Value bi_sign(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_sign); }

/* complex accessors (real numbers behave as z with a zero imaginary part) */
static Value sc_real(Interp *I, Value v) { (void)I;
    if (v.kind == VAL_COMPLEX)                       return val_float(v.as.z.re);
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT)    return v;
    runtime_error(I, "real: expected a number, got %s", type_name(v.kind)); }
static Value sc_imag(Interp *I, Value v) { (void)I;
    if (v.kind == VAL_COMPLEX)                       return val_float(v.as.z.im);
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT)    return val_float(0.0);
    runtime_error(I, "imag: expected a number, got %s", type_name(v.kind)); }
static Value sc_conj(Interp *I, Value v) { (void)I;
    if (v.kind == VAL_COMPLEX)                       return val_complex(v.as.z.re, -v.as.z.im);
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT)    return v;
    runtime_error(I, "conj: expected a number, got %s", type_name(v.kind)); }
static Value sc_angle(Interp *I, Value v) { (void)I;
    if (v.kind == VAL_COMPLEX)                       return val_float(atan2(v.as.z.im, v.as.z.re));
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT)    return val_float(atan2(0.0, as_double(v)));
    runtime_error(I, "angle: expected a number, got %s", type_name(v.kind)); }
static Value bi_real (Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_real); }
static Value bi_imag (Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_imag); }
static Value bi_conj (Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_conj); }
static Value bi_angle(Interp *I, Value *a, uint32_t n) { (void)n; return map_unary(I, a[0], sc_angle); }

/* binary elementwise (broadcasts array/scalar like the arithmetic ops) */
static Value map_binary(Interp *I, Value a, Value b, Value (*f)(Interp *, Value, Value))
{
    if (!is_array(a) && !is_array(b)) return f(I, a, b);
    bool aa, ba; uint32_t rows, cols;
    ew_dims(I, a, b, &aa, &ba, &rows, &cols);
    size_t nn = (size_t)rows * cols;
    Value *tmp = nn ? malloc(nn * sizeof *tmp) : nullptr;
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    volatile size_t done = 0;
    if (setjmp(I->jmp)) array_build_abort(I, tmp, done, saved);
    for (size_t k = 0; k < nn; k++) {
        Value av = aa ? arr_get(as_arr(a), k) : a;
        Value bv = ba ? arr_get(as_arr(b), k) : b;
        tmp[k] = f(I, av, bv); done = k + 1;
    }
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    Value r = pack_array(tmp, nn, rows, cols);
    free(tmp);
    return r;
}
static Value sc_atan2(Interp *I, Value y, Value x) {
    if (y.kind == VAL_COMPLEX || x.kind == VAL_COMPLEX) runtime_error(I, "atan2: expected real numbers");
    return val_float(atan2(as_double(y), as_double(x))); }
static Value sc_hypot(Interp *I, Value a, Value b) {
    if (a.kind == VAL_COMPLEX || b.kind == VAL_COMPLEX) runtime_error(I, "hypot: expected real numbers");
    return val_float(hypot(as_double(a), as_double(b))); }
static Value sc_mod(Interp *I, Value a, Value b) {
    if (a.kind == VAL_COMPLEX || b.kind == VAL_COMPLEX) runtime_error(I, "mod: expected real numbers");
    double x = as_double(a), y = as_double(b);
    double r = (y == 0.0) ? x : x - y * floor(x / y);
    if (a.kind == VAL_INT && b.kind == VAL_INT) return val_int((int64_t)llround(r));
    return val_float(r); }
static Value sc_rem(Interp *I, Value a, Value b) {
    if (a.kind == VAL_COMPLEX || b.kind == VAL_COMPLEX) runtime_error(I, "rem: expected real numbers");
    double x = as_double(a), y = as_double(b);
    double r = (y == 0.0) ? nan("") : fmod(x, y);
    if (a.kind == VAL_INT && b.kind == VAL_INT && y != 0.0) return val_int((int64_t)r);
    return val_float(r); }
static Value bi_atan2(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_atan2); }
static Value bi_hypot(Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_hypot); }
static Value bi_mod  (Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_mod); }
static Value bi_rem  (Interp *I, Value *a, uint32_t n) { (void)n; return map_binary(I, a[0], a[1], sc_rem); }

/* reductions over all elements */
static Value bi_min(Interp *I, Value *args, uint32_t n)
{
    if (n == 3) {                                     /* min(A, [], dim): axis reduction */
        if (!(is_array(args[1]) && (size_t)as_arr(args[1])->rows * as_arr(args[1])->cols == 0))
            runtime_error(I, "min: the 3-argument form is min(A, [], dim)");
        if (!is_array(args[0])) runtime_error(I, "min: the dim form needs an array");
        if (as_arr(args[0])->elt == ELT_COMPLEX) runtime_error(I, "min: undefined for complex");
        return reduce_dim(I, as_arr(args[0]), dim_arg(I, args[2], "min"), val_null(), fold_min);
    }
    if (n == 2) return map_binary(I, args[0], args[1], sc_min);
    Value v = args[0];
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "min: expected an array or number");
    ArrObj *a = as_arr(v);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "min: undefined for complex");
    size_t nn = (size_t)a->rows * a->cols;
    if (nn == 0) runtime_error(I, "min: empty array");
    Value best = arr_get(a, 0);
    for (size_t k = 1; k < nn; k++) { Value e = arr_get(a, k); if (as_double(e) < as_double(best)) best = e; }
    return best;
}
static Value bi_max(Interp *I, Value *args, uint32_t n)
{
    if (n == 3) {                                     /* max(A, [], dim): axis reduction */
        if (!(is_array(args[1]) && (size_t)as_arr(args[1])->rows * as_arr(args[1])->cols == 0))
            runtime_error(I, "max: the 3-argument form is max(A, [], dim)");
        if (!is_array(args[0])) runtime_error(I, "max: the dim form needs an array");
        if (as_arr(args[0])->elt == ELT_COMPLEX) runtime_error(I, "max: undefined for complex");
        return reduce_dim(I, as_arr(args[0]), dim_arg(I, args[2], "max"), val_null(), fold_max);
    }
    if (n == 2) return map_binary(I, args[0], args[1], sc_max);
    Value v = args[0];
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "max: expected an array or number");
    ArrObj *a = as_arr(v);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "max: undefined for complex");
    size_t nn = (size_t)a->rows * a->cols;
    if (nn == 0) runtime_error(I, "max: empty array");
    Value best = arr_get(a, 0);
    for (size_t k = 1; k < nn; k++) { Value e = arr_get(a, k); if (as_double(e) > as_double(best)) best = e; }
    return best;
}
/* ------------------------------------------------------------------ */
/* tic / toc                                                           */
/* ------------------------------------------------------------------ */

static double g_tic_when;
static bool   g_tic_set;

static double mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static Value bi_tic(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)args; (void)n;
    g_tic_when = mono_now();
    g_tic_set = true;
    return val_null();
}
static Value bi_toc(Interp *I, Value *args, uint32_t n)
{
    (void)args; (void)n;
    if (!g_tic_set) runtime_error(I, "toc: no timer started (call tic first)");
    return val_float(mono_now() - g_tic_when);
}

/* ------------------------------------------------------------------ */
/* unique                                                              */
/* ------------------------------------------------------------------ */

static int dbl_cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    if (isnan(x)) return isnan(y) ? 0 : 1;      /* NaNs sort last (total order: */
    if (isnan(y)) return -1;                    /* qsort needs transitivity)    */
    return (x > y) - (x < y);
}

static int i64_cmp(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* unique(A): sorted distinct elements. A vector keeps its orientation; a
 * matrix flattens to a row vector. NaNs compare unequal to everything,
 * themselves included, so they are all kept (Octave-compatible). */
static Value bi_unique(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    Value v = args[0];
    if (is_num(v) || v.kind == VAL_BOOL) return v;      /* scalar: already unique */
    if (!is_array(v)) runtime_error(I, "unique: expected numeric data, got %s", type_name(v.kind));
    ArrObj *a = as_arr(v);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "unique: complex data has no ordering");
    size_t nn = (size_t)a->rows * a->cols;
    if (nn == 0) return val_array(a->elt, 0, 0);
    bool col = (a->cols == 1 && a->rows > 1);           /* column vector keeps shape */

    if (a->elt == ELT_INT) {
        int64_t *buf = malloc(nn * sizeof *buf);
        if (!buf) abort();
        memcpy(buf, a->data, nn * sizeof *buf);
        qsort(buf, nn, sizeof *buf, i64_cmp);
        size_t k = 0;
        for (size_t i = 0; i < nn; i++)
            if (i == 0 || buf[i] != buf[k-1]) buf[k++] = buf[i];
        Value out = val_array(ELT_INT, col ? (uint32_t)k : 1, col ? 1 : (uint32_t)k);
        memcpy(as_arr(out)->data, buf, k * sizeof *buf);
        free(buf);
        return out;
    }
    if (a->elt == ELT_BOOL) {
        bool seen_f = false, seen_t = false;
        const uint8_t *bd = (const uint8_t *)a->data;
        for (size_t i = 0; i < nn; i++) { if (bd[i]) seen_t = true; else seen_f = true; }
        uint32_t k = (uint32_t)seen_f + (uint32_t)seen_t;
        Value out = val_array(ELT_BOOL, col ? k : 1, col ? 1 : k);
        uint8_t *od = (uint8_t *)as_arr(out)->data;
        uint32_t w = 0;
        if (seen_f) od[w++] = 0;
        if (seen_t) od[w++] = 1;
        return out;
    }
    double *buf = malloc(nn * sizeof *buf);
    if (!buf) abort();
    memcpy(buf, a->data, nn * sizeof *buf);
    qsort(buf, nn, sizeof *buf, dbl_cmp);
    size_t k = 0;
    for (size_t i = 0; i < nn; i++)
        if (i == 0 || !(buf[i] == buf[k-1])) buf[k++] = buf[i];   /* NaN != NaN: kept */
    Value out = val_array(ELT_FLOAT, col ? (uint32_t)k : 1, col ? 1 : (uint32_t)k);
    memcpy(as_arr(out)->data, buf, k * sizeof *buf);
    free(buf);
    return out;
}

/* ------------------------------------------------------------------ */
/* descriptive statistics: var, std, median, quantile                  */
/* ------------------------------------------------------------------ */

/* Copy a value's elements (or one dim-slice) into a double buffer.
 * Rejects complex; Bool converts as 0/1 like the arithmetic folds. */
static double stat_elt(Interp *I, Value e, const char *who)
{
    if (e.kind == VAL_BOOL) return e.as.b ? 1.0 : 0.0;
    if (e.kind != VAL_INT && e.kind != VAL_FLOAT)
        runtime_error(I, "%s: expected real data, got %s", who, type_name(e.kind));
    return as_double(e);
}

typedef double (*StatKernel)(Interp *, double *, size_t, double);

/* Two-pass variance. w = 0: sample (N-1, default); w = 1: population (N). */
static double st_var(Interp *I, double *buf, size_t n, double w)
{
    (void)I;
    if (n == 1) return 0.0;
    double m = 0.0;
    for (size_t k = 0; k < n; k++) m += buf[k];
    m /= (double)n;
    double ss = 0.0;
    for (size_t k = 0; k < n; k++) { double d = buf[k] - m; ss += d * d; }
    return ss / ((w == 1.0) ? (double)n : (double)(n - 1));
}
static double st_std(Interp *I, double *buf, size_t n, double w)
{
    return sqrt(st_var(I, buf, n, w));
}
static double st_median(Interp *I, double *buf, size_t n, double unused)
{
    (void)I; (void)unused;
    qsort(buf, n, sizeof *buf, dbl_cmp);
    return (n & 1) ? buf[n / 2] : 0.5 * (buf[n/2 - 1] + buf[n/2]);
}
/* Linear interpolation between order statistics (NumPy default / R type 7). */
static double st_quantile(Interp *I, double *buf, size_t n, double p)
{
    (void)I;
    qsort(buf, n, sizeof *buf, dbl_cmp);
    if (n == 1) return buf[0];
    double h = (double)(n - 1) * p;
    size_t lo = (size_t)h;
    if (lo >= n - 1) return buf[n - 1];
    double frac = h - (double)lo;
    return buf[lo] + frac * (buf[lo + 1] - buf[lo]);
}

/* Apply kernel to every element of v (scalar / whole array). */
static Value stat_all(Interp *I, Value v, StatKernel f, double param, const char *who)
{
    if (is_num(v) && v.kind != VAL_COMPLEX) {
        double d = stat_elt(I, v, who);
        return val_float(f(I, &d, 1, param));
    }
    if (!is_array(v)) runtime_error(I, "%s: expected numeric data, got %s", who, type_name(v.kind));
    ArrObj *a = as_arr(v);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "%s: complex data has no ordering", who);
    size_t n = (size_t)a->rows * a->cols;
    if (n == 0) runtime_error(I, "%s: empty data", who);
    double *buf = malloc(n * sizeof *buf);
    if (!buf) abort();
    for (size_t k = 0; k < n; k++) buf[k] = stat_elt(I, arr_get(a, k), who);
    double r = f(I, buf, n, param);
    free(buf);
    return val_float(r);
}

/* Apply kernel along dim (1 = down columns, 2 = across rows). */
static Value stat_dim(Interp *I, ArrObj *a, int dim, StatKernel f, double param, const char *who)
{
    if (a->elt == ELT_COMPLEX) runtime_error(I, "%s: complex data has no ordering", who);
    uint32_t slices = (dim == 1) ? a->cols : a->rows;
    uint32_t len    = (dim == 1) ? a->rows : a->cols;
    if (len == 0) runtime_error(I, "%s: empty dimension", who);
    double *buf = malloc((size_t)len * sizeof *buf);
    if (!buf) abort();
    Value out = val_array(ELT_FLOAT, dim == 1 ? 1 : a->rows, dim == 1 ? a->cols : 1);
    double *od = (double *)as_arr(out)->data;
    for (uint32_t s = 0; s < slices; s++) {
        for (uint32_t k = 0; k < len; k++) {
            size_t idx = (dim == 1) ? (size_t)k * a->cols + s : (size_t)s * a->cols + k;
            buf[k] = stat_elt(I, arr_get(a, idx), who);
        }
        od[s] = f(I, buf, len, param);
    }
    free(buf);
    return out;
}

/* Load column c of a into buf (real data only). */
static void stat_col(Interp *I, ArrObj *a, uint32_t c, double *buf, const char *who)
{
    for (uint32_t r = 0; r < a->rows; r++)
        buf[r] = stat_elt(I, arr_get(a, (size_t)r * a->cols + c), who);
}

/* Covariance matrix of X's columns (rows = observations), or of two vectors. */
static Value cov_matrix(Interp *I, ArrObj *X, double w, const char *who, bool to_corr)
{
    if (X->elt == ELT_COMPLEX) runtime_error(I, "%s: complex data has no ordering", who);
    uint32_t n = X->rows, p = X->cols;
    if (n == 0 || p == 0) runtime_error(I, "%s: empty data", who);
    double *cols = malloc((size_t)n * p * sizeof *cols);
    double *mu   = malloc((size_t)p * sizeof *mu);
    if (!cols || !mu) abort();
    for (uint32_t c = 0; c < p; c++) {
        stat_col(I, X, c, cols + (size_t)c * n, who);
        double m = 0.0;
        for (uint32_t r = 0; r < n; r++) m += cols[(size_t)c * n + r];
        mu[c] = m / (double)n;
    }
    double denom = (n == 1) ? 1.0 : ((w == 1.0) ? (double)n : (double)(n - 1));
    Value out = val_array(ELT_FLOAT, p, p);
    double *od = (double *)as_arr(out)->data;
    for (uint32_t i = 0; i < p; i++)
        for (uint32_t j = i; j < p; j++) {
            double s = 0.0;
            const double *xi = cols + (size_t)i * n, *xj = cols + (size_t)j * n;
            for (uint32_t r = 0; r < n; r++) s += (xi[r] - mu[i]) * (xj[r] - mu[j]);
            double cij = (n == 1) ? 0.0 : s / denom;
            od[(size_t)i * p + j] = od[(size_t)j * p + i] = cij;
        }
    free(cols); free(mu);
    if (to_corr) {                             /* snapshot sds first: normalizing in place
                                                  would corrupt diagonals still to be read */
        double *sd = malloc((size_t)p * sizeof *sd);
        if (!sd) abort();
        for (uint32_t i = 0; i < p; i++) sd[i] = sqrt(od[(size_t)i * p + i]);
        for (uint32_t i = 0; i < p; i++)
            for (uint32_t j = 0; j < p; j++)
                od[(size_t)i * p + j] = (i == j && sd[i] > 0.0)
                                      ? 1.0
                                      : od[(size_t)i * p + j] / (sd[i] * sd[j]);   /* 0-variance -> nan */
        free(sd);
    }
    return out;
}

/* Validate a real vector argument; return its length (no allocation). */
static size_t stat_veclen(Interp *I, Value v, const char *who)
{
    if (!is_array(v)) runtime_error(I, "%s: expected a vector, got %s", who, type_name(v.kind));
    ArrObj *a = as_arr(v);
    if (a->rows != 1 && a->cols != 1) runtime_error(I, "%s: expected a vector, got %ux%u", who, a->rows, a->cols);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "%s: complex data has no ordering", who);
    size_t n = (size_t)a->rows * a->cols;
    if (n == 0) runtime_error(I, "%s: empty data", who);
    return n;
}

/* Scalar covariance of two equal-length vectors. All validation happens
 * before any allocation, so no error path needs cleanup. */
static double cov_pair(Interp *I, Value xv, Value yv, double w, const char *who)
{
    size_t nx = stat_veclen(I, xv, who);
    size_t ny = stat_veclen(I, yv, who);
    if (nx != ny) runtime_error(I, "%s: vectors differ in length (%zu vs %zu)", who, nx, ny);
    double *x = malloc(nx * sizeof *x), *y = malloc(nx * sizeof *y);
    if (!x || !y) abort();
    ArrObj *xa = as_arr(xv), *ya = as_arr(yv);
    for (size_t k = 0; k < nx; k++) {
        x[k] = stat_elt(I, arr_get(xa, k), who);   /* element kinds already vetted */
        y[k] = stat_elt(I, arr_get(ya, k), who);
    }
    double mx = 0.0, my = 0.0;
    for (size_t k = 0; k < nx; k++) { mx += x[k]; my += y[k]; }
    mx /= (double)nx; my /= (double)nx;
    double s = 0.0, sx = 0.0, sy = 0.0;
    for (size_t k = 0; k < nx; k++) {
        s  += (x[k] - mx) * (y[k] - my);
        sx += (x[k] - mx) * (x[k] - mx);
        sy += (y[k] - my) * (y[k] - my);
    }
    free(x); free(y);
    if (who[2] == 'r') {                       /* "corr": normalize */
        return s / sqrt(sx * sy);              /* 0-variance -> nan */
    }
    double denom = (nx == 1) ? 1.0 : ((w == 1.0) ? (double)nx : (double)(nx - 1));
    return (nx == 1) ? 0.0 : s / denom;
}

/* cov(X[, w]) | cov(x, y[, w]);  corr(X) | corr(x, y). */
static Value bi_cov(Interp *I, Value *args, uint32_t n)
{
    double w = 0.0;
    bool pair = (n >= 2 && is_array(args[1]));
    uint32_t wpos = pair ? 2 : 1;
    if (n > wpos) {
        double wv = stat_elt(I, args[wpos], "cov");
        if (wv != 0.0 && wv != 1.0) runtime_error(I, "cov: normalization must be 0 (N-1) or 1 (N)");
        w = wv;
    }
    if (pair) return val_float(cov_pair(I, args[0], args[1], w, "cov"));
    if (!is_array(args[0])) runtime_error(I, "cov: expected numeric data, got %s", type_name(args[0].kind));
    ArrObj *X = as_arr(args[0]);
    if (X->rows == 1 || X->cols == 1) return val_float(cov_pair(I, args[0], args[0], w, "cov"));
    return cov_matrix(I, X, w, "cov", false);
}
static Value bi_corr(Interp *I, Value *args, uint32_t n)
{
    if (n == 2) return val_float(cov_pair(I, args[0], args[1], 0.0, "corr"));
    if (!is_array(args[0])) runtime_error(I, "corr: expected numeric data, got %s", type_name(args[0].kind));
    ArrObj *X = as_arr(args[0]);
    if (X->rows == 1 || X->cols == 1) return val_float(cov_pair(I, args[0], args[0], 0.0, "corr"));
    return cov_matrix(I, X, 0.0, "corr", true);
}

/* var(A) | var(A, w) | var(A, w, dim); w = 0 (N-1, default) or 1 (N). */
static Value stat_var_common(Interp *I, Value *args, uint32_t n, StatKernel f, const char *who)
{
    double w = 0.0;
    if (n >= 2) {
        if (args[1].kind == VAL_NULL || (is_array(args[1]) && (size_t)as_arr(args[1])->rows * as_arr(args[1])->cols == 0)) w = 0.0;   /* [] placeholder */
        else {
            double wv = stat_elt(I, args[1], who);
            if (wv != 0.0 && wv != 1.0) runtime_error(I, "%s: normalization must be 0 (N-1) or 1 (N)", who);
            w = wv;
        }
    }
    if (n == 3) {
        if (!is_array(args[0])) runtime_error(I, "%s: the dim form needs an array", who);
        return stat_dim(I, as_arr(args[0]), dim_arg(I, args[2], who), f, w, who);
    }
    return stat_all(I, args[0], f, w, who);
}
static Value bi_var(Interp *I, Value *args, uint32_t n) { return stat_var_common(I, args, n, st_var, "var"); }
static Value bi_std(Interp *I, Value *args, uint32_t n) { return stat_var_common(I, args, n, st_std, "std"); }

static Value bi_median(Interp *I, Value *args, uint32_t n)
{
    if (n == 2) {
        if (!is_array(args[0])) runtime_error(I, "median: the dim form needs an array");
        return stat_dim(I, as_arr(args[0]), dim_arg(I, args[1], "median"), st_median, 0.0, "median");
    }
    return stat_all(I, args[0], st_median, 0.0, "median");
}

/* quantile(x, p): p a probability in [0, 1], scalar or vector -> matching shape. */
static Value bi_quantile(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    Value pv = args[1];
    if (is_num(pv)) {
        double p = stat_elt(I, pv, "quantile");
        if (p < 0.0 || p > 1.0) runtime_error(I, "quantile: p must be in [0, 1]");
        return stat_all(I, args[0], st_quantile, p, "quantile");
    }
    if (!is_array(pv)) runtime_error(I, "quantile: p must be a probability or a vector of them");
    ArrObj *pa = as_arr(pv);
    if (pa->elt == ELT_COMPLEX || (pa->rows != 1 && pa->cols != 1))
        runtime_error(I, "quantile: p must be a probability or a vector of them");
    size_t np = (size_t)pa->rows * pa->cols;
    if (np == 0) runtime_error(I, "quantile: empty p");
    Value out = val_array(ELT_FLOAT, pa->rows, pa->cols);
    double *od = (double *)as_arr(out)->data;
    for (size_t k = 0; k < np; k++) {
        double p = stat_elt(I, arr_get(pa, k), "quantile");
        if (p < 0.0 || p > 1.0) runtime_error(I, "quantile: p must be in [0, 1]");
        Value r = stat_all(I, args[0], st_quantile, p, "quantile");
        od[k] = r.as.f;
    }
    return out;
}

static Value bi_mean(Interp *I, Value *args, uint32_t n)
{
    Value v = args[0];
    if (n == 2) {
        if (!is_array(v)) runtime_error(I, "mean: the dim form needs an array");
        int dim = dim_arg(I, args[1], "mean");
        ArrObj *a = as_arr(v);
        int64_t len = (dim == 1) ? a->rows : a->cols;
        if (len == 0) runtime_error(I, "mean: empty dimension");
        Value sums = reduce_dim(I, a, dim, val_int(0), fold_add);
        ArrObj *so = as_arr(sums);
        size_t sn = (size_t)so->rows * so->cols;
        Value *tmp = malloc(sizeof(Value) * (sn ? sn : 1));
        for (size_t k = 0; k < sn; k++) tmp[k] = scalar_arith_k(I, AR_DIV, arr_get(so, k), val_int(len));
        Value out = pack_array(tmp, sn, so->rows, so->cols);
        free(tmp); value_release(sums);
        return out;
    }
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "mean: expected an array or number");
    ArrObj *a = as_arr(v);
    size_t nn = (size_t)a->rows * a->cols;
    if (nn == 0) runtime_error(I, "mean: empty array");
    Value acc = val_int(0);
    for (size_t k = 0; k < nn; k++) acc = scalar_arith_k(I, AR_ADD, acc, arr_get(a, k));
    return scalar_arith_k(I, AR_DIV, acc, val_int((int64_t)nn));
}
static Value bi_prod(Interp *I, Value *args, uint32_t n)
{
    Value v = args[0];
    if (n == 2) {
        if (!is_array(v)) runtime_error(I, "prod: the dim form needs an array");
        return reduce_dim(I, as_arr(v), dim_arg(I, args[1], "prod"), val_int(1), fold_mul);
    }
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "prod: expected an array or number");
    ArrObj *a = as_arr(v);
    size_t nn = (size_t)a->rows * a->cols;
    Value acc = val_int(1);
    for (size_t k = 0; k < nn; k++) acc = scalar_arith_k(I, AR_MUL, acc, arr_get(a, k));
    return acc;
}

/* ---------- array utilities ---------- */

static bool elt_nonzero(Value e)
{
    switch (e.kind) {
    case VAL_BOOL:    return e.as.b;
    case VAL_INT:     return e.as.i != 0;
    case VAL_FLOAT:   return e.as.f != 0.0;
    case VAL_COMPLEX: return e.as.z.re != 0.0 || e.as.z.im != 0.0;
    default:          return false;
    }
}

static int64_t as_count(Interp *I, Value v, const char *name)
{
    if (v.kind == VAL_INT) return v.as.i;
    if (v.kind == VAL_FLOAT && v.as.f == (double)(int64_t)v.as.f) return (int64_t)v.as.f;
    runtime_error(I, "%s: expected an integer count", name);
}

static Value bi_length(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n;
    if (!is_array(args[0])) return val_int(1);
    ArrObj *a = as_arr(args[0]);
    if ((size_t)a->rows * a->cols == 0) return val_int(0);
    return val_int(a->rows > a->cols ? a->rows : a->cols);
}

static Value bi_numel(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n;
    if (!is_array(args[0])) return val_int(1);
    ArrObj *a = as_arr(args[0]);
    return val_int((int64_t)a->rows * a->cols);
}

static Value bi_find(Interp *I, Value *args, uint32_t n);
static Value bi_where(Interp *I, Value *args, uint32_t n)
{
    if (n == 1) return bi_find(I, args, 1);            /* where(mask) -> indices, like find */
    if (n != 3) runtime_error(I, "where: takes 1 argument (indices) or 3 (select)");
    Value m = args[0], a = args[1], b = args[2];       /* where(mask, a, b): pick a where true, else b */
    Value src[3] = { m, a, b }; bool isa[3];
    uint32_t rows = 0, cols = 0; bool have = false;
    for (int i = 0; i < 3; i++) {
        isa[i] = is_array(src[i]);
        if (isa[i]) {
            uint32_t r = as_arr(src[i])->rows, c = as_arr(src[i])->cols;
            if (!have) { rows = r; cols = c; have = true; }
            else if (r != rows || c != cols)
                runtime_error(I, "where: shape mismatch (%ux%u vs %ux%u)", rows, cols, r, c);
        }
    }
    if (!have) return elt_nonzero(m) ? value_retain(a) : value_retain(b);
    size_t nn = (size_t)rows * cols;
    Value *tmp = nn ? malloc(nn * sizeof *tmp) : nullptr;
    for (size_t k = 0; k < nn; k++) {
        Value mv = isa[0] ? arr_get(as_arr(m), k) : m;
        Value av = isa[1] ? arr_get(as_arr(a), k) : a;
        Value bv = isa[2] ? arr_get(as_arr(b), k) : b;
        tmp[k] = elt_nonzero(mv) ? av : bv;
    }
    Value r = pack_array(tmp, nn, rows, cols);
    free(tmp);
    return r;
}

static Value bi_find(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n; Value v = args[0];
    if (!is_array(v)) {
        if (!elt_nonzero(v)) return val_array(ELT_INT, 0, 0);
        Value out = val_array(ELT_INT, 1, 1); ((int64_t *)as_arr(out)->data)[0] = 1; return out;
    }
    ArrObj *a = as_arr(v);
    size_t nn = (size_t)a->rows * a->cols, cnt = 0;
    for (size_t k = 0; k < nn; k++) if (elt_nonzero(arr_get(a, k))) cnt++;
    bool row = (a->rows == 1);                         /* row vector -> row result, else column */
    Value out = val_array(ELT_INT, row ? 1 : (uint32_t)cnt, row ? (uint32_t)cnt : 1);
    int64_t *od = (int64_t *)as_arr(out)->data;
    size_t w = 0;
    for (size_t k = 0; k < nn; k++) if (elt_nonzero(arr_get(a, k))) od[w++] = (int64_t)(k + 1);
    return out;
}

static int cmp_val_asc(const void *x, const void *y)
{
    double a = as_double(*(const Value *)x), b = as_double(*(const Value *)y);
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static Value bi_sort(Interp *I, Value *args, uint32_t n)
{
    (void)n; Value v = args[0];
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "sort: expected an array or number");
    ArrObj *a = as_arr(v);
    if (a->elt == ELT_COMPLEX) runtime_error(I, "sort: undefined for complex");
    uint32_t R = a->rows, C = a->cols;
    Value out = val_array(a->elt, R, C); ArrObj *o = as_arr(out);
    if (R == 1 || C == 1) {                            /* vector: one sequence */
        size_t nn = (size_t)R * C;
        Value *buf = nn ? malloc(nn * sizeof *buf) : nullptr;
        for (size_t k = 0; k < nn; k++) buf[k] = arr_get(a, k);
        qsort(buf, nn, sizeof *buf, cmp_val_asc);
        for (size_t k = 0; k < nn; k++) arr_set(o, k, buf[k]);
        free(buf);
    } else {                                           /* matrix: sort each column */
        Value *col = malloc(R * sizeof *col);
        for (uint32_t c = 0; c < C; c++) {
            for (uint32_t r = 0; r < R; r++) col[r] = arr_get(a, (size_t)r * C + c);
            qsort(col, R, sizeof *col, cmp_val_asc);
            for (uint32_t r = 0; r < R; r++) arr_set(o, (size_t)r * C + c, col[r]);
        }
        free(col);
    }
    return out;
}

static Value numify(Value e) { return e.kind == VAL_BOOL ? val_int(e.as.b ? 1 : 0) : e; }

static Value cumulate(Interp *I, Value v, Arith op, const char *name)
{
    if (is_num(v)) return value_retain(v);
    if (!is_array(v)) runtime_error(I, "%s: expected an array or number", name);
    ArrObj *a = as_arr(v);
    uint32_t R = a->rows, C = a->cols;
    Value out = val_array(a->elt == ELT_BOOL ? ELT_INT : a->elt, R, C);
    ArrObj *o = as_arr(out);
    Value id = (op == AR_MUL) ? val_int(1) : val_int(0);
    if (R == 1 || C == 1) {
        size_t nn = (size_t)R * C; Value acc = id;
        for (size_t k = 0; k < nn; k++) { acc = scalar_arith_k(I, op, acc, numify(arr_get(a, k))); arr_set(o, k, acc); }
    } else {
        for (uint32_t c = 0; c < C; c++) {
            Value acc = id;
            for (uint32_t r = 0; r < R; r++) {
                acc = scalar_arith_k(I, op, acc, numify(arr_get(a, (size_t)r * C + c)));
                arr_set(o, (size_t)r * C + c, acc);
            }
        }
    }
    return out;
}
static Value bi_cumsum(Interp *I, Value *args, uint32_t n)  { (void)n; return cumulate(I, args[0], AR_ADD, "cumsum"); }
static Value bi_cumprod(Interp *I, Value *args, uint32_t n) { (void)n; return cumulate(I, args[0], AR_MUL, "cumprod"); }

static Value bi_diff(Interp *I, Value *args, uint32_t n)
{
    (void)n; Value v = args[0];
    if (!is_array(v)) runtime_error(I, "diff: expected an array");
    ArrObj *a = as_arr(v);
    uint32_t R = a->rows, C = a->cols;
    EltType relt = a->elt == ELT_BOOL ? ELT_INT : a->elt;
    if (R == 1 || C == 1) {
        size_t nn = (size_t)R * C, m = nn ? nn - 1 : 0;
        Value out = val_array(relt, R == 1 ? 1 : (uint32_t)m, R == 1 ? (uint32_t)m : 1);
        ArrObj *o = as_arr(out);
        for (size_t k = 0; k + 1 < nn; k++)
            arr_set(o, k, scalar_arith_k(I, AR_SUB, numify(arr_get(a, k + 1)), numify(arr_get(a, k))));
        return out;
    }
    Value out = val_array(relt, R ? R - 1 : 0, C); ArrObj *o = as_arr(out);
    for (uint32_t c = 0; c < C; c++)
        for (uint32_t r = 0; r + 1 < R; r++)
            arr_set(o, (size_t)r * C + c,
                    scalar_arith_k(I, AR_SUB, numify(arr_get(a, (size_t)(r + 1) * C + c)),
                                              numify(arr_get(a, (size_t)r * C + c))));
    return out;
}

static Value bi_repmat(Interp *I, Value *args, uint32_t n)
{
    Value v = args[0];
    int64_t M = as_count(I, args[1], "repmat");
    int64_t N = (n == 3) ? as_count(I, args[2], "repmat") : M;
    if (M < 0 || N < 0) runtime_error(I, "repmat: negative tile count");
    bool arr = is_array(v);
    uint32_t R = arr ? as_arr(v)->rows : 1, C = arr ? as_arr(v)->cols : 1;
    EltType relt = arr ? as_arr(v)->elt : scalar_elt(I, v);
    uint32_t OR = (uint32_t)(R * (uint32_t)M), OC = (uint32_t)(C * (uint32_t)N);
    Value out = val_array(relt, OR, OC); ArrObj *o = as_arr(out);
    for (uint32_t i = 0; i < OR; i++)
        for (uint32_t j = 0; j < OC; j++)
            arr_set(o, (size_t)i * OC + j, arr ? arr_get(as_arr(v), (size_t)(i % R) * C + (j % C)) : v);
    return out;
}

static Value bi_flipud(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n; Value v = args[0];
    if (!is_array(v)) return value_retain(v);
    ArrObj *a = as_arr(v); uint32_t R = a->rows, C = a->cols;
    Value out = val_array(a->elt, R, C); ArrObj *o = as_arr(out);
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t c = 0; c < C; c++)
            arr_set(o, (size_t)r * C + c, arr_get(a, (size_t)(R - 1 - r) * C + c));
    return out;
}

static Value bi_fliplr(Interp *I, Value *args, uint32_t n)
{
    (void)I; (void)n; Value v = args[0];
    if (!is_array(v)) return value_retain(v);
    ArrObj *a = as_arr(v); uint32_t R = a->rows, C = a->cols;
    Value out = val_array(a->elt, R, C); ArrObj *o = as_arr(out);
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t c = 0; c < C; c++)
            arr_set(o, (size_t)r * C + c, arr_get(a, (size_t)r * C + (C - 1 - c)));
    return out;
}

/* ---- random number generation ---- */
static void rng_dims(Interp *I, Value *args, uint32_t n, uint32_t off,
                     const char *name, uint32_t *rows, uint32_t *cols)
{
    uint32_t nd = n - off;
    if (nd == 0) { *rows = *cols = 1; return; }
    int64_t r = as_count(I, args[off], name);
    int64_t c = (nd >= 2) ? as_count(I, args[off + 1], name) : r;   /* one dim -> square */
    if (r < 0 || c < 0) runtime_error(I, "%s: negative size", name);
    *rows = (uint32_t)r; *cols = (uint32_t)c;
}

static Value bi_rng(Interp *I, Value *args, uint32_t n)
{
    (void)n;
    rng_seed(I, (uint64_t)as_count(I, args[0], "rng"));
    return val_null();
}

static Value bi_rand(Interp *I, Value *args, uint32_t n)
{
    if (n == 0) return val_float(rng_uniform(I));
    uint32_t R, C; rng_dims(I, args, n, 0, "rand", &R, &C);
    Value out = val_array(ELT_FLOAT, R, C);
    double *d = (double *)as_arr(out)->data;
    size_t nn = (size_t)R * C;
    for (size_t k = 0; k < nn; k++) d[k] = rng_uniform(I);
    return out;
}

static Value bi_randn(Interp *I, Value *args, uint32_t n)
{
    if (n == 0) { double z0, z1; rng_normal_pair(I, &z0, &z1); return val_float(z0); }
    uint32_t R, C; rng_dims(I, args, n, 0, "randn", &R, &C);
    Value out = val_array(ELT_FLOAT, R, C);
    double *d = (double *)as_arr(out)->data;
    size_t nn = (size_t)R * C;
    for (size_t k = 0; k + 1 < nn; k += 2) rng_normal_pair(I, &d[k], &d[k + 1]);
    if (nn & 1) { double z0, z1; rng_normal_pair(I, &z0, &z1); d[nn - 1] = z0; }
    return out;
}

static Value bi_randi(Interp *I, Value *args, uint32_t n)
{
    int64_t lo = 1, hi;
    if (is_array(args[0])) {
        ArrObj *a = as_arr(args[0]);
        if ((size_t)a->rows * a->cols != 2) runtime_error(I, "randi: range must be [lo, hi]");
        lo = as_count(I, arr_get(a, 0), "randi");
        hi = as_count(I, arr_get(a, 1), "randi");
    } else {
        hi = as_count(I, args[0], "randi");
    }
    if (hi < lo) runtime_error(I, "randi: empty range");
    int64_t span = hi - lo + 1;
    if (n == 1) return val_int(lo + (int64_t)(rng_uniform(I) * (double)span));
    uint32_t R, C; rng_dims(I, args, n, 1, "randi", &R, &C);
    Value out = val_array(ELT_INT, R, C);
    int64_t *d = (int64_t *)as_arr(out)->data;
    size_t nn = (size_t)R * C;
    for (size_t k = 0; k < nn; k++) d[k] = lo + (int64_t)(rng_uniform(I) * (double)span);
    return out;
}

/* ---- floating-point predicates (elementwise -> logical) ---- */
static Value sc_isnan(Interp *I, Value v)
{
    (void)I;
    switch (v.kind) {
    case VAL_FLOAT:   return val_bool(isnan(v.as.f));
    case VAL_COMPLEX: return val_bool(isnan(v.as.z.re) || isnan(v.as.z.im));
    default:          return val_bool(false);            /* int, bool: never NaN */
    }
}
static Value sc_isinf(Interp *I, Value v)
{
    (void)I;
    switch (v.kind) {
    case VAL_FLOAT:   return val_bool(isinf(v.as.f));
    case VAL_COMPLEX: return val_bool(isinf(v.as.z.re) || isinf(v.as.z.im));
    default:          return val_bool(false);
    }
}
static Value sc_isfinite(Interp *I, Value v)
{
    (void)I;
    switch (v.kind) {
    case VAL_FLOAT:   return val_bool(isfinite(v.as.f));
    case VAL_COMPLEX: return val_bool(isfinite(v.as.z.re) && isfinite(v.as.z.im));
    default:          return val_bool(true);             /* int, bool: always finite */
    }
}
static Value bi_isnan(Interp *I, Value *args, uint32_t n)    { (void)n; return map_unary(I, args[0], sc_isnan); }
static Value bi_isinf(Interp *I, Value *args, uint32_t n)    { (void)n; return map_unary(I, args[0], sc_isinf); }
static Value bi_isfinite(Interp *I, Value *args, uint32_t n) { (void)n; return map_unary(I, args[0], sc_isfinite); }

static double cmp_key(Interp *I, Value v)
{
    switch (v.kind) {
    case VAL_INT:   return (double)v.as.i;
    case VAL_FLOAT: return v.as.f;
    case VAL_BOOL:  return v.as.b ? 1.0 : 0.0;
    default:        runtime_error(I, "min/max: undefined for %s", type_name(v.kind));
    }
}
static Value sc_min(Interp *I, Value a, Value b) { return cmp_key(I, a) <= cmp_key(I, b) ? a : b; }
static Value sc_max(Interp *I, Value a, Value b) { return cmp_key(I, a) >= cmp_key(I, b) ? a : b; }

static void def_builtin(EnvObj *e, const char *name, BuiltinFn fn, uint32_t lo, uint32_t hi)
{
    Value b = val_builtin(name, fn, lo, hi);
    env_define(e, name, (uint32_t)strlen(name), b);
    value_release(b);
}

EnvObj *globals_new(void)
{
    EnvObj *e = env_new(nullptr);
    def_builtin(e, "print", bi_print, 0, UINT32_MAX);
    def_builtin(e, "sum",   bi_sum,   1, 2);
    def_builtin(e, "size",  bi_size,  1, 1);
    def_builtin(e, "map",   bi_map,   2, 2);
    def_builtin(e, "abs",   bi_abs,   1, 1);
    def_builtin(e, "sqrt",  bi_sqrt,  1, 1);
    def_builtin(e, "zeros", bi_zeros, 2, 2);
    def_builtin(e, "ones",  bi_ones,  2, 2);
    def_builtin(e, "any",   bi_any,   1, 2);
    def_builtin(e, "all",   bi_all,   1, 2);
    def_builtin(e, "eye",     bi_eye,     1, 1);
    def_builtin(e, "diag",    bi_diag,    1, 1);
    def_builtin(e, "trace",   bi_trace,   1, 1);
    def_builtin(e, "det",     bi_det,     1, 1);
    def_builtin(e, "inv",     bi_inv,     1, 1);
    def_builtin(e, "dot",     bi_dot,     2, 2);
    def_builtin(e, "norm",    bi_norm,    1, 2);
    def_builtin(e, "kron",    bi_kron,    2, 2);
    def_builtin(e, "reshape", bi_reshape, 3, 3);
    def_builtin(e, "linspace",bi_linspace,3, 3);
    def_builtin(e, "lu",      bi_lu,      1, 1);
    def_builtin(e, "qr",      bi_qr,      1, 1);
    def_builtin(e, "chol",    bi_chol,    1, 1);
    def_builtin(e, "eig",     bi_eig,     1, 1);
    def_builtin(e, "svd",     bi_svd,     1, 1);
    def_builtin(e, "exp",     bi_exp,     1, 1);
    def_builtin(e, "log",     bi_log,     1, 1);
    def_builtin(e, "sin",     bi_sin,     1, 1);
    def_builtin(e, "cos",     bi_cos,     1, 1);
    def_builtin(e, "tan",     bi_tan,     1, 1);
    def_builtin(e, "floor",   bi_floor,   1, 1);
    def_builtin(e, "ceil",    bi_ceil,    1, 1);
    def_builtin(e, "round",   bi_round,   1, 1);
    def_builtin(e, "trunc",   bi_trunc,   1, 1);
    def_builtin(e, "ln",      bi_log,     1, 1);
    def_builtin(e, "log10",   bi_log10,   1, 1);
    def_builtin(e, "log2",    bi_log2,    1, 1);
    def_builtin(e, "asin",    bi_asin,    1, 1);
    def_builtin(e, "acos",    bi_acos,    1, 1);
    def_builtin(e, "atan",    bi_atan,    1, 1);
    def_builtin(e, "sinh",    bi_sinh,    1, 1);
    def_builtin(e, "cosh",    bi_cosh,    1, 1);
    def_builtin(e, "tanh",    bi_tanh,    1, 1);
    def_builtin(e, "asinh",   bi_asinh,   1, 1);
    def_builtin(e, "acosh",   bi_acosh,   1, 1);
    def_builtin(e, "atanh",   bi_atanh,   1, 1);
    def_builtin(e, "sign",    bi_sign,    1, 1);
    def_builtin(e, "real",    bi_real,    1, 1);
    def_builtin(e, "imag",    bi_imag,    1, 1);
    def_builtin(e, "conj",    bi_conj,    1, 1);
    def_builtin(e, "angle",   bi_angle,   1, 1);
    def_builtin(e, "arg",     bi_angle,   1, 1);
    def_builtin(e, "cbrt",    bi_cbrt,    1, 1);
    def_builtin(e, "gamma",   bi_gamma,   1, 1);
    def_builtin(e, "lgamma",  bi_lgamma,  1, 1);
    def_builtin(e, "erf",     bi_erf,     1, 1);
    def_builtin(e, "erfc",    bi_erfc,    1, 1);
    def_builtin(e, "beta",    bi_beta,    2, 2);
    def_builtin(e, "lbeta",   bi_lbeta,   2, 2);
    def_builtin(e, "gammainc",bi_gammainc,2, 2);
    def_builtin(e, "betainc", bi_betainc, 3, 3);
    def_builtin(e, "norminv", bi_norminv, 1, 1);
    def_builtin(e, "digamma", bi_digamma, 1, 1);
    def_builtin(e, "besselj", bi_besselj, 2, 2);
    def_builtin(e, "bessely", bi_bessely, 2, 2);
    def_builtin(e, "atan2",   bi_atan2,   2, 2);
    def_builtin(e, "hypot",   bi_hypot,   2, 2);
    def_builtin(e, "mod",     bi_mod,     2, 2);
    def_builtin(e, "rem",     bi_rem,     2, 2);
    def_builtin(e, "min",     bi_min,     1, 3);
    def_builtin(e, "max",     bi_max,     1, 3);
    def_builtin(e, "tic",     bi_tic,     0, 0);
    def_builtin(e, "toc",     bi_toc,     0, 0);
    def_builtin(e, "unique",  bi_unique,  1, 1);
    def_builtin(e, "cov",     bi_cov,     1, 3);
    def_builtin(e, "corr",    bi_corr,    1, 2);
    def_builtin(e, "var",     bi_var,     1, 3);
    def_builtin(e, "std",     bi_std,     1, 3);
    def_builtin(e, "median",  bi_median,  1, 2);
    def_builtin(e, "quantile",bi_quantile,2, 2);
    def_builtin(e, "mean",    bi_mean,    1, 2);
    def_builtin(e, "prod",    bi_prod,    1, 2);
    def_builtin(e, "length",  bi_length,  1, 1);
    def_builtin(e, "numel",   bi_numel,   1, 1);
    def_builtin(e, "find",    bi_find,    1, 1);
    def_builtin(e, "where",   bi_where,   1, 3);
    def_builtin(e, "sort",    bi_sort,    1, 1);
    def_builtin(e, "cumsum",  bi_cumsum,  1, 1);
    def_builtin(e, "cumprod", bi_cumprod, 1, 1);
    def_builtin(e, "diff",    bi_diff,    1, 1);
    def_builtin(e, "repmat",  bi_repmat,  2, 3);
    def_builtin(e, "flipud",  bi_flipud,  1, 1);
    def_builtin(e, "fliplr",  bi_fliplr,  1, 1);
    def_builtin(e, "rng",     bi_rng,     1, 1);
    def_builtin(e, "rand",    bi_rand,    0, 2);
    def_builtin(e, "randn",   bi_randn,   0, 2);
    def_builtin(e, "randi",   bi_randi,   1, 3);
    def_builtin(e, "isnan",    bi_isnan,    1, 1);
    def_builtin(e, "isinf",    bi_isinf,    1, 1);
    def_builtin(e, "isfinite", bi_isfinite, 1, 1);
    def_builtin(e, "who",   bi_who,   0, 0);
    def_builtin(e, "help",  bi_help,  0, 1);
    def_builtin(e, "system",bi_system,1, 1);
    def_builtin(e, "dis",   bi_dis,   1, 1);
    def_builtin(e, "plot",  bi_plot,  1, 3);
    def_builtin(e, "hist",  bi_hist,  1, 3);
    def_builtin(e, "format",bi_format,0, 1);
    return e;
}
