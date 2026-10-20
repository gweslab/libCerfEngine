#include "cerfir_memory_build.h"

namespace cerfengine::cerfir {

void MemoryBuild::AddressingFault(Op op, Ref address, const MemAttr& attr) {
    if (attr.base_slot != kNoSlot) {
        Reject("a base-slot access takes no address or writeback value", op);
    }
    if (address == kNoRef) Reject("an access without a base slot needs an address", op);
    Reject("writeback value and writeback slot disagree", op);
}

void MemoryBuild::WidthMismatch(Op op) {
    Reject("64-bit memory value outside a doubleword access", op);
}

Ref MemoryBuild::Swap(Type type, Ref address, Ref value, uint32_t attr) {
    const MemAttr& m = block_.MemAttrAt(attr);
    if (m.writeback_slot != kNoSlot) Reject("a swap has no writeback", Op::kSwap);
    if (type == Type::kI64 || m.width_bytes == 8u) WidthMismatch(Op::kSwap);
    const Ref r = Emit(Op::kSwap, type, address, value, kNoRef, attr, 0u);
    NoteMemorySlots(m);
    return r;
}

void MemoryBuild::TransferWords(uint32_t attr, uint32_t list) {
    const MemAttr&  m = block_.MemAttrAt(attr);
    const WordList& w = block_.WordListAt(list);
    if (m.base_slot == kNoSlot || !m.complete || m.width_bytes != 4u || w.count == 0u ||
        w.count > kMaxWords) {
        Reject("a word transfer needs a base slot, the complete kind and 1 to 16 words",
               Op::kTransferWords);
    }
    cycle_site_    = nullptr;
    last_terminal_ = false;
    if (record_) {
        block_.Append(Op::kTransferWords, Type::kVoid, kNoRef, kNoRef, kNoRef,
                      static_cast<uint16_t>(list), attr, 0u, 0u);
    }
    lowering_.EmitTransferWords(attr, list);
    if (!record_) return;
    if (!w.store) {
        for (uint32_t k = 0; k < w.count; ++k) NoteSlotWrite(w.slot[k], 4u);
    }
    NoteSlotWrite(m.writeback_slot, 4u);
}

}
