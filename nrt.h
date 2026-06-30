/* nrt.h — Neutrino runtime helpers shared between the tree-walker (eval.c)
 * and the bytecode VM (vm.c / compile.c).
 *
 * These are the Value-level operations: they take fully-evaluated operands and
 * return a fresh (+1) result. Crucially they are *non-consuming* — they never
 * release their inputs — so the VM can leave operands on the operand stack
 * until an op completes successfully. If one of these raises (runtime_error
 * longjmps), the operands are still on the stack and the VM's error handler
 * reclaims them in a single sweep. That is what erases the tree-walker's
 * error-path temp leak. */
#ifndef NEUTRINO_NRT_H
#define NEUTRINO_NRT_H

#include "lexer.h"     /* enum TokenKind */
#include "value.h"
#include "eval.h"      /* Interp */

[[noreturn]] void runtime_error(Interp *I, const char *fmt, ...);

/* numeric / array operations (non-consuming; result is +1) */
Value apply_binop(Interp *I, enum TokenKind op, Value a, Value b);
Value apply_unary(Interp *I, enum TokenKind op, Value v);
Value transpose (Interp *I, Value v, bool conj);
Value make_range(Interp *I, Value start, Value stop, Value step);

/* aggregate builders (non-consuming: inputs stay owned by the caller, so on the
 * VM they remain on the operand stack and an error here is swept). Result +1. */
Value build_matrix(Interp *I, Value *ev, uint32_t nrows, const int64_t *rowcounts);
Value do_index(Interp *I, Value target, Value *idx, uint32_t argc, uint8_t colonmask);
Value do_index_set(Interp *I, Value target, Value *idx, uint32_t argc, uint8_t colonmask, Value value);

/* calls: builtins run in-place; a VM closure (CloObj.chunk != null) runs its
 * compiled proto on a nested VM frame. call_value routes VM closures here. */
Value call_value(Interp *I, Value callee, Value *args, uint32_t n);
Value vm_run_closure(Interp *I, Value callee, Value *args, uint32_t n);

/* literal decoders (used by the compiler to fold literals into constants) */
int64_t parse_int_lit  (const char *s, uint32_t len);
double  parse_float_lit(const char *s, uint32_t len);
Value   decode_string  (const char *s, uint32_t len);

#endif
