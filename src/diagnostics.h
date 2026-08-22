#ifndef CINC_DIAGNOSTICS_H
#define CINC_DIAGNOSTICS_H

#include "lexer.h"

#include <stdio.h>

typedef struct {
    FILE *out;
    size_t error_count;
} diagnostics;

void diagnostics_init(diagnostics *diag, FILE *out);
void diagnostics_error(
    diagnostics *diag,
    const token *tok,
    const char *format,
    ...);
bool diagnostics_had_error(diagnostics *diag);

#endif
