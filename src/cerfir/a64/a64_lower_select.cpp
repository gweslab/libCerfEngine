#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

uint8_t SelectOperand(A64RegAlloc& ra, Ref r) {
    if (IsConstRef(r) && ra.ConstOf(r) == 0u) return a64::kZr;
    return ra.Materialize(r);
}

}

void A64LowerSelect(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    cond = inst.args[0];
    if (inst.type != Type::kI32 || IsConstRef(cond)) {
        throw EmitFault("CerfIR A64 lowering: select operands");
    }

    uint8_t cc = a64::kNe;
    if (ra.At(cond).kind == A64RegAlloc::Kind::kFlags) {
        cc = ra.At(cond).cond;
    } else {
        ra.BeforeFlagsClobber();
        l.ClearFlagSource();
        const uint8_t reg = ra.Materialize(cond);
        a64::EmitTstReg(c, reg, reg);
    }
    const uint8_t t = SelectOperand(ra, inst.args[1]);
    const uint8_t f = SelectOperand(ra, inst.args[2]);
    ra.Consume(cond);
    ra.Consume(inst.args[1]);
    ra.Consume(inst.args[2]);
    const uint8_t dst = ra.DestReg(t, f);
    a64::EmitCsel(c, dst, t, f, cc);
    ra.DefineInReg(ref, dst);
}

}
