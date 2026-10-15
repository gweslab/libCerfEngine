#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VPADDL / VPADAL (A8.8.364 / A8.8.361). */
class ArmNeon2RegPairwiseAddLong : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kPaddl = 0u;
    static constexpr uint32_t kPadal = 1u;

    void HandlePairwiseAddLong(uint32_t op_sel, uint32_t U, uint32_t esize,
                               uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandlePairwiseAddLongHelper(ArmNeon2RegPairwiseAddLong* svc,
                                                             uint32_t op_sel, uint32_t U,
                                                             uint32_t esize, uint32_t d_idx,
                                                             uint32_t m_idx, uint32_t regs);
};

}
