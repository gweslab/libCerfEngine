#include "a64_lowering.h"

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "../../host_platform.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

bool IsView(Op op) {
    return op == Op::kPair || op == Op::kTruncate;
}

}

const void* A64Lowering::BuildEntryStub(const PinnedSlots& pinned, uint8_t* region) {
    A64PinnedSlots pins;
    pins.Reset(pinned);
    uint8_t scratch[kEntryStubBytes * 2];
    uint8_t* end = scratch;
    pins.EmitLoads(end, pins.Full());
    a64::EmitBr(end, a64::kX0);
    if (static_cast<size_t>(end - scratch) > kEntryStubBytes) {
        throw EmitFault("CerfIR A64 lowering: the block entry stub exceeds its slot");
    }
    const platform::CodeWriteScope write;
    uint8_t* c = region;
    pins.EmitLoads(c, pins.Full());
    a64::EmitBr(c, a64::kX0);
    platform::FlushInstructionCache(region, static_cast<size_t>(c - region));
    return region;
}

uint8_t* A64Lowering::EmitCycleAdd(uint32_t cycles) {
    if (!HasRoom()) throw A64ArenaFull{};
    const int32_t slot = target_->model->cycle_slot;
    pins_.Load(cursor_, kA64Tmp0, slot);
    uint8_t* site = nullptr;
    if (cycles < kCycleAddLimit) {
        site = cursor_;
        a64::EmitAddImm(cursor_, kA64Tmp0, kA64Tmp0, cycles);
    } else {
        a64::EmitMovImm32(cursor_, kA64Tmp1, cycles);
        a64::EmitAddReg(cursor_, kA64Tmp0, kA64Tmp0, kA64Tmp1);
    }
    pins_.Store(cursor_, kA64Tmp0, slot);
    return site;
}

void A64Lowering::Begin(Block& block, uint8_t* code, uint8_t* code_end,
                        const A64GuestTarget& target) {
    if (target.model->pinned.count != 0u && target.entry_stub == nullptr) {
        throw EmitFault("CerfIR A64 lowering: the guest pins slots and has no block entry stub");
    }
    block_      = &block;
    info_       = block.InfoData();
    target_     = &target;
    cursor_     = code;
    code_start_ = code;
    code_end_   = code_end;
    fixups_.clear();
    ClearFlagSource();
    tails_.BeginBlock();
    pins_.Reset(target.model->pinned);
    regs_.Reset(block, info_);
}

size_t A64Lowering::Finish() {
    if (!tails_.Emit(*this)) return 0u;
    if (!fixups_.empty()) throw EmitFault("CerfIR A64 lowering: a branch targets no bound label");
    return static_cast<size_t>(cursor_ - code_start_);
}

void A64Lowering::Lower(Ref ref) {
    if (!HasRoom()) throw A64ArenaFull{};
    regs_.Undefine(ref);
    if (info_[ref].wide != 0u) regs_.Undefine(HiHalf(ref));
    if ((info_[ref].meta & kValueIsValueOp) == 0u) {
        A64LowerFnFor(block_->At(ref).op)(*this, ref);
        regs_.EndOp();
        return;
    }
    LowerValueOp(ref);
}

void A64Lowering::Drop(Ref r) {
    if (IsView(block_->At(r).op)) {
        ConsumeArg(r);
        return;
    }
    regs_.Consume(r);
    if (info_[r].wide != 0u) regs_.Consume(HiHalf(r));
}

void A64Lowering::DropHalf(Ref r, bool hi) {
    const Inst& inst = block_->At(r);
    if (inst.op == Op::kPair) {
        ConsumeArg(inst.args[hi ? 1 : 0]);
        return;
    }
    if (info_[r].wide == 0u) {
        throw EmitFault("CerfIR A64 lowering: half drop of a value that is not 64-bit");
    }
    regs_.Consume(hi ? HiHalf(r) : r);
}

void A64Lowering::LowerValueOp(uint32_t i) {
    const ValueInfo v    = info_[i];
    const Inst&     inst = block_->Insts()[i];
    if (v.uses == 0u && (v.wide == 0u || info_[HiHalf(static_cast<Ref>(i))].uses == 0u)) {
        SkipDead(inst);
        return;
    }
    A64LowerFnFor(inst.op)(*this, static_cast<Ref>(i));
    if (inst.dest != kNoDest) {
        if ((v.meta & kValueDestSext) != 0u) {
            A64StoreSlotSext(*this, static_cast<Ref>(i), inst.dest);
        } else {
            StoreDest(static_cast<Ref>(i), inst.dest);
        }
    }
    regs_.EndOp();
    if ((v.meta & kValueStoresFlags) != 0u) {
        A64StoreProducerFlags(*this, static_cast<Ref>(i));
        regs_.EndOp();
    }
}

void A64Lowering::StoreDest(Ref value, int32_t disp) {
    regs_.BeforeSlotWrite(disp, 4u);
    const A64RegAlloc::Loc& loc = regs_.At(value);
    if (loc.kind == A64RegAlloc::Kind::kReg) {
        pins_.Store(cursor_, loc.reg, disp);
        regs_.NoteStored(value, disp);
        regs_.Consume(value);
        return;
    }
    A64StoreSlot(*this, value, disp);
}

void A64Lowering::SkipDead(const Inst& inst) {
    for (Ref r : inst.args) ConsumeArg(r);
}

void A64Lowering::ConsumeArg(Ref r) {
    if (IsValueRef(r) && IsView(block_->At(r).op)) {
        const Inst& view = block_->At(r);
        ConsumeArg(view.args[0]);
        if (view.op == Op::kPair) ConsumeArg(view.args[1]);
        return;
    }
    regs_.Consume(r);
}

A64WideHalf A64Lowering::Half(Ref value, bool hi) const {
    const Block& b = *block_;
    if (IsConstRef(value)) {
        const uint64_t x = b.ConstValue(value);
        return A64WideHalf{kNoRef, true, static_cast<uint32_t>(hi ? (x >> 32) : x)};
    }
    const Inst& inst = b.At(value);
    if (inst.op == Op::kPair) {
        const Ref part = inst.args[hi ? 1 : 0];
        if (IsConstRef(part)) {
            return A64WideHalf{kNoRef, true, static_cast<uint32_t>(b.ConstValue(part))};
        }
        return A64WideHalf{part, false, 0u};
    }
    return A64WideHalf{hi ? HiHalf(value) : value, false, 0u};
}

}
