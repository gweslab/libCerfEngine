#include "../arm_emit_services.h"
#include "../arm_neon_2regscalar.h"
#include "plan_fns.h"

namespace cerfengine {

/* 2-reg-and-scalar Long form (A8.8.338 T2/A2 VMLAL/VMLSL by scalar).
   D op scalar -> Q; bit[24]=U selects sign/zero extend before mul. */
ArmCallPlan ArmPlanNeonData2RegScalarLong(const DecodedInsn& d, const BlockContext& ctx) {
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

    if (size == 3u) {
        ctx.emit->Owner().Panic("ArmPlanNeonData2RegScalarLong: size=3 word=0x%08X at guest pc=0x%08X",
                                w, d.guest_address);
    }
    /* A8.8.338 (p. A8-934), A8.8.352 (p. A8-962) T2/A2: "if size == '00' || Vd<0> == '1'
       then UNDEFINED". */
    if (size == 0u || (d_idx & 1u) != 0u) return ArmUndefinedCallPlan();

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

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegScalar::HandleScalarMlsMlaLongHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegScalar()), op, d_idx, n_idx, m_idx, esize, index, U});
}

}
