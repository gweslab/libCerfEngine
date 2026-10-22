#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"
#include "a64_memory_operands.h"
#include "a64_soft_tlb.h"

namespace cerfengine::cerfir {

namespace {

CERFENGINE_FORCEINLINE void EmitBasedAddress(A64PinnedSlots& p, uint8_t*& c, const MemAttr& m) {
    p.Load(c, kA64AddressReg, m.base_slot);
    if (m.pre_index) A64AddOffset(c, kA64AddressReg, m.offset);
}

CERFENGINE_FORCEINLINE void EmitSlowChecks(A64Lowering& l, uint8_t*& c, const MemAttr& m,
                                           A64MemoryTail& t) {
    A64PinnedSlots& p = l.Pins();
    if (A64InlineLookup(l, m)) p.Flush(c);
    if (m.slow_mask != 0u) {
        if (a64::LogicImmFits(m.slow_mask, false)) {
            a64::EmitLogicImm(c, a64::kAnds, a64::kZr, kA64AddressReg, m.slow_mask);
        } else {
            a64::EmitMovImm32(c, kA64Tmp1, m.slow_mask);
            a64::EmitTstReg(c, kA64AddressReg, kA64Tmp1);
        }
        A64MemoryTails::RecordBranch(t, p.State());
        t.align[0] = a64::EmitBCondLabel(c, a64::kNe);
    }
    if (m.cross_granule != 0u) {
        a64::EmitLogicImm(c, a64::kAnd, kA64Tmp0, kA64AddressReg, m.cross_granule - 1u);
        a64::EmitAddImm(c, kA64Tmp0, kA64Tmp0, m.width_bytes - 1u);
        a64::EmitLogicImm(c, a64::kAnds, a64::kZr, kA64Tmp0, m.cross_granule);
        A64MemoryTails::RecordBranch(t, p.State());
        t.align[1] = a64::EmitBCondLabel(c, a64::kNe);
    }
}

CERFENGINE_FORCEINLINE void MarkResume(A64PinnedSlots& p, uint8_t* c, A64MemoryTail& t) {
    t.resume          = c;
    t.pin_resume_live = p.Full() & ~p.State().stale;
}

CERFENGINE_FORCEINLINE void EmitHostLoad(uint8_t*& c, uint32_t width_bytes, bool sign) {
    const a64::MemSize size = width_bytes == 1u ? a64::kByte : width_bytes == 2u ? a64::kHalf : a64::kWord;
    const a64::MemOpc  opc  = sign && width_bytes != 4u ? a64::kLoadSigned32 : a64::kLoad;
    a64::EmitLoadStoreImm(c, size, opc, kA64ValueReg, kA64HostReg, 0);
}

CERFENGINE_FORCEINLINE void EmitHostStore(uint8_t*& c, uint32_t width_bytes) {
    const a64::MemSize size = width_bytes == 1u ? a64::kByte : width_bytes == 2u ? a64::kHalf : a64::kWord;
    a64::EmitLoadStoreImm(c, size, a64::kStore, kA64ValueReg, kA64HostReg, 0);
}

CERFENGINE_FORCEINLINE void EmitExtensionWord(A64PinnedSlots& p, uint8_t*& c, const MemAttr& m,
                                              uint8_t value_reg) {
    if (m.result_slot2 == kNoSlot) return;
    if (!m.sign) {
        p.Store(c, a64::kZr, m.result_slot2);
        return;
    }
    a64::EmitAsrImm(c, kA64Tmp0, value_reg, 31u);
    p.Store(c, kA64Tmp0, m.result_slot2);
}

CERFENGINE_FORCEINLINE void EmitSuccessWriteback(A64PinnedSlots& p, uint8_t*& c, const MemAttr& m,
                                                 bool based, const A64Operand& wb) {
    if (m.complete) return;
    if (based && m.pre_index) {
        if (wb.kind != A64OperandKind::kNone) p.Store(c, kA64AddressReg, m.writeback_slot);
        return;
    }
    A64EmitWriteback(p, c, wb, m.writeback_slot);
}

CERFENGINE_FORCEINLINE void FinishAccess(A64Lowering& l, Ref w, const A64Operand& wb,
                                         const MemAttr& m) {
    A64RegAlloc& ra = l.Regs();
    ra.ReleaseTemp(kA64AddressReg);
    l.ClearFlagSource();
    if (w == kNoRef) return;
    if (!m.complete && wb.kind != A64OperandKind::kConst) ra.NoteStored(w, m.writeback_slot);
    ra.Consume(w);
}

CERFENGINE_FORCEINLINE void PlaceAccessAddress(A64Lowering& l, Ref address, bool based) {
    if (based) {
        l.Regs().ClaimFixed(kA64AddressReg);
    } else {
        l.Regs().CopyIntoFixed(address, kA64AddressReg);
    }
}

CERFENGINE_FORCEINLINE bool LeanShape(A64Lowering& l, const MemAttr& m) {
    return m.base_slot != kNoSlot && m.writeback_slot == kNoSlot && !m.unpriv && !m.complete &&
           l.Regs().Idle();
}

CERFENGINE_FORCEINLINE uint8_t* EmitAbortBranch(A64PinnedSlots& p, uint8_t*& c, A64MemoryTail& t) {
    A64MemoryTails::RecordBranch(t, p.State());
    uint8_t* const site = a64::EmitCbzLabel(c, kA64HostReg, false, true);
    t.abort[t.abort_count++] = site;
    return site;
}

CERFENGINE_FORCEINLINE void LeanLoad(A64Lowering& l, const MemAttr& m) {
    A64PinnedSlots& p = l.Pins();
    A64MemoryTail&  t = l.Tails().New();
    A64PrepareMemoryTail(l, t, m, false);
    uint8_t* c = l.Cursor();
    EmitBasedAddress(p, c, m);
    EmitSlowChecks(l, c, m, t);
    c = A64EmitTlbLookup(l, c, m, TlbAccess::kRead);
    EmitAbortBranch(p, c, t);
    EmitHostLoad(c, m.width_bytes, m.sign);
    MarkResume(p, c, t);
    p.Store(c, kA64ValueReg, m.result_slot);
    EmitExtensionWord(p, c, m, kA64ValueReg);
    l.Cursor() = c;
    l.ClearFlagSource();
}

CERFENGINE_FORCEINLINE void LeanStore(A64Lowering& l, const MemAttr& m, Ref value) {
    A64MemoryTail& t = l.Tails().New();
    if (IsConstRef(value)) {
        t.value = A64Operand{A64OperandKind::kConst, 0u, 0,
                             static_cast<uint32_t>(l.GetBlock().ConstValue(value))};
    } else {
        t.value = A64Operand{A64OperandKind::kSlot, 0u, SlotRefDisp(value), 0u};
    }
    A64PrepareMemoryTail(l, t, m, true);
    A64PinnedSlots& p = l.Pins();
    uint8_t*        c = l.Cursor();
    EmitBasedAddress(p, c, m);
    EmitSlowChecks(l, c, m, t);
    c = A64EmitTlbLookup(l, c, m, TlbAccess::kWrite);
    EmitAbortBranch(p, c, t);
    A64LoadOperand(p, c, t.value, kA64ValueReg);
    EmitHostStore(c, m.width_bytes);
    MarkResume(p, c, t);
    l.Cursor() = c;
    l.ClearFlagSource();
}

void SoftTlbLoad(A64Lowering& l, Ref ref, Ref address, Ref writeback, const MemAttr& m) {
    A64RegAlloc& ra    = l.Regs();
    const bool   based = m.base_slot != kNoSlot;
    A64CheckAccessWidth(m.width_bytes);
    if (ref == kNoRef && m.result_slot != kNoSlot && LeanShape(l, m)) {
        LeanLoad(l, m);
        return;
    }

    ra.BeforeFlagsClobber();
    PlaceAccessAddress(l, address, based);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    if (m.result_slot2 != kNoSlot) ra.BeforeSlotWrite(m.result_slot2, 4u);
    if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    A64MemoryTail& t = l.Tails().New();
    if (based) {
        A64RecomputedWriteback(m, t.wb);
    } else {
        A64PlaceOperand(l, writeback, t.wb);
    }
    A64PrepareMemoryTail(l, t, m, false);
    ra.Evict(kA64LookupClobberRegs);

    A64PinnedSlots& p = l.Pins();
    uint8_t*        c = l.Cursor();
    if (based) EmitBasedAddress(p, c, m);
    EmitSlowChecks(l, c, m, t);
    c = A64EmitTlbLookup(l, c, m, TlbAccess::kRead);
    EmitAbortBranch(p, c, t);
    EmitHostLoad(c, m.width_bytes, m.sign);
    EmitSuccessWriteback(p, c, m, based, t.wb);
    MarkResume(p, c, t);
    if (m.result_slot != kNoSlot) p.Store(c, kA64ValueReg, m.result_slot);
    EmitExtensionWord(p, c, m, kA64ValueReg);
    l.Cursor() = c;

    FinishAccess(l, writeback, t.wb, m);
    if (ref == kNoRef || ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, kA64ValueReg);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

void SoftTlbStore(A64Lowering& l, Ref address, Ref value, Ref writeback, const MemAttr& m) {
    A64RegAlloc& ra    = l.Regs();
    const bool   based = m.base_slot != kNoSlot;
    A64CheckAccessWidth(m.width_bytes);
    if ((IsConstRef(value) || IsSlotRef(value)) && LeanShape(l, m)) {
        LeanStore(l, m, value);
        return;
    }

    ra.BeforeFlagsClobber();
    PlaceAccessAddress(l, address, based);
    if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    A64MemoryTail& t = l.Tails().New();
    A64PlaceOperand(l, value, t.value);
    if (based) {
        A64RecomputedWriteback(m, t.wb);
    } else {
        A64PlaceOperand(l, writeback, t.wb);
    }
    A64PrepareMemoryTail(l, t, m, true);
    ra.Evict(kA64LookupClobberRegs);

    A64PinnedSlots& p = l.Pins();
    uint8_t*        c = l.Cursor();
    if (based) EmitBasedAddress(p, c, m);
    EmitSlowChecks(l, c, m, t);
    c = A64EmitTlbLookup(l, c, m, TlbAccess::kWrite);
    EmitAbortBranch(p, c, t);
    A64LoadOperand(p, c, t.value, kA64ValueReg);
    EmitHostStore(c, m.width_bytes);
    EmitSuccessWriteback(p, c, m, based, t.wb);
    MarkResume(p, c, t);
    l.Cursor() = c;

    FinishAccess(l, writeback, t.wb, m);
    ra.Consume(value);
}

void SoftTlbSwap(A64Lowering& l, Ref ref) {
    const Block&   b    = l.GetBlock();
    const Inst&    inst = b.At(ref);
    const MemAttr& m    = b.MemAttrAt(inst.aux);
    A64RegAlloc&   ra   = l.Regs();
    if (!m.complete || (m.width_bytes != 1u && m.width_bytes != 4u)) {
        throw EmitFault("CerfIR A64 lowering: a swap must be a complete-kind 1- or 4-byte access");
    }

    ra.BeforeFlagsClobber();
    ra.CopyIntoFixed(inst.args[0], kA64AddressReg);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    A64MemoryTail& t = l.Tails().New();
    A64PlaceOperand(l, inst.args[1], t.value);
    A64PrepareMemoryTail(l, t, m, false);
    ra.Evict(kA64LookupClobberRegs);

    A64PinnedSlots& p = l.Pins();
    uint8_t*        c = l.Cursor();
    EmitSlowChecks(l, c, m, t);
    c = A64EmitTlbLookup(l, c, m, TlbAccess::kReadWrite);
    EmitAbortBranch(p, c, t);
    const a64::MemSize size = m.width_bytes == 1u ? a64::kByte : a64::kWord;
    a64::EmitLoadStoreImm(c, size, a64::kLoad, kA64ValueReg, kA64HostReg, 0);
    A64LoadOperand(p, c, t.value, kA64Tmp1);
    a64::EmitLoadStoreImm(c, size, a64::kStore, kA64Tmp1, kA64HostReg, 0);
    if (m.result_slot != kNoSlot) p.Store(c, kA64ValueReg, m.result_slot);
    MarkResume(p, c, t);
    l.Cursor() = c;

    ra.ReleaseTemp(kA64AddressReg);
    l.ClearFlagSource();
    ra.Consume(inst.args[1]);
    if (ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, kA64ValueReg);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

[[noreturn]] void NoMemoryModel() {
    throw EmitFault("CerfIR A64 lowering: the guest has no lowering for this memory op");
}

}

bool A64Lowering::EmitLeanLoad(const MemAttr& m) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (Target().model->memory != MemoryModelKind::kSoftTlb || m.result_slot == kNoSlot ||
        !LeanShape(*this, m)) {
        return false;
    }
    A64CheckAccessWidth(m.width_bytes);
    LeanLoad(*this, m);
    return true;
}

bool A64Lowering::EmitLeanStore(const MemAttr& m, Ref value) {
    if (!HasRoom()) throw A64ArenaFull{};
    if (Target().model->memory != MemoryModelKind::kSoftTlb ||
        !(IsConstRef(value) || IsSlotRef(value)) || !LeanShape(*this, m)) {
        return false;
    }
    A64CheckAccessWidth(m.width_bytes);
    LeanStore(*this, m, value);
    return true;
}

void A64EmitLoad(A64Lowering& l, Ref ref, Ref address, Ref writeback, uint32_t attr) {
    const MemAttr& m = l.GetBlock().MemAttrAt(attr);
    switch (l.Target().model->memory) {
    case MemoryModelKind::kSoftTlb: SoftTlbLoad(l, ref, address, writeback, m); return;
    case MemoryModelKind::kHelper:  A64CallLoad(l, ref, address, m); return;
    case MemoryModelKind::kNone:    break;
    }
    NoMemoryModel();
}

void A64EmitStore(A64Lowering& l, Ref address, Ref value, Ref writeback, uint32_t attr) {
    const MemAttr& m = l.GetBlock().MemAttrAt(attr);
    switch (l.Target().model->memory) {
    case MemoryModelKind::kSoftTlb: SoftTlbStore(l, address, value, writeback, m); return;
    case MemoryModelKind::kHelper:  A64CallStore(l, address, value, m); return;
    case MemoryModelKind::kNone:    break;
    }
    NoMemoryModel();
}

void A64LowerLoad(A64Lowering& l, Ref ref) {
    const Inst& inst = l.GetBlock().At(ref);
    A64EmitLoad(l, ref, inst.args[0], inst.args[1], inst.aux);
}

void A64LowerSwap(A64Lowering& l, Ref ref) {
    if (l.Target().model->memory == MemoryModelKind::kSoftTlb) {
        SoftTlbSwap(l, ref);
        return;
    }
    NoMemoryModel();
}

}
