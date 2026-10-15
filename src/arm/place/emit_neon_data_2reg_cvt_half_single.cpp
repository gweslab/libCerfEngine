#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_cvt_half_single.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VCVT half↔single Advanced SIMD (A8.8.310) - A7.4.5 A=10, bits[10:7]=11x0
   with bit[6]=0. bit[8]=op selects direction (0=single→half, 1=half→single). */
uint8_t* PlaceNeonData2RegCvtHalfSingle(uint8_t*      cursor,
                                        DecodedInsn*  d,
                                        BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t op    = d->op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* ARM DDI 0406C.c B4.1.109 (p. B4-1658): MVFR1 "A_SIMD HPFP, bits[23:20]
       Indicates whether the Advanced SIMD Extension implements half-precision
       floating-point conversion instructions. 0b0000 Not implemented." */
    if (((emit->ProcessorConfig()->mvfr1 >> 20) & 0xFu) == 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    /* ARM DDI 0406C.c A8.8.310 (p. A8-878): "if size != '01' then UNDEFINED;
       if half_to_single && Vd<0> == '1' then UNDEFINED; if !half_to_single &&
       Vm<0> == '1' then UNDEFINED". */
    if (size != 1u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (op == ArmNeon2RegCvtHalfSingle::kHalfToSingle && (d_idx & 1u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (op == ArmNeon2RegCvtHalfSingle::kSingleToHalf && (m_idx & 1u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegCvtHalfSingle())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegCvtHalfSingle::HandleCvtHalfSingleHelper));
    EmitAddRegImm32(cursor, kEsp, 16);
    return cursor;
}

}
