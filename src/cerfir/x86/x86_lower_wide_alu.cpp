#include <utility>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_shift.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using Operand = X86RegAlloc::Operand;

constexpr uint8_t kWantedN  = 1u;
constexpr uint8_t kWantedZ  = 2u;
constexpr uint8_t kWantedNz = kWantedN | kWantedZ;

bool IsVirtualWide(const Block& b, Ref v) {
    return IsConstRef(v) || b.At(v).op == Op::kPair;
}

}

uint8_t X86TakeHalf(X86Lowering& l, const X86WideHalf& h, Ref def) {
    if (!h.imm) return l.Regs().TakeOrCopy(h.ref, def, kX86AllocRegs);
    const uint8_t reg = l.Regs().AllocTemp(kX86AllocRegs);
    x86::EmitMovRegImm32(l.Cursor(), reg, h.value);
    return reg;
}

void X86RecordWideNz(X86Lowering& l, Ref ref, uint8_t lo, uint8_t hi) {
    const uint8_t want = l.WantedFlags(ref);
    if (want == 0u) return;
    if ((want & ~kWantedNz) != 0u) {
        throw EmitFault("CerfIR x86 lowering: carry or overflow of a 64-bit value");
    }
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    ra.UnlockAll();
    ra.Lock(lo);
    ra.Lock(hi);
    if ((want & kWantedZ) != 0u) {
        const bool    keep_in_reg = (want & kWantedN) != 0u;
        const uint8_t t = ra.AllocTemp(keep_in_reg ? kX86ByteRegs : kX86AllocRegs);
        x86::EmitMovRegReg(c, t, hi);
        x86::EmitOrReg32Reg32(c, t, lo);
        if (keep_in_reg) {
            x86::EmitSetccReg8(c, static_cast<uint8_t>(x86::kSetO + kX86CondZ), t);
            l.SetFlag(ref, kFlagIndexZ, X86FlagKind::kReg, t);
        } else {
            ra.ReleaseTemp(t);
            l.SetFlag(ref, kFlagIndexZ, X86FlagKind::kEflags, kX86CondZ);
        }
    }
    if ((want & kWantedN) != 0u) {
        x86::EmitTestRegReg(c, hi, hi);
        l.SetFlag(ref, kFlagIndexN, X86FlagKind::kEflags, kX86CondS);
    }
}

void X86LowerWideAdd(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (inst.op != Op::kAdd && inst.op != Op::kSub) {
        throw EmitFault("CerfIR x86 lowering: 64-bit op other than an add or a subtract");
    }
    ra.BeforeFlagsClobber();

    const bool add = inst.op == Op::kAdd;
    Ref        a   = inst.args[0];
    Ref        src = inst.args[1];
    if (add && IsVirtualWide(b, a) && !IsVirtualWide(b, src)) std::swap(a, src);
    const X86WideHalf a_lo   = l.Half(a, false);
    const X86WideHalf a_hi   = l.Half(a, true);
    const X86WideHalf src_lo = l.Half(src, false);
    const X86WideHalf src_hi = l.Half(src, true);
    const bool        no_low = src_lo.imm && src_lo.value == 0u;
    const uint8_t     lo     = X86TakeHalf(l, a_lo, ref);
    const uint8_t     hi     = X86TakeHalf(l, a_hi, ref);
    const Operand     o_lo   = X86HalfOperand(ra, src_lo);
    const Operand     o_hi   = X86HalfOperand(ra, src_hi);
    if (no_low) {
        X86EmitAluOperand(c, add ? Op::kAdd : Op::kSub, hi, o_hi);
    } else {
        X86EmitAluOperand(c, add ? Op::kAdd : Op::kSub, lo, o_lo);
        X86EmitAluOperand(c, add ? Op::kAdc : Op::kSbc, hi, o_hi);
    }
    if (!src_lo.imm) ra.Consume(src_lo.ref);
    if (!src_hi.imm) ra.Consume(src_hi.ref);
    ra.DefineInReg(ref, lo, true);
    ra.DefineInReg(l.HiHalf(ref), hi, true);
    X86RecordWideNz(l, ref, lo, hi);
}

/* SDM Vol. 2B 4-167 NEG: "The CF flag set to 0 if the source operand is 0;
   otherwise it is set to 1." */
void X86LowerWideNeg(X86Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (l.WantedFlags(ref) != 0u) throw EmitFault("CerfIR x86 lowering: flags of a 64-bit negate");
    ra.BeforeFlagsClobber();
    const uint8_t lo = X86TakeHalf(l, l.Half(inst.args[0], false), ref);
    const uint8_t hi = X86TakeHalf(l, l.Half(inst.args[0], true), ref);
    x86::EmitNegReg32(c, hi);
    x86::EmitNegReg32(c, lo);
    x86::EmitSbbRegImm32(c, hi, 0u);
    ra.DefineInReg(ref, lo, true);
    ra.DefineInReg(l.HiHalf(ref), hi, true);
}

/* SDM Vol. 2B 4-638 SHRD: shifts the destination "to the right ... The second
   operand (source operand) provides bits to shift in from the left". */
void X86LowerWideShift(X86Lowering& l, Ref ref) {
    const Block& b      = l.GetBlock();
    const Inst&  inst   = b.At(ref);
    X86RegAlloc& ra     = l.Regs();
    uint8_t*&    c      = l.Cursor();
    const Ref    amount = inst.args[1];
    const uint32_t n    = IsConstRef(amount) ? static_cast<uint32_t>(b.ConstValue(amount)) : 0u;
    if ((inst.op != Op::kAsr && inst.op != Op::kLsr) || n == 0u || n > 31u) {
        throw EmitFault("CerfIR x86 lowering: 64-bit shift other than a right shift by 1..31");
    }
    ra.BeforeFlagsClobber();

    const X86WideHalf lo_half = l.Half(inst.args[0], false);
    const X86WideHalf hi_half = l.Half(inst.args[0], true);
    const Ref         hi_ref  = l.HiHalf(ref);
    const uint8_t     lo      = X86TakeHalf(l, lo_half, ref);
    if (ra.Remaining(hi_ref) != 0u) {
        const uint8_t hi = X86TakeHalf(l, hi_half, ref);
        x86::EmitShrdReg32Reg32Imm8(c, lo, hi, static_cast<uint8_t>(n));
        if (inst.op == Op::kAsr) {
            x86::EmitSarReg32Imm(c, hi, static_cast<uint8_t>(n));
        } else {
            x86::EmitShrReg32Imm(c, hi, static_cast<uint8_t>(n));
        }
        ra.DefineInReg(ref, lo, true);
        ra.DefineInReg(hi_ref, hi, true);
        return;
    }
    if (hi_half.imm) {
        const uint8_t t = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegImm32(c, t, hi_half.value);
        x86::EmitShrdReg32Reg32Imm8(c, lo, t, static_cast<uint8_t>(n));
        ra.ReleaseTemp(t);
    } else {
        x86::EmitShrdReg32Reg32Imm8(c, lo, ra.MaterializeWide(hi_half.ref, kX86AllocRegs),
                                    static_cast<uint8_t>(n));
        ra.Consume(hi_half.ref);
    }
    ra.DefineInReg(ref, lo, true);
}

}
