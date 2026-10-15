#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VQABS / VQNEG (A8.8.369 / A8.8.375). */
class ArmNeon2RegSatAbsNeg : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kQabs = 0u;
    static constexpr uint32_t kQneg = 1u;

    void HandleSatAbsNeg(uint32_t op_sel, uint32_t esize,
                         uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleSatAbsNegHelper(ArmNeon2RegSatAbsNeg* svc,
                                                       uint32_t op_sel, uint32_t esize,
                                                       uint32_t d_idx, uint32_t m_idx,
                                                       uint32_t regs);
};

}
