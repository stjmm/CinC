#ifndef CINC_TYPE_H
#define CINC_TYPE_H

#include <stdbool.h>

enum type_kind {
    TY_VOID,
    TY_INT,
    TY_POINTER,
    TY_FUNCTION
};

struct type {
    enum type_kind kind;
    struct type *base; // TY_POINTER: pointee, TY_FUNCTION: return type.
                       
    /*
     * Function parameters types.
     *
     * C11 note:
     *      int f(void) declares a function with zero parameters
     *      int f() declares a function with no prototype
     *
     * C11 6.7.6.3.
     */
    struct type **params;
    int param_count;
    bool is_variadic;
};

struct type *type_void(void);
struct type *type_int(void);
struct type *type_pointer(struct type *base);

struct type *type_function(struct type *return_type, struct type **params,
                            int param_count, bool is_variadic);

bool type_equal(struct type *a, struct type *b);

#endif
