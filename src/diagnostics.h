#ifndef CINC_DIAGNOSTICS_H
#define CINC_DIAGNOSTICS_H

#include "lexer.h"

#include <stdio.h>
#include <stdarg.h>

void diagnostics_init(FILE *out);
void diagnostics_error(
    const token_t *tok,
    const char *fmt,
    va_list args);
bool diagnostics_had_error(void);

#endif
