#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2regscalar.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* 2-reg-and-scalar Long form (A8.8.338 T2/A2 VMLAL/VMLSL by scalar).
   D op scalar -> Q; bit[24]=U selects sign/zero extend before mul. */
uint8_t* PlaceNeonData2RegScalarLong(uint8_t*      cursor,
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
    const uint32_t U     = (w >> 24) & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;

    if (size == 3u) {
        emit->Owner().Panic("PlaceNeonData2RegScalarLong: size=3 word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }
    /* A8.8.338 (p. A8-934), A8.8.352 (p. A8-962) T2/A2: "if size == '00' || Vd<0> == '1'
       then UNDEFINED". */
    if (size == 0u || (d_idx & 1u) != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* A8.8.338 (p. A8-934): "m = UInt(Vm<2:0>); index = UInt(M:Vm<3>)" for esize 16,
       "m = UInt(Vm); index = UInt(M)" for esize 32. */
    uint32_t esize, m_idx, index;
    if (size == 1u) {
        esize = 16u;
        m_idx = Vm & 0x7u;
        index = (Mbit << 1) | ((Vm >> 3) & 1u);
    } else {
        esize = 32u;
        m_idx = Vm;
        index = Mbit;
    }

    EmitPush32(cursor, U);
    EmitPush32(cursor, index);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, n_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegScalar())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegScalar::HandleScalarMlsMlaLongHelper));
    EmitAddRegImm32(cursor, kEsp, 32);
    return cursor;
}

}
