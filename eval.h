/* eval.h — interpreter state and runtime entry points for Neutrino.
 *
 * The evaluator is the bytecode VM (vm.c); this header owns the shared Interp
 * state, builtin-environment construction, and the runtime helpers the VM and
 * the builtins call (apply_binop, transpose, do_index, build_matrix, ... — see
 * nrt.h). The original tree-walker was removed at the stage-4 cutover. */
#ifndef NEUTRINO_EVAL_H
#define NEUTRINO_EVAL_H

#include <setjmp.h>
#include "value.h"
#include "ast.h"

typedef struct Interp {
    jmp_buf  jmp;
    char     err[256];
    uint32_t cur_line, cur_col;   /* updated as evaluation proceeds, for diagnostics */
    bool     had_error;
    EnvObj  *globals;             /* current global frame (for who/help introspection) */
    uint64_t rng_s[4];            /* xoshiro256** state (never all-zero) */
} Interp;

void    interp_init(Interp *I);
EnvObj *globals_new(void);                 /* env preloaded with builtins (+1 ref) */

Value eval_map_builtin(void);

#endif
