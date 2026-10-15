#include "arm_neon_vtbl.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeonVtbl);

void ArmNeonVtbl::HandleVtbl(uint32_t op_sel, uint32_t d_idx, uint32_t n_idx,
                             uint32_t m_idx, uint32_t length) {
    auto* state = engine_.Get<ArmCpu>().State();

    uint8_t table[32];
    for (uint32_t i = 0; i < length; ++i) {
        std::memcpy(table + i * 8u, &state->vfp_d[n_idx + i], 8);
    }

    uint8_t m_bytes[8];
    std::memcpy(m_bytes, &state->vfp_d[m_idx], 8);

    uint8_t res[8];
    std::memcpy(res, &state->vfp_d[d_idx], 8);

    const uint32_t limit = 8u * length;
    for (uint32_t i = 0; i < 8u; ++i) {
        const uint32_t index = m_bytes[i];
        if (index < limit) {
            res[i] = table[index];
        } else if (op_sel == kTbl) {
            res[i] = 0u;
        }
    }
    std::memcpy(&state->vfp_d[d_idx], res, 8);
}

void CERFENGINE_CDECL ArmNeonVtbl::HandleVtblHelper(ArmNeonVtbl* svc, uint32_t op_sel,
                                                    uint32_t d_idx, uint32_t n_idx,
                                                    uint32_t m_idx, uint32_t length) {
    svc->HandleVtbl(op_sel, d_idx, n_idx, m_idx, length);
}

}
