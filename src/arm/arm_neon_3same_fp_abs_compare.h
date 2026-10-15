#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmNeon3SameFpAbsCompare : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kAcge = 0u;
    static constexpr uint32_t kAcgt = 1u;

    void Handle3SameFpAbsCompare(uint32_t op_sel, uint32_t d_idx,
                                 uint32_t n_idx, uint32_t m_idx,
                                 uint32_t regs);

    static void CERFENGINE_CDECL Handle3SameFpAbsCompareHelper(
            ArmNeon3SameFpAbsCompare* svc,
            uint32_t                  op_sel,
            uint32_t                  d_idx,
            uint32_t                  n_idx,
            uint32_t                  m_idx,
            uint32_t                  regs);
};

}
