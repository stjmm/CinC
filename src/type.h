#ifndef CINC_TYPE_H
#define CINC_TYPE_H

#include <stdbool.h>

enum type_kind {
    TY_VOID,
    TY_INT,
    TY_FUNCTION
};

struct type {
    enum type_kind kind;

    int size;
    int align;

    union {
        struct {
            struct type *return_type;
            struct decl *params;
            int param_count;

            /*
             * C11 keeps a distinction between:
             *   int f();       no prototype
             *   int f(void);   has prototype
             *   inf f(int)     has prototype
             */
            bool has_prototype;
        } func;
    };
};

struct type *type_void(void);
struct type *type_int(void);
struct type *type_function(struct type *return_type, struct decl *params, int param_count, bool has_prototype);

bool type_is_void(struct type *ty);
bool type_is_int(struct type *ty);
bool type_is_function(struct type *ty);
bool type_is_object(struct type *ty);
bool types_compatible(struct type *a, struct type *b);

#endif
