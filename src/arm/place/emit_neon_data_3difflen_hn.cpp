#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_3difflen.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* 3-reg-different-length "narrow returning high half" family
   (A8.8.284/416/383/394). Inputs are two Q-regs; output is one D-reg. */
uint8_t* PlaceNeonData3DiffLenHN(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t op    = d->op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >> 7)  & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (size == 3u) {
        emit->Owner().Panic("PlaceNeonData3DiffLenHN: size=3 word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }
    if ((n_idx & 1u) != 0u || (m_idx & 1u) != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t esize = 8u << size;

    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, n_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon3DiffLen())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon3DiffLen::HandleAddSubHNHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
