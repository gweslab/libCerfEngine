#include "../arm_emit_services.h"
#include "../arm_neon_scalar_move.h"
#include "plan_fns.h"

namespace cerfengine {

namespace {

uint32_t ExtractLaneIndex(const DecodedInsn& d, uint32_t esize) {
    const uint32_t opc1_lsb = d.cp_opc & 1u;
    const uint32_t opc2     = d.cp & 3u;
    if (esize == 8u) {
        return (opc1_lsb << 2) | opc2;
    }
    return (opc1_lsb << 1) | (opc2 >> 1);
}

}

/* VMOV (ARM core register to scalar) - A8.8.341, L=0. */
ArmCallPlan ArmPlanNeonCoreToScalar(const DecodedInsn& d, const BlockContext& ctx) {
    /* Rt == 15 is UNPREDICTABLE per A8.8.341. */
    if (d.rd == 15u) return ArmUndefinedCallPlan();

    const uint32_t esize      = (d.cp_opc & 2u) ? 8u : 16u;
    const uint32_t lane_index = ExtractLaneIndex(d, esize);
    const uint32_t d_idx      = (((d.cp >> 2) & 1u) << 4) | (d.crn & 0xFu);
    const uint32_t rt_index   = d.rd;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeonScalarMove::HandleCoreToScalarHelper),
        {ArmCallPlanPointer(ctx.emit->NeonScalarMove()), d_idx, lane_index, esize, rt_index});
}

/* VMOV (scalar to ARM core register) - A8.8.342, L=1. cp_opc<2> = U
   (0=signed extend, 1=zero extend) for esize=8/16. */
ArmCallPlan ArmPlanNeonScalarToCore(const DecodedInsn& d, const BlockContext& ctx) {
    /* Rt == 15 is UNPREDICTABLE per A8.8.342. */
    if (d.rd == 15u) return ArmUndefinedCallPlan();

    const uint32_t esize        = (d.cp_opc & 2u) ? 8u : 16u;
    const uint32_t lane_index   = ExtractLaneIndex(d, esize);
    const uint32_t n_idx        = (((d.cp >> 2) & 1u) << 4) | (d.crn & 0xFu);
    const uint32_t rt_index     = d.rd;
    const uint32_t unsigned_ext = (d.cp_opc >> 2) & 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeonScalarMove::HandleScalarToCoreHelper),
        {ArmCallPlanPointer(ctx.emit->NeonScalarMove()), n_idx, lane_index, esize, unsigned_ext,
         rt_index});
}

}
