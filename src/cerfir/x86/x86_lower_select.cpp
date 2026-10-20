#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

void X86LowerSelect(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    cond = inst.args[0];
    if (inst.type != Type::kI32 || IsConstRef(cond)) {
        throw EmitFault("CerfIR x86 lowering: select operands");
    }

    uint8_t cc = kX86CondNz;
    if (ra.At(cond).kind == X86RegAlloc::Kind::kFlags) {
        cc = ra.At(cond).cond;
    } else {
        ra.BeforeFlagsClobber();
        const uint8_t reg = ra.Materialize(cond, kX86AllocRegs);
        if (ra.At(cond).wide) {
            x86::EmitTestRegReg(c, reg, reg);
        } else {
            x86::EmitCmpReg8Imm8(c, reg, 0u);
        }
    }
    ra.Consume(cond);

    const Ref     if_false = inst.args[2];
    const uint8_t dst      = ra.TakeOrCopy(inst.args[1], ref, kX86AllocRegs);
    uint8_t       src;
    if (IsConstRef(if_false)) {
        src = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegImm32(c, src, static_cast<uint32_t>(b.ConstValue(if_false)));
    } else {
        src = ra.MaterializeWide(if_false, kX86AllocRegs);
    }
    x86::EmitCmovccRegReg(c, X86InvertCond(cc), dst, src);
    if (IsConstRef(if_false)) {
        ra.ReleaseTemp(src);
    } else {
        ra.Consume(if_false);
    }
    ra.DefineInReg(ref, dst, true);
}

}
