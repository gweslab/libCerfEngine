#include "x86_reg_alloc.h"

#include <cstdio>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine::cerfir {

namespace {

[[noreturn]] void Fault(const char* what, uint32_t value) {
    char text[128];
    std::snprintf(text, sizeof(text), "CerfIR x86 register allocation: %s (value %%%u)", what,
                  value);
    throw EmitFault(text);
}

void SwapRemove(std::vector<Ref>& list, uint32_t& count, Ref r) {
    for (uint32_t i = count; i-- > 0u;) {
        if (list[i] != r) continue;
        list[i] = list[--count];
        return;
    }
}

constexpr uint8_t kScratchOrder[] = {x86::kEax, x86::kEcx, x86::kEdx, x86::kSavedRegA,
                                     x86::kSavedRegB};
constexpr uint8_t kSavedOrder[]   = {x86::kSavedRegA, x86::kSavedRegB, x86::kEax, x86::kEcx,
                                     x86::kEdx};

}

void X86RegAlloc::ReleaseOpTemps() {
    for (uint32_t m = op_temps_; m != 0u; m &= m - 1u) {
        const uint8_t reg = static_cast<uint8_t>(LowestSetBit(m));
        if (owner_[reg] == kTempOwner) SetOwner(reg, kNoRef);
    }
    op_temps_ = 0u;
}

uint8_t X86RegAlloc::LoadSlotOperand(Ref r, uint32_t allowed) {
    const uint8_t reg = PickReg(allowed);
    SetOwner(reg, kTempOwner);
    Lock(reg);
    op_temps_ |= RegBit(reg);
    x86::EmitMovRegBaseDisp32(c_, reg, x86::kStateReg, SlotRefDisp(r));
    return reg;
}

void X86RegAlloc::ConsumeFault(Ref r) {
    Fault("value consumed more often than it is used", r);
}

uint8_t X86RegAlloc::TakeOrCopySlow(Ref src, Ref, uint32_t allowed) {
    if (IsConstRef(src)) {
        const uint8_t reg = PickReg(allowed);
        SetOwner(reg, kTempOwner);
        Lock(reg);
        x86::EmitMovRegImm32(c_, reg, static_cast<uint32_t>(block_->ConstValue(src)));
        return reg;
    }
    Loc& l = loc_[src];
    if (l.kind == Kind::kFlags || (l.kind == Kind::kReg && !l.wide)) {
        MaterializeWide(src, kX86AllocRegs);
    }
    if (l.kind == Kind::kReg) {
        Lock(l.reg);
        if (info_[src].uses == 1u && (allowed & RegBit(l.reg)) != 0u) {
            const uint8_t reg = l.reg;
            info_[src].uses   = 0u;
            l.kind            = Kind::kNone;
            SetOwner(reg, kTempOwner);
            return reg;
        }
    }
    const uint8_t reg = PickReg(allowed);
    SetOwner(reg, kTempOwner);
    Lock(reg);
    CopyValue(src, reg);
    Consume(src);
    return reg;
}

void X86RegAlloc::CopyValue(Ref src, uint8_t reg) {
    const Loc& l = loc_[src];
    switch (l.kind) {
    case Kind::kReg:
        x86::EmitMovRegReg(c_, reg, l.reg);
        break;
    case Kind::kSlot:
        if (X86SlotBytes(block_->At(src).type) == 4u) {
            x86::EmitMovRegBaseDisp32(c_, reg, x86::kStateReg, l.disp);
        } else {
            x86::EmitMovzxRegByteBaseDisp32(c_, reg, x86::kStateReg, l.disp);
        }
        break;
    case Kind::kSpill:
        x86::EmitMovRegDwordPtr(c_, reg, &spill_area_[l.disp]);
        break;
    default:
        Fault("operand is not available", src);
    }
}

uint8_t X86RegAlloc::PickRegByEviction(uint32_t allowed) {
    for (uint32_t i = 0; i < 5u; ++i) {
        const uint8_t reg = kScratchOrder[i];
        if ((allowed & RegBit(reg)) != 0u && (locked_ & RegBit(reg)) == 0u &&
            owner_[reg] != kTempOwner) {
            SpillOwner(reg);
            return reg;
        }
    }
    char text[160];
    std::snprintf(text, sizeof(text),
                  "CerfIR x86 register allocation: no allocatable register (allowed 0x%X locked "
                  "0x%X owners scratch %u %u %u saved %u %u)",
                  allowed, locked_, owner_[x86::kEax], owner_[x86::kEcx], owner_[x86::kEdx],
                  owner_[x86::kSavedRegA], owner_[x86::kSavedRegB]);
    throw EmitFault(text);
}

uint32_t X86RegAlloc::NextSpill() {
    for (uint32_t i = 0; i < kSpillSlots; ++i) {
        if ((spill_used_ & (1u << i)) == 0u) {
            spill_used_ |= 1u << i;
            return i;
        }
    }
    Fault("spill area exhausted", 0u);
}

void X86RegAlloc::SpillOwner(uint8_t reg) {
    const Ref r = owner_[reg];
    Loc&      l = loc_[r];
    if (l.home != kNoHome) {
        l.kind      = Kind::kSlot;
        l.disp      = l.home;
        l.wide      = true;
        SetOwner(reg, kNoRef);
        lazy_[lazy_count_++] = r;
        return;
    }
    const uint32_t slot = NextSpill();
    x86::EmitMovDwordPtrReg(c_, &spill_area_[slot], reg);
    l.kind      = Kind::kSpill;
    l.disp      = static_cast<int32_t>(slot);
    SetOwner(reg, kNoRef);
}

uint8_t X86RegAlloc::LoadInto(Ref r, uint8_t reg) {
    Loc& l = loc_[r];
    switch (l.kind) {
    case Kind::kReg:
        if (l.reg != reg) {
            x86::EmitMovRegReg(c_, reg, l.reg);
            SetOwner(l.reg, kNoRef);
        }
        break;
    case Kind::kSlot: {
        const Type t = block_->At(r).type;
        if (X86SlotBytes(t) == 4u) {
            x86::EmitMovRegBaseDisp32(c_, reg, x86::kStateReg, l.disp);
        } else if (X86SlotBytes(t) == 1u) {
            x86::EmitMovzxRegByteBaseDisp32(c_, reg, x86::kStateReg, l.disp);
        } else {
            Fault("slot width has no load", r);
        }
        l.wide = true;
        SwapRemove(lazy_, lazy_count_, r);
        break;
    }
    case Kind::kSpill:
        x86::EmitMovRegDwordPtr(c_, reg, &spill_area_[l.disp]);
        spill_used_ &= ~(1u << l.disp);
        break;
    case Kind::kFlags:
        if ((kX86ByteRegs & RegBit(reg)) == 0u) Fault("flag into a register without a byte form", r);
        x86::EmitSetccReg8(c_, static_cast<uint8_t>(x86::kSetO + l.cond), reg);
        l.wide = false;
        SwapRemove(flag_values_, flag_count_, r);
        break;
    case Kind::kNone:
        Fault("value is not available", r);
    }
    l.kind      = Kind::kReg;
    l.reg       = reg;
    SetOwner(reg, r);
    if (l.home != kNoHome) homed_ |= RegBit(reg);
    return reg;
}

uint8_t X86RegAlloc::MaterializeSlow(Ref r, uint32_t allowed) {
    if (IsSlotRef(r)) return LoadSlotOperand(r, allowed);
    if (IsConstRef(r) || r == kNoRef) Fault("materialize of a constant", r);
    Loc& l = loc_[r];
    if (l.kind == Kind::kReg && (allowed & RegBit(l.reg)) != 0u) {
        Lock(l.reg);
        return l.reg;
    }
    if (l.kind == Kind::kReg) Lock(l.reg);
    if (l.kind == Kind::kFlags && (allowed & kX86ByteRegs) == 0u) {
        const uint8_t byte_reg = PickReg(kX86ByteRegs);
        LoadInto(r, byte_reg);
        Lock(byte_reg);
    }
    if (l.kind == Kind::kReg && (allowed & RegBit(l.reg)) == 0u && !l.wide) {
        x86::EmitMovzxReg32Reg8(c_, l.reg, l.reg);
        l.wide = true;
    }
    const uint32_t mask = l.kind == Kind::kFlags ? (allowed & kX86ByteRegs) : allowed;
    const uint8_t  reg  = PickReg(mask);
    LoadInto(r, reg);
    Lock(reg);
    return reg;
}

uint8_t X86RegAlloc::MaterializeWideSlow(Ref r, uint32_t allowed) {
    if (IsSlotRef(r)) return LoadSlotOperand(r, allowed);
    const uint8_t reg = Materialize(r, allowed);
    if (!loc_[r].wide) {
        x86::EmitMovzxReg32Reg8(c_, reg, reg);
        loc_[r].wide = true;
    }
    return reg;
}

void X86RegAlloc::ForgetSlow(Ref r) {
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

uint8_t X86RegAlloc::CopyIntoFixed(Ref src, uint8_t reg) {
    if (IsSlotRef(src)) {
        ClaimFixed(reg);
        x86::EmitMovRegBaseDisp32(c_, reg, x86::kStateReg, SlotRefDisp(src));
        return reg;
    }
    if (!IsConstRef(src) && loc_[src].kind == Kind::kReg && loc_[src].reg == reg &&
        loc_[src].wide && info_[src].uses == 1u) {
        info_[src].uses   = 0u;
        loc_[src].kind    = Kind::kNone;
        SetOwner(reg, kTempOwner);
        Lock(reg);
        return reg;
    }
    ClaimFixed(reg);
    if (IsConstRef(src)) {
        x86::EmitMovRegImm32(c_, reg, static_cast<uint32_t>(block_->ConstValue(src)));
        return reg;
    }
    if (loc_[src].kind == Kind::kFlags || (loc_[src].kind == Kind::kReg && !loc_[src].wide)) {
        MaterializeWide(src, kX86AllocRegs & ~RegBit(reg));
    }
    CopyValue(src, reg);
    Consume(src);
    return reg;
}

void X86RegAlloc::CopyOut(Ref src, uint8_t reg) {
    if (loc_[src].kind == Kind::kFlags || (loc_[src].kind == Kind::kReg && !loc_[src].wide)) {
        MaterializeWide(src, kX86SavedRegs);
    }
    CopyValue(src, reg);
    Consume(src);
}

void X86RegAlloc::FixedRegisterBusy(uint8_t reg) {
    Fault("fixed register already in use by this op", reg);
}

uint8_t X86RegAlloc::AllocTemp(uint32_t allowed) {
    const uint8_t reg = PickReg(allowed);
    SetOwner(reg, kTempOwner);
    Lock(reg);
    return reg;
}

void X86RegAlloc::EvictSlow(uint32_t regs) {
    for (uint32_t m = regs & owned_; m != 0u; m &= m - 1u) {
        const uint8_t reg = static_cast<uint8_t>(LowestSetBit(m));
        const Ref     r   = owner_[reg];
        if (r == kTempOwner) Fault("temporary register live across an eviction", reg);
        const uint32_t room = kX86AllocRegs & ~regs & ~locked_;
        uint8_t        to   = 0xFFu;
        for (uint8_t cand : kSavedOrder) {
            if ((room & RegBit(cand)) != 0u && owner_[cand] == kNoRef) {
                to = cand;
                break;
            }
        }
        if (to == 0xFFu) {
            SpillOwner(reg);
            continue;
        }
        if (!loc_[r].wide) {
            x86::EmitMovzxReg32Reg8(c_, reg, reg);
            loc_[r].wide = true;
        }
        x86::EmitMovRegReg(c_, to, reg);
        SetOwner(reg, kNoRef);
        SetOwner(to, r);
        loc_[r].reg   = to;
        if (loc_[r].home != kNoHome) homed_ |= RegBit(to);
    }
}

void X86RegAlloc::SaveFlag(Ref r) {
    LoadInto(r, PickReg(kX86ByteRegs));
}

void X86RegAlloc::MaterializeFlags() {
    while (flag_count_ != 0u) SaveFlag(flag_values_[flag_count_ - 1u]);
}

void X86RegAlloc::BeforeFlagsClobberExcept(Ref keep) {
    for (uint32_t i = flag_count_; i-- > 0u;) {
        if (flag_values_[i] != keep || info_[keep].uses > 1u) SaveFlag(flag_values_[i]);
    }
}

void X86RegAlloc::MaterializeHomeless(Ref r) {
    const uint8_t reg = PickReg(kX86AllocRegs);
    LoadInto(r, reg);
    loc_[r].home = kNoHome;
    homed_ &= ~RegBit(reg);
}

void X86RegAlloc::MaterializeLazySlots(int32_t disp, uint32_t size) {
    for (uint32_t i = lazy_count_; i-- > 0u;) {
        const Ref      r     = lazy_[i];
        const int32_t  start = loc_[r].disp;
        const uint32_t bytes = X86SlotBytes(block_->At(r).type);
        if (start < disp + static_cast<int32_t>(size) &&
            disp < start + static_cast<int32_t>(bytes)) {
            MaterializeHomeless(r);
        }
    }
}

X86RegAlloc::Loc X86RegAlloc::PreserveAcrossCalls(Ref r) {
    Loc& l = loc_[r];
    if (l.kind == Kind::kFlags || (l.kind == Kind::kReg && !l.wide)) {
        MaterializeWide(r, kX86AllocRegs);
    }
    if (l.kind != Kind::kReg) return l;
    if ((kX86SavedRegs & RegBit(l.reg)) != 0u) {
        Lock(l.reg);
        return l;
    }
    const uint32_t free = kX86SavedRegs & ~locked_ & FreeMask();
    if (free != 0u) {
        const uint8_t to =
            (free & RegBit(x86::kSavedRegA)) != 0u ? x86::kSavedRegA : x86::kSavedRegB;
        LoadInto(r, to);
        Lock(to);
        return l;
    }
    SpillOwner(l.reg);
    return l;
}

void X86RegAlloc::FenceFault() {
    if (owned_ != 0u) Fault("value live at a label", owner_[LowestSetBit(owned_)]);
    if (lazy_count_ != 0u) Fault("slot read live at a label", lazy_[lazy_count_ - 1u]);
    Fault("flag live at a label", flag_values_[flag_count_ - 1u]);
}

}
