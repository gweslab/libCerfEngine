#pragma once

#include <array>
#include <cstdint>

#include "cerfir_memory_build.h"

namespace cerfengine::cerfir {

struct FlagSet {
    Ref n = kNoRef;
    Ref z = kNoRef;
    Ref c = kNoRef;
    Ref v = kNoRef;
};

class Builder : public MemoryBuild {
public:
    using MemoryBuild::MemoryBuild;

    CERFENGINE_FORCEINLINE void SetSlot(uint32_t offset, Ref value) {
        if (IsSlotRef(value)) CheckSlotHazard(Op::kSetSlot, value);
        last_terminal_ = false;
        Record(Op::kSetSlot, Type::kVoid, value, kNoRef, offset, 0u);
        lowering_.EmitSetSlot(value, offset, false);
        NoteSlotWrite(static_cast<int32_t>(offset), block_.TypeOf(value) == Type::kI1 ? 1u : 4u);
    }

    CERFENGINE_FORCEINLINE void SetSlotSext(uint32_t offset, Ref value) {
        if (IsSlotRef(value)) CheckSlotHazard(Op::kSetSlot, value);
        last_terminal_ = false;
        Record(Op::kSetSlot, Type::kVoid, value, kNoRef, offset, kSetSlotSext);
        lowering_.EmitSetSlot(value, offset, true);
        NoteSlotWrite(static_cast<int32_t>(offset), 8u);
    }

    CERFENGINE_FORCEINLINE void SetSlotConst(uint32_t offset, uint32_t value) {
        if (record_) {
            SetSlot(offset, Const32(value));
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSetSlotConst(offset, value, false);
    }

    CERFENGINE_FORCEINLINE void SetSlotSextConst(uint32_t offset, uint32_t value) {
        if (record_) {
            SetSlotSext(offset, Const32(value));
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSetSlotConst(offset, value, true);
    }

    CERFENGINE_FORCEINLINE Ref Compute(Op op, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags) {
        if (want == 0u) return ComputeInto(op, a, b, c, 0u, kNoDest);
        return ComputeFlags(op, a, b, c, want, flags, true);
    }
    CERFENGINE_FORCEINLINE void ComputeOnlyFlags(Op op, Ref a, Ref b, Ref c, uint8_t want,
                                                 FlagSet* flags) {
        ComputeFlags(op, a, b, c, want, flags, false);
    }
    void ComputeStoreFlags(Op op, Ref a, Ref b, Ref c, uint8_t store);

    template <bool kSext = false>
    CERFENGINE_FORCEINLINE Ref ComputeInto(Op op, Ref a, Ref b, Ref c, uint8_t store, uint32_t dest) {
        if (op == Op::kCopy && store == 0u && a != kNoRef && b == kNoRef && c == kNoRef &&
            dest <= kNoDest) {
            if (dest == kNoDest) return a;
            if constexpr (kSext) {
                SetSlotSext(dest, a);
            } else {
                SetSlot(dest, a);
            }
            return kNoRef;
        }
        const uint8_t fast = kFastBinary[static_cast<size_t>(op)];
        if (fast == 0u || c != kNoRef || b == kNoRef || IsConstRef(a) ||
            block_.TypeOf(a) != Type::kI32 || (store & ~fast & 0x0Fu) != 0u || dest > kNoDest ||
            (store != 0u && !block_.FlagSlotsOf().set) ||
            (kSext && (store != 0u || dest == kNoDest)) ||
            (store == 0u && IsConstRef(b) && IsIdentity(fast, block_.ConstValue(b)))) {
            return ComputeIntoSlow(op, a, b, c, store, dest, kSext);
        }
        if (IsSlotRef(a)) CheckSlotHazard(op, a);
        if (IsSlotRef(b)) CheckSlotHazard(op, b);
        return FastBinary<kSext>(op, a, b, store, dest, fast);
    }
    CERFENGINE_FORCEINLINE void ComputeIntoSext(Op op, Ref a, Ref b, uint32_t dest) {
        ComputeInto<true>(op, a, b, kNoRef, 0u, dest);
    }
    template <bool kSext = false>
    CERFENGINE_FORCEINLINE void ComputeSlotInto(Op op, uint32_t a_disp, Ref b, uint8_t store,
                                                uint32_t dest) {
        const Ref     a    = ReadSlot(a_disp);
        const uint8_t fast = kFastBinary[static_cast<size_t>(op)];
        if (fast == 0u || b == kNoRef || (store & ~fast & 0x0Fu) != 0u || dest > kNoDest ||
            (store != 0u && !block_.FlagSlotsOf().set) ||
            (kSext && (store != 0u || dest == kNoDest)) ||
            (store == 0u && IsConstRef(b) && IsIdentity(fast, block_.ConstValue(b)))) {
            ComputeIntoSlow(op, a, b, kNoRef, store, dest, kSext);
            return;
        }
        if (IsSlotRef(b)) CheckSlotHazard(op, b);
        FastBinary<kSext>(op, a, b, store, dest, fast);
    }
    CERFENGINE_FORCEINLINE void SlotImmInto(Op op, uint32_t a_disp, uint32_t imm, uint32_t dest,
                                            bool sext) {
        const bool identity = op == Op::kAnd ? imm == 0xFFFFFFFFu : imm == 0u;
        if (!record_ && !identity) {
            last_terminal_ = false;
            if (lowering_.EmitSlotImm(op, a_disp, imm, dest, sext)) return;
        }
        if (sext) ComputeSlotInto<true>(op, a_disp, Const32(imm), 0u, dest);
        else      ComputeSlotInto<false>(op, a_disp, Const32(imm), 0u, dest);
    }
    CERFENGINE_FORCEINLINE void ShiftedSlotInto(Op op, uint32_t a_disp, uint32_t b_disp, Op shift,
                                                uint32_t amount, uint8_t store, uint32_t dest) {
        if (!record_ &&
            lowering_.EmitShiftedSlotAlu(op, a_disp, b_disp, shift, amount, store, dest)) {
            last_terminal_ = false;
            return;
        }
        const Ref shifted = ComputeInto(shift, ReadSlot(b_disp), Const32(amount), kNoRef, 0u, kNoDest);
        ComputeSlotInto(op, a_disp, shifted, store, dest);
    }
    CERFENGINE_FORCEINLINE void SlotCopy32(uint32_t dest, uint32_t src, bool sext) {
        if (record_) {
            if (sext) SetSlotSext(dest, ReadSlot(src));
            else      SetSlot(dest, ReadSlot(src));
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSlotCopy32(dest, src, sext);
    }
    CERFENGINE_FORCEINLINE void SlotCopy64(uint32_t dest, uint32_t src) {
        if (record_) {
            SetSlot(dest, ReadSlot(src));
            SetSlot(dest + 4u, ReadSlot(src + 4u));
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSlotCopy64(dest, src);
    }
    CERFENGINE_FORCEINLINE void SlotSlotInto(Op op, uint32_t a_disp, uint32_t b_disp,
                                             uint32_t dest, bool sext) {
        if (record_) {
            if (sext) ComputeSlotInto<true>(op, a_disp, ReadSlot(b_disp), 0u, dest);
            else      ComputeSlotInto<false>(op, a_disp, ReadSlot(b_disp), 0u, dest);
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSlotSlot(op, a_disp, b_disp, dest, sext);
    }
    CERFENGINE_FORCEINLINE void SlotImmFlags(Op op, uint32_t a_disp, uint32_t imm, uint8_t store,
                                             uint32_t dest) {
        if (record_) {
            ComputeSlotInto(op, a_disp, Const32(imm), store, dest);
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSlotImmFlags(op, a_disp, imm, store, dest);
    }
    CERFENGINE_FORCEINLINE void SlotSlotFlags(Op op, uint32_t a_disp, uint32_t b_disp, uint8_t store,
                                              uint32_t dest) {
        if (record_) {
            ComputeSlotInto(op, a_disp, ReadSlot(b_disp), store, dest);
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSlotSlotFlags(op, a_disp, b_disp, store, dest);
    }
    CERFENGINE_FORCEINLINE Ref Pair(Ref lo, Ref hi) {
        if (IsConstRef(lo) && IsConstRef(hi)) {
            return block_.Const(Type::kI64,
                                block_.ConstValue(lo) | (block_.ConstValue(hi) << 32));
        }
        return Emit(Op::kPair, Type::kI64, lo, hi, kNoRef, 0u, 0u);
    }
    CERFENGINE_FORCEINLINE Ref Compare(Op op, Ref a, Ref b) {
        if (op != Op::kCmpEq && op != Op::kCmpNe && op != Op::kCmpLtS && op != Op::kCmpLtU &&
            op != Op::kCmpLeS) {
            NotACompare(op);
        }
        if (IsConstRef(a) && IsConstRef(b)) return Compute(op, a, b, kNoRef, 0u, nullptr);
        return Emit(op, Type::kI1, a, b, kNoRef, 0u, 0u);
    }
    Ref Wide64(Op op, Ref a, Ref b);
    CERFENGINE_FORCEINLINE Ref Lo32(Ref v) { return Half32(Op::kLo32, v); }
    CERFENGINE_FORCEINLINE Ref Hi32(Ref v) { return Half32(Op::kHi32, v); }
    Ref Op1(Op op, Ref a) { return Compute(op, a, kNoRef, kNoRef, 0u, nullptr); }
    Ref Op2(Op op, Ref a, Ref b) { return Compute(op, a, b, kNoRef, 0u, nullptr); }
    Ref Op3(Op op, Ref a, Ref b, Ref c) { return Compute(op, a, b, c, 0u, nullptr); }
    CERFENGINE_FORCEINLINE Ref Convert(Op op, Type to, Ref a) {
        if (op != Op::kZeroExtend && op != Op::kSignExtend && op != Op::kTruncate) NotAConversion(op);
        if (block_.TypeOf(a) == to) return a;
        if (IsConstRef(a)) return ConvertConst(op, to, a);
        return Emit(op, to, a, kNoRef, kNoRef, 0u, 0u);
    }
    Ref  Saturate(Op op, Ref a, Ref b, uint32_t sticky_slot, uint32_t sticky_mask);
    void OrSticky(Ref cond, uint32_t sticky_slot, uint32_t sticky_mask);

    uint32_t NewLabel() { return block_.NewLabel(); }
    CERFENGINE_FORCEINLINE void Label(uint32_t label) {
        cycle_site_    = nullptr;
        last_terminal_ = false;
        Record(Op::kLabel, Type::kVoid, kNoRef, kNoRef, label, 0u);
        lowering_.EmitLabel(label);
    }
    CERFENGINE_FORCEINLINE void Branch(Ref cond, uint32_t label, bool if_true) {
        if (block_.TypeOf(cond) != Type::kI1) BranchNotI1();
        if (IsConstRef(cond) && ((block_.ConstValue(cond) != 0u) != if_true)) {
            Drop(cond);
            return;
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        Record(if_true ? Op::kBranchIf : Op::kBranchIfNot, Type::kVoid, cond, kNoRef, label, 0u);
        lowering_.EmitBranch(cond, label, if_true);
    }
    CERFENGINE_FORCEINLINE void BranchSlots(const SlotTest& test, uint32_t label) {
        cycle_site_    = nullptr;
        last_terminal_ = false;
        if (record_) {
            block_.Append(Op::kBranchSlots, Type::kVoid, kNoRef, kNoRef, kNoRef,
                          static_cast<uint16_t>(block_.AddSlotTest(test)), label, 0u, 0u);
        }
        lowering_.EmitBranchSlots(test, label);
    }

    CERFENGINE_FORCEINLINE void SetSlotTest(const SlotTest& test, uint32_t dest) {
        if (record_) {
            SetSlot(dest, Convert(Op::kZeroExtend, Type::kI32, SlotTestValue(test)));
            return;
        }
        last_terminal_ = false;
        lowering_.EmitSetSlotTest(test, dest);
    }

    struct Guard {
        uint8_t* site;
        uint32_t label;
    };
    CERFENGINE_FORCEINLINE Guard GuardBranch(const SlotTest& test) {
        if (record_) {
            const uint32_t label = NewLabel();
            BranchSlots(test, label);
            return Guard{nullptr, label};
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        return Guard{lowering_.EmitGuardBranch(test), 0u};
    }
    CERFENGINE_FORCEINLINE void GuardBind(const Guard& guard) {
        if (record_) {
            Label(guard.label);
            return;
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        lowering_.EmitGuardBind(guard.site);
    }

    Ref  Call(const CallDesc& call, Type result, Ref a, Ref b, Ref c);
    CERFENGINE_FORCEINLINE Ref Call(const CallSpec& spec, Ref a, Ref b, Ref c) {
        const uint8_t num_args = c != kNoRef ? 3u : (b != kNoRef ? 2u : (a != kNoRef ? 1u : 0u));
        if ((num_args >= 2u && a == kNoRef) || (num_args == 3u && b == kNoRef) ||
            (spec.store_pc && block_.PcSlot() == kNoSlot)) {
            CallShapeFault();
        }
        if (DirectCallShape(spec.result, a, b, c)) {
            DirectCall(spec.fn, spec.conv, num_args, a, b, c, nullptr, 0u, spec.store_pc, spec.pc);
            return kNoRef;
        }
        CallDesc& d = block_.NewCall(spec.fn, spec.conv, num_args, 0u);
        if (spec.store_pc) {
            d.store_pc = true;
            d.pc       = spec.pc;
            NoteSlotWrite(block_.PcSlot(), 4u);
        }
        return Emit(Op::kCallHelper, spec.result, a, b, c, block_.CallCount() - 1u, 0u);
    }
    CERFENGINE_FORCEINLINE void CallConst(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                          CallArg a1, CallArg a2, CallArg a3) {
        if (spec.result != Type::kVoid || num_args > kMaxConstCallArgs ||
            (spec.store_pc && block_.PcSlot() == kNoSlot) || AnyHostWord(a0, a1, a2, a3)) {
            ConstCallFault();
        }
        if (record_) {
            CallConstRecorded(spec, num_args, a0, a1, a2, a3);
            return;
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        lowering_.EmitCallConst(spec, num_args, a0, a1, a2, a3);
    }
    Ref  HostLoad(const void* address, uint8_t width, bool sign_extend);
    void HostStore(const void* address, Ref value, uint8_t width);
    Ref  HostTest(const void* address, uint32_t mask);
    Ref  HostReplace(const void* address, Ref value);

    CERFENGINE_FORCEINLINE void ExitDirect(uint32_t target_pc, uint16_t chain_slot, bool store_pc,
                                           bool probe = false) {
        if ((chain_slot & ~kExitSlotMask) != 0u || (store_pc && block_.PcSlot() == kNoSlot)) {
            ExitDirectFault(chain_slot);
        }
        const uint16_t flags = static_cast<uint16_t>(chain_slot | (store_pc ? kExitStorePc : 0u) |
                                                     (probe ? kExitProbe : 0u));
        cycle_site_    = nullptr;
        last_terminal_ = true;
        Record(Op::kExitDirect, Type::kVoid, kNoRef, kNoRef, target_pc, flags);
        lowering_.EmitExitDirect(target_pc, flags);
    }
    CERFENGINE_FORCEINLINE void ExitIndirect() {
        cycle_site_    = nullptr;
        last_terminal_ = true;
        Record(Op::kExitIndirect, Type::kVoid, kNoRef, kNoRef, 0u, 0u);
        lowering_.EmitExitIndirect();
    }
    CERFENGINE_FORCEINLINE void ExitDispatcher() {
        cycle_site_    = nullptr;
        last_terminal_ = true;
        Record(Op::kExitDispatcher, Type::kVoid, kNoRef, kNoRef, 0u, 0u);
        lowering_.EmitExitDispatcher();
    }

private:
    CERFENGINE_FORCEINLINE bool DirectCallShape(Type result, Ref a, Ref b, Ref c) const {
        return !record_ && result == Type::kVoid && !IsValueRef(a) && !IsValueRef(b) &&
               !IsValueRef(c);
    }
    CERFENGINE_FORCEINLINE void DirectCall(const void* fn, CallConv conv, uint32_t num_args, Ref a,
                                           Ref b, Ref c, const CallArg* extra,
                                           uint32_t num_extra, bool store_pc, uint32_t pc) {
        cycle_site_    = nullptr;
        last_terminal_ = false;
        lowering_.EmitCallDirect(fn, conv, num_args, a, b, c, extra, num_extra, store_pc, pc);
    }

    static CERFENGINE_FORCEINLINE bool IsIdentity(uint8_t fast, uint64_t value) {
        if ((fast & kFastIdentityZero) != 0u) return value == 0u;
        if ((fast & kFastIdentityAllOne) != 0u) return value == 0xFFFFFFFFu;
        return false;
    }

    template <bool kSext>
    CERFENGINE_FORCEINLINE Ref FastBinary(Op op, Ref a, Ref b, uint8_t store, uint32_t dest,
                                          uint8_t fast) {
        const bool    fused = (fast & kFastFusable) != 0u && (dest != kNoDest || store != 0u) &&
                              IsSlotRef(a) && (IsConstRef(b) || IsSlotRef(b));
        const uint8_t meta  = static_cast<uint8_t>((store != 0u ? kValueStoresFlags : 0u) |
                                                   (kSext ? kValueDestSext : 0u));
        if (fused) {
            last_terminal_ = false;
            if (record_) {
                const Ref rec = block_.Append(op, Type::kI32, a, b, kNoRef, store, 0u,
                                              static_cast<uint8_t>(meta | kValueIsValueOp), 0u);
                block_.At(rec).dest = static_cast<uint16_t>(dest);
            }
            lowering_.EmitFusedAlu(op, a, b, store, dest, kSext);
            if (dest != kNoDest) NoteSlotWrite(static_cast<int32_t>(dest), kSext ? 8u : 4u);
            if (store != 0u) NoteFlagWrites(store);
            return kNoRef;
        }
        const uint16_t uses = dest == kNoDest && store == 0u
                                  ? 1u
                                  : static_cast<uint16_t>((dest != kNoDest ? 1u : 0u) +
                                                          (store != 0u ? 1u : 0u));
        const Ref r = Build(op, Type::kI32, a, b, kNoRef, 0u, store,
                            static_cast<uint16_t>(dest), meta, uses);
        if (dest != kNoDest) NoteSlotWrite(static_cast<int32_t>(dest), kSext ? 8u : 4u);
        if (store != 0u) NoteFlagWrites(store);
        return dest == kNoDest && store == 0u ? r : kNoRef;
    }

    Ref  ComputeFlags(Op op, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags, bool keep_value);
    Ref  ComputeIntoSlow(Op op, Ref a, Ref b, Ref c, uint8_t store, uint32_t dest, bool sext);
    Ref  ConvertConst(Op op, Type to, Ref a);
    [[noreturn]] static void NotAConversion(Op op);
    [[noreturn]] static void BranchNotI1();
    [[noreturn]] static void NotACompare(Op op);
    [[noreturn]] static void NotAWide64Op(Op op);
    CERFENGINE_FORCEINLINE Ref Half32(Op op, Ref v) {
        if (IsConstRef(v)) return Compute(op, v, kNoRef, kNoRef, 0u, nullptr);
        if (IsValueRef(v) && block_.At(v).op == Op::kPair) {
            const Inst& pair = block_.At(v);
            Drop(pair.args[op == Op::kHi32 ? 0 : 1]);
            return pair.args[op == Op::kHi32 ? 1 : 0];
        }
        return Emit(op, Type::kI32, v, kNoRef, kNoRef, 0u, 0u);
    }
    [[noreturn]] static void CallShapeFault();
    [[noreturn]] static void ConstCallFault();
    CERFENGINE_NOINLINE void CallConstRecorded(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                               CallArg a1, CallArg a2, CallArg a3);
    Ref SlotTestValue(const SlotTest& test);
    [[noreturn]] static void ExitDirectFault(uint16_t chain_slot);
    Ref  Prepare(Op op, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags, Type* type_out);
    Type ResultType(Op op, Ref a, Ref b) const;
    bool AllConst(Ref a, Ref b, Ref c) const;
    Ref  Simplify(Op op, Ref a, Ref b) const;
    Ref  FoldWithFlags(Op op, Type type, Ref a, Ref b, Ref c, uint8_t want, FlagSet* flags);
};

}
