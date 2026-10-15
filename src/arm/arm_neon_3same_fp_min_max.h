#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VMAX.F32 / VMIN.F32 - A7.4.1 opc=1111 B=0 U=0 (A8.8.335). */
class ArmNeon3SameFpMinMax : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kMax = 0u;
    static constexpr uint32_t kMin = 1u;

    void Handle3SameFpMinMax(uint32_t op_sel, uint32_t d_idx, uint32_t n_idx,
                             uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL Handle3SameFpMinMaxHelper(ArmNeon3SameFpMinMax* svc,
                                                           uint32_t op_sel,
                                                           uint32_t d_idx,
                                                           uint32_t n_idx,
                                                           uint32_t m_idx,
                                                           uint32_t regs);
};

}
