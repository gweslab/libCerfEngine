#include "a64_reg_alloc.h"

#include <cstdio>

#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"

namespace cerfengine::cerfir {

namespace {

[[noreturn]] void Fault(const char* what, uint32_t value) {
    char text[128];
    std::snprintf(text, sizeof(text), "CerfIR A64 register allocation: %s (value %%%u)", what, value);
    throw EmitFault(text);
}

void SwapRemove(std::vector<Ref>& list, uint32_t& count, Ref r) {
    for (uint32_t i = count; i-- > 0u;) {
        if (list[i] != r) continue;
        list[i] = list[--count];
        return;
    }
}

}

void A64RegAlloc::EmitSlotLoad(uint8_t reg, int32_t disp, uint32_t bytes) {
    if (bytes == 4u) {
        pins_.Load(c_, reg, disp);
    } else if (bytes == 1u) {
        pins_.CheckUnpinned(disp, 1u);
        a64::EmitLdrb(c_, reg, a64::kStateReg, disp);
    } else {
        Fault("slot width has no load", bytes);
    }
}

void A64RegAlloc::ReleaseOpTemps() {
    for (uint32_t m = op_temps_; m != 0u; m &= m - 1u) {
        const uint8_t reg = static_cast<uint8_t>(LowestSetBit(m));
        if (owner_[reg] == kTempOwner) SetOwner(reg, kNoRef);
    }
    op_temps_ = 0u;
}

uint8_t A64RegAlloc::LoadOperand(Ref r, uint32_t allowed) {
    const uint8_t reg = PickReg(allowed);
    SetOwner(reg, kTempOwner);
    Lock(reg);
    op_temps_ |= A64RegBit(reg);
    if (IsSlotRef(r)) {
        pins_.Load(c_, reg, SlotRefDisp(r));
    } else {
        a64::EmitMovImm32(c_, reg, static_cast<uint32_t>(block_->ConstValue(r)));
    }
    return reg;
}

void A64RegAlloc::ConsumeFault(Ref r) {
    Fault("value consumed more often than it is used", r);
}

void A64RegAlloc::CopyValue(Ref src, uint8_t reg) {
    const Loc& l = loc_[src];
    switch (l.kind) {
    case Kind::kReg:
        a64::EmitMovReg(c_, reg, l.reg);
        return;
    case Kind::kSlot:
        EmitSlotLoad(reg, l.disp, A64SlotBytes(block_->At(src).type));
        return;
    case Kind::kSpill:
        a64::EmitLdrW(c_, reg, a64::kSp, SpillOffset(static_cast<uint32_t>(l.disp)));
        return;
    case Kind::kFlags:
        a64::EmitCset(c_, reg, l.cond);
        return;
    case Kind::kNone:
        break;
    }
    Fault("operand is not available", src);
}

uint8_t A64RegAlloc::PickRegByEviction(uint32_t allowed) {
    const uint32_t candidates = allowed & alloc_regs_ & ~locked_;
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        const uint32_t set = candidates & (pass == 0u ? kA64ScratchRegs : saved_regs_);
        for (uint32_t m = set; m != 0u; m &= m - 1u) {
            const uint8_t reg = static_cast<uint8_t>(LowestSetBit(m));
            if (owner_[reg] == kTempOwner) continue;
            SpillOwner(reg);
            return reg;
        }
    }
    char text[160];
    std::snprintf(text, sizeof(text),
                  "CerfIR A64 register allocation: no allocatable register (allowed 0x%X locked 0x%X "
                  "owned 0x%X)",
                  allowed, locked_, owned_);
    throw EmitFault(text);
}

uint32_t A64RegAlloc::NextSpill() {
    for (uint32_t i = 0; i < kSpillSlots; ++i) {
        if ((spill_used_ & (1u << i)) == 0u) {
            spill_used_ |= 1u << i;
            return i;
        }
    }
    Fault("spill area exhausted", 0u);
}

void A64RegAlloc::SpillOwner(uint8_t reg) {
    const Ref r = owner_[reg];
    Loc&      l = loc_[r];
    if (l.home != kNoHome) {
        l.kind = Kind::kSlot;
        l.disp = l.home;
        SetOwner(reg, kNoRef);
        lazy_[lazy_count_++] = r;
        return;
    }
    const uint32_t slot = NextSpill();
    a64::EmitStrW(c_, reg, a64::kSp, SpillOffset(slot));
    l.kind = Kind::kSpill;
    l.disp = static_cast<int32_t>(slot);
    SetOwner(reg, kNoRef);
}

uint8_t A64RegAlloc::LoadInto(Ref r, uint8_t reg) {
    Loc& l = loc_[r];
    switch (l.kind) {
    case Kind::kReg:
        if (l.reg != reg) {
            a64::EmitMovReg(c_, reg, l.reg);
            SetOwner(l.reg, kNoRef);
        }
        break;
    case Kind::kSlot:
        EmitSlotLoad(reg, l.disp, A64SlotBytes(block_->At(r).type));
        SwapRemove(lazy_, lazy_count_, r);
        break;
    case Kind::kSpill:
        a64::EmitLdrW(c_, reg, a64::kSp, SpillOffset(static_cast<uint32_t>(l.disp)));
        spill_used_ &= ~(1u << l.disp);
        break;
    case Kind::kFlags:
        a64::EmitCset(c_, reg, l.cond);
        SwapRemove(flag_values_, flag_count_, r);
        break;
    case Kind::kNone:
        Fault("value is not available", r);
    }
    l.kind = Kind::kReg;
    l.reg  = reg;
    SetOwner(reg, r);
    if (l.home != kNoHome) homed_ |= A64RegBit(reg);
    return reg;
}

uint8_t A64RegAlloc::MaterializeSlow(Ref r, uint32_t allowed) {
    if (IsSlotRef(r) || IsConstRef(r)) return LoadOperand(r, allowed);
    if (r == kNoRef) Fault("materialize of no value", r);
    Loc& l = loc_[r];
    if (l.kind == Kind::kReg && (allowed & A64RegBit(l.reg)) != 0u) {
        Lock(l.reg);
        return l.reg;
    }
    if (l.kind == Kind::kReg) Lock(l.reg);
    const uint8_t reg = PickReg(allowed);
    LoadInto(r, reg);
    Lock(reg);
    return reg;
}

void A64RegAlloc::ForgetSlow(Ref r) {
    Loc& l = loc_[r];
    switch (l.kind) {
    case Kind::kReg:
        if (owner_[l.reg] == r) SetOwner(l.reg, kNoRef);
        break;
    case Kind::kSpill:
        spill_used_ &= ~(1u << l.disp);
        break;
    case Kind::kSlot:
        SwapRemove(lazy_, lazy_count_, r);
        break;
    case Kind::kFlags:
        SwapRemove(flag_values_, flag_count_, r);
        break;
    case Kind::kNone:
        break;
    }
    l.kind = Kind::kNone;
}

uint8_t A64RegAlloc::CopyIntoFixed(Ref src, uint8_t reg) {
    if (IsValueRef(src) && loc_[src].kind == Kind::kReg && loc_[src].reg == reg &&
        info_[src].uses == 1u) {
        info_[src].uses = 0u;
        loc_[src].kind  = Kind::kNone;
        SetOwner(reg, kTempOwner);
        Lock(reg);
        return reg;
    }
    ClaimFixed(reg);
    if (IsConstRef(src)) {
        a64::EmitMovImm32(c_, reg, static_cast<uint32_t>(block_->ConstValue(src)));
        return reg;
    }
    if (IsSlotRef(src)) {
        pins_.Load(c_, reg, SlotRefDisp(src));
        return reg;
    }
    CopyValue(src, reg);
    Consume(src);
    return reg;
}

void A64RegAlloc::CopyOut(Ref src, uint8_t reg) {
    CopyValue(src, reg);
    Consume(src);
}

void A64RegAlloc::FixedRegisterBusy(uint8_t reg) {
    Fault("fixed register already in use by this op", reg);
}

uint8_t A64RegAlloc::AllocTemp(uint32_t allowed) {
    const uint8_t reg = PickReg(allowed);
    SetOwner(reg, kTempOwner);
    Lock(reg);
    return reg;
}

void A64RegAlloc::EvictSlow(uint32_t regs) {
    for (uint32_t m = regs & owned_; m != 0u; m &= m - 1u) {
        const uint8_t reg = static_cast<uint8_t>(LowestSetBit(m));
        const Ref     r   = owner_[reg];
        if (r == kTempOwner) Fault("temporary register live across an eviction", reg);
        const uint32_t room  = alloc_regs_ & ~regs & ~locked_ & ~owned_;
        const uint32_t saved = room & saved_regs_;
        if (room == 0u) {
            SpillOwner(reg);
            continue;
        }
        const uint8_t to = static_cast<uint8_t>(LowestSetBit(saved != 0u ? saved : room));
        a64::EmitMovReg(c_, to, reg);
        SetOwner(reg, kNoRef);
        SetOwner(to, r);
        loc_[r].reg = to;
        if (loc_[r].home != kNoHome) homed_ |= A64RegBit(to);
    }
}

void A64RegAlloc::SaveFlag(Ref r) {
    LoadInto(r, PickReg(kA64AllocRegs));
}

void A64RegAlloc::MaterializeFlags() {
    while (flag_count_ != 0u) SaveFlag(flag_values_[flag_count_ - 1u]);
}

void A64RegAlloc::BeforeFlagsClobberExcept(Ref keep) {
    for (uint32_t i = flag_count_; i-- > 0u;) {
        if (flag_values_[i] != keep || info_[keep].uses > 1u) SaveFlag(flag_values_[i]);
    }
}

void A64RegAlloc::MaterializeHomeless(Ref r) {
    const uint8_t reg = PickReg(kA64AllocRegs);
    LoadInto(r, reg);
    loc_[r].home = kNoHome;
    homed_ &= ~A64RegBit(reg);
}

void A64RegAlloc::MaterializeLazySlots(int32_t disp, uint32_t size) {
    for (uint32_t i = lazy_count_; i-- > 0u;) {
        const Ref      r     = lazy_[i];
        const int32_t  start = loc_[r].disp;
        const uint32_t bytes = A64SlotBytes(block_->At(r).type);
        if (start < disp + static_cast<int32_t>(size) && disp < start + static_cast<int32_t>(bytes)) {
            MaterializeHomeless(r);
        }
    }
}

A64RegAlloc::Loc A64RegAlloc::PreserveAcrossCalls(Ref r) {
    Loc& l = loc_[r];
    if (l.kind == Kind::kFlags) Materialize(r, saved_regs_ != 0u ? saved_regs_ : alloc_regs_);
    if (l.kind != Kind::kReg) return l;
    if ((saved_regs_ & A64RegBit(l.reg)) != 0u) {
        Lock(l.reg);
        return l;
    }
    const uint32_t free = saved_regs_ & ~locked_ & FreeMask();
    if (free != 0u) {
        const uint8_t to = static_cast<uint8_t>(LowestSetBit(free));
        LoadInto(r, to);
        Lock(to);
        return l;
    }
    SpillOwner(l.reg);
    return l;
}

void A64RegAlloc::FenceFault() {
    if (owned_ != 0u) Fault("value live at a label", owner_[LowestSetBit(owned_)]);
    if (lazy_count_ != 0u) Fault("slot read live at a label", lazy_[lazy_count_ - 1u]);
    Fault("flag live at a label", flag_values_[flag_count_ - 1u]);
}

}
