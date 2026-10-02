#ifndef CINC_IR_H
#define CINC_IR_H

#include "sema.h"

typedef struct {

} ir_program_t;

ir_program_t *ir_build(const sema_result_t *result);

#endif
