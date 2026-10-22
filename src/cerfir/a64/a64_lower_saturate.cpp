#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

void EmitOrSticky(A64Lowering& l, int32_t sticky, uint32_t bits) {
    uint8_t*& c = l.Cursor();
    l.Pins().CheckUnpinned(sticky, 4u);
    a64::EmitLdrW(c, kA64Tmp0, a64::kStateReg, sticky);
    if (a64::LogicImmFits(bits, false)) {
        a64::EmitLogicImm(c, a64::kOrr, kA64Tmp0, kA64Tmp0, bits);
    } else {
        a64::EmitMovImm32(c, kA64Tmp1, bits);
        a64::EmitLogicReg(c, a64::kOrr, kA64Tmp0, kA64Tmp0, kA64Tmp1);
    }
    a64::EmitStrW(c, kA64Tmp0, a64::kStateReg, sticky);
}

uint8_t CopyToDest(A64Lowering& l, Ref value) {
    A64RegAlloc&  ra  = l.Regs();
    const uint8_t src = ra.Materialize(value);
    ra.Consume(value);
    const uint8_t dst = ra.DestReg(src);
    if (dst != src) a64::EmitMovReg(l.Cursor(), dst, src);
    return dst;
}

}

/* ARM DDI 0406C.c A2.2.1 (p. A2-44) SignedSatQ: above 2^(N-1)-1 gives 2^(N-1)-1, below
   -(2^(N-1)) gives -(2^(N-1)). */
void A64LowerSaturate(A64Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    mask = inst.args[2];
    if (inst.type != Type::kI32 || !IsConstRef(mask)) {
        throw EmitFault("CerfIR A64 lowering: saturation operands");
    }
    const int32_t sticky = static_cast<int32_t>(inst.aux);
    ra.BeforeFlagsClobber();
    ra.BeforeSlotWrite(sticky, 4u);
    l.ClearFlagSource();

    const Op      op  = inst.op == Op::kSatAdd ? Op::kAdd : Op::kSub;
    const uint8_t lhs = ra.Materialize(inst.args[0]);
    const auto    rhs = A64AluOperand(ra, op, inst.args[1]);
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    const uint8_t dst = ra.DestReg(lhs, rhs.imm ? A64RegAlloc::kNoReg : rhs.reg);
    A64EmitAlu(c, op, dst, lhs, rhs, true);
    uint8_t* in_range = a64::EmitBCondLabel(c, a64::kVc);
    a64::EmitAsrImm(c, dst, dst, 31u);
    a64::EmitLogicImm(c, a64::kEor, dst, dst, 0x80000000u);
    EmitOrSticky(l, sticky, static_cast<uint32_t>(b.ConstValue(mask)));
    a64::FixupLabel(in_range, c);
    ra.DefineInReg(ref, dst);
}

void A64LowerSignedSat(A64Lowering& l, Ref ref) {
    const Block&   b      = l.GetBlock();
    const Inst&    inst   = b.At(ref);
    A64RegAlloc&   ra     = l.Regs();
    uint8_t*&      c      = l.Cursor();
    const uint32_t bits   = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    const uint32_t mask   = static_cast<uint32_t>(b.ConstValue(inst.args[2]));
    const int32_t  sticky = static_cast<int32_t>(inst.aux);
    if (bits == 0u || bits > 31u) throw EmitFault("CerfIR A64 lowering: saturation width");
    ra.BeforeFlagsClobber();
    ra.BeforeSlotWrite(sticky, 4u);
    l.ClearFlagSource();

    const uint32_t bound = 1u << (bits - 1u);
    const uint32_t hi    = bound - 1u;
    const uint32_t lo    = 0u - bound;
    const uint8_t  dst   = CopyToDest(l, inst.args[0]);
    a64::EmitMovImm32(c, kA64Tmp1, hi);
    a64::EmitCmpReg(c, dst, kA64Tmp1);
    uint8_t* below_hi = a64::EmitBCondLabel(c, a64::kLe);
    a64::EmitMovReg(c, dst, kA64Tmp1);
    EmitOrSticky(l, sticky, mask);
    a64::FixupLabel(below_hi, c);
    a64::EmitMovImm32(c, kA64Tmp1, lo);
    a64::EmitCmpReg(c, dst, kA64Tmp1);
    uint8_t* above_lo = a64::EmitBCondLabel(c, a64::kGe);
    a64::EmitMovReg(c, dst, kA64Tmp1);
    EmitOrSticky(l, sticky, mask);
    a64::FixupLabel(above_lo, c);
    ra.DefineInReg(ref, dst);
}

void A64LowerOrSticky(A64Lowering& l, Ref ref) {
    const Block&   b      = l.GetBlock();
    const Inst&    inst   = b.At(ref);
    A64RegAlloc&   ra     = l.Regs();
    uint8_t*&      c      = l.Cursor();
    const Ref      cond   = inst.args[0];
    const int32_t  sticky = static_cast<int32_t>(inst.aux);
    const uint32_t bits   = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    ra.BeforeSlotWrite(sticky, 4u);
    if (IsConstRef(cond)) {
        EmitOrSticky(l, sticky, bits);
        return;
    }
    uint8_t* skip = nullptr;
    if (ra.At(cond).kind == A64RegAlloc::Kind::kFlags) {
        skip = a64::EmitBCondLabel(c, a64::InvertCond(ra.At(cond).cond));
    } else {
        skip = a64::EmitCbzLabel(c, ra.Materialize(cond), false, false);
    }
    ra.Consume(cond);
    EmitOrSticky(l, sticky, bits);
    a64::FixupLabel(skip, c);
}

}
