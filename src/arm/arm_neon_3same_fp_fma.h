#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VFMA / VFMS - A8.8.317 Advanced SIMD T1/A1, opc=1100 C=1 U=0 sz=0. */
class ArmNeon3SameFpFma : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kFma = 0u;
    static constexpr uint32_t kFms = 1u;

    void Handle3SameFpFma(uint32_t op_sel, uint32_t d_idx, uint32_t n_idx,
                          uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL Handle3SameFpFmaHelper(ArmNeon3SameFpFma* svc,
                                                        uint32_t op_sel,
                                                        uint32_t d_idx,
                                                        uint32_t n_idx,
                                                        uint32_t m_idx,
                                                        uint32_t regs);
};

}
