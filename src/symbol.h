#ifndef CINC_SYMBOL_H
#define CINC_SYMBOL_H

#include <stdbool.h>

#include "type.h"
#include "lexer.h"

// C11 6.7.1
enum storage_class {
    SC_NONE,
    SC_EXTERN,
    SC_STATIC,
    SC_AUTO,
};

// C11 6.2.2 linkages of identifiers
enum linkage {
    LINK_NONE,
    LINK_INTERNAL,
    LINK_EXTERNAL
};

// C11 6.2.4 storage duration of objects
enum storage_duration {
    SD_NONE,
    SD_AUTO,
    SD_STATIC,
};

enum symbol_kind {
    SYM_OBJECT,
    SYM_FUNCTION,
    SYM_PARAMETER
};

struct symbol {
    struct token name;

    enum symbol_kind kind;
    struct type *type;

    enum storage_class storage_class;
    enum linkage linkage;
    enum storage_duration storage_duration;

    /*
     * int x;        // tentative definition at file scope
     * int x = 1;    // definition
     * extern int x; // declaration, not definition
     */
    bool is_defined;
    bool is_tentative;

    // Name used by IR (for locals unique)
    char *ir_name;

    struct ast_node *decl;
};

#endif
