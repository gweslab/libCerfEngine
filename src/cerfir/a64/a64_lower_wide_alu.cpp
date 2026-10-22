#include <utility>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kWantedN  = 1u;
constexpr uint8_t kWantedZ  = 2u;
constexpr uint8_t kWantedNz = kWantedN | kWantedZ;

bool IsVirtualWide(const Block& b, Ref v) {
    return IsConstRef(v) || b.At(v).op == Op::kPair;
}

}

uint8_t A64HalfReg(A64Lowering& l, const A64WideHalf& h, bool allow_zr) {
    A64RegAlloc& ra = l.Regs();
    if (!h.imm) return ra.Materialize(h.ref);
    if (h.value == 0u && allow_zr) return a64::kZr;
    const uint8_t reg = ra.AllocTemp();
    a64::EmitMovImm32(l.Cursor(), reg, h.value);
    return reg;
}

void A64ReleaseHalf(A64Lowering& l, const A64WideHalf& h, uint8_t reg) {
    if (h.imm) {
        l.Regs().ReleaseTemp(reg);
    } else {
        l.Regs().Consume(h.ref);
    }
}

/* ARM DDI 0487 M.d C1.2.5: a 32-bit write clears the upper 32 bits of the register. */
void A64RecordWideNz(A64Lowering& l, Ref ref, uint8_t lo, uint8_t hi) {
    const uint8_t want = l.WantedFlags(ref);
    if (want == 0u) return;
    if ((want & ~kWantedNz) != 0u) {
        throw EmitFault("CerfIR A64 lowering: carry or overflow of a 64-bit value");
    }
    uint8_t*& c = l.Cursor();
    a64::EmitMovReg(c, kA64Tmp0, lo);
    a64::EmitLogicReg(c, a64::kOrr, kA64Tmp0, kA64Tmp0, hi, a64::kLsl, 32u, true);
    a64::EmitTstReg(c, kA64Tmp0, kA64Tmp0, true);
    if ((want & kWantedN) != 0u) l.SetFlag(ref, kA64FlagIndexN, A64FlagKind::kNzcv, a64::kMi);
    if ((want & kWantedZ) != 0u) l.SetFlag(ref, kA64FlagIndexZ, A64FlagKind::kNzcv, a64::kEq);
}

void A64LowerWideAdd(A64Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (inst.op != Op::kAdd && inst.op != Op::kSub) {
        throw EmitFault("CerfIR A64 lowering: 64-bit op other than an add or a subtract");
    }
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();

    const bool add = inst.op == Op::kAdd;
    Ref        a   = inst.args[0];
    Ref        src = inst.args[1];
    if (add && IsVirtualWide(b, a) && !IsVirtualWide(b, src)) std::swap(a, src);
    const A64WideHalf a_lo   = l.Half(a, false);
    const A64WideHalf a_hi   = l.Half(a, true);
    const A64WideHalf src_lo = l.Half(src, false);
    const A64WideHalf src_hi = l.Half(src, true);
    const bool        no_low = src_lo.imm && src_lo.value == 0u;
    const bool        lo_imm = src_lo.imm && !no_low &&
                               (a64::AddSubImmFits(src_lo.value) || a64::AddSubImmFits(0u - src_lo.value));

    const uint8_t al = A64HalfReg(l, a_lo, false);
    const uint8_t ah = A64HalfReg(l, a_hi, true);
    const uint8_t bl = lo_imm || no_low ? a64::kZr : A64HalfReg(l, src_lo, true);
    const uint8_t bh = A64HalfReg(l, src_hi, true);
    A64ReleaseHalf(l, a_lo, al);
    A64ReleaseHalf(l, a_hi, ah);
    if (!lo_imm && !no_low) A64ReleaseHalf(l, src_lo, bl);
    A64ReleaseHalf(l, src_hi, bh);

    const uint32_t hi_inputs = (ah != a64::kZr ? A64RegBit(ah) : 0u) |
                               (bh != a64::kZr ? A64RegBit(bh) : 0u);
    const uint8_t  lo = ra.DestReg(al, bl, hi_inputs);
    const uint8_t  hi = ra.DestReg(ah, bh, A64RegBit(lo));
    if (no_low) {
        if (lo != al) a64::EmitMovReg(c, lo, al);
        A64EmitAlu(c, inst.op, hi, ah, ra.Reg(bh), false);
    } else {
        const A64RegAlloc::Operand lo_rhs = lo_imm ? ra.Imm(src_lo.value) : ra.Reg(bl);
        A64EmitAlu(c, inst.op, lo, al, lo_rhs, true);
        A64EmitAlu(c, add ? Op::kAdc : Op::kSbc, hi, ah, ra.Reg(bh), false);
    }
    ra.DefineInReg(ref, lo);
    ra.DefineInReg(l.HiHalf(ref), hi);
    A64RecordWideNz(l, ref, lo, hi);
}

void A64LowerWideNeg(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (l.WantedFlags(ref) != 0u) throw EmitFault("CerfIR A64 lowering: flags of a 64-bit negate");
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();
    const A64WideHalf h_lo = l.Half(inst.args[0], false);
    const A64WideHalf h_hi = l.Half(inst.args[0], true);
    const uint8_t     al   = A64HalfReg(l, h_lo, true);
    const uint8_t     ah   = A64HalfReg(l, h_hi, true);
    A64ReleaseHalf(l, h_lo, al);
    A64ReleaseHalf(l, h_hi, ah);
    const uint8_t lo = ra.DestReg(al, A64RegAlloc::kNoReg, ah != a64::kZr ? A64RegBit(ah) : 0u);
    const uint8_t hi = ra.DestReg(ah, A64RegAlloc::kNoReg, A64RegBit(lo));
    a64::EmitSubsReg(c, lo, a64::kZr, al);
    a64::EmitSbc(c, hi, a64::kZr, ah);
    ra.DefineInReg(ref, lo);
    ra.DefineInReg(l.HiHalf(ref), hi);
}

/* ARM DDI 0487 M.d C6.2.160 EXTR: result = (operand1::operand2)[lsb+datasize-1:lsb]. */
void A64LowerWideShift(A64Lowering& l, Ref ref) {
    const Block&   b      = l.GetBlock();
    const Inst&    inst   = b.At(ref);
    A64RegAlloc&   ra     = l.Regs();
    uint8_t*&      c      = l.Cursor();
    const Ref      amount = inst.args[1];
    const uint32_t n      = IsConstRef(amount) ? static_cast<uint32_t>(b.ConstValue(amount)) : 0u;
    if ((inst.op != Op::kAsr && inst.op != Op::kLsr) || n == 0u || n > 31u) {
        throw EmitFault("CerfIR A64 lowering: 64-bit shift other than a right shift by 1..31");
    }
    const A64WideHalf lo_half = l.Half(inst.args[0], false);
    const A64WideHalf hi_half = l.Half(inst.args[0], true);
    const Ref         hi_ref  = l.HiHalf(ref);
    const uint8_t     lo_src  = A64HalfReg(l, lo_half, true);
    const uint8_t     hi_src  = A64HalfReg(l, hi_half, true);
    A64ReleaseHalf(l, lo_half, lo_src);
    A64ReleaseHalf(l, hi_half, hi_src);
    const uint32_t hi_input = hi_src != a64::kZr ? A64RegBit(hi_src) : 0u;
    const uint8_t  lo       = ra.DestReg(lo_src, A64RegAlloc::kNoReg, hi_input);
    a64::EmitExtr(c, lo, hi_src, lo_src, n);
    ra.DefineInReg(ref, lo);
    if (ra.Remaining(hi_ref) == 0u) return;
    const uint8_t hi = ra.DestReg(hi_src, A64RegAlloc::kNoReg, A64RegBit(lo));
    if (inst.op == Op::kAsr) {
        a64::EmitAsrImm(c, hi, hi_src, n);
    } else {
        a64::EmitLsrImm(c, hi, hi_src, n);
    }
    ra.DefineInReg(hi_ref, hi);
}

}
