#pragma once

#include <cstdint>
#include <cstring>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>

#include "../../cerfir/cerfir_builder.h"

namespace cerfengine {

class ArmBlockCompiler;

enum class ArmIrFlag : uint8_t { kN, kZ, kC, kV };

constexpr uint32_t kArmIrNoLabel = 0xFFFFFFFFu;

struct ArmIrContext {
    cerfir::Builder*   b;
    BlockContext*      block;
    const DecodedInsn* d;
    ArmBlockCompiler*  compiler;
};

cerfir::Ref ArmIrReadPc(ArmIrContext& c);

CERFENGINE_FORCEINLINE cerfir::Ref ArmIrReadGpr(ArmIrContext& c, uint32_t n) {
    if (n == ArmGpr::kR15) return ArmIrReadPc(c);
    return c.b->ReadSlot(static_cast<uint32_t>(ArmGprDisp(n)));
}

CERFENGINE_FORCEINLINE void ArmIrWriteGpr(ArmIrContext& c, uint32_t n, cerfir::Ref value) {
    c.b->SetSlot(static_cast<uint32_t>(ArmGprDisp(n)), value);
}

CERFENGINE_FORCEINLINE void ArmIrWriteGprConst(ArmIrContext& c, uint32_t n, uint32_t value) {
    c.b->SetSlotConst(static_cast<uint32_t>(ArmGprDisp(n)), value);
}

CERFENGINE_FORCEINLINE void ArmIrCallConst(ArmIrContext& c, const void* fn, cerfir::CallConv conv,
                                           uint32_t num_args, cerfir::CallArg a0,
                                           cerfir::CallArg a1, cerfir::CallArg a2) {
    c.b->CallConst(cerfir::CallSpec{fn, conv, cerfir::Type::kVoid}, num_args, a0, a1, a2, 0u);
}

CERFENGINE_FORCEINLINE void ArmIrMemOrigin(const ArmIrContext& c, cerfir::MemAttr& m) {
    m.guest_pc = c.d->guest_address;
    m.next_pc  = c.d->guest_address + c.d->length;
    m.raw      = c.d->raw;
}

cerfir::Ref ArmIrReadFlag(ArmIrContext& c, ArmIrFlag flag);
void        ArmIrWriteFlag(ArmIrContext& c, ArmIrFlag flag, cerfir::Ref value);
void        ArmIrStoreItState(ArmIrContext& c, uint32_t itstate);

}
