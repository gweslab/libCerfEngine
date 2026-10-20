#include <cstring>

#include <cerfengine/calling_convention.h>
#include <cerfengine/arm/arm_cpu.h>

#include "../arm_emit_services.h"
#include "plan_fns.h"

namespace cerfengine {

namespace {

/* ARM DDI 0406C.c A8.8.313, VDUP (scalar). */
void CERFENGINE_CDECL DuplicateScalar(ArmCpuState* state, uint32_t word) {
    const uint32_t imm4 = (word >> 16) & 15u;
    const uint32_t bytes = (imm4 & 1u) ? 1u : (imm4 & 2u) ? 2u : 4u;
    const uint32_t index = imm4 / (2u * bytes);
    const uint32_t src = (word & 15u) | ((word >> 1) & 16u);
    const uint32_t dst = ((word >> 12) & 15u) | ((word >> 18) & 16u);
    const uint32_t regs = ((word >> 6) & 1u) + 1u;
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(&state->vfp_d[src]) + index * bytes, bytes);
    auto* output = reinterpret_cast<uint8_t*>(&state->vfp_d[dst]);
    for (uint32_t offset = 0; offset < regs * 8u; offset += bytes)
        std::memcpy(output + offset, &value, bytes);
}

}

ArmCallPlan ArmPlanNeonVdupScalar(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t word = d.immediate;
    if (((word >> 16) & 7u) == 0u ||
        (((word >> 6) & 1u) && ((word >> 12) & 1u)))
        return ArmUndefinedCallPlan();
    return ArmPlannedCall(reinterpret_cast<const void*>(&DuplicateScalar),
                          {ArmCallPlanPointer(ctx.emit->Cpu()->State()), word});
}

}
