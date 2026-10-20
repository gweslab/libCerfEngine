#pragma once

#include <cstdint>

#include "cerfir_build_core.h"

namespace cerfengine::cerfir {

class MemoryBuild : public BuildCore {
public:
    using BuildCore::BuildCore;

    CERFENGINE_FORCEINLINE MemAttr& NewMemAttr(uint32_t* index) { return block_.NewMemAttr(index); }
    CERFENGINE_FORCEINLINE WordList& NewWordList(uint32_t* index) { return block_.NewWordList(index); }
    void TransferWords(uint32_t attr, uint32_t list);

    CERFENGINE_FORCEINLINE Ref Load(Type type, Ref address, Ref writeback, uint32_t attr) {
        const MemAttr& m = block_.MemAttrAt(attr);
        CheckAddressing(Op::kLoad, address, writeback, m);
        return CheckedLoad(type, address, writeback, attr, m);
    }
    CERFENGINE_FORCEINLINE Ref LoadBased(Type type, uint32_t attr, const MemAttr& m) {
        if (m.base_slot == kNoSlot) AddressingFault(Op::kLoad, kNoRef, m);
        return CheckedLoad(type, kNoRef, kNoRef, attr, m);
    }
    CERFENGINE_FORCEINLINE void Store(Ref address, Ref value, Ref writeback, uint32_t attr) {
        const MemAttr& m = block_.MemAttrAt(attr);
        CheckAddressing(Op::kStore, address, writeback, m);
        DirectStore(address, value, writeback, attr, m);
    }
    CERFENGINE_FORCEINLINE void StoreBased(Ref value, uint32_t attr, const MemAttr& m) {
        if (m.base_slot == kNoSlot) AddressingFault(Op::kStore, kNoRef, m);
        DirectStore(kNoRef, value, kNoRef, attr, m);
    }
    CERFENGINE_FORCEINLINE bool LeanLoad(const MemAttr& m) {
        if (record_ || !lowering_.EmitLeanLoad(m)) return false;
        cycle_site_    = nullptr;
        last_terminal_ = false;
        return true;
    }
    CERFENGINE_FORCEINLINE bool LeanStore(const MemAttr& m, Ref value) {
        if (record_ || !lowering_.EmitLeanStore(m, value)) return false;
        cycle_site_    = nullptr;
        last_terminal_ = false;
        return true;
    }
    CERFENGINE_FORCEINLINE bool SimpleLoad(uint32_t width, bool sign, uint32_t base_slot,
                                           uint32_t offset, uint32_t result_slot,
                                           uint32_t result_slot2, uint32_t guest_pc) {
        if (record_ ||
            !lowering_.EmitSimpleLoad(width, sign, static_cast<int32_t>(base_slot),
                                      static_cast<int32_t>(offset), static_cast<int32_t>(result_slot),
                                      static_cast<int32_t>(result_slot2), guest_pc)) {
            return false;
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        return true;
    }
    CERFENGINE_FORCEINLINE bool SimpleStore(uint32_t width, uint32_t base_slot, uint32_t offset,
                                            Ref value, uint32_t guest_pc) {
        if (record_ || !lowering_.EmitSimpleStore(width, static_cast<int32_t>(base_slot),
                                                  static_cast<int32_t>(offset), value, guest_pc)) {
            return false;
        }
        cycle_site_    = nullptr;
        last_terminal_ = false;
        return true;
    }
    Ref Swap(Type type, Ref address, Ref value, uint32_t attr);

private:
    CERFENGINE_FORCEINLINE Ref CheckedLoad(Type type, Ref address, Ref writeback, uint32_t attr,
                                           const MemAttr& m) {
        CheckValueWidth(Op::kLoad, type, m);
        if (m.result_slot != kNoSlot) {
            DirectLoad(type, address, writeback, attr, m);
            return kNoRef;
        }
        const Ref r = Build(Op::kLoad, type, address, writeback, kNoRef, attr, 0u, kNoDest, 0u, 1u);
        NoteMemorySlots(m);
        return r;
    }

    CERFENGINE_FORCEINLINE void DirectLoad(Type type, Ref address, Ref writeback, uint32_t attr,
                                           const MemAttr& m) {
        if (IsSlotRef(address)) address = ResolveSlotArg(Op::kLoad, address, 0u);
        if (IsSlotRef(writeback)) writeback = ResolveSlotArg(Op::kLoad, writeback, 1u);
        cycle_site_    = nullptr;
        last_terminal_ = false;
        if (record_) block_.Append(Op::kLoad, type, address, writeback, kNoRef, 0u, attr, 0u, 0u);
        lowering_.EmitLoad(address, writeback, attr);
        NoteMemorySlots(m);
    }

    CERFENGINE_FORCEINLINE void DirectStore(Ref address, Ref value, Ref writeback, uint32_t attr,
                                            const MemAttr& m) {
        CheckValueWidth(Op::kStore, block_.TypeOf(value), m);
        if (IsSlotRef(address)) address = ResolveSlotArg(Op::kStore, address, 0u);
        if (IsSlotRef(value)) value = ResolveSlotArg(Op::kStore, value, 1u);
        if (IsSlotRef(writeback)) writeback = ResolveSlotArg(Op::kStore, writeback, 2u);
        cycle_site_    = nullptr;
        last_terminal_ = false;
        if (record_) {
            block_.Append(Op::kStore, Type::kVoid, address, value, writeback, 0u, attr, 0u, 0u);
        }
        lowering_.EmitStore(address, value, writeback, attr);
        NoteMemorySlots(m);
    }

    CERFENGINE_FORCEINLINE void CheckAddressing(Op op, Ref address, Ref writeback,
                                                const MemAttr& attr) const {
        const bool based = attr.base_slot != kNoSlot;
        if (based ? (address != kNoRef || writeback != kNoRef)
                  : (address == kNoRef ||
                     (writeback != kNoRef) != (attr.writeback_slot != kNoSlot))) {
            AddressingFault(op, address, attr);
        }
    }
    [[noreturn]] static void AddressingFault(Op op, Ref address, const MemAttr& attr);
    CERFENGINE_FORCEINLINE static void CheckValueWidth(Op op, Type value, const MemAttr& attr) {
        if ((value == Type::kI64) != (attr.width_bytes == 8u)) WidthMismatch(op);
    }
    [[noreturn]] static void WidthMismatch(Op op);
};

}
