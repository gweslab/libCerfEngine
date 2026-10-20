#include <utility>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kWantedNz = 3u;

}

/* SDM Vol. 2A 3-502 IMUL: "The SF, ZF, AF, and PF flags are undefined." */
void X86LowerMul(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    if (inst.type != Type::kI32) {
        throw EmitFault("CerfIR x86 lowering: multiply of a type other than i32");
    }
    ra.BeforeFlagsClobber();

    Ref a   = inst.args[0];
    Ref src = inst.args[1];
    if (X86PreferSwap(ra, a, src)) std::swap(a, src);
    const uint8_t dst = ra.TakeOrCopy(a, ref, kX86AllocRegs);
    if (IsConstRef(src)) {
        x86::EmitImulReg32Reg32Imm32(c, dst, dst, static_cast<uint32_t>(b.ConstValue(src)));
    } else {
        x86::EmitImulReg32Reg32(c, dst, ra.MaterializeWide(src, kX86AllocRegs));
        ra.Consume(src);
    }
    ra.DefineInReg(ref, dst, true);
    if ((l.WantedFlags(ref) & kWantedNz) != 0u) {
        x86::EmitTestRegReg(c, dst, dst);
        X86RecordNz(l, ref);
    }
}

/* SDM Vol. 2B 4-150 MUL Table 4-9: EAX * r/m32 lands in EDX:EAX; IMUL r/m32
   (Vol. 2A 3-500) is the signed one-operand form. */
void X86LowerMulWide(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    ra.BeforeFlagsClobber();

    Ref a   = inst.args[0];
    Ref src = inst.args[1];
    if (!IsConstRef(src) && ra.At(src).kind == X86RegAlloc::Kind::kReg &&
        ra.At(src).reg == x86::kEax && ra.Remaining(src) == 1u) {
        std::swap(a, src);
    }
    ra.CopyIntoFixed(a, x86::kEax);
    ra.ClaimFixed(x86::kEdx);
    uint8_t operand = 0u;
    if (IsConstRef(src)) {
        operand = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegImm32(c, operand, static_cast<uint32_t>(b.ConstValue(src)));
    } else {
        operand = ra.MaterializeWide(src, kX86AllocRegs);
    }
    if (inst.op == Op::kMulS64) {
        x86::EmitImulReg32(c, operand);
    } else {
        x86::EmitMulReg32(c, operand);
    }
    if (IsConstRef(src)) {
        ra.ReleaseTemp(operand);
    } else {
        ra.Consume(src);
    }
    ra.DefineInReg(ref, x86::kEax, true);
    ra.DefineInReg(l.HiHalf(ref), x86::kEdx, true);
    X86RecordWideNz(l, ref, x86::kEax, x86::kEdx);
}

void X86LowerDivRem(X86Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();
    ra.CopyIntoFixed(inst.args[0], x86::kEax);
    ra.ClaimFixed(x86::kEdx);
    ra.CopyIntoFixed(inst.args[1], x86::kEcx);

    x86::EmitTestRegReg(c, x86::kEcx, x86::kEcx);
    uint8_t* nonzero = x86::EmitJnzLabel(c);
    x86::EmitMovRegImm32(c, x86::kEcx, 1u);
    if (inst.op == Op::kDivRemS32) {
        uint8_t* divide = x86::EmitJmpLabel(c);
        x86::FixupLabel(nonzero, c);
        x86::EmitCmpRegImm32(c, x86::kEax, 0x80000000u);
        uint8_t* not_min = x86::EmitJnzLabel(c);
        x86::EmitCmpRegImm32(c, x86::kEcx, 0xFFFFFFFFu);
        uint8_t* not_minus_one = x86::EmitJnzLabel(c);
        x86::EmitMovRegImm32(c, x86::kEcx, 1u);
        x86::FixupLabel(divide, c);
        x86::FixupLabel(not_min, c);
        x86::FixupLabel(not_minus_one, c);
        x86::EmitCdq(c);
        x86::EmitIdivReg32(c, x86::kEcx);
    } else {
        x86::FixupLabel(nonzero, c);
        x86::EmitXorRegReg(c, x86::kEdx, x86::kEdx);
        x86::EmitDivReg32(c, x86::kEcx);
    }
    ra.ReleaseTemp(x86::kEcx);
    ra.DefineInReg(ref, x86::kEax, true);
    ra.DefineInReg(l.HiHalf(ref), x86::kEdx, true);
}

void X86LowerHalf(X86Lowering& l, Ref ref) {
    const Inst&       inst = l.GetBlock().At(ref);
    const X86WideHalf h    = l.Half(inst.args[0], inst.op == Op::kHi32);
    l.Regs().DefineInReg(ref, X86TakeHalf(l, h, ref), true);
}

}
