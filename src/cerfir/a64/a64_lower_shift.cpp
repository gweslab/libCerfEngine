#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using a64::kZr;

constexpr uint8_t kWantC  = 4u;
constexpr uint8_t kWantNz = 3u;

uint8_t CarryInReg(A64Lowering& l, Ref cin) {
    A64RegAlloc& ra = l.Regs();
    if (!IsConstRef(cin)) return ra.Materialize(cin);
    if ((ra.ConstOf(cin) & 1u) == 0u) return kZr;
    a64::EmitMovImm32(l.Cursor(), a64::kIp0, 1u);
    return a64::kIp0;
}

void FinishFlags(A64Lowering& l, Ref ref, uint8_t dst, uint8_t carry, uint8_t want) {
    if ((want & kWantC) != 0u) l.SetFlag(ref, kA64FlagIndexC, A64FlagKind::kReg, carry);
    if ((want & kWantNz) != 0u) {
        a64::EmitTstReg(l.Cursor(), dst, dst);
        l.SetFlag(ref, kA64FlagIndexN, A64FlagKind::kNzcv, a64::kMi);
        l.SetFlag(ref, kA64FlagIndexZ, A64FlagKind::kNzcv, a64::kEq);
    }
}

uint32_t ConstantCarryBit(Op op, uint32_t n) {
    switch (op) {
    case Op::kShlC: return n < 32u ? 32u - n : 0u;
    case Op::kLsrC: return n <= 32u ? n - 1u : 0u;
    case Op::kAsrC: return n < 32u ? n - 1u : 31u;
    default:        return (n & 31u) != 0u ? (n & 31u) - 1u : 31u;
    }
}

void EmitConstantShift(uint8_t*& c, Op op, uint8_t dst, uint8_t src, uint32_t n) {
    switch (op) {
    case Op::kShlC:
        if (n < 32u) {
            a64::EmitLslImm(c, dst, src, n);
        } else {
            a64::EmitMovReg(c, dst, kZr);
        }
        return;
    case Op::kLsrC:
        if (n < 32u) {
            a64::EmitLsrImm(c, dst, src, n);
        } else {
            a64::EmitMovReg(c, dst, kZr);
        }
        return;
    case Op::kAsrC:
        a64::EmitAsrImm(c, dst, src, n < 32u ? n : 31u);
        return;
    default:
        if ((n & 31u) != 0u) {
            a64::EmitRorImm(c, dst, src, n & 31u);
        } else if (dst != src) {
            a64::EmitMovReg(c, dst, src);
        }
        return;
    }
}

void LowerConstantShiftCarry(A64Lowering& l, Ref ref, uint32_t n, uint8_t want) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const uint8_t src = ra.Materialize(inst.args[0]);
    uint8_t carry = A64RegAlloc::kNoReg;
    if ((want & kWantC) != 0u) {
        carry = ra.AllocTemp();
        const bool zero = (inst.op == Op::kShlC || inst.op == Op::kLsrC) && n > 32u;
        if (zero) {
            a64::EmitMovReg(c, carry, kZr);
        } else {
            a64::EmitUbfx(c, carry, src, ConstantCarryBit(inst.op, n), 1u);
        }
    }
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[2]);
    const uint8_t dst = ra.DestReg(src);
    EmitConstantShift(c, inst.op, dst, src, n);
    FinishFlags(l, ref, dst, carry, want);
    ra.DefineInReg(ref, dst);
}

/* ARM DDI 0487 M.d C6.2.271 LSLV, C6.2.274 LSRV, C6.2.21 ASRV, C6.2.349 RORV: the amount is
   "UInt(operand2) MOD datasize". */
void EmitCapturedShift(uint8_t*& c, Op op, uint8_t src, uint8_t amount) {
    switch (op) {
    case Op::kShlC:
        a64::EmitMovReg(c, kA64Tmp0, src);
        a64::EmitShiftReg(c, a64::kLsl, kA64Tmp0, kA64Tmp0, amount, true);
        a64::EmitCmpImm(c, amount, 63u);
        a64::EmitCsel(c, kA64Tmp0, kA64Tmp0, kZr, a64::kLs, true);
        a64::EmitUbfx(c, kA64Tmp2, kA64Tmp0, 32u, 1u, true);
        return;
    case Op::kLsrC:
        a64::EmitLslImm(c, kA64Tmp0, src, 32u, true);
        a64::EmitShiftReg(c, a64::kLsr, kA64Tmp0, kA64Tmp0, amount, true);
        a64::EmitCmpImm(c, amount, 63u);
        a64::EmitCsel(c, kA64Tmp0, kA64Tmp0, kZr, a64::kLs, true);
        a64::EmitUbfx(c, kA64Tmp2, kA64Tmp0, 31u, 1u);
        return;
    case Op::kAsrC:
        a64::EmitMovImm32(c, kA64Tmp1, 63u);
        a64::EmitCmpImm(c, amount, 63u);
        a64::EmitCsel(c, kA64Tmp1, amount, kA64Tmp1, a64::kLs);
        a64::EmitLslImm(c, kA64Tmp0, src, 32u, true);
        a64::EmitShiftReg(c, a64::kAsr, kA64Tmp0, kA64Tmp0, kA64Tmp1, true);
        a64::EmitUbfx(c, kA64Tmp2, kA64Tmp0, 31u, 1u);
        return;
    default:
        a64::EmitShiftReg(c, a64::kRor, kA64Tmp0, src, amount);
        a64::EmitUbfx(c, kA64Tmp2, kA64Tmp0, 31u, 1u);
        return;
    }
}

void EmitCapturedResult(uint8_t*& c, Op op, uint8_t dst) {
    if (op == Op::kShlC || op == Op::kRorC) {
        a64::EmitMovReg(c, dst, kA64Tmp0);
    } else {
        a64::EmitLsrImm(c, dst, kA64Tmp0, 32u, true);
    }
}

void EmitUncapturedShift(uint8_t*& c, Op op, uint8_t dst, uint8_t src, uint8_t amount) {
    switch (op) {
    case Op::kShlC:
    case Op::kLsrC:
        a64::EmitShiftReg(c, op == Op::kShlC ? a64::kLsl : a64::kLsr, kA64Tmp0, src, amount);
        a64::EmitCmpImm(c, amount, 32u);
        a64::EmitCsel(c, dst, kA64Tmp0, kZr, a64::kCc);
        return;
    case Op::kAsrC:
        a64::EmitMovImm32(c, kA64Tmp1, 31u);
        a64::EmitCmpImm(c, amount, 31u);
        a64::EmitCsel(c, kA64Tmp1, amount, kA64Tmp1, a64::kLs);
        a64::EmitShiftReg(c, a64::kAsr, dst, src, kA64Tmp1);
        return;
    default:
        a64::EmitShiftReg(c, a64::kRor, dst, src, amount);
        return;
    }
}

}

void A64LowerShiftCarry(A64Lowering& l, Ref ref) {
    const Block&  b      = l.GetBlock();
    const Inst&   inst   = b.At(ref);
    A64RegAlloc&  ra     = l.Regs();
    uint8_t*&     c      = l.Cursor();
    const uint8_t want   = l.WantedFlags(ref);
    const Ref     amount = inst.args[1];
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();

    if (IsConstRef(amount)) {
        const uint32_t n = static_cast<uint32_t>(b.ConstValue(amount));
        if (n == 0u) throw EmitFault("CerfIR A64 lowering: shift by constant zero reached the backend");
        LowerConstantShiftCarry(l, ref, n, want);
        return;
    }

    const uint8_t src = ra.Materialize(inst.args[0]);
    const uint8_t amt = ra.Materialize(amount);
    if ((want & kWantC) == 0u) {
        ra.Consume(inst.args[0]);
        ra.Consume(amount);
        ra.Consume(inst.args[2]);
        const uint8_t dst = ra.DestReg(src, amt);
        EmitUncapturedShift(c, inst.op, dst, src, amt);
        FinishFlags(l, ref, dst, A64RegAlloc::kNoReg, want);
        ra.DefineInReg(ref, dst);
        return;
    }
    const uint8_t cin = CarryInReg(l, inst.args[2]);
    EmitCapturedShift(c, inst.op, src, amt);
    const uint8_t carry = ra.AllocTemp();
    a64::EmitCmpImm(c, amt, 0u);
    a64::EmitCsel(c, carry, cin, kA64Tmp2, a64::kEq);
    ra.Consume(inst.args[0]);
    ra.Consume(amount);
    ra.Consume(inst.args[2]);
    const uint8_t dst = ra.DestReg(src, amt, A64RegBit(carry));
    EmitCapturedResult(c, inst.op, dst);
    FinishFlags(l, ref, dst, carry, want);
    ra.DefineInReg(ref, dst);
}

/* ARM DDI 0487 M.d C6.2.160 EXTR: result = (operand1::operand2)[lsb+datasize-1:lsb]. */
void A64LowerRrx(A64Lowering& l, Ref ref) {
    const Inst&   inst = l.GetBlock().At(ref);
    A64RegAlloc&  ra   = l.Regs();
    uint8_t*&     c    = l.Cursor();
    const uint8_t want = l.WantedFlags(ref);
    if ((want & kWantNz) != 0u) ra.BeforeFlagsClobber();
    l.ClearFlagSource();

    const uint8_t src   = ra.Materialize(inst.args[0]);
    const uint8_t cin   = CarryInReg(l, inst.args[1]);
    uint8_t       carry = A64RegAlloc::kNoReg;
    if ((want & kWantC) != 0u) {
        carry = ra.AllocTemp();
        a64::EmitUbfx(c, carry, src, 0u, 1u);
    }
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    const uint32_t keep = carry != A64RegAlloc::kNoReg ? A64RegBit(carry) : 0u;
    const uint8_t  dst  = ra.DestReg(src, A64RegAlloc::kNoReg, keep);
    a64::EmitExtr(c, dst, cin, src, 1u);
    FinishFlags(l, ref, dst, carry, want);
    ra.DefineInReg(ref, dst);
}

void A64LowerShiftModulo(A64Lowering& l, Ref ref) {
    const Block& b      = l.GetBlock();
    const Inst&  inst   = b.At(ref);
    A64RegAlloc& ra     = l.Regs();
    uint8_t*&    c      = l.Cursor();
    const Ref    amount = inst.args[1];
    if (inst.type == Type::kI64) {
        A64LowerWideShift(l, ref);
        return;
    }
    const uint8_t src = ra.Materialize(inst.args[0]);
    if (IsConstRef(amount)) {
        const uint32_t n = static_cast<uint32_t>(b.ConstValue(amount) & 31u);
        ra.Consume(inst.args[0]);
        const uint8_t dst = ra.DestReg(src);
        if (n == 0u) {
            if (dst != src) a64::EmitMovReg(c, dst, src);
        } else {
            switch (inst.op) {
            case Op::kShl: a64::EmitLslImm(c, dst, src, n); break;
            case Op::kLsr: a64::EmitLsrImm(c, dst, src, n); break;
            case Op::kAsr: a64::EmitAsrImm(c, dst, src, n); break;
            default:       a64::EmitRorImm(c, dst, src, n); break;
            }
        }
        ra.DefineInReg(ref, dst);
        return;
    }
    const uint8_t amt = ra.Materialize(amount);
    ra.Consume(inst.args[0]);
    ra.Consume(amount);
    const uint8_t dst = ra.DestReg(src, amt);
    switch (inst.op) {
    case Op::kShl: a64::EmitShiftReg(c, a64::kLsl, dst, src, amt); break;
    case Op::kLsr: a64::EmitShiftReg(c, a64::kLsr, dst, src, amt); break;
    case Op::kAsr: a64::EmitShiftReg(c, a64::kAsr, dst, src, amt); break;
    default:       a64::EmitShiftReg(c, a64::kRor, dst, src, amt); break;
    }
    ra.DefineInReg(ref, dst);
}

}
