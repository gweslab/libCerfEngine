#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_shift.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

struct CarryIn {
    bool    slot;
    bool    wide;
    uint8_t reg;
    int32_t disp;
};

CarryIn PrepareCarryIn(X86Lowering& l, Ref cin, uint32_t avoid) {
    X86RegAlloc& ra = l.Regs();
    if (IsConstRef(cin)) {
        const uint8_t t = ra.AllocTemp(kX86AllocRegs & ~avoid);
        x86::EmitMovRegImm32(l.Cursor(), t,
                             static_cast<uint32_t>(l.GetBlock().ConstValue(cin) & 1u));
        return CarryIn{false, true, t, 0};
    }
    if (ra.At(cin).kind == X86RegAlloc::Kind::kSlot) {
        return CarryIn{true, false, 0u, ra.At(cin).disp};
    }
    const uint8_t reg = ra.Materialize(cin, kX86AllocRegs & ~avoid);
    return CarryIn{false, ra.At(cin).wide, reg, 0};
}

void EmitCarryToCf(uint8_t*& c, const CarryIn& in) {
    if (in.slot) {
        x86::EmitCmpByteBaseDisp32Imm8(c, x86::kStateReg, in.disp, 1u);
    } else if (in.wide) {
        x86::EmitCmpRegImm8(c, in.reg, 1u);
    } else {
        x86::EmitCmpReg8Imm8(c, in.reg, 1u);
    }
    x86::EmitCmc(c);
}

void FinishFlags(X86Lowering& l, Ref ref, uint8_t dst, uint8_t want, bool nz_valid) {
    uint8_t*&     c       = l.Cursor();
    const bool    want_c  = (want & 4u) != 0u;
    const bool    want_nz = (want & 3u) != 0u;
    if (want_nz && !nz_valid) {
        if (want_c) {
            const uint8_t t = l.Regs().AllocTemp(kX86ByteRegs);
            x86::EmitSetccReg8(c, x86::kSetC, t);
            l.SetFlag(ref, kFlagIndexC, X86FlagKind::kReg, t);
        }
        x86::EmitTestRegReg(c, dst, dst);
    } else if (want_c) {
        l.SetFlag(ref, kFlagIndexC, X86FlagKind::kEflags, kX86CondC);
    }
    if (want_nz) {
        l.SetFlag(ref, kFlagIndexN, X86FlagKind::kEflags, kX86CondS);
        l.SetFlag(ref, kFlagIndexZ, X86FlagKind::kEflags, kX86CondZ);
    }
}

bool LowerConstantShift(uint8_t*& c, Op op, uint8_t dst, uint32_t n) {
    switch (op) {
    case Op::kShlC:
        if (n < 32u) {
            x86::EmitShlReg32Imm(c, dst, static_cast<uint8_t>(n));
        } else if (n == 32u) {
            x86::EmitShlReg32Imm(c, dst, 31u);
            x86::EmitShlReg32Imm(c, dst, 1u);
        } else {
            x86::EmitXorRegReg(c, dst, dst);
        }
        return true;
    case Op::kLsrC:
        if (n < 32u) {
            x86::EmitShrReg32Imm(c, dst, static_cast<uint8_t>(n));
        } else if (n == 32u) {
            x86::EmitShrReg32Imm(c, dst, 31u);
            x86::EmitShrReg32Imm(c, dst, 1u);
        } else {
            x86::EmitXorRegReg(c, dst, dst);
        }
        return true;
    case Op::kAsrC:
        if (n < 32u) {
            x86::EmitSarReg32Imm(c, dst, static_cast<uint8_t>(n));
        } else {
            x86::EmitSarReg32Imm(c, dst, 31u);
            x86::EmitSarReg32Imm(c, dst, 1u);
        }
        return true;
    default:
        if ((n & 31u) != 0u) {
            x86::EmitRorReg32Imm(c, dst, static_cast<uint8_t>(n & 31u));
        } else {
            x86::EmitBtRegImm8(c, dst, 31u);
        }
        return false;
    }
}

void LowerCapturedRegisterShift(uint8_t*& c, Op op, uint8_t dst, const CarryIn& cin) {
    x86::EmitTestRegReg(c, x86::kEcx, x86::kEcx);
    uint8_t* zero_l = x86::EmitJzLabel32(c);
    uint8_t* done_a = nullptr;
    uint8_t* done_b = nullptr;
    switch (op) {
    case Op::kShlC:
    case Op::kLsrC: {
        x86::EmitCmpRegImm8(c, x86::kEcx, 32);
        uint8_t* small_l = x86::EmitJbLabel32(c);
        uint8_t* eq_l    = x86::EmitJzLabel32(c);
        x86::EmitXorRegReg(c, dst, dst);
        done_a = x86::EmitJmpLabel32(c);
        x86::FixupLabel32(eq_l, c);
        x86::EmitBtRegImm8(c, dst, op == Op::kShlC ? 0u : 31u);
        x86::EmitMovRegImm32(c, dst, 0u);
        done_b = x86::EmitJmpLabel32(c);
        x86::FixupLabel32(small_l, c);
        if (op == Op::kShlC) x86::EmitShlReg32Cl(c, dst);
        else                 x86::EmitShrReg32Cl(c, dst);
        break;
    }
    case Op::kAsrC: {
        x86::EmitCmpRegImm8(c, x86::kEcx, 32);
        uint8_t* small_l = x86::EmitJbLabel32(c);
        x86::EmitSarReg32Imm(c, dst, 31u);
        x86::EmitSarReg32Imm(c, dst, 1u);
        done_a = x86::EmitJmpLabel32(c);
        x86::FixupLabel32(small_l, c);
        x86::EmitSarReg32Cl(c, dst);
        break;
    }
    default: {
        x86::EmitTestRegImm32(c, x86::kEcx, 31u);
        uint8_t* ror_l = x86::EmitJnzLabel32(c);
        x86::EmitBtRegImm8(c, dst, 31u);
        done_a = x86::EmitJmpLabel32(c);
        x86::FixupLabel32(ror_l, c);
        x86::EmitRorReg32Cl(c, dst);
        break;
    }
    }
    uint8_t* done_c = x86::EmitJmpLabel32(c);
    x86::FixupLabel32(zero_l, c);
    EmitCarryToCf(c, cin);
    x86::FixupLabel32(done_a, c);
    if (done_b != nullptr) x86::FixupLabel32(done_b, c);
    x86::FixupLabel32(done_c, c);
}

void LowerUncapturedRegisterShift(uint8_t*& c, Op op, uint8_t dst) {
    switch (op) {
    case Op::kShlC:
    case Op::kLsrC: {
        x86::EmitCmpRegImm8(c, x86::kEcx, 32);
        uint8_t* small_l = x86::EmitJbLabel32(c);
        x86::EmitXorRegReg(c, dst, dst);
        uint8_t* done_l = x86::EmitJmpLabel32(c);
        x86::FixupLabel32(small_l, c);
        if (op == Op::kShlC) x86::EmitShlReg32Cl(c, dst);
        else                 x86::EmitShrReg32Cl(c, dst);
        x86::FixupLabel32(done_l, c);
        break;
    }
    case Op::kAsrC: {
        x86::EmitCmpRegImm8(c, x86::kEcx, 32);
        uint8_t* small_l = x86::EmitJbLabel32(c);
        x86::EmitMovRegImm32(c, x86::kEcx, 31u);
        x86::FixupLabel32(small_l, c);
        x86::EmitSarReg32Cl(c, dst);
        break;
    }
    default:
        x86::EmitRorReg32Cl(c, dst);
        break;
    }
}

}

void X86LowerShiftCarry(X86Lowering& l, Ref ref) {
    const Block&  b      = l.GetBlock();
    const Inst&   inst   = b.At(ref);
    X86RegAlloc&  ra     = l.Regs();
    uint8_t*&     c      = l.Cursor();
    const uint8_t want   = l.WantedFlags(ref);
    const Ref     amount = inst.args[1];
    ra.BeforeFlagsClobber();

    if (IsConstRef(amount)) {
        const uint32_t n = static_cast<uint32_t>(b.ConstValue(amount));
        if (n == 0u) throw EmitFault("CerfIR x86 lowering: shift by constant zero reached the backend");
        const uint8_t dst      = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
        const bool    nz_valid = LowerConstantShift(c, inst.op, dst, n);
        ra.Consume(inst.args[2]);
        FinishFlags(l, ref, dst, want, nz_valid);
        ra.DefineInReg(ref, dst, true);
        return;
    }

    const bool    want_c = (want & 4u) != 0u;
    const uint8_t count  = ra.CopyIntoFixed(amount, x86::kEcx);
    const uint8_t dst    = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs & ~RegBit(count));
    if (want_c) {
        const CarryIn cin = PrepareCarryIn(l, inst.args[2], RegBit(count) | RegBit(dst));
        LowerCapturedRegisterShift(c, inst.op, dst, cin);
        if (!cin.slot && cin.wide) ra.ReleaseTemp(cin.reg);
    } else {
        LowerUncapturedRegisterShift(c, inst.op, dst);
    }
    ra.Consume(inst.args[2]);
    ra.ReleaseTemp(count);
    FinishFlags(l, ref, dst, want, false);
    ra.DefineInReg(ref, dst, true);
}

void X86LowerRrx(X86Lowering& l, Ref ref) {
    const Inst&   inst = l.GetBlock().At(ref);
    X86RegAlloc&  ra   = l.Regs();
    uint8_t*&     c    = l.Cursor();
    const uint8_t want = l.WantedFlags(ref);
    ra.BeforeFlagsClobber();

    const uint8_t dst = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    const CarryIn cin = PrepareCarryIn(l, inst.args[1], RegBit(dst));
    EmitCarryToCf(c, cin);
    x86::EmitRcrReg32By1(c, dst);
    if (!cin.slot && cin.wide) ra.ReleaseTemp(cin.reg);
    ra.Consume(inst.args[1]);
    FinishFlags(l, ref, dst, want, false);
    ra.DefineInReg(ref, dst, true);
}

void X86LowerShiftModulo(X86Lowering& l, Ref ref) {
    const Block& b      = l.GetBlock();
    const Inst&  inst   = b.At(ref);
    X86RegAlloc& ra     = l.Regs();
    uint8_t*&    c      = l.Cursor();
    const Ref    amount = inst.args[1];
    if (inst.type == Type::kI64) {
        X86LowerWideShift(l, ref);
        return;
    }
    ra.BeforeFlagsClobber();

    uint8_t count = 0xFFu;
    if (!IsConstRef(amount)) count = ra.CopyIntoFixed(amount, x86::kEcx);
    const uint32_t avoid = count == 0xFFu ? 0u : RegBit(count);
    const uint8_t  dst   = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs & ~avoid);
    if (IsConstRef(amount)) {
        const uint8_t n = static_cast<uint8_t>(b.ConstValue(amount) & 31u);
        if (n != 0u) {
            switch (inst.op) {
            case Op::kShl: x86::EmitShlReg32Imm(c, dst, n); break;
            case Op::kLsr: x86::EmitShrReg32Imm(c, dst, n); break;
            case Op::kAsr: x86::EmitSarReg32Imm(c, dst, n); break;
            default:       x86::EmitRorReg32Imm(c, dst, n); break;
            }
        }
    } else {
        switch (inst.op) {
        case Op::kShl: x86::EmitShlReg32Cl(c, dst); break;
        case Op::kLsr: x86::EmitShrReg32Cl(c, dst); break;
        case Op::kAsr: x86::EmitSarReg32Cl(c, dst); break;
        default:       x86::EmitRorReg32Cl(c, dst); break;
        }
        ra.ReleaseTemp(count);
    }
    ra.DefineInReg(ref, dst, true);
}

}
