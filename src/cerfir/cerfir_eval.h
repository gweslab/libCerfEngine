#pragma once

#include <cstdint>

#include "cerfir_op.h"
#include "cerfir_types.h"

namespace cerfengine::cerfir {

struct EvalArgs {
    uint64_t value[3];
    Type     type[3];
};

bool EvalPure(Op op, Type result_type, const EvalArgs& args, uint64_t* out);

bool EvalFlag(Op flag, Op producer, Type producer_type, const EvalArgs& producer_args,
              uint64_t* out);

}
