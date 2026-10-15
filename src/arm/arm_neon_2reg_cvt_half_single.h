#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VCVT half↔single Advanced SIMD (A8.8.310). */
class ArmNeon2RegCvtHalfSingle : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kSingleToHalf = 0u;
    static constexpr uint32_t kHalfToSingle = 1u;

    void HandleCvtHalfSingle(uint32_t op_sel, uint32_t d_idx, uint32_t m_idx);

    static void CERFENGINE_CDECL HandleCvtHalfSingleHelper(ArmNeon2RegCvtHalfSingle* svc,
                                                           uint32_t op_sel,
                                                           uint32_t d_idx,
                                                           uint32_t m_idx);
};

}
