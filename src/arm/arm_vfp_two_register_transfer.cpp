#include "arm_vfp_two_register_transfer.h"

#include <cstddef>

#include <cerfengine/arm/cpu_state.h>

namespace cerfengine {

ArmVfpTwoRegisterTransfer ArmVfpTwoRegisterTransferOf(const DecodedInsn& d) {
    ArmVfpTwoRegisterTransfer t;
    const uint32_t opc1 = (static_cast<uint32_t>(d.offset) >> 4) & 0xFu;
    if ((opc1 & 0xDu) != 0x1u) {
        t.undefined = true;
        return t;
    }

    const uint32_t M      = (opc1 >> 1) & 1u;
    const uint32_t Vm     = static_cast<uint32_t>(d.offset) & 0xFu;
    const bool     single = d.cp_num == 10u;
    const uint32_t m      = single ? ((Vm << 1) | M) : ((M << 4) | Vm);
    if (single && m == 31u) {
        t.undefined = true;
        return t;
    }

    t.to_arm  = d.l != 0u;
    t.rt      = d.crd;
    t.rt2     = d.rn;
    t.lo_disp = static_cast<int32_t>(offsetof(ArmCpuState, vfp_d) + (single ? m * 4u : m * 8u));
    t.hi_disp = t.lo_disp + 4;
    return t;
}

}
