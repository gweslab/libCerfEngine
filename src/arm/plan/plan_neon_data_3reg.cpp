#include "../arm_emit_services.h"
#include "../arm_neon_simd_3same.h"
#include "plan_fns.h"

namespace cerfengine {

ArmCallPlan ArmPlanNeonData3Same(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >> 7)  & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t Q     = (w >> 6)  & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (Q && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    /* VQ{R}DMULH A8.8.372/376: only signed 16/32-bit elements. */
    if ((op == ArmNeonSimd3Same::kS3Qdmulh ||
         op == ArmNeonSimd3Same::kS3Qrdmulh) && (size == 0u || size == 3u)) {
        return ArmUndefinedCallPlan();
    }

    if (size == 3u &&
        (op == ArmNeonSimd3Same::kS3Mul  ||
         op == ArmNeonSimd3Same::kS3Ceq  || op == ArmNeonSimd3Same::kS3Tst  ||
         op == ArmNeonSimd3Same::kS3CgtS || op == ArmNeonSimd3Same::kS3CgtU ||
         op == ArmNeonSimd3Same::kS3CgeS || op == ArmNeonSimd3Same::kS3CgeU ||
         op == ArmNeonSimd3Same::kS3MaxS || op == ArmNeonSimd3Same::kS3MaxU ||
         op == ArmNeonSimd3Same::kS3MinS || op == ArmNeonSimd3Same::kS3MinU ||
         op == ArmNeonSimd3Same::kS3HaddS  || op == ArmNeonSimd3Same::kS3HaddU  ||
         op == ArmNeonSimd3Same::kS3HsubS  || op == ArmNeonSimd3Same::kS3HsubU  ||
         op == ArmNeonSimd3Same::kS3RhaddS || op == ArmNeonSimd3Same::kS3RhaddU ||
         op == ArmNeonSimd3Same::kS3AbdS   || op == ArmNeonSimd3Same::kS3AbdU)) {
        return ArmUndefinedCallPlan();
    }

    /* VMUL.P8 (A8.8.350, op=1): polynomial multiply is defined ONLY at
       8-bit elements; size != 00 is UNDEFINED (A8.8.350). */
    if (op == ArmNeonSimd3Same::kS3MulP && size != 0u) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeonSimd3Same::HandleSimd3SameHelper),
                          {ArmCallPlanPointer(ctx.emit->Simd3Same()), op, d_idx, n_idx, m_idx,
                           esize, regs});
}

}
