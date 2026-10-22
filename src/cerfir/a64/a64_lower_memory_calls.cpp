#include <cstdint>

#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"
#include "a64_memory_operands.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kAddressArg = a64::kX0;
constexpr uint8_t kSecondArg  = a64::kX1;
constexpr uint8_t kThirdArg   = a64::kX2;

CERFENGINE_FORCEINLINE void BeginCallAccess(A64Lowering& l, const MemAttr& m) {
    const Block&             b  = l.GetBlock();
    const HelperMemoryModel& h  = l.Target().model->helper;
    A64RegAlloc&             ra = l.Regs();
    ra.BeforeCall();
    if (h.store_pc) {
        ra.BeforeSlotWrite(b.PcSlot(), 4u);
        l.Pins().StoreImm(l.Cursor(), b.PcSlot(), m.guest_pc, kA64Tmp0);
    }
    l.Pins().Park(l.Cursor());
}

CERFENGINE_FORCEINLINE void PlaceCallAddress(A64Lowering& l, Ref address, const MemAttr& m) {
    if (m.base_slot == kNoSlot) {
        l.Regs().CopyIntoFixed(address, kAddressArg);
        return;
    }
    l.Regs().ClaimFixed(kAddressArg);
    uint8_t*& c = l.Cursor();
    l.Pins().Load(c, kAddressArg, m.base_slot);
    A64AddOffset(c, kAddressArg, m.offset);
}

CERFENGINE_FORCEINLINE void CheckCallShape(const MemAttr& m) {
    A64CheckAccessWidth(m.width_bytes);
    if (m.writeback_slot != kNoSlot || !m.pre_index) {
        throw EmitFault("CerfIR A64 lowering: a helper-model access has no writeback form");
    }
}

CERFENGINE_FORCEINLINE bool LeanCallShape(A64Lowering& l, Ref address) {
    return (address == kNoRef || IsConstRef(address)) && l.Regs().Idle();
}

CERFENGINE_FORCEINLINE const void* LoadHelper(const A64Lowering& l, uint32_t width_bytes) {
    return l.Target().model->helper.load[l.Target().link->privilege][HelperWidthIndex(width_bytes)];
}

CERFENGINE_FORCEINLINE const void* StoreHelper(const A64Lowering& l, uint32_t width_bytes) {
    return l.Target().model->helper.store[l.Target().link->privilege][HelperWidthIndex(width_bytes)];
}

struct SimpleAccess {
    uint8_t  width_bytes;
    bool     sign;
    int32_t  base_slot;
    int32_t  offset;
    int32_t  result_slot;
    int32_t  result_slot2;
    uint32_t guest_pc;
};

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallPc(A64Lowering& l, uint8_t*& c, const Access& m) {
    if (l.Target().model->helper.store_pc) {
        l.Pins().StoreImm(c, l.GetBlock().PcSlot(), m.guest_pc, kA64Tmp0);
    }
    l.Pins().Park(c);
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallAddress(A64Lowering& l, uint8_t*& c, Ref address,
                                                const Access& m) {
    if (address != kNoRef) {
        a64::EmitMovImm32(c, kAddressArg, static_cast<uint32_t>(l.GetBlock().ConstValue(address)));
        return;
    }
    l.Pins().Load(c, kAddressArg, m.base_slot);
    A64AddOffset(c, kAddressArg, m.offset);
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitCallResult(A64PinnedSlots& p, uint8_t*& c, const Access& m) {
    if (m.sign && m.width_bytes == 1u) a64::EmitSxtb(c, a64::kX0, a64::kX0);
    if (m.sign && m.width_bytes == 2u) a64::EmitSxth(c, a64::kX0, a64::kX0);
    if (m.result_slot != kNoSlot) p.Store(c, a64::kX0, m.result_slot);
    if (m.result_slot2 == kNoSlot) return;
    if (m.sign) {
        a64::EmitAsrImm(c, kA64Tmp0, a64::kX0, 31u);
        p.Store(c, kA64Tmp0, m.result_slot2);
    } else {
        p.Store(c, a64::kZr, m.result_slot2);
    }
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallLoad(A64Lowering& l, Ref address, const Access& m) {
    const HelperMemoryModel& h = l.Target().model->helper;
    uint8_t*                 c = l.Cursor();
    EmitLeanCallPc(l, c, m);
    EmitLeanCallAddress(l, c, address, m);
    a64::EmitMovPtr(c, kSecondArg, h.context);
    a64::EmitCall(c, LoadHelper(l, m.width_bytes));
    l.Pins().Unpark();
    EmitCallResult(l.Pins(), c, m);
    l.Cursor() = c;
    l.ClearFlagSource();
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallStore(A64Lowering& l, Ref address, Ref value, const Access& m) {
    const HelperMemoryModel& h = l.Target().model->helper;
    uint8_t*                 c = l.Cursor();
    EmitLeanCallPc(l, c, m);
    EmitLeanCallAddress(l, c, address, m);
    if (IsConstRef(value)) {
        a64::EmitMovImm32(c, kSecondArg, static_cast<uint32_t>(l.GetBlock().ConstValue(value)));
    } else {
        l.Pins().Load(c, kSecondArg, SlotRefDisp(value));
    }
    a64::EmitMovPtr(c, kThirdArg, h.context);
    a64::EmitCall(c, StoreHelper(l, m.width_bytes));
    l.Pins().Unpark();
    l.Cursor() = c;
    l.ClearFlagSource();
}

CERFENGINE_FORCEINLINE bool SimpleCallAccess(A64Lowering& l, uint32_t width) {
    return l.Target().model->memory == MemoryModelKind::kHelper && l.Regs().Idle() &&
           (width == 1u || width == 2u || width == 4u);
}

}

void A64CallLoad(A64Lowering& l, Ref ref, Ref address, const MemAttr& m) {
    const HelperMemoryModel& h  = l.Target().model->helper;
    A64RegAlloc&             ra = l.Regs();
    CheckCallShape(m);
    if (ref == kNoRef && LeanCallShape(l, address)) {
        EmitLeanCallLoad(l, address, m);
        return;
    }

    BeginCallAccess(l, m);
    PlaceCallAddress(l, address, m);
    ra.ClaimFixed(kSecondArg);
    uint8_t*& c = l.Cursor();
    a64::EmitMovPtr(c, kSecondArg, h.context);
    a64::EmitCall(c, LoadHelper(l, m.width_bytes));
    l.Pins().Unpark();
    ra.ReleaseTemp(kAddressArg);
    ra.ReleaseTemp(kSecondArg);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    if (m.result_slot2 != kNoSlot) ra.BeforeSlotWrite(m.result_slot2, 4u);
    EmitCallResult(l.Pins(), c, m);
    l.ClearFlagSource();
    if (ref == kNoRef || ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, a64::kX0);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

void A64CallStore(A64Lowering& l, Ref address, Ref value, const MemAttr& m) {
    const HelperMemoryModel& h  = l.Target().model->helper;
    A64RegAlloc&             ra = l.Regs();
    CheckCallShape(m);
    if ((IsConstRef(value) || IsSlotRef(value)) && LeanCallShape(l, address)) {
        EmitLeanCallStore(l, address, value, m);
        return;
    }

    BeginCallAccess(l, m);
    PlaceCallAddress(l, address, m);
    ra.CopyIntoFixed(value, kSecondArg);
    ra.ClaimFixed(kThirdArg);
    uint8_t*& c = l.Cursor();
    a64::EmitMovPtr(c, kThirdArg, h.context);
    a64::EmitCall(c, StoreHelper(l, m.width_bytes));
    l.Pins().Unpark();
    ra.ReleaseTemp(kAddressArg);
    ra.ReleaseTemp(kSecondArg);
    ra.ReleaseTemp(kThirdArg);
    l.ClearFlagSource();
}

bool A64Lowering::EmitSimpleLoad(uint32_t width, bool sign, int32_t base_slot, int32_t offset,
                                 int32_t result_slot, int32_t result_slot2, uint32_t guest_pc) {
    if (!SimpleCallAccess(*this, width)) return false;
    if (!HasRoom()) throw A64ArenaFull{};
    const SimpleAccess m{static_cast<uint8_t>(width), sign, base_slot, offset, result_slot,
                         result_slot2, guest_pc};
    EmitLeanCallLoad(*this, kNoRef, m);
    return true;
}

bool A64Lowering::EmitSimpleStore(uint32_t width, int32_t base_slot, int32_t offset, Ref value,
                                  uint32_t guest_pc) {
    if (!SimpleCallAccess(*this, width) || !(IsConstRef(value) || IsSlotRef(value))) return false;
    if (!HasRoom()) throw A64ArenaFull{};
    const SimpleAccess m{static_cast<uint8_t>(width), false, base_slot, offset, kNoSlot, kNoSlot,
                         guest_pc};
    EmitLeanCallStore(*this, kNoRef, value, m);
    return true;
}

}
