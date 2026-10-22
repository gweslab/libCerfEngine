#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kWantedNz = 3u;

uint8_t MulOperand(A64RegAlloc& ra, Ref r) {
    if (IsConstRef(r) && ra.ConstOf(r) == 0u) return a64::kZr;
    return ra.Materialize(r);
}

}

void A64LowerMul(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (inst.type != Type::kI32) {
        throw EmitFault("CerfIR A64 lowering: multiply of a type other than i32");
    }
    const bool nz = (l.WantedFlags(ref) & kWantedNz) != 0u;
    if (nz) ra.BeforeFlagsClobber();
    const uint8_t a = MulOperand(ra, inst.args[0]);
    const uint8_t b = MulOperand(ra, inst.args[1]);
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    const uint8_t dst = ra.DestReg(a, b);
    a64::EmitMul(c, dst, a, b);
    ra.DefineInReg(ref, dst);
    if (nz) {
        a64::EmitTstReg(c, dst, dst);
        A64RecordNz(l, ref);
    }
}

/* ARM DDI 0487 M.d C6.2.369 SMADDL, C6.2.491 UMADDL: the 64-bit product of two 32-bit
   registers. */
void A64LowerMulWide(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if ((l.WantedFlags(ref) & kWantedNz) != 0u) ra.BeforeFlagsClobber();
    const uint8_t a = MulOperand(ra, inst.args[0]);
    const uint8_t b = MulOperand(ra, inst.args[1]);
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    const uint8_t lo = ra.DestReg(a, b);
    const uint8_t hi = ra.DestReg(A64RegAlloc::kNoReg, A64RegAlloc::kNoReg, A64RegBit(lo));
    if (inst.op == Op::kMulS64) {
        a64::EmitSmull(c, lo, a, b);
    } else {
        a64::EmitUmull(c, lo, a, b);
    }
    a64::EmitLsrImm(c, hi, lo, 32u, true);
    ra.DefineInReg(ref, lo);
    ra.DefineInReg(l.HiHalf(ref), hi);
    A64RecordWideNz(l, ref, lo, hi);
}

/* ARM DDI 0487 M.d C6.2.490 UDIV, C6.2.357 SDIV: "if divisor == 0 then result = 0"; SDIV
   writes result[datasize-1:0], so the most negative value divided by -1 is the most negative
   value. */
void A64LowerDivRem(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();
    const uint8_t n = ra.Materialize(inst.args[0]);
    const uint8_t d = ra.Materialize(inst.args[1]);
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    a64::EmitCmpImm(c, d, 0u);
    a64::EmitCsinc(c, kA64Tmp0, d, a64::kZr, a64::kNe);
    const uint8_t q = ra.DestReg(A64RegAlloc::kNoReg, A64RegAlloc::kNoReg, A64RegBit(n));
    const uint8_t r = ra.DestReg(n, d, A64RegBit(q));
    if (inst.op == Op::kDivRemS32) {
        a64::EmitSdiv(c, q, n, kA64Tmp0);
    } else {
        a64::EmitUdiv(c, q, n, kA64Tmp0);
    }
    a64::EmitMsub(c, r, q, kA64Tmp0, n);
    ra.DefineInReg(ref, q);
    ra.DefineInReg(l.HiHalf(ref), r);
}

void A64LowerHalf(A64Lowering& l, Ref ref) {
    const Inst&       inst = l.GetBlock().At(ref);
    A64RegAlloc&      ra   = l.Regs();
    const A64WideHalf h    = l.Half(inst.args[0], inst.op == Op::kHi32);
    if (h.imm) {
        const uint8_t reg = ra.AllocTemp();
        a64::EmitMovImm32(l.Cursor(), reg, h.value);
        ra.DefineInReg(ref, reg);
        return;
    }
    const uint8_t src = ra.Materialize(h.ref);
    ra.Consume(h.ref);
    const uint8_t dst = ra.DestReg(src);
    if (dst != src) a64::EmitMovReg(l.Cursor(), dst, src);
    ra.DefineInReg(ref, dst);
}

}
