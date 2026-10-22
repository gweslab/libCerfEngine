#include "a64_memory_tails.h"

#include <cstring>

#include <cerfengine/byte_order.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lowering.h"
#include "a64_memory_operands.h"

namespace cerfengine::cerfir {

void A64MemoryTails::Grow() {
    tails_.resize(tails_.empty() ? 16u : tails_.size() * 2u);
    capacity_ = tails_.size();
}

bool A64MemoryTails::Emit(A64Lowering& l) {
    for (size_t i = 0; i < count_; ++i) {
        if (!l.HasRoom()) return false;
        EmitOne(l, tails_[i]);
    }
    for (size_t i = 0; i < stub_count_; ++i) {
        if (!l.HasRoom()) return false;
        EmitMissStub(l, stubs_[i]);
    }
    return true;
}

void A64MemoryTails::EmitMissStub(A64Lowering& l, const A64MissStub& s) {
    uint8_t*       c    = l.Cursor();
    uint8_t* const stub = c;
    l.Pins().EmitStores(c, s.pin_dirty);
    a64::EmitB(c, s.target);
    le::Put32(s.call, 0x94000000u | a64::Imm26(s.call, stub));
    l.Cursor() = c;
}

void A64MemoryTails::RecordBranch(A64MemoryTail& t, const A64PinState& pins) {
    if (!t.pin_recorded) {
        t.pin_recorded = true;
        t.pin_dirty    = pins.dirty;
        t.pin_stale    = pins.stale;
        return;
    }
    t.pin_dirty |= pins.dirty;
    t.pin_stale |= pins.stale;
    if ((t.pin_dirty & t.pin_stale) != 0u) {
        throw EmitFault("CerfIR A64 lowering: a memory tail is reached with a pinned slot dirty on one "
                        "branch and stale on another");
    }
}

void A64MemoryTails::EmitOne(A64Lowering& l, const A64MemoryTail& t) {
    uint8_t*   c     = l.Cursor();
    const bool align = t.align[0] != nullptr || t.align[1] != nullptr;
    if (!t.pin_recorded && (align || t.abort_count != 0u)) {
        throw EmitFault("CerfIR A64 lowering: a memory tail branch has no recorded pinned-slot state");
    }

    uint8_t* to_common = nullptr;
    if (align) {
        if (t.align[0] != nullptr) a64::FixupLabel(t.align[0], c);
        if (t.align[1] != nullptr) a64::FixupLabel(t.align[1], c);
        a64::EmitMovPtr(c, a64::kIp0, t.entries.align);
        to_common = a64::EmitBLabel(c);
    }
    for (uint32_t i = 0; i < t.abort_count; ++i) a64::FixupLabel(t.abort[i], c);
    if (align) {
        a64::EmitMovPtr(c, a64::kIp0, t.entries.abort);
        a64::FixupLabel(to_common, c);
    }
    if (t.push_value && t.value.kind == A64OperandKind::kRecompute) A64SlowOperandFault();
    l.Pins().EmitStores(c, t.pin_dirty);

    const bool     has_wb = t.wb.kind != A64OperandKind::kNone;
    const uint32_t saved  = l.Regs().SavedRegs();
    a64::EmitMovReg(c, a64::kX0, kA64AddressReg);
    if (t.push_value) {
        A64LoadSlowOperand(c, t.value, a64::kX2, saved);
        if (has_wb) A64LoadSlowOperand(c, t.wb, a64::kX3, saved);
    } else if (has_wb) {
        A64LoadSlowOperand(c, t.wb, a64::kX2, saved);
    }
    uint8_t* const desc_site = c;
    a64::EmitNop(c);
    if (align) {
        a64::EmitBlr(c, a64::kIp0);
    } else {
        a64::EmitCall(c, t.entries.abort);
    }
    if (!t.complete) {
        a64::EmitLsrImm(c, kA64ValueReg, a64::kX0, 32u, true);
        if (t.pin_resume_live == 0u) {
            a64::FixupLabel(a64::EmitCbzLabel(c, a64::kX0, false, false), t.resume);
        } else {
            uint8_t* const to_exit = a64::EmitCbzLabel(c, a64::kX0, true, false);
            l.Pins().EmitLoads(c, t.pin_resume_live);
            a64::FixupLabel(a64::EmitBLabel(c), t.resume);
            a64::FixupLabel(to_exit, c);
        }
    }
    a64::EmitLdrX(c, a64::kLr, a64::kSp, kA64ReturnSlot);
    a64::EmitRet(c);

    c += (0u - reinterpret_cast<uintptr_t>(c)) & (alignof(MemorySlowDesc) - 1u);
    std::memcpy(c, &t.desc, sizeof(MemorySlowDesc));
    uint8_t* adr = desc_site;
    a64::EmitAdr(adr, a64::kX1, c);
    l.Cursor() = c + sizeof(MemorySlowDesc);
}

}
