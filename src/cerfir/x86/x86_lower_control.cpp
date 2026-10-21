#include <cstdio>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

uint8_t EmitConditionTest(X86Lowering& l, Ref cond) {
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (ra.At(cond).kind == X86RegAlloc::Kind::kFlags) {
        const uint8_t cc = ra.At(cond).cond;
        ra.Consume(cond);
        return cc;
    }
    ra.BeforeFlagsClobber();
    const uint8_t reg = ra.Materialize(cond, kX86AllocRegs);
    if (ra.At(cond).wide) {
        x86::EmitTestRegReg(c, reg, reg);
    } else {
        x86::EmitTestReg8Reg8(c, reg, reg);
    }
    ra.Consume(cond);
    return kX86CondNz;
}

CERFENGINE_FORCEINLINE uint8_t SlotTestTemp(X86RegAlloc& ra, uint32_t allowed) {
    const uint8_t r = ra.FreeScratch();
    return r != X86RegAlloc::kNoReg ? r : ra.AllocTemp(allowed);
}

uint8_t EmitSlotTest(X86Lowering& l, const SlotTest& t) {
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    ra.BeforeFlagsClobber();
    switch (t.pred) {
    case SlotPredicate::kNonZero:
        x86::EmitCmpByteBaseDisp32Imm8(c, x86::kStateReg, t.disp[0], 0u);
        return kX86CondNz;
    case SlotPredicate::kAndNot: {
        const uint8_t r = SlotTestTemp(ra, kX86ByteRegs);
        x86::EmitMovByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        x86::EmitSubByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[1]);
        x86::EmitCmpReg8Imm8(c, r, 1u);
        ra.ReleaseTemp(r);
        return kX86CondZ;
    }
    case SlotPredicate::kNotEqual: {
        const uint8_t r = SlotTestTemp(ra, kX86ByteRegs);
        x86::EmitMovByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        x86::EmitCmpReg8BaseDisp32(c, r, x86::kStateReg, t.disp[1]);
        ra.ReleaseTemp(r);
        return kX86CondNz;
    }
    case SlotPredicate::kOrNotEqual: {
        const uint8_t r = SlotTestTemp(ra, kX86ByteRegs);
        x86::EmitMovByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[1]);
        x86::EmitXorByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[2]);
        x86::EmitOrByteRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        ra.ReleaseTemp(r);
        return kX86CondNz;
    }
    case SlotPredicate::kZero32: {
        const uint8_t r = SlotTestTemp(ra, kX86AllocRegs);
        x86::EmitMovRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        x86::EmitTestRegReg(c, r, r);
        ra.ReleaseTemp(r);
        return kX86CondZ;
    }
    case SlotPredicate::kEqual32: {
        const uint8_t r = SlotTestTemp(ra, kX86AllocRegs);
        x86::EmitMovRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        x86::EmitCmpRegBaseDisp32(c, r, x86::kStateReg, t.disp[1]);
        ra.ReleaseTemp(r);
        return kX86CondZ;
    }
    case SlotPredicate::kZero64: {
        const uint8_t r = SlotTestTemp(ra, kX86AllocRegs);
        x86::EmitMovRegBaseDisp32(c, r, x86::kStateReg, t.disp[0]);
        x86::EmitOrRegBaseDisp32(c, r, x86::kStateReg, t.disp[0] + 4);
        ra.ReleaseTemp(r);
        return kX86CondZ;
    }
    case SlotPredicate::kEqual64: {
        const uint8_t lo = ra.AllocTemp(kX86AllocRegs);
        const uint8_t hi = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegBaseDisp32(c, lo, x86::kStateReg, t.disp[0]);
        x86::EmitXorRegBaseDisp32(c, lo, x86::kStateReg, t.disp[1]);
        x86::EmitMovRegBaseDisp32(c, hi, x86::kStateReg, t.disp[0] + 4);
        x86::EmitXorRegBaseDisp32(c, hi, x86::kStateReg, t.disp[1] + 4);
        x86::EmitOrReg32Reg32(c, lo, hi);
        ra.ReleaseTemp(hi);
        ra.ReleaseTemp(lo);
        return kX86CondZ;
    }
    }
    throw EmitFault("CerfIR x86 lowering: slot predicate has no lowering");
}

}

void X86EmitLabel(X86Lowering& l, uint32_t label) {
    l.Regs().Fence();
    l.ClearFlagSource();
    l.BindLabel(label);
}

void X86EmitBranch(X86Lowering& l, Ref cond, uint32_t label, bool if_true) {
    uint8_t*& c = l.Cursor();
    if (IsConstRef(cond)) {
        l.AddLabelFixup(label, x86::EmitJmpLabel32(c));
        return;
    }
    const uint8_t true_cc = EmitConditionTest(l, cond);
    const uint8_t cc      = if_true ? true_cc : X86InvertCond(true_cc);
    l.AddLabelFixup(label, x86::EmitRel32Label(c, static_cast<uint8_t>(0x80u + cc)));
}

uint8_t* X86EmitGuardBranch(X86Lowering& l, const SlotTest& t) {
    const uint8_t true_cc = EmitSlotTest(l, t);
    const uint8_t cc      = t.if_true ? true_cc : X86InvertCond(true_cc);
    return x86::EmitRel32Label(l.Cursor(), static_cast<uint8_t>(0x80u + cc));
}

void X86EmitBranchSlots(X86Lowering& l, const SlotTest& t, uint32_t label) {
    l.AddLabelFixup(label, X86EmitGuardBranch(l, t));
}

void X86EmitSetSlotTest(X86Lowering& l, const SlotTest& t, int32_t dest) {
    l.Regs().BeforeSlotWrite(dest, 4u);
    const uint8_t true_cc = EmitSlotTest(l, t);
    const uint8_t cc      = t.if_true ? true_cc : X86InvertCond(true_cc);
    uint8_t*&     c       = l.Cursor();
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, dest, 0u);
    x86::EmitSetccBaseDisp32(c, static_cast<uint8_t>(x86::kSetO + cc), x86::kStateReg, dest);
    l.ClearFlagSource();
}

void X86LowerGetSlot(X86Lowering& l, Ref ref) {
    const Inst& inst = l.GetBlock().At(ref);
    if (inst.type == Type::kI64) throw EmitFault("CerfIR x86 lowering: 64-bit slot read");
    l.Regs().DefineSlot(ref, static_cast<int32_t>(inst.aux));
}

void X86EmitSetSlot(X86Lowering& l, Ref value, int32_t disp, bool sext) {
    const Block& b  = l.GetBlock();
    X86RegAlloc& ra = l.Regs();
    if (sext) {
        X86StoreSlotSext(l, value, disp);
        return;
    }
    if (IsSlotRef(value)) {
        ra.BeforeSlotWrite(disp, 4u);
        uint8_t reg = ra.FreeScratch();
        if (reg == X86RegAlloc::kNoReg) {
            reg = ra.MaterializeWide(value, kX86AllocRegs);
        } else {
            x86::EmitMovRegBaseDisp32(l.Cursor(), reg, x86::kStateReg, SlotRefDisp(value));
        }
        x86::EmitMovBaseDisp32Reg(l.Cursor(), x86::kStateReg, disp, reg);
        return;
    }
    if (IsConstRef(value) && b.ConstType(value) == Type::kI32) {
        ra.BeforeSlotWrite(disp, 4u);
        x86::EmitMovBaseDisp32Imm32(l.Cursor(), x86::kStateReg, disp,
                                    static_cast<uint32_t>(b.ConstValue(value)));
        return;
    }
    X86StoreSlot(l, value, disp);
}

namespace {

void LoadCallSource(X86Lowering& l, const X86CallSite& s, uint32_t i, uint8_t reg) {
    uint8_t*& c = l.Cursor();
    if (i >= s.num_args) {
        X86LoadCallArg(c, s.extra[i - s.num_args], reg);
        return;
    }
    if (i >= 3u) throw EmitFault("CerfIR x86 lowering: a call site names more than three value arguments");
    const Block& b   = l.GetBlock();
    const Ref    arg = s.args[i];
    if (IsConstRef(arg)) {
        const uint64_t v = b.ConstValue(arg);
        if (b.ConstType(arg) == Type::kPtr) {
            x86::EmitMovPtrRegImm(c, reg, reinterpret_cast<const void*>(static_cast<uintptr_t>(v)));
        } else {
            x86::EmitMovRegImm32(c, reg, static_cast<uint32_t>(v));
        }
        return;
    }
    if (IsSlotRef(arg)) {
        x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, SlotRefDisp(arg));
        return;
    }
    l.Regs().CopyOut(arg, reg);
}

}

void X86EmitCallX64(X86Lowering& l, const X86CallSite& s) {
    X86RequireKnownCallAbi();
    const Block& b  = l.GetBlock();
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    for (uint32_t i = 0; i < s.num_args; ++i) {
        if (b.TypeOf(s.args[i]) == Type::kI64) {
            throw EmitFault("CerfIR x86 lowering: 64-bit helper argument");
        }
    }
    if (s.store_pc) {
        ra.BeforeSlotWrite(b.PcSlot(), 4u);
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, b.PcSlot(), s.pc);
    }
    ra.BeforeFlagsClobber();
    ra.BeforeStateWrite();
    ra.UnlockAll();
    ra.Evict(kX86ScratchRegs);

    const uint32_t total  = s.num_args + s.num_extra;
    const uint32_t stack  = total > kX86ArgRegCount ? total - kX86ArgRegCount : 0u;
    const uint32_t padded = stack + (stack & 1u);
    if (padded != stack) x86::EmitSubPtrRegImm32(c, x86::kEsp, 8u);
    for (uint32_t i = total; i-- > kX86ArgRegCount;) {
        LoadCallSource(l, s, i, x86::kR10);
        x86::EmitPushReg(c, x86::kR10);
    }
    const uint32_t in_regs = total < kX86ArgRegCount ? total : kX86ArgRegCount;
    for (uint32_t i = 0; i < in_regs; ++i) LoadCallSource(l, s, i, kX86ArgRegs[i]);
    x86::EmitCall(c, s.fn);
    if (padded != 0u) x86::EmitAddPtrRegImm32(c, x86::kEsp, padded * 8u);
    ra.UnlockAll();
    l.ClearFlagSource();
    if (s.result_type == Type::kI64) {
        x86::EmitMovPtrRegReg(c, x86::kEdx, x86::kEax);
        x86::EmitShrReg64Imm(c, x86::kEdx, 32u);
        ra.DefineInReg(s.result, x86::kEax, true);
        ra.DefineInReg(l.HiHalf(s.result), x86::kEdx, true);
    } else if (s.result_type != Type::kVoid) {
        ra.DefineInReg(s.result, x86::kEax, s.result_type != Type::kI1);
    }
}

bool X86EmitLeanCall(X86Lowering& l, const X86CallSite& s) {
    if constexpr (x86::kX64) {
        return false;
    } else {
        return X86EmitLeanCall32(l, s);
    }
}

void X86EmitCall(X86Lowering& l, const X86CallSite& s) {
    if constexpr (x86::kX64) {
        X86EmitCallX64(l, s);
    } else {
        X86EmitCall32(l, s);
    }
}

bool X86EmitLeanCall32(X86Lowering& l, const X86CallSite& s) {
    const Block& b = l.GetBlock();
    if (!l.Regs().Idle() || s.result_type != Type::kVoid) return false;
    for (uint32_t i = 0; i < s.num_args; ++i) {
        if (IsValueRef(s.args[i]) || (IsConstRef(s.args[i]) && b.ConstType(s.args[i]) == Type::kI64)) {
            return false;
        }
    }
    uint8_t*       c        = l.Cursor();
    const bool     fastcall = s.conv == CallConv::kFastcall;
    const uint32_t in_regs  = fastcall ? (s.num_args < 2u ? s.num_args : 2u) : 0u;
    if (s.store_pc) x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, b.PcSlot(), s.pc);
    for (uint32_t i = s.num_extra; i-- > 0u;) X86EmitPushCallArg(c, s.extra[i]);
    for (uint32_t i = s.num_args; i-- > in_regs;) {
        if (IsConstRef(s.args[i])) {
            x86::EmitPush32(c, static_cast<uint32_t>(b.ConstValue(s.args[i])));
        } else {
            x86::EmitPushBaseDisp32(c, x86::kStateReg, SlotRefDisp(s.args[i]));
        }
    }
    static constexpr uint8_t kArgRegs[2] = {x86::kEcx, x86::kEdx};
    for (uint32_t i = 0; i < in_regs; ++i) {
        if (IsConstRef(s.args[i])) {
            x86::EmitMovRegImm32(c, kArgRegs[i], static_cast<uint32_t>(b.ConstValue(s.args[i])));
        } else {
            x86::EmitMovRegBaseDisp32(c, kArgRegs[i], x86::kStateReg, SlotRefDisp(s.args[i]));
        }
    }
    x86::EmitCall(c, s.fn);
    const uint32_t arg_bytes = (s.num_args + s.num_extra) * 4u;
    if (!fastcall && arg_bytes != 0u) x86::EmitAddRegImm32(c, x86::kEsp, arg_bytes);
    l.Cursor() = c;
    l.ClearFlagSource();
    return true;
}

void X86EmitCall32(X86Lowering& l, const X86CallSite& s) {
    const Block& b  = l.GetBlock();
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    for (uint32_t i = 0; i < s.num_args; ++i) {
        if (b.TypeOf(s.args[i]) == Type::kI64) {
            throw EmitFault("CerfIR x86 lowering: 64-bit helper argument");
        }
    }
    const bool     fastcall = s.conv == CallConv::kFastcall;
    const uint32_t in_regs  = fastcall ? (s.num_args < 2u ? s.num_args : 2u) : 0u;
    if (s.store_pc) {
        ra.BeforeSlotWrite(b.PcSlot(), 4u);
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, b.PcSlot(), s.pc);
    }
    ra.BeforeFlagsClobber();
    for (uint32_t i = s.num_extra; i-- > 0u;) X86EmitPushCallArg(c, s.extra[i]);
    for (uint32_t i = s.num_args; i-- > in_regs;) {
        const Ref arg = s.args[i];
        if (IsConstRef(arg)) {
            x86::EmitPush32(c, static_cast<uint32_t>(b.ConstValue(arg)));
        } else if (IsSlotRef(arg)) {
            x86::EmitPushBaseDisp32(c, x86::kStateReg, SlotRefDisp(arg));
        } else if (ra.At(arg).kind == X86RegAlloc::Kind::kSlot && X86SlotBytes(b.TypeOf(arg)) == 4u) {
            x86::EmitPushBaseDisp32(c, x86::kStateReg, ra.At(arg).disp);
        } else {
            x86::EmitPushReg(c, ra.MaterializeWide(arg, kX86AllocRegs));
        }
    }
    for (uint32_t i = in_regs; i < s.num_args; ++i) ra.Consume(s.args[i]);
    ra.BeforeStateWrite();
    static constexpr uint8_t kArgRegs[2] = {x86::kEcx, x86::kEdx};
    bool direct = true;
    for (uint32_t i = 0; i < in_regs; ++i) direct = direct && !IsValueRef(s.args[i]);
    ra.UnlockAll();
    if (direct) {
        ra.Evict(kX86ScratchRegs);
        for (uint32_t i = 0; i < in_regs; ++i) {
            const Ref arg = s.args[i];
            if (IsConstRef(arg)) {
                x86::EmitMovRegImm32(c, kArgRegs[i], static_cast<uint32_t>(b.ConstValue(arg)));
            } else {
                x86::EmitMovRegBaseDisp32(c, kArgRegs[i], x86::kStateReg, SlotRefDisp(arg));
            }
        }
        x86::EmitCall(c, s.fn);
    } else {
        uint32_t claimed = 0u;
        for (uint32_t i = 0; i < in_regs; ++i) {
            ra.UnlockAll();
            for (uint32_t k = 0; k < i; ++k) ra.Lock(kArgRegs[k]);
            ra.CopyIntoFixed(s.args[i], kArgRegs[i]);
            claimed |= RegBit(kArgRegs[i]);
        }
        ra.UnlockAll();
        ra.Evict(kX86ScratchRegs & ~claimed);
        x86::EmitCall(c, s.fn);
        for (uint32_t i = 0; i < in_regs; ++i) ra.ReleaseTemp(kArgRegs[i]);
    }
    ra.UnlockAll();
    const uint32_t arg_bytes = (s.num_args + s.num_extra) * 4u;
    if (!fastcall && arg_bytes != 0u) x86::EmitAddRegImm32(c, x86::kEsp, arg_bytes);
    l.ClearFlagSource();
    if (s.result_type == Type::kI64) {
        ra.DefineInReg(s.result, x86::kEax, true);
        ra.DefineInReg(l.HiHalf(s.result), x86::kEdx, true);
    } else if (s.result_type != Type::kVoid) {
        ra.DefineInReg(s.result, x86::kEax, s.result_type != Type::kI1);
    }
}

void X86LowerCall(X86Lowering& l, Ref ref) {
    const Block&    b    = l.GetBlock();
    const Inst&     inst = b.At(ref);
    const CallDesc& call = b.CallAt(inst.aux);
    X86EmitCall(l, X86CallSite{call.fn, call.conv, call.num_args,
                               {inst.args[0], inst.args[1], inst.args[2]}, call.extra,
                               call.num_extra, call.store_pc, call.pc, ref, inst.type});
}

void X86Lowering::EmitCallConstBusy(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                    CallArg a1, CallArg a2, CallArg a3) {
    const Ref      refs[3]   = {num_args > 0u ? block_->ConstArg(a0) : kNoRef,
                                num_args > 1u ? block_->ConstArg(a1) : kNoRef,
                                num_args > 2u ? block_->ConstArg(a2) : kNoRef};
    const CallArg  extra[1]  = {a3};
    const uint32_t ref_args  = num_args > 3u ? 3u : num_args;
    const uint32_t num_extra = num_args - ref_args;
    X86EmitCall(*this, X86CallSite{spec.fn, spec.conv, ref_args, {refs[0], refs[1], refs[2]},
                                   extra, num_extra, spec.store_pc, spec.pc, kNoRef, Type::kVoid});
    regs_.EndOp();
}

void X86LowerUnsupported(X86Lowering& l, Ref ref) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR x86 lowering: op %s has no x86-32 lowering",
                  InfoOf(l.GetBlock().At(ref).op).name);
    throw EmitFault(text);
}

}
