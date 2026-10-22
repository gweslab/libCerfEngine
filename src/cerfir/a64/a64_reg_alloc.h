#pragma once

#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../lowest_set_bit.h"
#include "../cerfir_block.h"
#include "a64_host_abi.h"
#include "a64_pinned_slots.h"

namespace cerfengine::cerfir {

constexpr uint32_t A64RegBit(uint8_t reg) { return 1u << reg; }

constexpr uint32_t kA64ScratchRegs = A64RegBit(a64::kX0) | A64RegBit(a64::kX1) | A64RegBit(a64::kX2) |
                                     A64RegBit(a64::kX12) | A64RegBit(a64::kX13);
constexpr uint32_t kA64LookupClobberRegs =
    A64RegBit(a64::kX0) | A64RegBit(a64::kX2) | A64RegBit(a64::kX12) | A64RegBit(a64::kX13);
constexpr uint32_t kA64SavedRegs   = A64RegBit(a64::kX19) | A64RegBit(a64::kX20) | A64RegBit(a64::kX21) |
                                   A64RegBit(a64::kX22) | A64RegBit(a64::kX23) | A64RegBit(a64::kX24) |
                                   A64RegBit(a64::kX25) | A64RegBit(a64::kX26);
constexpr uint32_t kA64AllocRegs   = kA64ScratchRegs | kA64SavedRegs;
constexpr uint32_t kA64RegCount    = 32;

CERFENGINE_FORCEINLINE uint32_t A64SlotBytes(Type t) {
    return t == Type::kI1 ? 1u : TypeBits(t) / 8u;
}

class A64RegAlloc {
public:
    enum class Kind : uint8_t { kNone, kReg, kFlags, kSlot, kSpill };

    static constexpr int32_t  kNoHome     = INT32_MIN;
    static constexpr uint32_t kSpillSlots = 32;
    static constexpr uint8_t  kNoReg      = 0xFFu;

    struct Loc {
        Kind    kind = Kind::kNone;
        uint8_t reg  = 0;
        uint8_t cond = 0;
        int32_t disp = 0;
        int32_t home = kNoHome;
    };

    struct Operand {
        bool     imm   = false;
        uint8_t  reg   = 0;
        uint32_t value = 0;
    };

    A64RegAlloc(uint8_t*& cursor, A64PinnedSlots& pins)
        : c_(cursor), pins_(pins), loc_(kMaxInsts), lazy_(kMaxInsts), flag_values_(kMaxInsts) {}

    CERFENGINE_FORCEINLINE void Reset(const Block& block, ValueInfo* info) {
        block_      = &block;
        info_       = info;
        lazy_count_ = 0u;
        flag_count_ = 0u;
        static_assert(kNoRef == 0xFFFFu, "every byte of kNoRef is 0xFF");
        std::memset(owner_, 0xFF, sizeof(owner_));
        owned_      = 0u;
        homed_      = 0u;
        locked_     = 0u;
        spill_used_ = 0u;
        op_temps_   = 0u;
        saved_regs_ = kA64SavedRegs & ~pins_.HostMask();
        alloc_regs_ = kA64ScratchRegs | saved_regs_;
    }
    CERFENGINE_FORCEINLINE uint32_t SavedRegs() const { return saved_regs_; }
    CERFENGINE_FORCEINLINE const Loc& At(Ref r) const { return loc_[r]; }
    CERFENGINE_FORCEINLINE uint16_t   Remaining(Ref r) const { return info_[r].uses; }
    CERFENGINE_FORCEINLINE void       Undefine(Ref r) { loc_[r].kind = Kind::kNone; }

    CERFENGINE_FORCEINLINE void DefineSlot(Ref r, int32_t disp) {
        loc_[r] = Loc{Kind::kSlot, 0u, 0u, disp, disp};
        if (info_[r].uses != 0u) lazy_[lazy_count_++] = r;
    }

    CERFENGINE_FORCEINLINE void DefineFlags(Ref r, uint8_t cond) {
        loc_[r] = Loc{Kind::kFlags, 0u, cond, 0, kNoHome};
        if (info_[r].uses != 0u) flag_values_[flag_count_++] = r;
    }

    CERFENGINE_FORCEINLINE void DefineInReg(Ref r, uint8_t reg) {
        SetOwner(reg, r);
        loc_[r] = Loc{Kind::kReg, reg, 0u, 0, kNoHome};
        if (info_[r].uses == 0u) Forget(r);
    }

    CERFENGINE_FORCEINLINE uint8_t Materialize(Ref r, uint32_t allowed = kA64AllocRegs) {
        if (IsValueRef(r)) {
            const Loc& l = loc_[r];
            if (l.kind == Kind::kReg && (allowed & A64RegBit(l.reg)) != 0u) {
                Lock(l.reg);
                return l.reg;
            }
        }
        return MaterializeSlow(r, allowed);
    }

    CERFENGINE_FORCEINLINE void Consume(Ref r) {
        if (!IsValueRef(r)) return;
        uint16_t& uses = info_[r].uses;
        if (uses == 0u) ConsumeFault(r);
        if (--uses == 0u) Forget(r);
    }

    uint8_t CopyIntoFixed(Ref src, uint8_t reg);
    uint8_t AllocTemp(uint32_t allowed = kA64AllocRegs);
    void    CopyOut(Ref src, uint8_t reg);

    CERFENGINE_FORCEINLINE Operand Imm(uint32_t value) const { return Operand{true, 0u, value}; }
    CERFENGINE_FORCEINLINE Operand Reg(uint8_t reg) const { return Operand{false, reg, 0u}; }

    CERFENGINE_FORCEINLINE uint32_t ConstOf(Ref r) const {
        return static_cast<uint32_t>(block_->ConstValue(r));
    }

    CERFENGINE_FORCEINLINE bool Reusable(uint8_t reg) const {
        return owner_[reg] == kNoRef ||
               (owner_[reg] == kTempOwner && (op_temps_ & A64RegBit(reg)) != 0u);
    }

    CERFENGINE_FORCEINLINE uint8_t DestReg(uint8_t prefer_a, uint8_t prefer_b = kNoReg,
                                           uint32_t avoid = 0u) {
        const uint32_t ok = alloc_regs_ & ~avoid;
        if (prefer_a != kNoReg && (ok & A64RegBit(prefer_a)) != 0u && Reusable(prefer_a)) {
            return ClaimFree(prefer_a);
        }
        if (prefer_b != kNoReg && (ok & A64RegBit(prefer_b)) != 0u && Reusable(prefer_b)) {
            return ClaimFree(prefer_b);
        }
        return AllocTemp(ok);
    }

    CERFENGINE_FORCEINLINE uint8_t ClaimFixed(uint8_t reg) {
        if ((locked_ & A64RegBit(reg)) != 0u) FixedRegisterBusy(reg);
        Evict(A64RegBit(reg));
        SetOwner(reg, kTempOwner);
        Lock(reg);
        return reg;
    }

    CERFENGINE_FORCEINLINE void ReleaseTemp(uint8_t reg) {
        if (owner_[reg] == kTempOwner) SetOwner(reg, kNoRef);
    }
    CERFENGINE_FORCEINLINE void Lock(uint8_t reg) { locked_ |= A64RegBit(reg); }
    CERFENGINE_FORCEINLINE void UnlockAll() { locked_ = 0u; }
    CERFENGINE_FORCEINLINE void EndOp() {
        if (op_temps_ != 0u) ReleaseOpTemps();
        locked_ = 0u;
    }

    CERFENGINE_FORCEINLINE bool Quiet() const { return (homed_ | lazy_count_ | flag_count_) == 0u; }
    CERFENGINE_FORCEINLINE bool Idle() const {
        return (owned_ | homed_ | lazy_count_ | flag_count_) == 0u;
    }

    CERFENGINE_FORCEINLINE void Evict(uint32_t regs) {
        if ((regs & owned_) != 0u) EvictSlow(regs);
    }

    CERFENGINE_FORCEINLINE void BeforeFlagsClobber() {
        if (flag_count_ != 0u) MaterializeFlags();
    }
    void BeforeFlagsClobberExcept(Ref keep);
    CERFENGINE_FORCEINLINE void BeforeSlotWrite(int32_t disp, uint32_t size) {
        ClearHomes(disp, size);
        if (lazy_count_ != 0u) MaterializeLazySlots(disp, size);
    }

    Loc PreserveAcrossCalls(Ref r);

    static constexpr int32_t SpillOffset(uint32_t index) {
        return kA64SpillBase + static_cast<int32_t>(index * kA64SpillSlotSize);
    }

    CERFENGINE_FORCEINLINE void NoteStored(Ref value, int32_t disp) {
        if (!IsValueRef(value) || info_[value].uses == 0u) return;
        Loc& l = loc_[value];
        if (l.kind != Kind::kReg) return;
        l.home = disp;
        homed_ |= A64RegBit(l.reg);
    }
    CERFENGINE_FORCEINLINE void BeforeStateWrite() {
        for (uint32_t m = homed_; m != 0u; m &= m - 1u) loc_[owner_[LowestSetBit(m)]].home = kNoHome;
        homed_ = 0u;
        while (lazy_count_ != 0u) MaterializeHomeless(lazy_[lazy_count_ - 1u]);
    }
    CERFENGINE_FORCEINLINE void BeforeCall() {
        BeforeFlagsClobber();
        BeforeStateWrite();
        Evict(kA64ScratchRegs);
    }
    CERFENGINE_FORCEINLINE void Fence() {
        if ((owned_ | lazy_count_ | flag_count_) != 0u) FenceFault();
        spill_used_ = 0u;
        locked_     = 0u;
    }

private:
    static constexpr Ref kTempOwner = 0xFFFEu;

    CERFENGINE_FORCEINLINE uint8_t ClaimFree(uint8_t reg) {
        SetOwner(reg, kTempOwner);
        op_temps_ &= ~A64RegBit(reg);
        Lock(reg);
        return reg;
    }

    CERFENGINE_FORCEINLINE void SetOwner(uint8_t reg, Ref r) {
        owner_[reg] = r;
        homed_ &= ~A64RegBit(reg);
        if (r == kNoRef) {
            owned_ &= ~A64RegBit(reg);
        } else {
            owned_ |= A64RegBit(reg);
        }
    }

    CERFENGINE_FORCEINLINE void Forget(Ref r) {
        Loc& l = loc_[r];
        if (l.kind == Kind::kReg) {
            if (owner_[l.reg] == r) SetOwner(l.reg, kNoRef);
            l.kind = Kind::kNone;
            return;
        }
        if (l.kind != Kind::kNone) ForgetSlow(r);
    }

    CERFENGINE_FORCEINLINE void ClearHomes(int32_t disp, uint32_t size) {
        for (uint32_t m = homed_; m != 0u; m &= m - 1u) {
            const uint32_t reg   = LowestSetBit(m);
            const Ref      r     = owner_[reg];
            const int32_t  start = loc_[r].home;
            const int32_t  bytes = static_cast<int32_t>(A64SlotBytes(block_->At(r).type));
            if (start < disp + static_cast<int32_t>(size) && disp < start + bytes) {
                loc_[r].home = kNoHome;
                homed_ &= ~A64RegBit(static_cast<uint8_t>(reg));
            }
        }
    }

    CERFENGINE_FORCEINLINE uint8_t PickReg(uint32_t allowed) {
        const uint32_t free = allowed & ~locked_ & FreeMask();
        if (free != 0u) {
            if ((free & kA64ScratchRegs) != 0u) {
                return static_cast<uint8_t>(LowestSetBit(free & kA64ScratchRegs));
            }
            return static_cast<uint8_t>(LowestSetBit(free));
        }
        return PickRegByEviction(allowed);
    }

    CERFENGINE_FORCEINLINE uint32_t FreeMask() const { return alloc_regs_ & ~owned_; }

    [[noreturn]] void FixedRegisterBusy(uint8_t reg);
    [[noreturn]] void FenceFault();
    [[noreturn]] void ConsumeFault(Ref r);
    void    ReleaseOpTemps();
    void    EmitSlotLoad(uint8_t reg, int32_t disp, uint32_t bytes);
    uint8_t LoadOperand(Ref r, uint32_t allowed);
    void    EvictSlow(uint32_t regs);
    void    MaterializeLazySlots(int32_t disp, uint32_t size);
    void    MaterializeHomeless(Ref r);
    uint8_t PickRegByEviction(uint32_t allowed);
    uint8_t MaterializeSlow(Ref r, uint32_t allowed);
    void    MaterializeFlags();
    void    SaveFlag(Ref r);
    void    ForgetSlow(Ref r);
    void    SpillOwner(uint8_t reg);
    uint8_t LoadInto(Ref r, uint8_t reg);
    void    CopyValue(Ref src, uint8_t reg);
    uint32_t NextSpill();

    uint8_t*&        c_;
    A64PinnedSlots&  pins_;
    const Block*     block_ = nullptr;
    std::vector<Loc> loc_;
    ValueInfo*       info_ = nullptr;
    std::vector<Ref> lazy_;
    std::vector<Ref> flag_values_;
    uint32_t         lazy_count_ = 0;
    uint32_t         flag_count_ = 0;
    Ref              owner_[kA64RegCount]{};
    uint32_t         owned_      = 0;
    uint32_t         homed_      = 0;
    uint32_t         locked_     = 0;
    uint32_t         spill_used_ = 0;
    uint32_t         op_temps_   = 0;
    uint32_t         saved_regs_ = kA64SavedRegs;
    uint32_t         alloc_regs_ = kA64AllocRegs;
};

}
