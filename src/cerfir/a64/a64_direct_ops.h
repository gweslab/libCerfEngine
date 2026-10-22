#pragma once

#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace a64_direct {

CERFENGINE_FORCEINLINE A64RegAlloc::Operand ImmOperand(uint8_t*& c, Op op, uint32_t imm) {
    if (imm == 0u && op != Op::kShl && op != Op::kLsr && op != Op::kAsr && op != Op::kRor) {
        return A64RegAlloc::Operand{false, a64::kZr, 0u};
    }
    if (A64AluImmFits(op, imm) || op == Op::kShl || op == Op::kLsr || op == Op::kAsr ||
        op == Op::kRor) {
        return A64RegAlloc::Operand{true, 0u, imm};
    }
    a64::EmitMovImm32(c, kA64Tmp1, imm);
    return A64RegAlloc::Operand{false, kA64Tmp1, 0u};
}

CERFENGINE_FORCEINLINE A64RegAlloc::Operand SlotOperand(A64PinnedSlots& pins, uint8_t*& c,
                                                        uint32_t disp) {
    return A64RegAlloc::Operand{false, pins.Source(c, static_cast<int32_t>(disp), kA64Tmp1), 0u};
}

CERFENGINE_FORCEINLINE a64::Shift ShiftKind(Op shift) {
    switch (shift) {
    case Op::kShl: return a64::kLsl;
    case Op::kLsr: return a64::kLsr;
    case Op::kAsr: return a64::kAsr;
    default:       return a64::kRor;
    }
}

}

CERFENGINE_FORCEINLINE void A64Lowering::EmitLoad(Ref address, Ref writeback, uint32_t attr) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitLoad(*this, kNoRef, address, writeback, attr);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitStore(Ref address, Ref value, Ref writeback,
                                                   uint32_t attr) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitStore(*this, address, value, writeback, attr);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitCallDirect(const void* fn, CallConv, uint32_t num_args,
                                                        Ref a, Ref b, Ref c, const CallArg* extra,
                                                        uint32_t num_extra, bool store_pc,
                                                        uint32_t pc) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitCall(*this, A64CallSite{fn, num_args, {a, b, c}, extra, num_extra, store_pc, pc, kNoRef,
                                   Type::kVoid});
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitCallConst(const CallSpec& spec, uint32_t num_args,
                                                       CallArg a0, CallArg a1, CallArg a2,
                                                       CallArg a3) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Idle()) {
        EmitCallConstBusy(spec, num_args, a0, a1, a2, a3);
        return;
    }
    uint8_t* c = cursor_;
    if (spec.store_pc) pins_.StoreImm(c, block_->PcSlot(), spec.pc, kA64Tmp0);
    pins_.Park(c);
    const CallArg args[4] = {a0, a1, a2, a3};
    for (uint32_t i = 0; i < num_args; ++i) A64LoadCallArg(c, args[i], kA64ArgRegs[i]);
    a64::EmitCall(c, spec.fn);
    pins_.Unpark();
    cursor_ = c;
    ClearFlagSource();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotCopy32(uint32_t dest, uint32_t src, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        A64EmitSetSlot(*this, SlotRef(src), static_cast<int32_t>(dest), sext);
        regs_.EndOp();
        return;
    }
    const uint8_t value = pins_.Source(cursor_, static_cast<int32_t>(src), kA64Tmp0);
    if (sext) a64::EmitAsrImm(cursor_, kA64Tmp1, value, 31u);
    pins_.Store(cursor_, value, static_cast<int32_t>(dest));
    if (sext) pins_.Store(cursor_, kA64Tmp1, static_cast<int32_t>(dest) + 4);
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotCopy64(uint32_t dest, uint32_t src) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        for (uint32_t half = 0; half < 8u; half += 4u) {
            A64EmitSetSlot(*this, SlotRef(src + half), static_cast<int32_t>(dest + half), false);
            regs_.EndOp();
        }
        return;
    }
    pins_.Load(cursor_, kA64Tmp0, static_cast<int32_t>(src));
    pins_.Load(cursor_, kA64Tmp1, static_cast<int32_t>(src + 4u));
    pins_.Store(cursor_, kA64Tmp0, static_cast<int32_t>(dest));
    pins_.Store(cursor_, kA64Tmp1, static_cast<int32_t>(dest + 4u));
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitTransferWords(uint32_t attr, uint32_t list) {
    A64EmitTransferWords(*this, attr, list);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSetSlot(Ref value, uint32_t disp, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (regs_.Quiet()) {
        if (IsConstRef(value) && block_->ConstType(value) == Type::kI32) {
            const uint32_t v = static_cast<uint32_t>(block_->ConstValue(value));
            pins_.StoreImm(cursor_, static_cast<int32_t>(disp), v, kA64Tmp0);
            if (sext) {
                pins_.StoreImm(cursor_, static_cast<int32_t>(disp) + 4,
                               (v & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u, kA64Tmp0);
            }
            return;
        }
        if (IsSlotRef(value)) {
            EmitSlotCopy32(disp, static_cast<uint32_t>(SlotRefDisp(value)), sext);
            return;
        }
    }
    A64EmitSetSlot(*this, value, static_cast<int32_t>(disp), sext);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSetSlotConst(uint32_t disp, uint32_t value, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        A64EmitSetSlot(*this, block_->Const32(value), static_cast<int32_t>(disp), sext);
        regs_.EndOp();
        return;
    }
    pins_.StoreImm(cursor_, static_cast<int32_t>(disp), value, kA64Tmp0);
    if (sext) {
        pins_.StoreImm(cursor_, static_cast<int32_t>(disp) + 4,
                       (value & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u, kA64Tmp0);
    }
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitLabel(uint32_t label) {
    A64EmitLabel(*this, label);
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitBranch(Ref cond, uint32_t label, bool if_true) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitBranch(*this, cond, label, if_true);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitBranchSlots(const SlotTest& test, uint32_t label) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitBranchSlots(*this, test, label);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE uint8_t* A64Lowering::EmitGuardBranch(const SlotTest& test) {
    if (!HasRoom()) throw A64ArenaFull{};
    uint8_t* const site = A64EmitGuardBranch(*this, test);
    regs_.EndOp();
    return site;
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSetSlotTest(const SlotTest& test, uint32_t dest) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitSetSlotTest(*this, test, static_cast<int32_t>(dest));
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitGuardBind(uint8_t* site) {
    regs_.Fence();
    pins_.BindGuard(cursor_, site);
    ClearFlagSource();
    a64::FixupLabel(site, cursor_);
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitExitDirect(uint32_t target, uint16_t flags) {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitExitDirect(*this, target, flags);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitExitIndirect() {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitExitIndirect(*this);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitExitDispatcher() {
    if (!HasRoom()) throw A64ArenaFull{};
    A64EmitExitDispatcher(*this);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotAluFast(Op op, uint32_t a_disp,
                                                         const A64RegAlloc::Operand& b,
                                                         uint32_t dest, bool sext) {
    const uint8_t lhs = pins_.Source(cursor_, static_cast<int32_t>(a_disp), kA64Tmp0);
    if (sext) {
        A64EmitAlu(cursor_, op, kA64Tmp0, lhs, b, false);
        a64::EmitAsrImm(cursor_, kA64Tmp1, kA64Tmp0, 31u);
        pins_.Store(cursor_, kA64Tmp0, static_cast<int32_t>(dest));
        pins_.Store(cursor_, kA64Tmp1, static_cast<int32_t>(dest) + 4);
    } else {
        const uint8_t dst = pins_.Target(static_cast<int32_t>(dest), kA64Tmp0);
        A64EmitAlu(cursor_, op, dst, lhs, b, false);
        pins_.Commit(cursor_, dst, static_cast<int32_t>(dest));
    }
    ClearFlagSource();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotAluFlags(Op op, uint32_t a_disp,
                                                          const A64RegAlloc::Operand& b,
                                                          uint32_t mask, uint32_t dest) {
    A64CheckFusedFlags(op, mask);
    const uint8_t lhs = pins_.Source(cursor_, static_cast<int32_t>(a_disp), kA64Tmp0);
    const uint8_t dst =
        dest != kNoDest ? pins_.Target(static_cast<int32_t>(dest), kA64Tmp0) : kA64Tmp0;
    A64EmitAlu(cursor_, op, dst, lhs, b, mask != 0u);
    if (dest != kNoDest) pins_.Commit(cursor_, dst, static_cast<int32_t>(dest));
    A64StoreNzcvFlags(*this, mask, kA64ArithFlagConds);
    ClearFlagSource();
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitFusedAlu(Op op, Ref a, Ref b, uint32_t mask,
                                                      uint32_t dest, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if ((mask == 0u || !sext) && regs_.Quiet()) {
        const A64RegAlloc::Operand o =
            IsConstRef(b)
                ? a64_direct::ImmOperand(cursor_, op, static_cast<uint32_t>(block_->ConstValue(b)))
                : a64_direct::SlotOperand(pins_, cursor_, static_cast<uint32_t>(SlotRefDisp(b)));
        if (mask == 0u) {
            EmitSlotAluFast(op, static_cast<uint32_t>(SlotRefDisp(a)), o, dest, sext);
            return;
        }
        EmitSlotAluFlags(op, static_cast<uint32_t>(SlotRefDisp(a)), o, mask, dest);
        return;
    }
    A64EmitFusedAlu(*this, op, a, b, mask, static_cast<int32_t>(dest), sext);
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotImmFlags(Op op, uint32_t a_disp, uint32_t imm,
                                                          uint32_t mask, uint32_t dest) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        A64EmitFusedAlu(*this, op, SlotRef(a_disp), block_->Const32(imm), mask,
                        static_cast<int32_t>(dest), false);
        return;
    }
    EmitSlotAluFlags(op, a_disp, a64_direct::ImmOperand(cursor_, op, imm), mask, dest);
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotSlotFlags(Op op, uint32_t a_disp, uint32_t b_disp,
                                                           uint32_t mask, uint32_t dest) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        A64EmitFusedAlu(*this, op, SlotRef(a_disp), SlotRef(b_disp), mask,
                        static_cast<int32_t>(dest), false);
        return;
    }
    EmitSlotAluFlags(op, a_disp, a64_direct::SlotOperand(pins_, cursor_, b_disp), mask, dest);
}

CERFENGINE_FORCEINLINE bool A64Lowering::EmitSlotImm(Op op, uint32_t a_disp, uint32_t imm,
                                                     uint32_t dest, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        if (op == Op::kShl || op == Op::kLsr || op == Op::kAsr || op == Op::kRor) return false;
        A64EmitFusedAlu(*this, op, SlotRef(a_disp), block_->Const32(imm), 0u,
                        static_cast<int32_t>(dest), sext);
        return true;
    }
    EmitSlotAluFast(op, a_disp, a64_direct::ImmOperand(cursor_, op, imm), dest, sext);
    return true;
}

CERFENGINE_FORCEINLINE void A64Lowering::EmitSlotSlot(Op op, uint32_t a_disp, uint32_t b_disp,
                                                      uint32_t dest, bool sext) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) {
        A64EmitFusedAlu(*this, op, SlotRef(a_disp), SlotRef(b_disp), 0u, static_cast<int32_t>(dest),
                        sext);
        return;
    }
    EmitSlotAluFast(op, a_disp, a64_direct::SlotOperand(pins_, cursor_, b_disp), dest, sext);
}

CERFENGINE_FORCEINLINE bool A64Lowering::EmitShiftedSlotAlu(Op op, uint32_t a_disp, uint32_t b_disp,
                                                            Op shift, uint32_t amount, uint32_t mask,
                                                            uint32_t dest) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (!regs_.Quiet()) return false;
    const bool arith = op == Op::kAdd || op == Op::kSub;
    const bool logic = op == Op::kAnd || op == Op::kOr || op == Op::kXor;
    if ((!arith && !logic) || (arith && shift == Op::kRor)) return false;
    if (amount == 0u || amount >= 32u) {
        throw EmitFault("CerfIR A64 lowering: a shifted operand takes a constant amount of 1 to 31");
    }
    A64CheckFusedFlags(op, mask);
    const bool        set  = mask != 0u;
    const a64::Shift  kind = a64_direct::ShiftKind(shift);
    const uint8_t     lhs  = pins_.Source(cursor_, static_cast<int32_t>(a_disp), kA64Tmp0);
    const uint8_t     rhs  = pins_.Source(cursor_, static_cast<int32_t>(b_disp), kA64Tmp1);
    const uint8_t     dst =
        dest != kNoDest ? pins_.Target(static_cast<int32_t>(dest), kA64Tmp0) : kA64Tmp0;
    if (arith) {
        if (op == Op::kAdd) {
            set ? a64::EmitAddsReg(cursor_, dst, lhs, rhs, kind, amount)
                : a64::EmitAddReg(cursor_, dst, lhs, rhs, kind, amount);
        } else {
            set ? a64::EmitSubsReg(cursor_, dst, lhs, rhs, kind, amount)
                : a64::EmitSubReg(cursor_, dst, lhs, rhs, kind, amount);
        }
    } else {
        const a64::LogicOp lop = op == Op::kAnd ? (set ? a64::kAnds : a64::kAnd)
                                 : op == Op::kOr ? a64::kOrr
                                                 : a64::kEor;
        a64::EmitLogicReg(cursor_, lop, dst, lhs, rhs, kind, amount);
        if (set && op != Op::kAnd) a64::EmitTstReg(cursor_, dst, dst);
    }
    if (dest != kNoDest) pins_.Commit(cursor_, dst, static_cast<int32_t>(dest));
    A64StoreNzcvFlags(*this, mask, kA64ArithFlagConds);
    ClearFlagSource();
    return true;
}

}
