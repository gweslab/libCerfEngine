#pragma once

#include <cstdint>
#include <cstdio>

#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"
#include "a64_lowering.h"
#include "a64_operand.h"
#include "a64_soft_tlb.h"

namespace cerfengine::cerfir {

[[noreturn]] CERFENGINE_NOINLINE inline void A64AccessWidthFault(uint32_t width_bytes) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR A64 lowering: a %u-byte guest access has no lowering",
                  static_cast<unsigned>(width_bytes));
    throw EmitFault(text);
}

CERFENGINE_FORCEINLINE void A64CheckAccessWidth(uint32_t width_bytes) {
    if (width_bytes != 1u && width_bytes != 2u && width_bytes != 4u) A64AccessWidthFault(width_bytes);
}

[[noreturn]] inline void A64OperandHasNoLocation() {
    throw EmitFault("CerfIR A64 lowering: a memory operand has no location");
}

CERFENGINE_FORCEINLINE void A64AddOffset(uint8_t*& c, uint8_t reg, int32_t offset) {
    const uint32_t value = static_cast<uint32_t>(offset);
    if (value == 0u) return;
    if (a64::AddSubImmFits(value)) {
        a64::EmitAddImm(c, reg, reg, value);
    } else if (a64::AddSubImmFits(0u - value)) {
        a64::EmitSubImm(c, reg, reg, 0u - value);
    } else {
        a64::EmitMovImm32(c, a64::kIp1, value);
        a64::EmitAddReg(c, reg, reg, a64::kIp1);
    }
}

inline void A64PlaceOperand(A64Lowering& l, Ref r, A64Operand& o) {
    if (r == kNoRef) {
        o = A64Operand{};
        return;
    }
    if (IsConstRef(r)) {
        o = A64Operand{A64OperandKind::kConst, 0u, 0, static_cast<uint32_t>(l.GetBlock().ConstValue(r))};
        return;
    }
    if (IsSlotRef(r)) {
        o = A64Operand{A64OperandKind::kSlot, 0u, SlotRefDisp(r), 0u};
        return;
    }
    const A64RegAlloc::Loc loc = l.Regs().PreserveAcrossCalls(r);
    switch (loc.kind) {
    case A64RegAlloc::Kind::kReg:
        o = A64Operand{A64OperandKind::kReg, loc.reg, 0, 0u};
        return;
    case A64RegAlloc::Kind::kSlot:
        o = A64Operand{A64OperandKind::kSlot, 0u, loc.disp, 0u};
        return;
    case A64RegAlloc::Kind::kSpill:
        o = A64Operand{A64OperandKind::kSpill, 0u, loc.disp, 0u};
        return;
    default:
        A64OperandHasNoLocation();
    }
}

CERFENGINE_FORCEINLINE void A64LoadOperand(A64PinnedSlots& p, uint8_t*& c, const A64Operand& o,
                                           uint8_t reg) {
    switch (o.kind) {
    case A64OperandKind::kConst:
        a64::EmitMovImm32(c, reg, o.imm);
        return;
    case A64OperandKind::kRecompute:
        p.Load(c, reg, o.disp);
        A64AddOffset(c, reg, static_cast<int32_t>(o.imm));
        return;
    case A64OperandKind::kSlot:
        p.Load(c, reg, o.disp);
        return;
    case A64OperandKind::kSpill:
        a64::EmitLdrW(c, reg, a64::kSp, A64RegAlloc::SpillOffset(static_cast<uint32_t>(o.disp)));
        return;
    case A64OperandKind::kReg:
        a64::EmitMovReg(c, reg, o.reg);
        return;
    case A64OperandKind::kNone:
        break;
    }
    A64OperandHasNoLocation();
}

CERFENGINE_FORCEINLINE void A64EmitWriteback(A64PinnedSlots& p, uint8_t*& c, const A64Operand& wb,
                                             int32_t slot) {
    if (wb.kind == A64OperandKind::kNone) return;
    if (wb.kind == A64OperandKind::kConst) {
        p.StoreImm(c, slot, wb.imm, kA64Tmp0);
        return;
    }
    uint8_t source = wb.reg;
    if (wb.kind != A64OperandKind::kReg) {
        A64LoadOperand(p, c, wb, kA64Tmp0);
        source = kA64Tmp0;
    }
    p.Store(c, source, slot);
}

CERFENGINE_FORCEINLINE void A64RecomputedWriteback(const MemAttr& m, A64Operand& o) {
    o = A64Operand{m.writeback_slot != kNoSlot ? A64OperandKind::kRecompute : A64OperandKind::kNone,
                   0u, m.base_slot, static_cast<uint32_t>(m.offset)};
}

[[noreturn]] inline void A64SlowOperandFault() {
    throw EmitFault("CerfIR A64 lowering: a slow-path operand has no register form");
}

CERFENGINE_FORCEINLINE void A64LoadSlowOperand(uint8_t*& c, const A64Operand& o, uint8_t reg,
                                               uint32_t saved_regs) {
    switch (o.kind) {
    case A64OperandKind::kConst:
        a64::EmitMovImm32(c, reg, o.imm);
        return;
    case A64OperandKind::kSlot:
    case A64OperandKind::kRecompute:
        a64::EmitLdrW(c, reg, a64::kStateReg, o.disp);
        return;
    case A64OperandKind::kSpill:
        a64::EmitLdrW(c, reg, a64::kSp, A64RegAlloc::SpillOffset(static_cast<uint32_t>(o.disp)));
        return;
    case A64OperandKind::kReg:
        if ((saved_regs & A64RegBit(o.reg)) == 0u) break;
        a64::EmitMovReg(c, reg, o.reg);
        return;
    case A64OperandKind::kNone:
        break;
    }
    A64SlowOperandFault();
}

CERFENGINE_FORCEINLINE bool A64InlineLookup(const A64Lowering& l, const MemAttr& m) {
    return m.unpriv || l.Target().link->translation_off;
}

CERFENGINE_FORCEINLINE uint8_t* A64EmitTlbLookup(A64Lowering& l, uint8_t* c, const MemAttr& m,
                                                 TlbAccess access) {
    const A64SoftTlb& tlb  = *l.Target().tlb;
    const BlockLink&  link = *l.Target().link;
    A64PinnedSlots&   pins = l.Pins();
    if (!A64InlineLookup(l, m)) {
        uint8_t* call = nullptr;
        c = tlb.EmitLookup(c, link.privilege, access, &call);
        const uint32_t dirty = pins.State().dirty;
        if (dirty != 0u) l.Tails().AddMissStub(call, tlb.MissTarget(link.privilege, access), dirty);
        return c;
    }
    pins.Flush(c);
    c = m.unpriv ? tlb.EmitUserLookup(c, access) : tlb.EmitMissCall(c, access);
    pins.StaleCallerSaved();
    return c;
}

CERFENGINE_FORCEINLINE void A64PrepareMemoryTail(A64Lowering& l, A64MemoryTail& t, const MemAttr& m,
                                                 bool store) {
    const MemorySlowModel& slow   = l.Target().model->slow;
    const bool             has_wb = t.wb.kind != A64OperandKind::kNone;
    t.entries    = slow.entries[SlowShapeIndex(m.complete, store, m.width_bytes, m.sign, has_wb,
                                               m.stop_after_io)];
    t.push_value = store && !m.complete;
    t.complete   = m.complete;
    MemorySlowDesc& d    = t.desc;
    d.context            = slow.context;
    d.guest_pc           = m.guest_pc;
    d.next_pc            = m.next_pc;
    d.raw                = m.raw;
    d.result_slot        = m.result_slot;
    d.writeback_slot     = m.writeback_slot;
    d.wb_add             = t.wb.kind == A64OperandKind::kRecompute ? t.wb.imm : 0u;
    d.align              = m.align;
    d.unpriv             = m.unpriv;
    d.writeback_on_fault = m.writeback_on_fault;
    d.back_out           = m.back_out;
}

}
