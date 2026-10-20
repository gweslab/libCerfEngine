#pragma once

#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>

namespace cerfengine {

struct ArmVfpTwoRegisterTransfer {
    bool     undefined = false;
    bool     to_arm    = false;
    uint32_t rt        = 0;
    uint32_t rt2       = 0;
    int32_t  lo_disp   = 0;
    int32_t  hi_disp   = 0;
};

ArmVfpTwoRegisterTransfer ArmVfpTwoRegisterTransferOf(const DecodedInsn& d);

}
