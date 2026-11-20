#include <cerfengine/arm/arm_system_host.h>
#include "../arm_coproc_query.h"

#include "../arm_coproc_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrCoprocExtension(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (ArmIrCoprocessorPermissionCheck(c, false)) return;
    if (d.cp_num == 10u || d.cp_num == 11u) {
        ArmIrVfpTwoRegisterTransfer(c);
        return;
    }
    if (d.cp_num <= 1u && ArmIrXscaleRegisterTransferDouble(c)) return;
    const ArmCoprocDecision dec =
        QueryCoprocDecision(&d, c.block, ArmCoprocKind::kRegisterTransferDouble);
    ArmIrCoprocGate(c, dec.gate_mask);
    if (dec.action != ArmCoprocAction::kUndefined) ArmCoprocDoubleShapePanic(c.block, &d);
    ArmIrUndefined(c);
}

}
