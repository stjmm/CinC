#include "diagnostics.h"

#include <stdarg.h>

static void
print_diagnostic(
    diagnostics *diag,
    const token *tok,
    const char *format,
    va_list args)
{
    size_t column = tok->start - tok->line_start;

    fprintf(
        diag->out,
        "%s:%zu:%zu %s: ",
        tok->filename,
        tok->line,
        column + 1,
        "Error");

    vfprintf(diag->out, format, args);
    fputc('\n', diag->out);

    const char *line_end = tok->line_start;
    while (*line_end != '\0' && *line_end != '\n')
        line_end++;

    fprintf(
        diag->out,
        " %.*s\n",
        (int)(line_end - tok->line_start),
        tok->line_start);

    fprintf(diag->out, " %*s", (int)column, "");

    for (size_t i = 0; i < (tok->len > 0 ? tok->len : 1); i++)
        fputc('^', diag->out);

    fputc('\n', diag->out);
}

void
diagnostics_init(diagnostics *diag, FILE *out)
{
    *diag = (diagnostics){
        .error_count = 0,
        .out = out
    };
}

void
diagnostics_error(
    diagnostics *diag,
    const token *tok,
    const char *format,
    ...)
{
    diag->error_count++;

    va_list args;
    va_start(args, format);

    print_diagnostic(
        diag,
        tok,
        format,
        args);

    va_end(args);
}

bool
diagnostics_had_error(diagnostics *diag)
{
    return diag->error_count != 0;
}
