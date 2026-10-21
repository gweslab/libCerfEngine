#include "../../emit_fault.h"
#include <cerfengine/force_inline.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_pointer.h"
#include "../../x86_emit_shift.h"
#include "x86_lower_fns.h"
#include "x86_memory_operands.h"
#include "x86_soft_tlb.h"

namespace cerfengine::cerfir {

namespace {

using x86::kEax;
using x86::kEcx;
using x86::kEdx;
using x86::kStateReg;

CERFENGINE_FORCEINLINE void EmitBasedAddress(uint8_t*& c, const MemAttr& m) {
    x86::EmitMovRegBaseDisp32(c, kEcx, kStateReg, m.base_slot);
    if (m.pre_index && m.offset != 0) x86::EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(m.offset));
}

CERFENGINE_FORCEINLINE void EmitSlowChecks(uint8_t*& c, const MemAttr& m, X86MemoryTail& t) {
    using namespace x86;
    if (m.slow_mask != 0u) {
        EmitTestRegImm32(c, kEcx, m.slow_mask);
        t.align[0] = EmitJnzLabel32(c);
    }
    if (m.cross_granule != 0u) {
        EmitMovRegReg(c, kEdx, kEcx);
        EmitAndRegImm32(c, kEdx, m.cross_granule - 1u);
        EmitAddRegImm32(c, kEdx, m.width_bytes - 1u);
        EmitTestRegImm32(c, kEdx, m.cross_granule);
        t.align[1] = EmitJnzLabel32(c);
    }
}

CERFENGINE_FORCEINLINE void EmitHostLoad(uint8_t*& c, uint32_t width_bytes, bool sign) {
    using namespace x86;
    switch (width_bytes) {
    case 1u:
        if (sign) EmitMovsxRegByteBase(c, kEdx, kEax);
        else      EmitMovzxRegByteBase(c, kEdx, kEax);
        break;
    case 2u:
        if (sign) EmitMovsxRegWordBase(c, kEdx, kEax);
        else      EmitMovzxRegWordBase(c, kEdx, kEax);
        break;
    default:
        EmitMovRegBase(c, kEdx, kEax);
        break;
    }
}

CERFENGINE_FORCEINLINE void EmitHostStore(uint8_t*& c, uint32_t width_bytes) {
    using namespace x86;
    switch (width_bytes) {
    case 1u:  EmitMovBaseReg8(c, kEax, kDl); break;
    case 2u:  EmitMovBaseReg16(c, kEax, kEdx); break;
    default:  EmitMovBaseReg(c, kEax, kEdx); break;
    }
}

CERFENGINE_FORCEINLINE void EmitExtensionWord(uint8_t*& c, const MemAttr& m, uint8_t value_reg,
                                              uint8_t scratch) {
    using namespace x86;
    if (m.result_slot2 == kNoSlot) return;
    if (!m.sign) {
        EmitMovBaseDisp32Imm32(c, kStateReg, m.result_slot2, 0u);
        return;
    }
    EmitMovRegReg(c, scratch, value_reg);
    EmitSarReg32Imm(c, scratch, 31u);
    EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot2, scratch);
}

CERFENGINE_FORCEINLINE void EmitSuccessWriteback(uint8_t*& c, const MemAttr& m, bool based,
                                                 const X86Operand& wb) {
    if (m.complete) return;
    if (based && m.pre_index) {
        if (wb.kind != X86OperandKind::kNone) {
            x86::EmitMovBaseDisp32Reg(c, kStateReg, m.writeback_slot, kEcx);
        }
        return;
    }
    X86EmitWriteback(c, wb, m.writeback_slot);
}

CERFENGINE_FORCEINLINE void FinishAccess(X86Lowering& l, Ref w, const X86Operand& wb,
                                         const MemAttr& m) {
    X86RegAlloc& ra = l.Regs();
    ra.ReleaseTemp(kEcx);
    l.ClearFlagSource();
    if (w == kNoRef) return;
    if (!m.complete && wb.kind != X86OperandKind::kConst) ra.NoteStored(w, m.writeback_slot);
    ra.Consume(w);
}

CERFENGINE_FORCEINLINE void PlaceAccessAddress(X86Lowering& l, Ref address, bool based) {
    if (based) {
        l.Regs().ClaimFixed(kEcx);
    } else {
        l.Regs().CopyIntoFixed(address, kEcx);
    }
}

CERFENGINE_FORCEINLINE bool LeanShape(X86Lowering& l, const MemAttr& m) {
    return m.base_slot != kNoSlot && m.writeback_slot == kNoSlot && !m.unpriv && !m.complete &&
           l.Regs().Idle();
}

CERFENGINE_FORCEINLINE void LeanLoad(X86Lowering& l, const MemAttr& m) {
    X86MemoryTail& t = l.Tails().New();
    X86PrepareMemoryTail(l, t, m,false);
    uint8_t* c = l.Cursor();
    EmitBasedAddress(c, m);
    EmitSlowChecks(c, m, t);
    c = X86EmitTlbLookup(l, c, m,TlbAccess::kRead);
    x86::EmitTestPtrRegReg(c, kEax, kEax);
    t.abort = x86::EmitJzLabel32(c);
    EmitHostLoad(c, m.width_bytes, m.sign);
    t.resume = c;
    x86::EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot, kEdx);
    EmitExtensionWord(c, m, kEdx, kEax);
    l.Cursor() = c;
    l.ClearFlagSource();
}

CERFENGINE_FORCEINLINE void LeanStore(X86Lowering& l, const MemAttr& m, Ref value) {
    X86MemoryTail& t = l.Tails().New();
    if (IsConstRef(value)) {
        t.value = X86Operand{X86OperandKind::kConst, 0u, 0,
                             static_cast<uint32_t>(l.GetBlock().ConstValue(value)), nullptr};
    } else {
        t.value = X86Operand{X86OperandKind::kSlot, 0u, SlotRefDisp(value), 0u, nullptr};
    }
    X86PrepareMemoryTail(l, t, m,true);
    uint8_t* c = l.Cursor();
    EmitBasedAddress(c, m);
    EmitSlowChecks(c, m, t);
    c = X86EmitTlbLookup(l, c, m,TlbAccess::kWrite);
    x86::EmitTestPtrRegReg(c, kEax, kEax);
    t.abort = x86::EmitJzLabel32(c);
    X86LoadOperand(c, t.value, kEdx);
    EmitHostStore(c, m.width_bytes);
    t.resume   = c;
    l.Cursor() = c;
    l.ClearFlagSource();
}

void SoftTlbLoad(X86Lowering& l, Ref ref, Ref address, Ref writeback, const MemAttr& m) {
    X86RegAlloc& ra    = l.Regs();
    const bool   based = m.base_slot != kNoSlot;
    X86CheckAccessWidth(m.width_bytes);
    if (ref == kNoRef && m.result_slot != kNoSlot && LeanShape(l, m)) {
        LeanLoad(l, m);
        return;
    }

    ra.BeforeFlagsClobber();
    PlaceAccessAddress(l, address, based);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    if (m.result_slot2 != kNoSlot) ra.BeforeSlotWrite(m.result_slot2, 4u);
    if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    X86MemoryTail& t = l.Tails().New();
    if (based) {
        X86RecomputedWriteback(m, t.wb);
    } else {
        X86PlaceOperand(l, writeback, t.wb);
    }
    X86PrepareMemoryTail(l, t, m,false);
    ra.Evict(RegBit(kEax) | RegBit(kEdx));

    uint8_t* c = l.Cursor();
    if (based) EmitBasedAddress(c, m);
    EmitSlowChecks(c, m, t);
    c = X86EmitTlbLookup(l, c, m,TlbAccess::kRead);
    x86::EmitTestPtrRegReg(c, kEax, kEax);
    t.abort = x86::EmitJzLabel32(c);
    EmitHostLoad(c, m.width_bytes, m.sign);
    EmitSuccessWriteback(c, m, based, t.wb);
    t.resume = c;
    if (m.result_slot != kNoSlot) x86::EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot, kEdx);
    EmitExtensionWord(c, m, kEdx, kEax);
    l.Cursor() = c;

    FinishAccess(l, writeback, t.wb, m);
    if (ref == kNoRef || ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, kEdx, true);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

void SoftTlbStore(X86Lowering& l, Ref address, Ref value, Ref writeback, const MemAttr& m) {
    X86RegAlloc& ra    = l.Regs();
    const bool   based = m.base_slot != kNoSlot;
    X86CheckAccessWidth(m.width_bytes);
    if ((IsConstRef(value) || IsSlotRef(value)) && LeanShape(l, m)) {
        LeanStore(l, m, value);
        return;
    }

    ra.BeforeFlagsClobber();
    PlaceAccessAddress(l, address, based);
    if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    X86MemoryTail& t = l.Tails().New();
    X86PlaceOperand(l, value, t.value);
    if (based) {
        X86RecomputedWriteback(m, t.wb);
    } else {
        X86PlaceOperand(l, writeback, t.wb);
    }
    X86PrepareMemoryTail(l, t, m,true);
    ra.Evict(RegBit(kEax) | RegBit(kEdx));

    uint8_t* c = l.Cursor();
    if (based) EmitBasedAddress(c, m);
    EmitSlowChecks(c, m, t);
    c = X86EmitTlbLookup(l, c, m,TlbAccess::kWrite);
    x86::EmitTestPtrRegReg(c, kEax, kEax);
    t.abort = x86::EmitJzLabel32(c);
    X86LoadOperand(c, t.value, kEdx);
    EmitHostStore(c, m.width_bytes);
    EmitSuccessWriteback(c, m, based, t.wb);
    t.resume   = c;
    l.Cursor() = c;

    FinishAccess(l, writeback, t.wb, m);
    ra.Consume(value);
}

void SoftTlbSwap(X86Lowering& l, Ref ref) {
    using namespace x86;
    const Block&   b    = l.GetBlock();
    const Inst&    inst = b.At(ref);
    const MemAttr& m    = b.MemAttrAt(inst.aux);
    X86RegAlloc&   ra   = l.Regs();
    if (!m.complete || (m.width_bytes != 1u && m.width_bytes != 4u)) {
        throw EmitFault("CerfIR x86 lowering: a swap must be a complete-kind 1- or 4-byte access");
    }

    ra.BeforeFlagsClobber();
    ra.CopyIntoFixed(inst.args[0], kEcx);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    X86MemoryTail& t = l.Tails().New();
    X86PlaceOperand(l, inst.args[1], t.value);
    X86PrepareMemoryTail(l, t, m,false);
    ra.Evict(RegBit(kEax) | RegBit(kEdx));

    uint8_t* c = l.Cursor();
    EmitSlowChecks(c, m, t);
    c = X86EmitTlbLookup(l, c, m,TlbAccess::kReadWrite);
    EmitTestPtrRegReg(c, kEax, kEax);
    t.abort = EmitJzLabel32(c);
    if (m.width_bytes == 1u) {
        EmitMovzxRegByteBase(c, kEdx, kEax);
        X86LoadOperand(c, t.value, kEcx);
        EmitMovBaseReg8(c, kEax, kCl);
    } else {
        EmitMovRegBase(c, kEdx, kEax);
        X86LoadOperand(c, t.value, kEcx);
        EmitMovBaseReg(c, kEax, kEcx);
    }
    if (m.result_slot != kNoSlot) EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot, kEdx);
    t.resume   = c;
    l.Cursor() = c;

    ra.ReleaseTemp(kEcx);
    l.ClearFlagSource();
    ra.Consume(inst.args[1]);
    if (ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, kEdx, true);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

[[noreturn]] void NoMemoryModel() {
    throw EmitFault("CerfIR x86 lowering: the guest has no lowering for this memory op");
}

}

bool X86Lowering::EmitLeanLoad(const MemAttr& m) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (Target().model->memory != MemoryModelKind::kSoftTlb || m.result_slot == kNoSlot ||
        !LeanShape(*this, m)) {
        return false;
    }
    X86CheckAccessWidth(m.width_bytes);
    LeanLoad(*this, m);
    return true;
}

bool X86Lowering::EmitLeanStore(const MemAttr& m, Ref value) {
    if (!HasRoom()) throw X86ArenaFull{};
    if (Target().model->memory != MemoryModelKind::kSoftTlb ||
        !(IsConstRef(value) || IsSlotRef(value)) || !LeanShape(*this, m)) {
        return false;
    }
    X86CheckAccessWidth(m.width_bytes);
    LeanStore(*this, m, value);
    return true;
}

void X86EmitLoad(X86Lowering& l, Ref ref, Ref address, Ref writeback, uint32_t attr) {
    const MemAttr& m = l.GetBlock().MemAttrAt(attr);
    switch (l.Target().model->memory) {
    case MemoryModelKind::kSoftTlb: SoftTlbLoad(l, ref, address, writeback, m); return;
    case MemoryModelKind::kHelper:  X86CallLoad(l, ref, address, m); return;
    case MemoryModelKind::kNone:    break;
    }
    NoMemoryModel();
}

void X86EmitStore(X86Lowering& l, Ref address, Ref value, Ref writeback, uint32_t attr) {
    const MemAttr& m = l.GetBlock().MemAttrAt(attr);
    switch (l.Target().model->memory) {
    case MemoryModelKind::kSoftTlb: SoftTlbStore(l, address, value, writeback, m); return;
    case MemoryModelKind::kHelper:  X86CallStore(l, address, value, m); return;
    case MemoryModelKind::kNone:    break;
    }
    NoMemoryModel();
}

void X86LowerLoad(X86Lowering& l, Ref ref) {
    const Inst& inst = l.GetBlock().At(ref);
    X86EmitLoad(l, ref, inst.args[0], inst.args[1], inst.aux);
}

void X86LowerSwap(X86Lowering& l, Ref ref) {
    if (l.Target().model->memory == MemoryModelKind::kSoftTlb) {
        SoftTlbSwap(l, ref);
        return;
    }
    NoMemoryModel();
}

}
