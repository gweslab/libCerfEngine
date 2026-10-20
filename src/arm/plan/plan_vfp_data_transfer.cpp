#include <cerfengine/arm/block_context.h>

#include "../arm_emit_services.h"
#include "../arm_vfp_memory.h"
#include "plan_fns.h"

namespace cerfengine {

namespace {

uint32_t VfpRegisterIndex(const DecodedInsn& d) {
    return d.cp_num == 11u ? ((d.n << 4) | (d.crd & 0xFu)) : ((d.crd << 1) | (d.n & 0x1u));
}

ArmCallPlan SingleTransfer(const DecodedInsn& d, const BlockContext& ctx) {
    /* A8.8.413 VSTR (p. A8-1082) closes both T1/A1 and T2/A2 with "if n == 15
       && CurrentInstrSet() != InstrSet_ARM then UNPREDICTABLE"; A8.8.333 VLDR
       (p. A8-924) carries no such clause. */
    if (d.rn == 15u && d.l == 0u && ctx.thumb) return ArmUndefinedCallPlan();

    uint32_t flags = 0;
    if (d.l)              flags |= ArmVfpMemory::kFlagL;
    if (d.cp_num == 11u)  flags |= ArmVfpMemory::kFlagDp;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmVfpMemory::HandleSingleTransferHelper),
        {ArmCallPlanPointer(ctx.emit->VfpMem()), d.guest_address, ArmPcReadValue(&d, &ctx), d.rn,
         VfpRegisterIndex(d), static_cast<uint32_t>(d.offset), flags},
        true);
}

ArmCallPlan BlockTransfer(const DecodedInsn& d, const BlockContext& ctx) {
    /* DDI 0406C.c A8.8.332 VLDM (p. A8-922) and A8.8.412 VSTM (p. A8-1080)
       close every encoding with "if n == 15 && (wback || CurrentInstrSet() !=
       InstrSet_ARM) then UNPREDICTABLE". */
    if (d.rn == 15u && (ctx.thumb || d.w)) return ArmUndefinedCallPlan();

    const int32_t  signed_off = d.offset;
    const uint32_t abs_off    = static_cast<uint32_t>(signed_off < 0 ? -signed_off : signed_off);
    const uint32_t imm8       = (abs_off >> 2) & 0xFFu;

    uint32_t flags = 0;
    if (d.l)             flags |= ArmVfpMemory::kFlagL;
    if (d.w)             flags |= ArmVfpMemory::kFlagW;
    if (d.p)             flags |= ArmVfpMemory::kFlagP;
    if (d.cp_num == 11u) flags |= ArmVfpMemory::kFlagDp;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmVfpMemory::HandleBlockTransferHelper),
        {ArmCallPlanPointer(ctx.emit->VfpMem()), d.guest_address, ArmPcReadValue(&d, &ctx), d.rn,
         VfpRegisterIndex(d), imm8, flags},
        true);
}

}

ArmCallPlan ArmPlanVfpDataTransfer(const DecodedInsn& d, const BlockContext& ctx) {
    /* DDI 0406C.c A8.8.332 VLDM (p. A8-922) and A8.8.412 VSTM (p. A8-1080)
       carry "if P == U && W == '1' then UNDEFINED" in every encoding. */
    if (d.p == d.u && d.w == 1u) return ArmUndefinedCallPlan();
    if (d.p == 1u && d.w == 0u) return SingleTransfer(d, ctx);
    return BlockTransfer(d, ctx);
}

}
