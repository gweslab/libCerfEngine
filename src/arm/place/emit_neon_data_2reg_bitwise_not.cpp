#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_bitwise_not.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VMVN (register) - A8.8.354. */
uint8_t* PlaceNeonData2RegBitwiseNot(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* size != 00 UND per A8.8.354. */
    if (size != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegBitwiseNot())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegBitwiseNot::HandleMvnHelper));
    EmitAddRegImm32(cursor, kEsp, 16);
    return cursor;
}

}
