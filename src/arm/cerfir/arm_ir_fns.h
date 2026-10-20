#pragma once

#include <array>
#include <cstddef>

#include <cerfengine/arm/arm_insn_kind.h>

#include "../arm_call_plan.h"
#include "arm_ir_context.h"

namespace cerfengine {

using ArmIrTranslateFn = void (*)(ArmIrContext& c);

extern const std::array<ArmIrTranslateFn, kArmInsnKindCount> kArmIrTranslateTable;

inline ArmIrTranslateFn ArmIrTranslateFnFor(ArmInsnKind kind) {
    return kArmIrTranslateTable[static_cast<size_t>(kind)];
}

void ArmIrUndefined(ArmIrContext& c);
void ArmIrCallPlan(ArmIrContext& c, const ArmCallPlan& plan);

template <ArmCallPlanFn kPlan>
void ArmIrPlanned(ArmIrContext& c) {
    ArmIrCallPlan(c, kPlan(*c.d, *c.block));
}
void ArmIrRaiseUndefined(ArmIrContext& c);
void ArmIrRaiseDataAbort(ArmIrContext& c);
void ArmIrBxWritePc(ArmIrContext& c, cerfir::Ref address);
void ArmIrFinishPcWrite(ArmIrContext& c);
void ArmIrLoadWritePc(ArmIrContext& c, cerfir::Ref address);
void ArmIrAluWritePc(ArmIrContext& c, cerfir::Ref address);
void ArmIrExceptionReturn(ArmIrContext& c, cerfir::Ref address);
void ArmIrSpsrModeGuard(ArmIrContext& c, uint32_t system_label = kArmIrNoLabel);
void ArmIrSingleDataTransfer(ArmIrContext& c);
void ArmIrLoadStoreExtension(ArmIrContext& c);
void ArmIrBlockDataTransfer(ArmIrContext& c);
void ArmIrMultiply(ArmIrContext& c);
void ArmIrClz(ArmIrContext& c);
void ArmIrRev(ArmIrContext& c);
void ArmIrRev16(ArmIrContext& c);
void ArmIrRevsh(ArmIrContext& c);
void ArmIrRbit(ArmIrContext& c);
void ArmIrSxtb(ArmIrContext& c);
void ArmIrSxth(ArmIrContext& c);
void ArmIrUxtb(ArmIrContext& c);
void ArmIrUxth(ArmIrContext& c);
void ArmIrBfc(ArmIrContext& c);
void ArmIrBfi(ArmIrContext& c);
void ArmIrUbfx(ArmIrContext& c);
void ArmIrSbfx(ArmIrContext& c);
void ArmIrSaturatingArith(ArmIrContext& c);
void ArmIrHalfwordMultiply(ArmIrContext& c);
void ArmIrDualMultiply(ArmIrContext& c);
void ArmIrMostSignificantMultiply(ArmIrContext& c);
void ArmIrSsat(ArmIrContext& c);
void ArmIrRfe(ArmIrContext& c);
void ArmIrSrs(ArmIrContext& c);
void ArmIrWfi(ArmIrContext& c);
void ArmIrArmUnimplemented(ArmIrContext& c);
void ArmIrUnimplementedHalt(ArmIrContext& c, const void* helper, uint32_t info);
bool ArmIrCoprocessorPermissionCheck(ArmIrContext& c, bool user_allowed);
void ArmIrCoprocGate(ArmIrContext& c, uint32_t gate_mask);
void ArmIrUndefinedInUser(ArmIrContext& c);
void ArmIrSetNzcvFromWord(ArmIrContext& c, cerfir::Ref value);
void ArmIrVfpRegisterTransfer(ArmIrContext& c);
void ArmIrVfpTwoRegisterTransfer(ArmIrContext& c);
void ArmIrCoprocRegisterTransfer(ArmIrContext& c);
void ArmIrCoprocExtension(ArmIrContext& c);
void ArmIrCoprocDataTransfer(ArmIrContext& c);
void ArmIrCoprocDataOperation(ArmIrContext& c);
void ArmIrLdrex(ArmIrContext& c);
void ArmIrStrex(ArmIrContext& c);
cerfir::Ref ArmIrSignedHalf(ArmIrContext& c, cerfir::Ref value, bool top);
void ArmIrMrsOrMsr(ArmIrContext& c);
void ArmIrMsrImmediate(ArmIrContext& c);
void ArmIrNop(ArmIrContext& c);
void ArmIrDataProcessing(ArmIrContext& c);
void ArmIrDataProcessingReg(ArmIrContext& c);
void ArmIrDataProcessingShiftedReg(ArmIrContext& c);
void ArmIrBranch(ArmIrContext& c);
void ArmIrBx(ArmIrContext& c);
void ArmIrBlxReg(ArmIrContext& c);
void ArmIrArmBlxImm(ArmIrContext& c);
void ArmIrSvc(ArmIrContext& c);
void ArmIrThumbBlPrefix(ArmIrContext& c);
void ArmIrThumbBlSuffix(ArmIrContext& c);
void ArmIrThumbBlxImm(ArmIrContext& c);
void ArmIrCbz(ArmIrContext& c);
void ArmIrTableBranch(ArmIrContext& c);
void ArmIrClrex(ArmIrContext& c);
void ArmIrCps(ArmIrContext& c);
void ArmIrMovw(ArmIrContext& c);
void ArmIrMovt(ArmIrContext& c);

}
