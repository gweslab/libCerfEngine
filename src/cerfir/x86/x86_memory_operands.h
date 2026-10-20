#pragma once

#include <cstdint>
#include <cstdio>

#include "../../emit_fault.h"
#include <cerfengine/force_inline.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lowering.h"
#include "x86_operand.h"
#include "x86_soft_tlb.h"

namespace cerfengine::cerfir {

[[noreturn]] CERFENGINE_NOINLINE inline void X86AccessWidthFault(uint32_t width_bytes) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR x86 lowering: a %u-byte guest access has no lowering",
                  static_cast<unsigned>(width_bytes));
    throw EmitFault(text);
}

CERFENGINE_FORCEINLINE void X86CheckAccessWidth(uint32_t width_bytes) {
    if (width_bytes != 1u && width_bytes != 2u && width_bytes != 4u) X86AccessWidthFault(width_bytes);
}

[[noreturn]] inline void X86OperandHasNoLocation() {
    throw EmitFault("CerfIR x86 lowering: a memory operand has no location");
}

inline void X86PlaceOperand(X86Lowering& l, Ref r, X86Operand& o) {
    if (r == kNoRef) {
        o = X86Operand{};
        return;
    }
    if (IsConstRef(r)) {
        o = X86Operand{X86OperandKind::kConst, 0u, 0,
                       static_cast<uint32_t>(l.GetBlock().ConstValue(r)), nullptr};
        return;
    }
    if (IsSlotRef(r)) {
        o = X86Operand{X86OperandKind::kSlot, 0u, SlotRefDisp(r), 0u, nullptr};
        return;
    }
    X86RegAlloc&           ra  = l.Regs();
    const X86RegAlloc::Loc loc = ra.PreserveAcrossCalls(r);
    switch (loc.kind) {
    case X86RegAlloc::Kind::kReg:
        o = X86Operand{X86OperandKind::kReg, loc.reg, 0, 0u, nullptr};
        return;
    case X86RegAlloc::Kind::kSlot:
        o = X86Operand{X86OperandKind::kSlot, 0u, loc.disp, 0u, nullptr};
        return;
    case X86RegAlloc::Kind::kSpill:
        o = X86Operand{X86OperandKind::kSpill, 0u, 0, 0u,
                       static_cast<const uint32_t*>(ra.SpillAddress(static_cast<uint32_t>(loc.disp)))};
        return;
    default:
        X86OperandHasNoLocation();
    }
}

CERFENGINE_FORCEINLINE void X86LoadOperand(uint8_t*& c, const X86Operand& o, uint8_t reg) {
    switch (o.kind) {
    case X86OperandKind::kConst:
        x86::EmitMovRegImm32(c, reg, o.imm);
        return;
    case X86OperandKind::kRecompute:
        x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, o.disp);
        if (o.imm != 0u) x86::EmitAddRegImm32(c, reg, o.imm);
        return;
    case X86OperandKind::kSlot:
        x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, o.disp);
        return;
    case X86OperandKind::kSpill:
        x86::EmitMovRegDwordPtr(c, reg, o.spill);
        return;
    case X86OperandKind::kReg:
        x86::EmitMovRegReg(c, reg, o.reg);
        return;
    case X86OperandKind::kNone:
        break;
    }
    X86OperandHasNoLocation();
}

CERFENGINE_FORCEINLINE void X86EmitWriteback(uint8_t*& c, const X86Operand& wb, int32_t slot) {
    if (wb.kind == X86OperandKind::kNone) return;
    if (wb.kind == X86OperandKind::kConst) {
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, slot, wb.imm);
        return;
    }
    uint8_t source = wb.reg;
    if (wb.kind != X86OperandKind::kReg) {
        X86LoadOperand(c, wb, x86::kEax);
        source = x86::kEax;
    }
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, slot, source);
}

CERFENGINE_FORCEINLINE void X86RecomputedWriteback(const MemAttr& m, X86Operand& o) {
    o = X86Operand{m.writeback_slot != kNoSlot ? X86OperandKind::kRecompute : X86OperandKind::kNone,
                   0u, m.base_slot, static_cast<uint32_t>(m.offset), nullptr};
}

[[noreturn]] inline void X86SlowOperandFault() {
    throw EmitFault("CerfIR x86 lowering: a slow-path operand has no stack form");
}

CERFENGINE_FORCEINLINE void X86PushSlowOperand(uint8_t*& c, const X86Operand& o) {
    switch (o.kind) {
    case X86OperandKind::kConst:
        x86::EmitPush32(c, o.imm);
        return;
    case X86OperandKind::kSlot:
    case X86OperandKind::kRecompute:
        x86::EmitPushBaseDisp32(c, x86::kStateReg, o.disp);
        return;
    case X86OperandKind::kSpill:
        x86::EmitPushDwordPtr(c, o.spill);
        return;
    case X86OperandKind::kReg:
        if (o.reg != x86::kEdi && o.reg != x86::kEbp) break;
        x86::EmitPushReg(c, o.reg);
        return;
    case X86OperandKind::kNone:
        break;
    }
    X86SlowOperandFault();
}

CERFENGINE_FORCEINLINE uint8_t* X86EmitTlbLookup(X86Lowering& l, uint8_t* c, const MemAttr& m,
                                                 TlbAccess access) {
    const X86SoftTlb& tlb  = *l.Target().tlb;
    const BlockLink&  link = *l.Target().link;
    if (m.unpriv) return tlb.EmitUserLookup(c, access);
    if (link.translation_off) return tlb.EmitMissCall(c, access);
    return tlb.EmitLookup(c, link.privilege, access);
}

CERFENGINE_FORCEINLINE void X86PrepareMemoryTail(X86Lowering& l, X86MemoryTail& t,
                                                 const MemAttr& m, bool store) {
    const MemorySlowModel& slow   = l.Target().model->slow;
    const bool             has_wb = t.wb.kind != X86OperandKind::kNone;
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
    d.wb_add             = t.wb.kind == X86OperandKind::kRecompute ? t.wb.imm : 0u;
    d.align              = m.align;
    d.unpriv             = m.unpriv;
    d.writeback_on_fault = m.writeback_on_fault;
    d.back_out           = m.back_out;
}

}
