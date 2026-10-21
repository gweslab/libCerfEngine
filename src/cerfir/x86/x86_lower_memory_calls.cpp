#include <cstdint>

#include "../../emit_fault.h"
#include <cerfengine/force_inline.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lower_fns.h"
#include "x86_memory_operands.h"

namespace cerfengine::cerfir {

namespace {

using x86::kEax;
using x86::kEcx;
using x86::kEdx;
using x86::kStateReg;

constexpr uint8_t kAddressArg = x86::kX64 ? kX86ArgRegs[0] : kEcx;

CERFENGINE_FORCEINLINE void BeginCallAccess(X86Lowering& l, const MemAttr& m) {
    const Block&             b  = l.GetBlock();
    const HelperMemoryModel& h  = l.Target().model->helper;
    X86RegAlloc&             ra = l.Regs();
    ra.BeforeCall();
    if (h.store_pc) {
        ra.BeforeSlotWrite(b.PcSlot(), 4u);
        x86::EmitMovBaseDisp32Imm32(l.Cursor(), kStateReg, b.PcSlot(), m.guest_pc);
    }
}

CERFENGINE_FORCEINLINE void PlaceCallAddress(X86Lowering& l, Ref address, const MemAttr& m) {
    if (m.base_slot == kNoSlot) {
        l.Regs().CopyIntoFixed(address, kEcx);
        return;
    }
    l.Regs().ClaimFixed(kEcx);
    uint8_t*& c = l.Cursor();
    x86::EmitMovRegBaseDisp32(c, kEcx, kStateReg, m.base_slot);
    if (m.offset != 0) x86::EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(m.offset));
}

CERFENGINE_FORCEINLINE void CheckCallShape(const MemAttr& m) {
    X86CheckAccessWidth(m.width_bytes);
    if (m.writeback_slot != kNoSlot || !m.pre_index) {
        throw EmitFault("CerfIR x86 lowering: a helper-model access has no writeback form");
    }
}

CERFENGINE_FORCEINLINE bool LeanCallShape(X86Lowering& l, Ref address) {
    return (address == kNoRef || IsConstRef(address)) && l.Regs().Idle();
}

CERFENGINE_FORCEINLINE const void* LoadHelper(const X86Lowering& l, uint32_t width_bytes) {
    return l.Target().model->helper.load[l.Target().link->privilege][HelperWidthIndex(width_bytes)];
}

CERFENGINE_FORCEINLINE const void* StoreHelper(const X86Lowering& l, uint32_t width_bytes) {
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
CERFENGINE_FORCEINLINE void EmitLeanCallPc(X86Lowering& l, uint8_t*& c, const Access& m) {
    if (l.Target().model->helper.store_pc) {
        x86::EmitMovBaseDisp32Imm32(c, kStateReg, l.GetBlock().PcSlot(), m.guest_pc);
    }
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallAddress(X86Lowering& l, uint8_t*& c, Ref address,
                                                const Access& m) {
    using namespace x86;
    if (address != kNoRef) {
        EmitMovRegImm32(c, kAddressArg, static_cast<uint32_t>(l.GetBlock().ConstValue(address)));
        return;
    }
    EmitMovRegBaseDisp32(c, kAddressArg, kStateReg, m.base_slot);
    if (m.offset != 0) EmitAddRegImm32(c, kAddressArg, static_cast<uint32_t>(m.offset));
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitCallResult(uint8_t*& c, const Access& m) {
    using namespace x86;
    if (m.sign && m.width_bytes == 1u) EmitMovsxReg32Reg8(c, kEax, kEax);
    if (m.sign && m.width_bytes == 2u) EmitMovsxReg32Reg16(c, kEax, kEax);
    if (m.result_slot != kNoSlot) EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot, kEax);
    if (m.result_slot2 == kNoSlot) return;
    if (m.sign) {
        EmitCdq(c);
        EmitMovBaseDisp32Reg(c, kStateReg, m.result_slot2, kEdx);
    } else {
        EmitMovBaseDisp32Imm32(c, kStateReg, m.result_slot2, 0u);
    }
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallLoad(X86Lowering& l, Ref address, const Access& m) {
    using namespace x86;
    const HelperMemoryModel& h = l.Target().model->helper;
    uint8_t*                 c = l.Cursor();
    EmitLeanCallPc(l, c, m);
    EmitLeanCallAddress(l, c, address, m);
    if constexpr (kX64) {
        X86RequireKnownCallAbi();
        EmitMovPtrRegImm(c, kX86ArgRegs[1], h.context);
    } else {
        EmitMovRegImm32(c, kEdx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.context)));
    }
    EmitCall(c, LoadHelper(l, m.width_bytes));
    EmitCallResult(c, m);
    l.Cursor() = c;
    l.ClearFlagSource();
}

template <typename Access>
CERFENGINE_FORCEINLINE void EmitLeanCallStore(X86Lowering& l, Ref address, Ref value,
                                              const Access& m) {
    using namespace x86;
    const HelperMemoryModel& h = l.Target().model->helper;
    uint8_t*                 c = l.Cursor();
    constexpr uint8_t value_reg = kX64 ? kX86ArgRegs[1] : kEdx;
    if constexpr (kX64) X86RequireKnownCallAbi();
    EmitLeanCallPc(l, c, m);
    if constexpr (!kX64) {
        EmitPush32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.context)));
    }
    EmitLeanCallAddress(l, c, address, m);
    if (IsConstRef(value)) {
        EmitMovRegImm32(c, value_reg, static_cast<uint32_t>(l.GetBlock().ConstValue(value)));
    } else {
        EmitMovRegBaseDisp32(c, value_reg, kStateReg, SlotRefDisp(value));
    }
    if constexpr (kX64) EmitMovPtrRegImm(c, kX86ArgRegs[2], h.context);
    EmitCall(c, StoreHelper(l, m.width_bytes));
    l.Cursor() = c;
    l.ClearFlagSource();
}

CERFENGINE_FORCEINLINE bool SimpleCallAccess(X86Lowering& l, uint32_t width) {
    return l.Target().model->memory == MemoryModelKind::kHelper && l.Regs().Idle() &&
           (width == 1u || width == 2u || width == 4u);
}

}

void X86CallLoad(X86Lowering& l, Ref ref, Ref address, const MemAttr& m) {
    using namespace x86;
    const HelperMemoryModel& h  = l.Target().model->helper;
    X86RegAlloc&             ra = l.Regs();
    CheckCallShape(m);
    if (ref == kNoRef && LeanCallShape(l, address)) {
        EmitLeanCallLoad(l, address, m);
        return;
    }

    BeginCallAccess(l, m);
    PlaceCallAddress(l, address, m);
    ra.ClaimFixed(kEdx);
    uint8_t*& c = l.Cursor();
    if constexpr (kX64) {
        X86RequireKnownCallAbi();
        EmitMovRegReg(c, kX86ArgRegs[0], kEcx);
        EmitMovPtrRegImm(c, kX86ArgRegs[1], h.context);
    } else {
        EmitMovRegImm32(c, kEdx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.context)));
    }
    EmitCall(c, LoadHelper(l, m.width_bytes));
    ra.ReleaseTemp(kEcx);
    ra.ReleaseTemp(kEdx);
    if (m.result_slot != kNoSlot) ra.BeforeSlotWrite(m.result_slot, 4u);
    if (m.result_slot2 != kNoSlot) ra.BeforeSlotWrite(m.result_slot2, 4u);
    EmitCallResult(c, m);
    l.ClearFlagSource();
    if (ref == kNoRef || ra.Remaining(ref) == 0u) return;
    ra.DefineInReg(ref, kEax, true);
    if (m.result_slot != kNoSlot) ra.NoteStored(ref, m.result_slot);
}

void X86CallStore(X86Lowering& l, Ref address, Ref value, const MemAttr& m) {
    using namespace x86;
    const HelperMemoryModel& h  = l.Target().model->helper;
    X86RegAlloc&             ra = l.Regs();
    CheckCallShape(m);
    if ((IsConstRef(value) || IsSlotRef(value)) && LeanCallShape(l, address)) {
        EmitLeanCallStore(l, address, value, m);
        return;
    }

    BeginCallAccess(l, m);
    if constexpr (!kX64) {
        EmitPush32(l.Cursor(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.context)));
    }
    PlaceCallAddress(l, address, m);
    ra.CopyIntoFixed(value, kEdx);
    if constexpr (kX64) {
        X86RequireKnownCallAbi();
        uint8_t*& c = l.Cursor();
        EmitMovRegReg(c, kX86ArgRegs[0], kEcx);
        EmitMovRegReg(c, kX86ArgRegs[1], kEdx);
        EmitMovPtrRegImm(c, kX86ArgRegs[2], h.context);
    }
    EmitCall(l.Cursor(), StoreHelper(l, m.width_bytes));
    ra.ReleaseTemp(kEcx);
    ra.ReleaseTemp(kEdx);
    l.ClearFlagSource();
}

bool X86Lowering::EmitSimpleLoad(uint32_t width, bool sign, int32_t base_slot, int32_t offset,
                                 int32_t result_slot, int32_t result_slot2, uint32_t guest_pc) {
    if (!SimpleCallAccess(*this, width)) return false;
    if (!HasRoom()) throw X86ArenaFull{};
    const SimpleAccess m{static_cast<uint8_t>(width), sign, base_slot, offset, result_slot,
                         result_slot2, guest_pc};
    EmitLeanCallLoad(*this, kNoRef, m);
    return true;
}

bool X86Lowering::EmitSimpleStore(uint32_t width, int32_t base_slot, int32_t offset, Ref value,
                                  uint32_t guest_pc) {
    if (!SimpleCallAccess(*this, width) || !(IsConstRef(value) || IsSlotRef(value))) return false;
    if (!HasRoom()) throw X86ArenaFull{};
    const SimpleAccess m{static_cast<uint8_t>(width), false, base_slot, offset, kNoSlot, kNoSlot,
                         guest_pc};
    EmitLeanCallStore(*this, kNoRef, value, m);
    return true;
}

}
