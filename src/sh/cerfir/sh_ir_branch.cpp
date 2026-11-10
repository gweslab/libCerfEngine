#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::Op;

void ShIrLinkProcedure(ShIrContext& c) {
    if (c.d->link != 0u) c.b->SetSlotConst(ShPrDisp(), ShPcValue(c.d->guest_address));
}

void ShIrTranslateJump(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    switch (d.jump_target) {
    case ShJumpTarget::kRegister:
        c.branch_target = b.Snapshot(b.ReadSlot(ShGprDisp(d.m)));
        break;
    case ShJumpTarget::kPcPlusRegister:
        c.branch_target = b.Op2(Op::kAdd, b.ReadSlot(ShGprDisp(d.m)), b.Const32(ShPcValue(d.guest_address)));
        break;
    case ShJumpTarget::kProcedureRegister:
        c.branch_target = b.Snapshot(b.ReadSlot(ShPrDisp()));
        break;
    }
    ShIrLinkProcedure(c);
}

void ShIrTranslateBranchAlways(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    c.branch_target        = b.Const32(d.disp);
    c.branch_target_static = true;
    c.static_target        = d.disp;
    ShIrLinkProcedure(c);
}

void ShIrTranslateConditionalBranch(ShIrContext& c) {
    c.t_clear          = ShIrTClear(*c.b);
    c.taken_if_t_clear = c.d->branch_if_t == 0u;
}

}
