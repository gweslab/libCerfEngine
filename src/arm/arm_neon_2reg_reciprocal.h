#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VRECPE / VRSQRTE (A8.8.384 / A8.8.391). */
class ArmNeon2RegReciprocal : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kRecpe  = 0u;
    static constexpr uint32_t kRsqrte = 1u;

    void HandleReciprocal(uint32_t op_sel, uint32_t F,
                          uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleReciprocalHelper(ArmNeon2RegReciprocal* svc,
                                                        uint32_t op_sel, uint32_t F,
                                                        uint32_t d_idx, uint32_t m_idx,
                                                        uint32_t regs);
};

}
