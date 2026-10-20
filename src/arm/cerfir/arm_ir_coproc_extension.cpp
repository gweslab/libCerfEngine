#include <cerfengine/arm/arm_system_host.h>
#include "../arm_coproc_query.h"

#include "../arm_coproc_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrCoprocExtension(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    if (ArmIrCoprocessorPermissionCheck(c, false)) return;
    if (d.cp_num == 10u || d.cp_num == 11u) {
        ArmIrVfpTwoRegisterTransfer(c);
        return;
    }
    const ArmCoprocDecision dec =
        QueryCoprocDecision(&d, c.block, ArmCoprocKind::kRegisterTransferDouble);
    ArmIrCoprocGate(c, dec.gate_mask);
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
        ArmIrUndefined(c);
        return;
    case ArmCoprocAction::kRegister40: {
        if (dec.reg == nullptr) break;
        const uint8_t* const high = reinterpret_cast<const uint8_t*>(dec.reg) + 4;
        if (d.l != 0u) {
            ArmIrWriteGpr(c, d.crd, b.HostLoad(dec.reg, 4u, false));
            ArmIrWriteGpr(c, d.rn, b.HostLoad(high, 1u, true));
        } else {
            b.HostStore(dec.reg, ArmIrReadGpr(c, d.crd), 4u);
            b.HostStore(high, ArmIrReadGpr(c, d.rn), 1u);
        }
        return;
    }
    default:
        break;
    }
    ArmCoprocDoubleShapePanic(c.block, &d);
}

}
