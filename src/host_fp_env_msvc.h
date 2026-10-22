#pragma once

#include <cstdint>

#include "host_fp_op.h"

namespace cerfengine {

class HostFpEnvMsvc {
public:
    static float  Run(HostFpOp op, float a, float b, bool toward_zero, uint32_t* status);
    static double Run(HostFpOp op, double a, double b, bool toward_zero, uint32_t* status);
};

}
