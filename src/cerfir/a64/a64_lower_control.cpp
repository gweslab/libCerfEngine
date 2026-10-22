#include <cstdio>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using a64::kStateReg;

CERFENGINE_FORCEINLINE void LoadFlagByte(A64Lowering& l, uint8_t reg, int32_t disp) {
    l.Pins().CheckUnpinned(disp, 1u);
    a64::EmitLdrb(l.Cursor(), reg, kStateReg, disp);
}

uint8_t EmitSlotTest(A64Lowering& l, const SlotTest& t) {
    A64RegAlloc&    ra = l.Regs();
    A64PinnedSlots& p  = l.Pins();
    uint8_t*&       c  = l.Cursor();
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();
    switch (t.pred) {
    case SlotPredicate::kNonZero:
        LoadFlagByte(l, kA64Tmp0, t.disp[0]);
        a64::EmitCmpImm(c, kA64Tmp0, 0u);
        return a64::kNe;
    case SlotPredicate::kAndNot:
        LoadFlagByte(l, kA64Tmp0, t.disp[0]);
        LoadFlagByte(l, kA64Tmp1, t.disp[1]);
        a64::EmitSubReg(c, kA64Tmp0, kA64Tmp0, kA64Tmp1);
        a64::EmitUxtb(c, kA64Tmp0, kA64Tmp0);
        a64::EmitCmpImm(c, kA64Tmp0, 1u);
        return a64::kEq;
    case SlotPredicate::kNotEqual:
        LoadFlagByte(l, kA64Tmp0, t.disp[0]);
        LoadFlagByte(l, kA64Tmp1, t.disp[1]);
        a64::EmitCmpReg(c, kA64Tmp0, kA64Tmp1);
        return a64::kNe;
    case SlotPredicate::kOrNotEqual:
        LoadFlagByte(l, kA64Tmp0, t.disp[1]);
        LoadFlagByte(l, kA64Tmp1, t.disp[2]);
        a64::EmitLogicReg(c, a64::kEor, kA64Tmp0, kA64Tmp0, kA64Tmp1);
        LoadFlagByte(l, kA64Tmp1, t.disp[0]);
        a64::EmitLogicReg(c, a64::kOrr, kA64Tmp0, kA64Tmp0, kA64Tmp1);
        a64::EmitCmpImm(c, kA64Tmp0, 0u);
        return a64::kNe;
    case SlotPredicate::kZero32:
        a64::EmitCmpImm(c, p.Source(c, t.disp[0], kA64Tmp0), 0u);
        return a64::kEq;
    case SlotPredicate::kEqual32: {
        const uint8_t a = p.Source(c, t.disp[0], kA64Tmp0);
        const uint8_t b = p.Source(c, t.disp[1], kA64Tmp1);
        a64::EmitCmpReg(c, a, b);
        return a64::kEq;
    }
    case SlotPredicate::kZero64: {
        const uint8_t lo = p.Source(c, t.disp[0], kA64Tmp0);
        const uint8_t hi = p.Source(c, t.disp[0] + 4, kA64Tmp1);
        a64::EmitLogicReg(c, a64::kOrr, kA64Tmp0, lo, hi);
        a64::EmitCmpImm(c, kA64Tmp0, 0u);
        return a64::kEq;
    }
    case SlotPredicate::kEqual64: {
        const uint8_t a_lo = p.Source(c, t.disp[0], kA64Tmp0);
        const uint8_t b_lo = p.Source(c, t.disp[1], kA64Tmp1);
        const uint8_t a_hi = p.Source(c, t.disp[0] + 4, kA64Tmp2);
        const uint8_t b_hi = p.Source(c, t.disp[1] + 4, a64::kIp0);
        a64::EmitCmpReg(c, a_lo, b_lo);
        a64::EmitCcmpReg(c, a_hi, b_hi, 0u, a64::kEq);
        return a64::kEq;
    }
    }
    throw EmitFault("CerfIR A64 lowering: slot predicate has no lowering");
}

constexpr uint32_t kNoLabel = 0xFFFFFFFFu;

uint8_t* EmitTestBranch(A64Lowering& l, const SlotTest& t, uint32_t label) {
    uint8_t*&       c = l.Cursor();
    A64PinnedSlots& p = l.Pins();
    if (t.pred == SlotPredicate::kNonZero) {
        LoadFlagByte(l, kA64Tmp0, t.disp[0]);
        if (label != kNoLabel) p.BranchToLabel(c, label);
        return a64::EmitCbzLabel(c, kA64Tmp0, t.if_true, false);
    }
    const uint8_t true_cc = EmitSlotTest(l, t);
    if (label != kNoLabel) p.BranchToLabel(c, label);
    return a64::EmitBCondLabel(c, t.if_true ? true_cc : a64::InvertCond(true_cc));
}

}

void A64EmitLabel(A64Lowering& l, uint32_t label) {
    l.Regs().Fence();
    l.Pins().BindLabel(l.Cursor(), label);
    l.ClearFlagSource();
    l.BindLabel(label);
}

void A64EmitBranch(A64Lowering& l, Ref cond, uint32_t label, bool if_true) {
    A64RegAlloc&    ra = l.Regs();
    A64PinnedSlots& p  = l.Pins();
    uint8_t*&       c  = l.Cursor();
    if (IsConstRef(cond)) {
        p.BranchToLabel(c, label);
        l.AddLabelFixup(label, a64::EmitBLabel(c));
        p.MarkUnreachable();
        return;
    }
    if (ra.At(cond).kind == A64RegAlloc::Kind::kFlags) {
        const uint8_t cc = ra.At(cond).cond;
        ra.Consume(cond);
        p.BranchToLabel(c, label);
        l.AddLabelFixup(label, a64::EmitBCondLabel(c, if_true ? cc : a64::InvertCond(cc)));
        return;
    }
    const uint8_t reg = ra.Materialize(cond);
    ra.Consume(cond);
    p.BranchToLabel(c, label);
    l.AddLabelFixup(label, a64::EmitCbzLabel(c, reg, if_true, false));
}

uint8_t* A64EmitGuardBranch(A64Lowering& l, const SlotTest& t) {
    uint8_t* const site = EmitTestBranch(l, t, kNoLabel);
    l.Pins().RecordGuard(site);
    return site;
}

void A64EmitBranchSlots(A64Lowering& l, const SlotTest& t, uint32_t label) {
    l.AddLabelFixup(label, EmitTestBranch(l, t, label));
}

void A64EmitSetSlotTest(A64Lowering& l, const SlotTest& t, int32_t dest) {
    l.Regs().BeforeSlotWrite(dest, 4u);
    const uint8_t true_cc = EmitSlotTest(l, t);
    const uint8_t cc      = t.if_true ? true_cc : a64::InvertCond(true_cc);
    uint8_t*&     c       = l.Cursor();
    const uint8_t dst     = l.Pins().Target(dest, kA64Tmp0);
    a64::EmitCset(c, dst, cc);
    l.Pins().Commit(c, dst, dest);
}

void A64LowerGetSlot(A64Lowering& l, Ref ref) {
    const Inst& inst = l.GetBlock().At(ref);
    if (inst.type == Type::kI64) throw EmitFault("CerfIR A64 lowering: 64-bit slot read");
    l.Regs().DefineSlot(ref, static_cast<int32_t>(inst.aux));
}

void A64StoreSlot(A64Lowering& l, Ref value, int32_t disp) {
    const Block&   b     = l.GetBlock();
    const uint32_t bytes = A64SlotBytes(b.TypeOf(value));
    A64RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    if (bytes != 1u && bytes != 4u) throw EmitFault("CerfIR A64 lowering: slot width has no store");

    ra.BeforeSlotWrite(disp, bytes);
    if (bytes == 1u) l.Pins().CheckUnpinned(disp, 1u);
    if (IsConstRef(value)) {
        const uint32_t v = static_cast<uint32_t>(b.ConstValue(value));
        if (bytes == 4u) {
            l.Pins().StoreImm(c, disp, v, kA64Tmp0);
        } else if ((v & 0xFFu) == 0u) {
            a64::EmitStrb(c, a64::kZr, kStateReg, disp);
        } else {
            a64::EmitMovImm32(c, kA64Tmp0, v & 0xFFu);
            a64::EmitStrb(c, kA64Tmp0, kStateReg, disp);
        }
        return;
    }
    if (bytes == 1u && IsValueRef(value) && ra.At(value).kind == A64RegAlloc::Kind::kFlags) {
        a64::EmitCset(c, kA64Tmp0, ra.At(value).cond);
        a64::EmitStrb(c, kA64Tmp0, kStateReg, disp);
        ra.Consume(value);
        return;
    }
    const uint8_t reg = ra.Materialize(value);
    if (bytes == 1u) {
        a64::EmitStrb(c, reg, kStateReg, disp);
    } else {
        l.Pins().Store(c, reg, disp);
    }
    ra.NoteStored(value, disp);
    ra.Consume(value);
}

void A64StoreSlotSext(A64Lowering& l, Ref value, int32_t disp) {
    const Block& b  = l.GetBlock();
    A64RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (b.TypeOf(value) != Type::kI32) {
        throw EmitFault("CerfIR A64 lowering: sign-extended slot store of a value other than i32");
    }
    ra.BeforeSlotWrite(disp, 8u);
    A64PinnedSlots& p = l.Pins();
    if (IsConstRef(value)) {
        const uint32_t v = static_cast<uint32_t>(b.ConstValue(value));
        p.StoreImm(c, disp, v, kA64Tmp0);
        p.StoreImm(c, disp + 4, (v & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u, kA64Tmp0);
        return;
    }
    if (IsSlotRef(value)) {
        p.Load(c, kA64Tmp0, SlotRefDisp(value));
        p.Store(c, kA64Tmp0, disp);
        a64::EmitAsrImm(c, kA64Tmp0, kA64Tmp0, 31u);
        p.Store(c, kA64Tmp0, disp + 4);
        return;
    }
    const uint8_t reg = ra.Materialize(value);
    p.Store(c, reg, disp);
    a64::EmitAsrImm(c, kA64Tmp0, reg, 31u);
    p.Store(c, kA64Tmp0, disp + 4);
    ra.NoteStored(value, disp);
    ra.Consume(value);
}

void A64EmitSetSlot(A64Lowering& l, Ref value, int32_t disp, bool sext) {
    const Block& b  = l.GetBlock();
    A64RegAlloc& ra = l.Regs();
    if (sext) {
        A64StoreSlotSext(l, value, disp);
        return;
    }
    if (IsSlotRef(value)) {
        ra.BeforeSlotWrite(disp, 4u);
        A64PinnedSlots& p   = l.Pins();
        const uint8_t   src = p.Source(l.Cursor(), SlotRefDisp(value), kA64Tmp0);
        p.Store(l.Cursor(), src, disp);
        return;
    }
    if (IsConstRef(value) && b.ConstType(value) == Type::kI32) {
        ra.BeforeSlotWrite(disp, 4u);
        l.Pins().StoreImm(l.Cursor(), disp, static_cast<uint32_t>(b.ConstValue(value)), kA64Tmp0);
        return;
    }
    A64StoreSlot(l, value, disp);
}

namespace {

uint32_t CallArgBytes(const A64CallSite& s, uint32_t i) {
    if (i < s.num_args) return 4u;
    return s.extra[i - s.num_args].kind == CallArgKind::kHostAddress ? 8u : 4u;
}

void LoadCallSource(A64Lowering& l, const A64CallSite& s, uint32_t i, uint8_t reg) {
    uint8_t*& c = l.Cursor();
    if (i >= s.num_args) {
        A64LoadCallArg(c, s.extra[i - s.num_args], reg);
        return;
    }
    if (i >= 3u) throw EmitFault("CerfIR A64 lowering: a call site names more than three value arguments");
    const Block& b   = l.GetBlock();
    const Ref    arg = s.args[i];
    if (IsConstRef(arg)) {
        const uint64_t v = b.ConstValue(arg);
        if (b.ConstType(arg) == Type::kPtr) {
            a64::EmitMovImm64(c, reg, v);
        } else {
            a64::EmitMovImm32(c, reg, static_cast<uint32_t>(v));
        }
        return;
    }
    if (IsSlotRef(arg)) {
        l.Pins().Load(c, reg, SlotRefDisp(arg));
        return;
    }
    l.Regs().CopyOut(arg, reg);
}

}

void A64EmitCall(A64Lowering& l, const A64CallSite& s) {
    const Block& b  = l.GetBlock();
    A64RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    for (uint32_t i = 0; i < s.num_args; ++i) {
        if (b.TypeOf(s.args[i]) == Type::kI64) {
            throw EmitFault("CerfIR A64 lowering: 64-bit helper argument");
        }
    }
    if (s.store_pc) {
        ra.BeforeSlotWrite(b.PcSlot(), 4u);
        l.Pins().StoreImm(c, b.PcSlot(), s.pc, kA64Tmp0);
    }
    ra.BeforeFlagsClobber();
    ra.BeforeStateWrite();
    ra.UnlockAll();
    ra.Evict(kA64ScratchRegs);
    l.Pins().Park(c);

    const uint32_t total = s.num_args + s.num_extra;
    A64StackArgs   stack;
    for (uint32_t i = kA64ArgRegCount; i < total; ++i) {
        const uint32_t bytes = CallArgBytes(s, i);
        const int32_t  at    = stack.Place(bytes);
        if (at + 8 > kA64OutArgBytes) throw EmitFault("CerfIR A64 lowering: call arguments exceed the frame");
        LoadCallSource(l, s, i, kA64Tmp0);
        if (bytes == 8u) {
            a64::EmitStrX(c, kA64Tmp0, a64::kSp, at);
        } else {
            a64::EmitStrW(c, kA64Tmp0, a64::kSp, at);
        }
    }
    const uint32_t in_regs = total < kA64ArgRegCount ? total : kA64ArgRegCount;
    for (uint32_t i = 0; i < in_regs; ++i) LoadCallSource(l, s, i, kA64ArgRegs[i]);
    a64::EmitCall(c, s.fn);
    l.Pins().Unpark();
    ra.UnlockAll();
    l.ClearFlagSource();
    if (s.result_type == Type::kI64) {
        a64::EmitLsrImm(c, a64::kX1, a64::kX0, 32u, true);
        ra.DefineInReg(s.result, a64::kX0);
        ra.DefineInReg(l.HiHalf(s.result), a64::kX1);
    } else if (s.result_type == Type::kI1) {
        a64::EmitUxtb(c, a64::kX0, a64::kX0);
        ra.DefineInReg(s.result, a64::kX0);
    } else if (s.result_type != Type::kVoid) {
        ra.DefineInReg(s.result, a64::kX0);
    }
}

void A64LowerCall(A64Lowering& l, Ref ref) {
    const Block&    b    = l.GetBlock();
    const Inst&     inst = b.At(ref);
    const CallDesc& call = b.CallAt(inst.aux);
    A64EmitCall(l, A64CallSite{call.fn, call.num_args, {inst.args[0], inst.args[1], inst.args[2]},
                               call.extra, call.num_extra, call.store_pc, call.pc, ref, inst.type});
}

void A64Lowering::EmitCallConstBusy(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                    CallArg a1, CallArg a2, CallArg a3) {
    const Ref      refs[3]   = {num_args > 0u ? block_->ConstArg(a0) : kNoRef,
                                num_args > 1u ? block_->ConstArg(a1) : kNoRef,
                                num_args > 2u ? block_->ConstArg(a2) : kNoRef};
    const CallArg  extra[1]  = {a3};
    const uint32_t ref_args  = num_args > 3u ? 3u : num_args;
    const uint32_t num_extra = num_args - ref_args;
    A64EmitCall(*this, A64CallSite{spec.fn, ref_args, {refs[0], refs[1], refs[2]}, extra, num_extra,
                                   spec.store_pc, spec.pc, kNoRef, Type::kVoid});
    regs_.EndOp();
}

void A64LowerUnsupported(A64Lowering& l, Ref ref) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR A64 lowering: op %s has no A64 lowering",
                  InfoOf(l.GetBlock().At(ref).op).name);
    throw EmitFault(text);
}

}
