#pragma once

#include <cstdint>

#include "cerfir_block.h"
#include "cerfir_build_class.h"
#include "cerfir_host_lowering.h"
#include "cerfir_op.h"
#include "cerfir_types.h"

namespace cerfengine::cerfir {

class BuildCore {
public:
    BuildCore(Block& block, HostLowering& lowering, bool record)
        : block_(block), lowering_(lowering), epochs_(block.Epochs()), record_(record) {
        epochs_.RebaseIfNear();
    }

    Block& GetBlock() { return block_; }
    bool   LastWasTerminal() const { return last_terminal_; }

    CERFENGINE_FORCEINLINE Ref ReadSlot(uint32_t disp) {
        if (disp >= kSlotRefBytes || (disp & 3u) != 0u) SlotRefFault(disp);
        if (record_) epochs_.read[disp >> 2] = epochs_.Tick();
        return SlotRef(disp);
    }
    CERFENGINE_FORCEINLINE Ref GetSlot(Type type, uint32_t offset) {
        return Emit(Op::kGetSlot, type, kNoRef, kNoRef, kNoRef, offset, 0u);
    }
    Ref Snapshot(Ref r);

    CERFENGINE_FORCEINLINE void Dup(Ref r) {
        if (!IsValueRef(r)) return;
        if ((block_.Info(r).meta & kValueSkip) != 0u) {
            DupView(r);
            return;
        }
        ++block_.Info(r).uses;
        if (block_.Info(r).wide != 0u) ++block_.Info(Block::HiHalf(r)).uses;
    }
    CERFENGINE_FORCEINLINE void Drop(Ref r) {
        if (IsValueRef(r)) lowering_.Drop(r);
    }
    CERFENGINE_FORCEINLINE void DropHalf(Ref r, bool hi) {
        if (IsValueRef(r)) lowering_.DropHalf(r, hi);
    }
    void Share(Ref r, uint32_t uses);

    CERFENGINE_FORCEINLINE Ref Const(Type type, uint64_t value) { return block_.Const(type, value); }
    CERFENGINE_FORCEINLINE Ref Const32(uint32_t value) { return block_.Const32(value); }
    CERFENGINE_FORCEINLINE Ref Const1(bool value) {
        return block_.Const(Type::kI1, value ? 1u : 0u);
    }
    CERFENGINE_FORCEINLINE Ref ConstPtr(const void* p) { return block_.ConstPtr(p); }
    CERFENGINE_FORCEINLINE Ref ConstArg(const CallArg& a) { return block_.ConstArg(a); }

    CERFENGINE_FORCEINLINE void CycleAdd(uint32_t cycles) {
        if (cycle_site_ != nullptr && cycle_total_ + cycles < HostLowering::kCycleAddLimit) {
            cycle_total_ += cycles;
            HostLowering::PatchCycleAdd(cycle_site_, cycle_total_);
            return;
        }
        if (cycles == 0u) return;
        cycle_total_ = cycles;
        cycle_site_  = lowering_.EmitCycleAdd(cycles);
    }

protected:
    [[noreturn]] static void Reject(const char* what, Op op);

    CERFENGINE_FORCEINLINE void Record(Op op, Type type, Ref a, Ref b, uint32_t aux,
                                       uint16_t aux16) {
        if (record_) block_.Append(op, type, a, b, kNoRef, aux16, aux, 0u, 0u);
    }

    CERFENGINE_FORCEINLINE void NoteSlotWrite(int32_t disp, uint32_t bytes) {
        if (record_) epochs_.NoteWrite(disp, bytes);
    }
    CERFENGINE_FORCEINLINE void NoteFlagWrites(uint8_t store) {
        if (!record_) return;
        const int32_t g = block_.FlagGranule();
        if (g >= 0) {
            epochs_.write[g] = epochs_.Tick();
            return;
        }
        NoteFlagWritesSlow(store);
    }
    CERFENGINE_FORCEINLINE void NoteMemorySlots(const MemAttr& attr) {
        NoteSlotWrite(attr.result_slot, 4u);
        NoteSlotWrite(attr.result_slot2, 4u);
        NoteSlotWrite(attr.writeback_slot, 4u);
    }

    CERFENGINE_FORCEINLINE void CheckSlotHazard(Op op, Ref r) const {
        if (record_ && epochs_.WrittenSinceRead(static_cast<uint32_t>(SlotRefDisp(r)) >> 2)) {
            SlotHazard(op);
        }
    }
    CERFENGINE_FORCEINLINE Ref ResolveSlotArg(Op op, Ref r, uint32_t index) {
        CheckSlotHazard(op, r);
        if (((kSlotArgMask[static_cast<size_t>(op)] >> index) & 1u) != 0u) return r;
        return SubstituteSlotArg(r);
    }
    CERFENGINE_FORCEINLINE void CheckViewParts(Ref r) {
        if (!IsValueRef(r) || (block_.Info(r).meta & kValueSkip) == 0u) return;
        const Inst& view = block_.At(r);
        if (IsSlotRef(view.args[0])) CheckSlotHazard(view.op, view.args[0]);
        if (view.op == Op::kPair && IsSlotRef(view.args[1])) CheckSlotHazard(view.op, view.args[1]);
    }

    CERFENGINE_FORCEINLINE Ref Emit(Op op, Type type, Ref a, Ref b, Ref c, uint32_t aux,
                                    uint16_t aux16) {
        return Build(op, type, a, b, c, aux, aux16, kNoDest, 0u, type != Type::kVoid ? 1u : 0u);
    }

    CERFENGINE_FORCEINLINE Ref Build(Op op, Type type, Ref a, Ref b, Ref c, uint32_t aux,
                                     uint16_t aux16, uint16_t dest, uint8_t meta, uint16_t uses) {
        if (IsSlotRef(a)) a = ResolveSlotArg(op, a, 0u);
        if (IsSlotRef(b)) b = ResolveSlotArg(op, b, 1u);
        if (IsSlotRef(c)) c = ResolveSlotArg(op, c, 2u);
        CheckViewParts(a);
        CheckViewParts(b);
        CheckViewParts(c);
        const uint8_t cls  = kBuildClass[static_cast<size_t>(op)];
        const bool    view = op == Op::kPair || op == Op::kTruncate;
        if ((cls & kBuildEndsCycleRun) != 0u) cycle_site_ = nullptr;
        last_terminal_ = (cls & kBuildTerminal) != 0u;
        meta |= static_cast<uint8_t>(((cls & kBuildIsValueOp) != 0u ? kValueIsValueOp : 0u) |
                                     (view ? kValueSkip : 0u));
        const Ref r = block_.Append(op, type, a, b, c, aux16, aux, meta, uses);
        if (dest != kNoDest) block_.At(r).dest = dest;
        if ((cls & kBuildWritesAllSlots) != 0u && record_) epochs_.all_write = epochs_.Tick();
        if (view) {
            lowering_.Undefine(r);
            return r;
        }
        if (type == Type::kI64) {
            block_.Info(r).wide = 1u;
            block_.Append(Op::kHi32, Type::kI32, r, kNoRef, kNoRef, 0u, 0u, kValueSkip,
                          static_cast<uint16_t>(uses - kFlagCount[(aux16 >> 4) & 0x0Fu]));
        }
        lowering_.Lower(r);
        return r;
    }

    Block&        block_;
    HostLowering& lowering_;
    SlotEpochs&   epochs_;
    uint8_t*      cycle_site_    = nullptr;
    uint32_t      cycle_total_   = 0;
    bool          record_        = false;
    bool          last_terminal_ = false;

private:
    void DupView(Ref view);
    Ref  SubstituteSlotArg(Ref r);
    void NoteFlagWritesSlow(uint8_t store);
    [[noreturn]] static void SlotHazard(Op op);
    [[noreturn]] static void SlotRefFault(uint32_t disp);
};

}
