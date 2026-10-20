#include "x86_lowering.h"

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

bool IsView(Op op) {
    return op == Op::kPair || op == Op::kTruncate;
}

}

uint8_t* X86Lowering::EmitCycleAddSlow(uint32_t cycles) {
    if (!HasRoom()) throw X86ArenaFull{};
    regs_.BeforeFlagsClobber();
    ClearFlagSource();
    if (cycles < 0x80u) {
        uint8_t* const p = cursor_;
        std::memcpy(p, cycle_add_, sizeof(cycle_add_));
        p[kCycleAddImmOffset] = static_cast<uint8_t>(cycles);
        cursor_ = p + kCycleAddBytes;
        return p + kCycleAddImmOffset;
    }
    x86::EmitAddBaseDisp32Imm32(cursor_, x86::kStateReg, target_->model->cycle_slot, cycles);
    return nullptr;
}

void X86Lowering::PrepareCycleAdd(int32_t disp) {
    cycle_add_disp_ = disp;
    uint8_t* p      = cycle_add_;
    x86::EmitAddBaseDisp32Imm8(p, x86::kStateReg, disp, 0u);
    if (p != cycle_add_ + kCycleAddBytes) {
        throw EmitFault("CerfIR x86 lowering: the cycle add encoding is not 7 bytes");
    }
}

void X86Lowering::Begin(Block& block, uint8_t* code, uint8_t* code_end,
                        const X86GuestTarget& target) {
    block_      = &block;
    info_       = block.InfoData();
    target_     = &target;
    cursor_     = code;
    code_start_ = code;
    code_end_   = code_end;
    fixups_.clear();
    ClearFlagSource();
    if (target.model->cycle_slot != cycle_add_disp_) PrepareCycleAdd(target.model->cycle_slot);
    tails_.BeginBlock();
    regs_.Reset(block, info_);
}

void X86Lowering::BuildFlagStamps(const Block& block) {
    if (!block.FlagSlotsOf().set) FlagStampFault();
    const int32_t* disp = block.FlagSlotsOf().disp;
    for (uint32_t carry = 0; carry < 2u; ++carry) {
        const uint8_t cond[4] = {kX86CondS, kX86CondZ, carry != 0u ? kX86CondC : kX86CondNc,
                                 kX86CondO};
        for (uint32_t mask = 0; mask < 16u; ++mask) {
            FlagStamp& s = flag_stamps_[carry][mask];
            uint8_t*   p = s.bytes;
            for (uint32_t i = 0; i < 4u; ++i) {
                if ((mask & (1u << i)) == 0u) continue;
                x86::EmitSetccBaseDisp32(p, static_cast<uint8_t>(x86::kSetO + cond[i]),
                                         x86::kStateReg, disp[i]);
            }
            s.length = static_cast<uint32_t>(p - s.bytes);
        }
    }
    flag_stamp_block_  = &block;
    flag_stamp_layout_ = block.FlagLayout();
}

void X86Lowering::FlagStampFault() {
    throw EmitFault("CerfIR x86 lowering: a flag store in a block without flag slots");
}

size_t X86Lowering::Finish() {
    if (!tails_.Emit(*this)) return 0u;
    if (!fixups_.empty()) throw EmitFault("CerfIR x86 lowering: a branch targets no bound label");
    return static_cast<size_t>(cursor_ - code_start_);
}

void X86Lowering::Lower(Ref ref) {
    if (!HasRoom()) throw X86ArenaFull{};
    regs_.Undefine(ref);
    if (info_[ref].wide != 0u) regs_.Undefine(HiHalf(ref));
    if ((info_[ref].meta & kValueIsValueOp) == 0u) {
        X86LowerFnFor(block_->At(ref).op)(*this, ref);
        regs_.EndOp();
        return;
    }
    LowerValueOp(ref);
}

void X86Lowering::Drop(Ref r) {
    if (IsView(block_->At(r).op)) {
        ConsumeArg(r);
        return;
    }
    regs_.Consume(r);
    if (info_[r].wide != 0u) regs_.Consume(HiHalf(r));
}

void X86Lowering::DropHalf(Ref r, bool hi) {
    const Inst& inst = block_->At(r);
    if (inst.op == Op::kPair) {
        ConsumeArg(inst.args[hi ? 1 : 0]);
        return;
    }
    if (info_[r].wide == 0u) throw EmitFault("CerfIR x86 lowering: half drop of a value that is not 64-bit");
    regs_.Consume(hi ? HiHalf(r) : r);
}

CERFENGINE_FORCEINLINE void X86Lowering::LowerValueOp(uint32_t i) {
    const ValueInfo v    = info_[i];
    const Inst&     inst = block_->Insts()[i];
    if (v.uses == 0u && (v.wide == 0u || info_[HiHalf(static_cast<Ref>(i))].uses == 0u)) {
        SkipDead(inst);
        return;
    }
    X86LowerFnFor(inst.op)(*this, static_cast<Ref>(i));
    if (inst.dest != kNoDest) {
        if ((v.meta & kValueDestSext) != 0u) {
            StoreDestSext(static_cast<Ref>(i), inst.dest);
        } else {
            StoreDest(static_cast<Ref>(i), inst.dest);
        }
    }
    regs_.EndOp();
    if ((v.meta & kValueStoresFlags) != 0u) {
        X86StoreProducerFlags(*this, static_cast<Ref>(i));
        regs_.EndOp();
    }
}

void X86Lowering::SkipDead(const Inst& inst) {
    for (Ref r : inst.args) ConsumeArg(r);
}

void X86Lowering::ConsumeArg(Ref r) {
    if (IsValueRef(r) && IsView(block_->At(r).op)) {
        const Inst& view = block_->At(r);
        ConsumeArg(view.args[0]);
        if (view.op == Op::kPair) ConsumeArg(view.args[1]);
        return;
    }
    regs_.Consume(r);
}

void X86Lowering::StoreDestSlow(Ref value, int32_t disp) {
    X86StoreSlot(*this, value, disp);
}

void X86Lowering::StoreDestSext(Ref value, int32_t disp) {
    X86StoreSlotSext(*this, value, disp);
}

X86WideHalf X86Lowering::Half(Ref value, bool hi) const {
    const Block& b = *block_;
    if (IsConstRef(value)) {
        const uint64_t x = b.ConstValue(value);
        return X86WideHalf{kNoRef, true, static_cast<uint32_t>(hi ? (x >> 32) : x)};
    }
    const Inst& inst = b.At(value);
    if (inst.op == Op::kPair) {
        const Ref part = inst.args[hi ? 1 : 0];
        if (IsConstRef(part)) {
            return X86WideHalf{kNoRef, true, static_cast<uint32_t>(b.ConstValue(part))};
        }
        return X86WideHalf{part, false, 0u};
    }
    return X86WideHalf{hi ? HiHalf(value) : value, false, 0u};
}

}
