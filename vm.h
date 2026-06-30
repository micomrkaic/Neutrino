/* vm.h — stack-based bytecode VM for Neutrino. */
#ifndef NEUTRINO_VM_H
#define NEUTRINO_VM_H

#include "value.h"
#include "ast.h"
#include "eval.h"

/* Compiles and runs each top-level statement on the operand-stack VM. Mirrors
 * eval_program: echoes non-silent results when echo is true, returns the last
 * value (+1). On any compile or runtime error it sets I->err / I->had_error and
 * returns null — and, unlike the tree-walker, every operand-stack temporary
 * live at the point of failure is released (a single sweep of the stack), so the
 * error path leaks nothing. */
Value vm_eval_program(Interp *I, AstNode *block, EnvObj *globals, bool echo);

/* Frees chunks retained across the session because they define closures whose
 * compiled protos must outlive the statement that created them. Call once at
 * shutdown, after global closures have been released. */
void vm_session_end(void);

#endif
