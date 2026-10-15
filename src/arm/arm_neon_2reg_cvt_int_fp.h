#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VCVT int↔fp Advanced SIMD (A8.8.305): op field at bits[8:7],
   op[1]=to_integer, op[0]=unsigned. */
class ArmNeon2RegCvtIntFp : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kIntSToFp = 0u;
    static constexpr uint32_t kIntUToFp = 1u;
    static constexpr uint32_t kFpToIntS = 2u;
    static constexpr uint32_t kFpToIntU = 3u;

    void HandleCvtIntFp(uint32_t op_sel, uint32_t d_idx, uint32_t m_idx,
                        uint32_t regs, uint32_t frac_bits = 0u);

    static void CERFENGINE_CDECL HandleCvtIntFpHelper(ArmNeon2RegCvtIntFp* svc,
                                                      uint32_t op_sel, uint32_t d_idx,
                                                      uint32_t m_idx, uint32_t regs,
                                                      uint32_t frac_bits);
};

}
