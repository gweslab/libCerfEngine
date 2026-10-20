#include <cstddef>

#include <cerfengine/arm/cpu_state.h>

#include "../arm_emit_services.h"
#include "../arm_vfp_register_form.h"
#include "../arm_vfp_system_transfer.h"
#include "../arm_vfp_two_register_transfer.h"
#include "../plan/plan_fns.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

Ref StateWord(ArmIrContext& c, int32_t disp) {
    return c.b->GetSlot(Type::kI32, static_cast<uint32_t>(disp));
}

void SetStateWord(ArmIrContext& c, int32_t disp, Ref value) {
    c.b->SetSlot(static_cast<uint32_t>(disp), value);
}

void SingleMove(ArmIrContext& c, uint32_t single) {
    const DecodedInsn& d       = *c.d;
    const int32_t      sn_disp = static_cast<int32_t>(offsetof(ArmCpuState, vfp_d) + single * 4u);
    if (d.l != 0u) {
        const Ref value = StateWord(c, sn_disp);
        if (d.rd == ArmGpr::kR15) {
            ArmIrSetNzcvFromWord(c, value);
            return;
        }
        SetStateWord(c, ArmGprDisp(d.rd), value);
        return;
    }
    SetStateWord(c, sn_disp, StateWord(c, ArmGprDisp(d.rd)));
}

void SystemRegister(ArmIrContext& c) {
    const DecodedInsn&         d = *c.d;
    cerfir::Builder&           b = *c.b;
    const ArmVfpSystemTransfer t = ArmVfpSystemTransferOf(d, *c.block->emit->ProcessorConfig());
    if (t.undefined_in_user) ArmIrUndefinedInUser(c);
    switch (t.action) {
    case ArmVfpSystemAction::kReadConstant:
        SetStateWord(c, ArmGprDisp(d.rd), b.Const32(t.constant));
        return;
    case ArmVfpSystemAction::kReadState:
        SetStateWord(c, ArmGprDisp(d.rd), StateWord(c, t.state_disp));
        return;
    case ArmVfpSystemAction::kReadStateToFlags:
        ArmIrSetNzcvFromWord(c, StateWord(c, t.state_disp));
        return;
    case ArmVfpSystemAction::kWriteState: {
        Ref value = StateWord(c, ArmGprDisp(d.rd));
        if (t.write_mask != 0xFFFFFFFFu) value = b.Op2(Op::kAnd, value, b.Const32(t.write_mask));
        SetStateWord(c, t.state_disp, value);
        return;
    }
    case ArmVfpSystemAction::kIgnore:
        return;
    case ArmVfpSystemAction::kUndefined:
        break;
    }
    ArmIrUndefined(c);
}

}

void ArmIrVfpRegisterTransfer(ArmIrContext& c) {
    if (ArmIrCoprocessorPermissionCheck(c, false)) return;
    const DecodedInsn&           d = *c.d;
    const ArmVfpRegisterTransfer t = ArmVfpRegisterTransferOf(d, *c.block->emit->ProcessorConfig());
    switch (t.form) {
    case ArmVfpRegisterForm::kNeonVdupCore:
        ArmIrCallPlan(c, ArmPlanNeonVdupCore(d, *c.block));
        return;
    case ArmVfpRegisterForm::kNeonCoreToScalar:
        ArmIrCallPlan(c, ArmPlanNeonCoreToScalar(d, *c.block));
        return;
    case ArmVfpRegisterForm::kNeonScalarToCore:
        ArmIrCallPlan(c, ArmPlanNeonScalarToCore(d, *c.block));
        return;
    case ArmVfpRegisterForm::kSystemRegister:
        SystemRegister(c);
        return;
    case ArmVfpRegisterForm::kSingleMove:
        SingleMove(c, t.single);
        return;
    case ArmVfpRegisterForm::kUndefined:
        break;
    }
    ArmIrUndefined(c);
}

void ArmIrVfpTwoRegisterTransfer(ArmIrContext& c) {
    const ArmVfpTwoRegisterTransfer t = ArmVfpTwoRegisterTransferOf(*c.d);
    if (t.undefined) {
        ArmIrUndefined(c);
        return;
    }
    if (t.to_arm) {
        SetStateWord(c, ArmGprDisp(t.rt), StateWord(c, t.lo_disp));
        SetStateWord(c, ArmGprDisp(t.rt2), StateWord(c, t.hi_disp));
        return;
    }
    SetStateWord(c, t.lo_disp, StateWord(c, ArmGprDisp(t.rt)));
    SetStateWord(c, t.hi_disp, StateWord(c, ArmGprDisp(t.rt2)));
}

}
