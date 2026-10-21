#pragma once

#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

CERFENGINE_FORCEINLINE uint8_t* X86Lowering::EmitCycleAdd(uint32_t cycles) {
    if (cycles >= 0x80u || !HasRoom() || !regs_.NoLiveFlags()) return EmitCycleAddSlow(cycles);
    uint8_t* const p = cursor_;
    std::memcpy(p, cycle_add_, sizeof(cycle_add_));
    p[kCycleAddImmOffset] = static_cast<uint8_t>(cycles);
    cursor_ = p + kCycleAddBytes;
    ClearFlagSource();
    return p + kCycleAddImmOffset;
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitLoad(Ref address, Ref writeback, uint32_t attr) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitLoad(*this, kNoRef, address, writeback, attr);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitStore(Ref address, Ref value, Ref writeback,
                                                   uint32_t attr) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitStore(*this, address, value, writeback, attr);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitCallDirect(const void* fn, CallConv conv,
                                                        uint32_t num_args, Ref a, Ref b, Ref c,
                                                        const CallArg* extra,
                                                        uint32_t num_extra, bool store_pc,
                                                        uint32_t pc) {
    if (!HasRoom()) throw X86ArenaFull{};
    const X86CallSite site{fn, conv, num_args, {a, b, c}, extra, num_extra, store_pc, pc,
                           kNoRef, Type::kVoid};
    if (!X86EmitLeanCall(*this, site)) X86EmitCall(*this, site);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitCallConst(const CallSpec& spec, uint32_t num_args,
                                                       CallArg a0, CallArg a1, CallArg a2,
                                                       CallArg a3) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (!regs_.Idle()) {
        EmitCallConstBusy(spec, num_args, a0, a1, a2, a3);
        return;
    }
    uint8_t* c = cursor_;
    if (spec.store_pc) x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, block_->PcSlot(), spec.pc);
    if constexpr (x86::kX64) {
        X86RequireKnownCallAbi();
        const CallArg args[4] = {a0, a1, a2, a3};
        for (uint32_t i = 0; i < num_args; ++i) X86LoadCallArg(c, args[i], kX86ArgRegs[i]);
        x86::EmitCall(c, spec.fn);
    } else {
        const bool     fastcall = spec.conv == CallConv::kFastcall;
        const uint32_t in_regs  = fastcall ? (num_args < 2u ? num_args : 2u) : 0u;
        if (num_args > 3u) x86::EmitPush32(c, X86CallArgImm(a3));
        if (num_args > 2u) x86::EmitPush32(c, X86CallArgImm(a2));
        if (num_args > 1u && in_regs < 2u) x86::EmitPush32(c, X86CallArgImm(a1));
        if (num_args > 0u && in_regs < 1u) x86::EmitPush32(c, X86CallArgImm(a0));
        if (in_regs > 0u) x86::EmitMovRegImm32(c, x86::kEcx, X86CallArgImm(a0));
        if (in_regs > 1u) x86::EmitMovRegImm32(c, x86::kEdx, X86CallArgImm(a1));
        x86::EmitCall(c, spec.fn);
        const uint32_t arg_bytes = num_args * 4u;
        if (!fastcall && arg_bytes != 0u) x86::EmitAddRegImm32(c, x86::kEsp, arg_bytes);
    }
    cursor_ = c;
    ClearFlagSource();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotCopy32(uint32_t dest, uint32_t src, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    const uint8_t reg = regs_.Quiet() ? regs_.FreeScratch() : X86RegAlloc::kNoReg;
    if (reg == X86RegAlloc::kNoReg) {
        X86EmitSetSlot(*this, SlotRef(src), static_cast<int32_t>(dest), sext);
        regs_.EndOp();
        return;
    }
    x86::EmitMovRegBaseDisp32(cursor_, reg, x86::kStateReg, static_cast<int32_t>(src));
    x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest), reg);
    if (sext) {
        x86::EmitSarReg32Imm(cursor_, reg, 31u);
        x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest) + 4, reg);
        ClearFlagSource();
    }
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotCopy64(uint32_t dest, uint32_t src) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (regs_.Quiet()) {
        const uint8_t reg = regs_.FreeScratch();
        if (reg != X86RegAlloc::kNoReg) {
            for (uint32_t half = 0; half < 8u; half += 4u) {
                x86::EmitMovRegBaseDisp32(cursor_, reg, x86::kStateReg,
                                          static_cast<int32_t>(src + half));
                x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg,
                                          static_cast<int32_t>(dest + half), reg);
            }
            return;
        }
    }
    for (uint32_t half = 0; half < 8u; half += 4u) {
        X86EmitSetSlot(*this, SlotRef(src + half), static_cast<int32_t>(dest + half), false);
        regs_.EndOp();
    }
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitTransferWords(uint32_t attr, uint32_t list) {
    X86EmitTransferWords(*this, attr, list);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSetSlot(Ref value, uint32_t disp, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (regs_.Quiet()) {
        if (IsConstRef(value) && block_->ConstType(value) == Type::kI32) {
            const uint32_t v = static_cast<uint32_t>(block_->ConstValue(value));
            x86::EmitMovBaseDisp32Imm32(cursor_, x86::kStateReg, static_cast<int32_t>(disp), v);
            if (sext) {
                x86::EmitMovBaseDisp32Imm32(cursor_, x86::kStateReg, static_cast<int32_t>(disp) + 4,
                                            (v & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u);
            }
            return;
        }
        const uint8_t reg = regs_.FreeScratch();
        if (IsSlotRef(value) && reg != X86RegAlloc::kNoReg) {
            x86::EmitMovRegBaseDisp32(cursor_, reg, x86::kStateReg, SlotRefDisp(value));
            x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(disp), reg);
            if (sext) {
                x86::EmitSarReg32Imm(cursor_, reg, 31u);
                x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(disp) + 4,
                                          reg);
                ClearFlagSource();
            }
            return;
        }
    }
    X86EmitSetSlot(*this, value, static_cast<int32_t>(disp), sext);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSetSlotConst(uint32_t disp, uint32_t value, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (!regs_.Quiet()) {
        X86EmitSetSlot(*this, block_->Const32(value), static_cast<int32_t>(disp), sext);
        regs_.EndOp();
        return;
    }
    x86::EmitMovBaseDisp32Imm32(cursor_, x86::kStateReg, static_cast<int32_t>(disp), value);
    if (sext) {
        x86::EmitMovBaseDisp32Imm32(cursor_, x86::kStateReg, static_cast<int32_t>(disp) + 4,
                                    (value & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u);
    }
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitLabel(uint32_t label) {
    X86EmitLabel(*this, label);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitBranch(Ref cond, uint32_t label, bool if_true) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitBranch(*this, cond, label, if_true);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitBranchSlots(const SlotTest& test, uint32_t label) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitBranchSlots(*this, test, label);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE uint8_t* X86Lowering::EmitGuardBranch(const SlotTest& test) {
    if (!HasRoom()) throw X86ArenaFull{};
    uint8_t* const site = X86EmitGuardBranch(*this, test);
    regs_.EndOp();
    return site;
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSetSlotTest(const SlotTest& test, uint32_t dest) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitSetSlotTest(*this, test, static_cast<int32_t>(dest));
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitGuardBind(uint8_t* site) {
    regs_.Fence();
    ClearFlagSource();
    x86::FixupLabel32(site, cursor_);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitExitDirect(uint32_t target, uint16_t flags) {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitExitDirect(*this, target, flags);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitExitIndirect() {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitExitIndirect(*this);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitExitDispatcher() {
    if (!HasRoom()) throw X86ArenaFull{};
    X86EmitExitDispatcher(*this);
    regs_.EndOp();
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitFusedAlu(Op op, Ref a, Ref b, uint32_t mask,
                                                      uint32_t dest, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    if ((mask == 0u || !sext) && regs_.Quiet()) {
        const uint8_t reg = regs_.FreeScratch();
        if (reg != X86RegAlloc::kNoReg) {
            const X86RegAlloc::Operand o =
                IsConstRef(b)
                    ? X86RegAlloc::Operand{X86RegAlloc::OperandKind::kImm, 0u, 0,
                                           static_cast<uint32_t>(block_->ConstValue(b))}
                    : X86RegAlloc::Operand{X86RegAlloc::OperandKind::kSlot, 0u, SlotRefDisp(b), 0u};
            if (mask == 0u) {
                EmitSlotAluFast(op, reg, static_cast<uint32_t>(SlotRefDisp(a)), o, dest, sext);
                return;
            }
            EmitSlotAluFlags(op, reg, static_cast<uint32_t>(SlotRefDisp(a)), o, mask, dest);
            return;
        }
    }
    X86EmitFusedAlu(*this, op, a, b, mask, static_cast<int32_t>(dest), sext);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotImmFlags(Op op, uint32_t a_disp, uint32_t imm,
                                                          uint32_t mask, uint32_t dest) {
    if (!HasRoom()) throw X86ArenaFull{};
    const uint8_t reg = regs_.Quiet() ? regs_.FreeScratch() : X86RegAlloc::kNoReg;
    if (reg == X86RegAlloc::kNoReg) {
        X86EmitFusedAlu(*this, op, SlotRef(a_disp), block_->Const32(imm), mask,
                        static_cast<int32_t>(dest), false);
        return;
    }
    EmitSlotAluFlags(op, reg, a_disp, X86RegAlloc::Operand{X86RegAlloc::OperandKind::kImm, 0u, 0, imm},
                     mask, dest);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotSlotFlags(Op op, uint32_t a_disp, uint32_t b_disp,
                                                           uint32_t mask, uint32_t dest) {
    if (!HasRoom()) throw X86ArenaFull{};
    const uint8_t reg = regs_.Quiet() ? regs_.FreeScratch() : X86RegAlloc::kNoReg;
    if (reg == X86RegAlloc::kNoReg) {
        X86EmitFusedAlu(*this, op, SlotRef(a_disp), SlotRef(b_disp), mask,
                        static_cast<int32_t>(dest), false);
        return;
    }
    EmitSlotAluFlags(op, reg, a_disp,
                     X86RegAlloc::Operand{X86RegAlloc::OperandKind::kSlot, 0u,
                                          static_cast<int32_t>(b_disp), 0u},
                     mask, dest);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotAluFlags(Op op, uint8_t reg, uint32_t a_disp,
                                                          const X86RegAlloc::Operand& b,
                                                          uint32_t mask, uint32_t dest) {
    X86CheckFusedFlags(op, mask);
    x86::EmitMovRegBaseDisp32(cursor_, reg, x86::kStateReg, static_cast<int32_t>(a_disp));
    X86EmitAluOperand(cursor_, op, reg, b);
    if (dest != kNoDest) {
        x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest), reg);
    }
    EmitFlagSetccs(op, mask);
    ClearFlagSource();
}

CERFENGINE_FORCEINLINE bool X86Lowering::EmitSlotImm(Op op, uint32_t a_disp, uint32_t imm,
                                                     uint32_t dest, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    const uint8_t reg = regs_.Quiet() ? regs_.FreeScratch() : X86RegAlloc::kNoReg;
    if (reg == X86RegAlloc::kNoReg) {
        if (op == Op::kShl || op == Op::kLsr || op == Op::kAsr || op == Op::kRor) return false;
        X86EmitFusedAlu(*this, op, SlotRef(a_disp), block_->Const32(imm), 0u,
                        static_cast<int32_t>(dest), sext);
        return true;
    }
    EmitSlotAluFast(op, reg, a_disp, X86RegAlloc::Operand{X86RegAlloc::OperandKind::kImm, 0u, 0, imm},
                    dest, sext);
    return true;
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotSlot(Op op, uint32_t a_disp, uint32_t b_disp,
                                                      uint32_t dest, bool sext) {
    if (!HasRoom()) throw X86ArenaFull{};
    const uint8_t reg = regs_.Quiet() ? regs_.FreeScratch() : X86RegAlloc::kNoReg;
    if (reg == X86RegAlloc::kNoReg) {
        X86EmitFusedAlu(*this, op, SlotRef(a_disp), SlotRef(b_disp), 0u,
                        static_cast<int32_t>(dest), sext);
        return;
    }
    EmitSlotAluFast(op, reg, a_disp,
                    X86RegAlloc::Operand{X86RegAlloc::OperandKind::kSlot, 0u,
                                         static_cast<int32_t>(b_disp), 0u},
                    dest, sext);
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitFlagSetccs(Op op, uint32_t mask) {
    if (flag_stamp_block_ != block_ || flag_stamp_layout_ != block_->FlagLayout()) {
        BuildFlagStamps(*block_);
    }
    const FlagStamp& s = flag_stamps_[X86FusedCarryCond(op) == kX86CondC ? 1u : 0u][mask & 0x0Fu];
    std::memcpy(cursor_, s.bytes, sizeof(s.bytes));
    cursor_ += s.length;
}

CERFENGINE_FORCEINLINE bool X86Lowering::EmitShiftedSlotAlu(Op op, uint32_t a_disp, uint32_t b_disp,
                                                            Op shift, uint32_t amount, uint32_t mask,
                                                            uint32_t dest) {
    using OperandKind = X86RegAlloc::OperandKind;
    if (!HasRoom()) throw X86ArenaFull{};
    if (!regs_.Quiet()) return false;
    const uint32_t free = regs_.FreeScratchMask();
    if (free == 0u || (op == Op::kSub && (free & (free - 1u)) == 0u)) return false;
    X86CheckFusedFlags(op, mask);
    const uint8_t shifted = static_cast<uint8_t>(LowestSetBit(free));
    x86::EmitMovRegBaseDisp32(cursor_, shifted, x86::kStateReg, static_cast<int32_t>(b_disp));
    X86EmitAluOperand(cursor_, shift, shifted, X86RegAlloc::Operand{OperandKind::kImm, 0u, 0, amount});
    uint8_t result = shifted;
    if (op == Op::kSub) {
        result = static_cast<uint8_t>(LowestSetBit(free & (free - 1u)));
        x86::EmitMovRegBaseDisp32(cursor_, result, x86::kStateReg, static_cast<int32_t>(a_disp));
        X86EmitAluOperand(cursor_, op, result,
                          X86RegAlloc::Operand{OperandKind::kReg, shifted, 0, 0u});
    } else {
        X86EmitAluOperand(cursor_, op, result,
                          X86RegAlloc::Operand{OperandKind::kSlot, 0u, static_cast<int32_t>(a_disp), 0u});
    }
    if (dest != kNoDest) {
        x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest), result);
    }
    EmitFlagSetccs(op, mask);
    ClearFlagSource();
    return true;
}

CERFENGINE_FORCEINLINE void X86Lowering::EmitSlotAluFast(Op op, uint8_t reg, uint32_t a_disp,
                                                         const X86RegAlloc::Operand& b,
                                                         uint32_t dest, bool sext) {
    x86::EmitMovRegBaseDisp32(cursor_, reg, x86::kStateReg, static_cast<int32_t>(a_disp));
    X86EmitAluOperand(cursor_, op, reg, b);
    x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest), reg);
    if (sext) {
        x86::EmitSarReg32Imm(cursor_, reg, 31u);
        x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, static_cast<int32_t>(dest) + 4, reg);
    }
    ClearFlagSource();
}

}
