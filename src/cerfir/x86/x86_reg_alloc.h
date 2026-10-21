#pragma once

#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

#include <cerfengine/force_inline.h>
#include "../../lowest_set_bit.h"
#include "../../x86_emit.h"
#include "../cerfir_block.h"

namespace cerfengine::cerfir {

constexpr uint32_t RegBit(uint8_t reg) { return 1u << reg; }

constexpr uint32_t kX86ScratchRegs = RegBit(x86::kEax) | RegBit(x86::kEcx) | RegBit(x86::kEdx);
constexpr uint32_t kX86SavedRegs   = RegBit(x86::kSavedRegA) | RegBit(x86::kSavedRegB);
constexpr uint32_t kX86AllocRegs   = kX86ScratchRegs | kX86SavedRegs;
constexpr uint32_t kX86ByteRegs    = kX86ScratchRegs;
constexpr uint32_t kX86RegCount    = x86::kX64 ? 16u : 8u;

/* SDM Vol. 2D B.1.4.7 Table B-10 (p. B-6): the tttn field; "the n part
   indicates whether to use the condition (n = 0) or its negation (n = 1)". */
enum X86Cond : uint8_t {
    kX86CondO  = 0x0,
    kX86CondNo = 0x1,
    kX86CondC  = 0x2,
    kX86CondNc = 0x3,
    kX86CondZ  = 0x4,
    kX86CondNz = 0x5,
    kX86CondBe = 0x6,
    kX86CondA  = 0x7,
    kX86CondS  = 0x8,
    kX86CondNs = 0x9,
    kX86CondL  = 0xC,
    kX86CondGe = 0xD,
    kX86CondLe = 0xE,
    kX86CondG  = 0xF,
};

constexpr uint8_t X86InvertCond(uint8_t cond) { return static_cast<uint8_t>(cond ^ 1u); }

CERFENGINE_FORCEINLINE uint32_t X86SlotBytes(Type t) {
    return t == Type::kI1 ? 1u : TypeBits(t) / 8u;
}

class X86RegAlloc {
public:
    enum class Kind : uint8_t { kNone, kReg, kFlags, kSlot, kSpill };

    static constexpr int32_t kNoHome = INT32_MIN;

    struct Loc {
        Kind    kind = Kind::kNone;
        uint8_t reg  = 0;
        uint8_t cond = 0;
        bool    wide = true;
        int32_t disp = 0;
        int32_t home = kNoHome;
    };

    enum class OperandKind : uint8_t { kReg, kSlot, kImm };

    struct Operand {
        OperandKind kind;
        uint8_t     reg;
        int32_t     disp;
        uint32_t    imm;
    };

    static constexpr uint32_t kSpillSlots = 32;

    X86RegAlloc(uint8_t*& cursor, uint32_t* spill_area)
        : c_(cursor), spill_area_(spill_area), loc_(kMaxInsts), lazy_(kMaxInsts),
          flag_values_(kMaxInsts) {}

    CERFENGINE_FORCEINLINE void Reset(const Block& block, ValueInfo* info) {
        block_ = &block;
        info_  = info;
        lazy_count_ = 0u;
        flag_count_ = 0u;
        static_assert(kNoRef == 0xFFFFu, "every byte of kNoRef is 0xFF");
        std::memset(owner_, 0xFF, sizeof(owner_));
        owned_      = 0u;
        homed_      = 0u;
        locked_     = 0u;
        spill_used_ = 0u;
        op_temps_   = 0u;
    }
    CERFENGINE_FORCEINLINE const Loc& At(Ref r) const { return loc_[r]; }
    CERFENGINE_FORCEINLINE uint16_t   Remaining(Ref r) const { return info_[r].uses; }
    CERFENGINE_FORCEINLINE void       Undefine(Ref r) { loc_[r].kind = Kind::kNone; }

    CERFENGINE_FORCEINLINE void DefineSlot(Ref r, int32_t disp) {
        loc_[r] = Loc{Kind::kSlot, 0u, 0u, true, disp, disp};
        if (info_[r].uses != 0u) lazy_[lazy_count_++] = r;
    }

    CERFENGINE_FORCEINLINE void DefineFlags(Ref r, uint8_t cond) {
        loc_[r] = Loc{Kind::kFlags, 0u, cond, false, 0, kNoHome};
        if (info_[r].uses != 0u) flag_values_[flag_count_++] = r;
    }

    CERFENGINE_FORCEINLINE void DefineInReg(Ref r, uint8_t reg, bool wide) {
        SetOwner(reg, r);
        loc_[r]     = Loc{Kind::kReg, reg, 0u, wide, 0, kNoHome};
        if (info_[r].uses == 0u) Forget(r);
    }

    CERFENGINE_FORCEINLINE uint8_t Materialize(Ref r, uint32_t allowed) {
        if (IsValueRef(r)) {
            const Loc& l = loc_[r];
            if (l.kind == Kind::kReg && (allowed & RegBit(l.reg)) != 0u) {
                Lock(l.reg);
                return l.reg;
            }
        }
        return MaterializeSlow(r, allowed);
    }

    CERFENGINE_FORCEINLINE uint8_t MaterializeWide(Ref r, uint32_t allowed) {
        if (IsValueRef(r)) {
            const Loc& l = loc_[r];
            if (l.kind == Kind::kReg && l.wide && (allowed & RegBit(l.reg)) != 0u) {
                Lock(l.reg);
                return l.reg;
            }
        }
        return MaterializeWideSlow(r, allowed);
    }

    CERFENGINE_FORCEINLINE Operand Rm(Ref r) {
        if (IsConstRef(r)) {
            return Operand{OperandKind::kImm, 0u, 0, static_cast<uint32_t>(block_->ConstValue(r))};
        }
        if (IsSlotRef(r)) return Operand{OperandKind::kSlot, 0u, SlotRefDisp(r), 0u};
        const Loc& l = loc_[r];
        if (l.kind == Kind::kReg && l.wide) {
            Lock(l.reg);
            return Operand{OperandKind::kReg, l.reg, 0, 0u};
        }
        if (l.kind == Kind::kSlot && TypeBits(block_->At(r).type) == 32u) {
            return Operand{OperandKind::kSlot, 0u, l.disp, 0u};
        }
        return Operand{OperandKind::kReg, MaterializeWideSlow(r, kX86AllocRegs), 0, 0u};
    }

    CERFENGINE_FORCEINLINE void Consume(Ref r) {
        if (!IsValueRef(r)) return;
        uint16_t& uses = info_[r].uses;
        if (uses == 0u) ConsumeFault(r);
        if (--uses == 0u) Forget(r);
    }

    CERFENGINE_FORCEINLINE uint8_t TakeOrCopy(Ref src, Ref def, uint32_t allowed) {
        if (IsValueRef(src)) {
            Loc& l = loc_[src];
            if (l.kind == Kind::kReg && l.wide && info_[src].uses == 1u &&
                (allowed & RegBit(l.reg)) != 0u) {
                const uint8_t reg = l.reg;
                Lock(reg);
                info_[src].uses = 0u;
                l.kind          = Kind::kNone;
                SetOwner(reg, kTempOwner);
                return reg;
            }
        } else if (IsSlotRef(src)) {
            const uint8_t reg = PickReg(allowed);
            SetOwner(reg, kTempOwner);
            Lock(reg);
            EmitSlotLoad(reg, SlotRefDisp(src));
            return reg;
        }
        return TakeOrCopySlow(src, def, allowed);
    }

    uint8_t  TakeOrCopySlow(Ref src, Ref def, uint32_t allowed);
    uint8_t  CopyIntoFixed(Ref src, uint8_t reg);
    uint8_t  AllocTemp(uint32_t allowed);
    void     CopyOut(Ref src, uint8_t reg);

    CERFENGINE_FORCEINLINE uint8_t ClaimFixed(uint8_t reg) {
        if ((locked_ & RegBit(reg)) != 0u) FixedRegisterBusy(reg);
        Evict(RegBit(reg));
        SetOwner(reg, kTempOwner);
        Lock(reg);
        return reg;
    }

    CERFENGINE_FORCEINLINE void ReleaseTemp(uint8_t reg) {
        if (owner_[reg] == kTempOwner) SetOwner(reg, kNoRef);
    }
    CERFENGINE_FORCEINLINE void     Lock(uint8_t reg) { locked_ |= RegBit(reg); }
    CERFENGINE_FORCEINLINE void     UnlockAll() { locked_ = 0u; }
    CERFENGINE_FORCEINLINE void     EndOp() {
        if (op_temps_ != 0u) ReleaseOpTemps();
        locked_ = 0u;
    }
    static constexpr uint8_t kNoReg = 0xFFu;

    CERFENGINE_FORCEINLINE bool Quiet() const {
        return (homed_ | lazy_count_ | flag_count_) == 0u;
    }
    CERFENGINE_FORCEINLINE bool Idle() const { return (owned_ | homed_ | lazy_count_ | flag_count_) == 0u; }
    CERFENGINE_FORCEINLINE bool NoLiveFlags() const { return flag_count_ == 0u; }

    CERFENGINE_FORCEINLINE uint32_t FreeScratchMask() const {
        return kX86ScratchRegs & ~owned_ & ~locked_;
    }
    CERFENGINE_FORCEINLINE uint8_t FreeScratch() const {
        const uint32_t free = FreeScratchMask();
        return free != 0u ? static_cast<uint8_t>(LowestSetBit(free)) : kNoReg;
    }
    CERFENGINE_FORCEINLINE void     Evict(uint32_t regs) {
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

    Loc         PreserveAcrossCalls(Ref r);
    const void* SpillAddress(uint32_t index) const { return &spill_area_[index]; }

    CERFENGINE_FORCEINLINE void NoteStored(Ref value, int32_t disp) {
        if (!IsValueRef(value) || info_[value].uses == 0u) return;
        Loc& l = loc_[value];
        if (l.kind != Kind::kReg) return;
        l.home  = disp;
        homed_ |= RegBit(l.reg);
    }
    CERFENGINE_FORCEINLINE void BeforeStateWrite() {
        for (uint32_t m = homed_; m != 0u; m &= m - 1u) loc_[owner_[LowestSetBit(m)]].home = kNoHome;
        homed_ = 0u;
        while (lazy_count_ != 0u) MaterializeHomeless(lazy_[lazy_count_ - 1u]);
    }
    CERFENGINE_FORCEINLINE void BeforeCall() {
        BeforeFlagsClobber();
        BeforeStateWrite();
        Evict(kX86ScratchRegs);
    }
    CERFENGINE_FORCEINLINE void Fence() {
        if ((owned_ | lazy_count_ | flag_count_) != 0u) FenceFault();
        spill_used_ = 0u;
        locked_     = 0u;
    }

private:
    static constexpr Ref kTempOwner = 0xFFFEu;

    CERFENGINE_FORCEINLINE void SetOwner(uint8_t reg, Ref r) {
        owner_[reg] = r;
        homed_ &= ~RegBit(reg);
        if (r == kNoRef) {
            owned_ &= ~RegBit(reg);
        } else {
            owned_ |= RegBit(reg);
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
            const int32_t  bytes = block_->At(r).type == Type::kI1
                                       ? 1 : static_cast<int32_t>(TypeBits(block_->At(r).type) / 8u);
            if (start < disp + static_cast<int32_t>(size) && disp < start + bytes) {
                loc_[r].home = kNoHome;
                homed_ &= ~RegBit(static_cast<uint8_t>(reg));
            }
        }
    }

    CERFENGINE_FORCEINLINE uint8_t PickReg(uint32_t allowed) {
        const uint32_t free = allowed & ~locked_ & FreeMask();
        if (free != 0u) {
            if ((free & kX86ScratchRegs) != 0u) {
                return static_cast<uint8_t>(LowestSetBit(free & kX86ScratchRegs));
            }
            return (free & RegBit(x86::kSavedRegA)) != 0u ? x86::kSavedRegA : x86::kSavedRegB;
        }
        return PickRegByEviction(allowed);
    }

    CERFENGINE_FORCEINLINE uint32_t FreeMask() const { return kX86AllocRegs & ~owned_; }

    CERFENGINE_FORCEINLINE void EmitSlotLoad(uint8_t reg, int32_t disp) {
        x86::EmitMovRegBaseDisp32(c_, reg, x86::kStateReg, disp);
    }

    [[noreturn]] void FixedRegisterBusy(uint8_t reg);
    [[noreturn]] void FenceFault();
    void     ReleaseOpTemps();
    uint8_t  LoadSlotOperand(Ref r, uint32_t allowed);
    void     EvictSlow(uint32_t regs);
    void     MaterializeLazySlots(int32_t disp, uint32_t size);
    void     MaterializeHomeless(Ref r);
    uint8_t  PickRegByEviction(uint32_t allowed);
    uint8_t  MaterializeSlow(Ref r, uint32_t allowed);
    uint8_t  MaterializeWideSlow(Ref r, uint32_t allowed);
    void     MaterializeFlags();
    void     SaveFlag(Ref r);
    [[noreturn]] void ConsumeFault(Ref r);
    void     ForgetSlow(Ref r);
    void     SpillOwner(uint8_t reg);
    uint8_t  LoadInto(Ref r, uint8_t reg);
    void     CopyValue(Ref src, uint8_t reg);
    uint32_t NextSpill();

    uint8_t*&             c_;
    uint32_t*             spill_area_;
    const Block*          block_ = nullptr;
    std::vector<Loc>      loc_;
    ValueInfo*            info_ = nullptr;
    std::vector<Ref>      lazy_;
    std::vector<Ref>      flag_values_;
    uint32_t              lazy_count_ = 0;
    uint32_t              flag_count_ = 0;
    Ref                   owner_[kX86RegCount]{};
    uint32_t              owned_      = 0;
    uint32_t              homed_      = 0;
    uint32_t              locked_     = 0;
    uint32_t              spill_used_ = 0;
    uint32_t              op_temps_   = 0;
};

}
