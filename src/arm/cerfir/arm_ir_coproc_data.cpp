#include <cerfengine/arm/arm_system_host.h>
#include "../arm_coproc_query.h"

#include "../arm_coproc_rules.h"
#include "../arm_unimplemented_halt.h"
#include "../plan/plan_fns.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

void DecidedOperation(ArmIrContext& c, ArmCoprocKind kind, const void* halt_helper,
                      uint32_t halt_info, const char* name) {
    const DecodedInsn& d = *c.d;
    const ArmCoprocDecision dec = QueryCoprocDecision(&d, c.block, kind);
    if (ArmIrCoprocessorPermissionCheck(c, false)) return;
    ArmIrCoprocGate(c, dec.gate_mask);
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
        ArmIrUndefined(c);
        return;
    case ArmCoprocAction::kUnimplemented:
        ArmIrUnimplementedHalt(c, halt_helper, halt_info);
        return;
    default:
        ArmCoprocDataShapePanic(c.block, &d, name);
    }
}

bool IsVfp(const DecodedInsn& d) {
    return d.cp_num == 10u || d.cp_num == 11u;
}

}

void ArmIrCoprocDataTransfer(ArmIrContext& c) {
    if (IsVfp(*c.d)) {
        if (ArmIrCoprocessorPermissionCheck(c, false)) return;
        ArmIrCallPlan(c, ArmPlanVfpDataTransfer(*c.d, *c.block));
        return;
    }
    DecidedOperation(c, ArmCoprocKind::kDataTransfer,
                     reinterpret_cast<const void*>(&ArmUnimplementedHalt::CoprocDataTransferHelper),
                     CoprocDataTransferHaltInfo(c.d), "LDC/STC");
}

void ArmIrCoprocDataOperation(ArmIrContext& c) {
    if (IsVfp(*c.d)) {
        if (ArmIrCoprocessorPermissionCheck(c, false)) return;
        ArmIrCallPlan(c, ArmPlanVfpDataOperation(*c.d, *c.block));
        return;
    }
    DecidedOperation(c, ArmCoprocKind::kDataOperation,
                     reinterpret_cast<const void*>(&ArmUnimplementedHalt::CoprocDataOperationHelper),
                     CoprocDataOperationHaltInfo(c.d), "CDP");
}

}
