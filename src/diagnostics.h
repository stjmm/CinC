#ifndef CINC_DIAGNOSTICS_H
#define CINC_DIAGNOSTICS_H

#include "lexer.h"

#include <stdio.h>

void diagnostics_init(FILE *out);
void diagnostics_error(
    const token_t *tok,
    const char *fmt,
    ...);
bool diagnostics_had_error(void);

#endif
