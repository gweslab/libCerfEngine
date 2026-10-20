#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_shift.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

/* SDM Vol. 2A 3-32 ADD, Vol. 2B 4-682 SUB: OF indicates "an overflow in the
   signed ... result". ARM DDI 0406C.c A2.2.1 (p. A2-44) SignedSatQ: above
   2^(N-1)-1 gives 2^(N-1)-1, below -(2^(N-1)) gives -(2^(N-1)). */
void X86LowerSaturate(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    mask = inst.args[2];
    if (inst.type != Type::kI32 || !IsConstRef(mask)) {
        throw EmitFault("CerfIR x86 lowering: saturation operands");
    }
    const int32_t sticky = static_cast<int32_t>(inst.aux);
    ra.BeforeFlagsClobber();
    ra.BeforeSlotWrite(sticky, 4u);

    const uint8_t        dst = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    X86RegAlloc::Operand o   = ra.Rm(inst.args[1]);
    X86EmitAluOperand(c, inst.op == Op::kSatAdd ? Op::kAdd : Op::kSub, dst, o);
    ra.Consume(inst.args[1]);
    uint8_t* in_range = x86::EmitJnoLabel(c);
    x86::EmitSarReg32Imm(c, dst, 31u);
    x86::EmitXorRegImm32(c, dst, 0x80000000u);
    x86::EmitOrBaseDisp32Imm32(c, x86::kStateReg, sticky,
                               static_cast<uint32_t>(b.ConstValue(mask)));
    x86::FixupLabel(in_range, c);
    ra.DefineInReg(ref, dst, true);
}

/* ARM DDI 0406C.c A2.2.1 (p. A2-44) SignedSatQ; SDM Vol. 2A 3-547 Jcc: JLE "(ZF=1
   or SF≠ OF)", JGE "(SF=OF)". */
void X86LowerSignedSat(X86Lowering& l, Ref ref) {
    const Block&   b      = l.GetBlock();
    const Inst&    inst   = b.At(ref);
    X86RegAlloc&   ra     = l.Regs();
    uint8_t*&      c      = l.Cursor();
    const uint32_t bits   = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    const uint32_t mask   = static_cast<uint32_t>(b.ConstValue(inst.args[2]));
    const int32_t  sticky = static_cast<int32_t>(inst.aux);
    if (bits == 0u || bits > 31u) throw EmitFault("CerfIR x86 lowering: saturation width");
    ra.BeforeFlagsClobber();
    ra.BeforeSlotWrite(sticky, 4u);

    const uint32_t bound = 1u << (bits - 1u);
    const uint32_t hi    = bound - 1u;
    const uint32_t lo    = 0u - bound;
    const uint8_t  dst   = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    x86::EmitCmpRegImm32(c, dst, hi);
    uint8_t* below_hi = x86::EmitJleLabel(c);
    x86::EmitMovRegImm32(c, dst, hi);
    x86::EmitOrBaseDisp32Imm32(c, x86::kStateReg, sticky, mask);
    x86::FixupLabel(below_hi, c);
    x86::EmitCmpRegImm32(c, dst, lo);
    uint8_t* above_lo = x86::EmitJgeLabel(c);
    x86::EmitMovRegImm32(c, dst, lo);
    x86::EmitOrBaseDisp32Imm32(c, x86::kStateReg, sticky, mask);
    x86::FixupLabel(above_lo, c);
    ra.DefineInReg(ref, dst, true);
}

void X86LowerOrSticky(X86Lowering& l, Ref ref) {
    const Block&   b      = l.GetBlock();
    const Inst&    inst   = b.At(ref);
    X86RegAlloc&   ra     = l.Regs();
    uint8_t*&      c      = l.Cursor();
    const Ref      cond   = inst.args[0];
    const int32_t  sticky = static_cast<int32_t>(inst.aux);
    const uint32_t bits   = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    ra.BeforeSlotWrite(sticky, 4u);
    if (IsConstRef(cond)) {
        ra.BeforeFlagsClobber();
        x86::EmitOrBaseDisp32Imm32(c, x86::kStateReg, sticky, bits);
        return;
    }
    uint8_t set_cc = kX86CondNz;
    ra.BeforeFlagsClobberExcept(cond);
    if (ra.At(cond).kind == X86RegAlloc::Kind::kFlags) {
        set_cc = ra.At(cond).cond;
    } else {
        const uint8_t reg = ra.Materialize(cond, kX86AllocRegs);
        if (ra.At(cond).wide) {
            x86::EmitTestRegReg(c, reg, reg);
        } else {
            x86::EmitCmpReg8Imm8(c, reg, 0u);
        }
    }
    ra.Consume(cond);
    uint8_t* skip = x86::EmitRel8Label(c, static_cast<uint8_t>(0x70u + X86InvertCond(set_cc)));
    x86::EmitOrBaseDisp32Imm32(c, x86::kStateReg, sticky, bits);
    x86::FixupLabel(skip, c);
}

}
