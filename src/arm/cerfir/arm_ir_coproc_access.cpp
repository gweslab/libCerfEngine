#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>

#include "../arm_coproc_rules.h"
#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

void BindLabel(ArmIrContext& c, uint32_t label) {
    c.b->Label(label);
}

Ref UserMode(ArmIrContext& c) {
    cerfir::Builder& b    = *c.b;
    const Ref        mode = b.Op2(Op::kAnd,
                                  b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmCpsrDisp())),
                                  b.Const32(kArmCpsrModeMask));
    return b.Op2(Op::kCmpEq, mode, b.Const32(ArmMode::kUser));
}

/* ARM DDI 0406C.c A8.7.1 (p. A8-296) Coproc_Accepted(): CPACR<2*cp_num+1:2*cp_num>
   "when '00' UNDEFINED; when '01' if !CurrentModeIsNotUser() then UNDEFINED;"
   "when '10' UNPREDICTABLE; when '11' // CPACR permits access". */
void CpacrCheck(ArmIrContext& c) {
    cerfir::Builder& b     = *c.b;
    const uint32_t   pass  = b.NewLabel();
    const uint32_t   und   = b.NewLabel();
    const Ref        cpacr = b.HostLoad(&c.block->emit->Mmu()->State()->coprocessor_access, 4u,
                                        false);
    const Ref field = b.Op2(Op::kAnd, b.Op2(Op::kLsr, cpacr, b.Const32(2u * c.d->cp_num)),
                            b.Const32(3u));
    b.Dup(field);
    b.Branch(b.Op2(Op::kCmpEq, field, b.Const32(3u)), pass, true);
    b.Branch(b.Op2(Op::kCmpNe, field, b.Const32(1u)), und, true);
    b.Branch(UserMode(c), und, true);
    b.Branch(b.Const1(true), pass, true);
    BindLabel(c, und);
    ArmIrRaiseUndefined(c);
    BindLabel(c, pass);
}

}

void ArmIrUndefinedInUser(ArmIrContext& c) {
    const uint32_t not_user = c.b->NewLabel();
    c.b->Branch(UserMode(c), not_user, false);
    ArmIrRaiseUndefined(c);
    BindLabel(c, not_user);
}

bool ArmIrCoprocessorPermissionCheck(ArmIrContext& c, bool user_allowed) {
    switch (ArmCoprocAccessRuleFor(*c.block->emit->ProcessorConfig(), c.d->cp_num, user_allowed)) {
    case ArmCoprocAccessRule::kAccepted:
        return false;
    case ArmCoprocAccessRule::kUndefinedInUser:
        ArmIrUndefinedInUser(c);
        return false;
    case ArmCoprocAccessRule::kUndefined:
        ArmIrUndefined(c);
        return true;
    case ArmCoprocAccessRule::kCpacr:
        CpacrCheck(c);
        return false;
    }
    return false;
}

void ArmIrCoprocGate(ArmIrContext& c, uint32_t gate_mask) {
    if (gate_mask == 0u) return;
    cerfir::Builder& b       = *c.b;
    const uint32_t   enabled = b.NewLabel();
    b.Branch(b.HostTest(&c.block->emit->Mmu()->State()->coprocessor_access, gate_mask), enabled,
             true);
    ArmIrUndefined(c);
    BindLabel(c, enabled);
}

}
