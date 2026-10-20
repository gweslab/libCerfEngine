#include "../arm_emit_services.h"
#include "../arm_neon_2reg_cvt_int_fp.h"
#include "plan_fns.h"

namespace cerfengine {

/* VCVT int↔fp Advanced SIMD (A8.8.305) - A7.4.5 A=11, bits[10:9]=11,
   bits[8:7]=op. Also handles immediate-scaled VCVT (A8.8.307). */
ArmCallPlan ArmPlanNeonData2RegCvtIntFp(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;
    const bool fixed = ((w >> 8) & 0xEu) == 0xEu;
    const uint32_t imm6 = (w >> 16) & 63u;
    const uint32_t frac_bits = fixed ? 64u - imm6 : 0u;

    if (fixed && (w & 0x80u)) {
        ctx.emit->Owner().Panic("ArmPlanNeonData2RegCvtIntFp: L=1 word=0x%08X at guest pc=0x%08X",
                                w, d.guest_address);
    }
    /* A8.8.305 (p. A8-868): "if size != '10' then UNDEFINED"; A8.8.307 (p. A8-872):
       "if imm6 IN '0xxxxx' then UNDEFINED". */
    if ((!fixed && size != 2u) || (fixed && (imm6 & 0x20u) == 0u)) return ArmUndefinedCallPlan();
    /* A8.8.305, A8.8.307. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegCvtIntFp::HandleCvtIntFpHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegCvtIntFp()), op, d_idx, m_idx, regs,
                           frac_bits});
}

}
