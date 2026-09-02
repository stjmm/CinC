#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include "ast.h"
#include "diagnostics.h"

bool sema_analysis(
    ast_program *program,
    diagnostics *diag);

#endif
