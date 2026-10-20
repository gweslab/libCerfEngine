#include "../arm_emit_services.h"
#include "../arm_vfp.h"
#include "plan_fns.h"

namespace cerfengine {

ArmCallPlan ArmPlanVfpDataOperation(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t packed =
        ((d.cp_opc & 0xFu) << 19) |
        ((d.crn    & 0xFu) << 15) |
        ((d.crd    & 0xFu) << 11) |
        ((d.cp_num & 0xFu) <<  7) |
        ((d.cp     & 0x7u) <<  4) |
         (d.crm    & 0xFu);
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmVfp::ExecuteCdpHelper),
                          {ArmCallPlanPointer(ctx.emit->Vfp()), d.guest_address, packed}, true);
}

}
