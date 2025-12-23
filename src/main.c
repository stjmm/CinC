#include <stdio.h>

#include "lexer.h"

int main(void)
{
    const char *program_source = "int main() {\n    return 0;\n}\n";
    lexer_print_all(program_source);
    
    return 0;
}
