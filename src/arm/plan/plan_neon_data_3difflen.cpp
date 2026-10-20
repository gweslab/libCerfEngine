#include "../arm_emit_services.h"
#include "../arm_neon_3difflen.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-different-length data-proc (A7.4.2). */
ArmCallPlan ArmPlanNeonData3DiffLen(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
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
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (size == 3u) {
        ctx.emit->Owner().Panic("ArmPlanNeonData3DiffLen: size=3 word=0x%08X at guest pc=0x%08X",
                                w, d.guest_address);
    }
    /* A8.8.285 (p. A8-834), A8.8.417 (p. A8-1090): "if Vd<0> == '1' || (op == '1' &&
       Vn<0> == '1') then UNDEFINED". */
    const bool is_wide = (op == ArmNeon3DiffLen::kDlAddWide) ||
                         (op == ArmNeon3DiffLen::kDlSubWide);
    if ((d_idx & 1u) != 0u || (is_wide && (n_idx & 1u) != 0u)) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon3DiffLen::HandleAddSubLWHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon3DiffLen()), op, d_idx, n_idx, m_idx,
                           esize, U});
}

}
