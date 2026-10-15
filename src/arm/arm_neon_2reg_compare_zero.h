#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VCEQ / VCGT / VCGE / VCLE / VCLT immediate #0
   (A8.8.292 / A8.8.296 / A8.8.294 / A8.8.298 / A8.8.301). */
class ArmNeon2RegCompareZero : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kCgt = 0u;
    static constexpr uint32_t kCge = 1u;
    static constexpr uint32_t kCeq = 2u;
    static constexpr uint32_t kCle = 3u;
    static constexpr uint32_t kClt = 4u;

    void HandleCompareZero(uint32_t op_sel, uint32_t F, uint32_t esize,
                           uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleCompareZeroHelper(ArmNeon2RegCompareZero* svc,
                                                         uint32_t op_sel, uint32_t F,
                                                         uint32_t esize, uint32_t d_idx,
                                                         uint32_t m_idx, uint32_t regs);
};

}
